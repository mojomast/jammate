// RT-002 local runtime probe instrumentation.
//
// Fixed-capacity, preallocated allocation/free and lock counters. This header
// is deliberately allocation-free: every counter is a static std::atomic and
// every detail record lives in a static array sized at compile time. Arming is
// a single thread_local bool store, so no work happens on the callback unless
// the probe has explicitly armed the region it wants to measure.
//
// This file is part of the RT-002 independent probe. It does not modify the
// processor, JUCE, or any shared source.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace rtprobe
{

// C++ new/delete families are kept distinct from the C malloc family so a
// positive result can say which allocator was reached, not merely "something
// allocated".
enum class Kind : std::uint8_t
{
    cxxNew = 0,
    cxxNewArray,
    cxxNewNothrow,
    cxxNewAligned,
    cxxDelete,
    cxxDeleteArray,
    cxxDeleteSized,
    cxxDeleteAligned,
    cMalloc,
    cCalloc,
    cRealloc,
    cFree,
    count
};

constexpr std::size_t kKindCount = (std::size_t) Kind::count;

const char* kindName (Kind k) noexcept;

// One record per observed allocator/free call while armed. `caller` is the
// instruction address that entered the allocator (return address), useful for
// attributing a positive hit. Records are best-effort: once the fixed capacity
// is reached an overflow counter is bumped instead of allocating.
struct AllocRecord
{
    std::uint64_t seq;
    Kind          kind;
    std::size_t   bytes;
    void*         ptr;
    void*         caller;
};

constexpr std::size_t kMaxAllocRecords = 1u << 16;

struct LockRecord
{
    std::uint64_t seq;
    std::uint64_t waitedNs;
    void*         mutex;
    void*         caller;
    bool          tryOnly;
};

constexpr std::size_t kMaxLockRecords = 1u << 14;

// A lock that itself waited at least this long is reported as blocking. The
// audio callback must acquire no lock at all; the threshold only separates
// "instant" uncontended acquisitions from observable waits.
constexpr std::uint64_t kBlockedLockNs = 1000;

extern std::atomic<std::uint64_t> gAllocCalls[kKindCount];
extern std::atomic<std::uint64_t> gAllocBytes[kKindCount];
extern std::atomic<std::uint64_t> gAllocRecordCount;
extern std::atomic<std::uint64_t> gAllocRecordOverflow;
extern AllocRecord               gAllocRecords[kMaxAllocRecords];

extern std::atomic<std::uint64_t> gLockCalls;
extern std::atomic<std::uint64_t> gTrylockCalls;
extern std::atomic<std::uint64_t> gUnlockCalls;
extern std::atomic<std::uint64_t> gCondWaitCalls;
extern std::atomic<std::uint64_t> gBlockedLockCalls;
extern std::atomic<std::uint64_t> gLockedNanos;
extern std::atomic<std::uint64_t> gMaxLockNs;
extern std::atomic<std::uint64_t> gLockRecordCount;
extern std::atomic<std::uint64_t> gLockRecordOverflow;
extern LockRecord                gLockRecords[kMaxLockRecords];

// free(NULL)/delete nullptr are defined no-ops; they are counted here rather
// than as heap frees so a "0 frees" claim stays precise.
extern std::atomic<std::uint64_t> gNoopFreeCalls;

// Caller offsets of the first few no-op frees, for attribution only.
constexpr std::size_t kMaxNoopFreeSites = 256;
extern std::atomic<void*>         gNoopFreeCallers[kMaxNoopFreeSites];
extern std::atomic<std::uint64_t> gNoopFreeCallerCount;

extern thread_local bool tArmed;

inline bool armed() noexcept { return tArmed; }

// Must be called once from control code before any armed region. Resolves the
// main module load bias (so records can report file-relative caller offsets)
// while no measurement is in progress.
void initInstrumentation() noexcept;

// Arming must be called from control code only, never from an allocator.
void arm() noexcept;
void disarm() noexcept;

// Called by the allocator wrappers. Must not allocate or lock.
void recordAlloc (Kind k, std::size_t bytes, void* ptr, void* caller) noexcept;
void recordLock (void* mutex, std::uint64_t waitedNs, void* caller, bool tryOnly) noexcept;
void recordCondWait (void* mutex, std::uint64_t waitedNs, void* caller) noexcept;

struct Snapshot
{
    std::uint64_t allocCalls[kKindCount];
    std::uint64_t allocBytes[kKindCount];
    std::uint64_t allocRecordOverflow;
    std::uint64_t lockCalls;
    std::uint64_t trylockCalls;
    std::uint64_t unlockCalls;
    std::uint64_t condWaitCalls;
    std::uint64_t blockedLockCalls;
    std::uint64_t lockedNanos;
    std::uint64_t maxLockNs;
    std::uint64_t lockRecordOverflow;
    std::uint64_t noopFrees;
};

Snapshot snapshot() noexcept;
Snapshot delta (const Snapshot& before, const Snapshot& after) noexcept;

// Zeroes every counter and record cursor. Control thread only.
void resetAll() noexcept;

// Total allocated/freed calls across the categories, for compact reporting.
std::uint64_t allocCallTotal (const Snapshot& s) noexcept;
std::uint64_t freeCallTotal (const Snapshot& s) noexcept;

// Proves the instrument itself works: an unarmed region must record nothing,
// an armed region must record the known new/delete, malloc/realloc/free and
// mutex operations it performs. Returns 0 on success, non-zero on failure.
int runSelfCheck (std::FILE* out);

} // namespace rtprobe
