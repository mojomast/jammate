// RT signalling contracts from SPEC.md 7.1, 8.2, 8.3 and 18.1.
//
// Concurrency tests have fixed operation counts, joined threads, no sleeps and
// no randomness. They assert payload coherence / order, never an interleaving
// or a timing threshold. Worker threads record results locally; only the main
// thread touches the deliberately single-threaded JamTest harness.
//
// Allocation proof is structural: RtSignal.h has only inline byte storage and
// always-lock-free atomics, and never calls user-defined payload operations.
// AnalysisAudioRingTests.cpp already replaces global new/delete and hides its
// counters in an anonymous namespace. Defining replacements here would clash;
// pretending those counters can be extern'd would not measure anything.

#include "JamTest.h"

#include "rt/RtSignal.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <type_traits>

namespace
{

struct Snapshot
{
    uint64_t generation;
    uint64_t complement;
    uint64_t sampleTime;
    double bpm;
    float confidence;
    uint32_t flags;
    uint64_t lanes[4];
};

Snapshot snapshotFor (uint64_t generation) noexcept
{
    Snapshot result {};
    result.generation = generation;
    result.complement = ~generation;
    result.sampleTime = generation * 128 + 7;
    result.bpm = 60.0 + static_cast<double> (generation);
    result.confidence = static_cast<float> (generation % 17) * 0.0625f;
    result.flags = static_cast<uint32_t> (generation) ^ uint32_t { 0xa53cc35a };
    for (std::size_t i = 0; i < 4; ++i)
        result.lanes[i] = generation ^ (uint64_t { 0x9e3779b97f4a7c15 } * (i + 1));
    return result;
}

bool sameSnapshot (const Snapshot& actual, const Snapshot& expected) noexcept
{
    if (actual.generation != expected.generation
        || actual.complement != expected.complement
        || actual.sampleTime != expected.sampleTime
        || actual.bpm != expected.bpm
        || actual.confidence != expected.confidence
        || actual.flags != expected.flags)
        return false;

    for (std::size_t i = 0; i < 4; ++i)
        if (actual.lanes[i] != expected.lanes[i])
            return false;
    return true;
}

struct Command
{
    uint64_t sequence;
    uint64_t complement;
    uint32_t kind;
};

Command commandFor (uint64_t sequence) noexcept
{
    return { sequence, ~sequence, static_cast<uint32_t> (sequence % 5) };
}

bool sameCommand (const Command& actual, const Command& expected) noexcept
{
    return actual.sequence == expected.sequence
        && actual.complement == expected.complement
        && actual.kind == expected.kind;
}

// Atomic payload bytes also allow a trivially-copyable value whose ordinary
// default constructor is deleted. No hidden T temporary or constructor is used.
struct NoDefault
{
    NoDefault() = delete;
    explicit NoDefault (uint64_t v) noexcept : value (v) {}
    uint64_t value;
};

using Latest = jam::rt::LatestValue<Snapshot>;
using Queue = jam::rt::CommandQueue<Command, 3>;

static_assert (std::is_trivially_copyable_v<Snapshot>);
static_assert (std::is_nothrow_copy_constructible_v<Snapshot>);
static_assert (std::is_trivially_copyable_v<Command>);
static_assert (std::is_nothrow_copy_constructible_v<Command>);
static_assert (std::is_trivially_copyable_v<NoDefault>);
static_assert (std::is_nothrow_copy_constructible_v<NoDefault>);
static_assert (Latest::readAttemptLimit == 1);
static_assert (std::is_nothrow_default_constructible_v<jam::rt::SignalFlag>);
static_assert (std::is_nothrow_default_constructible_v<Latest>);
static_assert (std::is_nothrow_default_constructible_v<Queue>);
static_assert (alignof (jam::rt::SignalFlag) >= 64);
static_assert (alignof (Latest) >= 64);
static_assert (alignof (Queue) >= 64);
static_assert (! std::is_copy_constructible_v<jam::rt::SignalFlag>);
static_assert (! std::is_copy_constructible_v<Latest>);
static_assert (! std::is_copy_constructible_v<Queue>);

static_assert (noexcept (static_cast<jam::rt::SignalFlag*> (nullptr)->signal()));
static_assert (noexcept (static_cast<jam::rt::SignalFlag*> (nullptr)->consume()));
static_assert (noexcept (static_cast<const jam::rt::SignalFlag*> (nullptr)->pending()));
static_assert (noexcept (static_cast<Latest*> (nullptr)->publish (
    *static_cast<const Snapshot*> (nullptr))));
static_assert (noexcept (static_cast<const Latest*> (nullptr)->tryRead (
    *static_cast<Snapshot*> (nullptr))));
static_assert (noexcept (static_cast<Latest*> (nullptr)->invalidate()));
static_assert (noexcept (static_cast<Queue*> (nullptr)->push (
    *static_cast<const Command*> (nullptr))));
static_assert (noexcept (static_cast<Queue*> (nullptr)->pop (
    *static_cast<Command*> (nullptr))));
static_assert (noexcept (static_cast<const Queue*> (nullptr)->droppedCount()));

} // namespace

JAM_TEST (RtSignal, emptySignalAndSingleEdge)
{
    jam::rt::SignalFlag flag;
    CHECK (! flag.pending());
    CHECK (! flag.consume());
    flag.signal();
    CHECK (flag.pending());
    CHECK (flag.pending());       // Observation must not consume.
    CHECK (flag.consume());
    CHECK (! flag.pending());
    CHECK (! flag.consume());
    CHECK (! flag.consume());     // Clearing is idempotent.
    flag.signal();
    CHECK (flag.consume());       // A fresh edge works after clearing.
    CHECK (! flag.consume());
}

JAM_TEST (RtSignal, tenThousandSignalsCoalesceToExactlyOneEdge)
{
    jam::rt::SignalFlag flag;
    for (int i = 0; i < 10000; ++i)
        flag.signal();

    uint64_t consumedEdges = 0;
    CHECK (flag.pending());
    if (flag.consume())
        ++consumedEdges;
    CHECK_EQ (consumedEdges, uint64_t { 1 });
    CHECK (! flag.pending());
    for (int i = 0; i < 10000; ++i)
        if (flag.consume())
            ++consumedEdges;
    CHECK_EQ (consumedEdges, uint64_t { 1 });
}

JAM_TEST (RtSignal, signalHandoffPublishesPrecedingState)
{
    jam::rt::SignalFlag flag;
    uint64_t state = 0;
    bool consumed = false;
    uint64_t observed = 0;

    std::thread writer ([&] {
        state = 42;
        flag.signal();
    });
    std::thread reader ([&] {
        // No waiting loop. If the one attempt wins the edge, the release /
        // acquire handoff must make the preceding (now stable) state visible.
        consumed = flag.consume();
        if (consumed)
            observed = state;
    });
    writer.join();
    reader.join();

    if (consumed)
        CHECK_EQ (observed, uint64_t { 42 });
    else
        CHECK (flag.consume());
    CHECK (! flag.consume());
}

JAM_TEST (RtSignal, uninitialisedLatestReadLeavesOutputUntouched)
{
    Latest latest;
    Snapshot out = snapshotFor (999);
    CHECK (! latest.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (999)));
    latest.invalidate();
    CHECK (! latest.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (999)));
}

JAM_TEST (RtSignal, latestPublicationIsReadExactlyOnce)
{
    Latest latest;
    const Latest& reader = latest;
    Snapshot out {};
    latest.publish (snapshotFor (7));
    REQUIRE (reader.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (7)));
    CHECK (! reader.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (7)));
    latest.publish (snapshotFor (8));
    REQUIRE (reader.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (8)));
    CHECK (! reader.tryRead (out));
}

JAM_TEST (RtSignal, newestUnreadPublicationWinsWithoutAQueue)
{
    Latest latest;
    latest.publish (snapshotFor (1));
    latest.publish (snapshotFor (2));
    Snapshot out {};
    REQUIRE (latest.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (2)));
    CHECK (! latest.tryRead (out));

    for (uint64_t i = 3; i <= 10000; ++i)
        latest.publish (snapshotFor (i));
    REQUIRE (latest.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (10000)));
    CHECK (! latest.tryRead (out));
}

JAM_TEST (RtSignal, invalidationForcesOneRereadAndCoalesces)
{
    Latest latest;
    latest.publish (snapshotFor (12));
    Snapshot out {};
    REQUIRE (latest.tryRead (out));
    CHECK (! latest.tryRead (out));
    for (int i = 0; i < 10000; ++i)
        latest.invalidate();
    REQUIRE (latest.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (12)));
    CHECK (! latest.tryRead (out));

    latest.invalidate();
    latest.publish (snapshotFor (13));
    REQUIRE (latest.tryRead (out));
    CHECK (sameSnapshot (out, snapshotFor (13)));
    CHECK (! latest.tryRead (out));
}

JAM_TEST (RtSignal, representationsNeedNoPayloadConstruction)
{
    jam::rt::LatestValue<NoDefault> latest;
    const NoDefault source (19);
    NoDefault out (0);
    latest.publish (source);
    REQUIRE (latest.tryRead (out));
    CHECK_EQ (out.value, uint64_t { 19 });

    jam::rt::CommandQueue<NoDefault, 1> queue;
    REQUIRE (queue.push (source));
    out.value = 0;
    REQUIRE (queue.pop (out));
    CHECK_EQ (out.value, uint64_t { 19 });
}

JAM_TEST (RtSignal, concurrentLatestReadsNeverAcceptTornValues)
{
    constexpr uint64_t kPublications = 100000;
    constexpr uint64_t kReadCalls = 150000;
    Latest latest;
    latest.publish (snapshotFor (1));
    Snapshot out {};
    REQUIRE (latest.tryRead (out));
    uint64_t mixedValues = 0;
    uint64_t regressedValues = 0;
    uint64_t changedOnFailure = 0;

    std::thread writer ([&] {
        for (uint64_t i = 2; i <= kPublications + 1; ++i)
            latest.publish (snapshotFor (i));
    });
    std::thread reader ([&] {
        uint64_t previousGeneration = 1;
        for (uint64_t i = 0; i < kReadCalls; ++i)
        {
            const Snapshot previous = out;
            if (latest.tryRead (out))
            {
                if (out.generation < 1 || out.generation > kPublications + 1
                    || ! sameSnapshot (out, snapshotFor (out.generation)))
                    ++mixedValues;
                if (out.generation <= previousGeneration)
                    ++regressedValues;
                previousGeneration = out.generation;
            }
            else if (! sameSnapshot (out, previous))
            {
                ++changedOnFailure;
            }
        }
    });
    writer.join();
    reader.join();

    CHECK_EQ (mixedValues, uint64_t { 0 });
    CHECK_EQ (regressedValues, uint64_t { 0 });
    CHECK_EQ (changedOnFailure, uint64_t { 0 });
    CHECK_EQ (Latest::readAttemptLimit, std::size_t { 1 });
    // Either the reader already took the final generation, or this serial poll
    // takes it. No assertion depends on which thread the scheduler ran first.
    latest.tryRead (out);
    CHECK (sameSnapshot (out, snapshotFor (kPublications + 1)));
    CHECK (! latest.tryRead (out));
}

JAM_TEST (RtSignal, concurrentInvalidationForcesACompletedValueToBeReread)
{
    Latest latest;
    latest.publish (snapshotFor (22));
    Snapshot out {};
    REQUIRE (latest.tryRead (out));
    uint64_t mixedValues = 0;

    std::thread lifecycle ([&] {
        for (int i = 0; i < 10000; ++i)
            latest.invalidate();
    });
    std::thread reader ([&] {
        for (int i = 0; i < 10000; ++i)
            if (latest.tryRead (out) && ! sameSnapshot (out, snapshotFor (22)))
                ++mixedValues;
    });
    lifecycle.join();
    reader.join();

    CHECK_EQ (mixedValues, uint64_t { 0 });
    latest.tryRead (out);
    CHECK (sameSnapshot (out, snapshotFor (22)));
    CHECK (! latest.tryRead (out));
    latest.invalidate();
    CHECK (latest.tryRead (out));
    CHECK (! latest.tryRead (out));
}

JAM_TEST (RtSignal, commandQueueDropsIncomingAndPreservesFifo)
{
    Queue queue;
    CHECK_EQ (queue.capacity(), std::size_t { 3 });
    Command out = commandFor (999);
    CHECK (! queue.pop (out));
    CHECK (sameCommand (out, commandFor (999)));
    for (uint64_t i = 1; i <= 3; ++i)
        REQUIRE (queue.push (commandFor (i)));
    CHECK (! queue.push (commandFor (4)));
    CHECK (! queue.push (commandFor (5)));
    CHECK_EQ (queue.droppedCount(), uint64_t { 2 });
    for (uint64_t i = 1; i <= 3; ++i)
    {
        REQUIRE (queue.pop (out));
        CHECK (sameCommand (out, commandFor (i)));
    }
    CHECK (! queue.pop (out));
    CHECK (sameCommand (out, commandFor (3)));
    CHECK_EQ (queue.droppedCount(), uint64_t { 2 });
    REQUIRE (queue.push (commandFor (6)));
    REQUIRE (queue.pop (out));
    CHECK (sameCommand (out, commandFor (6)));
}

JAM_TEST (RtSignal, commandQueueZeroCapacityCountsEveryDrop)
{
    jam::rt::CommandQueue<Command, 0> queue;
    CHECK_EQ (queue.capacity(), std::size_t { 0 });
    Command out = commandFor (99);
    for (uint64_t i = 0; i < 10000; ++i)
        CHECK (! queue.push (commandFor (i)));
    CHECK_EQ (queue.droppedCount(), uint64_t { 10000 });
    CHECK (! queue.pop (out));
    CHECK (sameCommand (out, commandFor (99)));
}

JAM_TEST (RtSignal, commandQueueWrapsSlotsWithoutLosingCommands)
{
    Queue queue;
    Command out {};
    for (uint64_t round = 0; round < 10000; ++round)
    {
        for (uint64_t i = 0; i < 3; ++i)
            REQUIRE (queue.push (commandFor (round * 3 + i)));
        for (uint64_t i = 0; i < 3; ++i)
        {
            REQUIRE (queue.pop (out));
            CHECK (sameCommand (out, commandFor (round * 3 + i)));
        }
        CHECK (! queue.pop (out));
    }
    CHECK_EQ (queue.droppedCount(), uint64_t { 0 });
}

JAM_TEST (RtSignal, concurrentCommandsStayCoherentAndOrderedWithCountedDrops)
{
    constexpr uint64_t kCommands = 100000;
    jam::rt::CommandQueue<Command, 7> queue;
    uint64_t accepted = 0;
    uint64_t dropped = 0;
    uint64_t popped = 0;
    uint64_t corruptOrUnordered = 0;
    uint64_t lastSequence = 0;

    std::thread producer ([&] {
        for (uint64_t i = 1; i <= kCommands; ++i)
            if (queue.push (commandFor (i)))
                ++accepted;
            else
                ++dropped;
    });
    std::thread consumer ([&] {
        Command out {};
        for (uint64_t i = 0; i < kCommands; ++i)
            if (queue.pop (out))
            {
                ++popped;
                if (out.sequence <= lastSequence || out.sequence > kCommands
                    || ! sameCommand (out, commandFor (out.sequence)))
                    ++corruptOrUnordered;
                lastSequence = out.sequence;
            }
    });
    producer.join();
    consumer.join();

    // At most capacity commands remain. Drain with a fixed bound, not a
    // producer-dependent wait loop, then prove conservation and drop accounting.
    Command out {};
    for (std::size_t i = 0; i < queue.capacity(); ++i)
        if (queue.pop (out))
        {
            ++popped;
            if (out.sequence <= lastSequence || out.sequence > kCommands
                || ! sameCommand (out, commandFor (out.sequence)))
                ++corruptOrUnordered;
            lastSequence = out.sequence;
        }
    CHECK (! queue.pop (out));
    CHECK_EQ (corruptOrUnordered, uint64_t { 0 });
    CHECK_EQ (accepted, popped);
    CHECK_EQ (accepted + dropped, kCommands);
    CHECK_EQ (queue.droppedCount(), dropped);
}
