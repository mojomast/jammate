// RhythmAnalyzer — the live rhythm-analysis worker (DEVPLAN ANALYSIS-001).
//
// This is the one thread that owns beat-tracker evidence between the audio
// callback and the Musical Clock. It drains a bounded SPSC audio queue
// (AnalysisAudioRing, SPEC.md 8.1), runs an injected IRhythmTracker backend, and
// republishes every observation onto a *second* bounded SPSC queue for the
// future clock/audio consumer (SPEC.md 7.2, 8.2/8.3).
//
// Thread model (SPEC.md 7):
//   - The audio callback ONLY calls AnalysisAudioRing::push. This class adds
//     nothing to that path: no notify, no lock, no message. The worker discovers
//     work by polling.
//   - The worker thread only touches the ring's consumer side, the tracker, this
//     object's atomics, and the output queue's producer side. It never calls a
//     UI or device API and never blocks on the audio thread.
//   - start()/stop() are lifecycle calls from ONE non-real-time owner thread.
//     stop() joins; it must be called before the ring is reset or destroyed, and
//     before this object is destroyed (the destructor joins as a backstop).
//
// Evidence semantics (SPEC.md 9.1):
//   - A tracker backend never changes drum tempo. The analyzer never transforms a
//     BPM into a drum decision; the pipeline stays
//         audio -> RhythmObservation -> MusicalClock -> ClockSnapshot.
//   - The observation travels through the output queue BYTE-FOR-BYTE unaltered.
//     The envelope adds only metadata: the causal availability (end of the input
//     block, the device time at which the evidence became knowable) and the
//     stream generation. `observation.inputSampleTime` is the *event* time; it
//     may legitimately be earlier than the availability. Keeping both makes
//     "when it happened" and "when we could know it" distinct, which is what a
//     clock consumer needs to avoid stepping backwards in time (EVAL-004).
//
// Discontinuity policy:
//   - The device sample clock is expected to advance contiguously. A frame gap,
//     an out-of-order frame, a change of sourceSampleRate, or a non-finite /
//     non-positive rate tears down the stream: the tracker is reset at the new
//     rate, the stream generation is bumped, and old-generation envelopes already
//     in the queue stay tagged with their own generation so a consumer can tell
//     them apart. `sampleTime + numSamples` is computed in exact uint64_t
//     modular arithmetic, so the wrap of the device clock is not a false gap.
//   - A frame with an invalid rate is dropped and flagged; the next valid frame
//     is forced through the reset path.
//
// Queue-full policy (SPEC.md 8.1/8.3, same direction as AnalysisAudioRing):
//   - The output queue is fixed capacity. When full, the analyzer DROPS the
//     incoming envelope and increments an explicit counter. It never overwrites
//     queued evidence, never retries and never pretends the lost beat is still
//     present. `stats().droppedObservations` makes the loss visible.
//
// Bounds:
//   - AnalysisAudioRing clips producers to jam::kMaxAnalysisBlock (2048) frames.
//     A device block larger than that is silently clipped at the ring, which is
//     the ring's documented decision; this worker operates on whatever frame it
//     receives and does not re-chunk. Callers that can exceed 2048 frames per
//     callback must bound their analysis tap into chunks <= kMaxAnalysisBlock
//     before the future processor integration (INT-ANALYSIS-001).
//
// IRhythmTracker::process is contractually non-blocking: it must return the
// observation for the block it was given, never wait for future audio and never
// block indefinitely. stop() cannot force an uncooperative backend to return;
// it only signals and joins.

#pragma once

#include "AnalysisAudioRing.h"
#include "IRhythmTracker.h"
#include "RhythmTypes.h"
#include "rt/RtSignal.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace jam
{

/** Fixed capacity of the analyzer's evidence queue. Bounded and small: the
    consumer (Musical Clock thread) is expected to drain faster than evidence is
    produced, so the queue is a safety buffer for beat events, not a reservoir.
    Reuses jam::rt::CommandQueue, whose full policy is drop-incoming-and-count. */
inline constexpr std::size_t kObservationQueueCapacity = 32;

/** One queued unit of evidence: the backend's observation, unaltered, plus the
    metadata a consumer needs to place it on the device timeline. */
struct ObservationEnvelope
{
    RhythmObservation observation;          // backend output, unmodified

    uint64_t streamGeneration = 0;          // bumped on every tracker reset
    uint64_t blockStartSampleTime = 0;      // device time of the block's first sample
    uint64_t availabilitySampleTime = 0;    // device time at the block's END: when this
                                            // evidence became causally knowable
    double   sourceSampleRate = 48000.0;    // device rate the block was captured at
    uint64_t sequence = 0;                  // monotone enqueue order

    /** True when the backend reported an event time that is not in the future of
        the causal availability. A false value means the event claim cannot be
        acted on at its stated time (it is non-causal); the observation itself is
        left untouched so the consumer decides what to do. */
    bool observationCausal() const noexcept
    {
        return observation.inputSampleTime <= availabilitySampleTime;
    }
};

/** Outcome of RhythmAnalyzer::start(). Rejection is explicit data, not a silent
    no-op, so the lifecycle owner can distinguish duplicate starts from bad
    configuration. */
enum class AnalyzerStartResult
{
    started = 0,
    alreadyRunning,
    noTracker,
    invalidSampleRate,
    trackerResetFailed,
    threadStartFailed
};

inline const char* toString (AnalyzerStartResult result) noexcept
{
    switch (result)
    {
        case AnalyzerStartResult::started:            return "started";
        case AnalyzerStartResult::alreadyRunning:     return "alreadyRunning";
        case AnalyzerStartResult::noTracker:          return "noTracker";
        case AnalyzerStartResult::invalidSampleRate:  return "invalidSampleRate";
        case AnalyzerStartResult::trackerResetFailed: return "trackerResetFailed";
        case AnalyzerStartResult::threadStartFailed:  return "threadStartFailed";
    }
    return "unknown";
}

/** Relaxed, multi-field diagnostic sample. Individual counters are each exact;
    the set is not a single atomic snapshot, and is documented as such. */
struct RhythmAnalyzerStats
{
    bool running = false;
    bool failed = false;

    uint64_t streamGeneration = 0;
    uint64_t processedFrames = 0;       // ring frames fed to the backend
    uint64_t processedSamples = 0;      // summed numSamples of those frames
    uint64_t discontinuities = 0;       // tracker resets after the priming reset
    uint64_t invalidRateFrames = 0;     // frames dropped for a non-finite/bad rate
    uint64_t enqueuedObservations = 0;  // envelopes accepted by the output queue
    uint64_t droppedObservations = 0;   // envelopes dropped because the queue was full
    uint64_t ringOverruns = 0;          // AnalysisAudioRing producer drops (read-only)
};

class RhythmAnalyzer
{
public:
    /** Deletes a tracker created with ordinary `new` (tests, in-process
        backends). Plugin backends use the three-argument constructor instead. */
    using TrackerDeleter = void (*) (IRhythmTracker*);
    using TrackerPtr = std::unique_ptr<IRhythmTracker, TrackerDeleter>;

    /** @param ring   bounded audio queue; MUST outlive this analyzer and MUST NOT
                      be reset or destroyed before stop() has joined.
        @param tracker  backend, constructed off the audio thread. May be null
                      only to force a clean `noTracker` rejection at start(). */
    RhythmAnalyzer (AnalysisAudioRing& ring,
                    std::unique_ptr<IRhythmTracker> tracker) noexcept;

    /** Ownership of a dlopen()ed plugin backend, torn down through its C
        interface (`jam_rhythm_destroy`) rather than `delete`, so the backend's
        own allocator/destructor in the shared object is used. The shared object
        must stay loaded until this analyzer is destroyed. */
    RhythmAnalyzer (AnalysisAudioRing& ring,
                    IRhythmTracker* tracker,
                    TrackerDeleter deleter) noexcept;

    /** Joins the worker if one is running. Non-RT. Safe when already stopped. */
    ~RhythmAnalyzer();

    RhythmAnalyzer (const RhythmAnalyzer&) = delete;
    RhythmAnalyzer& operator= (const RhythmAnalyzer&) = delete;

    /** Start the worker at a nominal device rate. The tracker is reset at this
        rate immediately (on the caller's thread); the first frame re-establishes
        it if that frame declares a different rate. Rejects a duplicate start, a
        null tracker and a non-finite / non-positive rate without starting.

        Requires the consumer to be quiescent: start() flushes any envelopes left
        by a previous session so the new session is reproducible. */
    AnalyzerStartResult start (double sampleRate);

    /** Signal and join. Non-RT, cannot be called from the worker. Idempotent.
        Must be called before the ring is reset/destroyed or this object dies. */
    void stop() noexcept;

    bool running() const noexcept;

    /** Consumer side of the evidence queue. Bounded, non-blocking. Returns false
        when empty; `out` is then untouched. */
    bool popObservation (ObservationEnvelope& out) noexcept;

    /** Drain every queued envelope. Lifecycle-owner / consumer-quiescent path
        only, never the audio thread. Exposed so a restart contract can be stated
        and tested. */
    void flushObservations() noexcept;

    static constexpr std::size_t observationQueueCapacity() noexcept
    {
        return kObservationQueueCapacity;
    }

    RhythmAnalyzerStats stats() const noexcept;

    /** Worker failure flag. Set once when a backend throws from reset()/process().
        The worker then stops; the owner must stop()/join and decide what to do. */
    bool failed() const noexcept;
    const char* failureMessage() const noexcept;

private:
    void workerLoop() noexcept;
    bool drainRing() noexcept;
    void processFrame (const AnalysisFrame& frame) noexcept;
    void resetTracker (double rate) noexcept;
    void requestStopFromWorker() noexcept;
    void setFailure (const char* message) noexcept;

    AnalysisAudioRing& ring_;
    TrackerPtr tracker_;
    rt::CommandQueue<ObservationEnvelope, kObservationQueueCapacity> output_;

    // Lifecycle. The mutex is only ever taken by non-RT lifecycle/worker code.
    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    bool stopRequested_ = false;
    std::thread thread_;

    std::atomic<bool> running_ { false };

    // Worker-thread-owned stream continuity state (no atomics needed: only the
    // worker reads/writes it after start(); start() touches it only while the
    // previous worker is joined).
    double   streamRate_ = 48000.0;
    uint64_t expectedNextSampleTime_ = 0;
    bool     haveAnchor_ = false;
    bool     forceReset_ = false;

    // Worker-owned scratch frame. Kept as a member so drainRing() does not
    // zero-initialise an 8 KiB local on every poll; only the worker touches it.
    AnalysisFrame scratchFrame_ {};

    // Published counters.
    std::atomic<uint64_t> generation_ { 0 };
    std::atomic<uint64_t> processedFrames_ { 0 };
    std::atomic<uint64_t> processedSamples_ { 0 };
    std::atomic<uint64_t> discontinuities_ { 0 };
    std::atomic<uint64_t> invalidRateFrames_ { 0 };
    std::atomic<uint64_t> enqueuedObservations_ { 0 };
    std::atomic<uint64_t> sequence_ { 0 };

    std::atomic<bool> failed_ { false };
    char failureMessage_[256] = {};
};

} // namespace jam
