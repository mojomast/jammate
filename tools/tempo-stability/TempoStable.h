// TempoStable — diagnostic-only post-readiness tempo-stability decorator
// (TRACK-008).
//
// WHAT IT IS
// ----------
// A JUCE-free IRhythmTracker decorator that composes an existing backend (in
// practice the real jam::BTrackBackend adapter) and changes EXACTLY ONE field of
// the evidence it forwards: `bpmCandidate`. Every other field is copied through
// byte-for-byte from the wrapped backend's observation. No truth, no future
// audio, no fixture metadata and no scorer state are consulted: the replacement
// is a function only of the beat intervals the wrapped backend has ALREADY
// emitted, plus the wrapped backend's own forwarded value.
//
// WHY (TRACK-007 corrected evidence)
// ----------------------------------
// The old fixed variant (tools/tempo-variant) is not slow to become ready; it is
// unstable immediately AFTER readiness. At the first full-ring beat its
// `60/median(last 4 intervals)` can sit well outside the scorer's 2 % agreement
// band while the wrapped backend's own value was closer to the truth. This
// decorator gates the derived estimate behind a self-consistency confirmation:
// it forwards the backend value until the derived estimate has corroborated
// itself, then commits.
//
// THE PREDECLARED METHOD (frozen before score; see docs/research/tempo-stability/PROTOCOL.md)
// -----------------------------------------------------------------------------------------
//   interval ring: last 4 valid consecutive emitted beat intervals.
//   derived      = 60 / median4(ring)              (only when ring full)
//   confirmation = three consecutive full-ring updates whose derived value
//                  agrees with the previous derived value within 0.02 relative
//                  (the scorer's frozen kBpmAgreementFraction, reused).
//   before confirmation -> the wrapped backend's own bpmCandidate is forwarded
//                          unchanged (explicit startup/fallback).
//   after  confirmation -> the derived value is emitted until a ring reset.
//   between beats       -> the last emitted value is retained (stale).
//   any ring reset      -> ring, previous derived, stable count and confirmation
//                          are cleared.
//
// Interval validity / reset policy is fixed and predeclared (same window and
// malformed/gap/out-of-order/non-causal/frame rules as TRACK-005):
//   - an interval is the difference of two CONSECUTIVE emitted beat samples, in
//     seconds at the rate passed to reset();
//   - non-finite, <= 0, non-monotonic (sample time does not advance) and
//     sub-minimum (< 60/maxBpm s) intervals are MALFORMED -> ring reset;
//   - an interval above the maximum (60/minBpm s) is a GAP -> ring reset;
//   - a non-causal beat event (reported after the end of the block that produced
//     it) is MALFORMED -> ring reset and it cannot anchor an interval;
//   - invalid frame metadata -> frame-invalid reset.
//   The interval window is the wrapped adapter's OWN declared tempo range
//   (BTrackBackendConfig minBpm=40, maxBpm=240 -> [0.25, 1.50] s). It is NOT
//   fitted to any corpus result.
//
// There is NO octave/half/double correction, NO grid or truth snapping, and NO
// parameter sweep. The ring is fixed at 4 intervals; confirmation is fixed at 3
// agreeing updates within 2 %.
//
// TRUTHFUL AVAILABILITY
// ---------------------
// `bpmCandidate` carries no readiness flag in jam::RhythmObservation, so the
// method's availability is exported SEPARATELY through MethodRecord (per block)
// and written to its own CSV by tools/tempo-stability. `emittedBpm` equals the
// forwarded base value whenever `confirmed` is false, so a consumer can always
// tell a derived report from a fallback.
//
// BOUNDED / REAL-TIME SCOPE
// -------------------------
// The WRAPPER's own arithmetic is fixed and bounded: a 4-element std::array
// ring, no allocation, no locking, no I/O, O(1) except a 4-element insertion
// sort. This describes the decorator ONLY. The wrapped backend runs on the
// rhythm-analysis worker (never the audio callback) and may allocate; the
// optional log callback is invoked synchronously from process(), so a callback
// that performs I/O (the plugin's fstream logger) is a DIAGNOSTIC/offline path
// and must never be used on a real-time thread.
//
// RESET CONTRACT
// --------------
// reset(sampleRate) forwards `sampleRate` to the wrapped backend unchanged and
// uses a finite positive value for the wrapper's own interval arithmetic,
// falling back to 48000 for its OWN maths if the value is non-finite or <= 0.

#pragma once

#include "jam/IRhythmTracker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace tempo_stable
{

/** Number of consecutive valid intervals held (the estimator core). Frozen: the
    same median-of-4 core as the old fixed variant, so the measured difference is
    attributable to the confirmation gate. */
inline constexpr std::size_t kRingCapacity = 4;

/** Number of consecutive agreeing full-ring updates required to commit. Frozen. */
inline constexpr int kConfirmUpdates = 3;

/** Relative agreement required between successive derived values. This is the
    scorer's frozen BPM agreement fraction (kBpmAgreementFraction = 0.02),
    reused, not tuned. */
inline constexpr double kAgreementFraction = 0.02;

/** Wrapped adapter's declared tempo window (BTrackBackendConfig). The interval
    window is derived from it, not tuned to results. */
inline constexpr double kMinBpm = 40.0;
inline constexpr double kMaxBpm = 240.0;
inline constexpr double kMinIntervalSeconds = 60.0 / kMaxBpm;   // 0.25 s
inline constexpr double kMaxIntervalSeconds = 60.0 / kMinBpm;   // 1.50 s

/** What happened to the beat interval (if any) on a block. Exported so the
    method's confirmation and reset causes are auditable, not inferred. */
enum class IntervalState : int
{
    NoBeat = 0,          // block emitted no beat event
    FirstBeat = 1,       // first beat after reset/ring reset: no interval yet
    Accepted = 2,        // valid consecutive interval accepted into the ring
    MalformedReset = 3,  // non-finite / <=0 / sub-minimum -> ring reset
    GapReset = 4,        // interval > max (missing/suppressed beat) -> ring reset
    OutOfOrderReset = 5, // duplicate/non-monotonic sample time -> ring reset
    FrameInvalid = 6     // frame metadata invalid (overflow/oversize) -> ring reset
};

const char* toString (IntervalState s) noexcept;

/** One auditable method step. Emitted once per processed block by the log
    callback. `confirmed` is the truthful availability of the derived estimate;
    `baseBpm` and `emittedBpm` are the forwarded and emitted values so a fallback
    is never mistaken for a measurement. */
struct MethodRecord
{
    std::uint64_t blockIndex = 0;
    double blockStartSeconds = 0.0;
    double blockEndSeconds = 0.0;      // causal availability of this block

    bool   beatEvent = false;
    double eventSeconds = 0.0;         // emitted beat timestamp (base, unchanged)
    /** True only when a genuine positive consecutive interval was computed. A
        missing interval (first beat, duplicate/non-monotonic, non-causal,
        invalid frame) leaves this false and `intervalSeconds` at 0; the CSV
        writes an EMPTY cell, never a fabricated number. */
    bool   intervalMeasured = false;
    double intervalSeconds = 0.0;      // consecutive interval (0 when not measured)
    IntervalState intervalState = IntervalState::NoBeat;

    std::size_t ringCount = 0;         // valid consecutive intervals held (0..4)
    bool   ringFull = false;           // ringCount == kRingCapacity
    int    stableCount = 0;            // consecutive agreeing full-ring updates
    bool   confirmed = false;          // committed to the derived estimate

    double baseBpm = 0.0;              // wrapped backend's forwarded candidate
    double derivedBpm = 0.0;           // 60/median4 when the ring is full, else 0
    bool   derivedValid = false;       // derivedBpm is finite and positive
    double emittedBpm = 0.0;           // value actually emitted this block
};

using MethodLogFn = std::function<void (const MethodRecord&)>;

/** Decorator. Takes ownership of `inner` through a type-erased deleter, so it
    can wrap any IRhythmTracker unique_ptr (default delete, or a dlopen
    plugin's own jam_rhythm_destroy() deleter). */
class TempoStableTracker : public jam::IRhythmTracker
{
public:
    using InnerDeleter = std::function<void (jam::IRhythmTracker*)>;
    using InnerPtr = std::unique_ptr<jam::IRhythmTracker, InnerDeleter>;

    template <typename D>
    TempoStableTracker (std::unique_ptr<jam::IRhythmTracker, D> inner,
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

    /** Diagnostics for tests: the current ring size and confirmation state. */
    std::size_t ringCount() const noexcept { return ringCount_; }
    bool ringFull() const noexcept { return ringCount_ == kRingCapacity; }
    bool confirmed() const noexcept { return confirmed_; }
    int stableCount() const noexcept { return stableCount_; }

private:
    void resetState() noexcept;
    void pushInterval (double interval) noexcept;
    double medianInterval() const noexcept;
    void updateConfirmation (double derived) noexcept;

    InnerPtr         inner_;
    MethodLogFn      log_;
    std::array<double, kRingCapacity> ring_ {};
    std::size_t      ringCount_ = 0;
    std::uint64_t    prevBeatSample_ = 0;
    bool             havePrevBeat_ = false;

    // Confirmation state.
    bool             havePrevDerived_ = false;
    double           prevDerived_ = 0.0;
    int              stableCount_ = 0;
    bool             confirmed_ = false;

    std::uint64_t    blockIndex_ = 0;
    double           rate_ = 48000.0;
};

} // namespace tempo_stable
