#include "JamTest.h"

#include <algorithm>
#include <cstdlib>

namespace jamtest
{

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

State& state()
{
    static State s;
    return s;
}

void fail (const char* file, int line, const std::string& message)
{
    ++state().failures;
    std::printf ("    FAIL %s:%d\n      %s\n", file, line, message.c_str());
}

std::string describe (double v)
{
    char buf[64];
    std::snprintf (buf, sizeof buf, "%.6f", v);
    return std::string (buf);
}

std::string describe (long long v)
{
    char buf[32];
    std::snprintf (buf, sizeof buf, "%lld", v);
    return std::string (buf);
}

std::string describe (bool v)
{
    return v ? "true" : "false";
}

std::string describe (const std::string& v)
{
    return "\"" + v + "\"";
}

int runAll (const char* filter)
{
    auto& cases = registry();
    std::stable_sort (cases.begin(), cases.end(),
                      [] (const TestCase& a, const TestCase& b) {
                          return std::strcmp (a.suite, b.suite) < 0;
                      });

    const std::string f = filter != nullptr ? filter : "";

    int matched = 0;
    int failedTests = 0;
    std::string currentSuite;

    for (const TestCase& c : cases)
    {
        const std::string full = std::string (c.suite) + "." + c.name;
        if (! f.empty() && full.find (f) == std::string::npos)
            continue;

        if (currentSuite != c.suite)
        {
            currentSuite = c.suite;
            std::printf ("[suite] %s\n", currentSuite.c_str());
        }

        const int before = state().failures;
        state().currentTest = full;
        c.fn();

        const bool ok = state().failures == before;
        ++matched;
        if (! ok)
            ++failedTests;
        std::printf ("  %s %s\n", ok ? "PASS" : "FAIL", full.c_str());
    }

    if (matched == 0)
    {
        // Deliberate: a filter that matches nothing is a hard failure. A renamed
        // or deleted suite must never look like a green run.
        std::printf ("ERROR: filter \"%s\" matched zero tests\n", f.c_str());
        return 2;
    }

    std::printf ("\n%d tests, %d checks, %d failed check(s) in %d test(s)\n",
                 matched, state().checks, state().failures, failedTests);

    return failedTests == 0 ? 0 : 1;
}

} // namespace jamtest

int main (int argc, char** argv)
{
    return jamtest::runAll (argc > 1 ? argv[1] : "");
}