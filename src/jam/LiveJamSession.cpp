#include "LiveJamSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace jam
{

namespace
{
// Unit conversion. Not a tunable: 60 seconds in a minute.
constexpr double kSecondsPerMinute = 60.0;

// Bounded per-tick work. Caps how long release()/stopWorker() can wait behind a
// saturated corner without ever spinning unbounded, and keeps the control loop
// deterministic for the manual test seam.
constexpr std::size_t kMaxCommandsPerTick = 32;
constexpr std::size_t kMaxObservationsPerTick = 64;

// Worker discovery interval. The audio callback never notifies us (SPEC 7.1),
// so the worker polls with a short timed wait. Worker-side and non-RT.
constexpr auto kControlIdlePoll = std::chrono::milliseconds (1);

ClockCommand clockCommand (ClockCommandType type, std::uint64_t tapSampleTime) noexcept
{
    ClockCommand command;
    command.type = type;
    command.tapSampleTime = tapSampleTime;
    return command;
}
} // namespace

LiveJamSession::LiveJamSession (const LiveJamSessionConfig& config)
    : config_ (config),
      ring_ (config.audioRingCapacity),
      clock_ (config.clock),
      bridge_ (config.bridge),
      policy_ (config.policy)
{
}

LiveJamSession::~LiveJamSession()
{
    release();
}

bool LiveJamSession::setTracker (std::unique_ptr<IRhythmTracker> tracker) noexcept
{
    // Ownership is handed to the analyzer exactly once. A second hand-over after
    // the analyzer exists would make the live tracker ambiguous.
    if (analyzer_ != nullptr)
        return false;

    pendingTracker_ = std::move (tracker);
    return true;
}

void LiveJamSession::resetSessionStats() noexcept
{
    baseRingOverruns_ = ring_.overrunCount();
    baseCommandDrops_ = commands_.droppedCount();
    staleCommandRejects_ = 0;
    agedObservationDrops_ = 0;
    discontinuities_ = 0;
}

bool LiveJamSession::prepare (double sampleRate, int maximumBlockSize,
                              bool startControlThread)
{
    if (! (std::isfinite (sampleRate) && sampleRate > 0.0))
        return false;

    // --- quiescent lifecycle boundary -------------------------------------
    stopWorker();
    if (analyzer_ != nullptr)
        analyzer_->stop();

    sessionGeneration_ = generation_.fetch_add (1, std::memory_order_relaxed) + 1;
    sampleRate_ = sampleRate;

    // Time and grids restart at this origin. The audio side is expected to have
    // reset its own cursor to 0 in the same prepare boundary, so the worker
    // clock and the attached DrumEngine share the domain.
    ring_.reset();
    bridge_.prepare (sampleRate, maximumBlockSize);
    clock_.reset();
    policy_.reset();

    haveClockAnchor_ = false;
    clockSampleTime_ = 0;
    requestedRunning_ = false;
    lastEcho_ = DrumPlaybackEcho {};
    discontinuitySeen_ = false;
    haveAnalyzerGeneration_ = false;
    lastAnalyzerGeneration_ = 0;
    lastEventSampleTime_ = 0;
    lastInputHorizon_ = 0;
    lastReceipt_ = 0;
    receiptMeasured_ = false;
    candidateBpm_ = 0.0f;
    heldInputPeak_ = 0.0f;
    inputPeak_.store (0.0f, std::memory_order_relaxed);
    audioCursor_.store (0, std::memory_order_relaxed);

    // --- analyzer -----------------------------------------------------------
    if (analyzer_ == nullptr)
    {
        haveTracker_ = pendingTracker_ != nullptr;
        analyzer_ = std::make_unique<RhythmAnalyzer> (ring_, std::move (pendingTracker_));
    }

    backend_ = haveTracker_ ? config_.availableBackend : JamLiveBackend::unavailable;
    failure_ = JamLiveFailure::none;

    const AnalyzerStartResult startResult = analyzer_->start (sampleRate);
    if (startResult == AnalyzerStartResult::noTracker)
        failure_ = JamLiveFailure::unavailableBackend;
    else if (startResult != AnalyzerStartResult::started)
        failure_ = JamLiveFailure::workerFailure;

    // UI commands are kept across a re-prepare and rejected by GENERATION once
    // the worker runs, so a command that was in flight while prepare() bumped the
    // generation can never be applied to the new session. Rebaseline the
    // cumulative drop counter so userCommandDrops is per-session.
    resetSessionStats();

    prepared_.store (true, std::memory_order_release);
    state_.invalidate();

    if (startControlThread)
        startWorker();

    return true;
}

void LiveJamSession::release() noexcept
{
    stopWorker();
    if (analyzer_ != nullptr)
        analyzer_->stop();

    prepared_.store (false, std::memory_order_release);
    state_.invalidate();
}

void LiveJamSession::pushAudio (const float* mono, std::uint32_t numSamples,
                                std::uint64_t sampleTime, double sampleRate) noexcept
{
    if (mono != nullptr && numSamples > 0)
    {
        ring_.push (mono, numSamples, sampleTime, sampleRate);

        float peak = 0.0f;
        for (std::uint32_t i = 0; i < numSamples; ++i)
        {
            const float a = std::fabs (mono[i]);
            if (a > peak)
                peak = a;
        }
        if (peak > inputPeak_.load (std::memory_order_relaxed))
            inputPeak_.store (peak, std::memory_order_relaxed);

        publishAudioCursor (sampleTime + static_cast<std::uint64_t> (numSamples));
    }
    else
    {
        publishAudioCursor (sampleTime);
    }
}

void LiveJamSession::publishAudioCursor (std::uint64_t cursor) noexcept
{
    const std::uint64_t current = audioCursor_.load (std::memory_order_relaxed);
    if (cursor > current)
        audioCursor_.store (cursor, std::memory_order_relaxed);
}

void LiveJamSession::publishDrumEcho (const DrumPlaybackEcho& echo) noexcept
{
    echo_.publish (echo);
}

bool LiveJamSession::submitCommand (const JamLiveCommand& command) noexcept
{
    QueuedCommand queued;
    queued.command = command;
    queued.generation = generation_.load (std::memory_order_acquire);
    return commands_.push (queued);
}

bool LiveJamSession::readState (JamLiveState& out) const noexcept
{
    return state_.tryRead (out);
}

bool LiveJamSession::injectObservationForTesting (const ObservationEnvelope& envelope) noexcept
{
    return injectedObservations_.push (envelope);
}

void LiveJamSession::stepControlForTesting() noexcept
{
    stepControl (audioCursor_.load (std::memory_order_acquire));
}

bool LiveJamSession::popObservation (ObservationEnvelope& out) noexcept
{
    // Deterministic test envelopes take priority so a manually-stepped session
    // does not depend on the analyzer thread.
    if (injectedObservations_.pop (out))
        return true;
    if (analyzer_ != nullptr)
        return analyzer_->popObservation (out);
    return false;
}

void LiveJamSession::advanceClockTo (std::uint64_t cursor, double rate) noexcept
{
    if (! haveClockAnchor_)
    {
        clock_.advance (cursor, rate);
        clockSampleTime_ = cursor;
        haveClockAnchor_ = true;
        return;
    }

    if (cursor < clockSampleTime_
        || cursor - clockSampleTime_ > config_.bridge.maxForwardJumpSamples)
    {
        // Backwards or implausibly large jump: the grid we derived no longer
        // describes the audio timeline. Re-anchor at the new origin without
        // fabricating elapsed beats, and count the discontinuity.
        clock_.reset();
        clock_.advance (cursor, rate);
        clockSampleTime_ = cursor;
        ++discontinuities_;
        discontinuitySeen_ = true;
        return;
    }

    const std::uint64_t delta = cursor - clockSampleTime_;
    if (delta == 0)
        return;

    clock_.advance (delta, rate);
    clockSampleTime_ = cursor;
}

void LiveJamSession::processObservation (const ObservationEnvelope& envelope,
                                         std::uint64_t cursor) noexcept
{
    // The receipt is stamped from the ACTUAL audio cursor at the moment the
    // control worker popped the envelope. The envelope itself is left
    // unmodified: this is a separate, block-resolution availability signal, not
    // a physical device-latency claim and not fabricated sub-block timing.
    lastReceipt_ = cursor;
    receiptMeasured_ = true;

    // Distinguish a fresh tracker stream from a tracker reset: a generation
    // change is a discontinuity, so the clock is not silently fed evidence from
    // a different stream.
    if (haveAnalyzerGeneration_ && envelope.streamGeneration != lastAnalyzerGeneration_)
    {
        ++discontinuities_;
        discontinuitySeen_ = true;
    }
    lastAnalyzerGeneration_ = envelope.streamGeneration;
    haveAnalyzerGeneration_ = true;

    const std::uint64_t event = envelope.observation.inputSampleTime;

    // Fail closed: an event that claims a future time, or one older than the
    // bounded age, is dropped and counted rather than applied to a clock that has
    // already moved on. This keeps the event phase honest.
    if (event > cursor
        || cursor - event > config_.maxObservationAgeSamples)
    {
        ++agedObservationDrops_;
        return;
    }

    lastEventSampleTime_ = event;
    lastInputHorizon_ = envelope.inputHorizonSampleTime;
    candidateBpm_ = envelope.observation.bpmCandidate;
    clock_.observe (envelope.observation);
}

void LiveJamSession::syncBridgePhaseToClock (std::uint64_t cursor) noexcept
{
    const ClockSnapshot snapshot = clock_.snapshot();
    if (snapshot.lockState == ClockLockState::Lost)
        return;
    if (! (snapshot.bpm > 0.0) || ! std::isfinite (snapshot.bpm))
        return;

    const double samplesPerBeat =
        kSecondsPerMinute * sampleRate_ / snapshot.bpm;
    double beatsToNextBar =
        (1.0 - snapshot.barPhase01) * static_cast<double> (snapshot.beatsPerBar);
    if (beatsToNextBar <= 1.0e-9)
        beatsToNextBar += static_cast<double> (snapshot.beatsPerBar);

    const double offset = beatsToNextBar * samplesPerBeat;
    const std::uint64_t target =
        cursor + static_cast<std::uint64_t> (std::llround (offset));
    if (target > cursor)
        bridge_.requestResyncNextBar (target);
}

void LiveJamSession::applyCommand (const JamLiveCommand& command,
                                   std::uint64_t cursor) noexcept
{
    switch (command.type)
    {
        case JamLiveCommandType::Start:
            if (! haveTracker_)
            {
                // No live backend in this build: reject Start and report the
                // failure. Never fall back to a simulator.
                failure_ = JamLiveFailure::unavailableBackend;
                requestedRunning_ = false;
                return;
            }
            if (failure_ == JamLiveFailure::unavailableBackend)
                failure_ = JamLiveFailure::none;
            requestedRunning_ = true;
            break;

        case JamLiveCommandType::Stop:
        case JamLiveCommandType::StopAtNextBar:
            // Stop is a bounded commitment at the next bar boundary; it never
            // tears down the pipeline. The policy turns this into a stop and
            // clears the engaged state.
            requestedRunning_ = false;
            break;

        case JamLiveCommandType::TapTempo:
            clock_.command (clockCommand (ClockCommandType::TapTempo, cursor));
            syncBridgePhaseToClock (cursor);
            break;

        case JamLiveCommandType::ResyncNextBeat:
            clock_.command (clockCommand (ClockCommandType::ResyncNextBeat, cursor));
            // Same absolute phase statement to the engine so both grids re-phase
            // together. The clock remains the tempo authority.
            bridge_.requestResyncNextBeat (cursor);
            break;

        case JamLiveCommandType::ResyncNextBar:
            clock_.command (clockCommand (ClockCommandType::ResyncNextBar, cursor));
            bridge_.requestResyncNextBar (cursor);
            break;

        case JamLiveCommandType::HalfTime:
            clock_.command (clockCommand (ClockCommandType::HalfTime, 0));
            // The clock owns tempo and phase; the bridge follows by landing its
            // next downbeat on the clock's. No raw BPM is written to the engine.
            syncBridgePhaseToClock (cursor);
            break;

        case JamLiveCommandType::DoubleTime:
            clock_.command (clockCommand (ClockCommandType::DoubleTime, 0));
            syncBridgePhaseToClock (cursor);
            break;

        case JamLiveCommandType::FreezeTempo:
            clock_.command (clockCommand (ClockCommandType::FreezeTempo, 0));
            break;

        case JamLiveCommandType::ResumeFollow:
            clock_.command (clockCommand (ClockCommandType::ResumeFollow, 0));
            break;

        case JamLiveCommandType::SetMode:
        {
            const int requested = static_cast<int> (std::lround (command.value));
            if (requested >= 0 && requested < kTempoModeCount)
                clock_.command (clockCommand (ClockCommandType::SetMode,
                                              static_cast<std::uint64_t> (requested)));
            break;
        }

        case JamLiveCommandType::Reset:
            // A full session reset: the clock forgets tempo/phase, the policy
            // forgets its engagement and the grid is safely stopped. The user
            // must press Start again; there is no automatic resume.
            clock_.command (clockCommand (ClockCommandType::Reset, 0));
            haveClockAnchor_ = false;
            clockSampleTime_ = 0;
            requestedRunning_ = false;
            policy_.reset();
            bridge_.requestStopAtNextBar();
            discontinuitySeen_ = true;
            ++discontinuities_;
            break;
    }
}

void LiveJamSession::stepControl (std::uint64_t cursor) noexcept
{
    // 1. Fold the audio owner's drum echo. The control worker is the only reader.
    DrumPlaybackEcho echo;
    if (echo_.tryRead (echo))
        lastEcho_ = echo;

    // 2. Advance the clock from the actual audio cursor, whether or not any
    //    evidence arrived and whether or not Jam is playing.
    advanceClockTo (cursor, sampleRate_);

    // 3. Anchor the bridge's grid to the same absolute cursor before any command
    //    that needs an up-to-date `now`.
    if (! bridge_.setClockSample (cursor))
    {
        // The bridge saw a discontinuity and already published a Clear.
        ++discontinuities_;
        discontinuitySeen_ = true;
    }

    // 4. Drain UI commands. Stale-generation commands (submitted before a
    //    re-prepare) are rejected here rather than applied.
    {
        QueuedCommand queued;
        std::size_t applied = 0;
        while (applied < kMaxCommandsPerTick && commands_.pop (queued))
        {
            ++applied;
            if (queued.generation != sessionGeneration_)
            {
                // In flight across a re-prepare: reject rather than apply to the
                // new session.
                ++staleCommandRejects_;
                continue;
            }
            applyCommand (queued.command, cursor);
        }
    }

    // 5. Drain observations (injected test evidence first, then the analyzer).
    {
        ObservationEnvelope envelope;
        std::size_t drained = 0;
        while (drained < kMaxObservationsPerTick && popObservation (envelope))
        {
            ++drained;
            processObservation (envelope, cursor);
        }
    }

    // 6. Publish the (possibly updated) clock belief to the bridge. This only
    //    stages tempo at the next bar boundary; it never writes a raw detector
    //    value to the engine.
    bridge_.applySnapshot (clock_.snapshot());

    // 7. Minimal director: one join from a usable lock, hold through holdover,
    //    safe stop on loss/reset.
    const JamJoinDecision decision =
        policy_.update (clock_.snapshot(), requestedRunning_, discontinuitySeen_);
    discontinuitySeen_ = false;

    switch (decision.action)
    {
        case JamJoinAction::joinAtNextBar:
            bridge_.requestJoinAtNextBar (config_.groove);
            break;
        case JamJoinAction::stopAtNextBar:
        case JamJoinAction::clearNow:
            // A discontinuity has already asked the engine to Clear; a plain
            // loss only needs the grid stopped.
            bridge_.requestStopAtNextBar();
            break;
        case JamJoinAction::none:
            break;
    }

    // 8. Confirm/abandon a pending join from the real audio-owner echo.
    policy_.notifyPlaybackEcho (lastEcho_.attached
                                && lastEcho_.injectedActive
                                && lastEcho_.injectedPlaying);

    // 9. One coherent state publication for the UI reader.
    publishState (cursor);
}

void LiveJamSession::publishState (std::uint64_t cursor) noexcept
{
    JamLiveState state;
    state.sessionGeneration = sessionGeneration_;
    state.prepared = prepared_.load (std::memory_order_relaxed);
    state.requestedRunning = requestedRunning_;
    state.joinPending = policy_.joinPending();
    state.drumsPlaying = lastEcho_.attached
                         && lastEcho_.injectedActive
                         && lastEcho_.injectedPlaying;
    state.backend = backend_;
    state.failure = failure_;
    state.clock = clock_.snapshot();
    state.mode = clock_.mode();
    state.candidateBpm = candidateBpm_;

    const float peak = inputPeak_.exchange (0.0f, std::memory_order_relaxed);
    heldInputPeak_ = std::max (peak, heldInputPeak_ * 0.85f);
    state.inputPeak = heldInputPeak_;

    state.sampleRate = sampleRate_;
    state.audioSampleTime = cursor;
    state.lastEventSampleTime = lastEventSampleTime_;
    state.lastInputHorizonSampleTime = lastInputHorizon_;
    state.lastReceiptSampleTime = lastReceipt_;
    state.receiptMeasured = receiptMeasured_;

    const std::uint64_t ringOverruns = ring_.overrunCount();
    state.analysisDrops = ringOverruns >= baseRingOverruns_
                              ? ringOverruns - baseRingOverruns_ : 0;

    std::uint64_t analyzerDrops = 0;
    if (analyzer_ != nullptr)
        analyzerDrops = analyzer_->stats().droppedObservations;
    state.observationDrops = analyzerDrops + agedObservationDrops_;

    const std::uint64_t queueDrops = commands_.droppedCount() >= baseCommandDrops_
                                         ? commands_.droppedCount() - baseCommandDrops_ : 0;
    state.userCommandDrops = queueDrops + staleCommandRejects_;
    state.drumCommandDrops = bridge_.queueDropCount();
    state.discontinuities = discontinuities_;

    state_.publish (state);
}

void LiveJamSession::startWorker() noexcept
{
    {
        std::lock_guard<std::mutex> lock (lifecycleMutex_);
        stopRequested_ = false;
    }

    running_.store (true, std::memory_order_release);
    try
    {
        thread_ = std::thread (&LiveJamSession::workerLoop, this);
    }
    catch (...)
    {
        running_.store (false, std::memory_order_release);
        failure_ = JamLiveFailure::workerFailure;
    }
}

void LiveJamSession::stopWorker() noexcept
{
    {
        std::lock_guard<std::mutex> lock (lifecycleMutex_);
        stopRequested_ = true;
    }
    lifecycleCv_.notify_all();

    if (thread_.joinable())
        thread_.join();

    running_.store (false, std::memory_order_release);
}

void LiveJamSession::workerLoop() noexcept
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock (lifecycleMutex_);
            if (stopRequested_)
                break;
        }

        stepControl (audioCursor_.load (std::memory_order_acquire));

        std::unique_lock<std::mutex> lock (lifecycleMutex_);
        if (stopRequested_)
            break;
        lifecycleCv_.wait_for (lock, kControlIdlePoll,
                               [this] { return stopRequested_; });
        if (stopRequested_)
            break;
    }

    running_.store (false, std::memory_order_release);
}

} // namespace jam
