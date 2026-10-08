#include "RhythmAnalyzer.h"

#include <chrono>
#include <cmath>
#include <exception>

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

} // namespace

RhythmAnalyzer::RhythmAnalyzer (AnalysisAudioRing& ring,
                                std::unique_ptr<IRhythmTracker> tracker) noexcept
    : ring_ (ring),
      tracker_ (tracker.release(), &deleteTracker)
{
}

RhythmAnalyzer::RhythmAnalyzer (AnalysisAudioRing& ring,
                                IRhythmTracker* tracker,
                                TrackerDeleter deleter) noexcept
    : ring_ (ring),
      tracker_ (tracker, deleter)
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

    {
        std::lock_guard<std::mutex> lock (lifecycleMutex_);
        // A thread that has exited but not been joined is still a live session:
        // the owner must stop() before it may start again. `running_` alone is
        // not enough because the worker clears it on exit.
        if (thread_.joinable() || running_.load (std::memory_order_acquire))
            return AnalyzerStartResult::alreadyRunning;

        stopRequested_ = false;
    }

    // Flush stale evidence from any previous session. The contract requires the
    // consumer to be quiescent here; start() is a non-RT lifecycle call.
    flushObservations();

    // A restart is a new, reproducible session: counters reset, generation bumps
    // so any envelope a consumer kept from before is unambiguously stale.
    processedFrames_.store (0, std::memory_order_relaxed);
    processedSamples_.store (0, std::memory_order_relaxed);
    discontinuities_.store (0, std::memory_order_relaxed);
    invalidRateFrames_.store (0, std::memory_order_relaxed);
    enqueuedObservations_.store (0, std::memory_order_relaxed);
    sequence_.store (0, std::memory_order_relaxed);
    failed_.store (false, std::memory_order_relaxed);
    failureMessage_[0] = '\0';

    streamRate_ = sampleRate;
    expectedNextSampleTime_ = 0;
    haveAnchor_ = false;
    forceReset_ = false;

    generation_.fetch_add (1, std::memory_order_relaxed);

    // Prime the backend at the nominal rate on the caller's (non-RT) thread. The
    // first frame re-primes it if the device declares a different rate, so the
    // backend always sees the adapter's true device rate (SPEC.md 7.2); this
    // worker performs no resampling of its own.
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

    try
    {
        thread_ = std::thread (&RhythmAnalyzer::workerLoop, this);
    }
    catch (const std::exception& e)
    {
        setFailure (e.what());
        return AnalyzerStartResult::threadStartFailed;
    }
    catch (...)
    {
        setFailure ("std::thread construction threw a non-std exception");
        return AnalyzerStartResult::threadStartFailed;
    }

    running_.store (true, std::memory_order_release);
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
    s.droppedObservations = output_.droppedCount();
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
    envelope.observation = observation;
    envelope.streamGeneration = generation_.load (std::memory_order_relaxed);
    envelope.blockStartSampleTime = frame.sampleTime;
    envelope.availabilitySampleTime = frame.sampleTime + frame.numSamples;
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
