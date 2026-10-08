#include "RhythmAnalyzer.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace jam
{

namespace
{

// Bounded drain per loop iteration. The worker re-checks the stop flag between
// iterations, so this caps how long stop() can wait behind a continuously full
// ring without ever spinning unbounded on the audio side (the audio thread does
// not participate in this loop at all).
constexpr std::size_t kMaxDrainPerWake = 256;

// Poll period used only when the worker found the ring empty. The audio callback
// never notifies us (SPEC.md 7.1 forbids message posting there), so a short
// timed wait is the discovery mechanism. It is worker-side and non-RT.
constexpr auto kIdlePollInterval = std::chrono::milliseconds (1);

void deleteTracker (IRhythmTracker* tracker) noexcept
{
    delete tracker;
}

// Validates the plugin-ownership contract BEFORE the unique_ptr member is
// constructed, so a throwing constructor never leaves a live pointer paired with
// a null deleter (whose unwind would call a null function pointer).
IRhythmTracker* requirePluginDeleter (IRhythmTracker* tracker,
                                      RhythmAnalyzer::TrackerDeleter deleter)
{
    if (tracker != nullptr && deleter == nullptr)
        throw std::invalid_argument (
            "RhythmAnalyzer: a non-null tracker requires a non-null deleter");
    return tracker;
}

} // namespace

RhythmAnalyzer::RhythmAnalyzer (AnalysisAudioRing& ring,
                                std::unique_ptr<IRhythmTracker> tracker) noexcept
    : ring_ (ring),
      tracker_ (tracker.release(), &deleteTracker)
{
}

RhythmAnalyzer::RhythmAnalyzer (AnalysisAudioRing& ring,
                                IRhythmTracker* tracker,
                                TrackerDeleter deleter)
    : ring_ (ring),
      tracker_ (requirePluginDeleter (tracker, deleter), deleter)
{
}

RhythmAnalyzer::~RhythmAnalyzer()
{
    stop();
}

bool RhythmAnalyzer::running() const noexcept
{
    return running_.load (std::memory_order_acquire);
}

AnalyzerStartResult RhythmAnalyzer::start (double sampleRate)
{
    if (tracker_ == nullptr)
        return AnalyzerStartResult::noTracker;

    if (! (std::isfinite (sampleRate) && sampleRate > 0.0))
        return AnalyzerStartResult::invalidSampleRate;

    bool restart = false;
    {
        std::lock_guard<std::mutex> lock (lifecycleMutex_);
        // A thread that has exited but not been joined is still a live session:
        // the owner must stop() before it may start again. `running_` alone is
        // not enough because the worker clears it on exit.
        if (thread_.joinable() || running_.load (std::memory_order_acquire))
            return AnalyzerStartResult::alreadyRunning;

        stopRequested_ = false;
        restart = hasStartedOnce_;
        hasStartedOnce_ = true;
    }

    // The producer (audio callback) and the evidence consumer must be quiescent
    // here; start() is a non-RT lifecycle call. Flush stale EVIDENCE, and on a
    // restart also DISCARD stale AUDIO so a previous stream cannot be processed
    // under the new generation.
    flushObservations();
    discardedAudioBlocks_.store (0, std::memory_order_relaxed);
    discardedAudioFrames_.store (0, std::memory_order_relaxed);
    discardedAudioDiscardEvents_.store (0, std::memory_order_relaxed);
    if (restart)
        discardQueuedAudio();

    // A restart is a new, reproducible session: counters reset, generation bumps
    // so any envelope a consumer kept from before is unambiguously stale.
    processedFrames_.store (0, std::memory_order_relaxed);
    processedSamples_.store (0, std::memory_order_relaxed);
    discontinuities_.store (0, std::memory_order_relaxed);
    invalidRateFrames_.store (0, std::memory_order_relaxed);
    enqueuedObservations_.store (0, std::memory_order_relaxed);
    sequence_.store (0, std::memory_order_relaxed);
    failed_.store (false, std::memory_order_relaxed);
    // failureMessage_ is deliberately NOT cleared: failureMessage() returns a
    // pointer that stays stable until the next failure, and it is only read by
    // the lifecycle owner after join (or by a quiescent diagnostic reader).

    // Session-local drop baseline: the CommandQueue drop counter is cumulative
    // over the object's lifetime, so stats() reports the per-session delta.
    dropBaseAtStart_.store (output_.droppedCount(), std::memory_order_relaxed);

    streamRate_ = sampleRate;
    expectedNextSampleTime_ = 0;
    haveAnchor_ = false;
    forceReset_ = false;

    generation_.fetch_add (1, std::memory_order_relaxed);

    // Prime the backend at the nominal rate on the caller's (non-RT) thread. The
    // first frame re-primes it if the device declares a different rate, so the
    // backend always sees the adapter's true device rate (SPEC.md 7.2); this
    // worker performs no resampling of its own. After this point the backend is
    // exclusively owned by the worker thread until stop() joins.
    try
    {
        tracker_->reset (sampleRate);
    }
    catch (const std::exception& e)
    {
        setFailure (e.what());
        return AnalyzerStartResult::trackerResetFailed;
    }
    catch (...)
    {
        setFailure ("tracker reset() threw a non-std exception");
        return AnalyzerStartResult::trackerResetFailed;
    }

    // running_ is published BEFORE the thread starts, so a worker that fails
    // immediately and clears it to false cannot be overwritten by a later store
    // here. The worker's exit store is therefore definitive.
    running_.store (true, std::memory_order_release);
    try
    {
        thread_ = std::thread (&RhythmAnalyzer::workerLoop, this);
    }
    catch (const std::exception& e)
    {
        running_.store (false, std::memory_order_release);
        setFailure (e.what());
        return AnalyzerStartResult::threadStartFailed;
    }
    catch (...)
    {
        running_.store (false, std::memory_order_release);
        setFailure ("std::thread construction threw a non-std exception");
        return AnalyzerStartResult::threadStartFailed;
    }

    return AnalyzerStartResult::started;
}

void RhythmAnalyzer::stop() noexcept
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

bool RhythmAnalyzer::popObservation (ObservationEnvelope& out) noexcept
{
    return output_.pop (out);
}

void RhythmAnalyzer::flushObservations() noexcept
{
    ObservationEnvelope discarded;
    while (output_.pop (discarded))
    {
    }
}

void RhythmAnalyzer::discardQueuedAudio() noexcept
{
    AnalysisFrame discarded;
    uint64_t blocks = 0;
    uint64_t frames = 0;
    while (ring_.pop (discarded))
    {
        ++blocks;
        frames += discarded.numSamples;
    }

    if (blocks > 0)
    {
        discardedAudioBlocks_.fetch_add (blocks, std::memory_order_relaxed);
        discardedAudioFrames_.fetch_add (frames, std::memory_order_relaxed);
        discardedAudioDiscardEvents_.fetch_add (1, std::memory_order_relaxed);
    }
}

RhythmAnalyzerStats RhythmAnalyzer::stats() const noexcept
{
    RhythmAnalyzerStats s;
    s.running = running_.load (std::memory_order_acquire);
    s.failed = failed_.load (std::memory_order_acquire);
    s.streamGeneration = generation_.load (std::memory_order_relaxed);
    s.processedFrames = processedFrames_.load (std::memory_order_relaxed);
    s.processedSamples = processedSamples_.load (std::memory_order_relaxed);
    s.discontinuities = discontinuities_.load (std::memory_order_relaxed);
    s.invalidRateFrames = invalidRateFrames_.load (std::memory_order_relaxed);
    s.enqueuedObservations = enqueuedObservations_.load (std::memory_order_relaxed);
    s.lifetimeDroppedObservations = output_.droppedCount();
    s.droppedObservations =
        s.lifetimeDroppedObservations - dropBaseAtStart_.load (std::memory_order_relaxed);
    s.discardedAudioBlocks = discardedAudioBlocks_.load (std::memory_order_relaxed);
    s.discardedAudioFrames = discardedAudioFrames_.load (std::memory_order_relaxed);
    s.discardedAudioDiscardEvents =
        discardedAudioDiscardEvents_.load (std::memory_order_relaxed);
    s.ringOverruns = ring_.overrunCount();
    return s;
}

bool RhythmAnalyzer::failed() const noexcept
{
    return failed_.load (std::memory_order_acquire);
}

const char* RhythmAnalyzer::failureMessage() const noexcept
{
    if (! failed_.load (std::memory_order_acquire))
        return "";
    return failureMessage_;
}

void RhythmAnalyzer::workerLoop() noexcept
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock (lifecycleMutex_);
            if (stopRequested_)
                break;
        }

        const bool drainedSome = drainRing();

        if (failed_.load (std::memory_order_acquire))
            break;

        std::unique_lock<std::mutex> lock (lifecycleMutex_);
        if (stopRequested_)
            break;

        if (! drainedSome)
            lifecycleCv_.wait_for (lock, kIdlePollInterval,
                                   [this] { return stopRequested_; });

        if (stopRequested_)
            break;
    }

    running_.store (false, std::memory_order_release);
}

bool RhythmAnalyzer::drainRing() noexcept
{
    bool any = false;

    for (std::size_t i = 0; i < kMaxDrainPerWake; ++i)
    {
        if (! ring_.pop (scratchFrame_))
            break;

        any = true;
        processFrame (scratchFrame_);

        if (failed_.load (std::memory_order_acquire))
            break;
    }

    return any;
}

void RhythmAnalyzer::processFrame (const AnalysisFrame& frame) noexcept
{
    const double rate = frame.sourceSampleRate;

    // A non-finite or non-positive rate cannot be handed to a backend. Drop the
    // frame, count it, and force the next valid frame through the reset path so
    // the backend never carries an unknown-rate state forward.
    if (! (std::isfinite (rate) && rate > 0.0))
    {
        invalidRateFrames_.fetch_add (1, std::memory_order_relaxed);
        forceReset_ = true;
        return;
    }

    const bool firstFrame = ! haveAnchor_;
    const bool rateChanged = (rate != streamRate_);
    const bool timeGap = haveAnchor_ && (frame.sampleTime != expectedNextSampleTime_);

    if (forceReset_ || (! firstFrame && (rateChanged || timeGap)))
    {
        // A real discontinuity after the stream started: re-establish the
        // backend at the new rate and bump the generation. Any envelope already
        // queued keeps its old generation, so stale evidence is distinguishable.
        resetTracker (rate);
        discontinuities_.fetch_add (1, std::memory_order_relaxed);
    }
    else if (firstFrame && rateChanged)
    {
        // start() primed the backend at its nominal rate, but the first frame
        // declares a different device rate. Re-prime once at the true rate.
        resetTracker (rate);
        discontinuities_.fetch_add (1, std::memory_order_relaxed);
    }

    if (failed_.load (std::memory_order_acquire))
        return;

    forceReset_ = false;
    haveAnchor_ = true;
    streamRate_ = rate;
    // Exact uint64_t modular arithmetic: a contiguous stream that wraps the
    // device counter still compares equal here, so wrap is not a false gap.
    expectedNextSampleTime_ = frame.sampleTime + frame.numSamples;

    RhythmObservation observation {};
    try
    {
        observation = tracker_->process (frame);
    }
    catch (const std::exception& e)
    {
        setFailure (e.what());
        requestStopFromWorker();
        return;
    }
    catch (...)
    {
        setFailure ("tracker process() threw a non-std exception");
        requestStopFromWorker();
        return;
    }

    ObservationEnvelope envelope;
    std::memcpy (&envelope.observation, &observation, sizeof (observation));
    envelope.streamGeneration = generation_.load (std::memory_order_relaxed);
    envelope.blockStartSampleTime = frame.sampleTime;
    // Lower-bound audio-data horizon: the end of this input block. NOT measured
    // live availability (availabilityMeasured stays false).
    envelope.inputHorizonSampleTime = frame.sampleTime + frame.numSamples;
    envelope.availabilityMeasured = false;
    envelope.sourceSampleRate = rate;
    envelope.sequence = sequence_.fetch_add (1, std::memory_order_relaxed);

    if (output_.push (envelope))
        enqueuedObservations_.fetch_add (1, std::memory_order_relaxed);
    // else: bounded queue is full. CommandQueue dropped the incoming envelope
    // and counted it; we deliberately do not overwrite queued evidence.

    processedFrames_.fetch_add (1, std::memory_order_relaxed);
    processedSamples_.fetch_add (frame.numSamples, std::memory_order_relaxed);
}

void RhythmAnalyzer::resetTracker (double rate) noexcept
{
    generation_.fetch_add (1, std::memory_order_relaxed);
    streamRate_ = rate;
    haveAnchor_ = false;
    expectedNextSampleTime_ = 0;

    try
    {
        tracker_->reset (rate);
    }
    catch (const std::exception& e)
    {
        setFailure (e.what());
    }
    catch (...)
    {
        setFailure ("tracker reset() threw a non-std exception");
    }
}

void RhythmAnalyzer::requestStopFromWorker() noexcept
{
    {
        std::lock_guard<std::mutex> lock (lifecycleMutex_);
        stopRequested_ = true;
    }
    lifecycleCv_.notify_all();
}

void RhythmAnalyzer::setFailure (const char* message) noexcept
{
    if (message == nullptr)
        message = "unknown tracker failure";

    std::size_t i = 0;
    for (; i + 1 < sizeof (failureMessage_) && message[i] != '\0'; ++i)
        failureMessage_[i] = message[i];
    failureMessage_[i] = '\0';

    // Release publishes the message bytes; failureMessage() reads them only
    // after an acquire load of failed_.
    failed_.store (true, std::memory_order_release);
}

} // namespace jam
