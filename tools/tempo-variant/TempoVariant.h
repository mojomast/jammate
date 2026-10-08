// TempoVariant — diagnostic-only BPM-report variant wrapper (TRACK-005).
//
// WHAT IT IS
// ----------
// An IRhythmTracker decorator that composes an existing backend (in practice
// the real jam::BTrackBackend adapter) and changes EXACTLY ONE field of the
// evidence it forwards: `bpmCandidate`. Every other field — `beatEvent`,
// `inputSampleTime` (the emitted beat timestamp), `onsetStrength01`,
// `energyRmsDbfs`, `beatConfidence01`, `silence`, `phaseValid`, `beatPhase01`,
// `transientDensity01` — is copied through byte-for-byte from the wrapped
// backend's observation. No truth, no future audio and no fixture metadata are
// consulted: the replacement is a function only of the beat intervals the
// wrapped backend has ALREADY emitted.
//
// THE PREDECLARED METHOD (frozen before the corpus is scored)
// -----------------------------------------------------------
//   candidate = 60 / median( last 4 positive finite consecutive emitted
//                            beat-event intervals )
//   readiness = true iff at least 5 beat events have been emitted and the ring
//               currently holds 4 valid consecutive intervals.
//   before readiness -> the wrapped backend's own `bpmCandidate` is forwarded
//                       unchanged (explicit startup fallback).
//
// Interval validity / reset policy is fixed and predeclared:
//   - an interval is the difference of two CONSECUTIVE emitted beat samples,
//     in seconds at the rate passed to reset();
//   - non-finite, <= 0, non-monotonic (sample time does not advance) and
//     sub-minimum (< 60/maxBpm s) intervals are MALFORMED -> the ring is reset;
//   - an interval above the maximum (60/minBpm s) is a GAP (a missing beat,
//     a gate-suppressed beat or a silence interruption) -> the ring is reset;
//   - a non-causal beat event (reported after the end of the block that
//     produced it) is MALFORMED -> the ring is reset and it cannot anchor an
//     interval;
//   - between beats the last candidate is simply retained (it is STALE, not a
//     new measurement); silence does not add or modify intervals because the
//     wrapped backend suppresses beat events while silent.
//   The interval window is the wrapped adapter's OWN declared tempo range
//   (BTrackBackendConfig minBpm=40, maxBpm=240 -> [0.25, 1.50] s). It is NOT
//   fitted to any corpus result.
//
// There is NO octave/half/double correction, NO grid or truth snapping, and NO
// automatic parameter sweep. The window is fixed at 4 intervals (5 events).
//
// TRUTHFUL AVAILABILITY
// ---------------------
// `bpmCandidate` carries no readiness flag in jam::RhythmObservation, so the
// method's availability is exported SEPARATELY through MethodRecord (per block)
// and written to its own CSV by tools/tempo-variant. `variantBpm` equals the
// forwarded base value whenever `ready` is false, so a consumer can always tell
// a derived report from a fallback.
//
// BOUNDED / REAL-TIME
// -------------------
// The ring is a fixed std::array of 4 doubles; process() performs no heap
// allocation, no locking and no I/O (the optional log callback is invoked with
// an already-formed record). All operations are O(1) except a 4-element
// insertion sort.

#pragma once

#include "jam/IRhythmTracker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace tempo_variant
{

/** Number of consecutive valid intervals required to emit a derived report
    (5 emitted beat events). Frozen: see the brief's "median last 4". */
inline constexpr std::size_t kRequiredIntervals = 4;

/** Fixed ring capacity. Equal to kRequiredIntervals by construction. */
inline constexpr std::size_t kRingCapacity = kRequiredIntervals;

/** Wrapped adapter's declared tempo window (BTrackBackendConfig). The interval
    window is derived from it, not tuned to results. */
inline constexpr double kMinBpm = 40.0;
inline constexpr double kMaxBpm = 240.0;
inline constexpr double kMinIntervalSeconds = 60.0 / kMaxBpm;   // 0.25 s
inline constexpr double kMaxIntervalSeconds = 60.0 / kMinBpm;   // 1.50 s

/** What happened to the beat interval (if any) on a block. Exported so the
    method's readiness and reset causes are auditable, not inferred. */
enum class IntervalState : int
{
    NoBeat = 0,          // block emitted no beat event
    FirstBeat = 1,       // first beat after reset/ring reset: no interval yet
    Accepted = 2,        // valid consecutive interval accepted into the ring
    MalformedReset = 3,  // non-finite / <=0 / sub-minimum -> ring reset
    GapReset = 4,        // interval > max (missing/suppressed beat) -> ring reset
    OutOfOrderReset = 5  // non-monotonic sample time -> ring reset
};

const char* toString (IntervalState s) noexcept;

/** One auditable method step. Emitted once per processed block by the log
    callback. `ready` and `ringCount` are the truthful availability of the
    derived estimate; `baseBpm` and `variantBpm` are the forwarded and emitted
    values so a fallback is never mistaken for a measurement. */
struct MethodRecord
{
    std::uint64_t blockIndex = 0;
    double blockStartSeconds = 0.0;
    double blockEndSeconds = 0.0;      // causal availability of this block

    bool   beatEvent = false;
    double eventSeconds = 0.0;         // emitted beat timestamp (base, unchanged)
    double intervalSeconds = 0.0;      // consecutive interval (0 when none)
    IntervalState intervalState = IntervalState::NoBeat;

    std::size_t ringCount = 0;         // valid consecutive intervals held (0..4)
    bool   ready = false;              // ringCount == kRequiredIntervals
    double baseBpm = 0.0;              // wrapped backend's forwarded candidate
    double variantBpm = 0.0;           // value actually emitted this block
};

using MethodLogFn = std::function<void (const MethodRecord&)>;

/** Decorator. Takes ownership of `inner` through a type-erased deleter, so it
    can wrap any IRhythmTracker unique_ptr (default delete, or a dlopen
    plugin's own jam_rhythm_destroy() deleter). */
class TempoVariantTracker : public jam::IRhythmTracker
{
public:
    using InnerDeleter = std::function<void (jam::IRhythmTracker*)>;
    using InnerPtr = std::unique_ptr<jam::IRhythmTracker, InnerDeleter>;

    template <typename D>
    TempoVariantTracker (std::unique_ptr<jam::IRhythmTracker, D> inner,
                         MethodLogFn log = {})
        : log_ (std::move (log))
    {
        if (inner)
        {
            D deleter = std::move (inner.get_deleter());
            jam::IRhythmTracker* raw = inner.release();
            inner_ = InnerPtr (raw, [deleter] (jam::IRhythmTracker* p) mutable
                                      { if (p != nullptr) deleter (p); });
        }
    }

    void reset (double sampleRate) override;
    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override;
    const char* id() const noexcept override;

    /** Diagnostics for tests: the current ring size and readiness. */
    std::size_t ringCount() const noexcept { return ringCount_; }
    bool ready() const noexcept { return ringCount_ == kRequiredIntervals; }

private:
    void resetRing() noexcept;
    void pushInterval (double interval) noexcept;
    double medianInterval() const noexcept;

    InnerPtr         inner_;
    MethodLogFn      log_;
    std::array<double, kRingCapacity> ring_ {};
    std::size_t      ringCount_ = 0;
    std::uint64_t    prevBeatSample_ = 0;
    bool             havePrevBeat_ = false;
    std::uint64_t    blockIndex_ = 0;
    double           rate_ = 48000.0;
};

} // namespace tempo_variant
