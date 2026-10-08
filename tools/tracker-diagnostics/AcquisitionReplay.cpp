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

/** True when the full lock condition holds for the run starting at `i`; if so
    returns the run length actually satisfied. `outLongest` receives the run
    length when the condition holds at least one beat. Mirrors Metrics.cpp. */
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
        const double truthBpm = localBpmAtIndex (truth, static_cast<std::size_t> (gi));
        if (truthBpm > 0.0)
        {
            const rhythmeval::TempoSample* s =
                tempoSampleNear (obs.tempoSamples, pred[i + k]);
            if (s == nullptr || ! s->phaseValid || ! (s->bpm > 0.0)
                || std::fabs (s->bpm - truthBpm) / truthBpm > bpmAgreement)
                return false;
        }
        previousGi = gi;
    }
    runLength = k;
    return true;
}

std::size_t longestRun (const rhythmeval::RhythmTruth& truth,
                        const rhythmeval::ObservationSeries& obs,
                        double tol, bool checkTempo, double bpmAgreement)
{
    const std::vector<double>& pred = obs.beatTimesSeconds;
    const std::vector<double>& beats = truth.beats;
    std::size_t best = 0;
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
            if (checkTempo)
            {
                const double truthBpm = localBpmAtIndex (truth, static_cast<std::size_t> (gi));
                if (truthBpm > 0.0)
                {
                    const rhythmeval::TempoSample* s =
                        tempoSampleNear (obs.tempoSamples, pred[i + k]);
                    if (s == nullptr || ! s->phaseValid || ! (s->bpm > 0.0)
                        || std::fabs (s->bpm - truthBpm) / truthBpm > bpmAgreement)
                        break;
                }
            }
            ++run;
            previousGi = gi;
        }
        best = std::max (best, run);
    }
    return best;
}

} // namespace

const char* toString (AcquireReason r) noexcept
{
    switch (r)
    {
        case AcquireReason::AcquiredWithin2Bars:        return "AcquiredWithin2Bars";
        case AcquireReason::AcquiredAfter2Bars:         return "AcquiredAfter2Bars";
        case AcquireReason::InsufficientBeatEvents:     return "InsufficientBeatEvents";
        case AcquireReason::LockTempoAgreementFailure:  return "LockTempoAgreementFailure";
        case AcquireReason::PhaseConflictOrDropouts:    return "PhaseConflictOrDropouts";
        case AcquireReason::NoMatchingBeats:            return "NoMatchingBeats";
        case AcquireReason::Other:                      return "Other";
    }
    return "Unknown";
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

    r.longestMatchRun = longestRun (truth, obs, tol, /*checkTempo=*/false, bpmAgreement);
    r.longestRunWithTempo =
        longestRun (truth, obs, tol, /*checkTempo=*/true, bpmAgreement);

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
        if (i < obs.beatAvailabilitySeconds.size())
        {
            r.hasAvailability = true;
            r.startAvailabilitySeconds = obs.beatAvailabilitySeconds[i];
            if (r.confirmPredIndex < obs.beatAvailabilitySeconds.size())
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
    d.medianBpm = median (validBpm);
    if (d.hasNominalBpm && d.nominalBpm > 0.0)
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
    d.ratioToTruth = median (ratios);
    if (d.ratioToTruth > 0.0)
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
        if (n > 0)
        {
            d.meanSignedPhaseMs = sum / n * 1000.0;
            d.meanAbsPhaseMs = absSum / n * 1000.0;
        }
    }

    // --- reason -------------------------------------------------------------
    const std::size_t L = static_cast<std::size_t> (rhythmeval::kLockRunLength);
    char buf[256];
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
        d.reason = AcquireReason::LockTempoAgreementFailure;
        if (d.octaveSuspect)
            std::snprintf (buf, sizeof buf,
                           "octave suspect: reported/truth ratio %.4f, longest match run %zu",
                           d.ratioToTruth, d.lock.longestMatchRun);
        else
            std::snprintf (buf, sizeof buf,
                           "BPM outside 2%%: median %.3f vs nominal %.1f (err %.4f), "
                           "ratio %.4f, longest match run %zu",
                           d.medianBpm, d.nominalBpm, d.medianBpmError,
                           d.ratioToTruth, d.lock.longestMatchRun);
        d.reasonDetail = buf;
    }
    else if (d.lock.longestMatchRun == 0)
    {
        d.reason = AcquireReason::NoMatchingBeats;
        std::snprintf (buf, sizeof buf,
                       "%d beats, none within %.0f ms of a truth beat; mean abs phase %.1f ms",
                       d.predictedBeats, tol * 1000.0, d.meanAbsPhaseMs);
        d.reasonDetail = buf;
    }
    else if (d.lock.longestMatchRun < L)
    {
        d.reason = AcquireReason::PhaseConflictOrDropouts;
        std::snprintf (buf, sizeof buf,
                       "longest forward-advancing match run %zu < %zu; matched %d/%d; "
                       "mean abs phase %.1f ms",
                       d.lock.longestMatchRun, L, d.matchedBeats, d.truthBeats,
                       d.meanAbsPhaseMs);
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
