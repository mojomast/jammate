// AcquisitionReplay — an independent re-implementation of the EVAL-002/004
// scorer's acquisition rule, run against a TRACK-004 trace (TRACK-004).
//
// WHY THIS EXISTS
// --------------
// The brief forbids touching tools/rhythm-eval/Metrics.cpp and forbids silently
// swapping the lock-run semantics, but it requires the causal trace to be
// replayed through "the exact scorer acquisition rules". `findFirstLockFrom()`
// and its helpers are in an anonymous namespace inside Metrics.cpp, so they
// cannot be called from another translation unit. This file therefore restates
// the rule *verbatim* from Metrics.cpp:112-167 and Metrics.h:41-53 and proves,
// per fixture, that its verdict agrees with `rhythmeval::scoreFixture` on the
// very same ObservationSeries. Agreement is the contract; disagreement is a
// hard error in the tests and is reported as a warning in the tool output.
//
// The two CLOCKS (event vs causal availability, EVAL-004) are preserved: a lock
// is confirmed by the scorer on the EVENT clock, while `confirmAvailability`
// reports when the confirming beat had actually been returned by process().

#pragma once

#include "Metrics.h"

#include <cstddef>
#include <string>
#include <vector>

namespace tracker_diag
{

/** Machine-readable classification of why acquisition did or did not happen.
    The label is chosen by a fixed priority from measured trace evidence; the
    supporting numbers are all reported beside it. */
enum class AcquireReason
{
    AcquiredWithin2Bars = 0,
    AcquiredAfter2Bars,
    InsufficientBeatEvents,
    LockTempoAgreementFailure,
    PhaseConflictOrDropouts,
    NoMatchingBeats,
    Other
};

const char* toString (AcquireReason r) noexcept;

/** Where and when an acquisition lock run begins and is confirmed. */
struct LockRun
{
    bool found = false;
    std::size_t startPredIndex = 0;      // first beat of the confirming run
    std::size_t confirmPredIndex = 0;    // last beat of the run (kLockRunLength-1 later)
    int truthIndex = -1;                 // nearest truth beat to the run start

    double startEventSeconds = 0.0;      // event time of the run's first beat
    double startAvailabilitySeconds = 0.0;
    bool   hasAvailability = false;

    double confirmEventSeconds = 0.0;    // event time of the run's last beat
    double confirmAvailabilitySeconds = 0.0;

    /** Scorer-identical acquisition fields (event clock). */
    double acquisitionSeconds = 0.0;
    double acquisitionBeats = 0.0;
    double acquisitionBars = 0.0;

    /** How many consecutive beats the full (tempo-agreeing) condition held.
        Equals kLockRunLength when found; otherwise the longest observed. */
    std::size_t longestRunWithTempo = 0;

    /** Longest run of consecutive beats that each land within tolerance of a
        distinct forward-advancing truth beat, IGNORING tempo agreement. */
    std::size_t longestMatchRun = 0;
};

/** Replays EVAL-002/004's `findFirstLockFrom` on `obs` (event clock). `minTime`
    is -infinity for initial acquisition, or the evidence-resume time for the
    stop/start recovery replay. `tol` should be the manifest tolerance and
    `bpmAgreement` is kBpmAgreementFraction (0.02) unless a test pins another. */
LockRun replayLock (const rhythmeval::RhythmTruth& truth,
                    const rhythmeval::ObservationSeries& obs,
                    double tol,
                    double minTime = -std::numeric_limits<double>::infinity(),
                    std::size_t lockRunLength = static_cast<std::size_t> (rhythmeval::kLockRunLength),
                    double bpmAgreement = rhythmeval::kBpmAgreementFraction);

/** Recovery lock replay for `stop_start`: same resume-time rule as
    Metrics.cpp scoreFixture (end of the last true-silence span, then the first
    onset at or after it). Returns a non-found LockRun for other fixtures. */
LockRun replayRecoveryLock (const rhythmeval::RhythmTruth& truth,
                            const rhythmeval::ObservationSeries& obs,
                            double tol);

/** Diagnosis of one fixture built from its trace + replay + scorer result. */
struct FixtureDiagnosis
{
    std::string name;
    bool core = false;
    bool steady = false;
    bool ramp = false;
    bool hasNominalBpm = false;
    double nominalBpm = 0.0;

    int predictedBeats = 0;
    int truthBeats = 0;
    int matchedBeats = 0;

    bool scorerAcquired = false;
    double scorerAcquisitionBars = 0.0;
    double scorerAcquisitionSeconds = 0.0;

    LockRun lock;                        // initial-acquisition replay
    LockRun recovery;                    // stop_start only
    bool agreesWithScorer = true;        // replay acquired == scorer acquired

    // --- trace evidence -----------------------------------------------------
    double medianBpm = 0.0;              // median of phase-valid tempo samples
    double medianBpmError = 0.0;         // |median - nominal| / nominal (if nominal)
    double bpmAgreementFractionInWindow = 0.0; // fraction of phase-valid samples within 2%
    double ratioToTruth = 0.0;           // median reported / local truth BPM at matched beats
    bool octaveSuspect = false;          // ratio within 10% of 0.5 or 2.0
    int silenceBlocks = 0;
    double silenceSeconds = 0.0;
    double meanSignedPhaseMs = 0.0;      // matched beats, event clock
    double meanAbsPhaseMs = 0.0;

    AcquireReason reason = AcquireReason::Other;
    std::string reasonDetail;
    std::string primaryReason () const { return toString (reason); }
};

/** Builds the diagnosis. `bpmAgreement` defaults to kBpmAgreementFraction. */
FixtureDiagnosis diagnoseFixture (const rhythmeval::RhythmTruth& truth,
                                  const rhythmeval::ObservationSeries& obs,
                                  const rhythmeval::FixtureMetrics& scorerResult,
                                  double tol);

} // namespace tracker_diag
