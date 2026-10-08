// AcquisitionReplay implementation. See AcquisitionReplay.h.

#include "AcquisitionReplay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace tracker_diag
{

namespace
{

struct RunStat
{
    std::size_t length = 0;
    std::size_t start = 0;
};

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

double localPeriodAtIndex (const rhythmeval::RhythmTruth& truth, std::size_t i)
{
    const std::size_t n = truth.beats.size();
    if (n < 2)
        return truth.hasNominalBpm && truth.nominalBpm > 0.0
                   ? 60.0 / truth.nominalBpm : 0.0;
    if (i + 1 < n)
        return truth.beats[i + 1] - truth.beats[i];
    return truth.beats[i] - truth.beats[i - 1];
}

double localBpmAtIndex (const rhythmeval::RhythmTruth& truth, std::size_t i)
{
    const double period = localPeriodAtIndex (truth, i);
    return period > 0.0 ? 60.0 / period : 0.0;
}

const rhythmeval::TempoSample* tempoSampleNear (
    const std::vector<rhythmeval::TempoSample>& samples, double t)
{
    if (samples.empty())
        return nullptr;
    const auto it = std::lower_bound (
        samples.begin(), samples.end(), t,
        [] (const rhythmeval::TempoSample& s, double time) {
            return s.timeSeconds < time;
        });
    if (it == samples.begin())
        return &samples.front();
    if (it == samples.end())
        return &samples.back();
    const rhythmeval::TempoSample& hi = *it;
    const rhythmeval::TempoSample& lo = *(it - 1);
    return (t - lo.timeSeconds) <= (hi.timeSeconds - t) ? &lo : &hi;
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

/** Exact scorer tempo clause for one predicted beat. Returns 0 = agreeing,
    1 = no sample, 2 = phase invalid, 3 = bpm invalid, 4 = outside band.
    A truth beat with local BPM 0 is skipped by the scorer and counts as
    agreeing (0). */
int tempoClauseFailure (const rhythmeval::RhythmTruth& truth,
                        const rhythmeval::ObservationSeries& obs,
                        double predictedTime, double bpmAgreement)
{
    const int gi = nearestTruthIndex (truth.beats, predictedTime);
    if (gi < 0)
        return 0;
    const double truthBpm = localBpmAtIndex (truth, static_cast<std::size_t> (gi));
    if (! (truthBpm > 0.0))
        return 0;
    const rhythmeval::TempoSample* s = tempoSampleNear (obs.tempoSamples, predictedTime);
    if (s == nullptr)
        return 1;
    if (! s->phaseValid)
        return 2;
    if (! (s->bpm > 0.0))
        return 3;
    if (std::fabs (s->bpm - truthBpm) / truthBpm > bpmAgreement)
        return 4;
    return 0;
}

/** True when the full lock condition holds for the run starting at `i`; if so
    returns the run length actually satisfied. Mirrors Metrics.cpp. */
bool runSatisfied (const rhythmeval::RhythmTruth& truth,
                   const rhythmeval::ObservationSeries& obs,
                   std::size_t i, std::size_t lockRunLength, double tol,
                   double bpmAgreement, std::size_t& runLength)
{
    const std::vector<double>& pred = obs.beatTimesSeconds;
    const std::vector<double>& beats = truth.beats;
    if (i + lockRunLength > pred.size())
        return false;
    int previousGi = -1;
    std::size_t k = 0;
    for (; k < lockRunLength; ++k)
    {
        const int gi = nearestTruthIndex (beats, pred[i + k]);
        if (gi < 0 || std::fabs (pred[i + k] - beats[static_cast<std::size_t> (gi)]) > tol)
            return false;
        if (gi <= previousGi)
            return false;
        if (tempoClauseFailure (truth, obs, pred[i + k], bpmAgreement) != 0)
            return false;
        previousGi = gi;
    }
    runLength = k;
    return true;
}

RunStat longestRun (const rhythmeval::RhythmTruth& truth,
                    const rhythmeval::ObservationSeries& obs,
                    double tol, bool checkTempo, double bpmAgreement)
{
    const std::vector<double>& pred = obs.beatTimesSeconds;
    const std::vector<double>& beats = truth.beats;
    RunStat best;
    for (std::size_t i = 0; i < pred.size(); ++i)
    {
        std::size_t run = 0;
        int previousGi = -1;
        for (std::size_t k = 0; i + k < pred.size(); ++k)
        {
            const int gi = nearestTruthIndex (beats, pred[i + k]);
            if (gi < 0 || std::fabs (pred[i + k] - beats[static_cast<std::size_t> (gi)]) > tol)
                break;
            if (gi <= previousGi)
                break;
            if (checkTempo
                && tempoClauseFailure (truth, obs, pred[i + k], bpmAgreement) != 0)
                break;
            ++run;
            previousGi = gi;
        }
        if (run > best.length)
        {
            best.length = run;
            best.start = i;
        }
    }
    return best;
}

ClauseCounts clauseCounts (const rhythmeval::RhythmTruth& truth,
                           const rhythmeval::ObservationSeries& obs,
                           std::size_t start, std::size_t length,
                           double bpmAgreement)
{
    ClauseCounts c;
    const std::vector<double>& pred = obs.beatTimesSeconds;
    for (std::size_t k = 0; k < length && start + k < pred.size(); ++k)
    {
        ++c.beats;
        switch (tempoClauseFailure (truth, obs, pred[start + k], bpmAgreement))
        {
            case 1: ++c.missingSample; break;
            case 2: ++c.phaseInvalid; break;
            case 3: ++c.bpmInvalid; break;
            case 4: ++c.outsideBand; break;
            default:
                ++c.agreeing;
                if (localBpmAtIndex (truth,
                        static_cast<std::size_t> (nearestTruthIndex (truth.beats,
                                                                     pred[start + k]))) <= 0.0)
                    ++c.truthBpmZero;
                break;
        }
    }
    return c;
}

} // namespace

const char* toString (AcquireReason r) noexcept
{
    switch (r)
    {
        case AcquireReason::AcquiredWithin2Bars:  return "AcquiredWithin2Bars";
        case AcquireReason::AcquiredAfter2Bars:   return "AcquiredAfter2Bars";
        case AcquireReason::InsufficientBeatEvents: return "InsufficientBeatEvents";
        case AcquireReason::TempoOutsideBand:     return "TempoOutsideBand";
        case AcquireReason::TempoEvidenceMissing: return "TempoEvidenceMissing";
        case AcquireReason::TempoPhaseInvalid:    return "TempoPhaseInvalid";
        case AcquireReason::TempoBpmInvalid:      return "TempoBpmInvalid";
        case AcquireReason::TempoAgreementFailure: return "TempoAgreementFailure";
        case AcquireReason::NoSustainedMatchRun:  return "NoSustainedMatchRun";
        case AcquireReason::NoMatchingBeats:      return "NoMatchingBeats";
        case AcquireReason::Other:                return "Other";
    }
    return "Unknown";
}

std::string optionalNumber (bool measured, double value)
{
    if (! measured)
        return std::string();
    char buf[40];
    std::snprintf (buf, sizeof buf, "%.10g", std::isfinite (value) ? value : 0.0);
    return std::string (buf);
}

LockRun replayLock (const rhythmeval::RhythmTruth& truth,
                    const rhythmeval::ObservationSeries& obs,
                    double tol,
                    double minTime,
                    std::size_t lockRunLength,
                    double bpmAgreement)
{
    LockRun r;
    const std::vector<double>& pred = obs.beatTimesSeconds;
    const std::vector<double>& beats = truth.beats;

    r.longestMatchRun = longestRun (truth, obs, tol, false, bpmAgreement).length;
    r.longestRunWithTempo = longestRun (truth, obs, tol, true, bpmAgreement).length;

    if (beats.empty() || pred.size() < lockRunLength)
        return r;

    for (std::size_t i = 0; i + lockRunLength <= pred.size(); ++i)
    {
        if (pred[i] < minTime - 1.0e-9)
            continue;
        std::size_t run = 0;
        if (! runSatisfied (truth, obs, i, lockRunLength, tol, bpmAgreement, run))
            continue;

        r.found = true;
        r.startPredIndex = i;
        r.confirmPredIndex = i + lockRunLength - 1;
        r.truthIndex = nearestTruthIndex (beats, pred[i]);
        r.startEventSeconds = pred[i];
        r.confirmEventSeconds = pred[r.confirmPredIndex];
        if (r.confirmPredIndex < obs.beatAvailabilitySeconds.size())
        {
            r.hasAvailability = true;
            r.startAvailabilitySeconds = obs.beatAvailabilitySeconds[i];
            r.confirmAvailabilitySeconds =
                obs.beatAvailabilitySeconds[r.confirmPredIndex];
        }
        const double lockTime = pred[i];
        r.acquisitionSeconds = std::max (0.0, lockTime - beats.front());
        r.acquisitionBeats = std::max (0.0, rhythmeval::beatPositionAt (truth, lockTime));
        r.acquisitionBars = truth.beatsPerBar > 0
                                ? r.acquisitionBeats
                                      / static_cast<double> (truth.beatsPerBar)
                                : 0.0;
        return r;
    }
    return r;
}

LockRun replayRecoveryLock (const rhythmeval::RhythmTruth& truth,
                            const rhythmeval::ObservationSeries& obs,
                            double tol)
{
    LockRun r;
    if (truth.name != "stop_start" || truth.trueSilenceSpans.empty())
        return r;
    double silenceEnd = 0.0;
    for (const rhythmeval::SilenceSpan& s : truth.trueSilenceSpans)
        silenceEnd = std::max (silenceEnd, s.endSeconds);
    double evidenceResume = silenceEnd;
    for (const double onset : truth.onsets)
        if (onset >= silenceEnd)
        {
            evidenceResume = onset;
            break;
        }
    return replayLock (truth, obs, tol, evidenceResume);
}

FixtureDiagnosis diagnoseFixture (const rhythmeval::RhythmTruth& truth,
                                  const rhythmeval::ObservationSeries& obs,
                                  const rhythmeval::FixtureMetrics& scorerResult,
                                  double tol)
{
    FixtureDiagnosis d;
    d.name = truth.name;
    d.core = truth.isCore();
    d.steady = truth.isSteady();
    d.ramp = truth.isRamp();
    d.hasNominalBpm = truth.hasNominalBpm;
    d.nominalBpm = truth.nominalBpm;
    d.predictedBeats = static_cast<int> (obs.beatTimesSeconds.size());
    d.truthBeats = static_cast<int> (truth.beats.size());
    d.scorerAcquired = scorerResult.acquired;
    d.scorerAcquisitionBars = scorerResult.acquisitionBars;
    d.scorerAcquisitionSeconds = scorerResult.acquisitionSeconds;

    d.lock = replayLock (truth, obs, tol);
    d.recovery = replayRecoveryLock (truth, obs, tol);
    d.agreesWithScorer = (d.lock.found == d.scorerAcquired);
    if (d.lock.found && d.scorerAcquired)
    {
        if (std::fabs (d.lock.acquisitionBars - d.scorerAcquisitionBars) > 1.0e-9)
            d.agreesWithScorer = false;
    }

    // The clause tally is taken over the longest forward-advancing positional
    // match run, so it describes the run that came closest to locking.
    const RunStat matchRun = longestRun (truth, obs, tol, false,
                                         rhythmeval::kBpmAgreementFraction);
    d.clause = clauseCounts (truth, obs, matchRun.start, matchRun.length,
                             rhythmeval::kBpmAgreementFraction);

    std::vector<int> predMatch;
    d.matchedBeats = rhythmeval::matchBeats (obs.beatTimesSeconds, truth.beats, tol, predMatch);

    // --- BPM trajectory evidence -------------------------------------------
    std::vector<double> validBpm;
    int withinBand = 0, validCount = 0;
    for (const rhythmeval::TempoSample& s : obs.tempoSamples)
    {
        if (! s.phaseValid || ! (s.bpm > 0.0))
            continue;
        validBpm.push_back (s.bpm);
        ++validCount;
        double truthBpm = 0.0;
        if (truth.hasNominalBpm && truth.nominalBpm > 0.0)
            truthBpm = truth.nominalBpm;
        else
            truthBpm = rhythmeval::localTruthBpmAtTime (truth, s.timeSeconds);
        if (truthBpm > 0.0
            && std::fabs (s.bpm - truthBpm) / truthBpm <= rhythmeval::kBpmAgreementFraction)
            ++withinBand;
    }
    d.bpmMeasured = validCount > 0;
    d.medianBpm = median (validBpm);
    d.medianBpmErrorMeasured = d.bpmMeasured && d.hasNominalBpm && d.nominalBpm > 0.0;
    if (d.medianBpmErrorMeasured)
        d.medianBpmError = std::fabs (d.medianBpm - d.nominalBpm) / d.nominalBpm;
    d.bpmAgreementFractionInWindow =
        validCount > 0 ? static_cast<double> (withinBand) / validCount : 0.0;

    std::vector<double> ratios;
    for (std::size_t i = 0; i < obs.beatTimesSeconds.size(); ++i)
    {
        if (predMatch[i] < 0)
            continue;
        const rhythmeval::TempoSample* s =
            tempoSampleNear (obs.tempoSamples, obs.beatTimesSeconds[i]);
        const double truthBpm =
            rhythmeval::localTruthBpmAtTime (truth, obs.beatTimesSeconds[i]);
        if (s != nullptr && s->phaseValid && s->bpm > 0.0 && truthBpm > 0.0)
            ratios.push_back (s->bpm / truthBpm);
    }
    d.ratioMeasured = ! ratios.empty();
    d.ratioToTruth = median (ratios);
    if (d.ratioMeasured)
        d.octaveSuspect = std::fabs (d.ratioToTruth - 0.5) <= 0.05
                          || std::fabs (d.ratioToTruth - 2.0) <= 0.10;

    // --- silence gate evidence ---------------------------------------------
    for (const rhythmeval::TempoSample& s : obs.tempoSamples)
    {
        if (! s.silence)
            continue;
        ++d.silenceBlocks;
        d.silenceSeconds += std::max (0.0, s.availabilitySeconds - s.timeSeconds);
    }

    // --- phase evidence (event clock) --------------------------------------
    {
        double sum = 0.0, absSum = 0.0;
        int n = 0;
        for (std::size_t i = 0; i < obs.beatTimesSeconds.size(); ++i)
        {
            if (predMatch[i] < 0)
                continue;
            const int near = nearestTruthIndex (truth.beats, obs.beatTimesSeconds[i]);
            if (near < 0)
                continue;
            const double e = obs.beatTimesSeconds[i] - truth.beats[static_cast<std::size_t> (near)];
            sum += e; absSum += std::fabs (e); ++n;
        }
        d.phaseMatchedBeats = n;
        d.phaseMeasured = n > 0;
        if (d.phaseMeasured)
        {
            d.meanSignedPhaseMs = sum / n * 1000.0;
            d.meanAbsPhaseMs = absSum / n * 1000.0;
        }
    }

    // --- reason -------------------------------------------------------------
    const std::size_t L = static_cast<std::size_t> (rhythmeval::kLockRunLength);
    char buf[320];
    if (d.scorerAcquired && d.scorerAcquisitionBars <= 2.0)
    {
        d.reason = AcquireReason::AcquiredWithin2Bars;
        std::snprintf (buf, sizeof buf, "lock at %.3f bars (%.3f s), confirmed %.3f s",
                       d.lock.acquisitionBars, d.lock.acquisitionSeconds,
                       d.lock.confirmEventSeconds);
        d.reasonDetail = buf;
    }
    else if (d.scorerAcquired)
    {
        d.reason = AcquireReason::AcquiredAfter2Bars;
        std::snprintf (buf, sizeof buf, "lock only at %.3f bars (%.3f s)",
                       d.scorerAcquisitionBars, d.scorerAcquisitionSeconds);
        d.reasonDetail = buf;
    }
    else if (d.predictedBeats < static_cast<int> (L))
    {
        d.reason = AcquireReason::InsufficientBeatEvents;
        std::snprintf (buf, sizeof buf, "%d beat events < lock run %zu",
                       d.predictedBeats, L);
        d.reasonDetail = buf;
    }
    else if (d.lock.longestMatchRun >= L && d.lock.longestRunWithTempo < L)
    {
        const ClauseCounts& c = d.clause;
        const int kinds = (c.missingSample > 0) + (c.phaseInvalid > 0)
                          + (c.bpmInvalid > 0) + (c.outsideBand > 0);
        if (kinds > 1)
            d.reason = AcquireReason::TempoAgreementFailure;
        else if (c.outsideBand > 0)
            d.reason = AcquireReason::TempoOutsideBand;
        else if (c.missingSample > 0)
            d.reason = AcquireReason::TempoEvidenceMissing;
        else if (c.phaseInvalid > 0)
            d.reason = AcquireReason::TempoPhaseInvalid;
        else if (c.bpmInvalid > 0)
            d.reason = AcquireReason::TempoBpmInvalid;
        else
            d.reason = AcquireReason::Other;

        std::snprintf (buf, sizeof buf,
                       "longest %zu-beat match run: agreeing %d, outsideBand %d, "
                       "missingSample %d, phaseInvalid %d, bpmInvalid %d; "
                       "medianBpm %s, nominal %.3f, ratio %s",
                       d.lock.longestMatchRun, c.agreeing, c.outsideBand,
                       c.missingSample, c.phaseInvalid, c.bpmInvalid,
                       d.bpmMeasured ? std::to_string (d.medianBpm).c_str() : "n/a",
                       d.nominalBpm,
                       d.ratioMeasured ? std::to_string (d.ratioToTruth).c_str() : "n/a");
        d.reasonDetail = buf;
    }
    else if (d.lock.longestMatchRun == 0)
    {
        d.reason = AcquireReason::NoMatchingBeats;
        std::snprintf (buf, sizeof buf,
                       "%d beats, none within %.0f ms of a truth beat; mean abs phase %s",
                       d.predictedBeats, tol * 1000.0,
                       d.phaseMeasured ? std::to_string (d.meanAbsPhaseMs).c_str() : "n/a");
        d.reasonDetail = buf;
    }
    else if (d.lock.longestMatchRun < L)
    {
        d.reason = AcquireReason::NoSustainedMatchRun;
        std::snprintf (buf, sizeof buf,
                       "longest forward-advancing positional run %zu < %zu "
                       "(matched %d/%d); possible causes include gaps, duplicate beats "
                       "or a grid alias; no tempo clause was isolated",
                       d.lock.longestMatchRun, L, d.matchedBeats, d.truthBeats);
        d.reasonDetail = buf;
    }
    else
    {
        d.reason = AcquireReason::Other;
        std::snprintf (buf, sizeof buf,
                       "match run %zu, tempo run %zu, matched %d/%d",
                       d.lock.longestMatchRun, d.lock.longestRunWithTempo,
                       d.matchedBeats, d.truthBeats);
        d.reasonDetail = buf;
    }
    return d;
}

} // namespace tracker_diag
