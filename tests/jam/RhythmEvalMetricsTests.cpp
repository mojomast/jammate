// EVAL-002 metric tests: suite RhythmEvalMetrics.
//
// These exercise the *scoring* end-to-end with hand-built observation series and
// no audio, which is only possible because Metrics.h/.cpp are pure and I/O-free
// (DEVPLAN EVAL-002: "the harness must support deterministic synthetic
// observation tests separately from audio tests"). A perfect input must score
// perfectly; each known degradation must produce the value computed by hand in
// the comments; the ramp test proves the non-uniform ground-truth grid is used;
// boundary inputs must stay finite; and repeated scoring/serialisation must be
// byte-identical.
//
// Wiring note: jam-core/CMakeLists.txt is frozen for EVAL-002 and globs only
// src/jam/*.cpp and tests/jam/*.cpp, so the metric implementation is compiled
// into this test binary by direct inclusion. The CLI builds the same sources as
// a normal translation unit; there is no second copy of the algorithm.
//
// The corpus is located the same way as the RhythmCorpus suite: the
// JAM_RHYTHM_CORPUS environment variable, then __FILE__, then the working
// directory. A relocated corpus is a red test, never a suite that silently
// validates nothing.

#include "JamTest.h"

#include "../../tools/rhythm-eval/Metrics.cpp"
#include "../../tools/rhythm-eval/Manifest.cpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using namespace jamtest;
using namespace rhythmeval;

// ---------------------------------------------------------------------------
// Corpus discovery
// ---------------------------------------------------------------------------

std::string parentOf (const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return std::string();
    if (slash == 0)
        return std::string("/");
    return path.substr (0, slash);
}

std::string joinPath (const std::string& a, const std::string& b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    const char last = a[a.size() - 1];
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

bool fileExists (const std::string& path)
{
    std::ifstream in (path.c_str(), std::ios::binary);
    return in.good();
}

std::string corpusDir()
{
    if (const char* env = std::getenv ("JAM_RHYTHM_CORPUS"))
    {
        const std::string dir = env;
        if (fileExists (joinPath (dir, "manifest.json")))
            return dir;
    }

    std::string dir = parentOf (parentOf (__FILE__));   // <root>/tests
    for (int i = 0; i < 6 && ! dir.empty(); ++i)
    {
        const std::string candidate = joinPath (dir, "testdata/rhythm");
        if (fileExists (joinPath (candidate, "manifest.json")))
            return candidate;
        dir = parentOf (dir);
    }

    dir = ".";
    for (int i = 0; i < 8; ++i)
    {
        const std::string candidate = joinPath (dir, "testdata/rhythm");
        if (fileExists (joinPath (candidate, "manifest.json")))
            return candidate;
        const std::string up = parentOf (dir);
        if (up == dir)
            break;
        dir = up;
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// Synthetic ground truth / observation builders
// ---------------------------------------------------------------------------

RhythmTruth makeSteady (const std::string& name,
                        double bpm,
                        int count,
                        double first = 0.5,
                        int beatsPerBar = 4,
                        bool core = false)
{
    RhythmTruth t;
    t.name = name;
    t.nominalBpm = bpm;
    t.hasNominalBpm = true;
    t.tempoProfile = "constant";
    t.beatsPerBar = beatsPerBar;
    t.meterNumerator = beatsPerBar;
    t.meterDenominator = 4;
    if (core)
        t.tags.push_back ("core");
    t.tags.push_back ("steady_tempo");

    const double period = 60.0 / bpm;
    for (int i = 0; i < count; ++i)
        t.beats.push_back (first + static_cast<double> (i) * period);
    t.onsets = t.beats;
    t.durationSeconds = t.beats.empty() ? period : t.beats.back() + period;
    return t;
}

/** Observations at every ground-truth beat with a phase-valid tempo sample at
    each. `bpmOverride <= 0` uses the local ground-truth tempo (needed for the
    non-uniform ramp grid). */
ObservationSeries perfectSeries (const RhythmTruth& truth, double bpmOverride = -1.0)
{
    ObservationSeries obs;
    obs.audioDurationSeconds = truth.durationSeconds;
    obs.sampleRate = truth.sampleRate;
    for (const double b : truth.beats)
    {
        obs.beatTimesSeconds.push_back (b);
        TempoSample s;
        s.timeSeconds = b;
        s.bpm = bpmOverride > 0.0 ? bpmOverride : localTruthBpmAtTime (truth, b);
        s.phaseValid = true;
        obs.tempoSamples.push_back (s);
    }
    return obs;
}

bool allFinite (const FixtureMetrics& m)
{
    const double values[] = {
        m.beatToleranceSeconds,
        m.precision, m.recall, m.fMeasure,
        m.acquisitionSeconds, m.acquisitionBeats, m.acquisitionBars,
        m.lockedBpm, m.bpmRelativeError,
        m.trueSilenceSeconds, m.trueSilenceFractionOfDuration,
        m.falseBeatsInTrueSilencePerSecond,
        m.unplayedBeatWindowsSeconds, m.falseBeatsInUnplayedBeatWindowsPerSecond,
        m.falseBeatsOffGridInUnplayedBeatWindowsPerSecond,
        m.recoverySeconds, m.recoveryBeats,
        m.syncopationTempoStdDevBpm, m.syncopationTempoCv,
        m.syncopationMaxDeviationBpm, m.syncopationMaxDeviationFraction,
        m.syncopationMaxStepBpm, m.syncopationMaxStepFraction,
        m.rampLocalTempoRelErrorMean, m.rampLocalTempoRelErrorWorst,
        m.phaseMeanMs, m.phaseMeanAbsMs, m.phaseP50AbsMs, m.phaseP95AbsMs,
        m.phaseMeanBeats, m.phaseMeanAbsBeats, m.phaseP95AbsBeats,
        m.cpuSeconds
    };
    for (const double v : values)
        if (! std::isfinite (v))
            return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Perfect predictions score perfectly.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, perfectPredictionsScorePerfectly)
{
    RhythmTruth truth = makeSteady ("perfect", 120.0, 12);
    // Two different declared regions, and a perfect tracker predicts no beat
    // inside either: an unplayed beat window (silenceSpans) and a region of
    // genuine silence (trueSilenceSpans). Both counters must read exactly zero,
    // which is the anchor that proves the metric switch did not break the
    // perfect case.
    truth.silenceSpans.push_back (SilenceSpan { 3.10, 3.20 });
    truth.trueSilenceSpans.push_back (SilenceSpan { 4.60, 4.70 });

    const ObservationSeries obs = perfectSeries (truth, 120.0);
    const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);

    CHECK_NEAR (m.fMeasure, 1.0, 1e-12);
    CHECK_NEAR (m.precision, 1.0, 1e-12);
    CHECK_NEAR (m.recall, 1.0, 1e-12);
    CHECK_EQ (m.predictedBeats, 12);
    CHECK_EQ (m.truePositives, 12);

    CHECK (m.acquired);
    CHECK_NEAR (m.acquisitionBars, 0.0, 1e-12);
    CHECK_NEAR (m.acquisitionBeats, 0.0, 1e-12);

    CHECK (m.hasBpmLock);
    CHECK_NEAR (m.bpmRelativeError, 0.0, 1e-12);
    CHECK_NEAR (m.phaseMeanMs, 0.0, 1e-9);
    CHECK_NEAR (m.phaseMeanAbsMs, 0.0, 1e-9);
    CHECK_NEAR (m.phaseP95AbsMs, 0.0, 1e-9);

    CHECK (! m.halfDoubleTimeError);
    CHECK_EQ (m.falseBeatsInTrueSilence, 0);
    CHECK_NEAR (m.falseBeatsInTrueSilencePerSecond, 0.0, 1e-12);
    CHECK_EQ (m.falseBeatsInUnplayedBeatWindows, 0);
    CHECK_NEAR (m.falseBeatsInUnplayedBeatWindowsPerSecond, 0.0, 1e-12);
    CHECK (allFinite (m));
}

// ---------------------------------------------------------------------------
// 2. Known degradations produce the metric values computed by hand.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, knownDegradationsProduceKnownMetricValues)
{
    // (a) every 4th beat missed: 16 truth beats, 12 predicted; TP=12, FP=0,
    //     FN=4. recall = 12/16 = 0.75, precision = 1, F = 2*0.75/1.75.
    {
        RhythmTruth truth = makeSteady ("miss", 120.0, 16);
        ObservationSeries obs = perfectSeries (truth, 120.0);
        std::vector<double> predicted;
        for (std::size_t i = 0; i < truth.beats.size(); ++i)
            if (i % 4 != 3)
                predicted.push_back (truth.beats[i]);
        obs.beatTimesSeconds = predicted;

        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK_NEAR (m.recall, 0.75, 1e-9);
        CHECK_NEAR (m.precision, 1.0, 1e-9);
        CHECK_NEAR (m.fMeasure, 2.0 * 0.75 / 1.75, 1e-9);
        CHECK_EQ (m.truePositives, 12);
        CHECK_EQ (m.falseNegatives, 4);
        CHECK_EQ (m.falsePositives, 0);
    }

    // (b) doubled tempo: every truth beat plus one prediction halfway between.
    //     8 truth beats, 16 predictions: TP=8, FP=8, recall=1, precision=0.5.
    //     The reported BPM is 2x nominal, so the half/double metric fires AND
    //     the plain BPM relative error fires (both are reported separately).
    {
        RhythmTruth truth = makeSteady ("double", 120.0, 8);
        ObservationSeries obs;
        obs.audioDurationSeconds = truth.durationSeconds;
        obs.sampleRate = truth.sampleRate;
        const double period = 60.0 / 120.0;
        for (const double b : truth.beats)
        {
            obs.beatTimesSeconds.push_back (b);
            obs.beatTimesSeconds.push_back (b + 0.5 * period);
            TempoSample s;
            s.timeSeconds = b;
            s.bpm = 240.0;
            s.phaseValid = true;
            obs.tempoSamples.push_back (s);
        }

        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK (m.doubleTimeLock);
        CHECK (m.halfDoubleTimeError);
        CHECK_NEAR (m.bpmRelativeError, 1.0, 1e-9);
        CHECK_NEAR (m.recall, 1.0, 1e-9);
        CHECK_NEAR (m.precision, 0.5, 1e-9);
    }

    // (c) a constant 40 ms offset. With the documented 70 ms tolerance every
    //     prediction still matches, so F stays 1 and the phase error is exactly
    //     +40 ms. The test also asserts the tolerance sits between 40 ms and
    //     80 ms: halving it must break the match, and doubling it must not,
    //     which pins the documented value rather than just exercising it.
    {
        RhythmTruth truth = makeSteady ("offset", 120.0, 12);
        ObservationSeries obs = perfectSeries (truth, 120.0);
        for (double& b : obs.beatTimesSeconds)
            b += 0.040;

        const double tol = kBeatMatchToleranceSeconds;
        CHECK_NEAR (tol, 0.070, 1e-12);

        const FixtureMetrics loose = scoreFixture (truth, obs, tol);
        CHECK_NEAR (loose.phaseMeanMs, 40.0, 1e-6);
        CHECK_NEAR (loose.recall, 1.0, 1e-9);
        CHECK (loose.fMeasure > 0.99);

        const FixtureMetrics half = scoreFixture (truth, obs, 0.5 * tol);
        CHECK (half.fMeasure < 0.99);
        CHECK_NEAR (half.recall, 0.0, 1e-9);
    }

    // (d) predictions only inside an UNPLAYED-BEAT window (silenceSpans): they
    //     are counted by the secondary counter but NOT as false beats in
    //     silence, because the guitar did not stop — the beat was merely not
    //     played. The span sits between beats so no prediction is the maintained
    //     grid beat and the off-grid diagnostic agrees. The primary
    //     (true-silence) counter must stay zero.
    {
        RhythmTruth truth = makeSteady ("silent", 120.0, 8);
        truth.silenceSpans.push_back (SilenceSpan { 1.20, 1.40 });
        ObservationSeries obs;
        obs.audioDurationSeconds = truth.durationSeconds;
        obs.sampleRate = truth.sampleRate;
        obs.beatTimesSeconds = { 1.25, 1.30 };
        TempoSample s;
        s.timeSeconds = 1.25;
        s.bpm = 120.0;
        s.phaseValid = true;
        obs.tempoSamples.push_back (s);

        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK_EQ (m.falseBeatsInTrueSilence, 0);
        CHECK_EQ (m.falseBeatsInUnplayedBeatWindows, 2);
        CHECK (m.falseBeatsInUnplayedBeatWindowsPerSecond > 0.0);
        CHECK_EQ (m.falseBeatsOffGridInUnplayedBeatWindows, 2);
    }

    // (e) 3 s of delay before locking. At 120 BPM the beat is 0.5 s, so the
    //     tracker starts six beats late: acquisition = 6 beats = 1.5 bars.
    {
        RhythmTruth truth = makeSteady ("delay", 120.0, 16);
        ObservationSeries obs = perfectSeries (truth, 120.0);
        obs.beatTimesSeconds.erase (obs.beatTimesSeconds.begin(),
                                    obs.beatTimesSeconds.begin() + 6);

        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK (m.acquired);
        CHECK_NEAR (m.acquisitionSeconds, 3.0, 1e-9);
        CHECK_NEAR (m.acquisitionBeats, 6.0, 1e-9);
        CHECK_NEAR (m.acquisitionBars, 1.5, 1e-9);
    }
}

// ---------------------------------------------------------------------------
// 2b. The two false-beat counters measure two different failures.
// ---------------------------------------------------------------------------
//
// SPEC 12.3's "false beat rate in silence" must be measured against
// `trueSilenceSpans` (the guitar stopped), not `silenceSpans` (+/-30 ms windows
// around beats the player deliberately did not play while playing around them).
// Scoring the latter counted a correct held grid as fabrication (16.7/s on
// `stop_start`). These tests pin the switch: a beat in true silence IS false; a
// beat in an unplayed-beat window is NOT counted as a false beat but IS seen by
// the separate counter.

JAM_TEST (RhythmEvalMetrics, trueSilencePredictionsAreFalseBeats)
{
    RhythmTruth truth = makeSteady ("truesil", 120.0, 8);
    // The span contains the maintained grid beat at 1.0 and an off-grid beat at
    // 1.2. BOTH count: true silence is the guitar stopping, so a beat there is
    // fabricated whether or not it lands on the notional grid.
    truth.trueSilenceSpans.push_back (SilenceSpan { 0.95, 1.35 });
    truth.silenceSpans.clear();   // no unplayed-beat windows in this fixture

    ObservationSeries obs;
    obs.audioDurationSeconds = truth.durationSeconds;
    obs.sampleRate = truth.sampleRate;
    obs.beatTimesSeconds = { 1.0, 1.2 };

    const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
    CHECK_EQ (m.falseBeatsInTrueSilence, 2);
    CHECK_NEAR (m.falseBeatsInTrueSilencePerSecond, 2.0 / 0.40, 1e-9);
    CHECK_EQ (m.falseBeatsInUnplayedBeatWindows, 0);
    CHECK (allFinite (m));
}

JAM_TEST (RhythmEvalMetrics, unplayedBeatWindowsAreNotTrueSilence)
{
    RhythmTruth truth = makeSteady ("twocounters", 120.0, 8);
    truth.silenceSpans.push_back (SilenceSpan { 1.20, 1.40 });      // unplayed beat
    truth.trueSilenceSpans.push_back (SilenceSpan { 2.45, 2.55 });  // real stop

    ObservationSeries obs;
    obs.audioDurationSeconds = truth.durationSeconds;
    obs.sampleRate = truth.sampleRate;
    // 1.25 and 1.30 are in the unplayed window only; 2.50 is in true silence
    // only. Neither is a maintained grid beat.
    obs.beatTimesSeconds = { 1.25, 1.30, 2.50 };

    const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
    // The guitar did not stop at 1.25/1.30: those are not false beats in
    // silence, but the separate counter sees both.
    CHECK_EQ (m.falseBeatsInTrueSilence, 1);
    CHECK_EQ (m.falseBeatsInUnplayedBeatWindows, 2);
    CHECK_EQ (m.falseBeatsOffGridInUnplayedBeatWindows, 2);
    CHECK (m.falseBeatsInUnplayedBeatWindowsPerSecond > 0.0);
    CHECK (m.unplayedBeatWindowsSeconds < m.trueSilenceSeconds + 0.5);
    CHECK (allFinite (m));
}

// The real `stop_start` fixture: the two counters must disagree by the amount
// the corpus repair was for. A tracker holding the correct grid through the stop
// used to score 16.667 false beats/s because 8 held beats sat in 0.48 s of
// unplayed-beat windows. Against true silence the same grid scores ~2.10/s
// (10 held beats over 4.762 s), because the denominator is the true silence and
// the metric no longer treats every held beat as fabrication. Both numbers are
// asserted, so a regression in either field is caught.
JAM_TEST (RhythmEvalMetrics, stopStartCountersAreIndependentlyCorrect)
{
    const std::string dir = corpusDir();
    REQUIRE (! dir.empty());
    const Manifest manifest = readManifestFile (joinPath (dir, "manifest.json"));

    const ManifestFixture* stopStart = nullptr;
    for (const ManifestFixture& f : manifest.fixtures)
        if (f.name == "stop_start")
            stopStart = &f;
    REQUIRE (stopStart != nullptr);

    const RhythmTruth truth = toTruth (*stopStart);
    REQUIRE (! truth.trueSilenceSpans.empty());
    REQUIRE (! truth.silenceSpans.empty());

    // A tracker that holds the exact ground-truth grid through everything.
    const ObservationSeries held = perfectSeries (truth);
    const FixtureMetrics m = scoreFixture (truth, held, kBeatMatchToleranceSeconds);

    CHECK_NEAR (m.unplayedBeatWindowsSeconds, 0.480, 1e-2);
    CHECK_NEAR (m.trueSilenceSeconds, 4.762, 1e-2);
    // The old (wrong) field would read 16.667/s here.
    CHECK_EQ (m.falseBeatsInUnplayedBeatWindows, 8);
    CHECK_NEAR (m.falseBeatsInUnplayedBeatWindowsPerSecond,
                8.0 / m.unplayedBeatWindowsSeconds, 1e-9);
    // The primary metric reads ~2.10/s: ~8x lower, not the old fabrication rate.
    CHECK_EQ (m.falseBeatsInTrueSilence, 10);
    CHECK_NEAR (m.falseBeatsInTrueSilencePerSecond,
                10.0 / m.trueSilenceSeconds, 1e-9);
    CHECK (m.falseBeatsInTrueSilencePerSecond < 3.0);
    CHECK (m.falseBeatsInTrueSilencePerSecond
               < 0.2 * m.falseBeatsInUnplayedBeatWindowsPerSecond);

    // A tracker that fabricates only inside true silence still lights the
    // primary counter, and the secondary one stays quiet: the two are
    // independent.
    ObservationSeries fabricator;
    fabricator.sampleRate = truth.sampleRate;
    const SilenceSpan gap = truth.trueSilenceSpans.back();
    for (const double b : truth.beats)
    {
        const double t = b + 0.15;   // off-grid: > the 30 ms near-beat window
        if (t > gap.startSeconds && t < gap.endSeconds)
            fabricator.beatTimesSeconds.push_back (t);
    }
    REQUIRE (! fabricator.beatTimesSeconds.empty());
    const FixtureMetrics fab =
        scoreFixture (truth, fabricator, kBeatMatchToleranceSeconds);
    CHECK (fab.falseBeatsInTrueSilence > 0);
    CHECK_NEAR (fab.falseBeatsInTrueSilencePerSecond,
                static_cast<double> (fab.falseBeatsInTrueSilence) / m.trueSilenceSeconds,
                1e-9);
    CHECK_EQ (fab.falseBeatsInUnplayedBeatWindows, 0);
    CHECK (allFinite (fab));
}

// ---------------------------------------------------------------------------
// 2c. Latency compensation: explicit, exact, and consistent across metrics.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, latencyCompensationShiftsEveryBeatTimeMetric)
{
    // (a) exactness + no-op: a constant 30 ms offset, compensated by 0/10/20/30
    //     ms, must move the signed phase by exactly the requested amount and
    //     leave F untouched (all offsets are inside the 70 ms tolerance).
    {
        RhythmTruth truth = makeSteady ("lat-exact", 120.0, 12);
        ObservationSeries obs = perfectSeries (truth, 120.0);
        for (double& t : obs.beatTimesSeconds)
            t += 0.030;

        const double comps[] = { 0.0, 0.010, 0.020, 0.030 };
        for (const double c : comps)
        {
            const FixtureMetrics m =
                scoreFixture (truth, obs, kBeatMatchToleranceSeconds, c);
            CHECK_NEAR (m.phaseMeanMs, 30.0 - c * 1000.0, 1e-6);
            CHECK_NEAR (m.phaseMeanAbsMs, std::fabs (30.0 - c * 1000.0), 1e-6);
            CHECK_NEAR (m.fMeasure, 1.0, 1e-9);
            CHECK (m.acquired);
            // Acquisition uses predicted beat times too, so it moves with the
            // compensation as well: 0.03 s minus the requested compensation.
            CHECK_NEAR (m.acquisitionSeconds, 0.030 - c, 1e-9);
        }

        // Default argument and explicit zero are the same run: compensation is
        // a no-op at 0.
        const FixtureMetrics implicitZero =
            scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        const FixtureMetrics explicitZero =
            scoreFixture (truth, obs, kBeatMatchToleranceSeconds, 0.0);
        CHECK_EQ (fixtureMetricsToJson (implicitZero).dump(),
                  fixtureMetricsToJson (explicitZero).dump());
    }

    // (b) consistency across F, acquisition and false beats. A 100 ms offset is
    //     outside tolerance (F = 0, no lock, the beat at 1.1 is inside a true
    //     silence span). Compensating 50 ms brings it back inside tolerance;
    //     compensating 100 ms lands it exactly on the grid. A compensation that
    //     fixed phase but not F would fail here.
    {
        RhythmTruth truth = makeSteady ("lat-consist", 120.0, 16);
        truth.trueSilenceSpans.push_back (SilenceSpan { 1.08, 1.12 });
        ObservationSeries obs = perfectSeries (truth, 120.0);
        for (double& t : obs.beatTimesSeconds)
            t += 0.100;

        const FixtureMetrics raw =
            scoreFixture (truth, obs, kBeatMatchToleranceSeconds, 0.0);
        CHECK_NEAR (raw.fMeasure, 0.0, 1e-12);
        CHECK (! raw.acquired);
        CHECK_EQ (raw.falseBeatsInTrueSilence, 1);

        const FixtureMetrics halfComp =
            scoreFixture (truth, obs, kBeatMatchToleranceSeconds, 0.050);
        CHECK_NEAR (halfComp.phaseMeanMs, 50.0, 1e-6);
        CHECK_NEAR (halfComp.fMeasure, 1.0, 1e-9);
        CHECK (halfComp.acquired);
        CHECK_EQ (halfComp.falseBeatsInTrueSilence, 0);

        const FixtureMetrics fullComp =
            scoreFixture (truth, obs, kBeatMatchToleranceSeconds, 0.100);
        CHECK_NEAR (fullComp.phaseMeanMs, 0.0, 1e-6);
        CHECK_NEAR (fullComp.fMeasure, 1.0, 1e-9);
        CHECK (fullComp.acquired);
        CHECK_EQ (fullComp.falseBeatsInTrueSilence, 0);
    }
}

// ---------------------------------------------------------------------------
// 3. Half-time is a distinct metric from plain BPM error.
// ---------------------------------------------------------------------------
//
// SPEC 19 sets a *separate* < 5% target on half/double-time errors, because a
// tracker that locks half-time is wrong in a way a 50% BPM error alone does not
// usefully summarise: the reported tempo is a metrical multiple of the truth.
// This test makes both the dedicated flag and the gate visible.

JAM_TEST (RhythmEvalMetrics, halfTimeDetectionIsDistinctFromPlainBpmError)
{
    RhythmTruth truth = makeSteady ("half", 120.0, 12, 0.5, 4, true);
    ObservationSeries obs = perfectSeries (truth, 60.0);
    std::vector<double> predicted;
    for (std::size_t i = 0; i < truth.beats.size(); i += 2)
        predicted.push_back (truth.beats[i]);
    obs.beatTimesSeconds = predicted;

    const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
    CHECK (m.halfTimeLock);
    CHECK (m.halfDoubleTimeError);
    // The plain BPM metric also fires (0.5 == 50% error), which is why the
    // dedicated metric must exist: it identifies the *kind* of error.
    CHECK_NEAR (m.bpmRelativeError, 0.5, 1e-9);

    const std::vector<FixtureMetrics> one { m };
    const AggregateMetrics agg = aggregateFixtures (one);
    CHECK_EQ (agg.halfDoubleEvaluatedCore, 1);
    CHECK_EQ (agg.halfDoubleErrorsCore, 1);
    CHECK (agg.halfDoubleErrorRateCore >= 0.05);
    CHECK (! agg.gateHalfDouble5Core);
}

// ---------------------------------------------------------------------------
// 3b. The BPM gate really is 2%, not a doubled 4%.
// ---------------------------------------------------------------------------
//
// Running the real corpus exposed that `gateBpm2Core` compared the worst core
// error against 2 * kBpmAgreementFraction (4%) while its name, comment and the
// constant all say 2%. BTrack's 2.34% was being reported as a PASS. This test
// pins the boundary from both sides so the doubling cannot come back.

JAM_TEST (RhythmEvalMetrics, bpmGateUsesTheSpecTwoPercent)
{
    auto gateFor = [] (double lockedBpm)
    {
        RhythmTruth truth = makeSteady ("bpmgate", 120.0, 8, 0.5, 4, true);
        const ObservationSeries obs = perfectSeries (truth, lockedBpm);
        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        return aggregateFixtures (std::vector<FixtureMetrics> { m });
    };

    CHECK (gateFor (121.2).gateBpm2Core);        // 1.0% -> PASS
    CHECK (gateFor (122.3).gateBpm2Core);        // 1.92% -> PASS
    CHECK (! gateFor (122.5).gateBpm2Core);      // 2.08% -> FAIL
    CHECK (! gateFor (123.6).gateBpm2Core);      // 3.0% -> FAIL (the old 4% gate passed this)
}

// ---------------------------------------------------------------------------
// 4. Ramp scoring uses the non-uniform ground-truth grid.
// ---------------------------------------------------------------------------
//
// The two ramp fixtures' `beats` arrays follow the analytic integral of a
// linear tempo curve, so they are genuinely non-uniform. Scoring them against a
// single nominal BPM would make every tempo-drift number meaningless. Here the
// accelerando ground-truth grid scores ~0, and a constant nominal-BPM grid
// scores large, which can only happen if the real grid is being used.

JAM_TEST (RhythmEvalMetrics, rampScoringUsesTheNonUniformGroundTruthGrid)
{
    const std::string dir = corpusDir();
    REQUIRE (! dir.empty());
    const Manifest manifest = readManifestFile (joinPath (dir, "manifest.json"));

    const ManifestFixture* accelerando = nullptr;
    for (const ManifestFixture& f : manifest.fixtures)
        if (f.name == "accelerando")
            accelerando = &f;
    REQUIRE (accelerando != nullptr);

    const RhythmTruth truth = toTruth (*accelerando);
    REQUIRE (truth.isRamp());
    REQUIRE (truth.beats.size() >= 8);

    // Predictions on the actual (non-uniform) grid.
    const ObservationSeries onGrid = perfectSeries (truth);
    const FixtureMetrics gridScore = scoreFixture (truth, onGrid, kBeatMatchToleranceSeconds);
    CHECK (gridScore.hasRamp);
    CHECK_NEAR (gridScore.rampLocalTempoRelErrorMean, 0.0, 1e-9);
    CHECK_NEAR (gridScore.rampLocalTempoRelErrorWorst, 0.0, 1e-9);

    // Predictions on a constant nominal-BPM grid (the mean of start and end).
    const double meanBpm = 0.5 * (truth.bpmStart + truth.bpmEnd);
    REQUIRE (meanBpm > 0.0);
    const double period = 60.0 / meanBpm;
    ObservationSeries flat;
    flat.audioDurationSeconds = truth.durationSeconds;
    flat.sampleRate = truth.sampleRate;
    for (double t = truth.beats.front(); t <= truth.beats.back() + 0.5 * period; t += period)
    {
        flat.beatTimesSeconds.push_back (t);
        TempoSample s;
        s.timeSeconds = t;
        s.bpm = meanBpm;
        s.phaseValid = true;
        flat.tempoSamples.push_back (s);
    }

    const FixtureMetrics flatScore = scoreFixture (truth, flat, kBeatMatchToleranceSeconds);
    CHECK (flatScore.hasRamp);
    CHECK (flatScore.rampLocalTempoRelErrorMean > 0.02);
    CHECK (flatScore.rampLocalTempoRelErrorMean > gridScore.rampLocalTempoRelErrorMean);
}

// ---------------------------------------------------------------------------
// 5. Boundary conditions stay finite.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, boundaryConditionsAreFinite)
{
    // Empty predictions against a real grid.
    {
        RhythmTruth truth = makeSteady ("emptyPred", 120.0, 8);
        const ObservationSeries obs;   // no beats, no tempo samples
        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK (allFinite (m));
        CHECK_EQ (m.predictedBeats, 0);
        CHECK_NEAR (m.recall, 0.0, 1e-12);
        CHECK_NEAR (m.precision, 1.0, 1e-12);
        CHECK_NEAR (m.fMeasure, 0.0, 1e-12);
    }

    // More predictions than truth beats.
    {
        RhythmTruth truth = makeSteady ("manyPred", 120.0, 4);
        ObservationSeries obs = perfectSeries (truth, 120.0);
        for (int k = 0; k < 20; ++k)
            obs.beatTimesSeconds.push_back (10.0 + 0.01 * k);
        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK (allFinite (m));
        CHECK_NEAR (m.recall, 1.0, 1e-12);
        CHECK (m.precision < 0.5);
    }

    // A single ground-truth beat (shorter than the lock run).
    {
        RhythmTruth truth = makeSteady ("single", 120.0, 1);
        ObservationSeries obs = perfectSeries (truth, 120.0);
        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK (allFinite (m));
        CHECK_NEAR (m.fMeasure, 1.0, 1e-12);
        CHECK (! m.acquired);
    }

    // Duplicate prediction timestamps.
    {
        RhythmTruth truth = makeSteady ("dupes", 120.0, 4);
        ObservationSeries obs = perfectSeries (truth, 120.0);
        obs.beatTimesSeconds.insert (obs.beatTimesSeconds.begin(), truth.beats[0]);
        const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK (allFinite (m));
        CHECK (m.predictedBeats > m.truePositives);
    }

    // Empty ground truth, with and without predictions.
    {
        RhythmTruth truth = makeSteady ("noTruth", 120.0, 0);
        ObservationSeries obs;
        obs.beatTimesSeconds = { 0.5, 1.0, 1.5 };
        const FixtureMetrics withPred =
            scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
        CHECK (allFinite (withPred));
        const FixtureMetrics empty =
            scoreFixture (truth, ObservationSeries(), kBeatMatchToleranceSeconds);
        CHECK (allFinite (empty));
    }
}

// ---------------------------------------------------------------------------
// 6. Determinism, including serialisation.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, scoringAndSerialisationAreDeterministic)
{
    RhythmTruth truth = makeSteady ("det", 126.0, 20, 0.35, 4, true);
    ObservationSeries obs = perfectSeries (truth, 126.0);
    // A non-trivial input: drop some beats and jitter others.
    obs.beatTimesSeconds.erase (obs.beatTimesSeconds.begin() + 5);
    obs.beatTimesSeconds[2] += 0.012;

    const FixtureMetrics a = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
    const FixtureMetrics b = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);

    CHECK_EQ (a.truePositives, b.truePositives);
    CHECK_EQ (a.predictedBeats, b.predictedBeats);
    CHECK (a.fMeasure == b.fMeasure);
    CHECK (a.phaseMeanMs == b.phaseMeanMs);
    CHECK (a.bpmRelativeError == b.bpmRelativeError);

    CHECK_EQ (fixtureMetricsToJson (a).dump(), fixtureMetricsToJson (b).dump());
    CHECK_EQ (fixtureMetricsCsvRow (a), fixtureMetricsCsvRow (b));

    const std::vector<FixtureMetrics> oneA { a };
    const std::vector<FixtureMetrics> oneB { b };
    CHECK_EQ (aggregateMetricsToJson (aggregateFixtures (oneA)).dump(),
              aggregateMetricsToJson (aggregateFixtures (oneB)).dump());
}

// ---------------------------------------------------------------------------
// 7. Manifest parsing: the real corpus round-trips; malformed input is rejected.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, manifestParsingAndMalformedJsonRejection)
{
    const std::string dir = corpusDir();
    REQUIRE (! dir.empty());

    const Manifest manifest = readManifestFile (joinPath (dir, "manifest.json"));
    CHECK_EQ (manifest.schemaVersion, 1);
    CHECK_EQ (manifest.fixtures.size(), static_cast<std::size_t> (19));
    CHECK (manifest.beatToleranceSeconds > 0.0);

    for (const ManifestFixture& f : manifest.fixtures)
    {
        CHECK (! f.name.empty());
        CHECK (! f.file.empty());
        CHECK (! f.beats.empty());
        CHECK (f.beatsPerBar > 0);
        CHECK (! f.tags.empty());
        CHECK (toTruth (f).isSteady() || toTruth (f).isRamp());
    }

    // Malformed JSON: truncated object.
    {
        bool threw = false;
        try { (void) parseManifest ("{\"fixtures\":[{\"name\":\"x\""); }
        catch (const ManifestError&) { threw = true; }
        CHECK (threw);
    }

    // Valid JSON but a missing required field.
    {
        bool threw = false;
        try { (void) parseManifest ("{\"schemaVersion\":1,\"fixtures\":[{\"name\":\"x\"}]}"); }
        catch (const ManifestError&) { threw = true; }
        CHECK (threw);
    }

    // `trueSilenceSpans` is required: the corpus promises it on every fixture,
    // so silently defaulting it to empty would make the primary silence metric
    // score a corpus it is not the one on disk.
    {
        bool threw = false;
        try { (void) parseManifest (
            "{\"schemaVersion\":1,\"fixtures\":[{\"name\":\"x\",\"file\":\"x\","
            "\"tempoProfile\":\"constant\",\"durationSeconds\":1,\"sampleRate\":48000,"
            "\"meter\":{\"numerator\":4,\"denominator\":4,\"beatsPerBar\":4},"
            "\"beats\":[0.5],\"onsets\":[0.5],\"scenarioTags\":[\"x\"]}]}"); }
        catch (const ManifestError&) { threw = true; }
        CHECK (threw);
    }

    // Trailing content after the top-level value.
    {
        bool threw = false;
        try { (void) parseManifest ("{\"schemaVersion\":1,\"fixtures\":[]} trailing"); }
        catch (const ManifestError&) { threw = true; }
        CHECK (threw);
    }

    // Values must not be coerced: a string where a number is required.
    {
        bool threw = false;
        try { (void) parseManifest ("{\"fixtures\":[{\"name\":\"x\",\"file\":\"x\","
                                    "\"tempoProfile\":\"constant\",\"durationSeconds\":\"1\","
                                    "\"sampleRate\":48000,\"meter\":{\"numerator\":4,"
                                    "\"denominator\":4,\"beatsPerBar\":4},\"beats\":[0.5],"
                                    "\"onsets\":[0.5],\"scenarioTags\":[\"x\"]}]}"); }
        catch (const ManifestError&) { threw = true; }
        CHECK (threw);
    }

    // JSON reader/writer round-trip on a small document.
    {
        const rhythmjson::Value v = rhythmjson::parse ("{\"a\":1,\"b\":[true,null,\"x\"]}");
        CHECK_EQ (v.dump(), std::string ("{\"a\":1,\"b\":[true,null,\"x\"]}"));
    }
}

// ---------------------------------------------------------------------------
// 8. EVAL-004: recovery is measured from the REAL stop, not unplayed windows.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, recoveryUsesTrueSilenceStopNotUnplayedWindows)
{
    // A stop_start-style fixture. A narrow unplayed-beat window ends at 1.2 s;
    // the real stop is 4.0..6.0 s. A tracker that stayed locked until the stop
    // and never re-locked must NOT be reported as recovered. The old code took
    // the max end of `silenceSpans` (1.2 s), searched from there, and found the
    // pre-stop lock.
    RhythmTruth truth = makeSteady ("stop_start", 120.0, 17, 0.5);
    truth.silenceSpans.push_back (SilenceSpan { 1.00, 1.20 });
    truth.trueSilenceSpans.clear();
    truth.trueSilenceSpans.push_back (SilenceSpan { 4.00, 6.00 });
    truth.onsets.clear();
    for (const double b : truth.beats)
        if (b < 4.0 || b >= 6.0)
            truth.onsets.push_back (b);

    ObservationSeries beforeOnly;
    beforeOnly.audioDurationSeconds = truth.durationSeconds;
    beforeOnly.sampleRate = truth.sampleRate;
    for (const double b : truth.beats)
    {
        if (b >= 4.0)
            break;
        beforeOnly.beatTimesSeconds.push_back (b);
        TempoSample s;
        s.timeSeconds = b;
        s.bpm = 120.0;
        s.phaseValid = true;
        beforeOnly.tempoSamples.push_back (s);
    }

    const FixtureMetrics noRecovery =
        scoreFixture (truth, beforeOnly, kBeatMatchToleranceSeconds);
    CHECK (! noRecovery.hasRecovery);

    // A lock that begins after the real stop is a genuine recovery, measured
    // from the stop end to the first onset after it (6.0 + 0.5 = 6.5).
    ObservationSeries after;
    after.audioDurationSeconds = truth.durationSeconds;
    after.sampleRate = truth.sampleRate;
    for (const double b : truth.beats)
    {
        if (b < 6.5)
            continue;
        after.beatTimesSeconds.push_back (b);
        TempoSample s;
        s.timeSeconds = b;
        s.bpm = 120.0;
        s.phaseValid = true;
        after.tempoSamples.push_back (s);
    }

    const FixtureMetrics recovered =
        scoreFixture (truth, after, kBeatMatchToleranceSeconds);
    CHECK (recovered.hasRecovery);
    CHECK_NEAR (recovered.recoverySeconds, 0.5, 1e-9);
}

// ---------------------------------------------------------------------------
// 9. EVAL-004: true-silence coverage is a per-fixture property, not a duration
//    threshold, and no silence is NOT MEASURED rather than a trivial pass.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, silenceCoverageIsNotADurationThreshold)
{
    auto coverageOf = [] (const std::string& name, double silenceSeconds,
                          double duration)
    {
        RhythmTruth t = makeSteady (name, 120.0, 8);
        t.trueSilenceSpans.clear();
        if (silenceSeconds > 0.0)
            t.trueSilenceSpans.push_back (SilenceSpan { 0.0, silenceSeconds });
        t.durationSeconds = duration;
        return scoreFixture (t, perfectSeries (t, 120.0), kBeatMatchToleranceSeconds);
    };

    // 60 % true silence, but genuine sparse playing: MEASURED. The EVAL-002R
    // >= 50 % rule censored this.
    const FixtureMetrics genuine = coverageOf ("genuine_sparse", 6.0, 10.0);
    CHECK (genuine.falseBeatCoverage == FalseBeatCoverage::Measured);
    CHECK (genuine.falseBeatMetricInformative);

    // No silence at all: NOT MEASURED.
    const FixtureMetrics none = coverageOf ("no_silence", 0.0, 10.0);
    CHECK (none.falseBeatCoverage == FalseBeatCoverage::NoTrueSilence);
    CHECK (! none.trueSilenceMeasured);
    CHECK (! none.falseBeatMetricInformative);

    // The two known synthesis defects are flagged by name, not by percentage.
    CHECK (coverageOf ("sustained_chords", 8.0, 10.0).falseBeatCoverage
           == FalseBeatCoverage::CorpusDefect);
    CHECK (coverageOf ("tapping_muting_only", 8.0, 10.0).falseBeatCoverage
           == FalseBeatCoverage::CorpusDefect);
}

// ---------------------------------------------------------------------------
// 10. EVAL-004: a missing BPM lock on a core fixture cannot let the gate pass.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, missingBpmLockCannotPassTheCoreGate)
{
    RhythmTruth a = makeSteady ("coreA", 120.0, 8, 0.5, 4, true);
    RhythmTruth b = makeSteady ("coreB", 120.0, 8, 0.5, 4, true);

    ObservationSeries obsA = perfectSeries (a, 120.0);
    ObservationSeries obsB = perfectSeries (b, 120.0);
    obsB.tempoSamples.clear();   // no tempo evidence -> never locks BPM

    const FixtureMetrics ma = scoreFixture (a, obsA, kBeatMatchToleranceSeconds);
    const FixtureMetrics mb = scoreFixture (b, obsB, kBeatMatchToleranceSeconds);
    CHECK (ma.hasBpmLock);
    CHECK (! mb.hasBpmLock);

    const AggregateMetrics agg = aggregateFixtures (std::vector<FixtureMetrics> { ma, mb });
    CHECK_EQ (agg.coreFixtures, 2);
    CHECK_EQ (agg.bpmCoreLockedFixtures, 1);
    CHECK (! agg.bpmCoreAllLocked);
    CHECK (! agg.gateBpm2Core);   // NOT a pass on the one locked fixture
}

// ---------------------------------------------------------------------------
// 11. EVAL-004: zero phase with no matched beats is missing, not perfect.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, phaseIsUndefinedWhenNoBeatMatches)
{
    RhythmTruth truth = makeSteady ("nomatch", 120.0, 8);
    ObservationSeries obs = perfectSeries (truth, 120.0);
    for (double& t : obs.beatTimesSeconds)
        t += 0.25;   // half a beat off: no prediction is within 70 ms of a beat

    const FixtureMetrics m = scoreFixture (truth, obs, kBeatMatchToleranceSeconds);
    CHECK_EQ (m.truePositives, 0);
    CHECK (! m.phaseMeasured);
    CHECK_EQ (m.phaseMatchedBeats, 0);
    CHECK_NEAR (m.phaseP95AbsMs, 0.0, 1e-12);   // the raw field is zero ...
    CHECK (fixtureMetricsToJson (m).dump().find ("\"phaseMeasured\":false")
           != std::string::npos);               // ... but it is labelled missing
}

// ---------------------------------------------------------------------------
// 12. EVAL-004: the silence-acceleration diagnostic distinguishes a measured
//     tempo increase from insufficient evidence.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmEvalMetrics, silenceAccelerationDistinguishesInsufficientEvidence)
{
    RhythmTruth truth = makeSteady ("accel", 120.0, 8);
    truth.trueSilenceSpans.clear();
    truth.trueSilenceSpans.push_back (SilenceSpan { 2.0, 2.4 });

    ObservationSeries none;
    none.beatTimesSeconds = truth.beats;   // no tempo samples at all

    const FixtureMetrics m0 = scoreFixture (truth, none, kBeatMatchToleranceSeconds);
    CHECK (! m0.silenceAccelerationMeasured);
    CHECK (m0.silenceAccelerationInsufficientEvidence);
    CHECK_EQ (m0.silenceSpansInsufficientEvidence, 1);
    CHECK_NEAR (m0.maxSilenceTempoIncreaseBpm, 0.0, 1e-12);

    ObservationSeries up;
    up.beatTimesSeconds = truth.beats;
    TempoSample before;
    before.timeSeconds = 1.5;
    before.bpm = 120.0;
    before.phaseValid = true;
    TempoSample after;
    after.timeSeconds = 3.0;
    after.bpm = 126.0;
    after.phaseValid = true;
    up.tempoSamples.push_back (before);
    up.tempoSamples.push_back (after);

    const FixtureMetrics m1 = scoreFixture (truth, up, kBeatMatchToleranceSeconds);
    CHECK (m1.silenceAccelerationMeasured);
    CHECK (! m1.silenceAccelerationInsufficientEvidence);
    CHECK_NEAR (m1.maxSilenceTempoIncreaseBpm, 6.0, 1e-9);
}
