// TraceReplayTests — independent arithmetic tests for the TRACK-004 diagnostic
// trace and acquisition replay. Self-contained (no real backend, no audio
// device, no filesystem); built and run by tools/tracker-diagnostics/build.sh.
//
// These are the tests that make the trace/replay meaningful: they pin the BTrack
// quantiser arithmetic, the lock-run timestamps, the tempo-agreement rejection,
// the event-vs-availability separation, agreement with the real scorer
// (rhythmeval::scoreFixture) on the same series, and that TraceRunner reproduces
// the unmodified BackendRunner on a non-trivial fake backend.

#include "AcquisitionReplay.h"
#include "BackendRunner.h"
#include "BtrackGrid.h"
#include "Metrics.h"
#include "TraceRunner.h"

#include "jam/IRhythmTracker.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

int g_checks = 0;
int g_failures = 0;

void check (bool ok, const std::string& what)
{
    ++g_checks;
    if (! ok)
    {
        ++g_failures;
        std::printf ("FAIL: %s\n", what.c_str());
    }
}

void checkNear (double a, double b, double tol, const std::string& what)
{
    ++g_checks;
    if (! (std::fabs (a - b) <= tol))
    {
        ++g_failures;
        std::printf ("FAIL: %s (got %.12g, want %.12g +/- %g)\n",
                     what.c_str(), a, b, tol);
    }
}

using rhythmeval::ObservationSeries;
using rhythmeval::RhythmTruth;
using rhythmeval::TempoSample;

RhythmTruth constantTruth (const std::string& name, double bpm, int beats, double first,
                           double beatsPerBar = 4.0)
{
    RhythmTruth t;
    t.name = name;
    t.tempoProfile = "constant";
    t.hasNominalBpm = true;
    t.nominalBpm = bpm;
    t.beatsPerBar = static_cast<int> (beatsPerBar);
    const double period = 60.0 / bpm;
    for (int i = 0; i < beats; ++i)
        t.beats.push_back (first + period * i);
    t.durationSeconds = t.beats.back() + period;
    t.sampleRate = 48000.0;
    return t;
}

void addTempo (ObservationSeries& s, double t, double bpm, bool phaseValid = true)
{
    TempoSample ts;
    ts.timeSeconds = t;
    ts.availabilitySeconds = t + 0.002;
    ts.hasAvailability = true;
    ts.bpm = bpm;
    ts.phaseValid = phaseValid;
    s.tempoSamples.push_back (ts);
}

// ---------------------------------------------------------------------------

void testBtrackGridExactValues()
{
    // 60*44100/(512*42) = 123.046875 exactly; (512*41) = 126.048018...
    checkNear (tracker_diag::btrackBpmForLagHops (42, 512, 44100.0), 123.046875, 1e-9,
               "BTrack lag 42 -> 123.046875");
    checkNear (tracker_diag::btrackBpmForLagHops (41, 512, 44100.0),
               2646000.0 / 20992.0, 1e-9, "BTrack lag 41 -> 126.048018");

    const std::vector<tracker_diag::BtrackGridRow> grid = tracker_diag::btrackTempoGrid();
    check (grid.size() == 41, "grid has 41 candidates");
    // Candidate 126 BPM snaps to 41 hops -> 126.05, i.e. 126 IS representable.
    bool found126 = false, found123 = false;
    for (const auto& r : grid)
    {
        if (r.maxIndex == 23)
        {
            checkNear (r.candidateBpm, 126.0, 1e-9, "candidate 23 is 126 BPM");
            check (r.lagHops == 41, "candidate 126 snaps to 41 hops");
            checkNear (r.effectiveBpm, 2646000.0 / 20992.0, 1e-9,
                       "candidate 126 effective 126.049");
            found126 = true;
        }
        // Candidates 122 and 124 both round to lag 42 -> 123.046875.
        if (r.maxIndex == 21)
            found123 = r.lagHops == 42 && std::fabs (r.effectiveBpm - 123.046875) < 1e-9;
        if (r.maxIndex == 22)
            check (r.lagHops == 42 && std::fabs (r.effectiveBpm - 123.046875) < 1e-9,
                   "candidate 124 snaps to 42 hops -> 123.046875");
    }
    check (found126, "126 candidate present and representable");
    check (found123, "122 candidate snaps to 42 hops -> 123.046875");

    tracker_diag::BtrackGridRow near;
    check (tracker_diag::nearestGridCandidate (123.05, near), "nearest candidate exists");
    checkNear (near.effectiveBpm, 123.046875, 1e-9,
               "123.05 is the 123.046875 grid value");
}

void testReplayAcquiresCleanRunWithTimestamps()
{
    const RhythmTruth truth = constantTruth ("clean_eighths", 126.0, 6, 0.0);
    ObservationSeries s;
    for (double b : truth.beats)
    {
        s.beatTimesSeconds.push_back (b);
        s.beatAvailabilitySeconds.push_back (b + 0.020);   // 20 ms later
        addTempo (s, b, 126.0);
    }
    const tracker_diag::LockRun r =
        tracker_diag::replayLock (truth, s, 0.07);
    check (r.found, "clean run acquires");
    check (r.startPredIndex == 0 && r.confirmPredIndex == 3,
           "lock run is beats 0..3");
    checkNear (r.startEventSeconds, 0.0, 1e-12, "lock start event time");
    checkNear (r.confirmEventSeconds, truth.beats[3], 1e-12, "lock confirm event time");
    checkNear (r.startAvailabilitySeconds, 0.020, 1e-12, "lock start availability");
    checkNear (r.confirmAvailabilitySeconds, truth.beats[3] + 0.020, 1e-12,
               "lock confirm availability");
    check (r.longestMatchRun == 6 && r.longestRunWithTempo == 6,
           "full run lengths");
}

void testReplayRejectsTempoDisagreement()
{
    const RhythmTruth truth = constantTruth ("clean_eighths", 126.0, 6, 0.0);
    ObservationSeries s;
    for (std::size_t i = 0; i < truth.beats.size(); ++i)
    {
        s.beatTimesSeconds.push_back (truth.beats[i]);
        s.beatAvailabilitySeconds.push_back (truth.beats[i] + 0.020);
        // BTrack's quantised 123.046875: 2.34 % low, outside the 2 % agreement.
        addTempo (s, truth.beats[i], 123.046875);
    }
    const tracker_diag::LockRun r = tracker_diag::replayLock (truth, s, 0.07);
    check (! r.found, "tempo-disagreeing run does not acquire");
    check (r.longestMatchRun == 6, "beats still line up (match run 6)");
    check (r.longestRunWithTempo == 0, "no tempo-agreeing run");
}

void testReplayRejectsBacktracking()
{
    const RhythmTruth truth = constantTruth ("x", 120.0, 6, 0.0);
    ObservationSeries s;
    // Duplicate the first beat so the truth index does not strictly advance.
    s.beatTimesSeconds = {0.0, 0.0, 0.5, 1.0, 1.5, 2.0};
    for (double b : s.beatTimesSeconds)
    {
        s.beatAvailabilitySeconds.push_back (b + 0.01);
        addTempo (s, b, 120.0);
    }
    const tracker_diag::LockRun r = tracker_diag::replayLock (truth, s, 0.07);
    check (r.found, "a later advancing run still acquires");
    check (r.startPredIndex == 1, "the duplicate cluster is skipped");
}

void testAgreementWithScorer()
{
    // Two series: one that locks, one whose tempo is off by 2.34 %.
    for (int variant = 0; variant < 2; ++variant)
    {
        const RhythmTruth truth = constantTruth ("clean_eighths", 126.0, 8, 0.0);
        ObservationSeries s;
        const double reported = variant == 0 ? 126.0 : 123.046875;
        for (double b : truth.beats)
        {
            s.beatTimesSeconds.push_back (b);
            s.beatAvailabilitySeconds.push_back (b + 0.02);
            addTempo (s, b, reported);
        }
        s.audioDurationSeconds = truth.durationSeconds;
        const rhythmeval::FixtureMetrics m =
            rhythmeval::scoreFixture (truth, s, 0.07, 0.0);
        const tracker_diag::FixtureDiagnosis d =
            tracker_diag::diagnoseFixture (truth, s, m, 0.07);
        check (d.agreesWithScorer,
               "replay agrees with scoreFixture (variant " + std::to_string (variant) + ")");
        check (d.lock.found == m.acquired, "replay.acquired == scorer.acquired");
        if (m.acquired)
            checkNear (d.lock.acquisitionBars, m.acquisitionBars, 1e-9,
                       "replay bars == scorer bars");
        if (variant == 1)
            check (d.reason == tracker_diag::AcquireReason::LockTempoAgreementFailure,
                   "123.05 on 126 classifies as LockTempoAgreementFailure");
    }
}

// --- fake backend so TraceRunner can be compared with BackendRunner ---------

class FakeBackend : public jam::IRhythmTracker
{
public:
    void reset (double sampleRate) override { rate_ = sampleRate; next_ = 512; }
    const char* id() const noexcept override { return "fake"; }
    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        jam::RhythmObservation o;
        o.sourceSampleRate = rate_;
        o.inputSampleTime = frame.sampleTime;
        const std::uint64_t end = frame.sampleTime + frame.numSamples;
        o.bpmCandidate = 120.0f;
        o.phaseValid = frame.sampleTime % 256 < 128;
        o.beatConfidence01 = 0.5f;
        o.onsetStrength01 = 0.25f;
        o.energyRmsDbfs = -30.0f;
        o.silence = (frame.sampleTime / 128) % 7 == 6;
        if (frame.sampleTime < next_ && next_ <= end)
        {
            o.beatEvent = true;
            o.inputSampleTime = next_ - 1;   // causally reported, inside the block
            next_ += 512;
        }
        return o;
    }

private:
    double rate_ = 48000.0;
    std::uint64_t next_ = 512;
};

void testTraceRunnerMatchesBackendRunner()
{
    rhythmeval::WavData audio;
    audio.sampleRate = 48000.0;
    audio.frames = 5000;
    audio.samples.assign (audio.frames, 0.01f);

    FakeBackend a, b;
    const tracker_diag::TraceResult tr = tracker_diag::runTrace (a, audio, 128);
    rhythmeval::BackendRunner runner (128, false);
    const ObservationSeries ref = runner.run (b, audio);

    check (tr.series.beatTimesSeconds == ref.beatTimesSeconds,
           "trace beat event times match BackendRunner");
    check (tr.series.beatAvailabilitySeconds == ref.beatAvailabilitySeconds,
           "trace beat availability matches BackendRunner");
    check (tr.series.tempoSamples.size() == ref.tempoSamples.size(),
           "trace tempo sample count matches");
    bool tempoOk = tr.series.tempoSamples.size() == ref.tempoSamples.size();
    for (std::size_t i = 0; tempoOk && i < ref.tempoSamples.size(); ++i)
        tempoOk = std::fabs (tr.series.tempoSamples[i].bpm - ref.tempoSamples[i].bpm) < 1e-12
                  && tr.series.tempoSamples[i].silence == ref.tempoSamples[i].silence
                  && tr.series.tempoSamples[i].phaseValid == ref.tempoSamples[i].phaseValid
                  && tr.series.tempoSamples[i].timeSeconds == ref.tempoSamples[i].timeSeconds;
    check (tempoOk, "trace tempo/silence/phase match BackendRunner");
    check (tr.series.diagnostics.beatEvents == ref.diagnostics.beatEvents
           && tr.series.diagnostics.blocks == ref.diagnostics.blocks,
           "trace diagnostics match BackendRunner");
    check (! tr.series.beatTimesSeconds.empty(), "fake backend emitted beats");
}

} // namespace

int main()
{
    testBtrackGridExactValues();
    testReplayAcquiresCleanRunWithTimestamps();
    testReplayRejectsTempoDisagreement();
    testReplayRejectsBacktracking();
    testAgreementWithScorer();
    testTraceRunnerMatchesBackendRunner();

    std::printf ("TraceReplayTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
