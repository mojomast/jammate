// Pure implementations of the EVAL-002 rhythm metrics. See Metrics.h for the
// contract and task-notes/EVAL-002.md for the per-metric definition, unit,
// aggregation and SPEC 19 target.

#include "Metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace rhythmeval
{

namespace
{

bool isFinite (double v) { return std::isfinite (v); }

bool contains (const std::vector<std::string>& v, const char* s)
{
    return std::find (v.begin(), v.end(), std::string (s)) != v.end();
}

/** Local ground-truth beat period at grid index `i` (seconds per beat). */
double localPeriodAtIndex (const RhythmTruth& truth, std::size_t i)
{
    const std::size_t n = truth.beats.size();
    if (n < 2)
        return truth.hasNominalBpm && truth.nominalBpm > 0.0
                   ? 60.0 / truth.nominalBpm : 0.0;
    if (i + 1 < n)
        return truth.beats[i + 1] - truth.beats[i];
    return truth.beats[i] - truth.beats[i - 1];
}

/** Local ground-truth tempo at grid index `i`, BPM. */
double localBpmAtIndex (const RhythmTruth& truth, std::size_t i)
{
    const double period = localPeriodAtIndex (truth, i);
    return period > 0.0 ? 60.0 / period : 0.0;
}

/** Index of the nearest ground-truth beat to `t`, or -1 for an empty grid. */
int nearestTruthIndex (const std::vector<double>& beats, double t)
{
    if (beats.empty())
        return -1;
    const std::vector<double>::const_iterator it =
        std::lower_bound (beats.begin(), beats.end(), t);
    if (it == beats.begin())
        return 0;
    if (it == beats.end())
        return static_cast<int> (beats.size()) - 1;
    const int hi = static_cast<int> (it - beats.begin());
    const int lo = hi - 1;
    return (t - beats[static_cast<std::size_t> (lo)])
               <= (beats[static_cast<std::size_t> (hi)] - t)
           ? lo : hi;
}

double median (std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    const std::size_t n = v.size();
    if (n % 2 == 1)
        return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

/** Percentile of an unsorted vector using the nearest-rank convention. */
double percentile (std::vector<double> v, double p)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    const double clamped = std::min (1.0, std::max (0.0, p));
    const std::size_t idx = static_cast<std::size_t> (
        std::floor (clamped * static_cast<double> (v.size() - 1) + 0.5));
    return v[std::min (idx, v.size() - 1)];
}

/** Tempo sample nearest in time to `t` (tempo samples are emitted in time
    order). Returns nullptr when there are no samples. */
const TempoSample* tempoSampleNear (const std::vector<TempoSample>& samples, double t)
{
    if (samples.empty())
        return nullptr;
    const auto it = std::lower_bound (
        samples.begin(), samples.end(), t,
        [] (const TempoSample& s, double time) { return s.timeSeconds < time; });
    if (it == samples.begin())
        return &samples.front();
    if (it == samples.end())
        return &samples.back();
    const TempoSample& hi = *it;
    const TempoSample& lo = *(it - 1);
    return (t - lo.timeSeconds) <= (hi.timeSeconds - t) ? &lo : &hi;
}

/** Finds the first predicted beat at or after `minTime` that begins a
    sustained lock: kLockRunLength consecutive predicted beats, each within
    `tol` of a distinct, forward-advancing ground-truth beat, each accompanied
    by a tempo estimate within kBpmAgreementFraction. Requiring the ground-truth
    indices to strictly advance tolerates an occasional dropped beat (the
    tracker is still locked) while rejecting clustered or backtracking
    predictions. `minTime` is -infinity for the initial acquisition and the
    evidence-resume time for stop/start recovery. */
bool findFirstLockFrom (const RhythmTruth& truth,
                        const ObservationSeries& obs,
                        double tol,
                        double minTime,
                        std::size_t& lockPredIndex,
                        std::size_t& lockTruthIndex)
{
    const std::vector<double>& pred = obs.beatTimesSeconds;
    const std::vector<double>& beats = truth.beats;
    if (beats.empty() || pred.size() < static_cast<std::size_t> (kLockRunLength))
        return false;

    const std::size_t runLength = static_cast<std::size_t> (kLockRunLength);
    for (std::size_t i = 0; i + runLength <= pred.size(); ++i)
    {
        if (pred[i] < minTime - 1.0e-9)
            continue;

        bool ok = true;
        int previousGi = -1;
        for (std::size_t k = 0; k < runLength; ++k)
        {
            const int gi = nearestTruthIndex (beats, pred[i + k]);
            if (gi < 0 || std::fabs (pred[i + k] - beats[static_cast<std::size_t> (gi)]) > tol)
            {
                ok = false;
                break;
            }
            if (gi <= previousGi)   // must advance; rejects clusters/backtracking
            {
                ok = false;
                break;
            }
            const double truthBpm = localBpmAtIndex (truth, static_cast<std::size_t> (gi));
            if (truthBpm > 0.0)
            {
                const TempoSample* s = tempoSampleNear (obs.tempoSamples, pred[i + k]);
                if (s == nullptr || ! s->phaseValid || ! (s->bpm > 0.0)
                    || std::fabs (s->bpm - truthBpm) / truthBpm > kBpmAgreementFraction)
                {
                    ok = false;
                    break;
                }
            }
            previousGi = gi;
        }
        if (ok)
        {
            lockPredIndex = i;
            lockTruthIndex = static_cast<std::size_t> (
                nearestTruthIndex (beats, pred[i]));
            return true;
        }
    }
    return false;
}

double spanTotalSeconds (const std::vector<SilenceSpan>& spans)
{
    double total = 0.0;
    for (const SilenceSpan& s : spans)
        total += std::max (0.0, s.endSeconds - s.startSeconds);
    return total;
}

bool inAnySpan (const std::vector<SilenceSpan>& spans, double t)
{
    for (const SilenceSpan& s : spans)
        if (t >= s.startSeconds && t <= s.endSeconds)
            return true;
    return false;
}

bool nearAnyTruthBeat (const std::vector<double>& beats, double t, double window)
{
    if (beats.empty())
        return false;
    const std::vector<double>::const_iterator it =
        std::lower_bound (beats.begin(), beats.end(), t);
    if (it != beats.end() && std::fabs (*it - t) <= window)
        return true;
    if (it != beats.begin() && std::fabs (*(it - 1) - t) <= window)
        return true;
    return false;
}

/** One reviewed, hash-keyed known fast-decay silence defect. `sha256` is the
    match key: the DECLARED WAV identity, never the fixture name. `name` and
    `citation` are the reviewed label and the independent measurement that
    established the defect. */
struct KnownFastDecayDefect
{
    const char* name;
    const char* sha256;
    const char* citation;
};

/** Reviewed registry of defective recordings whose declared `trueSilenceSpans`
    are a synthesis artifact rather than a performance (EVAL-007).

    This was previously a by-NAME list (`sustained_chords`, `tapping_muting_only`)
    which EVAL-006 invalidated: the repaired `sustained_chords` render has the
    same name and genuine, independently measured 1.43 s of silence, while the
    `tapping_muting_only` audit found its sparse occupancy to be a property of
    tap playing, not a defect. Coverage is therefore keyed on the exact WAV
    hash, so:
      - the original fast-decay bytes stay `CorpusDefect`, even if renamed;
      - the repaired same-name render is `Measured`;
      - `tapping_muting_only` has no entry and is `Measured` from its onset audit.

    The citation is `docs/research/SUSTAIN-REPAIR.md` (which builds on
    `docs/research/CORPUS-ACOUSTIC-REVIEW.md`). Hashes are the declared manifest
    hashes, verified externally and re-hashed by the evidence tests; this table
    does not read or authenticate the PCM and is not a copyright conclusion. */
const KnownFastDecayDefect kKnownFastDecayDefects[] = {
    { "sustained_chords",
      "e4b9297fca341e70a639fc6a51fbd9e884c1321ee4fc802c5c3497feeefe6442",
      "EVAL-006 docs/research/SUSTAIN-REPAIR.md / CORPUS-ACOUSTIC-REVIEW.md: "
      "measured 30-35 dB attack collapse by 300 ms, 8.734 s of 11.35 s declared "
      "true silence from the two-stage fast decay" }
};

const KnownFastDecayDefect* findKnownFastDecayDefect (const RhythmTruth& truth)
{
    if (truth.sha256.empty())
        return nullptr;   // no identity: cannot be a hash-keyed defect
    for (const KnownFastDecayDefect& d : kKnownFastDecayDefects)
        if (truth.sha256 == d.sha256)
            return &d;
    return nullptr;
}

/** True for a derived-noise perturbation (`scenarioTags` carry both `derived`
    and `noise`). Such a clip inherits its parent's `trueSilenceSpans`, but the
    added noise floor now fills what used to be quiet, so those spans are
    STRUCTURAL and are not a re-measured acoustic stop. Name/source tags cannot
    by themselves establish physical silence, so this becomes an explicit
    `NotAssessedStructuralNoise` coverage rather than a silent `Measured`. The
    raw counts are still computed and reported. The base corpus uses the
    qualifier `noisy`, not `noise`, so `noisy_microphone` is unaffected. */
bool isDerivedNoiseTransform (const RhythmTruth& truth)
{
    return truth.hasTag ("derived") && truth.hasTag ("noise");
}


/** Steady-state window used for BPM / half-double / syncopation metrics. */
void steadyWindow (const RhythmTruth& truth, const FixtureMetrics& base,
                   double& start, double& end)
{
    if (truth.beats.empty())
    {
        start = 0.0;
        end = truth.durationSeconds;
        return;
    }
    end = truth.durationSeconds > 0.0 ? truth.durationSeconds : truth.beats.back();
    if (base.acquired && base.acquisitionSeconds >= 0.0)
        start = truth.beats.front() + base.acquisitionSeconds;
    else
        start = truth.beats.front()
                + 0.5 * (truth.beats.back() - truth.beats.front());
    if (start > end)
        start = end;
}

} // namespace

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

bool RhythmTruth::hasTag (const char* tag) const
{
    return contains (tags, tag);
}

const char* knownSilenceDefectCitation (const RhythmTruth& truth)
{
    const KnownFastDecayDefect* defect = findKnownFastDecayDefect (truth);
    return defect != nullptr ? defect->citation : "";
}

int matchBeats (const std::vector<double>& predicted,
                const std::vector<double>& truth,
                double toleranceSeconds,
                std::vector<int>& predMatch)
{
    predMatch.assign (predicted.size(), -1);
    if (predicted.empty() || truth.empty())
        return 0;

    std::size_t i = 0;   // prediction cursor
    std::size_t j = 0;   // truth cursor
    int matches = 0;
    while (i < predicted.size() && j < truth.size())
    {
        const double d = predicted[i] - truth[j];
        if (std::fabs (d) <= toleranceSeconds)
        {
            predMatch[i] = static_cast<int> (j);
            ++matches;
            ++i;
            ++j;
        }
        else if (d < 0.0)
        {
            ++i;   // prediction is too early for this truth beat
        }
        else
        {
            ++j;   // truth beat is too early; no prediction owns it
        }
    }
    return matches;
}

double beatPositionAt (const RhythmTruth& truth, double t)
{
    const std::vector<double>& b = truth.beats;
    if (b.empty())
        return 0.0;
    if (b.size() == 1)
        return 0.0;
    if (t <= b.front())
    {
        const double gap = b[1] - b[0];
        return gap > 0.0 ? (t - b.front()) / gap : 0.0;
    }
    if (t >= b.back())
    {
        const double gap = b[b.size() - 1] - b[b.size() - 2];
        return static_cast<double> (b.size() - 1)
               + (gap > 0.0 ? (t - b.back()) / gap : 0.0);
    }
    const std::vector<double>::const_iterator it =
        std::upper_bound (b.begin(), b.end(), t);
    const std::size_t hi = static_cast<std::size_t> (it - b.begin());
    const std::size_t lo = hi - 1;
    const double gap = b[hi] - b[lo];
    return static_cast<double> (lo)
           + (gap > 0.0 ? (t - b[lo]) / gap : 0.0);
}

double localTruthBpmAtTime (const RhythmTruth& truth, double t)
{
    const std::vector<double>& b = truth.beats;
    if (b.size() < 2)
        return truth.hasNominalBpm ? truth.nominalBpm : 0.0;

    std::size_t idx = 0;
    if (t <= b.front())
        idx = 0;
    else if (t >= b.back())
        idx = b.size() - 2;
    else
    {
        const std::vector<double>::const_iterator it =
            std::upper_bound (b.begin(), b.end(), t);
        const std::size_t hi = static_cast<std::size_t> (it - b.begin());
        idx = hi >= b.size() ? b.size() - 2 : hi - 1;
    }
    const double period = b[idx + 1] - b[idx];
    return period > 0.0 ? 60.0 / period : 0.0;
}

// ---------------------------------------------------------------------------
// scoreFixture
// ---------------------------------------------------------------------------

FixtureMetrics scoreFixture (const RhythmTruth& truth,
                             const ObservationSeries& obs,
                             double beatToleranceSeconds,
                             double latencyCompensationSeconds)
{
    FixtureMetrics m;
    m.name = truth.name;
    m.sourceSha256 = truth.sha256;
    m.core = truth.isCore();
    m.steady = truth.isSteady();
    m.ramp = truth.isRamp();
    m.beatToleranceSeconds = beatToleranceSeconds;
    m.hasNominalBpm = truth.hasNominalBpm;
    m.truthBeats = static_cast<int> (truth.beats.size());
    m.detectionMeasured = m.truthBeats > 0;
    m.predictedBeats = static_cast<int> (obs.beatTimesSeconds.size());
    m.cpuSeconds = obs.cpuSeconds;
    m.allocationCount = obs.allocationCount;

    // Backend timing diagnostics are copied verbatim; they are evidence about
    // how the runner mapped the backend onto its block clock, not scored input.
    m.timingDiagnosticsMeasured = ! obs.beatAvailabilitySeconds.empty();
    m.beatsReportedByBackend = obs.diagnostics.beatsReportedByBackend;
    m.beatsStampAtBlockStart = obs.diagnostics.beatsAtBlockStart;
    m.beatsRejectedNonCausal = obs.diagnostics.beatsRejectedNonCausal;
    m.rateMismatchBlocks = obs.diagnostics.rateMismatchBlocks;
    m.causalAvailabilityMeanSeconds =
        obs.diagnostics.meanReportedAvailabilityLatencySeconds;
    m.causalAvailabilityMaxSeconds =
        obs.diagnostics.maxReportedAvailabilityLatencySeconds;

    // Latency compensation is applied exactly once, here, to the predicted beat
    // clock. Every metric below reads `scored`, never `obs.beatTimesSeconds`, so
    // F-measure, phase, acquisition, false beats and recovery can never disagree
    // about when a beat happened. A no-op at 0.0 by construction.
    ObservationSeries scored = obs;
    if (latencyCompensationSeconds != 0.0)
        for (double& t : scored.beatTimesSeconds)
            t -= latencyCompensationSeconds;

    // --- matching, precision/recall/F, phase error -------------------------
    std::vector<int> predMatch;
    m.truePositives = matchBeats (scored.beatTimesSeconds, truth.beats,
                                  beatToleranceSeconds, predMatch);
    m.falsePositives = m.predictedBeats - m.truePositives;
    m.falseNegatives = m.truthBeats - m.truePositives;
    if (m.falsePositives < 0) m.falsePositives = 0;
    if (m.falseNegatives < 0) m.falseNegatives = 0;

    // Precision is defined as 1 when nothing was predicted (there are no false
    // positives), recall as 1 when the truth is empty. F is then well defined.
    m.precision = m.predictedBeats > 0
                      ? static_cast<double> (m.truePositives)
                            / static_cast<double> (m.predictedBeats)
                      : 1.0;
    m.recall = m.truthBeats > 0
                   ? static_cast<double> (m.truePositives)
                         / static_cast<double> (m.truthBeats)
                   : 1.0;
    const double pr = m.precision + m.recall;
    m.fMeasure = pr > 0.0 ? 2.0 * m.precision * m.recall / pr : 0.0;

    {
        std::vector<double> absMs, absBeats;
        double signedMsSum = 0.0;
        double signedBeatsSum = 0.0;
        int signedCount = 0;
        for (std::size_t i = 0; i < scored.beatTimesSeconds.size(); ++i)
        {
            if (predMatch[i] < 0)
                continue;
            const int near = nearestTruthIndex (truth.beats, scored.beatTimesSeconds[i]);
            if (near < 0)
                continue;
            const double signedSeconds =
                scored.beatTimesSeconds[i] - truth.beats[static_cast<std::size_t> (near)];
            const double localBpm = localBpmAtIndex (truth, static_cast<std::size_t> (near));
            const double localBeats =
                localBpm > 0.0 ? signedSeconds * localBpm / 60.0 : 0.0;
            signedMsSum += signedSeconds * 1000.0;
            signedBeatsSum += localBeats;
            absMs.push_back (std::fabs (signedSeconds) * 1000.0);
            absBeats.push_back (std::fabs (localBeats));
            ++signedCount;
        }
        if (signedCount > 0)
        {
            const double inv = 1.0 / static_cast<double> (signedCount);
            m.phaseMeanMs = signedMsSum * inv;
            m.phaseMeanBeats = signedBeatsSum * inv;
            double absMsSum = 0.0;
            double absBeatsSum = 0.0;
            for (const double v : absMs) absMsSum += v;
            for (const double v : absBeats) absBeatsSum += v;
            m.phaseMeanAbsMs = absMsSum * inv;
            m.phaseMeanAbsBeats = absBeatsSum * inv;
            m.phaseP50AbsMs = percentile (absMs, 0.50);
            m.phaseP95AbsMs = percentile (absMs, 0.95);
            m.phaseP95AbsBeats = percentile (absBeats, 0.95);
        }
        // Zero matched beats means the phase fields above are UNDEFINED, not
        // perfect. `phaseMeasured` is how a consumer knows to show them missing.
        m.phaseMatchedBeats = signedCount;
        m.phaseMeasured = signedCount > 0;
    }

    // --- acquisition -------------------------------------------------------
    {
        std::size_t lockPred = 0;
        std::size_t lockTruth = 0;
        if (findFirstLockFrom (truth, scored, beatToleranceSeconds,
                               -std::numeric_limits<double>::infinity(),
                               lockPred, lockTruth))
        {
            m.acquired = true;
            const double lockTime = scored.beatTimesSeconds[lockPred];
            m.acquisitionSeconds = std::max (0.0, lockTime - truth.beats.front());
            m.acquisitionBeats = std::max (0.0, beatPositionAt (truth, lockTime));
            m.acquisitionBars = truth.beatsPerBar > 0
                                    ? m.acquisitionBeats
                                          / static_cast<double> (truth.beatsPerBar)
                                    : 0.0;
        }
    }

    // --- BPM lock ----------------------------------------------------------
    double windowStart = 0.0;
    double windowEnd = 0.0;
    steadyWindow (truth, m, windowStart, windowEnd);
    {
        std::vector<double> locked;
        for (const TempoSample& s : obs.tempoSamples)
            if (s.phaseValid && s.bpm > 0.0
                && s.timeSeconds >= windowStart && s.timeSeconds <= windowEnd)
                locked.push_back (s.bpm);
        if (locked.empty())
            for (const TempoSample& s : obs.tempoSamples)
                if (s.phaseValid && s.bpm > 0.0)
                    locked.push_back (s.bpm);
        if (! locked.empty())
        {
            m.hasBpmLock = true;
            m.lockedBpm = median (locked);
            if (truth.hasNominalBpm && truth.nominalBpm > 0.0)
                m.bpmRelativeError =
                    std::fabs (m.lockedBpm - truth.nominalBpm) / truth.nominalBpm;
        }
    }

    // --- half / double time ------------------------------------------------
    if (truth.hasNominalBpm && truth.nominalBpm > 0.0 && m.hasBpmLock)
    {
        const double ratio = m.lockedBpm / truth.nominalBpm;
        const double b = kHalfDoubleBandFraction;
        m.halfTimeLock = ratio >= 0.5 * (1.0 - b) && ratio <= 0.5 * (1.0 + b);
        m.doubleTimeLock = ratio >= 2.0 * (1.0 - b) && ratio <= 2.0 * (1.0 + b);
        m.halfDoubleTimeError = m.halfTimeLock || m.doubleTimeLock;
    }

    // --- false beats -------------------------------------------------------
    {
        m.trueSilenceSeconds = spanTotalSeconds (truth.trueSilenceSpans);
        m.trueSilenceMeasured = m.trueSilenceSeconds > 0.0;
        m.unplayedBeatWindowsSeconds = spanTotalSeconds (truth.silenceSpans);
        m.trueSilenceFractionOfDuration =
            truth.durationSeconds > 0.0
                ? m.trueSilenceSeconds / truth.durationSeconds : 0.0;

        for (const double t : scored.beatTimesSeconds)
        {
            // Primary diagnostic: a beat event in genuine silence. This is a
            // DIAGNOSTIC, not SPEC 19's gate: the SPEC forbids false
            // ACCELERATION, and a short holdover that keeps the grid through a
            // gap is intended behaviour, so a count of held grid beats must not
            // fail a gate. See the acceleration diagnostic below.
            if (inAnySpan (truth.trueSilenceSpans, t))
                ++m.falseBeatsInTrueSilence;

            // Secondary: a beat in an unplayed-beat window is a different
            // failure (hallucination inside playing material). Counted
            // separately; the off-grid variant excludes the held grid beat.
            if (inAnySpan (truth.silenceSpans, t))
            {
                ++m.falseBeatsInUnplayedBeatWindows;
                if (! nearAnyTruthBeat (truth.beats, t, kSilentOnsetWindowSeconds))
                    ++m.falseBeatsOffGridInUnplayedBeatWindows;
            }
        }

        if (m.trueSilenceSeconds > 0.0)
            m.falseBeatsInTrueSilencePerSecond =
                static_cast<double> (m.falseBeatsInTrueSilence) / m.trueSilenceSeconds;
        if (m.unplayedBeatWindowsSeconds > 0.0)
        {
            m.falseBeatsInUnplayedBeatWindowsPerSecond =
                static_cast<double> (m.falseBeatsInUnplayedBeatWindows)
                / m.unplayedBeatWindowsSeconds;
            m.falseBeatsOffGridInUnplayedBeatWindowsPerSecond =
                static_cast<double> (m.falseBeatsOffGridInUnplayedBeatWindows)
                / m.unplayedBeatWindowsSeconds;
        }

        // Coverage/reason. A fixture with no silence is NOT MEASURED (a rate of
        // 0 is absent data, not a pass). A known corpus synthesis defect is
        // flagged by its exact WAV hash, never by name. A derived noise clip's
        // inherited spans are structural, not measured acoustic silence. None
        // of these is a numeric duration/occupancy threshold.
        if (! m.trueSilenceMeasured)
            m.falseBeatCoverage = FalseBeatCoverage::NoTrueSilence;
        else if (isDerivedNoiseTransform (truth))
            m.falseBeatCoverage = FalseBeatCoverage::NotAssessedStructuralNoise;
        else if (findKnownFastDecayDefect (truth) != nullptr)
            m.falseBeatCoverage = FalseBeatCoverage::CorpusDefect;
        else
            m.falseBeatCoverage = FalseBeatCoverage::Measured;
        if (m.falseBeatCoverage == FalseBeatCoverage::CorpusDefect)
            m.falseBeatCoverageCitation = knownSilenceDefectCitation (truth);
        m.falseBeatMetricInformative =
            (m.falseBeatCoverage == FalseBeatCoverage::Measured);

        // --- silence acceleration diagnostic -------------------------------
        // For each true-silence span, compare the reported BPM just after the
        // span with the reported BPM just before it. A positive difference is a
        // tempo INCREASE across the gap. "Before" is the last phase-valid sample
        // at or before the span start; "after" is the first phase-valid sample
        // at or after the span end. Missing either side is insufficient
        // evidence, not a pass and not a failure.
        for (const SilenceSpan& span : truth.trueSilenceSpans)
        {
            const TempoSample* before = nullptr;
            const TempoSample* after = nullptr;
            for (const TempoSample& s : obs.tempoSamples)
            {
                if (! s.phaseValid || ! (s.bpm > 0.0))
                    continue;
                if (s.timeSeconds <= span.startSeconds)
                    before = &s;
                if (s.timeSeconds >= span.endSeconds && after == nullptr)
                    after = &s;
            }
            if (before != nullptr && after != nullptr)
            {
                ++m.silenceSpansEvaluated;
                m.maxSilenceTempoIncreaseBpm =
                    std::max (m.maxSilenceTempoIncreaseBpm,
                              after->bpm - before->bpm);
            }
            else
            {
                ++m.silenceSpansInsufficientEvidence;
            }
        }
        m.silenceAccelerationMeasured = m.silenceSpansEvaluated > 0;
        m.silenceAccelerationInsufficientEvidence =
            m.silenceSpansEvaluated == 0 && m.silenceSpansInsufficientEvidence > 0;
    }

    // --- recovery after stop/start ----------------------------------------
    // EVAL-004 fix: recovery is measured from the end of the real stop
    // (`trueSilenceSpans`), not from the +/-30 ms unplayed-beat windows
    // (`silenceSpans`). The old code measured "first lock after the last
    // unplayed beat window", which on `stop_start` ended before the actual stop,
    // so a tracker that re-locked mid-stop could be scored as recovered.
    if (truth.name == "stop_start" && ! truth.trueSilenceSpans.empty())
    {
        double silenceEnd = 0.0;
        for (const SilenceSpan& s : truth.trueSilenceSpans)
            silenceEnd = std::max (silenceEnd, s.endSeconds);
        double evidenceResume = silenceEnd;
        for (const double onset : truth.onsets)
            if (onset >= silenceEnd)
            {
                evidenceResume = onset;
                break;
            }

        std::size_t lockPred = 0;
        std::size_t lockTruth = 0;
        const bool lockedAfter = findFirstLockFrom (truth, scored, beatToleranceSeconds,
                                                    evidenceResume, lockPred, lockTruth);
        if (lockedAfter)
        {
            const double lockTime = scored.beatTimesSeconds[lockPred];
            m.hasRecovery = true;
            m.recoverySeconds = std::max (0.0, lockTime - evidenceResume);
            const double bpm = localTruthBpmAtTime (truth, evidenceResume);
            m.recoveryBeats = bpm > 0.0 ? m.recoverySeconds * bpm / 60.0 : 0.0;
        }
    }

    // --- syncopation stability --------------------------------------------
    if (truth.name == "syncopated_funk" || truth.hasTag ("syncopated_funk"))
    {
        std::vector<double> stable;
        for (const TempoSample& s : obs.tempoSamples)
            if (s.phaseValid && s.bpm > 0.0
                && s.timeSeconds >= windowStart && s.timeSeconds <= windowEnd)
                stable.push_back (s.bpm);
        if (stable.size() >= 2)
        {
            m.hasSyncopation = true;
            double sum = 0.0;
            for (const double v : stable) sum += v;
            const double mean = sum / static_cast<double> (stable.size());
            double var = 0.0;
            for (const double v : stable) var += (v - mean) * (v - mean);
            var /= static_cast<double> (stable.size());
            m.syncopationTempoStdDevBpm = std::sqrt (var);
            m.syncopationTempoCv = mean > 0.0 ? m.syncopationTempoStdDevBpm / mean : 0.0;
            if (truth.hasNominalBpm && truth.nominalBpm > 0.0)
            {
                double maxDev = 0.0;
                double maxStep = 0.0;
                for (std::size_t i = 0; i < stable.size(); ++i)
                {
                    maxDev = std::max (maxDev, std::fabs (stable[i] - truth.nominalBpm));
                    if (i > 0)
                        maxStep = std::max (maxStep,
                                            std::fabs (stable[i] - stable[i - 1]));
                }
                m.syncopationMaxDeviationBpm = maxDev;
                m.syncopationMaxDeviationFraction = maxDev / truth.nominalBpm;
                m.syncopationMaxStepBpm = maxStep;
                m.syncopationMaxStepFraction = maxStep / truth.nominalBpm;
            }
        }
    }

    // --- ramp local-tempo tracking ----------------------------------------
    if (truth.isRamp() && scored.beatTimesSeconds.size() >= 2)
    {
        double sum = 0.0;
        double worst = 0.0;
        int count = 0;
        for (std::size_t i = 0; i + 1 < scored.beatTimesSeconds.size(); ++i)
        {
            const double gap = scored.beatTimesSeconds[i + 1] - scored.beatTimesSeconds[i];
            if (! (gap > 0.0) || ! isFinite (gap))
                continue;
            const double predBpm = 60.0 / gap;
            const double tMid = 0.5 * (scored.beatTimesSeconds[i]
                                       + scored.beatTimesSeconds[i + 1]);
            const double truthBpm = localTruthBpmAtTime (truth, tMid);
            if (! (truthBpm > 0.0) || ! isFinite (predBpm))
                continue;
            const double err = std::fabs (predBpm - truthBpm) / truthBpm;
            if (! isFinite (err))
                continue;
            sum += err;
            worst = std::max (worst, err);
            ++count;
        }
        if (count > 0)
        {
            m.hasRamp = true;
            m.rampLocalTempoRelErrorMean = sum / static_cast<double> (count);
            m.rampLocalTempoRelErrorWorst = worst;
        }
    }

    return m;
}

// ---------------------------------------------------------------------------
// aggregateFixtures
// ---------------------------------------------------------------------------

AggregateMetrics aggregateFixtures (const std::vector<FixtureMetrics>& perFixture)
{
    AggregateMetrics a;
    a.fixtures = static_cast<int> (perFixture.size());

    std::vector<double> bpmErrors;
    int coreSteady = 0;
    double causalAvailabilityWeighted = 0.0;
    for (const FixtureMetrics& m : perFixture)
    {
        if (m.core) ++a.coreFixtures;
        if (m.steady) ++a.steadyFixtures;
        if (m.ramp) ++a.rampFixtures;

        // Acquisition gate over core fixtures (all of which are steady).
        if (m.core && m.truthBeats > 0)
        {
            ++a.acquisitionCoreEvaluated;
            if (m.acquired && m.acquisitionBars <= 2.0)
                ++a.acquisitionCoreWithin2Bars;
            const double bars = m.acquired ? m.acquisitionBars : 1.0e9;
            a.acquisitionBarsWorstCore = std::max (a.acquisitionBarsWorstCore, bars);
        }

        if (m.steady && m.hasBpmLock)
        {
            bpmErrors.push_back (m.bpmRelativeError);
            if (m.core)
            {
                ++a.bpmRelErrorCoreEvaluated;
                a.bpmRelErrorWorstCore =
                    std::max (a.bpmRelErrorWorstCore, m.bpmRelativeError);
            }
        }

        if (m.detectionMeasured)
        {
            ++a.detectionFixtures;
            a.fMeasureMean += m.fMeasure;
            a.precisionMean += m.precision;
            a.recallMean += m.recall;
        }
        if (m.phaseMeasured)
            ++a.phaseMeasuredFixtures;
        a.cpuSecondsTotal += m.cpuSeconds;
        a.allocationsTotal += m.allocationCount;

        // Half/double is meaningful only where a single true BPM exists.
        if (m.steady && m.hasNominalBpm && m.hasBpmLock)
        {
            ++a.halfDoubleEvaluated;
            if (m.halfDoubleTimeError) ++a.halfDoubleErrors;
            if (m.core)
            {
                ++a.halfDoubleEvaluatedCore;
                if (m.halfDoubleTimeError) ++a.halfDoubleErrorsCore;
            }
        }

        if (m.core && m.steady)
            ++coreSteady;
        if (m.core && m.steady && m.hasBpmLock)
            ++a.bpmCoreLockedFixtures;

        // Coverage of the true-silence diagnostic. A fixture with no true
        // silence is counted as NOT MEASURED, never as a zero-rate pass.
        switch (m.falseBeatCoverage)
        {
            case FalseBeatCoverage::Measured:
                ++a.trueSilenceMeasuredFixtures;
                ++a.trueSilenceInformativeFixtures;
                break;
            case FalseBeatCoverage::NoTrueSilence:
                ++a.trueSilenceNoSilenceFixtures;
                break;
            case FalseBeatCoverage::CorpusDefect:
                ++a.trueSilenceCorpusDefectFixtures;
                break;
            case FalseBeatCoverage::NotAssessedStructuralNoise:
                ++a.trueSilenceStructuralNoiseFixtures;
                break;
        }
        if (m.trueSilenceMeasured)
        {
            ++a.silenceFixtures;
            a.falseBeatsInTrueSilencePerSecondWorst =
                std::max (a.falseBeatsInTrueSilencePerSecondWorst,
                          m.falseBeatsInTrueSilencePerSecond);
            if (m.falseBeatCoverage == FalseBeatCoverage::Measured)
                a.falseBeatsInTrueSilencePerSecondWorstInformative =
                    std::max (a.falseBeatsInTrueSilencePerSecondWorstInformative,
                              m.falseBeatsInTrueSilencePerSecond);
        }

        if (m.silenceAccelerationMeasured)
        {
            ++a.silenceAccelerationEvaluatedFixtures;
            a.maxSilenceTempoIncreaseBpm =
                std::max (a.maxSilenceTempoIncreaseBpm, m.maxSilenceTempoIncreaseBpm);
        }
        if (m.silenceAccelerationInsufficientEvidence)
            ++a.silenceAccelerationInsufficientFixtures;

        a.falseBeatsInUnplayedBeatWindowsPerSecondWorst =
            std::max (a.falseBeatsInUnplayedBeatWindowsPerSecondWorst,
                      m.falseBeatsInUnplayedBeatWindowsPerSecond);
        a.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst =
            std::max (a.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst,
                      m.falseBeatsOffGridInUnplayedBeatWindowsPerSecond);

        if (m.hasRecovery)
        {
            a.hasRecovery = true;
            a.recoverySecondsWorst = std::max (a.recoverySecondsWorst, m.recoverySeconds);
        }

        if (m.hasSyncopation)
        {
            a.hasSyncopation = true;
            ++a.syncopationMeasuredFixtures;
            a.syncopationMaxDeviationFraction =
                std::max (a.syncopationMaxDeviationFraction,
                          m.syncopationMaxDeviationFraction);
            a.syncopationMaxStepFraction =
                std::max (a.syncopationMaxStepFraction, m.syncopationMaxStepFraction);
        }

        if (m.hasRamp)
        {
            a.hasRamp = true;
            ++a.rampFixturesMeasured;
            a.rampLocalTempoRelErrorMean += m.rampLocalTempoRelErrorMean;
            a.rampLocalTempoRelErrorWorst =
                std::max (a.rampLocalTempoRelErrorWorst,
                          m.rampLocalTempoRelErrorWorst);
        }

        if (m.timingDiagnosticsMeasured)
            ++a.timingDiagnosticsFixtures;
        a.beatsReportedByBackend += m.beatsReportedByBackend;
        a.beatsStampAtBlockStart += m.beatsStampAtBlockStart;
        a.beatsRejectedNonCausal += m.beatsRejectedNonCausal;
        a.rateMismatchBlocks += m.rateMismatchBlocks;
        a.causalAvailabilityMaxSeconds =
            std::max (a.causalAvailabilityMaxSeconds, m.causalAvailabilityMaxSeconds);
        if (m.beatsReportedByBackend > 0)
            causalAvailabilityWeighted +=
                m.causalAvailabilityMeanSeconds
                * static_cast<double> (m.beatsReportedByBackend);
    }

    if (a.detectionFixtures > 0)
    {
        const double inv = 1.0 / static_cast<double> (a.detectionFixtures);
        a.fMeasureMean *= inv;
        a.precisionMean *= inv;
        a.recallMean *= inv;
    }

    if (! bpmErrors.empty())
    {
        double sum = 0.0;
        for (const double v : bpmErrors) sum += v;
        a.bpmRelErrorMeanSteady = sum / static_cast<double> (bpmErrors.size());
        a.bpmRelErrorMedianSteady = median (bpmErrors);
    }

    if (a.acquisitionCoreEvaluated > 0)
        a.acquisitionCorePassFraction =
            static_cast<double> (a.acquisitionCoreWithin2Bars)
            / static_cast<double> (a.acquisitionCoreEvaluated);

    if (a.halfDoubleEvaluated > 0)
        a.halfDoubleErrorRate =
            static_cast<double> (a.halfDoubleErrors)
            / static_cast<double> (a.halfDoubleEvaluated);
    if (a.halfDoubleEvaluatedCore > 0)
        a.halfDoubleErrorRateCore =
            static_cast<double> (a.halfDoubleErrorsCore)
            / static_cast<double> (a.halfDoubleEvaluatedCore);

    // Average ramp error over MEASURED ramp fixtures only; dividing by the
    // ramp fixture count when some produced no measurable ramp would bias the
    // mean toward zero.
    if (a.rampFixturesMeasured > 0)
        a.rampLocalTempoRelErrorMean /= static_cast<double> (a.rampFixturesMeasured);

    if (a.beatsReportedByBackend > 0)
        a.causalAvailabilityMeanSeconds =
            causalAvailabilityWeighted / static_cast<double> (a.beatsReportedByBackend);

    // --- SPEC 19 gates -----------------------------------------------------
    a.gateAcquire95Core = a.acquisitionCoreEvaluated > 0
                          && a.acquisitionCorePassFraction >= 0.95;
    // SPEC 19: <= 2%. kBpmAgreementFraction IS the 2% (0.02); the gate must use
    // it directly. An earlier revision multiplied it by 2, making the gate 4%
    // while its own name, comment and constant all said 2% -- a false PASS on any
    // tracker whose worst core error sat in (2%, 4%]. Found by running the real
    // corpus, where BTrack's 2.34% was being reported as a pass.
    //
    // EVAL-004: a missing BPM lock on ANY steady core fixture must not be
    // silently dropped, which would let the gate pass on the residual fixtures.
    // The gate is only EVALUATED when every steady core fixture locked; a
    // partial set reports NOT-MEASURED with the counts (handled in
    // `markdownSummary`). A missing lock cannot be a pass.
    a.bpmCoreAllLocked = coreSteady > 0 && a.bpmCoreLockedFixtures == coreSteady
                         && a.bpmRelErrorCoreEvaluated == coreSteady;
    a.gateBpm2Core = a.bpmCoreAllLocked
                     && a.bpmRelErrorWorstCore <= kBpmAgreementFraction;
    a.gateHalfDouble5Core = a.halfDoubleEvaluatedCore > 0
                            && a.halfDoubleErrorRateCore < 0.05;
    // SPEC 19's "no tempo jump from one isolated syncopated event" and "Follow
    // handles gradual ramps without abrupt audible discontinuities" are NOT
    // gated here: neither is measurable offline (see markdownSummary). The raw
    // numbers (syncopationMaxStepFraction, rampLocalTempoRelError*) are kept.

    return a;
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

namespace
{

std::string csvNumber (double v)
{
    char buf[40];
    std::snprintf (buf, sizeof buf, "%.9g", std::isfinite (v) ? v : 0.0);
    return std::string (buf);
}

} // namespace

rhythmjson::Value fixtureMetricsToJson (const FixtureMetrics& m)
{
    using rhythmjson::Value;
    Value o = Value::makeObject();
    o.set ("name", Value::makeString (m.name));
    o.set ("sourceSha256", Value::makeString (m.sourceSha256));
    o.set ("core", Value::makeBool (m.core));
    o.set ("steady", Value::makeBool (m.steady));
    o.set ("ramp", Value::makeBool (m.ramp));
    o.set ("hasNominalBpm", Value::makeBool (m.hasNominalBpm));
    o.set ("beatToleranceSeconds", Value::makeNumber (m.beatToleranceSeconds));

    o.set ("predictedBeats", Value::makeNumber (m.predictedBeats));
    o.set ("truthBeats", Value::makeNumber (m.truthBeats));
    o.set ("detectionMeasured", Value::makeBool (m.detectionMeasured));
    o.set ("truePositives", Value::makeNumber (m.truePositives));
    o.set ("falsePositives", Value::makeNumber (m.falsePositives));
    o.set ("falseNegatives", Value::makeNumber (m.falseNegatives));
    o.set ("precision", Value::makeNumber (m.precision));
    o.set ("recall", Value::makeNumber (m.recall));
    o.set ("fMeasure", Value::makeNumber (m.fMeasure));

    o.set ("acquired", Value::makeBool (m.acquired));
    o.set ("acquisitionSeconds", Value::makeNumber (m.acquisitionSeconds));
    o.set ("acquisitionBeats", Value::makeNumber (m.acquisitionBeats));
    o.set ("acquisitionBars", Value::makeNumber (m.acquisitionBars));

    o.set ("hasBpmLock", Value::makeBool (m.hasBpmLock));
    o.set ("lockedBpm", Value::makeNumber (m.lockedBpm));
    o.set ("bpmRelativeError", Value::makeNumber (m.bpmRelativeError));

    o.set ("halfTimeLock", Value::makeBool (m.halfTimeLock));
    o.set ("doubleTimeLock", Value::makeBool (m.doubleTimeLock));
    o.set ("halfDoubleTimeError", Value::makeBool (m.halfDoubleTimeError));

    o.set ("falseBeatsInTrueSilence", Value::makeNumber (m.falseBeatsInTrueSilence));
    o.set ("trueSilenceSeconds", Value::makeNumber (m.trueSilenceSeconds));
    o.set ("trueSilenceFractionOfDuration",
           Value::makeNumber (m.trueSilenceFractionOfDuration));
    o.set ("falseBeatsInTrueSilencePerSecond",
           Value::makeNumber (m.falseBeatsInTrueSilencePerSecond));
    o.set ("trueSilenceMeasured", Value::makeBool (m.trueSilenceMeasured));
    o.set ("falseBeatCoverage",
           Value::makeString (rhythmeval::toString (m.falseBeatCoverage)));
    o.set ("falseBeatCoverageCitation",
           Value::makeString (m.falseBeatCoverageCitation));
    o.set ("falseBeatMetricInformative", Value::makeBool (m.falseBeatMetricInformative));
    o.set ("silenceAccelerationMeasured", Value::makeBool (m.silenceAccelerationMeasured));
    o.set ("silenceAccelerationInsufficientEvidence",
           Value::makeBool (m.silenceAccelerationInsufficientEvidence));
    o.set ("silenceSpansEvaluated", Value::makeNumber (m.silenceSpansEvaluated));
    o.set ("silenceSpansInsufficientEvidence",
           Value::makeNumber (m.silenceSpansInsufficientEvidence));
    o.set ("maxSilenceTempoIncreaseBpm",
           Value::makeNumber (m.maxSilenceTempoIncreaseBpm));
    o.set ("falseBeatsInUnplayedBeatWindows",
           Value::makeNumber (m.falseBeatsInUnplayedBeatWindows));
    o.set ("unplayedBeatWindowsSeconds", Value::makeNumber (m.unplayedBeatWindowsSeconds));
    o.set ("falseBeatsInUnplayedBeatWindowsPerSecond",
           Value::makeNumber (m.falseBeatsInUnplayedBeatWindowsPerSecond));
    o.set ("falseBeatsOffGridInUnplayedBeatWindows",
           Value::makeNumber (m.falseBeatsOffGridInUnplayedBeatWindows));
    o.set ("falseBeatsOffGridInUnplayedBeatWindowsPerSecond",
           Value::makeNumber (m.falseBeatsOffGridInUnplayedBeatWindowsPerSecond));

    o.set ("timingDiagnosticsMeasured", Value::makeBool (m.timingDiagnosticsMeasured));
    o.set ("beatsReportedByBackend",
           Value::makeNumber (static_cast<double> (m.beatsReportedByBackend)));
    o.set ("beatsStampAtBlockStart",
           Value::makeNumber (static_cast<double> (m.beatsStampAtBlockStart)));
    o.set ("beatsRejectedNonCausal",
           Value::makeNumber (static_cast<double> (m.beatsRejectedNonCausal)));
    o.set ("rateMismatchBlocks",
           Value::makeNumber (static_cast<double> (m.rateMismatchBlocks)));
    o.set ("causalAvailabilityMeanSeconds",
           Value::makeNumber (m.causalAvailabilityMeanSeconds));
    o.set ("causalAvailabilityMaxSeconds",
           Value::makeNumber (m.causalAvailabilityMaxSeconds));

    o.set ("hasRecovery", Value::makeBool (m.hasRecovery));
    o.set ("recoverySeconds", Value::makeNumber (m.recoverySeconds));
    o.set ("recoveryBeats", Value::makeNumber (m.recoveryBeats));

    o.set ("hasSyncopation", Value::makeBool (m.hasSyncopation));
    o.set ("syncopationTempoStdDevBpm", Value::makeNumber (m.syncopationTempoStdDevBpm));
    o.set ("syncopationTempoCv", Value::makeNumber (m.syncopationTempoCv));
    o.set ("syncopationMaxDeviationBpm", Value::makeNumber (m.syncopationMaxDeviationBpm));
    o.set ("syncopationMaxDeviationFraction", Value::makeNumber (m.syncopationMaxDeviationFraction));
    o.set ("syncopationMaxStepBpm", Value::makeNumber (m.syncopationMaxStepBpm));
    o.set ("syncopationMaxStepFraction", Value::makeNumber (m.syncopationMaxStepFraction));

    o.set ("hasRamp", Value::makeBool (m.hasRamp));
    o.set ("rampLocalTempoRelErrorMean", Value::makeNumber (m.rampLocalTempoRelErrorMean));
    o.set ("rampLocalTempoRelErrorWorst", Value::makeNumber (m.rampLocalTempoRelErrorWorst));

    o.set ("phaseMeasured", Value::makeBool (m.phaseMeasured));
    o.set ("phaseMatchedBeats", Value::makeNumber (m.phaseMatchedBeats));
    o.set ("phaseMeanMs", Value::makeNumber (m.phaseMeanMs));
    o.set ("phaseMeanAbsMs", Value::makeNumber (m.phaseMeanAbsMs));
    o.set ("phaseP50AbsMs", Value::makeNumber (m.phaseP50AbsMs));
    o.set ("phaseP95AbsMs", Value::makeNumber (m.phaseP95AbsMs));
    o.set ("phaseMeanBeats", Value::makeNumber (m.phaseMeanBeats));
    o.set ("phaseMeanAbsBeats", Value::makeNumber (m.phaseMeanAbsBeats));
    o.set ("phaseP95AbsBeats", Value::makeNumber (m.phaseP95AbsBeats));

    o.set ("cpuSeconds", Value::makeNumber (m.cpuSeconds));
    o.set ("allocationCount", Value::makeNumber (static_cast<double> (m.allocationCount)));
    return o;
}

rhythmjson::Value aggregateMetricsToJson (const AggregateMetrics& a)
{
    using rhythmjson::Value;
    Value o = Value::makeObject();
    o.set ("fixtures", Value::makeNumber (a.fixtures));
    o.set ("coreFixtures", Value::makeNumber (a.coreFixtures));
    o.set ("steadyFixtures", Value::makeNumber (a.steadyFixtures));
    o.set ("rampFixtures", Value::makeNumber (a.rampFixtures));

    o.set ("acquisitionCoreEvaluated", Value::makeNumber (a.acquisitionCoreEvaluated));
    o.set ("acquisitionCoreWithin2Bars", Value::makeNumber (a.acquisitionCoreWithin2Bars));
    o.set ("acquisitionCorePassFraction", Value::makeNumber (a.acquisitionCorePassFraction));
    o.set ("acquisitionBarsWorstCore", Value::makeNumber (a.acquisitionBarsWorstCore));

    o.set ("bpmRelErrorCoreEvaluated", Value::makeNumber (a.bpmRelErrorCoreEvaluated));
    o.set ("bpmCoreLockedFixtures", Value::makeNumber (a.bpmCoreLockedFixtures));
    o.set ("bpmCoreAllLocked", Value::makeBool (a.bpmCoreAllLocked));
    o.set ("bpmRelErrorMeanSteady", Value::makeNumber (a.bpmRelErrorMeanSteady));
    o.set ("bpmRelErrorMedianSteady", Value::makeNumber (a.bpmRelErrorMedianSteady));
    o.set ("bpmRelErrorWorstCore", Value::makeNumber (a.bpmRelErrorWorstCore));

    o.set ("detectionFixtures", Value::makeNumber (a.detectionFixtures));
    o.set ("fMeasureMean", Value::makeNumber (a.fMeasureMean));
    o.set ("precisionMean", Value::makeNumber (a.precisionMean));
    o.set ("recallMean", Value::makeNumber (a.recallMean));
    o.set ("phaseMeasuredFixtures", Value::makeNumber (a.phaseMeasuredFixtures));

    o.set ("halfDoubleEvaluated", Value::makeNumber (a.halfDoubleEvaluated));
    o.set ("halfDoubleErrors", Value::makeNumber (a.halfDoubleErrors));
    o.set ("halfDoubleErrorRate", Value::makeNumber (a.halfDoubleErrorRate));
    o.set ("halfDoubleEvaluatedCore", Value::makeNumber (a.halfDoubleEvaluatedCore));
    o.set ("halfDoubleErrorsCore", Value::makeNumber (a.halfDoubleErrorsCore));
    o.set ("halfDoubleErrorRateCore", Value::makeNumber (a.halfDoubleErrorRateCore));

    o.set ("silenceFixtures", Value::makeNumber (a.silenceFixtures));
    o.set ("trueSilenceMeasuredFixtures",
           Value::makeNumber (a.trueSilenceMeasuredFixtures));
    o.set ("trueSilenceNoSilenceFixtures",
           Value::makeNumber (a.trueSilenceNoSilenceFixtures));
    o.set ("trueSilenceCorpusDefectFixtures",
           Value::makeNumber (a.trueSilenceCorpusDefectFixtures));
    o.set ("trueSilenceStructuralNoiseFixtures",
           Value::makeNumber (a.trueSilenceStructuralNoiseFixtures));
    o.set ("trueSilenceInformativeFixtures",
           Value::makeNumber (a.trueSilenceInformativeFixtures));
    o.set ("falseBeatsInTrueSilencePerSecondWorst",
           Value::makeNumber (a.falseBeatsInTrueSilencePerSecondWorst));
    o.set ("falseBeatsInTrueSilencePerSecondWorstInformative",
           Value::makeNumber (a.falseBeatsInTrueSilencePerSecondWorstInformative));
    o.set ("silenceAccelerationEvaluatedFixtures",
           Value::makeNumber (a.silenceAccelerationEvaluatedFixtures));
    o.set ("silenceAccelerationInsufficientFixtures",
           Value::makeNumber (a.silenceAccelerationInsufficientFixtures));
    o.set ("maxSilenceTempoIncreaseBpm",
           Value::makeNumber (a.maxSilenceTempoIncreaseBpm));
    o.set ("falseBeatsInUnplayedBeatWindowsPerSecondWorst",
           Value::makeNumber (a.falseBeatsInUnplayedBeatWindowsPerSecondWorst));
    o.set ("falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst",
           Value::makeNumber (a.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst));

    o.set ("hasRecovery", Value::makeBool (a.hasRecovery));
    o.set ("recoverySecondsWorst", Value::makeNumber (a.recoverySecondsWorst));

    o.set ("syncopationMeasuredFixtures",
           Value::makeNumber (a.syncopationMeasuredFixtures));
    o.set ("hasSyncopation", Value::makeBool (a.hasSyncopation));
    o.set ("syncopationMaxDeviationFraction",
           Value::makeNumber (a.syncopationMaxDeviationFraction));
    o.set ("syncopationMaxStepFraction", Value::makeNumber (a.syncopationMaxStepFraction));

    o.set ("rampFixturesMeasured", Value::makeNumber (a.rampFixturesMeasured));
    o.set ("hasRamp", Value::makeBool (a.hasRamp));
    o.set ("rampLocalTempoRelErrorMean", Value::makeNumber (a.rampLocalTempoRelErrorMean));
    o.set ("rampLocalTempoRelErrorWorst", Value::makeNumber (a.rampLocalTempoRelErrorWorst));

    o.set ("timingDiagnosticsFixtures", Value::makeNumber (a.timingDiagnosticsFixtures));
    o.set ("beatsReportedByBackend",
           Value::makeNumber (static_cast<double> (a.beatsReportedByBackend)));
    o.set ("beatsStampAtBlockStart",
           Value::makeNumber (static_cast<double> (a.beatsStampAtBlockStart)));
    o.set ("beatsRejectedNonCausal",
           Value::makeNumber (static_cast<double> (a.beatsRejectedNonCausal)));
    o.set ("rateMismatchBlocks",
           Value::makeNumber (static_cast<double> (a.rateMismatchBlocks)));
    o.set ("causalAvailabilityMeanSeconds",
           Value::makeNumber (a.causalAvailabilityMeanSeconds));
    o.set ("causalAvailabilityMaxSeconds",
           Value::makeNumber (a.causalAvailabilityMaxSeconds));

    o.set ("cpuSecondsTotal", Value::makeNumber (a.cpuSecondsTotal));
    o.set ("allocationsTotal", Value::makeNumber (static_cast<double> (a.allocationsTotal)));

    Value gates = Value::makeObject();
    gates.set ("gateAcquire95Core", Value::makeBool (a.gateAcquire95Core));
    gates.set ("gateBpm2Core", Value::makeBool (a.gateBpm2Core));
    gates.set ("bpm2CoreEvaluated", Value::makeBool (a.bpmCoreAllLocked));
    gates.set ("gateHalfDouble5Core", Value::makeBool (a.gateHalfDouble5Core));
    o.set ("spec19Gates", gates);
    return o;
}

rhythmjson::Value scoringVariantToJson (const ScoringVariant& variant)
{
    using rhythmjson::Value;
    Value o = Value::makeObject();
    o.set ("label", Value::makeString (variant.label));
    o.set ("latencyCompensationSeconds",
           Value::makeNumber (variant.latencyCompensationSeconds));
    Value fixtureArray = Value::makeArray();
    for (const FixtureMetrics& m : variant.fixtures)
        fixtureArray.push (fixtureMetricsToJson (m));
    o.set ("fixtures", fixtureArray);
    o.set ("aggregate", aggregateMetricsToJson (variant.aggregate));
    return o;
}

const char* fixtureMetricsCsvHeader()
{
    return "name,sourceSha256,core,steady,ramp,detectionMeasured,predictedBeats,truthBeats,truePositives,"
           "precision,recall,"
           "fMeasure,acquired,acquisitionBars,bpmRelativeError,halfDoubleTimeError,"
           "falseBeatsInTrueSilence,falseBeatsInTrueSilencePerSecond,trueSilenceSeconds,"
           "trueSilenceMeasured,"
           "falseBeatCoverage,falseBeatMetricInformative,"
           "falseBeatsInUnplayedBeatWindows,falseBeatsInUnplayedBeatWindowsPerSecond,"
           "falseBeatsOffGridInUnplayedBeatWindowsPerSecond,recoverySeconds,"
           "syncopationMaxDeviationFraction,rampLocalTempoRelErrorMean,phaseMeasured,"
           "phaseP95AbsMs,beatsReportedByBackend,beatsStampAtBlockStart,"
           "beatsRejectedNonCausal,causalAvailabilityMeanSeconds,"
           "causalAvailabilityMaxSeconds,cpuSeconds,allocationCount\n";
}

std::string fixtureMetricsCsvRow (const FixtureMetrics& m)
{
    std::string row;
    row += m.name;
    row += ',';
    row += m.sourceSha256;
    row += ',';
    row += m.core ? '1' : '0';
    row += ',';
    row += m.steady ? '1' : '0';
    row += ',';
    row += m.ramp ? '1' : '0';
    row += ',';
    row += m.detectionMeasured ? '1' : '0';
    row += ',';
    row += std::to_string (m.predictedBeats);
    row += ',';
    row += std::to_string (m.truthBeats);
    row += ',';
    row += std::to_string (m.truePositives);
    row += ',';
    row += csvNumber (m.precision);
    row += ',';
    row += csvNumber (m.recall);
    row += ',';
    row += csvNumber (m.fMeasure);
    row += ',';
    row += m.acquired ? '1' : '0';
    row += ',';
    row += csvNumber (m.acquisitionBars);
    row += ',';
    row += csvNumber (m.bpmRelativeError);
    row += ',';
    row += m.halfDoubleTimeError ? '1' : '0';
    row += ',';
    row += std::to_string (m.falseBeatsInTrueSilence);
    row += ',';
    row += csvNumber (m.falseBeatsInTrueSilencePerSecond);
    row += ',';
    row += csvNumber (m.trueSilenceSeconds);
    row += ',';
    row += m.trueSilenceMeasured ? '1' : '0';
    row += ',';
    row += rhythmeval::toString (m.falseBeatCoverage);
    row += ',';
    row += m.falseBeatMetricInformative ? '1' : '0';
    row += ',';
    row += std::to_string (m.falseBeatsInUnplayedBeatWindows);
    row += ',';
    row += csvNumber (m.falseBeatsInUnplayedBeatWindowsPerSecond);
    row += ',';
    row += csvNumber (m.falseBeatsOffGridInUnplayedBeatWindowsPerSecond);
    row += ',';
    row += csvNumber (m.recoverySeconds);
    row += ',';
    row += csvNumber (m.syncopationMaxDeviationFraction);
    row += ',';
    row += csvNumber (m.rampLocalTempoRelErrorMean);
    row += ',';
    row += m.phaseMeasured ? '1' : '0';
    row += ',';
    row += csvNumber (m.phaseP95AbsMs);
    row += ',';
    row += std::to_string (m.beatsReportedByBackend);
    row += ',';
    row += std::to_string (m.beatsStampAtBlockStart);
    row += ',';
    row += std::to_string (m.beatsRejectedNonCausal);
    row += ',';
    row += csvNumber (m.causalAvailabilityMeanSeconds);
    row += ',';
    row += csvNumber (m.causalAvailabilityMaxSeconds);
    row += ',';
    row += csvNumber (m.cpuSeconds);
    row += ',';
    row += std::to_string (m.allocationCount);
    row += '\n';
    return row;
}

const char* fixtureMetricsCsvHeaderWithVariant()
{
    static const std::string header =
        std::string ("variant,compensationSeconds,") + fixtureMetricsCsvHeader();
    return header.c_str();
}

std::string fixtureMetricsCsvRowWithVariant (const ScoringVariant& variant,
                                             const FixtureMetrics& m)
{
    std::string row = variant.label;
    row += ',';
    row += csvNumber (variant.latencyCompensationSeconds);
    row += ',';
    row += fixtureMetricsCsvRow (m);
    return row;
}

namespace
{

/** Mean signed / absolute / p95 phase over the fixtures that produced matched
    beats. Used by the latency-effect table. */
void meanPhase (const ScoringVariant& v,
                double& signedMeanMs, double& absMeanMs, double& p95MeanMs)
{
    double s = 0.0;
    double a = 0.0;
    double p = 0.0;
    int n = 0;
    for (const FixtureMetrics& m : v.fixtures)
    {
        if (m.truePositives <= 0)
            continue;
        s += m.phaseMeanMs;
        a += m.phaseMeanAbsMs;
        p += m.phaseP95AbsMs;
        ++n;
    }
    const double inv = n > 0 ? 1.0 / static_cast<double> (n) : 0.0;
    signedMeanMs = s * inv;
    absMeanMs = a * inv;
    p95MeanMs = p * inv;
}

const char* gateStatus (bool evaluated, bool pass)
{
    if (! evaluated)
        return "NOT-MEASURED";
    return pass ? "PASS" : "FAIL";
}

} // namespace

std::string markdownSummary (const std::string& backendId,
                             const std::string& corpusId,
                             double toleranceSeconds,
                             const std::vector<ScoringVariant>& variants)
{
    char buf[640];
    std::string md;
    md += "# Rhythm evaluation summary\n\n";
    md += "- backend: `" + backendId + "`\n";
    md += "- corpus: `" + corpusId + "`\n";
    std::snprintf (buf, sizeof buf, "- beat-match tolerance: %.0f ms\n",
                   toleranceSeconds * 1000.0);
    md += buf;
    md += "- variants in this run: ";
    for (std::size_t i = 0; i < variants.size(); ++i)
    {
        if (i != 0)
            md += ", ";
        std::snprintf (buf, sizeof buf, "`%s` (%.2f ms)",
                       variants[i].label.c_str(),
                       variants[i].latencyCompensationSeconds * 1000.0);
        md += buf;
    }
    md += "\n";
    if (! variants.empty())
    {
        std::snprintf (buf, sizeof buf,
                       "- fixtures per variant: %d (%d core, %d steady, %d ramp)\n",
                       variants.front().aggregate.fixtures,
                       variants.front().aggregate.coreFixtures,
                       variants.front().aggregate.steadyFixtures,
                       variants.front().aggregate.rampFixtures);
        md += buf;
    }
    md += "\n";

    // --- per-variant per-fixture tables ------------------------------------
    for (const ScoringVariant& v : variants)
    {
        std::snprintf (buf, sizeof buf, "## Variant `%s`", v.label.c_str());
        md += buf;
        if (v.latencyCompensationSeconds != 0.0)
        {
            std::snprintf (buf, sizeof buf, " — latency compensation %.2f ms",
                           v.latencyCompensationSeconds * 1000.0);
            md += buf;
        }
        else
        {
            md += " — uncompensated";
        }
        md += "\n\n";
        md += "\n**Per-fixture evidence.** `coverage` is the true-silence diagnostic's "
              "coverage (Measured / NoTrueSilence / CorpusDefect / "
              "NotAssessedStructuralNoise); `sil accel` is the "
              "silence tempo-increase diagnostic (blank when not measured); `causal lat` "
              "is the mean availability-minus-event delay of backend-reported beats. "
              "`p95 phase` is `n/a` when no predicted beat matched (a zero there is "
              "undefined, not perfect).\n\n";
        md += "| fixture | core | coverage | pred | TP | P | R | F | acq bars | BPM err | "
              "h/d | trueSil F/s | sil accel bpm | unplayed F/s | off-grid F/s | recovery s | "
              "sync dev | ramp err | p95 phase ms | causal lat ms |\n";
        md += "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
        for (const FixtureMetrics& m : v.fixtures)
        {
            char acqBuf[32];
            char silAccelBuf[32];
            char phaseBuf[32];
            if (m.acquired)
                std::snprintf (acqBuf, sizeof acqBuf, "%.2f", m.acquisitionBars);
            else
                std::snprintf (acqBuf, sizeof acqBuf, "-");
            if (m.silenceAccelerationMeasured)
                std::snprintf (silAccelBuf, sizeof silAccelBuf, "%+.2f",
                               m.maxSilenceTempoIncreaseBpm);
            else
                std::snprintf (silAccelBuf, sizeof silAccelBuf, "-");
            if (m.phaseMeasured)
                std::snprintf (phaseBuf, sizeof phaseBuf, "%.2f", m.phaseP95AbsMs);
            else
                std::snprintf (phaseBuf, sizeof phaseBuf, "n/a");
            std::snprintf (buf, sizeof buf,
                           "| %s | %s | %s | %d | %d | %.3f | %.3f | %.3f | %s | %.3f | "
                           "%s | %.3f | %s | %.3f | %.3f | %.3f | %.4f | %.4f | %s | %.2f |\n",
                           m.name.c_str(),
                           m.core ? "yes" : "",
                           rhythmeval::toString (m.falseBeatCoverage),
                           m.predictedBeats,
                           m.truePositives,
                           m.precision,
                           m.recall,
                           m.fMeasure,
                           acqBuf,
                           m.bpmRelativeError,
                           m.halfDoubleTimeError ? "ERR" : "-",
                           m.falseBeatsInTrueSilencePerSecond,
                           silAccelBuf,
                           m.falseBeatsInUnplayedBeatWindowsPerSecond,
                           m.falseBeatsOffGridInUnplayedBeatWindowsPerSecond,
                           m.recoverySeconds,
                           m.syncopationMaxDeviationFraction,
                           m.rampLocalTempoRelErrorMean,
                           phaseBuf,
                           m.causalAvailabilityMeanSeconds * 1000.0);
            md += buf;
        }
        md += "\n";
    }

    // --- aggregates --------------------------------------------------------
    md += "## Aggregates\n\n";
    for (const ScoringVariant& v : variants)
    {
        const AggregateMetrics& a = v.aggregate;
        std::snprintf (buf, sizeof buf, "### `%s`\n\n", v.label.c_str());
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- F-measure mean %.4f (precision %.4f, recall %.4f) over %d fixtures "
                       "with a ground-truth grid; phase measured on %d fixture(s)\n",
                       a.fMeasureMean, a.precisionMean, a.recallMean,
                       a.detectionFixtures, a.phaseMeasuredFixtures);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- BPM relative error (steady): mean %.4f, median %.4f; core worst %.4f "
                       "over %d/%d core fixtures locked\n",
                       a.bpmRelErrorMeanSteady, a.bpmRelErrorMedianSteady,
                       a.bpmRelErrorWorstCore, a.bpmCoreLockedFixtures, a.coreFixtures);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- half/double-time error rate: %.4f (%d/%d), core %.4f (%d/%d)\n",
                       a.halfDoubleErrorRate, a.halfDoubleErrors,
                       a.halfDoubleEvaluated, a.halfDoubleErrorRateCore,
                       a.halfDoubleErrorsCore, a.halfDoubleEvaluatedCore);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- acquisition within 2 bars (core): %d/%d = %.4f\n",
                       a.acquisitionCoreWithin2Bars, a.acquisitionCoreEvaluated,
                       a.acquisitionCorePassFraction);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- events in TRUE silence (diagnostic, not a gate): worst %.4f/s over %d "
                       "fixtures; measured-coverage worst %.4f/s over %d fixture(s); "
                       "coverage: %d measured, %d no-silence, %d corpus-defect, "
                       "%d structural-noise\n",
                       a.falseBeatsInTrueSilencePerSecondWorst, a.silenceFixtures,
                       a.falseBeatsInTrueSilencePerSecondWorstInformative,
                       a.trueSilenceMeasuredFixtures,
                       a.trueSilenceMeasuredFixtures, a.trueSilenceNoSilenceFixtures,
                       a.trueSilenceCorpusDefectFixtures,
                       a.trueSilenceStructuralNoiseFixtures);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- silence tempo-increase diagnostic: max %+.4f BPM over %d evaluated "
                       "fixture(s); insufficient evidence on %d fixture(s)\n",
                       a.maxSilenceTempoIncreaseBpm,
                       a.silenceAccelerationEvaluatedFixtures,
                       a.silenceAccelerationInsufficientFixtures);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- events in UNPLAYED-BEAT windows (separate metric): worst %.4f/s, "
                       "off-grid worst %.4f/s\n",
                       a.falseBeatsInUnplayedBeatWindowsPerSecondWorst,
                       a.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- ramp local-tempo relative error (raw, not a gate): mean %.4f, worst %.4f "
                       "over %d measured ramp fixture(s)\n",
                       a.rampLocalTempoRelErrorMean, a.rampLocalTempoRelErrorWorst,
                       a.rampFixturesMeasured);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- syncopation max deviation %.4f, max step %.4f (raw, not a gate; "
                       "%d fixture(s) measured)\n",
                       a.syncopationMaxDeviationFraction, a.syncopationMaxStepFraction,
                       a.syncopationMeasuredFixtures);
        md += buf;
        std::snprintf (buf, sizeof buf,
                       "- backend beat-timestamp mapping: %llu reported, %llu at block start, "
                       "%llu non-causal fallbacks, %llu rate-mismatch blocks; "
                       "causal availability delay mean %.2f ms, worst %.2f ms\n",
                       static_cast<unsigned long long> (a.beatsReportedByBackend),
                       static_cast<unsigned long long> (a.beatsStampAtBlockStart),
                       static_cast<unsigned long long> (a.beatsRejectedNonCausal),
                       static_cast<unsigned long long> (a.rateMismatchBlocks),
                       a.causalAvailabilityMeanSeconds * 1000.0,
                       a.causalAvailabilityMaxSeconds * 1000.0);
        md += buf;
        std::snprintf (buf, sizeof buf, "- CPU %.4f s total, %llu C++ new/delete allocations total\n\n",
                       a.cpuSecondsTotal,
                       static_cast<unsigned long long> (a.allocationsTotal));
        md += buf;
    }

    // --- latency compensation effect ---------------------------------------
    md += "## Latency compensation effect\n\n";
    if (variants.size() < 2)
    {
        md += "No compensation was requested (`--compensate-latency` absent or 0). "
              "The table below is therefore empty by design; re-run with a non-zero "
              "value to measure the effect.\n\n";
    }
    else
    {
        const ScoringVariant& base = variants.front();
        double baseSigned = 0.0, baseAbs = 0.0, baseP95 = 0.0;
        meanPhase (base, baseSigned, baseAbs, baseP95);
        md += "Positive mean phase means the predicted beats are LATE. Compensation subtracts "
              "the requested seconds from every predicted beat before scoring, so a correctly "
              "compensated backend moves the mean phase toward zero. Delta is "
              "`compensated - uncompensated`.\n\n";
        md += "| metric | uncompensated | compensated | delta |\n";
        md += "|---|---|---|---|\n";
        for (std::size_t i = 1; i < variants.size(); ++i)
        {
            const ScoringVariant& c = variants[i];
            double signedM = 0.0, absM = 0.0, p95M = 0.0;
            meanPhase (c, signedM, absM, p95M);
            std::snprintf (buf, sizeof buf, "| `%s` | | | |\n", c.label.c_str());
            md += buf;
            md += "| F-measure mean | ";
            std::snprintf (buf, sizeof buf, "%.4f | %.4f | %+.4f |\n",
                           base.aggregate.fMeasureMean, c.aggregate.fMeasureMean,
                           c.aggregate.fMeasureMean - base.aggregate.fMeasureMean);
            md += buf;
            md += "| precision mean | ";
            std::snprintf (buf, sizeof buf, "%.4f | %.4f | %+.4f |\n",
                           base.aggregate.precisionMean, c.aggregate.precisionMean,
                           c.aggregate.precisionMean - base.aggregate.precisionMean);
            md += buf;
            md += "| recall mean | ";
            std::snprintf (buf, sizeof buf, "%.4f | %.4f | %+.4f |\n",
                           base.aggregate.recallMean, c.aggregate.recallMean,
                           c.aggregate.recallMean - base.aggregate.recallMean);
            md += buf;
            md += "| mean signed phase (ms) | ";
            std::snprintf (buf, sizeof buf, "%.3f | %.3f | %+.3f |\n",
                           baseSigned, signedM, signedM - baseSigned);
            md += buf;
            md += "| mean |phase| (ms) | ";
            std::snprintf (buf, sizeof buf, "%.3f | %.3f | %+.3f |\n",
                           baseAbs, absM, absM - baseAbs);
            md += buf;
            md += "| mean p95 |phase| (ms) | ";
            std::snprintf (buf, sizeof buf, "%.3f | %.3f | %+.3f |\n",
                           baseP95, p95M, p95M - baseP95);
            md += buf;
            md += "| acquisition within 2 bars (core fraction) | ";
            std::snprintf (buf, sizeof buf, "%.4f | %.4f | %+.4f |\n",
                           base.aggregate.acquisitionCorePassFraction,
                           c.aggregate.acquisitionCorePassFraction,
                           c.aggregate.acquisitionCorePassFraction
                               - base.aggregate.acquisitionCorePassFraction);
            md += buf;
            md += "| BPM rel error core worst | ";
            std::snprintf (buf, sizeof buf, "%.4f | %.4f | %+.4f |\n",
                           base.aggregate.bpmRelErrorWorstCore,
                           c.aggregate.bpmRelErrorWorstCore,
                           c.aggregate.bpmRelErrorWorstCore
                               - base.aggregate.bpmRelErrorWorstCore);
            md += buf;
            md += "| false beats/s true silence (worst measured coverage; diagnostic) | ";
            std::snprintf (buf, sizeof buf, "%.4f | %.4f | %+.4f |\n",
                           base.aggregate.falseBeatsInTrueSilencePerSecondWorstInformative,
                           c.aggregate.falseBeatsInTrueSilencePerSecondWorstInformative,
                           c.aggregate.falseBeatsInTrueSilencePerSecondWorstInformative
                               - base.aggregate.falseBeatsInTrueSilencePerSecondWorstInformative);
            md += buf;
            md += "| false beats/s unplayed windows (worst, off-grid) | ";
            std::snprintf (buf, sizeof buf, "%.4f | %.4f | %+.4f |\n",
                           base.aggregate.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst,
                           c.aggregate.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst,
                           c.aggregate.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst
                               - base.aggregate.falseBeatsOffGridInUnplayedBeatWindowsPerSecondWorst);
            md += buf;
        }
        md += "\n";
    }
    md += "**The orchestrator, not the harness, decides whether to compensate.** "
          "Compensation is a claim about the backend, and it can be as wrong as no "
          "compensation: subtracting a latency the backend does not actually have is "
          "just a bias in the other direction, and for a non-causal or tempo-adaptive "
          "backend the delay may not even be constant. This table exists so the ADR can "
          "see the size of the decision, not so the harness can make it.\n\n";

    // --- SPEC 19 gate table, per variant -----------------------------------
    md += "## SPEC 19 gates\n\n";
    for (const ScoringVariant& v : variants)
    {
        const AggregateMetrics& a = v.aggregate;
        std::snprintf (buf, sizeof buf, "### `%s`\n\n", v.label.c_str());
        md += buf;
        md += "| gate | result | reason |\n|---|---|---|\n";
        auto row = [&md] (const char* name, const char* status, const std::string& reason)
        {
            md += "| ";
            md += name;
            md += " | ";
            md += status;
            md += " | ";
            md += reason;
            md += " |\n";
        };

        {
            std::snprintf (buf, sizeof buf,
                           "%d/%d core fixtures acquired within 2 bars (fraction %.4f)",
                           a.acquisitionCoreWithin2Bars, a.acquisitionCoreEvaluated,
                           a.acquisitionCorePassFraction);
            row ("acquire useful lock within 2 bars for >= 95% of core fixtures",
                 gateStatus (a.acquisitionCoreEvaluated > 0, a.gateAcquire95Core), buf);
        }
        {
            std::snprintf (buf, sizeof buf,
                           "worst core BPM relative error %.4f; %d/%d core fixtures locked",
                           a.bpmRelErrorWorstCore, a.bpmCoreLockedFixtures,
                           a.coreFixtures);
            row ("locked BPM relative error <= 2% on steady-tempo core fixtures",
                 gateStatus (a.bpmCoreAllLocked, a.gateBpm2Core), buf);
        }
        {
            std::snprintf (buf, sizeof buf, "core half/double-time errors %d/%d (rate %.4f)",
                           a.halfDoubleErrorsCore, a.halfDoubleEvaluatedCore,
                           a.halfDoubleErrorRateCore);
            row ("half/double-time errors < 5% on core fixtures",
                 gateStatus (a.halfDoubleEvaluatedCore > 0, a.gateHalfDouble5Core), buf);
        }
        {
            std::snprintf (buf, sizeof buf,
                           "raw diagnostic: largest first difference of reported BPM on "
                           "`syncopated_funk` is %.4f of nominal over %d measured fixture(s). "
                           "The fixture contains many syncopated events, so it cannot isolate "
                           "ONE; no numeric threshold is claimed.",
                           a.syncopationMaxStepFraction, a.syncopationMeasuredFixtures);
            row ("no tempo jump from one isolated syncopated event",
                 "NOT-MEASURED", buf);
        }
        {
            std::snprintf (buf, sizeof buf,
                           "raw diagnostic: %d/%d fixtures declare true silence (%d measured, "
                           "%d corpus-defect, %d structural-noise); worst %.4f events/s, worst "
                           "measured-coverage %.4f/s; silence tempo-increase max %+.4f BPM over "
                           "%d evaluated fixture(s), %d insufficient. SPEC forbids false "
                           "ACCELERATION, not beat events during a short intentional holdover; "
                           "the offline harness cannot attribute a tempo change to the silence "
                           "rather than to legitimate tempo follow, so no gate is claimed.",
                           a.silenceFixtures, a.fixtures,
                           a.trueSilenceMeasuredFixtures,
                           a.trueSilenceCorpusDefectFixtures,
                           a.trueSilenceStructuralNoiseFixtures,
                           a.falseBeatsInTrueSilencePerSecondWorst,
                           a.falseBeatsInTrueSilencePerSecondWorstInformative,
                           a.maxSilenceTempoIncreaseBpm,
                           a.silenceAccelerationEvaluatedFixtures,
                           a.silenceAccelerationInsufficientFixtures);
            row ("silence does not create false acceleration",
                 "NOT-MEASURED", buf);
        }
        row ("explicit resync establishes new phase within the requested boundary",
             "NOT-MEASURED",
             "the offline harness has no resync command path; the Musical Clock is the only place this can be tested");
        {
            std::snprintf (buf, sizeof buf,
                           "raw diagnostic: mean local-tempo relative error %.4f, worst %.4f "
                           "over %d measured ramp fixture(s). The SPEC target is 'without "
                           "abrupt audible discontinuities', which requires the Musical Clock "
                           "and audition; a 2 %% backend error threshold would be fabricated "
                           "(and is not the SPEC rule).",
                           a.rampLocalTempoRelErrorMean, a.rampLocalTempoRelErrorWorst,
                           a.rampFixturesMeasured);
            row ("Follow handles controlled gradual tempo ramps without abrupt audible discontinuities",
                 "NOT-MEASURED", buf);
        }
        row ("Loose Follow is measurably less reactive than Follow",
             "NOT-MEASURED",
             "requires the Musical Clock and a real controller; no clock runs in this offline harness");
        {
            std::snprintf (buf, sizeof buf, "offline proxy: worst post-stop lock recovery %.4f s; "
                                             "audio-device restart is not observable offline",
                           a.recoverySecondsWorst);
            row ("stop/start recovery succeeds without restarting the audio device",
                 "NOT-INFORMATIVE", buf);
        }
        md += "\n";
    }

    // --- not-informative fixtures ------------------------------------------
    md += "## Fixtures not informative for the true-silence false-beat metric\n\n";
    bool anyNotInformative = false;
    for (const ScoringVariant& v : variants)
    {
        for (const FixtureMetrics& m : v.fixtures)
        {
            if (m.falseBeatMetricInformative)
                continue;
            anyNotInformative = true;
            if (m.falseBeatCoverage == FalseBeatCoverage::CorpusDefect)
            {
                std::snprintf (buf, sizeof buf,
                               "- `%s` (`%s`, sha256 `%.12s…`): CORPUS DEFECT — %.2f s "
                               "(%.0f%%) of declared true silence is an artifact of the "
                               "fast-decay recording, not a performance. Its rate must not be "
                               "read as a pass or a fail. Citation: %s\n",
                               m.name.c_str(), v.label.c_str(),
                               m.sourceSha256.c_str(),
                               m.trueSilenceSeconds,
                               100.0 * m.trueSilenceFractionOfDuration,
                               m.falseBeatCoverageCitation.c_str());
            }
            else if (m.falseBeatCoverage
                     == FalseBeatCoverage::NotAssessedStructuralNoise)
            {
                std::snprintf (buf, sizeof buf,
                               "- `%s` (`%s`): NOT ASSESSED (STRUCTURAL NOISE) — the declared "
                               "true-silence spans are inherited by a derived noise transform "
                               "whose added floor fills what used to be quiet; they are not a "
                               "re-measured acoustic stop. The raw count is reported but the "
                               "rate is not a measured pass or fail.\n",
                               m.name.c_str(), v.label.c_str());
            }
            else
            {
                std::snprintf (buf, sizeof buf,
                               "- `%s` (`%s`): NO TRUE SILENCE — the metric is NOT MEASURED for "
                               "this fixture; a rate of 0 would be absent data, not a pass.\n",
                               m.name.c_str(), v.label.c_str());
            }
            md += buf;
        }
    }
    if (! anyNotInformative)
    {
        md += "None: every fixture has real true silence to measure against.\n";
    }
    md += "\nCriterion (stated, not hidden): a fixture is not informative when (a) it has "
          "no true-silence spans at all (`NoTrueSilence`), (b) it is a derived noise "
          "perturbation whose inherited spans are structural (`NotAssessedStructuralNoise`), "
          "or (c) its declared WAV sha256 is in the reviewed fast-decay defect registry "
          "(`CorpusDefect`, EVAL-006 `docs/research/SUSTAIN-REPAIR.md`). Classification is by "
          "WAV identity, not fixture name: the repaired `sustained_chords` render with hash "
          "`23b8cf21…` measures its own independently derived spans, while the original "
          "`e4b9297f…` bytes remain a defect even if renamed. The hash is the declared "
          "manifest hash, not an authentication of the PCM. No numeric silence-occupancy "
          "threshold is used; this replaces EVAL-002R's '>= 50 % silence' censoring rule, "
          "which also censored genuine sparse playing (`sparse_single_notes` sits at 48.55 %).\n";
    return md;
}

} // namespace rhythmeval
