// EVAL-LIVE-001 — frozen live-Jam facade and live-session contract tests.
//
// Scope and honesty:
//   * The static part asserts the frozen `jam/JamLiveInterface.h` POD, enum and
//     telemetry-initialisation contracts. It needs only the two frozen headers.
//   * The runtime part (compiled only when LIVE_JAM_HAVE_JAM_CORE is defined and
//     a jam-core archive is linked) exercises the *actual* MusicalClock and
//     DrumClockBridge engines for the semantics the replay harness depends on:
//     no raw detector BPM setter, no fabricated tempo during silence, and
//     next-bar stop not being an immediate stop.
//   * This file is NOT the actual-processor replay. The real
//     `GuitarCompanionProcessor::processBlock` replay is
//     `tools/live-jam-replay/src/LiveJamReplay.cpp`, which is intentionally kept
//     out of this binary because it requires the merged live pipeline.
//
// This test is owned by EVAL-LIVE-001 and is never wired into the shared
// tests/CMakeLists.txt (owned by the orchestrator). It is built and run by
// tools/live-jam-replay/build_replay.py.
#include "jam/JamLiveInterface.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <type_traits>

#ifdef LIVE_JAM_HAVE_JAM_CORE
#include "jam/DrumClockBridge.h"
#include "jam/MusicalClock.h"
#endif

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        ++failures;
        std::fprintf (stdout, "  FAIL: %s\n", what);
    }
}

//------------------------------------------------------------------------------
// Static contract assertions (frozen facade).
//------------------------------------------------------------------------------
static_assert (std::is_trivially_copyable_v<jam::JamLiveCommand>,
               "JamLiveCommand must stay trivially copyable across threads");
static_assert (std::is_trivially_copyable_v<jam::JamLiveState>,
               "JamLiveState must stay trivially copyable across threads");
static_assert (std::is_polymorphic_v<jam::IJamLiveControl>,
               "IJamLiveControl must expose a virtual facade");
static_assert (static_cast<int> (jam::JamLiveCommandType::Start) == 0,
               "Start must stay the first command value");
static_assert (static_cast<int> (jam::JamLiveCommandType::Stop) == 1,
               "Stop must stay the second command value");
static_assert (static_cast<int> (jam::JamLiveBackend::unavailable) == 0,
               "unavailable must stay the first backend value");
static_assert (static_cast<int> (jam::JamLiveFailure::none) == 0,
               "none must stay the first failure value");

// The frozen facade is exactly two bounded operations. There is deliberately no
// synchronous renderer setter and no raw detector BPM setter: tempo belongs to
// the MusicalClock, and the UI may only submit commands and read whole state.
using SubmitSignature = bool (jam::IJamLiveControl::*) (const jam::JamLiveCommand&) noexcept;
using ReadSignature   = bool (jam::IJamLiveControl::*) (jam::JamLiveState&) const noexcept;
static_assert (std::is_same_v<decltype (static_cast<SubmitSignature> (&jam::IJamLiveControl::submitJamCommand)),
                              SubmitSignature>,
               "submitJamCommand signature changed");
static_assert (std::is_same_v<decltype (static_cast<ReadSignature> (&jam::IJamLiveControl::readJamLiveState)),
                              ReadSignature>,
               "readJamLiveState signature changed");

void testInitialTelemetryIsZeroUnavailable()
{
    const jam::JamLiveState s {};
    check (s.sessionGeneration == 0, "initial sessionGeneration must be 0");
    check (! s.prepared, "initial prepared must be false");
    check (! s.requestedRunning, "initial requestedRunning must be false");
    check (! s.joinPending, "initial joinPending must be false");
    check (! s.drumsPlaying, "initial drumsPlaying must be false");
    check (s.backend == jam::JamLiveBackend::unavailable,
           "initial backend must be unavailable");
    check (s.failure == jam::JamLiveFailure::none, "initial failure must be none");
    check (s.clock.bpm == 0.0, "initial clock.bpm must be 0 (never simulated)");
    check (s.clock.generation == 0, "initial clock.generation must be 0");
    check (s.clock.lockState == jam::ClockLockState::Acquiring,
           "initial lock state must be Acquiring");
    check (! s.clock.tempoFrozen, "initial tempoFrozen must be false");
    check (s.mode == jam::TempoMode::Follow, "initial mode must be Follow");
    check (s.candidateBpm == 0.0f, "initial candidateBpm must be 0");
    check (s.inputPeak == 0.0f, "initial inputPeak must be 0");
    check (s.sampleRate == 0.0, "initial sampleRate must be 0 (device domain unset)");
    check (s.audioSampleTime == 0, "initial audioSampleTime must be 0");
    check (s.lastEventSampleTime == 0, "initial lastEventSampleTime must be 0");
    check (s.lastInputHorizonSampleTime == 0,
           "initial lastInputHorizonSampleTime must be 0");
    check (s.lastReceiptSampleTime == 0, "initial lastReceiptSampleTime must be 0");
    check (! s.receiptMeasured, "initial receiptMeasured must be false");
    check (s.analysisDrops == 0 && s.observationDrops == 0
           && s.userCommandDrops == 0 && s.drumCommandDrops == 0
           && s.discontinuities == 0, "initial drop counters must be 0");
}

void testCommandDefault()
{
    const jam::JamLiveCommand c {};
    check (c.type == jam::JamLiveCommandType::Start, "default command must be Start");
    check (c.value == 0.0, "default command value must be 0");
}

#ifdef LIVE_JAM_HAVE_JAM_CORE
//------------------------------------------------------------------------------
// Runtime engine semantics (actual MusicalClock / DrumClockBridge).
//------------------------------------------------------------------------------
void testClockDoesNotFabricateTempoDuringSilence()
{
    // The shipped clock publishes its configured fallback BPM while no belief
    // exists; that is an explicit fallback, not tracker-derived tempo. Prove it
    // by zeroing the fallback: advancing audio time during silence must still
    // yield bpm 0, confidence 0 and no lock.
    jam::ClockConfig cfg;
    cfg.fallbackBpm = 0.0;
    jam::MusicalClock clock (cfg);
    for (int i = 0; i < 200; ++i)
        clock.advance (512, 48000.0);

    const auto s = clock.snapshot();
    check (s.bpm == 0.0, "silence must not fabricate a clock BPM");
    check (s.confidence01 == 0.0f, "silence must not fabricate confidence");
    check (s.lockState == jam::ClockLockState::Acquiring,
           "silence must not fabricate a lock");

    // With the shipped fallback, silence yields exactly the fallback BPM and
    // still no lock/confidence.
    jam::MusicalClock shipped;
    for (int i = 0; i < 200; ++i)
        shipped.advance (512, 48000.0);
    const auto ss = shipped.snapshot();
    check (ss.bpm == jam::ClockConfig {}.fallbackBpm,
           "silence must publish exactly the configured fallback BPM");
    check (ss.confidence01 == 0.0f && ss.lockState == jam::ClockLockState::Acquiring,
           "shipped fallback must not fabricate confidence or a lock");

    // A user command is the only way to change mode; it never accepts a raw BPM.
    jam::ClockCommand cmd;
    cmd.type = jam::ClockCommandType::SetMode;
    cmd.tapSampleTime = static_cast<std::uint64_t> (jam::TempoMode::Fixed);
    clock.command (cmd);
    check (clock.mode() == jam::TempoMode::Fixed,
           "SetMode must change the clock mode");
    check (clock.snapshot().bpm == 0.0,
           "SetMode must not fabricate a BPM without tempo evidence");
}

void testStopIsNextBarNotImmediate()
{
    jam::DrumClockBridgeConfig cfg;
    cfg.initialBpm = 120.0;
    jam::DrumClockBridge bridge (cfg);
    bridge.prepare (48000.0, 512);
    bridge.setClockSample (0);

    // Arm the grid at bar 0, then advance past the first bar so the join has
    // taken effect (the bridge starts the transport when the boundary is
    // crossed by setClockSample, which is the worker-side model of the audio
    // cursor advancing).
    check (bridge.requestJoinAtNextBar() , "join request must be accepted");
    bridge.setClockSample (48000 * 2);        // 2 s at 120 BPM = 4 beats = 1 bar
    check (bridge.playing(), "transport must be playing after the join boundary");

    // Stop must be a pending commitment at the next bar boundary, never an
    // immediate state change: the audio side keeps rendering until the boundary.
    check (bridge.requestStopAtNextBar(), "stop request must be accepted");
    check (bridge.playing(), "StopAtNextBar must not stop immediately");
    check (bridge.stopPending(), "StopAtNextBar must publish a pending stop");

    const auto boundary = bridge.pendingStopBoundary();
    check (boundary > bridge.samplePosition(),
           "pending stop boundary must be strictly in the future");

    bridge.setClockSample (boundary);
    check (! bridge.playing(), "transport must stop once the bar boundary is crossed");
    check (! bridge.stopPending(), "pending stop must clear at the boundary");
}
#endif // LIVE_JAM_HAVE_JAM_CORE
} // namespace

int main()
{
    std::fprintf (stdout, "EVAL-LIVE-001 frozen live-Jam facade tests\n");
    std::fprintf (stdout, "=========================================\n");

    testInitialTelemetryIsZeroUnavailable();
    testCommandDefault();

#ifdef LIVE_JAM_HAVE_JAM_CORE
    testClockDoesNotFabricateTempoDuringSilence();
    testStopIsNextBarNotImmediate();
    std::fprintf (stdout, "  runtime engine checks: ENABLED (jam-core linked)\n");
#else
    std::fprintf (stdout, "  runtime engine checks: SKIPPED (jam-core not linked)\n");
#endif

    if (failures == 0)
    {
        std::fprintf (stdout, "\nFACADE CONTRACT TESTS PASS\n");
        return 0;
    }
    std::fprintf (stdout, "\nFACADE CONTRACT TESTS FAIL (%d)\n", failures);
    return 1;
}
