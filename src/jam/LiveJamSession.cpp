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
      policy_ (config.policy),
      director_ (config.director)
{
}

LiveJamSession::~LiveJamSession()
{
    release();
}

bool LiveJamSession::setTracker (std::unique_ptr<IRhythmTracker> tracker,
                                 JamLiveBackend backend) noexcept
{
    // Legal before the first prepare and after a release()/re-prepare, but never
    // while the control worker runs or the session is prepared: ownership of a
    // live tracker must stay unambiguous.
    if (running_.load (std::memory_order_acquire))
        return false;
    if (prepared_.load (std::memory_order_acquire))
        return false;

    pendingTracker_ = std::move (tracker);
    pendingBackend_ = backend;
    haveTracker_ = pendingTracker_ != nullptr;
    backend_ = haveTracker_ ? backend : JamLiveBackend::unavailable;

    // Drop any stopped analyzer so the next prepare rebuilds it around the new
    // tracker. Safe: not running and not prepared.
    analyzer_.reset();
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

void LiveJamSession::publishColdState (bool prepared) noexcept
{
    // Sole-publisher path: the control worker is joined here. Publishing a cold
    // coherent state means a stale prepared/playing payload can never leak
    // across a prepare/release.
    JamLiveState state;
    state.sessionGeneration = sessionGeneration_;
    state.prepared = prepared;
    state.sampleRate = sampleRate_;
    state.backend = backend_;
    state.failure = failure_;
    state.requestedRunning = false;
    state.joinPending = false;
    state.drumsPlaying = false;
    const auto settings = director_.settings();
    state.styleIndex = static_cast<int> (settings.style);
    state.intensity01 = settings.intensity01;
    state.complexity01 = settings.complexity01;
    state.fillAmount01 = settings.fillAmount01;
    state_.publish (state);
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

    ring_.reset();
    bridge_.prepare (sampleRate, maximumBlockSize);
    clock_.reset();
    policy_.reset();
    director_.reset (director_.settings());
    lastFeatures_ = RhythmObservation {};
    adaptiveChangeTarget_ = 0;
    havePerformanceCursor_ = haveOnsetSample_ = false;
    lastPerformanceCursor_ = lastOnsetSample_ = 0;

    haveClockAnchor_ = false;
    clockSampleTime_ = 0;
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
        analyzer_ = std::make_unique<RhythmAnalyzer> (ring_, std::move (pendingTracker_));

    if (backend_ != JamLiveBackend::unavailable)
        failure_ = JamLiveFailure::none;

    const AnalyzerStartResult startResult = analyzer_->start (sampleRate);
    if (startResult == AnalyzerStartResult::noTracker)
        failure_ = JamLiveFailure::unavailableBackend;
    else if (startResult != AnalyzerStartResult::started)
        failure_ = JamLiveFailure::workerFailure;

    resetSessionStats();

    // Publish the new generation's cold prepared state BEFORE the worker starts,
    // so no previous snapshot can be read under the new generation.
    prepared_.store (true, std::memory_order_release);
    publishColdState (true);
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

    // Cold released state from the lifecycle owner (worker joined), then a
    // re-read hint. The latest-value slot itself is never destroyed.
    publishColdState (false);
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
    if (! std::isfinite (command.value))
        return false;
    switch (command.type)
    {
        case JamLiveCommandType::SetStyle:
            if (command.value < 0.0 || command.value > 5.0
                || std::floor (command.value) != command.value)
                return false;
            break;
        case JamLiveCommandType::SetIntensity:
        case JamLiveCommandType::SetComplexity:
        case JamLiveCommandType::SetFillAmount:
            if (command.value < 0.0 || command.value > 1.0)
                return false;
            break;
        case JamLiveCommandType::SetMode:
            if (command.value < 0.0 || command.value >= kTempoModeCount
                || std::floor (command.value) != command.value)
                return false;
            break;
        default:
            if (static_cast<int> (command.type) < 0
                || static_cast<int> (command.type) > static_cast<int> (JamLiveCommandType::RequestFill))
                return false;
            break;
    }
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
    if (injectedObservations_.pop (out))
        return true;
    if (analyzer_ != nullptr)
        return analyzer_->popObservation (out);
    return false;
}

bool LiveJamSession::advanceClockTo (std::uint64_t cursor, double rate) noexcept
{
    if (! haveClockAnchor_)
    {
        clock_.advance (cursor, rate);
        clockSampleTime_ = cursor;
        haveClockAnchor_ = true;
        return false;
    }

    if (cursor < clockSampleTime_
        || cursor - clockSampleTime_ > config_.bridge.maxForwardJumpSamples)
    {
        // Backwards or implausibly large jump: re-anchor at the new origin
        // without fabricating elapsed beats. The caller counts this once.
        clock_.reset();
        clock_.advance (cursor, rate);
        clockSampleTime_ = cursor;
        return true;
    }

    const std::uint64_t delta = cursor - clockSampleTime_;
    if (delta == 0)
        return false;

    clock_.advance (delta, rate);
    clockSampleTime_ = cursor;
    return false;
}

void LiveJamSession::processObservation (const ObservationEnvelope& envelope,
                                         std::uint64_t cursor) noexcept
{
    // Receipt: the ACTUAL audio cursor at the moment the worker popped the
    // envelope. The envelope itself is left unmodified.
    lastReceipt_ = cursor;
    receiptMeasured_ = true;

    if (haveAnalyzerGeneration_ && envelope.streamGeneration != lastAnalyzerGeneration_)
    {
        ++discontinuities_;
        discontinuitySeen_ = true;
    }
    lastAnalyzerGeneration_ = envelope.streamGeneration;
    haveAnalyzerGeneration_ = true;

    const std::uint64_t event = envelope.observation.inputSampleTime;

    // Fail closed: future or too-old evidence is dropped and counted rather than
    // applied to a clock that has already moved on.
    if (event > cursor || cursor - event > config_.maxObservationAgeSamples)
    {
        ++agedObservationDrops_;
        return;
    }

    lastEventSampleTime_ = event;
    lastInputHorizon_ = envelope.inputHorizonSampleTime;
    candidateBpm_ = envelope.observation.bpmCandidate;
    lastFeatures_ = envelope.observation;
    clock_.observe (envelope.observation);
}

void LiveJamSession::syncBridgePhaseToClock (std::uint64_t cursor) noexcept
{
    const ClockSnapshot snapshot = clock_.snapshot();
    if (snapshot.lockState == ClockLockState::Lost)
        return;
    if (! (snapshot.bpm > 0.0) || ! std::isfinite (snapshot.bpm))
        return;

    const double samplesPerBeat = kSecondsPerMinute * sampleRate_ / snapshot.bpm;
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
                failure_ = JamLiveFailure::unavailableBackend;
                return;
            }
            if (failure_ == JamLiveFailure::unavailableBackend)
                failure_ = JamLiveFailure::none;
            policy_.notifyStart();
            director_.notifySessionStarted();
            break;

        case JamLiveCommandType::Stop:
            // Bounded stop at the next serviced audio block; cancels any queued
            // join and releases injected mode so manual transport works again.
            policy_.notifyStop (JamStopKind::now);
            director_.notifyStopRequested();
            adaptiveChangeTarget_ = 0;
            break;

        case JamLiveCommandType::StopAtNextBar:
            policy_.notifyStop (JamStopKind::nextBar);
            director_.notifyStopRequested();
            adaptiveChangeTarget_ = 0;
            break;

        case JamLiveCommandType::TapTempo:
            clock_.command (clockCommand (ClockCommandType::TapTempo, cursor));
            syncBridgePhaseToClock (cursor);
            break;

        case JamLiveCommandType::ResyncNextBeat:
            clock_.command (clockCommand (ClockCommandType::ResyncNextBeat, cursor));
            bridge_.requestResyncNextBeat (cursor);
            break;

        case JamLiveCommandType::ResyncNextBar:
            clock_.command (clockCommand (ClockCommandType::ResyncNextBar, cursor));
            bridge_.requestResyncNextBar (cursor);
            break;

        case JamLiveCommandType::HalfTime:
            clock_.command (clockCommand (ClockCommandType::HalfTime, 0));
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
            // Immediate-serviced stop + clock forget. No quiescent bridge reset
            // while audio is active: the policy issues a bounded cancel stop.
            clock_.command (clockCommand (ClockCommandType::Reset, 0));
            haveClockAnchor_ = false;
            clockSampleTime_ = 0;
            policy_.notifyReset();
            director_.reset (director_.settings());
            adaptiveChangeTarget_ = 0;
            discontinuitySeen_ = true;
            break;

        case JamLiveCommandType::SetStyle:
        case JamLiveCommandType::SetIntensity:
        case JamLiveCommandType::SetComplexity:
        case JamLiveCommandType::SetFillAmount:
        {
            auto settings = director_.settings();
            if (command.type == JamLiveCommandType::SetStyle)
                settings.style = static_cast<StyleId> (static_cast<int> (command.value));
            else if (command.type == JamLiveCommandType::SetIntensity)
                settings.intensity01 = static_cast<float> (command.value);
            else if (command.type == JamLiveCommandType::SetComplexity)
                settings.complexity01 = static_cast<float> (command.value);
            else
                settings.fillAmount01 = static_cast<float> (command.value);
            director_.setSettings (settings);
            break;
        }
        case JamLiveCommandType::RequestFill:
            if (policy_.requestedRunning() && ! policy_.stopPending())
            {
                auto settings = director_.settings();
                settings.requestFill = true;
                director_.setSettings (settings);
                settings.requestFill = false;
                director_.setSettings (settings);
            }
            break;
    }
}

void LiveJamSession::updatePerformance (std::uint64_t cursor, bool discontinuity) noexcept
{
    const auto snapshot = clock_.snapshot();
    const bool playing = lastEcho_.attached && lastEcho_.injectedActive && lastEcho_.injectedPlaying;
    const bool allowed = policy_.requestedRunning() && ! policy_.stopPending();
    // Re-arm after an actual stopped echo, including Start while a previously
    // accepted Stop is still completing. The join policy remains the authority.
    if (director_.state() == DirectorState::Stopping && ! playing && ! policy_.stopPending())
        director_.notifyStopCompleted();
    if (allowed)
        director_.notifySessionStarted();
    if (! allowed || discontinuity || snapshot.lockState == ClockLockState::Lost)
    {
        director_.cancelPending();
        adaptiveChangeTarget_ = 0;
    }
    const bool advanced = ! havePerformanceCursor_ || cursor != lastPerformanceCursor_;
    if (! advanced && cursor == 0)
        return; // the portable director reserves zero as its test-only sentinel

    DirectorInputs inputs;
    inputs.sessionGeneration = sessionGeneration_;
    inputs.audioCursor = cursor;
    inputs.discontinuity = discontinuity;
    inputs.clock = snapshot;
    inputs.transport = bridge_.position();
    inputs.transport.samplePosition = lastEcho_.samplePosition;
    inputs.transport.playing = playing;
    inputs.playbackEchoPlaying = playing;
    inputs.lifecycleAllowsPerformance = allowed;
    inputs.silence = lastFeatures_.silence || cursor < lastFeatures_.inputSampleTime
                     || cursor - lastFeatures_.inputSampleTime > config_.maxObservationAgeSamples;
    const float span = config_.energyCeilingDbfs - config_.energyFloorDbfs;
    if (! inputs.silence && std::isfinite (lastFeatures_.energyRmsDbfs)
        && std::isfinite (span) && span > 0.0f)
        inputs.energy01 = std::clamp ((lastFeatures_.energyRmsDbfs - config_.energyFloorDbfs) / span, 0.0f, 1.0f);
    inputs.onsetStrength01 = lastFeatures_.onsetStrength01;
    inputs.transientDensity01 = lastFeatures_.transientDensity01;
    // The feature sample identifies an onset once, rather than a repeated UI or
    // worker tick inventing additional onset evidence.
    inputs.onsetEvent = advanced && lastFeatures_.beatEvent
                        && (! haveOnsetSample_ || lastOnsetSample_ != lastFeatures_.inputSampleTime);
    const auto proposal = director_.update (inputs);
    if (inputs.onsetEvent)
    {
        lastOnsetSample_ = lastFeatures_.inputSampleTime;
        haveOnsetSample_ = true;
    }
    lastPerformanceCursor_ = cursor;
    havePerformanceCursor_ = true;
    if (proposal.hasBarChange && allowed && playing
        && snapshot.lockState == ClockLockState::Locked && ! discontinuity)
    {
        const auto target = bridge_.nextBarBoundarySample();
        if (director_.publishPending ([this] (const QueuedBarChange& c) { return bridge_.requestBarChange (c); }))
            adaptiveChangeTarget_ = target;
    }
}

void LiveJamSession::stepControl (std::uint64_t cursor) noexcept
{
    // 1. Fold the audio owner's drum echo; ignore a stale generation.
    DrumPlaybackEcho echo;
    if (echo_.tryRead (echo) && echo.sessionGeneration == sessionGeneration_)
        lastEcho_ = echo;

    // 2. Advance the clock from the actual audio cursor (even during silence).
    bool discontinuity = advanceClockTo (cursor, sampleRate_);

    // 3. Anchor the bridge to the same absolute cursor; a discontinuity there is
    //    the SAME event and is counted once.
    if (! bridge_.setClockSample (cursor))
        discontinuity = true;

    if (discontinuity)
    {
        ++discontinuities_;
        discontinuitySeen_ = true;
    }

    // 4. Drain UI commands; stale-generation commands are rejected.
    {
        QueuedCommand queued;
        std::size_t applied = 0;
        while (applied < kMaxCommandsPerTick && commands_.pop (queued))
        {
            ++applied;
            if (queued.generation != sessionGeneration_)
            {
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

    // 6. Publish the clock belief to the bridge (stages tempo at the next bar).
    bridge_.applySnapshot (clock_.snapshot());

    // 7. Minimal director: join / hold / stop. The bridge's accept/reject is
    //    reported back so a full queue is retried, never latched.
    const JamJoinDecision decision = policy_.update (clock_.snapshot(), discontinuitySeen_);
    const bool performanceDiscontinuity = discontinuitySeen_;
    discontinuitySeen_ = false;

    switch (decision.action)
    {
        case JamJoinAction::joinAtNextBar:
        {
            const auto style = director_.settings().style;
            const auto& descriptor = StyleCatalog::style (style);
            const auto initialGroove = style == StyleId::Rock ? config_.groove
                : descriptor.grooves[static_cast<int> (GrooveTier::Medium)][0].index;
            policy_.notifyJoinAccepted (bridge_.requestJoinAtNextBar (initialGroove));
            break;
        }
        case JamJoinAction::stopNow:
            policy_.notifyStopAccepted (bridge_.requestStopNow());
            break;
        case JamJoinAction::stopAtNextBar:
            policy_.notifyStopAccepted (bridge_.requestStopAtNextBar());
            break;
        case JamJoinAction::none:
            break;
    }

    // 8. Confirm/abandon from the real audio-owner echo.
    policy_.notifyPlaybackEcho (lastEcho_.attached
                                && lastEcho_.injectedActive
                                && lastEcho_.injectedPlaying);

    updatePerformance (cursor, performanceDiscontinuity);

    // 9. One coherent state publication for the UI reader.
    publishState (cursor);
}

void LiveJamSession::publishState (std::uint64_t cursor) noexcept
{
    JamLiveState state;
    state.sessionGeneration = sessionGeneration_;
    state.prepared = prepared_.load (std::memory_order_relaxed);
    state.requestedRunning = policy_.requestedRunning();
    state.joinPending = policy_.joinPending();
    state.drumsPlaying = lastEcho_.attached
                         && lastEcho_.injectedActive
                         && lastEcho_.injectedPlaying;
    state.backend = backend_;
    state.failure = failure_;
    state.clock = clock_.snapshot();
    state.mode = clock_.mode();
    const auto performance = director_.report();
    state.styleIndex = static_cast<int> (performance.settings.style);
    state.intensity01 = performance.settings.intensity01;
    state.complexity01 = performance.settings.complexity01;
    state.fillAmount01 = performance.settings.fillAmount01;
    state.performanceIntensity01 = performance.intensityEnvelope01;
    state.activeGroove = lastEcho_.attached && lastEcho_.injectedActive
                            ? lastEcho_.groove : kNoLibraryEntry;
    state.fillPlaying = state.drumsPlaying && lastEcho_.fillPlaying;
    state.adaptiveChangePending = adaptiveChangeTarget_ != 0
                                 && lastEcho_.samplePosition < adaptiveChangeTarget_;
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
