// EVAL-LIVE-001 — instrumentation self-check executable.
//
// This translation unit links ONLY the copied RT probe instrumentation
// (`RtProbeInstrumentation.*`). It deliberately does not construct the product
// processor, so it builds and runs even on the frozen contract base where the
// live-session implementation is not merged yet. Its job is to prove the
// allocation/lock gate and every detector before any processor replay is
// trusted, and to make the "harness ready" state a real, executed artifact
// rather than a claim.
//
// The check is a superset of rtprobe::runSelfCheck: it additionally proves that
// the authoritative allocation totals fold the nothrow/aligned `new` forms that
// have no dedicated plain column (the RT-005 accounting lesson).
#include "RtProbeInstrumentation.h"

#include <cstdint>
#include <cstdio>
#include <new>

namespace
{
int failures = 0;

void check (bool ok, const char* what, std::FILE* out)
{
    if (! ok)
    {
        ++failures;
        std::fprintf (out, "  SELFCHECK FAIL: %s\n", what);
    }
}

std::uint64_t calls (const rtprobe::Snapshot& s, rtprobe::Kind k)
{
    return s.allocCalls[(std::size_t) k];
}

std::uint64_t allocCxx (const rtprobe::Snapshot& s)
{
    return calls (s, rtprobe::Kind::cxxNew)
         + calls (s, rtprobe::Kind::cxxNewArray)
         + calls (s, rtprobe::Kind::cxxNewNothrow)
         + calls (s, rtprobe::Kind::cxxNewAligned);
}

std::uint64_t allocC (const rtprobe::Snapshot& s)
{
    return calls (s, rtprobe::Kind::cMalloc)
         + calls (s, rtprobe::Kind::cCalloc)
         + calls (s, rtprobe::Kind::cRealloc);
}
} // namespace

int main (int, char**)
{
    std::FILE* out = stdout;
    std::fprintf (out, "EVAL-LIVE-001 instrumentation self-check\n");
    std::fprintf (out, "========================================\n");

    if (rtprobe::runSelfCheck (out) != 0)
        ++failures;

    // Extra: the authoritative totals must fold nothrow/aligned (no plain
    // column) so a zero test based on them cannot miss an allocation. These go
    // through the globally replaced operators installed by the instrumentation
    // TU; they do not modify the copied instrumentation source.
    rtprobe::resetAll();
    rtprobe::arm();
    {
        void* const p1 = ::operator new (64, std::nothrow);
        void* const p2 = ::operator new (64, std::align_val_t (64));
        volatile void* keep1 = p1;
        volatile void* keep2 = p2;
        (void) keep1;
        (void) keep2;
        ::operator delete (p1);
        ::operator delete (p2, std::align_val_t (64));
    }
    rtprobe::disarm();
    {
        const auto s = rtprobe::snapshot();
        check (calls (s, rtprobe::Kind::cxxNewNothrow) == 1,
               "nothrow new not counted", out);
        check (calls (s, rtprobe::Kind::cxxNewAligned) == 1,
               "aligned new not counted", out);
        check (allocCxx (s) == 2, "authoritative C++ total did not fold forms", out);
        check (allocC (s) == 0, "C total nonzero for a C++-only region", out);
        std::fprintf (out,
                      "  authoritative: cxx_total=%llu c_total=%llu (new(nothrow)+new(aligned) folded)\n",
                      (unsigned long long) allocCxx (s),
                      (unsigned long long) allocC (s));
    }

    if (failures == 0)
    {
        std::fprintf (out, "\nINSTRUMENT SELF-CHECK PASS\n");
        return 0;
    }

    std::fprintf (out, "\nINSTRUMENT SELF-CHECK FAIL (%d)\n", failures);
    return 2;
}
