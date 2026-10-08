// LiveJamSession — the JUCE-free control core of the first audible live slice.
//
// INT-LIVE-001. This object wires the pipeline the contract names:
//
//   audio callback tap -> bounded analysis ring -> RhythmAnalyzer (one worker)
//     -> observations -> MusicalClock (control worker)
//     -> JamJoinPolicy -> DrumClockBridge -> bounded command queue -> DrumEngine
//
// It owns the ring, the analyzer, the clock, the bridge and the policy, plus the
// single control-worker thread. It is JUCE-free on purpose: the wiring and its
// tests must build and run with no audio device. The actual DrumEngine is not
// referenced here in any way — the bridge is handed to it by PluginProcessor as
// a bounded command queue pointer (DrumClockBridge owns the queue; the engine is
// the single audio-thread consumer).
//
// THREAD MODEL (SPEC.md 7 / LIVE-JAM-CONTRACT.md)
//   - The audio callback ONLY calls pushAudio()/publishDrumEcho(). Both are
//     bounded, allocation-free and never block: pushAudio copies into the
//     pre-allocated AnalysisAudioRing, which drops+counts the incoming block on
//     overflow. There is no notify, no lock and no message on that path.
//   - One control worker drains UI commands and observations, advances the
//     MusicalClock from the actual audio cursor, runs the join policy and
//     publishes one coherent JamLiveState via a lock-free latest-value slot.
//   - prepare()/release() are ONE non-RT lifecycle owner thread. prepare() stops
//     and joins the worker and the analyzer BEFORE it resets the ring or the
//     bridge, and never joins from a UI call. The worker is never started before
//     the audio side is initialised (prepare is the audio-init boundary).
//
// TIME
//   `publishAudioCursor()`/`pushAudio()` advance one session-relative absolute
//   uint64 audio sample counter for every callback, including while Jam is
//   stopped. It is not a DAW playhead. prepare() establishes origin 0 and the
//   device rate; the audio side and the control worker share that domain, and
//   the DrumEngine is attached at the same origin so the worker clock and the
//   rendered grid cannot drift apart.
//
// GENERATIONS
//   Every prepare() bumps a session generation. UI commands are tagged with the
//   generation observed at submission; the worker rejects a command whose tag no
//   longer matches, so a command that was in flight while the session was
//   re-prepared can never be applied to the new session. The published
//   latest-value slot is never reset or destroyed while UI readers exist — the
//   session object persists across device re-prepares.

#pragma once

#include "AnalysisAudioRing.h"
#include "DrumClockBridge.h"
#include "IRhythmTracker.h"
#include "JamJoinPolicy.h"
#include "JamLiveInterface.h"
#include "MusicalClock.h"
#include "RhythmAnalyzer.h"
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

/** Compile-time UI command capacity. Structural, never a tunable: the queue is
    a fixed member array, so nothing allocates after construction. */
inline constexpr std::size_t kJamLiveCommandCapacity = 16;

/** Compile-time capacity of the deterministic observation-injection seam used
    by tests. It never carries production data. */
inline constexpr std::size_t kJamLiveInjectedObservationCapacity = 32;

/** Default analysis ring capacity (blocks). Small on purpose: evidence is
    disposable and analysis must never make audio wait. */
inline constexpr std::size_t kJamLiveDefaultRingCapacity = 8;

/** A compact echo of the audio owner's drum transport, published by the audio
    callback and folded into JamLiveState by the control worker. UI never reads
    plain DrumEngine getters. */
struct DrumPlaybackEcho
{
    bool attached = false;          // a bridge queue is attached
    bool injectedActive = false;    // a join has engaged the injected transport
    bool injectedPlaying = false;   // the injected transport is really rendering
    std::uint64_t samplePosition = 0;
    std::uint64_t stepsFired = 0;
};

struct LiveJamSessionConfig
{
    std::size_t audioRingCapacity = kJamLiveDefaultRingCapacity;

    /** The one prepared 4/4 open groove. Library index 0 is ROCK/Basic in the
        shipped library; the DrumEngine refuses a non-4/4 groove. */
    LibraryIndex groove = 0;

    /** Backend reported once a tracker is present. A missing tracker is always
        reported as `unavailable`, never simulated. */
    JamLiveBackend availableBackend = JamLiveBackend::experimentalBTrack;

    ClockConfig clock {};
    DrumClockBridgeConfig bridge {};
    JamJoinPolicyConfig policy {};

    /** An observation older than this (in samples) is fail-closed: it is
        dropped and counted rather than applied late to a moved-on clock. */
    std::uint64_t maxObservationAgeSamples = 2ull * 48000ull;
};

class LiveJamSession
{
public:
    explicit LiveJamSession (const LiveJamSessionConfig& config = {});
    ~LiveJamSession();

    LiveJamSession (const LiveJamSession&) = delete;
    LiveJamSession& operator= (const LiveJamSession&) = delete;

    // --- lifecycle (one quiescent owner thread) ------------------------------

    /** Hand over the single tracker the analyzer will own. Valid only before the
        first prepare(); later calls are rejected so ownership stays unambiguous.
        A null tracker is accepted and yields an `unavailable` backend whose
        Start is rejected (never simulated). */
    bool setTracker (std::unique_ptr<IRhythmTracker> tracker) noexcept;

    bool hasTracker() const noexcept { return haveTracker_; }

    /** Prepare at a device rate. Stops/joins the worker and analyzer, resets the
        ring, bridge, clock and policy, bumps the session generation, restarts
        the analyzer, then (optionally) starts the control worker. Never called
        concurrently with the audio callback. */
    bool prepare (double sampleRate, int maximumBlockSize, bool startControlThread = true);

    /** Stop and join the worker and analyzer. Idempotent. Storage is kept, so a
        later prepare() reuses the same objects and the latest-value slot is never
        destroyed under a reader. */
    void release() noexcept;

    bool prepared() const noexcept { return prepared_.load (std::memory_order_acquire); }
    bool running() const noexcept { return running_.load (std::memory_order_acquire); }

    // --- audio side ----------------------------------------------------------

    /** Copy one mono guitar block into the bounded analysis ring, advancing the
        session audio cursor to the end of the block. Bounded, allocation-free,
        never blocks. Callers with a callback larger than jam::kMaxAnalysisBlock
        must split it into <=2048-sample chunks and pass each chunk with its own
        absolute sampleTime; this method does not silently truncate. */
    void pushAudio (const float* mono, std::uint32_t numSamples,
                    std::uint64_t sampleTime, double sampleRate) noexcept;

    /** Publish the absolute audio cursor (end of the processed callback). */
    void publishAudioCursor (std::uint64_t cursor) noexcept;

    /** Audio owner: publish the drum transport echo for the worker to fold into
        JamLiveState. Called every callback after the engine has run. */
    void publishDrumEcho (const DrumPlaybackEcho& echo) noexcept;

    // --- UI side -------------------------------------------------------------

    /** One message-thread producer. Bounded enqueue; false means full/rejected.
        The command is tagged with the current session generation so a re-prepare
        can reject it. No lifecycle work, no joins. */
    bool submitCommand (const JamLiveCommand& command) noexcept;

    /** One message-thread reader. Coherent latest-value attempt; false leaves
        `out` unchanged (the UI keeps its previous whole state). */
    bool readState (JamLiveState& out) const noexcept;

    // --- integration surface -------------------------------------------------

    DrumClockCommandQueue& drumCommandQueue() noexcept { return bridge_.commandQueue(); }
    const DrumClockCommandQueue& drumCommandQueue() const noexcept
    {
        return bridge_.commandQueue();
    }

    std::uint64_t audioCursor() const noexcept
    {
        return audioCursor_.load (std::memory_order_acquire);
    }

    JamLiveBackend backend() const noexcept { return backend_; }
    JamLiveFailure failure() const noexcept { return failure_; }

    // --- deterministic test seam --------------------------------------------

    /** Push an observation envelope exactly as the analyzer would have produced
        it. Drains before the analyzer queue, so a manually-stepped session is
        fully deterministic and needs no analyser thread. Returns false when the
        bounded injection queue is full. */
    bool injectObservationForTesting (const ObservationEnvelope& envelope) noexcept;

    /** Run exactly one control iteration on the calling thread. Intended for
        tests with the control worker stopped. */
    void stepControlForTesting() noexcept;

private:
    void workerLoop() noexcept;
    void stepControl (std::uint64_t cursor) noexcept;
    void applyCommand (const JamLiveCommand& command, std::uint64_t cursor) noexcept;
    bool popObservation (ObservationEnvelope& out) noexcept;
    void advanceClockTo (std::uint64_t cursor, double rate) noexcept;
    void processObservation (const ObservationEnvelope& envelope,
                             std::uint64_t cursor) noexcept;
    void syncBridgePhaseToClock (std::uint64_t cursor) noexcept;
    void publishState (std::uint64_t cursor) noexcept;
    void resetSessionStats() noexcept;
    void startWorker() noexcept;
    void stopWorker() noexcept;

    struct QueuedCommand
    {
        JamLiveCommand command;
        std::uint64_t generation = 0;
    };

    LiveJamSessionConfig config_;
    AnalysisAudioRing ring_;
    std::unique_ptr<IRhythmTracker> pendingTracker_;
    std::unique_ptr<RhythmAnalyzer> analyzer_;
    MusicalClock clock_;
    DrumClockBridge bridge_;
    JamJoinPolicy policy_;

    rt::CommandQueue<QueuedCommand, kJamLiveCommandCapacity> commands_;
    rt::CommandQueue<ObservationEnvelope, kJamLiveInjectedObservationCapacity>
        injectedObservations_;

    rt::LatestValue<JamLiveState> state_;
    rt::LatestValue<DrumPlaybackEcho> echo_;

    std::atomic<std::uint64_t> audioCursor_ { 0 };
    std::atomic<std::uint64_t> generation_ { 0 };
    std::atomic<bool> prepared_ { false };
    std::atomic<float> inputPeak_ { 0.0f };

    // Worker lifecycle. The mutex is only taken by non-RT lifecycle/worker code.
    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    bool stopRequested_ = false;
    std::thread thread_;
    std::atomic<bool> running_ { false };

    bool haveTracker_ = false;
    JamLiveBackend backend_ = JamLiveBackend::unavailable;
    JamLiveFailure failure_ = JamLiveFailure::none;

    // Worker-owned state (no atomics needed: only the worker touches it after
    // start(); prepare() touches it only while the worker is joined).
    std::uint64_t sessionGeneration_ = 0;
    double sampleRate_ = 48000.0;
    std::uint64_t clockSampleTime_ = 0;
    bool haveClockAnchor_ = false;
    bool requestedRunning_ = false;
    DrumPlaybackEcho lastEcho_ {};
    bool discontinuitySeen_ = false;
    std::uint64_t lastAnalyzerGeneration_ = 0;
    bool haveAnalyzerGeneration_ = false;
    std::uint64_t baseRingOverruns_ = 0;
    std::uint64_t baseCommandDrops_ = 0;
    std::uint64_t staleCommandRejects_ = 0;
    std::uint64_t agedObservationDrops_ = 0;
    std::uint64_t lastEventSampleTime_ = 0;
    std::uint64_t lastInputHorizon_ = 0;
    std::uint64_t lastReceipt_ = 0;
    bool receiptMeasured_ = false;
    float candidateBpm_ = 0.0f;
    float heldInputPeak_ = 0.0f;
    std::uint64_t discontinuities_ = 0;

    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "LiveJamSession requires lock-free uint64_t atomics");
};

} // namespace jam
