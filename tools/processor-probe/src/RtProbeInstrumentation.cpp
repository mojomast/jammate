// RT-002 local runtime probe instrumentation implementation.
//
// Global replacement of the C++ new/delete families and, via the linker's
// --wrap mechanism, of the C malloc family and the pthread mutex entry points.
// Every hook is inert unless the measuring code has armed the current thread,
// so construction/preparation/lifecycle traffic is never attributed to the
// callback. Nothing here allocates, and no hook runs after main unless armed.
//
// The replacement operators funnel through __real_malloc/__real_free, so a C++
// new is never double-counted as a C malloc.
#include "RtProbeInstrumentation.h"

#include <new>
#include <pthread.h>
#include <time.h>

#include <link.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// The linker (--wrap) supplies these aliases to the real symbols.
extern "C"
{
void* __real_malloc (std::size_t);
void* __real_calloc (std::size_t, std::size_t);
void* __real_realloc (void*, std::size_t);
void  __real_free (void*);

int __real_pthread_mutex_lock (pthread_mutex_t*);
int __real_pthread_mutex_trylock (pthread_mutex_t*);
int __real_pthread_mutex_unlock (pthread_mutex_t*);
int __real_pthread_cond_clockwait (pthread_cond_t*, pthread_mutex_t*, clockid_t,
                                   const struct timespec*);
}

namespace
{
// Main-module load bias, resolved once before measuring so records can carry
// file-relative caller offsets instead of ASLR-dependent addresses.
std::atomic<std::uintptr_t> gMainBase { 0 };
std::atomic<bool>           gMainBaseKnown { false };

int findMainBase (struct dl_phdr_info* info, std::size_t, void*)
{
    if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0')
    {
        gMainBase.store ((std::uintptr_t) info->dlpi_addr, std::memory_order_relaxed);
        gMainBaseKnown.store (true, std::memory_order_relaxed);
        return 1;
    }
    return 0;
}

void* callerOffset (void* caller) noexcept
{
    const std::uintptr_t base = gMainBaseKnown.load (std::memory_order_relaxed)
                                    ? gMainBase.load (std::memory_order_relaxed) : 0;
    const std::uintptr_t addr = (std::uintptr_t) caller;
    return (void*) (addr >= base ? addr - base : addr);
}
} // namespace

namespace rtprobe
{

std::atomic<std::uint64_t> gAllocCalls[kKindCount];
std::atomic<std::uint64_t> gAllocBytes[kKindCount];
std::atomic<std::uint64_t> gAllocRecordCount { 0 };
std::atomic<std::uint64_t> gAllocRecordOverflow { 0 };
AllocRecord                gAllocRecords[kMaxAllocRecords];

std::atomic<std::uint64_t> gLockCalls { 0 };
std::atomic<std::uint64_t> gTrylockCalls { 0 };
std::atomic<std::uint64_t> gUnlockCalls { 0 };
std::atomic<std::uint64_t> gCondWaitCalls { 0 };
std::atomic<std::uint64_t> gBlockedLockCalls { 0 };
std::atomic<std::uint64_t> gLockedNanos { 0 };
std::atomic<std::uint64_t> gMaxLockNs { 0 };
std::atomic<std::uint64_t> gLockRecordCount { 0 };
std::atomic<std::uint64_t> gLockRecordOverflow { 0 };
std::atomic<std::uint64_t> gNoopFreeCalls { 0 };
std::atomic<void*>         gNoopFreeCallers[kMaxNoopFreeSites];
std::atomic<std::uint64_t> gNoopFreeCallerCount { 0 };
LockRecord                 gLockRecords[kMaxLockRecords];

thread_local bool tArmed = false;

void initInstrumentation() noexcept
{
    if (! gMainBaseKnown.load (std::memory_order_relaxed))
        dl_iterate_phdr (findMainBase, nullptr);
}

void arm() noexcept    { tArmed = true; }
void disarm() noexcept { tArmed = false; }

const char* kindName (Kind k) noexcept
{
    switch (k)
    {
        case Kind::cxxNew:          return "cxx:new";
        case Kind::cxxNewArray:     return "cxx:new[]";
        case Kind::cxxNewNothrow:   return "cxx:new(nothrow)";
        case Kind::cxxNewAligned:   return "cxx:new(aligned)";
        case Kind::cxxDelete:       return "cxx:delete";
        case Kind::cxxDeleteArray:  return "cxx:delete[]";
        case Kind::cxxDeleteSized:  return "cxx:delete(sized)";
        case Kind::cxxDeleteAligned:return "cxx:delete(aligned)";
        case Kind::cMalloc:         return "c:malloc";
        case Kind::cCalloc:         return "c:calloc";
        case Kind::cRealloc:        return "c:realloc";
        case Kind::cFree:           return "c:free";
        case Kind::count:           break;
    }
    return "?";
}

std::uint64_t monotonicNs() noexcept
{
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (std::uint64_t) ts.tv_sec * 1000000000ull + (std::uint64_t) ts.tv_nsec;
}

void recordAlloc (Kind k, std::size_t bytes, void* ptr, void* caller) noexcept
{
    const auto ki = (std::size_t) k;
    const auto seq = gAllocCalls[ki].fetch_add (1, std::memory_order_relaxed);
    gAllocBytes[ki].fetch_add (bytes, std::memory_order_relaxed);

    const auto r = gAllocRecordCount.fetch_add (1, std::memory_order_relaxed);
    if (r < kMaxAllocRecords)
    {
        AllocRecord& rec = gAllocRecords[r];
        rec.seq    = seq;
        rec.kind   = k;
        rec.bytes  = bytes;
        rec.ptr    = ptr;
        rec.caller = callerOffset (caller);
    }
    else
    {
        gAllocRecordOverflow.fetch_add (1, std::memory_order_relaxed);
    }
}

void recordLock (void* mutex, std::uint64_t waitedNs, void* caller, bool tryOnly) noexcept
{
    if (tryOnly)
        gTrylockCalls.fetch_add (1, std::memory_order_relaxed);
    else
        gLockCalls.fetch_add (1, std::memory_order_relaxed);

    gLockedNanos.fetch_add (waitedNs, std::memory_order_relaxed);
    if (waitedNs > kBlockedLockNs)
        gBlockedLockCalls.fetch_add (1, std::memory_order_relaxed);

    std::uint64_t cur = gMaxLockNs.load (std::memory_order_relaxed);
    while (waitedNs > cur
           && ! gMaxLockNs.compare_exchange_weak (cur, waitedNs,
                                                  std::memory_order_relaxed))
    {}

    const auto r = gLockRecordCount.fetch_add (1, std::memory_order_relaxed);
    if (r < kMaxLockRecords)
    {
        LockRecord& rec = gLockRecords[r];
        rec.seq      = r;
        rec.waitedNs = waitedNs;
        rec.mutex    = mutex;
        rec.caller   = caller;
        rec.tryOnly  = tryOnly;
    }
    else
    {
        gLockRecordOverflow.fetch_add (1, std::memory_order_relaxed);
    }
}

Snapshot snapshot() noexcept
{
    Snapshot s {};
    for (std::size_t i = 0; i < kKindCount; ++i)
    {
        s.allocCalls[i] = gAllocCalls[i].load (std::memory_order_relaxed);
        s.allocBytes[i] = gAllocBytes[i].load (std::memory_order_relaxed);
    }
    s.allocRecordOverflow = gAllocRecordOverflow.load (std::memory_order_relaxed);
    s.lockCalls           = gLockCalls.load (std::memory_order_relaxed);
    s.trylockCalls        = gTrylockCalls.load (std::memory_order_relaxed);
    s.unlockCalls         = gUnlockCalls.load (std::memory_order_relaxed);
    s.condWaitCalls       = gCondWaitCalls.load (std::memory_order_relaxed);
    s.blockedLockCalls    = gBlockedLockCalls.load (std::memory_order_relaxed);
    s.lockedNanos         = gLockedNanos.load (std::memory_order_relaxed);
    s.maxLockNs           = gMaxLockNs.load (std::memory_order_relaxed);
    s.lockRecordOverflow  = gLockRecordOverflow.load (std::memory_order_relaxed);
    s.noopFrees           = gNoopFreeCalls.load (std::memory_order_relaxed);
    return s;
}

Snapshot delta (const Snapshot& before, const Snapshot& after) noexcept
{
    Snapshot d {};
    for (std::size_t i = 0; i < kKindCount; ++i)
    {
        d.allocCalls[i] = after.allocCalls[i] - before.allocCalls[i];
        d.allocBytes[i] = after.allocBytes[i] - before.allocBytes[i];
    }
    d.allocRecordOverflow = after.allocRecordOverflow - before.allocRecordOverflow;
    d.lockCalls           = after.lockCalls - before.lockCalls;
    d.trylockCalls        = after.trylockCalls - before.trylockCalls;
    d.unlockCalls         = after.unlockCalls - before.unlockCalls;
    d.condWaitCalls       = after.condWaitCalls - before.condWaitCalls;
    d.blockedLockCalls    = after.blockedLockCalls - before.blockedLockCalls;
    d.lockedNanos         = after.lockedNanos - before.lockedNanos;
    d.maxLockNs           = after.maxLockNs;
    d.lockRecordOverflow  = after.lockRecordOverflow - before.lockRecordOverflow;
    d.noopFrees           = after.noopFrees - before.noopFrees;
    return d;
}

void resetAll() noexcept
{
    for (std::size_t i = 0; i < kKindCount; ++i)
    {
        gAllocCalls[i].store (0, std::memory_order_relaxed);
        gAllocBytes[i].store (0, std::memory_order_relaxed);
    }
    gAllocRecordCount.store (0, std::memory_order_relaxed);
    gAllocRecordOverflow.store (0, std::memory_order_relaxed);
    gLockCalls.store (0, std::memory_order_relaxed);
    gTrylockCalls.store (0, std::memory_order_relaxed);
    gUnlockCalls.store (0, std::memory_order_relaxed);
    gCondWaitCalls.store (0, std::memory_order_relaxed);
    gBlockedLockCalls.store (0, std::memory_order_relaxed);
    gLockedNanos.store (0, std::memory_order_relaxed);
    gMaxLockNs.store (0, std::memory_order_relaxed);
    gLockRecordCount.store (0, std::memory_order_relaxed);
    gLockRecordOverflow.store (0, std::memory_order_relaxed);
    gNoopFreeCalls.store (0, std::memory_order_relaxed);
    gNoopFreeCallerCount.store (0, std::memory_order_relaxed);
}

std::uint64_t allocCallTotal (const Snapshot& s) noexcept
{
    return s.allocCalls[(std::size_t) Kind::cxxNew]
         + s.allocCalls[(std::size_t) Kind::cxxNewArray]
         + s.allocCalls[(std::size_t) Kind::cxxNewNothrow]
         + s.allocCalls[(std::size_t) Kind::cxxNewAligned]
         + s.allocCalls[(std::size_t) Kind::cMalloc]
         + s.allocCalls[(std::size_t) Kind::cCalloc]
         + s.allocCalls[(std::size_t) Kind::cRealloc];
}

std::uint64_t freeCallTotal (const Snapshot& s) noexcept
{
    return s.allocCalls[(std::size_t) Kind::cxxDelete]
         + s.allocCalls[(std::size_t) Kind::cxxDeleteArray]
         + s.allocCalls[(std::size_t) Kind::cxxDeleteSized]
         + s.allocCalls[(std::size_t) Kind::cxxDeleteAligned]
         + s.allocCalls[(std::size_t) Kind::cFree];
}

} // namespace rtprobe

//==============================================================================
// Allocation hooks
//==============================================================================

namespace
{
// Prevent the optimizer from eliding malloc/new calls in the self-check.
inline void* keepAlive (void* p) noexcept
{
    asm volatile ("" : : "r" (p) : "memory");
    return p;
}

void* rawAlloc (std::size_t n) noexcept
{
    return __real_malloc (n != 0 ? n : 1);
}

void* rawAllocAligned (std::size_t n, std::size_t alignment) noexcept
{
    if (alignment < sizeof (void*))
        alignment = sizeof (void*);
    void* p = nullptr;
    if (posix_memalign (&p, alignment, n != 0 ? n : 1) != 0)
        return nullptr;
    return p;
}
} // namespace

#define RTPROBE_RECORD(kind, bytes, ptr)                                     \
    do {                                                                     \
        if (rtprobe::tArmed)                                                 \
            rtprobe::recordAlloc ((kind), (bytes), (ptr),                    \
                                  __builtin_return_address (0));             \
    } while (false)

// free(NULL)/delete nullptr is a defined no-op: count it separately instead of
// reporting it as a heap free.
#define RTPROBE_FREE(kind, ptr)                                              \
    do {                                                                     \
        if (rtprobe::tArmed)                                                 \
        {                                                                    \
            if ((ptr) == nullptr)                                            \
            {                                                                \
                rtprobe::gNoopFreeCalls.fetch_add (1, std::memory_order_relaxed); \
                const auto _idx = rtprobe::gNoopFreeCallerCount.fetch_add (  \
                    1, std::memory_order_relaxed);                           \
                if (_idx < rtprobe::kMaxNoopFreeSites)                       \
                    rtprobe::gNoopFreeCallers[_idx].store (                  \
                        callerOffset (__builtin_return_address (0)),         \
                        std::memory_order_relaxed);                          \
            }                                                                \
            else                                                             \
                rtprobe::recordAlloc ((kind), 0, (ptr),                      \
                                      __builtin_return_address (0));         \
        }                                                                    \
    } while (false)

void* operator new (std::size_t n)
{
    void* p = rawAlloc (n);
    RTPROBE_RECORD (rtprobe::Kind::cxxNew, n, p);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new[] (std::size_t n)
{
    void* p = rawAlloc (n);
    RTPROBE_RECORD (rtprobe::Kind::cxxNewArray, n, p);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    void* p = rawAlloc (n);
    RTPROBE_RECORD (rtprobe::Kind::cxxNewNothrow, n, p);
    return p;
}

void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
    void* p = rawAlloc (n);
    RTPROBE_RECORD (rtprobe::Kind::cxxNewNothrow, n, p);
    return p;
}

void* operator new (std::size_t n, std::align_val_t a)
{
    void* p = rawAllocAligned (n, (std::size_t) a);
    RTPROBE_RECORD (rtprobe::Kind::cxxNewAligned, n, p);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new[] (std::size_t n, std::align_val_t a)
{
    void* p = rawAllocAligned (n, (std::size_t) a);
    RTPROBE_RECORD (rtprobe::Kind::cxxNewAligned, n, p);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    void* p = rawAllocAligned (n, (std::size_t) a);
    RTPROBE_RECORD (rtprobe::Kind::cxxNewAligned, n, p);
    return p;
}

void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    void* p = rawAllocAligned (n, (std::size_t) a);
    RTPROBE_RECORD (rtprobe::Kind::cxxNewAligned, n, p);
    return p;
}

void operator delete (void* p) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDelete, p);
    __real_free (p);
}

void operator delete[] (void* p) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteArray, p);
    __real_free (p);
}

void operator delete (void* p, const std::nothrow_t&) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDelete, p);
    __real_free (p);
}

void operator delete[] (void* p, const std::nothrow_t&) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteArray, p);
    __real_free (p);
}

void operator delete (void* p, std::size_t) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteSized, p);
    __real_free (p);
}

void operator delete[] (void* p, std::size_t) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteSized, p);
    __real_free (p);
}

void operator delete (void* p, std::align_val_t) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteAligned, p);
    __real_free (p);
}

void operator delete[] (void* p, std::align_val_t) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteAligned, p);
    __real_free (p);
}

void operator delete (void* p, std::size_t, std::align_val_t) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteAligned, p);
    __real_free (p);
}

void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept
{
    RTPROBE_FREE (rtprobe::Kind::cxxDeleteAligned, p);
    __real_free (p);
}

//==============================================================================
// C malloc family and pthread hooks (link-time --wrap targets)
//==============================================================================

extern "C" void* __wrap_malloc (std::size_t n)
{
    void* p = __real_malloc (n != 0 ? n : 1);
    if (rtprobe::tArmed)
        rtprobe::recordAlloc (rtprobe::Kind::cMalloc, n, p,
                              __builtin_return_address (0));
    return p;
}

extern "C" void* __wrap_calloc (std::size_t count, std::size_t size)
{
    void* p = __real_calloc (count, size);
    if (rtprobe::tArmed)
        rtprobe::recordAlloc (rtprobe::Kind::cCalloc, count * size, p,
                              __builtin_return_address (0));
    return p;
}

extern "C" void* __wrap_realloc (void* old, std::size_t n)
{
    void* p = __real_realloc (old, n != 0 ? n : 1);
    if (rtprobe::tArmed)
        rtprobe::recordAlloc (rtprobe::Kind::cRealloc, n, p,
                              __builtin_return_address (0));
    return p;
}

extern "C" void __wrap_free (void* p)
{
    RTPROBE_FREE (rtprobe::Kind::cFree, p);
    __real_free (p);
}

extern "C" int __wrap_pthread_mutex_lock (pthread_mutex_t* m)
{
    if (! rtprobe::tArmed)
        return __real_pthread_mutex_lock (m);

    const auto t0 = rtprobe::monotonicNs();
    const int r = __real_pthread_mutex_lock (m);
    const auto dt = rtprobe::monotonicNs() - t0;
    rtprobe::recordLock (m, dt, __builtin_return_address (0), false);
    return r;
}

extern "C" int __wrap_pthread_mutex_trylock (pthread_mutex_t* m)
{
    if (! rtprobe::tArmed)
        return __real_pthread_mutex_trylock (m);

    const auto t0 = rtprobe::monotonicNs();
    const int r = __real_pthread_mutex_trylock (m);
    const auto dt = rtprobe::monotonicNs() - t0;
    rtprobe::recordLock (m, dt, __builtin_return_address (0), true);
    return r;
}

extern "C" int __wrap_pthread_mutex_unlock (pthread_mutex_t* m)
{
    if (rtprobe::tArmed)
        rtprobe::gUnlockCalls.fetch_add (1, std::memory_order_relaxed);
    return __real_pthread_mutex_unlock (m);
}

extern "C" int __wrap_pthread_cond_clockwait (pthread_cond_t* c, pthread_mutex_t* m,
                                              clockid_t clockId,
                                              const struct timespec* abstime)
{
    if (! rtprobe::tArmed)
        return __real_pthread_cond_clockwait (c, m, clockId, abstime);

    rtprobe::gCondWaitCalls.fetch_add (1, std::memory_order_relaxed);
    const auto t0 = rtprobe::monotonicNs();
    const int r = __real_pthread_cond_clockwait (c, m, clockId, abstime);
    const auto dt = rtprobe::monotonicNs() - t0;
    rtprobe::recordLock (m, dt, __builtin_return_address (0), true);
    return r;
}

//==============================================================================
// Instrument self-check
//==============================================================================

namespace rtprobe
{

int runSelfCheck (std::FILE* out)
{
    int failures = 0;
    initInstrumentation();
    auto fail = [&] (const char* what)
    {
        ++failures;
        std::fprintf (out, "  SELFCHECK FAIL: %s\n", what);
    };

    // Phase 0: operations performed while DISARMED must not be recorded. This
    // is the proof the gating works; without it a silent zero could simply mean
    // the hooks were never installed.
    resetAll();
    {
        volatile int* scalar = (int*) keepAlive (new int (7));
        volatile void* raw = keepAlive (std::malloc (32));
        pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock (&m);
        pthread_mutex_unlock (&m);
        std::free ((void*) raw);
        delete scalar;
    }
    {
        const Snapshot s = snapshot();
        if (allocCallTotal (s) != 0 || freeCallTotal (s) != 0)
            fail ("unarmed region recorded allocation/free traffic");
        if (s.lockCalls != 0 || s.unlockCalls != 0)
            fail ("unarmed region recorded lock traffic");
    }

    // Phase 1: the same operations, armed, must be recorded under the correct
    // categories. This is the positive detection proof. The unsized C++
    // families are exercised through direct operator calls so they cannot be
    // elided or replaced by the compiler's sized-delete lowering.
    resetAll();
    arm();
    {
        void* unsized    = keepAlive (::operator new (16));
        void* unsizedArr = keepAlive (::operator new[] (16));

        volatile int* scalar = (int*) keepAlive (new int (7));
        volatile int* array  = (int*) keepAlive (new int[4]);

        volatile void* raw   = keepAlive (std::malloc (24));
        raw = keepAlive (std::realloc ((void*) raw, 48));
        volatile void* zed   = keepAlive (std::calloc (2, 8));
        pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock (&m);
        pthread_mutex_unlock (&m);

        delete scalar;
        delete[] array;
        ::operator delete (unsized);
        ::operator delete[] (unsizedArr);
        std::free ((void*) raw);
        std::free ((void*) zed);
    }
    disarm();

    {
        const Snapshot s = snapshot();
        const auto calls = [&] (Kind k) { return s.allocCalls[(std::size_t) k]; };

        if (calls (Kind::cxxNew)         < 1) fail ("armed new not detected");
        if (calls (Kind::cxxNewArray)    < 1) fail ("armed new[] not detected");
        if (calls (Kind::cxxDelete)      < 1) fail ("armed delete not detected");
        if (calls (Kind::cxxDeleteArray) < 1) fail ("armed delete[] not detected");
        if (calls (Kind::cMalloc)        < 1) fail ("armed malloc not detected");
        if (calls (Kind::cRealloc)       < 1) fail ("armed realloc not detected");
        if (calls (Kind::cCalloc)        < 1) fail ("armed calloc not detected");
        if (calls (Kind::cFree)          < 2) fail ("armed free not detected (need 2)");
        if (s.lockCalls                  < 1) fail ("armed pthread_mutex_lock not detected");
        if (s.unlockCalls                < 1) fail ("armed pthread_mutex_unlock not detected");

        std::fprintf (out,
                      "  armed: new=%llu new[]=%llu del=%llu del[]=%llu del(sized)=%llu"
                      " malloc=%llu calloc=%llu realloc=%llu free=%llu"
                      " lock=%llu unlock=%llu\n",
                      (unsigned long long) calls (Kind::cxxNew),
                      (unsigned long long) calls (Kind::cxxNewArray),
                      (unsigned long long) calls (Kind::cxxDelete),
                      (unsigned long long) calls (Kind::cxxDeleteArray),
                      (unsigned long long) calls (Kind::cxxDeleteSized),
                      (unsigned long long) calls (Kind::cMalloc),
                      (unsigned long long) calls (Kind::cCalloc),
                      (unsigned long long) calls (Kind::cRealloc),
                      (unsigned long long) calls (Kind::cFree),
                      (unsigned long long) s.lockCalls,
                      (unsigned long long) s.unlockCalls);
    }

    return failures == 0 ? 0 : 1;
}

} // namespace rtprobe
