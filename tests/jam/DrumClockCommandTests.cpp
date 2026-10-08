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
    }
}
