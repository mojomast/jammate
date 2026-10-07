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
        m.silenceSeconds, m.falseBeatsInSilencePerSecond,
        m.falseBeatsOffGridInSilencePerSecond,
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
    // A declared silence span between two beats; a perfect tracker predicts no
    // beat inside it, so the false-beat-in-silence rate must be exactly zero.
    truth.silenceSpans.push_back (SilenceSpan { 3.10, 3.20 });

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
    CHECK_EQ (m.falseBeatsInSilence, 0);
    CHECK_NEAR (m.falseBeatsInSilencePerSecond, 0.0, 1e-12);
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

    // (d) predictions only inside a declared silence span: the false-beat rate
    //     is positive. The span sits between beats so no prediction is the
    //     maintained grid beat; the off-grid diagnostic agrees.
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
        CHECK_EQ (m.falseBeatsInSilence, 2);
        CHECK (m.falseBeatsInSilencePerSecond > 0.0);
        CHECK_EQ (m.falseBeatsOffGridInSilence, 2);
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
