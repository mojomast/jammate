// Deterministic unit tests for jam::DrumTransportAdapter — the boundary-
// quantising, policy-free drum transport seam (DEVPLAN MOD-002).
//
// Every scenario below asserts a MUSICAL or SAFETY property, not merely that a
// method was called:
//
//   1. position is a pure function of the integer sample clock and one bar is
//      exactly one bar;
//   2. a queued groove change never lands mid-bar and lands exactly on the bar
//      boundary (SPEC.md product principle 4);
//   3. two changes for one boundary are consumed exactly once, deterministically,
//      with a stale-generation guard;
//   4. overflow drops the newest pending change and counts it (SPEC.md 8.3);
//   5. an explicit fill request lands on a boundary;
//   6. resync-next-beat establishes the phase without moving the bar counter;
//   7. resync-next-bar establishes a downbeat without moving the bar counter;
//   8. stop-at-next-bar stops on a boundary, leaves position inspectable, and
//      disposes of a pending change by the documented stop policy;
//   9. an absurd clock BPM is clamped by an explicit configured rail;
//  10. 30 000 blocks do not drift from the closed-form sample-time expectation;
//  11. the same call sequence yields an identical event log;
//  12. the adapter satisfies IDrumTransport and a third party can implement the
//      interface too (a recording fake proves it).
//
// Determinism policy (tests/jam/JamTest.h): no sleeping, no wall-clock, no
// threads, no file I/O, no unseeded randomness.

#include "JamTest.h"

#include "jam/DrumTransportAdapter.h"
#include "jam/RhythmTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

using namespace jam;

constexpr double kSr = 48000.0;

// One bar at 120 BPM / 4-4 is exactly 96000 samples (24000 per beat), which
// keeps the boundary assertions exact rather than tolerance-based.
constexpr std::uint64_t kBeat120 = 24000;
constexpr std::uint64_t kBar120 = 4 * kBeat120;

DrumTransportAdapter makeStarted (double bpm = 120.0, int beatsPerBar = 4,
                                  const TransportConfig& config = {})
{
    DrumTransportAdapter adapter (config);
    adapter.prepare (kSr, 512);
    adapter.startTransport (bpm, beatsPerBar, 4);
    return adapter;
}

ClockSnapshot makeLockedSnapshot (double bpm,
                                   ClockLockState state = ClockLockState::Locked,
                                   bool frozen = false)
{
    ClockSnapshot snapshot;
    snapshot.bpm = bpm;
    snapshot.beatsPerBar = 4;
    snapshot.beatUnit = 4;
    snapshot.lockState = state;
    snapshot.tempoFrozen = frozen;
    return snapshot;
}

// A third-party implementation of the frozen interface. It records the exact
// order and arguments of every call so a test can make ordering assertions;
// compiling it proves IDrumTransport is implementable outside the adapter.
class RecordingTransport final : public IDrumTransport
{
public:
    void prepare (double sampleRate, int maximumBlockSize) override
    {
        calls_.emplace_back ("prepare");
        sampleRate_ = sampleRate;
        maximumBlockSize_ = maximumBlockSize;
    }

    void startTransport (double bpm, int beatsPerBar, int beatUnit) override
    {
        calls_.emplace_back ("startTransport");
        bpm_ = bpm;
        beatsPerBar_ = beatsPerBar;
        beatUnit_ = beatUnit;
    }

    void stopTransport() override { calls_.emplace_back ("stopTransport"); }

    void setClockTempo (double bpm) override
    {
        calls_.emplace_back ("setClockTempo");
        bpm_ = bpm;
    }

    void requestResyncNextBeat (std::uint64_t targetSampleTime) override
    {
        calls_.emplace_back ("requestResyncNextBeat");
        target_ = targetSampleTime;
    }

    void requestResyncNextBar (std::uint64_t targetSampleTime) override
    {
        calls_.emplace_back ("requestResyncNextBar");
        target_ = targetSampleTime;
    }

    void queueBarChange (const QueuedBarChange& change) override
    {
        calls_.emplace_back ("queueBarChange");
        lastChange_ = change;
    }

    void requestFillAtNextBar (LibraryIndex fill) override
    {
        calls_.emplace_back ("requestFillAtNextBar");
        lastFill_ = fill;
    }

    void requestStopAtNextBar() override { calls_.emplace_back ("requestStopAtNextBar"); }

    TransportPosition position() const override
    {
        calls_.emplace_back ("position");
        return position_;
    }

    // mutable so the const position() override can still record its call.
    mutable std::vector<std::string> calls_;
    double sampleRate_ = 0.0;
    int maximumBlockSize_ = 0;
    double bpm_ = 0.0;
    int beatsPerBar_ = 0;
    int beatUnit_ = 0;
    std::uint64_t target_ = 0;
    QueuedBarChange lastChange_;
    LibraryIndex lastFill_ = kNoLibraryEntry;
    TransportPosition position_;
};

} // namespace

//==============================================================================
// 1. Position tracks the sample clock and the bar grid exactly.
//==============================================================================
JAM_TEST (DrumTransportAdapter, positionAdvancesFromSampleClock)
{
    auto adapter = makeStarted (120.0);

    TransportPosition p = adapter.position();
    CHECK_EQ (p.playing, true);
    CHECK_EQ (p.bar, 1);
    CHECK_EQ (p.beat, 1);
    CHECK_EQ (p.samplePosition, static_cast<std::uint64_t> (0));
    CHECK_NEAR (p.bpm, 120.0, 1e-9);

    adapter.advance (kBeat120); // exactly one beat
    p = adapter.position();
    CHECK_EQ (p.bar, 1);
    CHECK_EQ (p.beat, 2);
    CHECK_EQ (p.samplePosition, kBeat120);
    CHECK_NEAR (adapter.beatsElapsed(), 1.0, 1e-9);

    adapter.advance (3 * kBeat120); // rest of the bar
    p = adapter.position();
    CHECK_EQ (p.bar, 2);
    CHECK_EQ (p.beat, 1);
    CHECK_EQ (p.samplePosition, kBar120);
    CHECK_NEAR (adapter.beatsElapsed(), 4.0, 1e-9);

    // A change queued while stopped is held, then applied on the start downbeat.
    DrumTransportAdapter stopped;
    stopped.prepare (kSr, 512);
    QueuedBarChange held;
    held.generation = 1;
    held.groove = 7;
    stopped.queueBarChange (held);
    CHECK_EQ (stopped.appliedChange().groove, kNoLibraryEntry); // held, not applied
    stopped.startTransport (120.0, 4, 4);
    CHECK_EQ (stopped.appliedChange().groove, 7);               // applied at start
    CHECK_EQ (stopped.appliedBarChangeCount(), static_cast<std::uint64_t> (1));
}

//==============================================================================
// 2. A queued groove change applies at the exact bar boundary, never mid-bar.
//==============================================================================
JAM_TEST (DrumTransportAdapter, grooveChangeLandsOnExactBarBoundary)
{
    auto adapter = makeStarted (120.0);
    adapter.advance (kBeat120); // part-way through bar 1

    QueuedBarChange change;
    change.generation = 1;
    change.groove = 42;
    adapter.queueBarChange (change);
    CHECK_EQ (adapter.pendingChangeCount(), 1);

    // Two more beats: still bar 1, so the change must NOT have taken effect.
    adapter.advance (2 * kBeat120);
    CHECK_EQ (adapter.appliedChange().groove, kNoLibraryEntry);
    CHECK_EQ (adapter.position().bar, 1);

    // One sample before the boundary: still not applied.
    adapter.advance (kBeat120 - 1);
    CHECK_EQ (adapter.position().bar, 1);
    CHECK_EQ (adapter.appliedChange().groove, kNoLibraryEntry);

    // The boundary sample itself: applied, exactly there.
    adapter.advance (1);
    CHECK_EQ (adapter.position().bar, 2);
    CHECK_EQ (adapter.appliedChange().groove, 42);
    CHECK_EQ (adapter.pendingChangeCount(), 0);
    CHECK_EQ (adapter.appliedBarChangeCount(), static_cast<std::uint64_t> (1));
    CHECK_EQ (adapter.lastBoundarySample(), kBar120);

    // Once consumed it is never re-applied by further time passing.
    adapter.advance (kBar120);
    CHECK_EQ (adapter.appliedBarChangeCount(), static_cast<std::uint64_t> (1));
}

//==============================================================================
// 3. Two changes for the same boundary are consumed exactly once, last wins,
//    and a stale-generation change is dropped and counted.
//==============================================================================
JAM_TEST (DrumTransportAdapter, twoChangesOneBoundaryDeterministic)
{
    auto adapter = makeStarted (120.0);

    QueuedBarChange first;
    first.generation = 5;
    first.groove = 10;
    QueuedBarChange second;
    second.generation = 6;
    second.groove = 20;

    adapter.queueBarChange (first);
    adapter.queueBarChange (second);
    CHECK_EQ (adapter.pendingChangeCount(), 2);

    adapter.advance (kBar120);
    CHECK_EQ (adapter.appliedChange().groove, 20); // last-write-wins
    CHECK_EQ (adapter.appliedBarChangeCount(), static_cast<std::uint64_t> (1)); // applied once
    CHECK_EQ (adapter.consumedChangeCount(), static_cast<std::uint64_t> (2));   // both retired

    // A change stamped with an older generation than a pending one is stale.
    QueuedBarChange fresh;
    fresh.generation = 7;
    fresh.groove = 30;
    adapter.queueBarChange (fresh);

    QueuedBarChange stale;
    stale.generation = 6;
    stale.groove = 40;
    adapter.queueBarChange (stale);

    CHECK_EQ (adapter.staleDropCount(), static_cast<std::uint64_t> (1));
    CHECK_EQ (adapter.pendingChangeCount(), 1); // only the fresh one survives

    adapter.advance (kBar120);
    CHECK_EQ (adapter.appliedChange().groove, 30);
    CHECK_EQ (adapter.appliedBarChangeCount(), static_cast<std::uint64_t> (2));
    CHECK_EQ (adapter.consumedChangeCount(), static_cast<std::uint64_t> (3));
}

//==============================================================================
// 4. Overflow drops the newest pending change and counts it; queued entries
//    are left intact.
//==============================================================================
JAM_TEST (DrumTransportAdapter, overflowDropsNewestAndCounts)
{
    TransportConfig config;
    config.maxPendingChanges = 2;

    auto adapter = makeStarted (120.0, 4, config);

    for (int i = 0; i < 3; ++i)
    {
        QueuedBarChange change;
        change.generation = 0;
        change.groove = 100 + i;
        adapter.queueBarChange (change);
    }

    CHECK_EQ (adapter.overflowCount(), static_cast<std::uint64_t> (1));
    CHECK_EQ (adapter.pendingChangeCount(), 2);

    adapter.advance (kBar120);
    // FIFO: 100 then 101 are queued, the dropped 102 never applies; the
    // boundary effect is the last surviving change, applied once.
    CHECK_EQ (adapter.appliedChange().groove, 101);
    CHECK_EQ (adapter.appliedBarChangeCount(), static_cast<std::uint64_t> (1));
    CHECK_EQ (adapter.consumedChangeCount(), static_cast<std::uint64_t> (2));
}

//==============================================================================
// 5. An explicit fill request lands on the bar boundary.
//==============================================================================
JAM_TEST (DrumTransportAdapter, explicitFillLandsOnBoundary)
{
    auto adapter = makeStarted (120.0);
    adapter.advance (2 * kBeat120 + 123); // mid-bar

    adapter.requestFillAtNextBar (77);
    CHECK_EQ (adapter.appliedChange().fill, kNoLibraryEntry); // not yet
    CHECK_EQ (adapter.pendingChangeCount(), 1);

    adapter.advance (kBar120 - (2 * kBeat120 + 123)); // reach the boundary
    CHECK_EQ (adapter.appliedChange().fill, 77);
    CHECK_EQ (adapter.appliedFillCount(), static_cast<std::uint64_t> (1));
    CHECK_EQ (adapter.lastBoundarySample(), kBar120);
}

//==============================================================================
// 6. Resync-next-beat establishes zero phase without moving the bar counter.
//==============================================================================
JAM_TEST (DrumTransportAdapter, resyncNextBeatPreservesBarContinuity)
{
    auto adapter = makeStarted (120.0);
    adapter.advance (kBeat120 + 6000); // beats = 1.25

    const int barBefore = adapter.position().bar;
    const int beatBefore = adapter.position().beat;
    CHECK_EQ (barBefore, 1);
    CHECK_EQ (beatBefore, 2);

    const std::uint64_t target = adapter.samplePosition() + 6000; // beats = 1.5
    adapter.requestResyncNextBeat (target);
    adapter.advance (6000);

    CHECK_NEAR (adapter.beatsElapsed(), 1.0, 1e-9); // a beat onset sits on target
    CHECK_NEAR (adapter.beatPhase01(), 0.0, 1e-9);
    CHECK_EQ (adapter.position().bar, barBefore);   // no jump / rewind
    CHECK_EQ (adapter.position().beat, beatBefore);
}

//==============================================================================
// 7. Resync-next-bar establishes a downbeat without moving the bar counter.
//==============================================================================
JAM_TEST (DrumTransportAdapter, resyncNextBarEstablishesDownbeat)
{
    auto adapter = makeStarted (120.0);
    adapter.advance (kBeat120); // beats = 1.0

    const int barBefore = adapter.position().bar;
    CHECK_EQ (barBefore, 1);

    const std::uint64_t target = 40800; // beats = 1.7, barPhase = 0.425
    adapter.requestResyncNextBar (target);
    adapter.advance (target - adapter.samplePosition());

    CHECK_NEAR (adapter.barPhase01(), 0.0, 1e-9);   // downbeat on the boundary
    CHECK_EQ (adapter.position().beat, 1);
    CHECK_EQ (adapter.position().bar, barBefore);    // continuity preserved
}

//==============================================================================
// 8. Stop-at-next-bar stops on the boundary, leaves position inspectable, and
//    drops a pending change per the documented stop policy (stop wins).
//==============================================================================
JAM_TEST (DrumTransportAdapter, stopAtNextBarIsCleanAndInspectable)
{
    auto adapter = makeStarted (120.0);
    adapter.advance (kBeat120);

    QueuedBarChange change;
    change.generation = 1;
    change.groove = 55;
    adapter.queueBarChange (change);
    adapter.requestStopAtNextBar();

    adapter.advance (kBar120 - kBeat120); // reach the boundary

    CHECK_EQ (adapter.position().playing, false);
    CHECK_EQ (adapter.position().bar, 0);
    CHECK_EQ (adapter.position().beat, 0);
    CHECK_EQ (adapter.samplePosition(), kBar120); // inspectable, exact
    CHECK_EQ (adapter.appliedChange().groove, kNoLibraryEntry); // stop won
    CHECK_EQ (adapter.droppedAtStopCount(), static_cast<std::uint64_t> (1));
    CHECK_EQ (adapter.pendingChangeCount(), 0);
}

//==============================================================================
// 9. An absurd clock BPM is clamped by the explicitly configured rail; a
//    legitimate value passes through untouched.
//==============================================================================
JAM_TEST (DrumTransportAdapter, absurdBpmIsClampedByConfiguredRail)
{
    auto adapter = makeStarted (120.0);
    CHECK_NEAR (adapter.targetBpm(), 120.0, 1e-9);

    adapter.applyClock (makeLockedSnapshot (3.0));
    CHECK_NEAR (adapter.targetBpm(), 20.0, 1e-9); // min rail
    adapter.advance (1);
    CHECK_NEAR (adapter.position().bpm, 20.0, 1e-9);

    adapter.applyClock (makeLockedSnapshot (1000.0));
    CHECK_NEAR (adapter.targetBpm(), 400.0, 1e-9); // max rail
    adapter.advance (1);
    CHECK_NEAR (adapter.position().bpm, 400.0, 1e-9);

    adapter.applyClock (makeLockedSnapshot (400.0));
    CHECK_NEAR (adapter.targetBpm(), 400.0, 1e-9); // inside rail: unchanged
    adapter.advance (1);
    CHECK_NEAR (adapter.position().bpm, 400.0, 1e-9);

    adapter.applyClock (makeLockedSnapshot (96.0));
    adapter.advance (1);
    CHECK_NEAR (adapter.position().bpm, 96.0, 1e-9);

    // tempoFrozen pins the tempo against incoming clock beliefs.
    auto frozen = makeStarted (120.0);
    frozen.applyClock (makeLockedSnapshot (140.0, ClockLockState::Locked, true));
    CHECK_NEAR (frozen.targetBpm(), 120.0, 1e-9);
    CHECK_EQ (frozen.tempoIgnoredCount(), static_cast<std::uint64_t> (1));
    frozen.advance (kBeat120);
    CHECK_NEAR (frozen.position().bpm, 120.0, 1e-9);

    frozen.applyClock (makeLockedSnapshot (140.0, ClockLockState::Locked, false));
    frozen.advance (1);
    CHECK_NEAR (frozen.position().bpm, 140.0, 1e-9);

    // The tempo-only interface path clamps the same way.
    auto tempoOnly = makeStarted (120.0);
    tempoOnly.setClockTempo (123.0);
    CHECK_NEAR (tempoOnly.targetBpm(), 123.0, 1e-9);
}

//==============================================================================
// 9b. While Lost the grid is untrusted: a queued change is deferred, not lost.
//==============================================================================
JAM_TEST (DrumTransportAdapter, lostDefersChangesUntilRecovery)
{
    auto adapter = makeStarted (120.0);
    adapter.applyClock (makeLockedSnapshot (120.0, ClockLockState::Lost));
    adapter.advance (kBeat120);

    QueuedBarChange change;
    change.generation = 1;
    change.groove = 88;
    adapter.queueBarChange (change);

    adapter.advance (3 * kBeat120); // crosses the boundary at 96000 while Lost
    CHECK_EQ (adapter.appliedChange().groove, kNoLibraryEntry);
    CHECK_EQ (adapter.pendingChangeCount(), 1);
    CHECK_EQ (adapter.deferredBoundaryCount(), static_cast<std::uint64_t> (1));

    adapter.applyClock (makeLockedSnapshot (120.0, ClockLockState::Locked));
    adapter.advance (kBar120); // next trusted boundary
    CHECK_EQ (adapter.appliedChange().groove, 88);
}

//==============================================================================
// 10. Long run: 30 000 blocks match the closed-form sample-time expectation.
//     This is the anti-drift test; position is a pure function of sample time.
//==============================================================================
JAM_TEST (DrumTransportAdapter, thirtyThousandBlocksDoNotDrift)
{
    auto adapter = makeStarted (100.0);

    const std::uint64_t block = 512;
    const int blocks = 30000;
    for (int i = 0; i < blocks; ++i)
        adapter.advance (block);

    const double totalSamples = static_cast<double> (block) * static_cast<double> (blocks);
    const double expectedBeats = totalSamples * 100.0 / (60.0 * kSr);
    const long long expectedBeatIndex = static_cast<long long> (std::floor (expectedBeats));
    const int expectedBar = static_cast<int> (expectedBeatIndex / 4) + 1;
    const int expectedBeat = static_cast<int> (expectedBeatIndex % 4) + 1;

    CHECK_EQ (adapter.samplePosition(), static_cast<std::uint64_t> (totalSamples));
    CHECK_NEAR (adapter.beatsElapsed(), expectedBeats, 1e-6);
    CHECK_EQ (adapter.position().bar, expectedBar);
    CHECK_EQ (adapter.position().beat, expectedBeat);

    // 120 BPM over the same run is exact.
    auto exact = makeStarted (120.0);
    for (int i = 0; i < blocks; ++i)
        exact.advance (block);
    CHECK_NEAR (exact.beatsElapsed(), 640.0, 1e-9); // 15 360 000 / 24000
    CHECK_EQ (exact.position().bar, 161);
    CHECK_EQ (exact.position().beat, 1);
}

//==============================================================================
// 11. Determinism: the same call sequence yields an identical event log.
//==============================================================================
JAM_TEST (DrumTransportAdapter, identicalSequenceIdenticalEventLog)
{
    const auto run = []()
    {
        auto adapter = makeStarted (120.0);

        QueuedBarChange change;
        change.generation = 1;
        change.groove = 5;
        adapter.queueBarChange (change);
        adapter.advance (kBeat120);

        adapter.requestFillAtNextBar (9);
        adapter.advance (3 * kBeat120); // boundary at 96000

        adapter.applyClock (makeLockedSnapshot (140.0));
        adapter.advance (2 * kBeat120);

        adapter.requestResyncNextBeat (adapter.samplePosition() + 1000);
        adapter.advance (1000);

        adapter.requestStopAtNextBar();
        adapter.advance (kBar120);
        return adapter;
    };

    const DrumTransportAdapter a = run();
    const DrumTransportAdapter b = run();

    CHECK_EQ (a.totalEventCount(), b.totalEventCount());
    CHECK_EQ (a.eventCount(), b.eventCount());

    const std::size_t count = std::min (a.eventCount(), b.eventCount());
    for (std::size_t i = 0; i < count; ++i)
    {
        CHECK_EQ (a.event (i).sampleTime, b.event (i).sampleTime);
        CHECK_EQ (static_cast<int> (a.event (i).type), static_cast<int> (b.event (i).type));
        CHECK_EQ (a.event (i).index, b.event (i).index);
        CHECK_NEAR (a.event (i).value, b.event (i).value, 0.0);
    }

    CHECK (a.eventCount() > 0);
}

//==============================================================================
// 12. The adapter satisfies IDrumTransport; a third party can too, and records
//     the exact call order.
//==============================================================================
JAM_TEST (DrumTransportAdapter, satisfiesIDrumTransport)
{
    static_assert (std::is_base_of<IDrumTransport, DrumTransportAdapter>::value,
                   "DrumTransportAdapter must implement IDrumTransport");
    static_assert (! std::is_abstract<DrumTransportAdapter>::value,
                   "DrumTransportAdapter must be concrete");

    // Drive the adapter only through the frozen base pointer.
    DrumTransportAdapter adapter;
    IDrumTransport* base = &adapter;

    base->prepare (kSr, 512);

    QueuedBarChange change;
    change.generation = 1;
    change.groove = 12;
    base->startTransport (120.0, 4, 4);
    base->setClockTempo (120.0);
    base->queueBarChange (change);
    base->requestFillAtNextBar (3);
    base->requestResyncNextBeat (100);
    base->requestResyncNextBar (200);
    base->requestStopAtNextBar();

    const TransportPosition p = base->position();
    CHECK_EQ (p.playing, true);
    CHECK_EQ (p.bar, 1);

    base->stopTransport();
    CHECK_EQ (base->position().playing, false);

    // The recording fake proves the interface is implementable by a third party
    // and lets a test assert ordering, not just final state.
    RecordingTransport fake;
    IDrumTransport* fakeBase = &fake;
    fakeBase->prepare (kSr, 256);
    fakeBase->startTransport (120.0, 4, 4);
    fakeBase->setClockTempo (120.0);
    fake.queueBarChange (change);
    fakeBase->requestFillAtNextBar (3);
    fakeBase->requestStopAtNextBar();
    (void) fakeBase->position();

    CHECK_EQ (fake.calls_.size(), static_cast<std::size_t> (7));
    CHECK_EQ (fake.calls_[0], std::string ("prepare"));
    CHECK_EQ (fake.calls_[1], std::string ("startTransport"));
    CHECK_EQ (fake.calls_[2], std::string ("setClockTempo"));
    CHECK_EQ (fake.calls_[3], std::string ("queueBarChange"));
    CHECK_EQ (fake.calls_[4], std::string ("requestFillAtNextBar"));
    CHECK_EQ (fake.calls_[5], std::string ("requestStopAtNextBar"));
    CHECK_EQ (fake.calls_[6], std::string ("position"));
    CHECK_NEAR (fake.sampleRate_, kSr, 0.0);
    CHECK_EQ (fake.maximumBlockSize_, 256);
    CHECK_EQ (fake.lastChange_.groove, 12);
    CHECK_EQ (fake.lastFill_, 3);
}
