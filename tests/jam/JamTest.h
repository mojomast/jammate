// Minimal, dependency-free test harness for the platform-neutral jam-core.
//
// Why this exists instead of JUCE's TestHarness: the jam modules under
// src/jam are deliberately free of JUCE so that they can be compiled and
// exercised deterministically on any platform, including CI machines without
// audio hardware. SPEC.md 21.1 requires unit tests for queues, clock, director,
// style parsing, transport and persistence; those must not require an audio
// device to run.
//
// Design rules for this harness:
//   - A test that cannot fail is worthless. CHECK/REQUIRE are real asserts.
//   - Each suite is registered by a ctest name filter, and the runner FAILS if a
//     filter matches zero tests, so a renamed or accidentally deleted suite can
//     never report success silently. (Same policy as tests/CMakeLists.txt.)
//   - No sleeping, no wall-clock dependence, no random seeding outside the
//     deterministic generator the caller supplies.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace jamtest
{

struct TestCase
{
    const char* suite;
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar
{
    Registrar (const char* suite, const char* name, void (*fn)())
    {
        registry().push_back (TestCase { suite, name, fn });
    }
};

struct State
{
    int checks = 0;
    int failures = 0;
    std::string currentTest;
};

State& state();

void fail (const char* file, int line, const std::string& message);

std::string describe (double v);
std::string describe (long long v);
std::string describe (bool v);
std::string describe (const std::string& v);

/** Narrow-conversion catch-alls.

    CHECK_EQ compares with `auto`, so the exact type of the expression is
    whatever the test happened to write. Without these overloads a
    `CHECK_EQ (ring.capacity(), 4)` fails to compile for the *wrong* reason
    (ambiguous overload between double / long long / bool / std::string) and a
    worker is tempted to work around the harness instead of the real bug. Any
    integral type therefore renders as an integer, and C strings render quoted. */
template <typename T,
          typename = std::enable_if_t<std::is_integral_v<T>
                                      && ! std::is_same_v<std::remove_cv_t<T>, bool>>>
std::string describe (T v)
{
    return std::to_string (static_cast<long long> (v));
}

inline std::string describe (const char* v)
{
    return v != nullptr ? "\"" + std::string (v) + "\"" : std::string ("(null)");
}

/** Runs every registered test whose "suite.name" contains `filter`.
    Returns a process exit code: 0 only if at least one test ran and all
    checks passed. */
int runAll (const char* filter);

} // namespace jamtest

// ---------------------------------------------------------------------------

#define JAM_TEST_CAT2(a, b) a##b
#define JAM_TEST_CAT(a, b) JAM_TEST_CAT2 (a, b)

/** Declares a test case. Use: JAM_TEST(clock, perfect120) { ... } */
#define JAM_TEST(suite_name, test_name)                                          \
    static void jam_test_##suite_name##_##test_name();                          \
    static ::jamtest::Registrar jam_reg_##suite_name##_##test_name (             \
        #suite_name, #test_name, &jam_test_##suite_name##_##test_name);         \
    static void jam_test_##suite_name##_##test_name()

/** Records a boolean check. Non-fatal: execution continues. */
#define CHECK(cond)                                                             \
    do {                                                                        \
        ++::jamtest::state().checks;                                            \
        if (! (cond))                                                           \
            ::jamtest::fail (__FILE__, __LINE__, "CHECK failed: " #cond);       \
    } while (false)

/** Records a comparison with an explicit expected/reported rendering. */
#define CHECK_EQ(actual, expected)                                              \
    do {                                                                        \
        ++::jamtest::state().checks;                                            \
        auto jam_a_ = (actual);                                                 \
        auto jam_e_ = (expected);                                               \
        if (! (jam_a_ == jam_e_))                                               \
            ::jamtest::fail (__FILE__, __LINE__,                                 \
                             std::string ("CHECK_EQ failed: " #actual " == " #expected \
                                          "\n    actual:   ")                   \
                                 + ::jamtest::describe (jam_a_)                 \
                                 + "\n    expected: "                           \
                                 + ::jamtest::describe (jam_e_));               \
    } while (false)

#define CHECK_NEAR(actual, expected, tol)                                       \
    do {                                                                        \
        ++::jamtest::state().checks;                                            \
        const double jam_a_ = static_cast<double> (actual);                     \
        const double jam_e_ = static_cast<double> (expected);                   \
        const double jam_t_ = static_cast<double> (tol);                        \
        if (! (std::fabs (jam_a_ - jam_e_) <= jam_t_))                          \
            ::jamtest::fail (__FILE__, __LINE__,                                 \
                             std::string ("CHECK_NEAR failed: " #actual " ~= " #expected \
                                          "\n    actual:   ")                   \
                                 + ::jamtest::describe (jam_a_)                 \
                                 + "\n    expected: "                           \
                                 + ::jamtest::describe (jam_e_)                 \
                                 + "\n    tolerance: "                          \
                                 + ::jamtest::describe (jam_t_));               \
    } while (false)

#define CHECK_LE(actual, bound)                                                 \
    do {                                                                        \
        ++::jamtest::state().checks;                                            \
        const double jam_a_ = static_cast<double> (actual);                     \
        const double jam_b_ = static_cast<double> (bound);                      \
        if (! (jam_a_ <= jam_b_))                                               \
            ::jamtest::fail (__FILE__, __LINE__,                                 \
                             std::string ("CHECK_LE failed: " #actual " <= " #bound \
                                          "\n    actual:   ")                   \
                                 + ::jamtest::describe (jam_a_)                 \
                                 + "\n    bound:    "                           \
                                 + ::jamtest::describe (jam_b_));               \
    } while (false)

#define CHECK_GE(actual, bound)                                                 \
    do {                                                                        \
        ++::jamtest::state().checks;                                            \
        const double jam_a_ = static_cast<double> (actual);                     \
        const double jam_b_ = static_cast<double> (bound);                      \
        if (! (jam_a_ >= jam_b_))                                               \
            ::jamtest::fail (__FILE__, __LINE__,                                 \
                             std::string ("CHECK_GE failed: " #actual " >= " #bound \
                                          "\n    actual:   ")                   \
                                 + ::jamtest::describe (jam_a_)                 \
                                 + "\n    bound:    "                           \
                                 + ::jamtest::describe (jam_b_));               \
    } while (false)

/** Fatal check: aborts the current test case, not the whole run. */
#define REQUIRE(cond)                                                           \
    do {                                                                        \
        ++::jamtest::state().checks;                                            \
        if (! (cond))                                                           \
        {                                                                       \
            ::jamtest::fail (__FILE__, __LINE__, "REQUIRE failed: " #cond);     \
            return;                                                             \
        }                                                                       \
    } while (false)
