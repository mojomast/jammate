#pragma once

#include <string>
#include <vector>

//==============================================================================
// Tiny home-made test harness (no Catch2/GoogleTest: the repo builds offline
// from submodules, so no FetchContent).
//
//   TEST_CASE (spec_something) { CHECK (a == b); }
//
// CHECK   records a failure and keeps going.
// REQUIRE records a failure and aborts the current case (use when continuing
//         would crash, e.g. after a null/size check).
// INFO_MSG only prints - it never fails the run (used for known-issue reports).
namespace th
{

struct Case { const char* name; void (*fn)(); };

std::vector<Case>& registry();
void fail (const char* file, int line, const char* expr, const std::string& note);
void info (const std::string& msg);

struct Registrar { Registrar (const char* n, void (*f)()) { registry().push_back ({ n, f }); } };
struct Abort {};

} // namespace th

#define TEST_CASE(name)                                                          \
    static void name();                                                          \
    static const th::Registrar th_registrar_##name { #name, &name };             \
    static void name()

#define CHECK(expr)          do { if (! (expr)) th::fail (__FILE__, __LINE__, #expr, {}); } while (false)
#define CHECK_MSG(expr, note) do { if (! (expr)) th::fail (__FILE__, __LINE__, #expr, (note)); } while (false)
#define REQUIRE(expr)        do { if (! (expr)) { th::fail (__FILE__, __LINE__, #expr, "required"); throw th::Abort {}; } } while (false)
#define INFO_MSG(note)       th::info (note)
