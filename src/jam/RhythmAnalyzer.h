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
//     The envelope adds metadata: the input horizon (end of the input block) and
//     the stream generation. `observation.inputSampleTime` is the *event* time it
//     claims. The input horizon is a LOWER BOUND on live causality: with a
//     backlog, the worker's current frame can be behind the audio the device has
//     already produced, so the frame end is "the earliest this evidence could
//     possibly have been knowable", NOT the measured time it became knowable.
//     `availabilityMeasured` is false until a future processor/master-clock
//     integration stamps a real consumer receipt (INT-ANALYSIS-001).
//     `observationWithinInputHorizon()` tests the event against this lower bound
//     only; it is not a claim about live availability.
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

    /** Device time at the END of the input block: an audio-data HORIZON and a
        LOWER BOUND on when this evidence could have been knowable, not a
        measured live availability. See `availabilityMeasured`. */
    uint64_t inputHorizonSampleTime = 0;

    double   sourceSampleRate = 48000.0;    // device rate the block was captured at
    uint64_t sequence = 0;                  // monotone enqueue order

    /** True only when a real consumer-receipt device timestamp was stamped by a
        future integration. Always false for this worker: the horizon above is a
        bound, never a fabricated live measurement. */
    bool availabilityMeasured = false;

    /** Modular, wrap-safe test that the reported event lies within the input
        horizon (at or before it), using a bounded unsigned distance. Equal
        distances at half the counter range are ambiguous and rejected; a valid
        event just before a wrap with a horizon just after it is accepted.
        This is a bound test, not a live-availability claim. */
    bool observationWithinInputHorizon() const noexcept
    {
        static constexpr uint64_t kMaxEventToHorizonDistance = uint64_t { 1 } << 63;
        return (inputHorizonSampleTime - observation.inputSampleTime)
               < kMaxEventToHorizonDistance;
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
    the set is not a single atomic snapshot. Readers must be quiescent during
    start() so they cannot mix counters from two different sessions. */
struct RhythmAnalyzerStats
{
    bool running = false;
    bool failed = false;

    uint64_t streamGeneration = 0;
    uint64_t processedFrames = 0;       // ring frames fed to the backend this session
    uint64_t processedSamples = 0;      // summed numSamples of those frames
    uint64_t discontinuities = 0;       // tracker resets after the priming reset
    uint64_t invalidRateFrames = 0;     // frames dropped for a non-finite/bad rate
    uint64_t enqueuedObservations = 0;  // envelopes accepted by the output queue
    uint64_t droppedObservations = 0;   // PER-SESSION output-queue drops
    uint64_t lifetimeDroppedObservations = 0; // output-queue drops since construction
    uint64_t discardedAudioBlocks = 0;  // restart-only: queued audio blocks discarded
    uint64_t discardedAudioFrames = 0;  // restart-only: queued audio frames discarded
    uint64_t discardedAudioDiscardEvents = 0; // restart-only: number of restarts that discarded

    /** Cumulative producer drops reported by the PRE-EXISTING AnalysisAudioRing
        counter; it is not reset per session and can predate this analyzer. */
    uint64_t ringOverruns = 0;
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
        must stay loaded until this analyzer is destroyed.

        Contract: a non-null `tracker` MUST come with a non-null `deleter`. This
        never falls back to `delete` for a plugin object, because the wrong
        deallocator would cross the shared-object boundary. Violating the
        contract throws std::invalid_argument. A null `tracker` is accepted (it
        yields the `noTracker` rejection at start()). */
    RhythmAnalyzer (AnalysisAudioRing& ring,
                    IRhythmTracker* tracker,
                    TrackerDeleter deleter);

    /** Joins the worker if one is running. Non-RT. Safe when already stopped. */
    ~RhythmAnalyzer();

    RhythmAnalyzer (const RhythmAnalyzer&) = delete;
    RhythmAnalyzer& operator= (const RhythmAnalyzer&) = delete;

    /** Start the worker at a nominal device rate. The tracker is reset at this
        rate immediately (on the caller's thread); the first frame re-establishes
        it if that frame declares a different rate. Rejects a duplicate start, a
        null tracker and a non-finite / non-positive rate without starting.

        Lifecycle caller contract: start()/stop() are called by ONE non-RT owner
        thread, never concurrently with each other. The producer (audio callback)
        and the evidence consumer must be quiescent for the duration of start().

        First start preserves audio already queued in the ring (so tests and a
        caller that primes audio before starting are supported). Every LATER
        restart DISCARDS whatever audio is still queued from the previous stream
        and flushes the evidence queue, so a new session cannot process stale
        frames under a new generation. Discards are counted in stats(). */
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

    /** Failure text, valid only while failed() is true. The returned pointer is
        owned by this object and must only be read by the lifecycle owner after
        stop()/join (or by a diagnostic reader that accepts quiescence). It is
         stable until the next lifecycle call that produces a new failure; start()
         does not mutate it. This is a quiescent diagnostic API, not an audio
         callback API. */
    const char* failureMessage() const noexcept;

private:
    void workerLoop() noexcept;
    bool drainRing() noexcept;
    void processFrame (const AnalysisFrame& frame) noexcept;
    void resetTracker (double rate) noexcept;
    void requestStopFromWorker() noexcept;
    void setFailure (const char* message) noexcept;

    /** Restart-only: pops and counts every frame currently queued in the ring so
        a previous stream cannot leak into a new session. Bounded by ring
        capacity; producer must be quiescent (documented lifecycle contract). */
    void discardQueuedAudio() noexcept;

    AnalysisAudioRing& ring_;
    TrackerPtr tracker_;
    rt::CommandQueue<ObservationEnvelope, kObservationQueueCapacity> output_;

    // Lifecycle. The mutex is only ever taken by non-RT lifecycle/worker code.
    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    bool stopRequested_ = false;
    std::thread thread_;

    std::atomic<bool> running_ { false };

    // Lifecycle-owner-only session bookkeeping (start/stop caller).
    bool hasStartedOnce_ = false;
    std::atomic<uint64_t> dropBaseAtStart_ { 0 };

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
    std::atomic<uint64_t> discardedAudioBlocks_ { 0 };
    std::atomic<uint64_t> discardedAudioFrames_ { 0 };
    std::atomic<uint64_t> discardedAudioDiscardEvents_ { 0 };

    std::atomic<bool> failed_ { false };
    char failureMessage_[256] = {};

    static_assert (std::atomic<uint64_t>::is_always_lock_free,
                   "RhythmAnalyzer counters require lock-free uint64_t atomics");
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "RhythmAnalyzer flags require lock-free bool atomics");
};

} // namespace jam
