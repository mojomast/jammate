// Pure rhythm-tracker metrics for the guitar evaluation corpus (EVAL-002).
//
// SPEC.md 12.3 lists the metrics the tracker ADR must contain; SPEC.md 19 turns
// some of them into numeric gates. This file is the *definition* of those
// metrics. It is deliberately free of file I/O, wall-clock time and global
// state so that:
//
//   1. deterministic synthetic-observation tests can score hand-built
//      prediction series without touching audio (DEVPLAN EVAL-002 requires the
//      synthetic and audio paths to be separable), and
//   2. two runs over the same observations produce byte-identical numbers.
//
// The unit/aggregation/SPEC-19-target documentation for every field lives next
// to the field and in task-notes/EVAL-002.md; the two must stay in sync.
//
// Nothing here is licensed from or derived from a third party.

#pragma once

#include "Json.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rhythmeval
{

// ---------------------------------------------------------------------------
// Tolerances and thresholds
// ---------------------------------------------------------------------------
//
// All are named constants so a test can pin them and an ADR can cite them.

/** F-measure / phase / lock matching tolerance, seconds. Defaulted from the
    corpus manifest's `conventions.beatToleranceSeconds` (0.07) and overridable
    per run. See task-notes/EVAL-002.md ("Tolerances chosen") for the musical
    justification: 70 ms is well above the corpus's +/-10 ms humanisation and
    well below a beat at every corpus tempo, so it measures the tracker's
    placement rather than the player's jitter. */
inline constexpr double kBeatMatchToleranceSeconds = 0.070;

/** A "sustained lock" is this many consecutive predicted beats, each landing
    within tolerance of a distinct, forward-advancing ground-truth beat *and*
    accompanied by a tempo estimate within kBpmAgreementFraction of the local
    truth. Four beats is one bar of 4/4, so "acquired within 2 bars" is a real
    statement about a sustained correct lock, not a single lucky hit. The
    forward-advance rule tolerates an occasional dropped beat while rejecting a
    half/double-time cluster (whose tempo check also fails). */
inline constexpr int kLockRunLength = 4;

/** Tempo agreement used by the lock test and the BPM gate (SPEC 19: <= 2%). */
inline constexpr double kBpmAgreementFraction = 0.02;

/** Half/double band, as a fraction of the true ratio. A lock is "half" when
    reported/true is in [0.5*(1-b), 0.5*(1+b)] and "double" in
    [2*(1-b), 2*(1+b)]. 10% is tight enough that a merely wrong-but-not-metric
    tempo is not labelled half/double. */
inline constexpr double kHalfDoubleBandFraction = 0.10;

/** The corpus defines a `silentBeat` as one with no onset within +/-30 ms. The
    off-grid diagnostic reuses that window to decide whether a beat predicted
    inside a declared silence span is the maintained grid beat or an extra. */
inline constexpr double kSilentOnsetWindowSeconds = 0.030;

/** SPEC 19: "no tempo jump from one isolated syncopated event". A first
    difference of the reported BPM larger than this fraction of nominal is a
    jump. */
inline constexpr double kSyncopationJumpFraction = 0.05;

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

/** A declared silent region, seconds. */
struct SilenceSpan
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
};

/** Ground truth for one fixture, copied out of the corpus manifest. Only the
    fields a metric consumes are present; this is intentionally not the raw
    manifest record.

    `beats` is the metric grid. On the two `linear-ramp` fixtures its spacing is
    genuinely non-uniform (the analytic integral of the tempo curve), and every
    tempo metric must compare against this grid, never a single nominal BPM. */
struct RhythmTruth
{
    std::string name;

    std::vector<double> beats;                 // metric grid, strictly increasing
    std::vector<double> onsets;                // as-played attacks
    /** +/-30 ms windows around beats the player deliberately did not play while
        the phrase continued. NOT silence: a beat here is still intended and a
        tracker holding time through it is correct. Scored by the secondary
        counter only. */
    std::vector<SilenceSpan> silenceSpans;
    /** Regions where the guitar genuinely stopped (see the corpus manifest's
        `trueSilenceDerivation`). A beat event here is fabricated, whatever the
        notional grid says. This is the field the SPEC 19 silence gate is scored
        against. */
    std::vector<SilenceSpan> trueSilenceSpans;
    std::vector<int> silentBeats;              // indices into `beats`

    std::string tempoProfile;                  // "constant" | "linear-ramp"
    bool   hasNominalBpm = false;
    double nominalBpm = 0.0;
    double bpmStart = 0.0;
    double bpmEnd = 0.0;
    double rampStartSeconds = 0.0;
    double rampEndSeconds = 0.0;

    int beatsPerBar = 4;
    int meterNumerator = 4;
    int meterDenominator = 4;

    std::vector<std::string> tags;             // primary scenario first
    double durationSeconds = 0.0;
    double sampleRate = 48000.0;

    bool hasTag (const char* tag) const;
    bool isCore() const    { return hasTag ("core"); }
    bool isSteady() const  { return tempoProfile == "constant"; }
    bool isRamp() const    { return tempoProfile == "linear-ramp"; }
};

/** One time-stamped tempo/phase report from a backend. */
struct TempoSample
{
    double timeSeconds = 0.0;
    double bpm = 0.0;
    bool   phaseValid = false;
    bool   silence = false;
};

/** What an `IRhythmTracker` produced when driven over one fixture, reduced to
    the data the pure metrics need. Produced by `BackendRunner` from the
    backend's `RhythmObservation` stream, or built by hand in a unit test. */
struct ObservationSeries
{
    /** Predicted beat-event times, seconds, in emission order. */
    std::vector<double> beatTimesSeconds;

    /** Tempo/phase evidence over time (typically one sample per block). */
    std::vector<TempoSample> tempoSamples;

    double audioDurationSeconds = 0.0;
    double sampleRate = 48000.0;

    /** Resource metrics, carried through unchanged; not used by scoring maths. */
    double cpuSeconds = 0.0;
    std::size_t allocationCount = 0;
};

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------

/** All metrics for one fixture. Field comments give unit + aggregation. */
struct FixtureMetrics
{
    std::string name;
    bool core = false;
    bool steady = false;
    bool ramp = false;
    double beatToleranceSeconds = kBeatMatchToleranceSeconds;

    // --- beat-event detection (SPEC 12.3 "beat-event F-measure") -----------
    int predictedBeats = 0;
    int truthBeats = 0;
    int truePositives = 0;
    int falsePositives = 0;
    int falseNegatives = 0;
    // Fractions in [0,1]. Aggregated across fixtures by mean.
    double precision = 0.0;
    double recall = 0.0;
    double fMeasure = 0.0;

    // --- acquisition (SPEC 12.3 "acquisition time in beats/bars") ----------
    // acquisitionSeconds: lock time minus the first ground-truth beat.
    // acquisitionBeats: lock time as a fractional position in the GT grid
    //                   (0 == exactly on beat 0), so it is meaningful on ramps.
    // acquisitionBars:  acquisitionBeats / beatsPerBar.
    // acquired == false means no sustained lock was ever observed; the three
    // time fields are then 0 and must not be read.
    bool acquired = false;
    double acquisitionSeconds = 0.0;
    double acquisitionBeats = 0.0;
    double acquisitionBars = 0.0;

    // --- BPM relative error (SPEC 12.3 / SPEC 19 <= 2%) --------------------
    // Locked BPM is the median `bpmCandidate` over valid tempo samples in the
    // steady-state window; relative error is vs `nominalBpm`. Steady fixtures
    // only. Aggregated mean/median/worst; the gate uses the core worst case.
    bool hasNominalBpm = false;
    bool hasBpmLock = false;
    double lockedBpm = 0.0;
    double bpmRelativeError = 0.0;

    // --- half/double-time error (SPEC 12.3 / SPEC 19 < 5%) -----------------
    // ratio = lockedBpm / nominalBpm against the bands above. Distinct from
    // plain BPM error: it fires only when the lock is a metrical multiple.
    bool halfTimeLock = false;
    bool doubleTimeLock = false;
    bool halfDoubleTimeError = false;

    // --- false beats in silence (SPEC 12.3 / SPEC 19) ----------------------
    //
    // There are TWO distinct failures and they are reported separately, never
    // merged and never dropped.
    //
    // Primary: predicted beat events inside a `trueSilenceSpan` — a region where
    // the guitar genuinely stopped. A beat there is fabricated no matter where
    // the notional grid is, so every such prediction counts. This is SPEC 19's
    // "silence does not create false acceleration". Denominator is the total
    // true-silence duration.
    int falseBeatsInTrueSilence = 0;
    double trueSilenceSeconds = 0.0;
    double trueSilenceFractionOfDuration = 0.0;
    double falseBeatsInTrueSilencePerSecond = 0.0;
    /** False when this fixture cannot inform the primary metric: at least half
        its duration is true silence, so there is almost no playing for the
        tracker to fabricate against and the rate is low for the wrong reason.
        Such a fixture must be read as NOT-INFORMATIVE, never as a pass. */
    bool falseBeatMetricInformative = true;

    // Secondary (a different failure): predicted beat events inside the declared
    // `silenceSpans` — +/-30 ms windows around beats the player deliberately did
    // not play while playing around them. A tracker that invents beats here is
    // hallucinating inside playing material, not free-running through a stop.
    // `falseBeatsOffGridInUnplayedBeatWindows` excludes predictions that match
    // the maintained ground-truth grid beat (the maintained grid is inside these
    // windows by construction), so it is literally "extra beats where none were
    // played".
    int falseBeatsInUnplayedBeatWindows = 0;
    double unplayedBeatWindowsSeconds = 0.0;
    double falseBeatsInUnplayedBeatWindowsPerSecond = 0.0;
    int falseBeatsOffGridInUnplayedBeatWindows = 0;
    double falseBeatsOffGridInUnplayedBeatWindowsPerSecond = 0.0;

    // --- recovery after stop/start (SPEC 12.3) -----------------------------
    // Only for the `stop_start` fixture: lock time minus the first onset after
    // the last declared silence span. Seconds and beats. hasRecovery==false
    // means not applicable or no post-silence lock.
    bool hasRecovery = false;
    double recoverySeconds = 0.0;
    double recoveryBeats = 0.0;

    // --- syncopation stability (SPEC 12.3 / SPEC 19) -----------------------
    // For the syncopated fixture: standard deviation and worst absolute
    // deviation of reported BPM from nominal, plus the largest first difference
    // (the "jump" the gate forbids), as a fraction of nominal.
    bool hasSyncopation = false;
    double syncopationTempoStdDevBpm = 0.0;
    double syncopationTempoCv = 0.0;
    double syncopationMaxDeviationBpm = 0.0;
    double syncopationMaxDeviationFraction = 0.0;
    double syncopationMaxStepBpm = 0.0;
    double syncopationMaxStepFraction = 0.0;

    // --- tempo-drift on ramps (SPEC 12.3) ----------------------------------
    // Local tempo implied by consecutive predicted beats vs the local tempo of
    // the non-uniform ground-truth grid, at the same time. Relative error.
    bool hasRamp = false;
    double rampLocalTempoRelErrorMean = 0.0;
    double rampLocalTempoRelErrorWorst = 0.0;

    // --- phase error (SPEC 12.3) -------------------------------------------
    // Signed and absolute error of matched predicted beats against the nearest
    // ground-truth beat, in ms and in local beats. Mean and p50/p95 of |error|.
    double phaseMeanMs = 0.0;
    double phaseMeanAbsMs = 0.0;
    double phaseP50AbsMs = 0.0;
    double phaseP95AbsMs = 0.0;
    double phaseMeanBeats = 0.0;
    double phaseMeanAbsBeats = 0.0;
    double phaseP95AbsBeats = 0.0;

    // --- resources (SPEC 12.3) ---------------------------------------------
    double cpuSeconds = 0.0;
    std::size_t allocationCount = 0;
};

/** Per-metric aggregates across the whole corpus and the SPEC 19 gate flags.
    Aggregation rules: mean where an average is meaningful, median and worst
    where outliers decide; the gate fields name the exact rule used. */
struct AggregateMetrics
{
    int fixtures = 0;
    int coreFixtures = 0;
    int steadyFixtures = 0;
    int rampFixtures = 0;

    // SPEC 19: acquire useful lock within 2 bars for >= 95% of core fixtures.
    int acquisitionCoreEvaluated = 0;
    int acquisitionCoreWithin2Bars = 0;
    double acquisitionCorePassFraction = 0.0;
    double acquisitionBarsWorstCore = 0.0;

    // SPEC 19: locked BPM relative error <= 2% on steady-tempo core fixtures.
    int bpmRelErrorCoreEvaluated = 0;
    double bpmRelErrorMeanSteady = 0.0;
    double bpmRelErrorMedianSteady = 0.0;
    double bpmRelErrorWorstCore = 0.0;

    // Detection, averaged over all fixtures.
    double fMeasureMean = 0.0;
    double precisionMean = 0.0;
    double recallMean = 0.0;

    // SPEC 19: half/double-time errors < 5% on core fixtures.
    int halfDoubleEvaluated = 0;
    int halfDoubleErrors = 0;
    double halfDoubleErrorRate = 0.0;
    int halfDoubleEvaluatedCore = 0;
    int halfDoubleErrorsCore = 0;
    double halfDoubleErrorRateCore = 0.0;

    int silenceFixtures = 0;
    int trueSilenceInformativeFixtures = 0;
    double falseBeatsInTrueSilencePerSecondWorst = 0.0;
    /** Worst primary rate over fixtures that are informative for it; this is the
        number the silence gate is read from. */
    double falseBeatsInTrueSilencePerSecondWorstInformative = 0.0;
    double falseBeatsInUnplayedBeatWindowsPerSecondWorst = 0.0;
    double falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst = 0.0;

    bool hasRecovery = false;
    double recoverySecondsWorst = 0.0;

    bool hasSyncopation = false;
    double syncopationMaxDeviationFraction = 0.0;
    double syncopationMaxStepFraction = 0.0;

    bool hasRamp = false;
    double rampLocalTempoRelErrorMean = 0.0;
    double rampLocalTempoRelErrorWorst = 0.0;

    double cpuSecondsTotal = 0.0;
    std::size_t allocationsTotal = 0;

    // Gate evaluations. Each is the exact SPEC 19 rule named in the comment
    // above the corresponding FixtureMetrics field.
    bool gateAcquire95Core = false;
    bool gateBpm2Core = false;
    bool gateHalfDouble5Core = false;
    bool gateSyncopationNoJump = false;
    bool gateRampFollows = false;
};

// ---------------------------------------------------------------------------
// Pure functions
// ---------------------------------------------------------------------------

/** Score one fixture. Deterministic and I/O-free. `beatToleranceSeconds` may
    be overridden (e.g. from the manifest); it defaults to 70 ms.

    `latencyCompensationSeconds` is the backend's documented output delay. When
    non-zero it is SUBTRACTED from every predicted beat time before any metric
    is computed, so a backend that reports its beats `L` seconds late is scored
    as if it reported them on time. It is applied once, up front, so F-measure,
    phase error, acquisition, false beats and recovery all see the compensated
    clock; a compensation that fixed phase but not F would be a bug. Default 0
    (off): the harness never compensates unless the caller asks.

    Domain: `truth.beats` may be empty (returns a finite all-zero result);
    `obs` may be empty or contain duplicate/out-of-order beats (handled without
    NaN or division by zero). */
FixtureMetrics scoreFixture (const RhythmTruth& truth,
                             const ObservationSeries& obs,
                             double beatToleranceSeconds = kBeatMatchToleranceSeconds,
                             double latencyCompensationSeconds = 0.0);

/** Aggregate per-fixture results and evaluate the SPEC 19 gates. */
AggregateMetrics aggregateFixtures (const std::vector<FixtureMetrics>& perFixture);

/** One run of the whole corpus at a single latency-compensation setting. The
    CLI produces the uncompensated variant always and one variant per requested
    compensation, so the size of the effect is measured rather than asserted. */
struct ScoringVariant
{
    std::string label;                            // e.g. "uncompensated"
    double latencyCompensationSeconds = 0.0;
    std::vector<FixtureMetrics> fixtures;
    AggregateMetrics aggregate;
};

// ---------------------------------------------------------------------------
// Deterministic serialisation (SPEC 21.3: JSON/CSV + Markdown summary)
// ---------------------------------------------------------------------------
//
// These are pure functions of the metric values, so serialisation is as
// deterministic as scoring. They live here (rather than in the CLI) so the test
// suite can prove byte-identical JSON/CSV across repeated runs.

rhythmjson::Value fixtureMetricsToJson (const FixtureMetrics& metrics);
rhythmjson::Value aggregateMetricsToJson (const AggregateMetrics& aggregate);

/** One variant as {label, latencyCompensationSeconds, fixtures, aggregate}. */
rhythmjson::Value scoringVariantToJson (const ScoringVariant& variant);

const char* fixtureMetricsCsvHeader();
std::string fixtureMetricsCsvRow (const FixtureMetrics& metrics);

/** CSV header/row with a leading `variant,compensationSeconds` pair, so one
    per-fixture file and the combined CSV can carry every variant. */
const char* fixtureMetricsCsvHeaderWithVariant();
std::string fixtureMetricsCsvRowWithVariant (const ScoringVariant& variant,
                                             const FixtureMetrics& metrics);

/** Human-readable Markdown summary covering every variant: per-fixture tables,
    per-metric aggregates, the SPEC 19 gate table (each gate PASS / FAIL /
    NOT-INFORMATIVE with its reason) and the measured latency effect. */
std::string markdownSummary (const std::string& backendId,
                             const std::string& corpusId,
                             double toleranceSeconds,
                             const std::vector<ScoringVariant>& variants);

// ---------------------------------------------------------------------------
// Small pure helpers, exposed for tests and for EVAL-003 extensions
// ---------------------------------------------------------------------------

/** One-to-one maximum matching between predicted and ground-truth beats within
    `toleranceSeconds`, using the optimal two-pointer sweep over sorted inputs.
    Returns the number of matched pairs and fills `predMatch` with the matched
    ground-truth index for each matched prediction (-1 otherwise). */
int matchBeats (const std::vector<double>& predicted,
                const std::vector<double>& truth,
                double toleranceSeconds,
                std::vector<int>& predMatch);

/** Fractional beat position of time `t` in the ground-truth grid (0 at
    beats[0]), interpolating between beats and extrapolating with the local
    period. Returns 0 for a grid with fewer than one beat. */
double beatPositionAt (const RhythmTruth& truth, double t);

/** Local ground-truth tempo at time `t`, BPM, from the grid gaps. */
double localTruthBpmAtTime (const RhythmTruth& truth, double t);

} // namespace rhythmeval
