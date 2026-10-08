// Unit tests for jam::LiveJamSession — the JUCE-free control core of the first
// audible live slice (INT-LIVE-001, LIVE-JAM-CONTRACT.md).
//
// The tests are deterministic: a stub tracker plus a public observation-
// injection seam let the control loop be stepped by hand with no timing luck.
// Threaded cases are used only where the contract is inherently threaded
// (analyzer chunking, ring pressure, worker shutdown) and every wait is bounded.
//
// What is asserted:
//   - backend availability: no tracker => unavailable backend, Start rejected;
//   - join: tap locks the clock, Start joins at the next bar, the audio echo
//     distinguishes joinPending from drumsPlaying;
//   - clock commands: half/double/freeze/resume/resync act through the
//     MusicalClock and the bridge only gets bounded commands;
//   - stop/reset: an explicit Stop/Reset stops the grid and never auto-resumes;
//   - tap: a chunked >2048 callback is not truncated; ring pressure is counted;
//   - generations: a command submitted across a re-prepare is rejected;
//   - receipt/event/horizon are distinct; future and aged evidence fail closed;
//   - the clock advances from the cursor during silence and a backwards cursor
//     is a discontinuity;
//   - worker shutdown and a second prepare are clean.

#include "JamTest.h"

#include "jam/LiveJamSession.h"
#include "jam/RhythmAnalyzer.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using jam::AnalysisFrame;
using jam::ClockCommandType;
using jam::DrumClockCommand;
using jam::DrumClockCommandType;
using jam::JamLiveCommand;
using jam::JamLiveCommandType;
using jam::JamLiveFailure;
using jam::JamLiveState;
using jam::LiveJamSession;
using jam::LiveJamSessionConfig;
using jam::ObservationEnvelope;
using jam::RhythmObservation;

namespace
{

struct StubTracker : jam::IRhythmTracker
{
    std::atomic<uint64_t> processCalls { 0 };
    std::atomic<uint64_t> resetCalls { 0 };
    std::atomic<uint64_t> samplesSeen { 0 };

    bool blockProcess = false;
    std::atomic<bool> entered { false };
    std::atomic<bool> release { false };
    mutable std::mutex mutex;
    std::condition_variable cv;

    const char* id() const noexcept override { return "stub"; }

    void reset (double) override { resetCalls.fetch_add (1, std::memory_order_relaxed); }

    RhythmObservation process (const AnalysisFrame& frame) override
    {
        processCalls.fetch_add (1, std::memory_order_relaxed);
        samplesSeen.fetch_add (frame.numSamples, std::memory_order_relaxed);

        if (blockProcess)
        {
            {
                std::lock_guard<std::mutex> lock (mutex);
                entered.store (true, std::memory_order_release);
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lock (mutex);
            cv.wait_for (lock, std::chrono::seconds (5),
                         [this] { return release.load (std::memory_order_acquire); });
        }

        RhythmObservation observation;
        observation.inputSampleTime = frame.sampleTime;
        observation.sourceSampleRate = frame.sourceSampleRate;
        observation.silence = true;
        return observation;
    }

    void unblock()
    {
        release.store (true, std::memory_order_release);
        cv.notify_all();
    }
};

LiveJamSessionConfig testConfig()
{
    LiveJamSessionConfig config;
    config.availableBackend = jam::JamLiveBackend::injectedTest;
    config.groove = 0;
    return config;
}

std::unique_ptr<LiveJamSession> makeSession (StubTracker** outTracker = nullptr,
                                             LiveJamSessionConfig config = testConfig())
{
    auto session = std::make_unique<LiveJamSession> (config);
    auto tracker = std::make_unique<StubTracker>();
    if (outTracker != nullptr)
        *outTracker = tracker.get();
    session->setTracker (std::move (tracker));
    return session;
}

ObservationEnvelope observation (uint64_t eventSample, uint64_t horizonSample,
                                 float bpm = 120.0f, bool phaseValid = true,
                                 float phase = 0.0f)
{
    ObservationEnvelope envelope;
    envelope.observation.inputSampleTime = eventSample;
    envelope.observation.sourceSampleRate = 48000.0;
    envelope.observation.bpmCandidate = bpm;
    envelope.observation.beatPhase01 = phase;
    envelope.observation.beatConfidence01 = 0.9f;
    envelope.observation.energyRmsDbfs = -20.0f;
    envelope.observation.phaseValid = phaseValid;
    envelope.streamGeneration = 1;
    envelope.blockStartSampleTime = eventSample;
    envelope.inputHorizonSampleTime = horizonSample;
    envelope.availabilityMeasured = false;
    envelope.sourceSampleRate = 48000.0;
    return envelope;
}

std::vector<DrumClockCommand> drainCommands (LiveJamSession& session)
{
    std::vector<DrumClockCommand> commands;
    DrumClockCommand command;
    while (session.drumCommandQueue().pop (command))
        commands.push_back (command);
    return commands;
}

int countType (const std::vector<DrumClockCommand>& commands, DrumClockCommandType type)
{
    int n = 0;
    for (const auto& c : commands)
        if (c.type == type)
            ++n;
    return n;
}

JamLiveState stateOf (LiveJamSession& session)
{
    JamLiveState state;
    const bool ok = session.readState (state);
    CHECK (ok);
    return state;
}

// Two taps 0.5 s apart force a deterministic Locked 120 BPM clock.
void lockClockWithTaps (LiveJamSession& session)
{
    session.publishAudioCursor (0);
    session.submitCommand (JamLiveCommand { JamLiveCommandType::TapTempo, 0.0 });
    session.stepControlForTesting();

    session.publishAudioCursor (24000);
    session.submitCommand (JamLiveCommand { JamLiveCommandType::TapTempo, 0.0 });
    session.stepControlForTesting();
}

} // namespace

//============================================================================
// Backend availability
//============================================================================

JAM_TEST (livejamsession, stateUnavailableBeforeFirstTick)
{
    auto session = makeSession();
    session->prepare (48000.0, 512, false);

    JamLiveState state;
    CHECK (! session->readState (state)); // nothing published yet
}

JAM_TEST (livejamsession, noTrackerReportsUnavailableAndRejectsStart)
{
    auto session = std::make_unique<LiveJamSession> (testConfig());
    REQUIRE (session->setTracker (nullptr));
    REQUIRE (session->prepare (48000.0, 512, false));

    CHECK (session->backend() == jam::JamLiveBackend::unavailable);

    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    session->stepControlForTesting();

    const JamLiveState state = stateOf (*session);
    CHECK (! state.requestedRunning);
    CHECK (state.failure == JamLiveFailure::unavailableBackend);
    CHECK (state.backend == jam::JamLiveBackend::unavailable);
}

JAM_TEST (livejamsession, injectedTrackerReportsInjectedBackend)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    CHECK (session->backend() == jam::JamLiveBackend::injectedTest);

    session->stepControlForTesting();
    const JamLiveState state = stateOf (*session);
    CHECK (state.prepared);
    CHECK (state.sampleRate == 48000.0);
}

//============================================================================
// Join / echo
//============================================================================

JAM_TEST (livejamsession, tapTempoLocksClock)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);

    const JamLiveState state = stateOf (*session);
    CHECK (state.clock.lockState == jam::ClockLockState::Locked);
    CHECK_NEAR (state.clock.bpm, 120.0, 1.0);
}

JAM_TEST (livejamsession, startJoinsAtNextBarThenEchoConfirms)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    (void) drainCommands (*session);

    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    session->stepControlForTesting();

    {
        const JamLiveState state = stateOf (*session);
        CHECK (state.requestedRunning);
        CHECK (state.joinPending);
        CHECK (! state.drumsPlaying);
    }

    const auto commands = drainCommands (*session);
    CHECK (countType (commands, DrumClockCommandType::JoinAtBar) == 1);

    // Audio owner reports the injected transport is really rendering.
    session->publishDrumEcho (jam::DrumPlaybackEcho { true, true, true, 0, 0 });
    session->stepControlForTesting();

    const JamLiveState afterEcho = stateOf (*session);
    CHECK (afterEcho.drumsPlaying);
    CHECK (! afterEcho.joinPending);
    CHECK (afterEcho.requestedRunning);

    // A second tick must not spam another join.
    session->stepControlForTesting();
    const auto more = drainCommands (*session);
    CHECK (countType (more, DrumClockCommandType::JoinAtBar) == 0);
}

JAM_TEST (livejamsession, echoDistinguishesPendingFromPlaying)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    session->stepControlForTesting();

    // Attached but not yet playing: still pending, not playing.
    session->publishDrumEcho (jam::DrumPlaybackEcho { true, true, false, 0, 0 });
    session->stepControlForTesting();
    {
        const JamLiveState state = stateOf (*session);
        CHECK (state.joinPending);
        CHECK (! state.drumsPlaying);
    }

    session->publishDrumEcho (jam::DrumPlaybackEcho { true, true, true, 0, 0 });
    session->stepControlForTesting();
    {
        const JamLiveState state = stateOf (*session);
        CHECK (! state.joinPending);
        CHECK (state.drumsPlaying);
    }
}

//============================================================================
// Clock commands through the session
//============================================================================

JAM_TEST (livejamsession, halfTimeScalesClockTempo)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    const double before = stateOf (*session).clock.bpm;

    session->submitCommand (JamLiveCommand { JamLiveCommandType::HalfTime, 0.0 });
    session->stepControlForTesting();

    const JamLiveState state = stateOf (*session);
    CHECK_NEAR (state.clock.bpm, before * 0.5, 1.0);
    // The bridge must have been re-phased so its grid follows the clock.
    const auto commands = drainCommands (*session);
    CHECK (countType (commands, DrumClockCommandType::ResyncBar) >= 1);
}

JAM_TEST (livejamsession, doubleTimeScalesClockTempo)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    const double before = stateOf (*session).clock.bpm;

    session->submitCommand (JamLiveCommand { JamLiveCommandType::DoubleTime, 0.0 });
    session->stepControlForTesting();

    // Double of 120 is 240, but the clock's auto-follow ceiling (220) clamps it.
    const double after = stateOf (*session).clock.bpm;
    CHECK (after > before);
    CHECK_LE (after, 220.0 + 1.0e-6);
}

JAM_TEST (livejamsession, freezeAndResumeActThroughClock)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    CHECK (! stateOf (*session).clock.tempoFrozen);

    session->submitCommand (JamLiveCommand { JamLiveCommandType::FreezeTempo, 0.0 });
    session->stepControlForTesting();
    CHECK (stateOf (*session).clock.tempoFrozen);

    session->submitCommand (JamLiveCommand { JamLiveCommandType::ResumeFollow, 0.0 });
    session->stepControlForTesting();
    CHECK (! stateOf (*session).clock.tempoFrozen);
}

JAM_TEST (livejamsession, setModeChangesClockMode)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));

    session->submitCommand (JamLiveCommand { JamLiveCommandType::SetMode,
                                             (double) (int) jam::TempoMode::Loose });
    session->stepControlForTesting();
    CHECK (stateOf (*session).mode == jam::TempoMode::Loose);
}

JAM_TEST (livejamsession, resyncNextBarReachesBothClockAndBridge)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    (void) drainCommands (*session);

    session->publishAudioCursor (27000);
    session->submitCommand (JamLiveCommand { JamLiveCommandType::ResyncNextBar, 0.0 });
    session->stepControlForTesting();

    const auto commands = drainCommands (*session);
    CHECK (countType (commands, DrumClockCommandType::ResyncBar) >= 1);
    CHECK_NEAR (stateOf (*session).clock.barPhase01, 0.0, 0.02);
}

//============================================================================
// Stop / reset
//============================================================================

JAM_TEST (livejamsession, stopAtNextBarStopsAndDoesNotAutoResume)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    session->stepControlForTesting();
    session->publishDrumEcho (jam::DrumPlaybackEcho { true, true, true, 0, 0 });
    session->stepControlForTesting();
    (void) drainCommands (*session);

    session->submitCommand (JamLiveCommand { JamLiveCommandType::Stop, 0.0 });
    session->stepControlForTesting();

    {
        const JamLiveState state = stateOf (*session);
        CHECK (! state.requestedRunning);
    }
    const auto commands = drainCommands (*session);
    CHECK (countType (commands, DrumClockCommandType::StopAtBar) == 1);

    // Later ticks must not rejoin even with the lock still valid.
    session->publishDrumEcho (jam::DrumPlaybackEcho { true, true, false, 0, 0 });
    session->stepControlForTesting();
    session->stepControlForTesting();
    const auto later = drainCommands (*session);
    CHECK (countType (later, DrumClockCommandType::JoinAtBar) == 0);
    CHECK (! stateOf (*session).requestedRunning);
}

JAM_TEST (livejamsession, resetClearsAndDoesNotAutoResume)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    lockClockWithTaps (*session);
    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    session->stepControlForTesting();
    session->publishDrumEcho (jam::DrumPlaybackEcho { true, true, true, 0, 0 });
    session->stepControlForTesting();
    (void) drainCommands (*session);

    session->submitCommand (JamLiveCommand { JamLiveCommandType::Reset, 0.0 });
    session->stepControlForTesting();

    {
        const JamLiveState state = stateOf (*session);
        CHECK (! state.requestedRunning);
        CHECK (! state.joinPending);
        CHECK (state.clock.lockState == jam::ClockLockState::Acquiring);
    }
    const auto commands = drainCommands (*session);
    CHECK (countType (commands, DrumClockCommandType::StopAtBar) >= 1);

    // No automatic resume after Reset.
    session->stepControlForTesting();
    {
        const JamLiveState state = stateOf (*session);
        CHECK (! state.requestedRunning);
        CHECK (! state.joinPending);
    }
}

//============================================================================
// Tap chunking / pressure
//============================================================================

JAM_TEST (livejamsession, chunkedAudioIsNotTruncated)
{
    StubTracker* tracker = nullptr;
    auto session = makeSession (&tracker);
    REQUIRE (session->prepare (48000.0, 512, false));

    // A 6144-frame callback split into three <=2048 chunks, as the processor
    // must do: no remainder is silently dropped.
    session->pushAudio (nullptr, 0, 0, 48000.0); // cursor at 0
    float chunk[jam::kMaxAnalysisBlock] = {};
    for (uint32_t c = 0; c < 3; ++c)
        session->pushAudio (chunk, jam::kMaxAnalysisBlock,
                            (uint64_t) c * jam::kMaxAnalysisBlock, 48000.0);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (tracker->samplesSeen.load (std::memory_order_relaxed) < 6144
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));

    CHECK_EQ (tracker->samplesSeen.load (std::memory_order_relaxed),
              (uint64_t) 6144);
    CHECK_EQ (session->audioCursor(), (uint64_t) 6144);
}

JAM_TEST (livejamsession, ringPressureCountsAnalysisDrops)
{
    LiveJamSessionConfig config = testConfig();
    config.audioRingCapacity = 2;

    StubTracker* tracker = nullptr;
    auto session = makeSession (&tracker, config);
    tracker->blockProcess = true;
    REQUIRE (session->prepare (48000.0, 512, false));

    float chunk[jam::kMaxAnalysisBlock] = {};
    for (uint32_t c = 0; c < 5; ++c)
        session->pushAudio (chunk, jam::kMaxAnalysisBlock,
                            (uint64_t) c * jam::kMaxAnalysisBlock, 48000.0);

    session->stepControlForTesting();
    const JamLiveState state = stateOf (*session);
    CHECK_GE (state.analysisDrops, (uint64_t) 2);

    tracker->unblock();
    session->release();
}

//============================================================================
// Generations
//============================================================================

JAM_TEST (livejamsession, staleGenerationCommandRejected)
{
    auto session = makeSession();

    // Submitted before any prepare: tagged with generation 0.
    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    REQUIRE (session->prepare (48000.0, 512, false)); // generation 1
    session->stepControlForTesting();

    {
        const JamLiveState state = stateOf (*session);
        CHECK (! state.requestedRunning);
        CHECK_GE (state.userCommandDrops, (uint64_t) 1);
    }

    // A fresh Start under the current generation is accepted.
    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    session->stepControlForTesting();
    CHECK (stateOf (*session).requestedRunning);
}

JAM_TEST (livejamsession, secondPrepareUsesNewGeneration)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    session->stepControlForTesting();
    const uint64_t first = stateOf (*session).sessionGeneration;

    REQUIRE (session->prepare (48000.0, 512, false));
    session->stepControlForTesting();
    CHECK (stateOf (*session).sessionGeneration > first);
}

//============================================================================
// Receipt / event / horizon; fail-closed evidence
//============================================================================

JAM_TEST (livejamsession, receiptStampedFromAudioCursor)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    session->publishAudioCursor (5000);
    CHECK (session->injectObservationForTesting (observation (1000, 2000)));

    session->stepControlForTesting();
    const JamLiveState state = stateOf (*session);
    CHECK (state.receiptMeasured);
    CHECK_EQ (state.lastReceiptSampleTime, (uint64_t) 5000);
    CHECK_EQ (state.lastEventSampleTime, (uint64_t) 1000);
    CHECK_EQ (state.lastInputHorizonSampleTime, (uint64_t) 2000);
}

JAM_TEST (livejamsession, futureObservationFailsClosed)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    session->publishAudioCursor (1000);
    CHECK (session->injectObservationForTesting (observation (2000, 2000)));

    session->stepControlForTesting();
    const JamLiveState state = stateOf (*session);
    CHECK_EQ (state.lastEventSampleTime, (uint64_t) 0);
    CHECK_GE (state.observationDrops, (uint64_t) 1);
}

JAM_TEST (livejamsession, agedObservationFailsClosed)
{
    LiveJamSessionConfig config = testConfig();
    config.maxObservationAgeSamples = 1000;

    auto session = makeSession (nullptr, config);
    REQUIRE (session->prepare (48000.0, 512, false));
    session->publishAudioCursor (5000);
    CHECK (session->injectObservationForTesting (observation (1000, 1000)));

    session->stepControlForTesting();
    const JamLiveState state = stateOf (*session);
    CHECK_EQ (state.lastEventSampleTime, (uint64_t) 0);
    CHECK_GE (state.observationDrops, (uint64_t) 1);
}

//============================================================================
// Clock from cursor / discontinuity
//============================================================================

JAM_TEST (livejamsession, clockAdvancesDuringSilence)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));

    session->publishAudioCursor (0);
    session->stepControlForTesting();
    const double phase0 = stateOf (*session).clock.beatPhase01;

    // One second of silence: the clock still advances from the audio cursor.
    session->publishAudioCursor (48000);
    session->stepControlForTesting();
    const JamLiveState state = stateOf (*session);
    CHECK_EQ (state.audioSampleTime, (uint64_t) 48000);
    CHECK (std::fabs (state.clock.beatPhase01 - phase0) > 0.1);
}

JAM_TEST (livejamsession, largeForwardJumpIsDiscontinuity)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, false));
    session->publishAudioCursor (100000);
    session->stepControlForTesting();
    const uint64_t before = stateOf (*session).discontinuities;

    // A jump larger than the bridge's one-minute discontinuity rail: the grid is
    // re-anchored and the discontinuity is counted rather than fabricating a
    // minute of elapsed beats.
    session->publishAudioCursor (100000 + 60ull * 48000ull + 48000ull);
    session->stepControlForTesting();
    CHECK (stateOf (*session).discontinuities > before);
}

//============================================================================
// Worker lifecycle
//============================================================================

JAM_TEST (livejamsession, workerShutdownAndSecondPrepare)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, true));
    CHECK (session->running());

    session->publishAudioCursor (4800);
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
    (void) stateOf (*session);

    session->release();
    CHECK (! session->running());

    // A device re-prepare must be clean and keep the object usable.
    REQUIRE (session->prepare (48000.0, 512, true));
    session->publishAudioCursor (9600);
    std::this_thread::sleep_for (std::chrono::milliseconds (5));
    const JamLiveState state = stateOf (*session);
    CHECK (state.prepared);
    CHECK_EQ (state.sampleRate, 48000.0);
    session->release();
    CHECK (! session->running());
}

JAM_TEST (livejamsession, destructorJoinsCleanly)
{
    auto session = makeSession();
    REQUIRE (session->prepare (48000.0, 512, true));
    session->publishAudioCursor (1000);
    session->submitCommand (JamLiveCommand { JamLiveCommandType::Start, 0.0 });
    // Destructor (not release) must stop and join the worker.
    session.reset();
    CHECK (true);
}
