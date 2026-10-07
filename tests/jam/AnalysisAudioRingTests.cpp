// Unit tests for jam::AnalysisAudioRing — the bounded SPSC analysis queue
// frozen by SPEC.md section 8.1 and exercised under the audio-thread contract
// of SPEC.md section 7.1.
//
// The audio callback pushes into this ring and the rhythm-analysis worker pops
// out of it, so what is asserted here are safety properties, not conveniences:
//
//   - push() is bounded: it never allocates, never blocks, never retries, and
//     never waits for the consumer (SPEC 7.1 forbids all of those on the audio
//     thread);
//   - overflow drops the *incoming* block, bumps an explicit counter and returns
//     immediately, leaving already-queued frames byte-for-byte intact;
//   - capacity 0 is legal and degrades to a permanent sink, which is the safe
//     direction to fail in;
//   - payload and metadata survive FIFO order, wraparound and mixed block sizes.
//
// Determinism policy (matches tests/jam/JamTest.h): no sleeping, no wall-clock,
// no threads, no file I/O, no unseeded randomness. The only pseudo-randomness is
// the fixed-seed LCG defined below, which is written in exact uint32_t arithmetic
// so it produces the identical sequence on every platform and compiler.

#include "JamTest.h"

#include "jam/AnalysisAudioRing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <vector>

//==============================================================================
// Heap-allocation counter.
//
// SPEC 8.1 requires "storage fully allocated in prepareToPlay" and SPEC 7.1
// forbids heap allocation in the callback. There is no way to observe that from
// the ring's own API, so the replacement global operators below count every
// allocation made by the whole test binary while tracking is enabled. They are
// deliberate replacements (not wrappers around malloc) and delegate to
// malloc/free/aligned_alloc, so standard behaviour is unchanged.
namespace
{

bool             g_trackAllocations = false;
uint64_t         g_allocationCount = 0;
uint64_t         g_allocatedBytes = 0;

void noteAllocation (std::size_t size) noexcept
{
    if (g_trackAllocations)
    {
        ++g_allocationCount;
        g_allocatedBytes += size;
    }
}

} // namespace

void* operator new (std::size_t size)
{
    noteAllocation (size);

    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    noteAllocation (size);

    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    noteAllocation (size);
    return std::malloc (size != 0 ? size : 1);
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    noteAllocation (size);
    return std::malloc (size != 0 ? size : 1);
}

void* operator new (std::size_t size, std::align_val_t alignment)
{
    noteAllocation (size);

    const std::size_t align = static_cast<std::size_t> (alignment);
    const std::size_t rounded = (size + align - 1) & ~(align - 1);

    if (void* p = std::aligned_alloc (align, rounded))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return ::operator new (size, alignment);
}

void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }

//==============================================================================
namespace
{

/** Counts allocations only while alive, so a test can prove that a region of
    code touching the ring did not touch the heap. */
class AllocationScope
{
public:
    AllocationScope() noexcept
        : previousTracking_ (g_trackAllocations)
    {
        g_allocationCount = 0;
        g_allocatedBytes = 0;
        g_trackAllocations = true;
    }

    ~AllocationScope()
    {
        g_trackAllocations = previousTracking_;
    }

    AllocationScope (const AllocationScope&) = delete;
    AllocationScope& operator= (const AllocationScope&) = delete;

    uint64_t count() const noexcept { return g_allocationCount; }
    uint64_t bytes() const noexcept { return g_allocatedBytes; }

private:
    bool previousTracking_;
};

// --- deterministic data ------------------------------------------------------

constexpr uint32_t kMaxBlock = static_cast<uint32_t> (jam::kMaxAnalysisBlock);

// Distinct capture rates per frame: metadata that is unique per frame is what
// distinguishes "the right frame came back" from "some frame came back".
constexpr double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
constexpr int kRateCount = 4;

double rateFor (int index) noexcept
{
    return kRates[static_cast<int> (static_cast<unsigned> (index) % kRateCount)];
}

/** Deterministic sample payload. Every value is a multiple of 1/16 and so is
    exactly representable in binary32, which lets the assertions compare
    bit-for-bit: a swapped or stale sample can never hide behind a tolerance. */
float sampleValue (int frame, int index) noexcept
{
    return static_cast<float> (((frame * 13 + index * 7) % 33) - 16) * 0.0625f;
}

struct ExpectedFrame
{
    uint64_t         sampleTime = 0;
    double           sampleRate = 48000.0;
    std::vector<float> samples;
};

/** Builds the frame the ring is supposed to hand back for `index`. The sample
    clock is unique per frame, so metadata that leaks between slots is caught. */
ExpectedFrame makeExpected (int index, uint32_t numSamples)
{
    ExpectedFrame e;
    e.sampleTime = static_cast<uint64_t> (index) * uint64_t { 1000000 } + uint64_t { 7 };
    e.sampleRate = rateFor (index);
    e.samples.resize (numSamples);

    for (uint32_t i = 0; i < numSamples; ++i)
        e.samples[i] = sampleValue (index, static_cast<int> (i));

    return e;
}

/** Every field, every sample. Used where the frame is small enough that a
    per-sample check is affordable and a failure pinpoints the offending
    sample index. */
void checkFrame (const jam::AnalysisFrame& actual, const ExpectedFrame& expected)
{
    CHECK_EQ (actual.sampleTime, expected.sampleTime);
    CHECK_EQ (actual.sourceSampleRate, expected.sampleRate);
    CHECK_EQ (actual.numSamples, static_cast<uint32_t> (expected.samples.size()));

    for (std::size_t i = 0; i < expected.samples.size(); ++i)
        CHECK_EQ (actual.samples[i], expected.samples[i]);
}

// --- content hashing ---------------------------------------------------------
//
// Long tests cannot afford one check per sample (a 10 000-operation stress run
// would emit tens of millions of lines on failure), so they compare an exact
// 64-bit FNV-1a hash over the metadata bits and every sample bit instead. The
// hash is exact, not statistical: a mismatch is still a guaranteed mismatch.

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void mixValue (uint64_t& hash, uint64_t value) noexcept
{
    for (int byte = 0; byte < 8; ++byte)
    {
        hash ^= (value >> (byte * 8)) & uint64_t { 0xff };
        hash *= kFnvPrime;
    }
}

uint64_t hashFrame (uint64_t sampleTime, double sampleRate, uint32_t numSamples, const float* samples) noexcept
{
    uint64_t hash = kFnvOffsetBasis;
    mixValue (hash, sampleTime);

    uint64_t rateBits = 0;
    std::memcpy (&rateBits, &sampleRate, sizeof rateBits);
    mixValue (hash, rateBits);
    mixValue (hash, numSamples);

    for (uint32_t i = 0; i < numSamples; ++i)
    {
        uint32_t sampleBits = 0;
        std::memcpy (&sampleBits, &samples[i], sizeof sampleBits);
        mixValue (hash, sampleBits);
    }

    return hash;
}

uint64_t hashFrame (const jam::AnalysisFrame& actual) noexcept
{
    return hashFrame (actual.sampleTime, actual.sourceSampleRate, actual.numSamples, actual.samples);
}

uint64_t hashFrame (const ExpectedFrame& expected) noexcept
{
    return hashFrame (expected.sampleTime, expected.sampleRate,
                      static_cast<uint32_t> (expected.samples.size()), expected.samples.data());
}

/** Numerical Recipes LCG. Fixed multiplier/increment and uint32_t arithmetic,
    so the sequence is bit-identical everywhere; no <random>, no seeding, no
    platform-dependent distribution. */
class Lcg
{
public:
    explicit Lcg (uint32_t seed) noexcept : state_ (seed) {}

    uint32_t next() noexcept
    {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }

private:
    uint32_t state_;
};

} // namespace

//==============================================================================
// 1. Empty ring.

JAM_TEST (AnalysisAudioRing, emptyRingRefusesToPop)
{
    jam::AnalysisAudioRing ring (8);

    CHECK (ring.valid());
    CHECK_EQ (ring.capacity(), std::size_t { 8 });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

    jam::AnalysisFrame out;
    out.numSamples = 0xdead;     // poison: a failed pop must not write anything

    CHECK (! ring.pop (out));
    CHECK_EQ (out.numSamples, uint32_t { 0xdead });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

    // Repeated failed pops stay harmless: the consumer polls, it does not wait.
    for (int i = 0; i < 8; ++i)
        CHECK (! ring.pop (out));

    CHECK_EQ (out.numSamples, uint32_t { 0xdead });
}

//==============================================================================
// 2. Capacity 0 is legal and is the safe failure direction.

JAM_TEST (AnalysisAudioRing, zeroCapacityIsALegalPermanentSink)
{
    jam::AnalysisAudioRing ring (0);

    CHECK (! ring.valid());
    CHECK_EQ (ring.capacity(), std::size_t { 0 });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });

    const std::vector<float> block (16, 0.25f);

    // Every push is refused immediately — the callback never waits and never
    // retries, it just keeps rendering audio.
    for (int i = 0; i < 64; ++i)
        CHECK (! ring.push (block.data(), 16, static_cast<uint64_t> (i), 48000.0));

    // Nothing was ever stored, so nothing can ever be read back.
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });

    jam::AnalysisFrame out;
    CHECK (! ring.pop (out));

    // The ring is inert but not broken: its queries still answer.
    CHECK_EQ (ring.capacity(), std::size_t { 0 });
    CHECK (! ring.valid());
}

// DEVIATION from the frozen header, recorded in task-notes/MOD-001.md under
// "Known limitations" and reported to the orchestrator. AnalysisAudioRing.h
// lines 52-53 document push() as "Returns false (and increments the overrun
// counter) when the ring is full or disabled", and lines 10-11 describe a
// capacity-0 ring as a "permanent-overrun sink". The implementation returns at
// AnalysisAudioRing.h line 57 — before the counter is touched — so a disabled
// ring reports overrunCount() == 0 forever and never reports a drop. The safe
// direction (never blocks, never stores, never corrupts) IS proved by the test
// above; only the counter semantics differ from the comment. This case pins
// the observed behaviour so the discrepancy cannot drift unnoticed — it is a
// characterisation, not an endorsement.
JAM_TEST (AnalysisAudioRing, zeroCapacityPushIsNotCountedAsOverrun)
{
    jam::AnalysisAudioRing ring (0);
    const std::vector<float> block (8, 0.5f);

    for (int i = 0; i < 5; ++i)
        CHECK (! ring.push (block.data(), 8, 0, 48000.0));

    // Documented behaviour would be 5.
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

    // droppedBlocks() is documented as an alias of the overrun counter, so the
    // two must agree even where the counter is (wrongly) not bumped.
    CHECK_EQ (ring.droppedBlocks(), ring.overrunCount());
}

//==============================================================================
// 3. One push, one pop: payload and metadata survive exactly.

JAM_TEST (AnalysisAudioRing, singlePushPopPreservesPayloadAndMetadata)
{
    jam::AnalysisAudioRing ring (4);

    constexpr uint32_t n = 16;
    const ExpectedFrame expected = makeExpected (1, n);

    REQUIRE (ring.push (expected.samples.data(), n, expected.sampleTime, expected.sampleRate));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 1 });
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

    jam::AnalysisFrame out;
    REQUIRE (ring.pop (out));

    CHECK_EQ (out.sampleTime, uint64_t { 1000007 });
    CHECK_EQ (out.sourceSampleRate, 48000.0);   // rateFor (1)
    CHECK_EQ (out.numSamples, uint32_t { n });

    for (uint32_t i = 0; i < n; ++i)
        CHECK_EQ (out.samples[i], expected.samples[i]);

    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });

    // Exactly one frame was queued: a second pop must fail rather than replay
    // the same slot.
    CHECK (! ring.pop (out));
}

//==============================================================================
// 4. FIFO order across a full ring.

JAM_TEST (AnalysisAudioRing, framesComeBackInProducerOrder)
{
    constexpr int kFrames = 16;
    jam::AnalysisAudioRing ring (kFrames);

    std::vector<ExpectedFrame> expected;

    for (int i = 0; i < kFrames; ++i)
    {
        const ExpectedFrame e = makeExpected (i, static_cast<uint32_t> (1 + i * 3));
        expected.push_back (e);

        REQUIRE (ring.push (e.samples.data(),
                            static_cast<uint32_t> (e.samples.size()),
                            e.sampleTime, e.sampleRate));
        CHECK_EQ (ring.pushedBlocks(), static_cast<uint64_t> (i + 1));
    }

    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

    jam::AnalysisFrame out;
    for (int i = 0; i < kFrames; ++i)
    {
        REQUIRE (ring.pop (out));
        checkFrame (out, expected[static_cast<std::size_t> (i)]);
        CHECK_EQ (ring.pushedBlocks(), static_cast<uint64_t> (kFrames - i - 1));
    }

    CHECK (! ring.pop (out));
}

//==============================================================================
// 5. Wraparound: the indices wrap many times over.

JAM_TEST (AnalysisAudioRing, wraparoundPreservesTheFrameSequence)
{
    constexpr std::size_t kCapacity = 4;
    constexpr int kRounds = 200;

    jam::AnalysisAudioRing ring (kCapacity);

    // One push per round, one pop every round once the pipe is primed: the write
    // index advances 200 times against 4 slots (~50 wraps) while the read index
    // trails it. An implementation that confuses "index wrapped" with "empty" or
    // "full" loses, duplicates or reorders frames here. The internal indices are
    // private monotonic uint64_t counters, so the only observable evidence of a
    // correct wrap is this: every frame, in order, with its own payload.
    std::vector<ExpectedFrame> inFlight;

    for (int round = 0; round < kRounds; ++round)
    {
        const ExpectedFrame e = makeExpected (round, static_cast<uint32_t> (1 + (round % 37)));
        inFlight.push_back (e);

        REQUIRE (ring.push (e.samples.data(),
                            static_cast<uint32_t> (e.samples.size()),
                            e.sampleTime, e.sampleRate));
        CHECK_LE (ring.pushedBlocks(), static_cast<uint64_t> (kCapacity));
        CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

        if (inFlight.size() >= 3)
        {
            jam::AnalysisFrame out;
            REQUIRE (ring.pop (out));
            checkFrame (out, inFlight.front());
            inFlight.erase (inFlight.begin());
        }
    }

    // The ring is back down to a deterministic residue, still in order.
    jam::AnalysisFrame out;
    while (! inFlight.empty())
    {
        REQUIRE (ring.pop (out));
        checkFrame (out, inFlight.front());
        inFlight.erase (inFlight.begin());
    }

    CHECK (! ring.pop (out));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });
}

//==============================================================================
// 6. Full ring: the incoming block is dropped, the queued data is not.

JAM_TEST (AnalysisAudioRing, overflowOnAFullRingLeavesQueuedDataIntact)
{
    constexpr std::size_t kCapacity = 3;
    jam::AnalysisAudioRing ring (kCapacity);

    std::vector<ExpectedFrame> queued;

    for (int i = 0; i < 3; ++i)
    {
        const ExpectedFrame e = makeExpected (i, 32);
        queued.push_back (e);
        REQUIRE (ring.push (e.samples.data(), 32, e.sampleTime, e.sampleRate));
    }

    CHECK_EQ (ring.pushedBlocks(), uint64_t { 3 });

    // Fourth block arrives with no room. SPEC 8.1: drop it, count it, continue
    // audio immediately.
    const ExpectedFrame rejected = makeExpected (99, 48);
    CHECK (! ring.push (rejected.samples.data(), 48, rejected.sampleTime, rejected.sampleRate));

    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });
    CHECK_EQ (ring.droppedBlocks(), uint64_t { 1 });

    // The write index did not move, so the overflow cost no queue state.
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 3 });

    // This is the assertion that matters: the rejected block targeted slot
    // (3 % 3) == 0, which still holds the oldest queued frame. Had the producer
    // written before checking for room, that frame would now be corrupted.
    jam::AnalysisFrame out;
    for (const ExpectedFrame& e : queued)
    {
        REQUIRE (ring.pop (out));
        checkFrame (out, e);
    }

    CHECK (! ring.pop (out));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });
    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });   // draining is not an overrun
}

//==============================================================================
// 7. The overrun counter is monotonic and counts exactly the failed pushes.

JAM_TEST (AnalysisAudioRing, overrunCountTracksExactlyTheFailedPushes)
{
    constexpr std::size_t kCapacity = 2;
    jam::AnalysisAudioRing ring (kCapacity);

    const ExpectedFrame a = makeExpected (0, 24);
    const ExpectedFrame b = makeExpected (1, 24);
    const ExpectedFrame c = makeExpected (2, 24);

    REQUIRE (ring.push (a.samples.data(), 24, a.sampleTime, a.sampleRate));
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });
    REQUIRE (ring.push (b.samples.data(), 24, b.sampleTime, b.sampleRate));
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

    // Five consecutive refusals while full.
    for (int i = 0; i < 5; ++i)
        CHECK (! ring.push (c.samples.data(), 24, c.sampleTime, c.sampleRate));

    CHECK_EQ (ring.overrunCount(), uint64_t { 5 });

    // Make room, then succeed: the counter must not move on a successful push.
    jam::AnalysisFrame out;
    REQUIRE (ring.pop (out));
    checkFrame (out, a);

    REQUIRE (ring.push (c.samples.data(), 24, c.sampleTime, c.sampleRate));
    CHECK_EQ (ring.overrunCount(), uint64_t { 5 });

    // Full again, refuse once more: the count is cumulative and never resets.
    CHECK (! ring.push (a.samples.data(), 24, a.sampleTime, a.sampleRate));
    CHECK_EQ (ring.overrunCount(), uint64_t { 6 });

    ring.reset();
    CHECK_EQ (ring.overrunCount(), uint64_t { 6 });
}

//==============================================================================
// 8. The dropped block left no residue, not even partial writes.

JAM_TEST (AnalysisAudioRing, rejectedBlockLeavesNoResidueInItsTargetSlot)
{
    constexpr std::size_t kCapacity = 2;
    jam::AnalysisAudioRing ring (kCapacity);

    const ExpectedFrame a = makeExpected (0, 64);   // lands in slot 0
    const ExpectedFrame b = makeExpected (1, 64);   // lands in slot 1

    REQUIRE (ring.push (a.samples.data(), 64, a.sampleTime, a.sampleRate));
    REQUIRE (ring.push (b.samples.data(), 64, b.sampleTime, b.sampleRate));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 2 });

    // The next push would target slot (2 % 2) == 0, i.e. a's slot. It is refused.
    const ExpectedFrame rejected = makeExpected (7, 64);
    CHECK (! ring.push (rejected.samples.data(), 64, rejected.sampleTime, rejected.sampleRate));
    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 2 });

    jam::AnalysisFrame out;

    // Still exactly a, byte for byte: no partial write happened before the
    // capacity check.
    REQUIRE (ring.pop (out));
    checkFrame (out, a);

    // The freed slot is reused by a fresh block. Nothing from the rejected block
    // can be observed through it, because pop() copies exactly numSamples.
    const ExpectedFrame d = makeExpected (8, 8);
    REQUIRE (ring.push (d.samples.data(), 8, d.sampleTime, d.sampleRate));

    REQUIRE (ring.pop (out));
    checkFrame (out, b);
    CHECK_EQ (out.numSamples, uint32_t { 64 });

    REQUIRE (ring.pop (out));
    checkFrame (out, d);   // 8 valid samples, not 64

    CHECK (! ring.pop (out));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });
}

//==============================================================================
// 9. Metadata stays attached to its own frame across a wrap.

JAM_TEST (AnalysisAudioRing, metadataStaysWithItsOwnFrameAcrossWraparound)
{
    constexpr std::size_t kCapacity = 3;
    jam::AnalysisAudioRing ring (kCapacity);

    std::vector<ExpectedFrame> expected;

    // Fill the ring (write index 0..2).
    for (int i = 0; i < 3; ++i)
    {
        const ExpectedFrame e = makeExpected (i, static_cast<uint32_t> (16 + i));
        expected.push_back (e);
        REQUIRE (ring.push (e.samples.data(),
                            static_cast<uint32_t> (e.samples.size()),
                            e.sampleTime, e.sampleRate));
    }

    // Consume one (write index 3), then push two more so writes land on slots 0
    // and 1 again. From here the ring has wrapped at least once.
    jam::AnalysisFrame out;
    REQUIRE (ring.pop (out));
    checkFrame (out, expected[0]);
    expected.erase (expected.begin());

    for (int i = 3; i < 5; ++i)
    {
        const ExpectedFrame e = makeExpected (i, static_cast<uint32_t> (16 + i));
        expected.push_back (e);
        REQUIRE (ring.push (e.samples.data(),
                            static_cast<uint32_t> (e.samples.size()),
                            e.sampleTime, e.sampleRate));

        // Interleave a pop so the next push also has room; the ring never
        // reports an overrun.
        REQUIRE (ring.pop (out));
        checkFrame (out, expected.front());
        expected.erase (expected.begin());
    }

    CHECK_EQ (ring.pushedBlocks(), static_cast<uint64_t> (expected.size()));

    // Every frame carries its own sample clock and capture rate: the analysis
    // worker resamples from sourceSampleRate, so a leaked rate is a wrong tempo.
    for (const ExpectedFrame& e : expected)
    {
        REQUIRE (ring.pop (out));
        checkFrame (out, e);
    }

    CHECK (! ring.pop (out));
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });
}

//==============================================================================
// 10. Variable block sizes, up to and including the configured maximum.

JAM_TEST (AnalysisAudioRing, variableBlockSizesRoundTrip)
{
    const uint32_t sizes[] = { 1, 2, 127, 128, 512, 1024, kMaxBlock };
    constexpr int kCount = static_cast<int> (sizeof (sizes) / sizeof (sizes[0]));

    jam::AnalysisAudioRing ring (kCount);

    std::vector<ExpectedFrame> expected;

    for (int i = 0; i < kCount; ++i)
    {
        const ExpectedFrame e = makeExpected (i, sizes[i]);
        expected.push_back (e);

        REQUIRE (ring.push (e.samples.data(), sizes[i], e.sampleTime, e.sampleRate));
        CHECK_EQ (ring.pushedBlocks(), static_cast<uint64_t> (i + 1));
    }

    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });

    jam::AnalysisFrame out;
    for (int i = 0; i < kCount; ++i)
    {
        REQUIRE (ring.pop (out));
        CHECK_EQ (out.numSamples, sizes[i]);
        checkFrame (out, expected[static_cast<std::size_t> (i)]);
    }

    CHECK (! ring.pop (out));
}

//==============================================================================
// 11. Oversized input is clamped, not rejected and not overrun.

JAM_TEST (AnalysisAudioRing, oversizedInputIsClampedToTheMaximumBlock)
{
    jam::AnalysisAudioRing ring (2);

    // Source is deliberately larger than the ring's per-block maximum so the
    // clamp has something to drop.
    const uint32_t requested = kMaxBlock * 2u;
    std::vector<float> source (requested);

    for (uint32_t i = 0; i < requested; ++i)
        source[i] = sampleValue (5, static_cast<int> (i));

    // There is room, so a too-large block must succeed: clamping, not failure.
    REQUIRE (ring.push (source.data(), requested, 4242, 96000.0));
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 1 });

    jam::AnalysisFrame out;
    REQUIRE (ring.pop (out));

    CHECK_EQ (out.numSamples, kMaxBlock);
    CHECK_EQ (out.sampleTime, uint64_t { 4242 });
    CHECK_EQ (out.sourceSampleRate, 96000.0);

    // The head of the block survives verbatim; the tail was never copied.
    for (uint32_t i = 0; i < kMaxBlock; ++i)
        CHECK_EQ (out.samples[i], source[i]);

    CHECK (! ring.pop (out));

    // Room, not size, decides acceptance: a clamped block still fits a slot that
    // has one free, and the clamp must never be reported as an overrun.
    const ExpectedFrame fits = makeExpected (6, kMaxBlock);
    const ExpectedFrame tooBig = makeExpected (7, kMaxBlock + 1u);

    REQUIRE (ring.push (fits.samples.data(), kMaxBlock, fits.sampleTime, fits.sampleRate));
    REQUIRE (ring.push (tooBig.samples.data(), kMaxBlock + 1u, tooBig.sampleTime, tooBig.sampleRate));
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 2 });

    // The oversized frame comes back holding only its first kMaxBlock samples,
    // with its own metadata: truncation, not rejection.
    ExpectedFrame clamped = tooBig;
    clamped.samples.resize (kMaxBlock);

    REQUIRE (ring.pop (out));
    checkFrame (out, fits);
    REQUIRE (ring.pop (out));
    checkFrame (out, clamped);
    CHECK_EQ (out.numSamples, kMaxBlock);

    // With the ring genuinely full, even a clampable block is dropped and
    // counted: the drop is about room, not about the oversized request.
    REQUIRE (ring.push (fits.samples.data(), kMaxBlock, fits.sampleTime, fits.sampleRate));
    REQUIRE (ring.push (tooBig.samples.data(), kMaxBlock, tooBig.sampleTime, tooBig.sampleRate));
    CHECK (! ring.push (tooBig.samples.data(), kMaxBlock + 1u, tooBig.sampleTime, tooBig.sampleRate));
    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 2 });
}

//==============================================================================
// 12. Null pointer and zero-length pushes are refused, state stays intact.

JAM_TEST (AnalysisAudioRing, nullPointerAndZeroLengthPushesAreRejected)
{
    jam::AnalysisAudioRing ring (4);

    const ExpectedFrame a = makeExpected (0, 8);
    REQUIRE (ring.push (a.samples.data(), 8, a.sampleTime, a.sampleRate));

    CHECK (! ring.push (nullptr, 8, 111, 48000.0));
    CHECK (! ring.push (a.samples.data(), 0, 222, 48000.0));
    CHECK (! ring.push (nullptr, 0, 333, 48000.0));

    // The ring was neither full nor disabled, so these are not overruns.
    CHECK_EQ (ring.overrunCount(), uint64_t { 0 });
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 1 });

    jam::AnalysisFrame out;
    REQUIRE (ring.pop (out));
    checkFrame (out, a);

    // Still fully usable afterwards.
    const ExpectedFrame b = makeExpected (1, 8);
    REQUIRE (ring.push (b.samples.data(), 8, b.sampleTime, b.sampleRate));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 1 });

    REQUIRE (ring.pop (out));
    checkFrame (out, b);
    CHECK (! ring.pop (out));
}

//==============================================================================
// 13. reset().

JAM_TEST (AnalysisAudioRing, resetMakesTheRingEmptyAgain)
{
    jam::AnalysisAudioRing ring (4);

    std::vector<ExpectedFrame> queued;
    for (int i = 0; i < 4; ++i)
    {
        const ExpectedFrame e = makeExpected (i, 16);
        queued.push_back (e);
        REQUIRE (ring.push (e.samples.data(), 16, e.sampleTime, e.sampleRate));
    }

    // Fill the ring, then force one drop so the counter is non-zero going in.
    const ExpectedFrame rejected = makeExpected (9, 16);
    CHECK (! ring.push (rejected.samples.data(), 16, rejected.sampleTime, rejected.sampleRate));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 4 });

    ring.reset();

    // Queued blocks are abandoned, which is safe: analysis data is disposable.
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });

    jam::AnalysisFrame out;
    CHECK (! ring.pop (out));

    // reset() is a positions-only operation. It does NOT clear the overrun
    // counter (AnalysisAudioRing.h lines 124-128 store 0 into writePos_ and
    // readPos_ only), so the total stays monotonic across device restarts, which
    // is what a diagnostics counter wants. See task-notes/MOD-001.md.
    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });

    // Storage and capacity survive: the header calls reset() "Stops accepting
    // data", but the implementation leaves the slots live and keeps accepting
    // pushes. Asserted as implemented and flagged as a documentation
    // discrepancy in task-notes/MOD-001.md.
    CHECK (ring.valid());
    CHECK_EQ (ring.capacity(), std::size_t { 4 });

    REQUIRE (ring.push (queued[0].samples.data(), 16, queued[0].sampleTime, queued[0].sampleRate));
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 1 });
    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });

    REQUIRE (ring.pop (out));
    checkFrame (out, queued[0]);
}

//==============================================================================
// 14. pushedBlocks() reports current occupancy.

JAM_TEST (AnalysisAudioRing, pushedBlocksReportsCurrentOccupancy)
{
    constexpr std::size_t kCapacity = 4;
    jam::AnalysisAudioRing ring (kCapacity);

    const ExpectedFrame e = makeExpected (0, 32);

    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });

    for (std::size_t i = 0; i < kCapacity; ++i)
    {
        REQUIRE (ring.push (e.samples.data(), 32, e.sampleTime, e.sampleRate));
        CHECK_EQ (ring.pushedBlocks(), static_cast<uint64_t> (i + 1));
    }

    // Occupancy is bounded by capacity even when the producer keeps pushing.
    CHECK (! ring.push (e.samples.data(), 32, e.sampleTime, e.sampleRate));
    CHECK_EQ (ring.pushedBlocks(), static_cast<uint64_t> (kCapacity));
    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });

    for (std::size_t i = kCapacity; i > 0; --i)
    {
        jam::AnalysisFrame out;
        REQUIRE (ring.pop (out));
        CHECK_EQ (ring.pushedBlocks(), static_cast<uint64_t> (i - 1));
    }

    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });
    CHECK_EQ (ring.overrunCount(), uint64_t { 1 });   // consumer activity never counts
}

//==============================================================================
// 15. Long deterministic stress run, executed twice and compared.

namespace
{

constexpr std::size_t kStressCapacity = 7;
constexpr int kStressOperations = 10000;
constexpr uint32_t kStressSeed = 0x1a2b3c4du;

struct StressResult
{
    uint64_t pushes = 0;
    uint64_t pops = 0;
    uint64_t drops = 0;
    uint64_t maxOccupancy = 0;
    uint64_t payloadChecksum = 0;
    uint64_t overrunCount = 0;
    uint64_t clampedPushes = 0;
};

/** Drives the ring for 10 000 operations against a 7-slot buffer using a
    fixed-seed LCG: roughly half the operations are pushes, about a quarter of
    those arrive with no room and must be refused, and a deterministic subset
    arrives oversized so the clamp path is exercised too.

    A shadow FIFO of expected frames is maintained alongside the ring. Every pop
    is compared against the shadow by exact content hash, so any lost,
    duplicated, reordered or corrupted frame fails the run.
*/
StressResult runStressSequence()
{
    jam::AnalysisAudioRing ring (kStressCapacity);
    StressResult result;

    Lcg rng (kStressSeed);
    std::vector<ExpectedFrame> shadow;
    uint64_t nextSampleTime = 0;

    for (int op = 0; op < kStressOperations; ++op)
    {
        const uint32_t roll = rng.next();

        // Note: the low bits of an LCG have very short periods (bit 0 simply
        // alternates), so the push/pop decision and the clamp trigger are taken
        // from the high, long-period part of the word. Mixing them in one value
        // would also make the two decisions correlated.
        const bool wantsToPush = (roll % 100u) < 55u;

        uint32_t requested = 1u + ((roll >> 8) % kMaxBlock);
        if ((roll & 0x80000000u) != 0u)
            requested += kMaxBlock;                    // deterministic clamp path

        std::vector<float> source (requested);
        for (uint32_t i = 0; i < requested; ++i)
            source[i] = sampleValue (op, static_cast<int> (i));

        if (wantsToPush)
        {
            const uint32_t clamped = std::min (requested, kMaxBlock);
            const uint64_t sampleTime = nextSampleTime;
            const double sampleRate = rateFor (op);

            const bool shouldAccept = shadow.size() < kStressCapacity;
            const bool accepted = ring.push (source.data(), requested, sampleTime, sampleRate);
            CHECK_EQ (accepted, shouldAccept);

            if (accepted)
            {
                ++result.pushes;
                nextSampleTime += clamped;

                ExpectedFrame e;
                e.sampleTime = sampleTime;
                e.sampleRate = sampleRate;
                e.samples.assign (source.begin(), source.begin() + clamped);
                shadow.push_back (e);

                if (clamped != requested)
                    ++result.clampedPushes;
            }
            else
            {
                ++result.drops;
            }
        }
        else
        {
            jam::AnalysisFrame out;
            const bool shouldGive = ! shadow.empty();
            const bool got = ring.pop (out);
            CHECK_EQ (got, shouldGive);

            if (got)
            {
                ++result.pops;

                // A frame came back, so one was owed. CHECK rather than REQUIRE:
                // REQUIRE expands to a bare `return`, which is illegal in a
                // non-void helper, and the guard below must not read an empty
                // shadow if the ring ever invented a frame.
                const bool owedOne = ! shadow.empty();
                CHECK (owedOne);

                if (! owedOne)
                    return result;

                const uint64_t expectedHash = hashFrame (shadow.front());
                CHECK_EQ (hashFrame (out), expectedHash);

                result.payloadChecksum = result.payloadChecksum * kFnvPrime + expectedHash;
                shadow.erase (shadow.begin());
            }
        }

        // The ring's view of occupancy must match the shadow at all times.
        const uint64_t occupancy = ring.pushedBlocks();
        CHECK_EQ (occupancy, static_cast<uint64_t> (shadow.size()));
        CHECK_LE (occupancy, static_cast<uint64_t> (kStressCapacity));
        result.maxOccupancy = std::max (result.maxOccupancy, occupancy);
    }

    // Drain whatever is left; the order must still be FIFO.
    jam::AnalysisFrame out;
    while (ring.pop (out))
    {
        ++result.pops;
        const bool owedOne = ! shadow.empty();
        CHECK (owedOne);

        if (! owedOne)
            return result;

        CHECK_EQ (hashFrame (out), hashFrame (shadow.front()));
        result.payloadChecksum = result.payloadChecksum * kFnvPrime + hashFrame (shadow.front());
        shadow.erase (shadow.begin());
    }

    // Every accepted push came back out exactly once, and every refused push was
    // counted once. `pushes` counts accepted pushes only, so the conservation
    // law is pushes == pops, not pushes == pops + drops.
    CHECK (shadow.empty());
    CHECK_EQ (ring.pushedBlocks(), uint64_t { 0 });
    CHECK_EQ (ring.overrunCount(), result.drops);
    CHECK_EQ (result.pushes, result.pops);

    result.overrunCount = ring.overrunCount();
    return result;
}

} // namespace

JAM_TEST (AnalysisAudioRing, stressSequenceIsExactlyReproducible)
{
    const StressResult first = runStressSequence();
    const StressResult second = runStressSequence();

    // Same seed, same operations, same final state: there is no hidden global
    // state in the ring and no dependence on allocation addresses or timing.
    CHECK_EQ (first.pushes, second.pushes);
    CHECK_EQ (first.pops, second.pops);
    CHECK_EQ (first.drops, second.drops);
    CHECK_EQ (first.clampedPushes, second.clampedPushes);
    CHECK_EQ (first.maxOccupancy, second.maxOccupancy);
    CHECK_EQ (first.payloadChecksum, second.payloadChecksum);
    CHECK_EQ (first.overrunCount, second.overrunCount);

    // ...and the run really did exercise the paths that matter, rather than
    // passing because it did nothing interesting.
    CHECK_GE (first.pushes, uint64_t { 1000 });
    CHECK_GE (first.pops, uint64_t { 1000 });
    CHECK (first.drops > 0);             // overflow happened
    CHECK (first.clampedPushes > 0);     // oversized input happened
    CHECK_EQ (first.maxOccupancy, static_cast<uint64_t> (kStressCapacity));
    CHECK (first.payloadChecksum != 0);

    // Pin the generator itself: if the LCG constants were ever "improved" the
    // stress sequence would silently change what this test covers.
    Lcg probe (kStressSeed);
    CHECK_EQ (probe.next(), 0xae2cb148u);
    CHECK_EQ (probe.next(), 0x335ea407u);
}

//==============================================================================
// Real-time contract: storage is preallocated, push()/pop() never touch the
// heap.

JAM_TEST (AnalysisAudioRing, storageIsPreallocatedAndPushPopNeverAllocate)
{
    // Everything the measured region touches is allocated before tracking starts,
    // so any allocation counted below is attributable to the ring.
    std::vector<float> block (kMaxBlock);
    for (uint32_t i = 0; i < kMaxBlock; ++i)
        block[i] = sampleValue (3, static_cast<int> (i));

    jam::AnalysisFrame out;

    constexpr std::size_t kCapacity = 4;

    // Counts are captured into locals and only asserted at the very end: a
    // failing CHECK_EQ builds a std::string for its message, and that string is
    // itself a heap allocation which would otherwise be counted as if the ring
    // had made it.
    uint64_t afterConstruction = 0;
    uint64_t afterUse = 0;
    uint64_t afterDestruction = 0;
    uint64_t bytesAtConstruction = 0;
    uint64_t overruns = 0;

    {
        AllocationScope scope;

        // Construction performs the single allocation that backs every slot.
        {
            jam::AnalysisAudioRing ring (kCapacity);
            afterConstruction = scope.count();
            bytesAtConstruction = scope.bytes();

            // 2000 rounds of six pushes against four slots, then five pops: every
            // round overflows twice. Not one of these may reach the heap, retry,
            // or block.
            for (int i = 0; i < 2000; ++i)
            {
                for (int j = 0; j < 6; ++j)
                    ring.push (block.data(), static_cast<uint32_t> (1 + (i % 64)), static_cast<uint64_t> (i), 48000.0);

                for (int j = 0; j < 5; ++j)
                    ring.pop (out);

                overruns = ring.overrunCount();
            }

            afterUse = scope.count();
        }

        // Destruction of the slot storage is allocation-free too.
        afterDestruction = scope.count();
    }

    CHECK_EQ (afterConstruction, uint64_t { 1 });
    CHECK_GE (bytesAtConstruction, static_cast<uint64_t> (kCapacity) * kMaxBlock * sizeof (float));
    CHECK_EQ (afterUse, afterConstruction);
    CHECK_EQ (afterDestruction, afterConstruction);

    // The loop really did overflow (and really did push and pop).
    CHECK (overruns > 0);
    CHECK_EQ (overruns, uint64_t { 4000 });
}

//==============================================================================
// pop() must not copy more than numSamples.

JAM_TEST (AnalysisAudioRing, popCopiesOnlyTheValidSamples)
{
    jam::AnalysisAudioRing ring (2);

    const ExpectedFrame e = makeExpected (0, 4);
    REQUIRE (ring.push (e.samples.data(), 4, e.sampleTime, e.sampleRate));

    // Poison the destination: anything the ring does not copy must survive.
    jam::AnalysisFrame out;
    for (std::size_t i = 0; i < jam::kMaxAnalysisBlock; ++i)
        out.samples[i] = -12345.0f;

    REQUIRE (ring.pop (out));
    checkFrame (out, e);

    // The consumer is expected to honour numSamples; a full-array copy would let
    // stale bytes masquerade as audio, so the tail must be untouched.
    for (std::size_t i = 4; i < jam::kMaxAnalysisBlock; ++i)
        CHECK_EQ (out.samples[i], -12345.0f);
}

//==============================================================================
// The frozen frame type itself.

JAM_TEST (AnalysisAudioRing, analysisFrameHasAFixedBoundedPayload)
{
    // SPEC 8.1 / RhythmTypes.h: the block maximum is a compile-time constant
    // chosen to exceed any supported device block, and the frame carries its
    // samples inline so the queue payload is bounded and predictable.
    CHECK_EQ (jam::kMaxAnalysisBlock, std::size_t { 2048 });
    CHECK_EQ (sizeof (jam::AnalysisFrame::samples), 2048u * sizeof (float));

    // Crossing the audio-thread boundary, the frame must stay a plain value type
    // that memcpy/move cannot trap.
    CHECK (std::is_trivially_copyable_v<jam::AnalysisFrame>);
    CHECK (std::is_standard_layout_v<jam::AnalysisFrame>);
}