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

double silenceTotalSeconds (const RhythmTruth& truth)
{
    double total = 0.0;
    for (const SilenceSpan& s : truth.silenceSpans)
        total += std::max (0.0, s.endSeconds - s.startSeconds);
    return total;
}

bool inAnySilenceSpan (const RhythmTruth& truth, double t)
{
    for (const SilenceSpan& s : truth.silenceSpans)
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
                             double beatToleranceSeconds)
{
    FixtureMetrics m;
    m.name = truth.name;
    m.core = truth.isCore();
    m.steady = truth.isSteady();
    m.ramp = truth.isRamp();
    m.beatToleranceSeconds = beatToleranceSeconds;
    m.hasNominalBpm = truth.hasNominalBpm;
    m.truthBeats = static_cast<int> (truth.beats.size());
    m.predictedBeats = static_cast<int> (obs.beatTimesSeconds.size());
    m.cpuSeconds = obs.cpuSeconds;
    m.allocationCount = obs.allocationCount;

    // --- matching, precision/recall/F, phase error -------------------------
    std::vector<int> predMatch;
    m.truePositives = matchBeats (obs.beatTimesSeconds, truth.beats,
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
        for (std::size_t i = 0; i < obs.beatTimesSeconds.size(); ++i)
        {
            if (predMatch[i] < 0)
                continue;
            const int near = nearestTruthIndex (truth.beats, obs.beatTimesSeconds[i]);
            if (near < 0)
                continue;
            const double signedSeconds =
                obs.beatTimesSeconds[i] - truth.beats[static_cast<std::size_t> (near)];
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
    }

    // --- acquisition -------------------------------------------------------
    {
        std::size_t lockPred = 0;
        std::size_t lockTruth = 0;
        if (findFirstLockFrom (truth, obs, beatToleranceSeconds,
                               -std::numeric_limits<double>::infinity(),
                               lockPred, lockTruth))
        {
            m.acquired = true;
            const double lockTime = obs.beatTimesSeconds[lockPred];
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

    // --- false beats in silence -------------------------------------------
    {
        m.silenceSeconds = silenceTotalSeconds (truth);
        for (const double t : obs.beatTimesSeconds)
        {
            if (! inAnySilenceSpan (truth, t))
                continue;
            ++m.falseBeatsInSilence;
            if (! nearAnyTruthBeat (truth.beats, t, kSilentOnsetWindowSeconds))
                ++m.falseBeatsOffGridInSilence;
        }
        if (m.silenceSeconds > 0.0)
        {
            m.falseBeatsInSilencePerSecond =
                static_cast<double> (m.falseBeatsInSilence) / m.silenceSeconds;
            m.falseBeatsOffGridInSilencePerSecond =
                static_cast<double> (m.falseBeatsOffGridInSilence) / m.silenceSeconds;
        }
    }

    // --- recovery after stop/start ----------------------------------------
    if (truth.name == "stop_start" && ! truth.silenceSpans.empty())
    {
        double silenceEnd = 0.0;
        for (const SilenceSpan& s : truth.silenceSpans)
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
        const bool lockedAfter = findFirstLockFrom (truth, obs, beatToleranceSeconds,
                                                    evidenceResume, lockPred, lockTruth);
        if (lockedAfter)
        {
            const double lockTime = obs.beatTimesSeconds[lockPred];
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
    if (truth.isRamp() && obs.beatTimesSeconds.size() >= 2)
    {
        double sum = 0.0;
        double worst = 0.0;
        int count = 0;
        for (std::size_t i = 0; i + 1 < obs.beatTimesSeconds.size(); ++i)
        {
            const double gap = obs.beatTimesSeconds[i + 1] - obs.beatTimesSeconds[i];
            if (! (gap > 0.0) || ! isFinite (gap))
                continue;
            const double predBpm = 60.0 / gap;
            const double tMid = 0.5 * (obs.beatTimesSeconds[i]
                                       + obs.beatTimesSeconds[i + 1]);
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

        a.fMeasureMean += m.fMeasure;
        a.precisionMean += m.precision;
        a.recallMean += m.recall;
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

        if (m.silenceSeconds > 0.0)
        {
            ++a.silenceFixtures;
            a.falseBeatsInSilencePerSecondWorst =
                std::max (a.falseBeatsInSilencePerSecondWorst,
                          m.falseBeatsInSilencePerSecond);
        }

        if (m.hasRecovery)
        {
            a.hasRecovery = true;
            a.recoverySecondsWorst = std::max (a.recoverySecondsWorst, m.recoverySeconds);
        }

        if (m.hasSyncopation)
        {
            a.hasSyncopation = true;
            a.syncopationMaxDeviationFraction =
                std::max (a.syncopationMaxDeviationFraction,
                          m.syncopationMaxDeviationFraction);
            a.syncopationMaxStepFraction =
                std::max (a.syncopationMaxStepFraction, m.syncopationMaxStepFraction);
        }

        if (m.hasRamp)
        {
            a.hasRamp = true;
            a.rampLocalTempoRelErrorMean += m.rampLocalTempoRelErrorMean;
            a.rampLocalTempoRelErrorWorst =
                std::max (a.rampLocalTempoRelErrorWorst,
                          m.rampLocalTempoRelErrorWorst);
        }
    }

    if (a.fixtures > 0)
    {
        const double inv = 1.0 / static_cast<double> (a.fixtures);
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

    if (a.rampFixtures > 0)
        a.rampLocalTempoRelErrorMean /= static_cast<double> (a.rampFixtures);

    // --- SPEC 19 gates -----------------------------------------------------
    a.gateAcquire95Core = a.acquisitionCoreEvaluated > 0
                          && a.acquisitionCorePassFraction >= 0.95;
    a.gateBpm2Core = a.bpmRelErrorCoreEvaluated > 0
                     && a.bpmRelErrorWorstCore <= 2.0 * kBpmAgreementFraction;
    a.gateHalfDouble5Core = a.halfDoubleEvaluatedCore > 0
                            && a.halfDoubleErrorRateCore < 0.05;
    a.gateSyncopationNoJump =
        ! a.hasSyncopation || a.syncopationMaxStepFraction <= kSyncopationJumpFraction;
    a.gateRampFollows =
        ! a.hasRamp || a.rampLocalTempoRelErrorMean <= kBpmAgreementFraction;

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
    o.set ("core", Value::makeBool (m.core));
    o.set ("steady", Value::makeBool (m.steady));
    o.set ("ramp", Value::makeBool (m.ramp));
    o.set ("hasNominalBpm", Value::makeBool (m.hasNominalBpm));
    o.set ("beatToleranceSeconds", Value::makeNumber (m.beatToleranceSeconds));

    o.set ("predictedBeats", Value::makeNumber (m.predictedBeats));
    o.set ("truthBeats", Value::makeNumber (m.truthBeats));
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

    o.set ("falseBeatsInSilence", Value::makeNumber (m.falseBeatsInSilence));
    o.set ("silenceSeconds", Value::makeNumber (m.silenceSeconds));
    o.set ("falseBeatsInSilencePerSecond", Value::makeNumber (m.falseBeatsInSilencePerSecond));
    o.set ("falseBeatsOffGridInSilence", Value::makeNumber (m.falseBeatsOffGridInSilence));
    o.set ("falseBeatsOffGridInSilencePerSecond",
           Value::makeNumber (m.falseBeatsOffGridInSilencePerSecond));

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
    o.set ("bpmRelErrorMeanSteady", Value::makeNumber (a.bpmRelErrorMeanSteady));
    o.set ("bpmRelErrorMedianSteady", Value::makeNumber (a.bpmRelErrorMedianSteady));
    o.set ("bpmRelErrorWorstCore", Value::makeNumber (a.bpmRelErrorWorstCore));

    o.set ("fMeasureMean", Value::makeNumber (a.fMeasureMean));
    o.set ("precisionMean", Value::makeNumber (a.precisionMean));
    o.set ("recallMean", Value::makeNumber (a.recallMean));

    o.set ("halfDoubleEvaluated", Value::makeNumber (a.halfDoubleEvaluated));
    o.set ("halfDoubleErrors", Value::makeNumber (a.halfDoubleErrors));
    o.set ("halfDoubleErrorRate", Value::makeNumber (a.halfDoubleErrorRate));
    o.set ("halfDoubleEvaluatedCore", Value::makeNumber (a.halfDoubleEvaluatedCore));
    o.set ("halfDoubleErrorsCore", Value::makeNumber (a.halfDoubleErrorsCore));
    o.set ("halfDoubleErrorRateCore", Value::makeNumber (a.halfDoubleErrorRateCore));

    o.set ("silenceFixtures", Value::makeNumber (a.silenceFixtures));
    o.set ("falseBeatsInSilencePerSecondWorst",
           Value::makeNumber (a.falseBeatsInSilencePerSecondWorst));

    o.set ("hasRecovery", Value::makeBool (a.hasRecovery));
    o.set ("recoverySecondsWorst", Value::makeNumber (a.recoverySecondsWorst));

    o.set ("hasSyncopation", Value::makeBool (a.hasSyncopation));
    o.set ("syncopationMaxDeviationFraction",
           Value::makeNumber (a.syncopationMaxDeviationFraction));
    o.set ("syncopationMaxStepFraction", Value::makeNumber (a.syncopationMaxStepFraction));

    o.set ("hasRamp", Value::makeBool (a.hasRamp));
    o.set ("rampLocalTempoRelErrorMean", Value::makeNumber (a.rampLocalTempoRelErrorMean));
    o.set ("rampLocalTempoRelErrorWorst", Value::makeNumber (a.rampLocalTempoRelErrorWorst));

    o.set ("cpuSecondsTotal", Value::makeNumber (a.cpuSecondsTotal));
    o.set ("allocationsTotal", Value::makeNumber (static_cast<double> (a.allocationsTotal)));

    Value gates = Value::makeObject();
    gates.set ("gateAcquire95Core", Value::makeBool (a.gateAcquire95Core));
    gates.set ("gateBpm2Core", Value::makeBool (a.gateBpm2Core));
    gates.set ("gateHalfDouble5Core", Value::makeBool (a.gateHalfDouble5Core));
    gates.set ("gateSyncopationNoJump", Value::makeBool (a.gateSyncopationNoJump));
    gates.set ("gateRampFollows", Value::makeBool (a.gateRampFollows));
    o.set ("spec19Gates", gates);
    return o;
}

const char* fixtureMetricsCsvHeader()
{
    return "name,core,steady,ramp,predictedBeats,truthBeats,truePositives,precision,recall,"
           "fMeasure,acquired,acquisitionBars,bpmRelativeError,halfDoubleTimeError,"
           "falseBeatsInSilencePerSecond,falseBeatsOffGridInSilencePerSecond,recoverySeconds,"
           "syncopationMaxDeviationFraction,rampLocalTempoRelErrorMean,phaseP95AbsMs,cpuSeconds,"
           "allocationCount\n";
}

std::string fixtureMetricsCsvRow (const FixtureMetrics& m)
{
    std::string row;
    row += m.name;
    row += ',';
    row += m.core ? '1' : '0';
    row += ',';
    row += m.steady ? '1' : '0';
    row += ',';
    row += m.ramp ? '1' : '0';
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
    row += csvNumber (m.falseBeatsInSilencePerSecond);
    row += ',';
    row += csvNumber (m.falseBeatsOffGridInSilencePerSecond);
    row += ',';
    row += csvNumber (m.recoverySeconds);
    row += ',';
    row += csvNumber (m.syncopationMaxDeviationFraction);
    row += ',';
    row += csvNumber (m.rampLocalTempoRelErrorMean);
    row += ',';
    row += csvNumber (m.phaseP95AbsMs);
    row += ',';
    row += csvNumber (m.cpuSeconds);
    row += ',';
    row += std::to_string (m.allocationCount);
    row += '\n';
    return row;
}

std::string markdownSummary (const std::string& backendId,
                             const std::string& corpusId,
                             double toleranceSeconds,
                             const std::vector<FixtureMetrics>& fixtures,
                             const AggregateMetrics& aggregate)
{
    char buf[512];
    std::string md;
    md += "# Rhythm evaluation summary\n\n";
    md += "- backend: `" + backendId + "`\n";
    md += "- corpus: `" + corpusId + "`\n";
    std::snprintf (buf, sizeof buf, "- beat-match tolerance: %.0f ms\n",
                   toleranceSeconds * 1000.0);
    md += buf;
    std::snprintf (buf, sizeof buf, "- fixtures: %d (%d core, %d steady, %d ramp)\n\n",
                   aggregate.fixtures, aggregate.coreFixtures,
                   aggregate.steadyFixtures, aggregate.rampFixtures);
    md += buf;

    md += "| fixture | core | pred | TP | P | R | F | acq bars | BPM err | h/d | "
          "false/s | false off-grid/s | recovery s | sync dev | ramp err | p95 phase ms |\n";
    md += "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const FixtureMetrics& m : fixtures)
    {
        const double acq = m.acquired ? m.acquisitionBars : -1.0;
        std::snprintf (buf, sizeof buf,
                       "| %s | %s | %d | %d | %.3f | %.3f | %.3f | %.2f | %.3f | %s | "
                       "%.3f | %.3f | %.3f | %.4f | %.4f | %.2f |\n",
                       m.name.c_str(),
                       m.core ? "yes" : "",
                       m.predictedBeats,
                       m.truePositives,
                       m.precision,
                       m.recall,
                       m.fMeasure,
                       acq,
                       m.bpmRelativeError,
                       m.halfDoubleTimeError ? "ERR" : "-",
                       m.falseBeatsInSilencePerSecond,
                       m.falseBeatsOffGridInSilencePerSecond,
                       m.recoverySeconds,
                       m.syncopationMaxDeviationFraction,
                       m.rampLocalTempoRelErrorMean,
                       m.phaseP95AbsMs);
        md += buf;
    }

    md += "\n## Aggregates\n\n";
    std::snprintf (buf, sizeof buf,
                   "- F-measure mean %.4f (precision %.4f, recall %.4f)\n",
                   aggregate.fMeasureMean, aggregate.precisionMean, aggregate.recallMean);
    md += buf;
    std::snprintf (buf, sizeof buf,
                   "- BPM relative error (steady): mean %.4f, median %.4f, core worst %.4f\n",
                   aggregate.bpmRelErrorMeanSteady, aggregate.bpmRelErrorMedianSteady,
                   aggregate.bpmRelErrorWorstCore);
    md += buf;
    std::snprintf (buf, sizeof buf,
                   "- half/double-time error rate: %.4f (%d/%d), core %.4f (%d/%d)\n",
                   aggregate.halfDoubleErrorRate, aggregate.halfDoubleErrors,
                   aggregate.halfDoubleEvaluated, aggregate.halfDoubleErrorRateCore,
                   aggregate.halfDoubleErrorsCore, aggregate.halfDoubleEvaluatedCore);
    md += buf;
    std::snprintf (buf, sizeof buf,
                   "- acquisition within 2 bars (core): %d/%d = %.4f\n",
                   aggregate.acquisitionCoreWithin2Bars, aggregate.acquisitionCoreEvaluated,
                   aggregate.acquisitionCorePassFraction);
    md += buf;
    std::snprintf (buf, sizeof buf,
                   "- worst false beats/s in declared silence: %.4f\n",
                   aggregate.falseBeatsInSilencePerSecondWorst);
    md += buf;
    std::snprintf (buf, sizeof buf,
                   "- ramp local-tempo relative error: mean %.4f, worst %.4f\n",
                   aggregate.rampLocalTempoRelErrorMean,
                   aggregate.rampLocalTempoRelErrorWorst);
    md += buf;
    std::snprintf (buf, sizeof buf,
                   "- syncopation max deviation %.4f, max step %.4f\n",
                   aggregate.syncopationMaxDeviationFraction,
                   aggregate.syncopationMaxStepFraction);
    md += buf;
    std::snprintf (buf, sizeof buf, "- CPU %.4f s total, %llu allocations total\n\n",
                   aggregate.cpuSecondsTotal,
                   static_cast<unsigned long long> (aggregate.allocationsTotal));
    md += buf;

    md += "## SPEC 19 gates\n\n";
    md += "| gate | result |\n|---|---|\n";
    auto gate = [&md] (const char* name, bool pass)
    {
        md += "| ";
        md += name;
        md += " | ";
        md += pass ? "PASS" : "FAIL";
        md += " |\n";
    };
    gate ("acquire within 2 bars for >= 95% of core", aggregate.gateAcquire95Core);
    gate ("locked BPM relative error <= 2% on core", aggregate.gateBpm2Core);
    gate ("half/double-time errors < 5% on core", aggregate.gateHalfDouble5Core);
    gate ("no tempo jump from isolated syncopation", aggregate.gateSyncopationNoJump);
    gate ("follow ramps (mean local-tempo error <= 2%)", aggregate.gateRampFollows);
    md += "\n";
    md += "The silence gate (SPEC 19 \"silence does not create false acceleration\") "
          "is qualitative; the false-beat-in-silence columns above report the raw "
          "numbers and the `*_offGrid` column excludes the maintained grid beat.\n";
    return md;
}

} // namespace rhythmeval
