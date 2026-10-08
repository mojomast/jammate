// Portable, device-free tests for jam::DrumClockBridge — the explicit-clock
// command seam between the Musical Clock and the audio-thread DrumEngine
// (DEVPLAN DRUM-001 / INT-DRUM-001).
//
// These assert the mechanical contract only: the bridge derives its grid from
// an explicit absolute sample (never a free block counter), stages tempo to bar
// boundaries, publishes bounded POD commands, drops and counts overflow, detects
// discontinuities, ignores stale snapshots, and is deterministic. The actual
// DrumEngine integration lives in tests/DrumClockBridgeTests.cpp.

#include "JamTest.h"

#include "jam/DrumClockBridge.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
using namespace jam;

constexpr double kSr = 48000.0;

DrumClockBridgeConfig config120()
{
    DrumClockBridgeConfig config;
    config.initialBpm = 120.0;
    config.defaultSampleRate = kSr;
    return config;
}

// 120 BPM @ 48 kHz: one sixteenth is 6000 samples, one bar is 96000 samples.
constexpr std::uint64_t kBar120 = 96000;

DrumClockCommand popOne (DrumClockBridge& bridge)
{
    DrumClockCommand command;
    const bool ok = bridge.popCommand (command);
    CHECK (ok);
    return command;
}

ClockSnapshot lockedSnapshot (double bpm, std::uint64_t generation)
{
    ClockSnapshot snapshot;
    snapshot.bpm = bpm;
    snapshot.generation = generation;
    snapshot.beatsPerBar = 4;
    snapshot.beatUnit = 4;
    snapshot.lockState = ClockLockState::Locked;
    return snapshot;
}
} // namespace

//==============================================================================
JAM_TEST (DrumClockBridge, commandIsTriviallyCopyablePod)
{
    static_assert (std::is_trivially_copyable<DrumClockCommand>::value,
                   "DrumClockCommand must cross the audio boundary as raw bytes");
    static_assert (kDrumClockCommandCapacity >= 8,
                   "capacity must hold the small in-flight command set");
    CHECK_EQ (DrumClockCommandQueue::capacity(), kDrumClockCommandCapacity);
}

//==============================================================================
JAM_TEST (DrumClockBridge, prepareRejectsInvalidDomain)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (-1.0, 512);
    CHECK_EQ (bridge.isPrepared(), false);
    CHECK_EQ (bridge.requestJoinAtNextBar (0), false);
    CHECK_GE (bridge.invalidRequestCount(), static_cast<std::uint64_t> (2));

    bridge.prepare (kSr, 512);
    CHECK_EQ (bridge.isPrepared(), true);
    CHECK_EQ (bridge.requestJoinAtNextBar (0), true); // 0 = first Rock groove
}

//==============================================================================
JAM_TEST (DrumClockBridge, positionComesFromExplicitClockNotABlockCounter)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);

    bridge.setClockSample (0);
    bridge.setClockSample (24000); // exactly one beat at 120 BPM

    CHECK_NEAR (bridge.beatsAt (24000), 1.0, 1e-12);
    CHECK_EQ (bridge.samplePosition(), static_cast<std::uint64_t> (24000));

    bridge.requestJoinAtNextBar (0);
    const TransportPosition p = bridge.position();
    CHECK_EQ (p.playing, true);
    CHECK_EQ (p.bar, 1);
    CHECK_EQ (p.beat, 2);
    CHECK_NEAR (p.bpm, 120.0, 1e-12);

    // Jumping the explicit clock straight to the bar-2 downbeat moves position
    // there in one call: there is no block counter that has to catch up.
    bridge.setClockSample (kBar120);
    CHECK_NEAR (bridge.beatsAt (kBar120), 4.0, 1e-12);
    CHECK_EQ (bridge.position().bar, 2);
    CHECK_EQ (bridge.position().beat, 1);
}

//==============================================================================
JAM_TEST (DrumClockBridge, joinPublishesAtNextBarBoundary)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (30000); // mid bar-1

    CHECK (bridge.requestJoinAtNextBar (5));
    const DrumClockCommand command = popOne (bridge);
    CHECK_EQ (static_cast<int> (command.type),
              static_cast<int> (DrumClockCommandType::JoinAtBar));
    CHECK_EQ (command.sampleTime, kBar120);
    CHECK_EQ (command.groove, 5);
    CHECK_NEAR (command.bpm, 120.0, 1e-12);
    CHECK (command.sequence > 0);
}

//==============================================================================
JAM_TEST (DrumClockBridge, tempoIsStagedAndAppliedAtNextBarBoundary)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    bridge.applySnapshot (lockedSnapshot (150.0, 1));

    const DrumClockCommand command = popOne (bridge);
    CHECK_EQ (static_cast<int> (command.type),
              static_cast<int> (DrumClockCommandType::SetTempo));
    CHECK_EQ (command.sampleTime, kBar120);
    CHECK_NEAR (command.bpm, 150.0, 1e-12);

    // The grid still runs at the old tempo until the boundary is crossed.
    CHECK_NEAR (bridge.bpm(), 120.0, 1e-12);
    bridge.setClockSample (kBar120 - 1);
    CHECK_NEAR (bridge.bpm(), 120.0, 1e-12);

    bridge.setClockSample (kBar120);
    CHECK_NEAR (bridge.bpm(), 150.0, 1e-12);
    CHECK_NEAR (bridge.beatsAt (kBar120), 4.0, 1e-9); // phase continuous
}

//==============================================================================
JAM_TEST (DrumClockBridge, staleSnapshotIsIgnoredAndCounted)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    bridge.applySnapshot (lockedSnapshot (130.0, 7));
    popOne (bridge); // SetTempo for generation 7

    bridge.applySnapshot (lockedSnapshot (200.0, 7)); // stale generation
    bridge.applySnapshot (lockedSnapshot (200.0, 6)); // older still

    CHECK_EQ (bridge.staleSnapshotCount(), static_cast<std::uint64_t> (2));
    CHECK_NEAR (bridge.bpm(), 120.0, 1e-12); // stale beliefs never win

    DrumClockCommand command;
    CHECK_EQ (bridge.popCommand (command), false); // no third command published
}

//==============================================================================
JAM_TEST (DrumClockBridge, discontinuityClearsAndPublishesClear)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (10000);

    CHECK_EQ (bridge.setClockSample (5000), false); // backwards
    CHECK_EQ (bridge.discontinuityCount(), static_cast<std::uint64_t> (1));

    const DrumClockCommand command = popOne (bridge);
    CHECK_EQ (static_cast<int> (command.type),
              static_cast<int> (DrumClockCommandType::Clear));
    CHECK_EQ (command.sampleTime, static_cast<std::uint64_t> (5000));

    // The new origin is the discontinuity point.
    CHECK_EQ (bridge.samplePosition(), static_cast<std::uint64_t> (5000));
    CHECK_NEAR (bridge.beatsAt (5000), 0.0, 1e-12);
}

//==============================================================================
JAM_TEST (DrumClockBridge, forwardJumpPastBoundIsDiscontinuity)
{
    DrumClockBridgeConfig config = config120();
    config.maxForwardJumpSamples = 100000;

    DrumClockBridge bridge (config);
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    CHECK_EQ (bridge.setClockSample (200000), false);
    CHECK_EQ (bridge.discontinuityCount(), static_cast<std::uint64_t> (1));

    // A jump inside the bound is ordinary elapsed time.
    DrumClockBridge tight (config);
    tight.prepare (kSr, 512);
    tight.setClockSample (0);
    CHECK_EQ (tight.setClockSample (50000), true);
    CHECK_EQ (tight.discontinuityCount(), static_cast<std::uint64_t> (0));
}

//==============================================================================
JAM_TEST (DrumClockBridge, stopAndResyncPublishExactTargets)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    CHECK (bridge.requestStopAtNextBar());
    CHECK (bridge.requestResyncNextBeat (30000));
    CHECK (bridge.requestResyncNextBar (40000));

    const DrumClockCommand stop = popOne (bridge);
    CHECK_EQ (static_cast<int> (stop.type),
              static_cast<int> (DrumClockCommandType::StopAtBar));
    CHECK_EQ (stop.sampleTime, kBar120);

    const DrumClockCommand beat = popOne (bridge);
    CHECK_EQ (static_cast<int> (beat.type),
              static_cast<int> (DrumClockCommandType::ResyncBeat));
    CHECK_EQ (beat.sampleTime, static_cast<std::uint64_t> (30000));

    const DrumClockCommand bar = popOne (bridge);
    CHECK_EQ (static_cast<int> (bar.type),
              static_cast<int> (DrumClockCommandType::ResyncBar));
    CHECK_EQ (bar.sampleTime, static_cast<std::uint64_t> (40000));

    // Resync in the past is a request error, counted, nothing published.
    bridge.setClockSample (1000);
    CHECK_EQ (bridge.requestResyncNextBeat (1), false);
    CHECK_GE (bridge.invalidRequestCount(), static_cast<std::uint64_t> (1));
}

//==============================================================================
JAM_TEST (DrumClockBridge, queueOverflowDropsIncomingAndCounts)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    int accepted = 0;
    for (int i = 0; i < static_cast<int> (kDrumClockCommandCapacity) + 3; ++i)
        if (bridge.requestJoinAtNextBar (0))
            ++accepted;

    CHECK_EQ (accepted, static_cast<int> (kDrumClockCommandCapacity));
    CHECK_EQ (bridge.queueDropCount(), static_cast<std::uint64_t> (3));
    CHECK_GE (bridge.invalidRequestCount(), static_cast<std::uint64_t> (3));

    // Already-queued commands are intact and still FIFO.
    int drained = 0;
    DrumClockCommand command;
    while (bridge.popCommand (command))
    {
        CHECK_EQ (static_cast<int> (command.type),
                  static_cast<int> (DrumClockCommandType::JoinAtBar));
        ++drained;
    }
    CHECK_EQ (drained, static_cast<int> (kDrumClockCommandCapacity));
}

//==============================================================================
JAM_TEST (DrumClockBridge, sameSequenceProducesIdenticalCommands)
{
    const auto run = []()
    {
        DrumClockBridge bridge (config120());
        bridge.prepare (kSr, 512);
        bridge.setClockSample (0);
        bridge.setClockSample (5000);
        bridge.applySnapshot (lockedSnapshot (132.0, 1));
        bridge.requestJoinAtNextBar (3);
        bridge.setClockSample (kBar120);
        bridge.requestResyncNextBar (kBar120 + 40000);
        bridge.setClockSample (kBar120 + 40000);

        std::vector<DrumClockCommand> out;
        DrumClockCommand command;
        while (bridge.popCommand (command))
            out.push_back (command);
        return out;
    };

    const auto a = run();
    const auto b = run();

    REQUIRE (a.size() == b.size());
    CHECK (a.size() > 0);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK_EQ (static_cast<int> (a[i].type), static_cast<int> (b[i].type));
        CHECK_EQ (a[i].sampleTime, b[i].sampleTime);
        CHECK_EQ (a[i].sequence, b[i].sequence);
        CHECK_NEAR (a[i].bpm, b[i].bpm, 0.0);
        CHECK_EQ (a[i].groove, b[i].groove);
        CHECK_EQ (a[i].phaseStep, b[i].phaseStep);
    }
}

//==============================================================================
// BLOCK1: a join must carry the staged effective tempo, in BOTH command orders,
// so the first joined bar never starts at the stale tempo.
//==============================================================================
JAM_TEST (DrumClockBridge, joinCarriesStagedEffectiveTempo)
{
    {
        DrumClockBridge bridge (config120());
        bridge.prepare (kSr, 512);
        bridge.setClockSample (0);

        bridge.applySnapshot (lockedSnapshot (150.0, 1)); // stages SetTempo @96000
        CHECK (bridge.requestJoinAtNextBar (0));          // join must carry 150

        const DrumClockCommand tempo = popOne (bridge);
        const DrumClockCommand join = popOne (bridge);
        CHECK_EQ (static_cast<int> (tempo.type),
                  static_cast<int> (DrumClockCommandType::SetTempo));
        CHECK_EQ (static_cast<int> (join.type),
                  static_cast<int> (DrumClockCommandType::JoinAtBar));
        CHECK_EQ (join.sampleTime, kBar120);
        CHECK_NEAR (join.bpm, 150.0, 1e-12);
    }

    {
        DrumClockBridge bridge (config120());
        bridge.prepare (kSr, 512);
        bridge.setClockSample (0);

        CHECK (bridge.requestJoinAtNextBar (0));          // join at 120
        bridge.applySnapshot (lockedSnapshot (150.0, 1)); // then tempo @96000

        const DrumClockCommand join = popOne (bridge);
        const DrumClockCommand tempo = popOne (bridge);
        CHECK_EQ (static_cast<int> (join.type),
                  static_cast<int> (DrumClockCommandType::JoinAtBar));
        CHECK_NEAR (join.bpm, 120.0, 1e-12);
        CHECK_EQ (static_cast<int> (tempo.type),
                  static_cast<int> (DrumClockCommandType::SetTempo));
        CHECK_NEAR (tempo.bpm, 150.0, 1e-12);
    }
}

//==============================================================================
// GAP7: a stop is a pending commitment, not an immediate state flip.
//==============================================================================
JAM_TEST (DrumClockBridge, stopIsPendingUntilItsBoundary)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    CHECK (bridge.requestJoinAtNextBar (0));
    popOne (bridge); // join

    CHECK (bridge.requestStopAtNextBar());
    CHECK_EQ (bridge.stopPending(), true);
    CHECK_EQ (bridge.playing(), true); // still rendering to the boundary
    CHECK_EQ (bridge.pendingStopBoundary(), kBar120);

    bridge.setClockSample (kBar120 - 1);
    CHECK_EQ (bridge.playing(), true);
    CHECK_EQ (bridge.stopPending(), true);

    bridge.setClockSample (kBar120);
    CHECK_EQ (bridge.playing(), false);
    CHECK_EQ (bridge.stopPending(), false);
}

//==============================================================================
// BLOCK2: resync carries an explicit phase step so worker and engine agree.
//==============================================================================
JAM_TEST (DrumClockBridge, resyncCarriesPhaseStep)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (120000); // beat 5 at 120 BPM

    // 125000 contains beat 5 -> beat 1 of the bar -> step 4.
    CHECK (bridge.requestResyncNextBeat (125000));
    const DrumClockCommand beat = popOne (bridge);
    CHECK_EQ (static_cast<int> (beat.type),
              static_cast<int> (DrumClockCommandType::ResyncBeat));
    CHECK_EQ (beat.sampleTime, static_cast<std::uint64_t> (125000));
    CHECK_EQ (beat.phaseStep, 4);

    CHECK (bridge.requestResyncNextBar (126000));
    const DrumClockCommand bar = popOne (bridge);
    CHECK_EQ (bar.phaseStep, 0);
}

//==============================================================================
// GAP3: prepare clears the grid and drains the queue.
//==============================================================================
JAM_TEST (DrumClockBridge, prepareClearsGridAndDrainsQueue)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (24000);
    bridge.applySnapshot (lockedSnapshot (150.0, 1));
    CHECK (bridge.requestJoinAtNextBar (0));

    bridge.prepare (kSr, 512); // quiescent re-prepare
    CHECK_EQ (bridge.samplePosition(), static_cast<std::uint64_t> (0));
    CHECK_EQ (bridge.playing(), false);
    CHECK_EQ (bridge.stopPending(), false);
    CHECK_NEAR (bridge.bpm(), 120.0, 1e-12); // back to the configured initial

    DrumClockCommand command;
    CHECK_EQ (bridge.popCommand (command), false); // queue drained

    // Snapshot bookkeeping was reset, so generation 1 is accepted again.
    bridge.setClockSample (0);
    bridge.applySnapshot (lockedSnapshot (130.0, 1));
    CHECK_EQ (bridge.staleSnapshotCount(), static_cast<std::uint64_t> (0));
}

//==============================================================================
// GAP2 domain: above maxExplicitSample the grid is unrepresentable and refused.
//==============================================================================
JAM_TEST (DrumClockBridge, hugeExplicitSampleRejected)
{
    DrumClockBridgeConfig config = config120();
    config.maxExplicitSample = 1000;

    DrumClockBridge bridge (config);
    bridge.prepare (kSr, 512);
    CHECK_EQ (bridge.setClockSample (1001), false);
    CHECK_GE (bridge.invalidRequestCount(), static_cast<std::uint64_t> (1));
}

//==============================================================================
// N5: a resync target beyond a staged tempo boundary must use the piecewise
// effective clock. At 150 BPM staged for 96000, the beat containing 192000 is
// beat 9 (step 4), not beat 8 (step 0).
//==============================================================================
JAM_TEST (DrumClockBridge, resyncPhaseStepAccountsForStagedTempo)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    bridge.applySnapshot (lockedSnapshot (150.0, 1)); // staged at 96000
    CHECK (bridge.requestResyncNextBeat (192000));    // target beyond the boundary

    const DrumClockCommand tempo = popOne (bridge);
    const DrumClockCommand resync = popOne (bridge);
    CHECK_EQ (static_cast<int> (tempo.type),
              static_cast<int> (DrumClockCommandType::SetTempo));
    CHECK_EQ (tempo.sampleTime, static_cast<std::uint64_t> (96000));
    CHECK_EQ (static_cast<int> (resync.type),
              static_cast<int> (DrumClockCommandType::ResyncBeat));
    CHECK_EQ (resync.sampleTime, static_cast<std::uint64_t> (192000));
    CHECK_EQ (resync.phaseStep, 4); // piecewise; a single old-rate read would give 0
}

//==============================================================================
// N4: prepare() rebaselines the per-session queue drop count.
//==============================================================================
JAM_TEST (DrumClockBridge, prepareRebaselinesQueueDrops)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    for (int i = 0; i < 20; ++i)
        bridge.requestJoinAtNextBar (0);

    CHECK_EQ (bridge.queueDropCount(), static_cast<std::uint64_t> (4));

    bridge.prepare (kSr, 512);
    CHECK_EQ (bridge.queueDropCount(), static_cast<std::uint64_t> (0));
}

//==============================================================================
// Residual 1: resetForNewSession() drains the queue and rebaselines drops too.
//==============================================================================
JAM_TEST (DrumClockBridge, resetForNewSessionRebaselinesDrops)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);

    for (int i = 0; i < 20; ++i)
        bridge.requestJoinAtNextBar (0); // 16 queued, 4 dropped

    CHECK_EQ (bridge.queueDropCount(), static_cast<std::uint64_t> (4));

    bridge.resetForNewSession (120.0);
    CHECK_EQ (bridge.queueDropCount(), static_cast<std::uint64_t> (0));
    CHECK_EQ (bridge.samplePosition(), static_cast<std::uint64_t> (0));
    CHECK_EQ (bridge.playing(), false);

    // The queue was drained and one Clear was published for a still-attached
    // engine; nothing else survives.
    DrumClockCommand command;
    REQUIRE (bridge.popCommand (command));
    CHECK_EQ (static_cast<int> (command.type),
              static_cast<int> (DrumClockCommandType::Clear));
    CHECK_EQ (bridge.popCommand (command), false);
}

//==============================================================================
// Residual 2: a single setClockSample that overshoots BOTH a staged resync
// target T and a staged tempo boundary B must end in the same grid as crossing
// them stepwise, for T<B, B<T and T==B, with both resync kinds and a nonzero
// origin. A wrong collision order drifts the later grid.
//==============================================================================
JAM_TEST (DrumClockBridge, overshootUpdateMatchesStepwise)
{
    struct State { double beats; std::uint64_t nextBar; double bpm; std::uint64_t pos; };

    const auto build = [] (std::uint64_t origin, bool barResync, std::uint64_t target,
                           bool overshoot)
    {
        DrumClockBridge bridge (config120());
        bridge.prepare (kSr, 512);
        bridge.setClockSample (origin);
        bridge.applySnapshot (lockedSnapshot (150.0, 1)); // B = origin + 96000

        if (barResync)
            bridge.requestResyncNextBar (target);
        else
            bridge.requestResyncNextBeat (target);

        const std::uint64_t boundary = origin + 96000;
        const std::uint64_t end = origin + 200000;

        if (overshoot)
        {
            bridge.setClockSample (end);
        }
        else
        {
            std::uint64_t steps[2] = { target, boundary };
            if (steps[0] > steps[1]) std::swap (steps[0], steps[1]);
            for (const std::uint64_t s : steps)
                if (s > origin && s <= end)
                    bridge.setClockSample (s);
            bridge.setClockSample (end);
        }

        State state;
        state.beats = bridge.beatsAt (end);
        state.nextBar = bridge.nextBarBoundarySample();
        state.bpm = bridge.bpm();
        state.pos = bridge.samplePosition();
        return state;
    };

    for (const std::uint64_t origin : { std::uint64_t (0), std::uint64_t (100000) })
    {
        const std::uint64_t below = origin + 48000;   // T < B
        const std::uint64_t above = origin + 120000;  // B < T
        const std::uint64_t equal = origin + 96000;   // T == B

        for (const bool barResync : { true, false })
            for (const std::uint64_t target : { below, above, equal })
            {
                const State oneShot = build (origin, barResync, target, true);
                const State stepped = build (origin, barResync, target, false);
                CHECK_NEAR (oneShot.beats, stepped.beats, 1e-9);
                CHECK_EQ (oneShot.nextBar, stepped.nextBar);
                CHECK_NEAR (oneShot.bpm, stepped.bpm, 0.0);
                CHECK_EQ (oneShot.pos, stepped.pos);
            }
    }
}
