#include "TestHarness.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace th
{

std::vector<Case>& registry()
{
    static std::vector<Case> cases;   // built by the static Registrars
    return cases;
}

static int caseFailures = 0;
static int totalFailures = 0;

void fail (const char* file, int line, const char* expr, const std::string& note)
{
    ++caseFailures;
    ++totalFailures;
    std::printf ("      FAIL %s:%d  %s%s%s\n", file, line, expr,
                 note.empty() ? "" : "  -- ", note.c_str());
}

void info (const std::string& msg)
{
    std::printf ("      info: %s\n", msg.c_str());
}

} // namespace th

//==============================================================================
// Usage: PedalForgeTests [name-filter]
// The filter is a plain substring of the test case name ("spec_", "library_").
// Exit code: 0 = everything passed, 1 = at least one CHECK failed or the
// filter matched no test at all (a renamed case must not silently pass).
int main (int argc, char** argv)
{
    const char* filter = (argc > 1 ? argv[1] : nullptr);
    const auto t0 = std::chrono::steady_clock::now();
    int ran = 0, failedCases = 0;

    for (const auto& c : th::registry())
    {
        if (filter != nullptr && std::strstr (c.name, filter) == nullptr)
            continue;

        ++ran;
        th::caseFailures = 0;
        std::printf ("  [ run  ] %s\n", c.name);

        try                       { c.fn(); }
        catch (const th::Abort&)  { /* REQUIRE already logged it */ }
        catch (const std::exception& e) { th::fail (__FILE__, __LINE__, "unexpected std::exception", e.what()); }
        catch (...)               { th::fail (__FILE__, __LINE__, "unexpected exception", {}); }

        if (th::caseFailures > 0)
            ++failedCases;

        std::printf ("  [ %s ] %s\n", th::caseFailures == 0 ? " ok  " : "FAILED", c.name);
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        std::chrono::steady_clock::now() - t0).count();

    std::printf ("\n%d case(s) run in %lld ms, %d failed case(s), %d failed check(s)\n",
                 ran, (long long) ms, failedCases, th::totalFailures);

    if (ran == 0)
    {
        std::printf ("ERROR: the filter '%s' matched no test case\n", filter != nullptr ? filter : "");
        return 1;
    }

    return th::totalFailures == 0 ? 0 : 1;
}
