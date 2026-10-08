// DRUM-ADAPT-002 — portable, device-free tests for the adaptive BarChange seam
// on jam::DrumClockBridge.
//
// These assert the worker-side mechanical contract only: a BarChange/one-bar
// fill is published as ONE bounded POD command for the next STRICTLY FUTURE bar
// boundary, repeated identical requests coalesce instead of flooding the queue,
// a full queue returns false without latching any accepted state, stale
// generations, NaN parameters and out-of-domain indices are rejected whole, and
// Stop/Clear/reset/discontinuity cancel a staged change. The actual DrumEngine
// integration (pattern switch, one-bar fill then reversion, intensity/swing/
// humanization) lives in tests/DrumAdaptiveTests.cpp.
//
// The bridge cannot validate library kind/meter (the library is JUCE), so those
// are engine-side; here every index >= 0 is structurally valid.

#include "JamTest.h"

#include "jam/DrumClockBridge.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace
{
using namespace jam;

constexpr double kSr = 48000.0;
// 120 BPM @ 48 kHz: one sixteenth is 6000 samples, one bar is 96000.
constexpr std::uint64_t kBar120 = 96000;

DrumClockBridgeConfig config120()
{
    DrumClockBridgeConfig config;
    config.initialBpm = 120.0;
    config.defaultSampleRate = kSr;
    return config;
}

DrumClockCommand popOne (DrumClockBridge& bridge)
{
    DrumClockCommand command;
    const bool ok = bridge.popCommand (command);
    CHECK (ok);
    return command;
}

int drainCount (DrumClockBridge& bridge)
{
    int n = 0;
    DrumClockCommand command;
    while (bridge.popCommand (command))
        ++n;
    return n;
}
} // namespace

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, commandCarriesBarChangePayloadAsPod)
{
    static_assert (std::is_trivially_copyable<DrumClockCommand>::value,
                   "BarChange must still cross the audio boundary as raw bytes");

    DrumClockCommand command;
    command.fill = 42;
    command.changeFields = static_cast<std::uint8_t> (
        DrumChangeField::Groove | DrumChangeField::Fill | DrumChangeField::Params);
    command.intensity01 = 0.25f;
    command.swing01 = 0.5f;
    command.humanizeVelocity = 0.1f;
    command.humanizeTiming = 0.2f;
    command.humanizeRoundRobin = 0.3f;

    const DrumClockCommand copy = command;
    CHECK_EQ (copy.fill, 42);
    CHECK_EQ (copy.changeFields, static_cast<std::uint8_t> (7));
    CHECK_NEAR (copy.intensity01, 0.25, 0.0);
    CHECK_NEAR (copy.swing01, 0.5, 0.0);
    CHECK (hasField (copy.changeFields, DrumChangeField::Groove));
    CHECK (hasField (copy.changeFields, DrumChangeField::Fill));
    CHECK (hasField (copy.changeFields, DrumChangeField::Params));
    CHECK (! hasField (copy.changeFields, DrumChangeField::None));
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, barChangePublishesAtStrictlyNextFutureBar)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (30000); // mid bar-1

    QueuedBarChange change;
    change.generation = 7;
    change.groove = 3;
    change.fill = 9;             // fill + groove for the same bar
    change.intensity01 = 0.8f;
    change.swing01 = 0.4f;
    change.humanizeVelocity = 0.2f;
    change.humanizeTiming = 0.1f;
    change.humanizeRoundRobin = 0.5f;

    CHECK (bridge.requestBarChange (change));
    CHECK (bridge.barChangePending());
    CHECK_EQ (bridge.barChangeBoundarySample(), kBar120);

    const DrumClockCommand command = popOne (bridge);
    CHECK_EQ (static_cast<int> (command.type),
              static_cast<int> (DrumClockCommandType::BarChange));
    CHECK_EQ (command.sampleTime, kBar120);
    CHECK_EQ (command.generation, static_cast<std::uint64_t> (7));
    CHECK_EQ (command.groove, 3);
    CHECK_EQ (command.fill, 9);
    CHECK_EQ (command.changeFields, static_cast<std::uint8_t> (7));
    CHECK_NEAR (command.intensity01, 0.8, 1e-6);
    CHECK_NEAR (command.swing01, 0.4, 1e-6);
    CHECK_NEAR (command.humanizeVelocity, 0.2, 1e-6);
    CHECK_NEAR (command.humanizeTiming, 0.1, 1e-6);
    CHECK_NEAR (command.humanizeRoundRobin, 0.5, 1e-6);
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, barChangeDoesNotDisturbTheGrid)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (30000);
    const double bpmBefore = bridge.bpm();
    const std::uint64_t posBefore = bridge.samplePosition();

    QueuedBarChange change;
    change.intensity01 = 0.7f;
    CHECK (bridge.requestBarChange (change));

    CHECK_NEAR (bridge.bpm(), bpmBefore, 0.0);          // tempo authority untouched
    CHECK_EQ (bridge.samplePosition(), posBefore);      // no position move
    CHECK_EQ (bridge.playing(), false);                 // not a join
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, unpreparedBridgeRejectsBarChange)
{
    DrumClockBridge bridge (config120());
    QueuedBarChange change;
    CHECK (! bridge.requestBarChange (change));
    CHECK (! bridge.requestFillAtNextBar (4));
    CHECK (! bridge.barChangePending());
    CHECK_GE (bridge.invalidRequestCount(), static_cast<std::uint64_t> (2));
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, nanAndInfiniteParametersRejectedWhole)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    QueuedBarChange change;
    change.intensity01 = nan;
    CHECK (! bridge.requestBarChange (change));
    change.intensity01 = 0.5f;

    change.swing01 = inf;
    CHECK (! bridge.requestBarChange (change));
    change.swing01 = 0.0f;

    change.humanizeVelocity = nan;
    CHECK (! bridge.requestBarChange (change));
    change.humanizeVelocity = 0.25f;

    change.humanizeTiming = inf;
    CHECK (! bridge.requestBarChange (change));
    change.humanizeTiming = 0.15f;

    change.humanizeRoundRobin = nan;
    CHECK (! bridge.requestBarChange (change));

    CHECK (! bridge.barChangePending());
    CHECK_EQ (bridge.publishedCount(), static_cast<std::uint64_t> (0));
    CHECK_GE (bridge.invalidRequestCount(), static_cast<std::uint64_t> (5));
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, outOfDomainIndicesRejected)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change;
    change.groove = -2; // below the kNoLibraryEntry sentinel
    CHECK (! bridge.requestBarChange (change));
    change.groove = kNoLibraryEntry;
    change.fill = -5;
    CHECK (! bridge.requestBarChange (change));
    CHECK (! bridge.requestFillAtNextBar (-1));
    CHECK (! bridge.barChangePending());
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, finiteOutOfRangeParametersAreBoundedNotRejected)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change;
    change.intensity01 = 5.0f;
    change.swing01 = -3.0f;
    change.humanizeVelocity = 2.0f;
    CHECK (bridge.requestBarChange (change));

    const DrumClockCommand command = popOne (bridge);
    CHECK_NEAR (command.intensity01, 1.0, 0.0);
    CHECK_NEAR (command.swing01, 0.0, 0.0);
    CHECK_NEAR (command.humanizeVelocity, 1.0, 0.0);
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, staleGenerationRejected)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange newer;
    newer.generation = 10;
    CHECK (bridge.requestBarChange (newer));
    popOne (bridge); // consume but the staged generation is latched

    QueuedBarChange older;
    older.generation = 9;
    CHECK (! bridge.requestBarChange (older));
    CHECK_GE (bridge.staleBarChangeCount(), static_cast<std::uint64_t> (1));

    // A newer generation is accepted, and generation 0 ("unknown") is accepted.
    newer.generation = 11;
    CHECK (bridge.requestBarChange (newer));
    QueuedBarChange unknown;
    unknown.generation = 0;
    CHECK (bridge.requestBarChange (unknown));
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, repeatedIdenticalRequestsCoalesce)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change;
    change.generation = 1;
    change.groove = 2;
    change.intensity01 = 0.6f;

    for (int i = 0; i < 50; ++i)
        CHECK (bridge.requestBarChange (change)); // never floods the queue

    CHECK_EQ (drainCount (bridge), 1); // exactly one command staged
    CHECK_EQ (bridge.queueDropCount(), static_cast<std::uint64_t> (0));
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, changedTargetReusesTheSameStagedBoundary)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange first;
    first.groove = 2;
    CHECK (bridge.requestBarChange (first));
    const std::uint64_t boundary = bridge.barChangeBoundarySample();
    CHECK_EQ (boundary, kBar120);

    // A later, different value for the SAME bar must not slip to the bar after.
    QueuedBarChange second;
    second.groove = 4;
    CHECK (bridge.requestBarChange (second));
    CHECK_EQ (bridge.barChangeBoundarySample(), boundary);

    const DrumClockCommand a = popOne (bridge);
    const DrumClockCommand b = popOne (bridge);
    CHECK_EQ (a.sampleTime, boundary);
    CHECK_EQ (b.sampleTime, boundary);
    CHECK_EQ (a.groove, 2);
    CHECK_EQ (b.groove, 4); // last wins at the engine
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, fullQueueReturnsFalseWithoutLatching)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    DrumClockCommand filler;
    filler.type = DrumClockCommandType::None;
    while (bridge.commandQueue().push (filler))
    {
    }
    // The terminating push of the fill loop already counted one drop.
    const std::uint64_t dropsBefore = bridge.queueDropCount();

    QueuedBarChange change;
    change.groove = 6;
    CHECK (! bridge.requestBarChange (change));
    CHECK (! bridge.barChangePending());          // nothing committed
    CHECK_EQ (bridge.queueDropCount(), dropsBefore + 1u);

    // Drain, then the SAME request is accepted: a rejected call is retryable and
    // never leaves a phantom accepted state.
    CHECK (drainCount (bridge) > 0);
    CHECK (bridge.requestBarChange (change));
    CHECK (bridge.barChangePending());
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, fillRequestIsFillOnlyAndRejectsNegative)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    CHECK (! bridge.requestFillAtNextBar (-1));
    CHECK (bridge.requestFillAtNextBar (12));
    CHECK (bridge.barChangePending());

    const DrumClockCommand command = popOne (bridge);
    CHECK_EQ (static_cast<int> (command.type),
              static_cast<int> (DrumClockCommandType::BarChange));
    CHECK_EQ (command.sampleTime, kBar120);
    CHECK_EQ (command.fill, 12);
    CHECK_EQ (command.groove, kNoLibraryEntry);   // selected groove untouched
    CHECK_EQ (command.changeFields, static_cast<std::uint8_t> (2)); // Fill only
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, fullBarChangeIsAuthoritativeForAllFields)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change; // groove/fill both kNoLibraryEntry
    CHECK (bridge.requestBarChange (change));
    const DrumClockCommand command = popOne (bridge);
    CHECK_EQ (command.changeFields, static_cast<std::uint8_t> (7));
    CHECK_EQ (command.groove, kNoLibraryEntry);
    CHECK_EQ (command.fill, kNoLibraryEntry);
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, stagingIsReleasedAfterItsBarBoundary)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change;
    change.groove = 1;
    CHECK (bridge.requestBarChange (change));
    CHECK (bridge.barChangePending());
    drainCount (bridge);

    // Cross the staged boundary: the latch is released, and the next request
    // stages for the FOLLOWING bar, not the passed one.
    bridge.setClockSample (kBar120);
    CHECK (! bridge.barChangePending());
    CHECK_EQ (bridge.barChangeBoundarySample(), static_cast<std::uint64_t> (0));

    change.groove = 2;
    CHECK (bridge.requestBarChange (change));
    CHECK_EQ (bridge.barChangeBoundarySample(), static_cast<std::uint64_t> (2 * kBar120));
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, stopAtNextBarCancelsStagedChange)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change;
    change.groove = 5;
    CHECK (bridge.requestBarChange (change));
    CHECK (bridge.barChangePending());
    drainCount (bridge);

    CHECK (bridge.requestStopAtNextBar());
    CHECK (! bridge.barChangePending()); // stop supersedes the same-bar change
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, stopNowCancelsStagedChange)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change;
    change.groove = 5;
    CHECK (bridge.requestBarChange (change));
    drainCount (bridge);
    CHECK (bridge.requestStopNow());
    CHECK (! bridge.barChangePending());
    CHECK (! bridge.playing());
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, resetForNewSessionCancelsStagedChange)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (1000);

    QueuedBarChange change;
    change.groove = 5;
    CHECK (bridge.requestBarChange (change));
    CHECK (bridge.barChangePending());

    bridge.resetForNewSession (100.0);
    CHECK (! bridge.barChangePending());
    CHECK_EQ (bridge.staleBarChangeCount(), static_cast<std::uint64_t> (0)); // rebaselined
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, discontinuityCancelsStagedChangeAndPublishesClear)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (100000);

    QueuedBarChange change;
    change.groove = 5;
    CHECK (bridge.requestBarChange (change));
    drainCount (bridge);

    // Backwards jump -> discontinuity.
    CHECK (! bridge.setClockSample (10));
    CHECK (! bridge.barChangePending());
    CHECK_GE (bridge.discontinuityCount(), static_cast<std::uint64_t> (1));
    const DrumClockCommand clear = popOne (bridge);
    CHECK_EQ (static_cast<int> (clear.type),
              static_cast<int> (DrumClockCommandType::Clear));
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, sameSequenceProducesIdenticalAdaptiveCommands)
{
    const auto run = []
    {
        DrumClockBridge bridge (config120());
        bridge.prepare (kSr, 512);
        bridge.setClockSample (0);
        bridge.setClockSample (5000);
        for (int i = 0; i < 5; ++i)
        {
            QueuedBarChange change;
            change.generation = static_cast<std::uint64_t> (i + 1);
            change.groove = 2 + i;
            change.fill = i % 2 == 0 ? 20 + i : kNoLibraryEntry;
            change.intensity01 = 0.4f + 0.1f * (float) i;
            change.swing01 = 0.1f * (float) i;
            bridge.requestBarChange (change);
        }
        bridge.requestFillAtNextBar (30);
        std::vector<DrumClockCommand> out;
        DrumClockCommand command;
        while (bridge.popCommand (command))
            out.push_back (command);
        return out;
    };

    const auto a = run();
    const auto b = run();
    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK_EQ (a[i].sampleTime, b[i].sampleTime);
        CHECK_EQ (a[i].groove, b[i].groove);
        CHECK_EQ (a[i].fill, b[i].fill);
        CHECK_EQ (a[i].changeFields, b[i].changeFields);
        CHECK_NEAR (a[i].intensity01, b[i].intensity01, 0.0);
        CHECK_NEAR (a[i].swing01, b[i].swing01, 0.0);
    }
}

//==============================================================================
JAM_TEST (DrumAdaptiveBridge, adaptiveCommandsDoNotBreakExistingJoin)
{
    DrumClockBridge bridge (config120());
    bridge.prepare (kSr, 512);
    bridge.setClockSample (0);
    bridge.setClockSample (30000);

    CHECK (bridge.requestJoinAtNextBar (0));
    const DrumClockCommand join = popOne (bridge);
    CHECK_EQ (static_cast<int> (join.type),
              static_cast<int> (DrumClockCommandType::JoinAtBar));
    CHECK_EQ (join.sampleTime, kBar120);
    CHECK (bridge.playing());
}
