// TempoVariantTests — independent unit replay fixtures for the TRACK-005
// derived-BPM decorator. Self-contained: no real backend, no audio device, no
// filesystem. Built and run by tools/tempo-variant/build.sh.
//
// These tests pin the PREDECLARED method on synthetic observation streams:
//   - steady 126 with a quantised 123.046875 base report -> variant near 126;
//   - the first 4 beats forward the base value (startup causality/fallback);
//   - a single missing beat is inside the window and the 4-median is robust to
//     it; two missing beats in the window move the median;
//   - a gap longer than the declared maximum resets the ring;
//   - a half-time alias is reported as-is (no octave correction) and a
//     double-time train above the declared window resets;
//   - silence and every non-bpm field are forwarded unchanged;
//   - reset() clears the ring; non-causal and non-monotonic events reset;
//   - the derived candidate persists on non-beat blocks once ready.

#include "TempoVariant.h"

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

constexpr double kRate = 44100.0;   // 60/126 * 44100 = 21000 samples exactly
constexpr std::size_t kBlock = 128;

struct FakeObs
{
    std::uint64_t sample = 0;
    bool beat = false;
    float bpm = 0.0f;
    float phase = 0.0f;
    float confidence = 0.0f;
    float onset = 0.0f;
    float rms = -120.0f;
    bool silence = false;
    bool phaseValid = false;
};

class FakeTracker : public jam::IRhythmTracker
{
public:
    std::vector<FakeObs> script;
    std::size_t index = 0;
    double rate = kRate;

    void reset (double sampleRate) override { rate = sampleRate; index = 0; }

    jam::RhythmObservation process (const jam::AnalysisFrame&) override
    {
        jam::RhythmObservation o;
        const FakeObs s = (index < script.size()) ? script[index] : FakeObs {};
        ++index;
        o.sourceSampleRate = rate;
        o.inputSampleTime = s.sample;
        o.beatEvent = s.beat;
        o.bpmCandidate = s.bpm;
        o.beatPhase01 = s.phase;
        o.beatConfidence01 = s.confidence;
        o.onsetStrength01 = s.onset;
        o.energyRmsDbfs = s.rms;
        o.silence = s.silence;
        o.phaseValid = s.phaseValid;
        return o;
    }

    const char* id() const noexcept override { return "fake"; }
};

jam::AnalysisFrame frameAt (std::uint64_t start)
{
    jam::AnalysisFrame f;
    f.sampleTime = start;
    f.sourceSampleRate = kRate;
    f.numSamples = static_cast<std::uint32_t> (kBlock);
    return f;
}

/** Build a one-beat-per-2-block stream: blocks advance by kBlock; a beat lands
    on the block that contains its sample time. bpm/other fields are constant. */
struct Stream
{
    std::vector<FakeObs> obs;
    std::vector<std::uint64_t> blockStarts;
};

Stream makeStream (const std::vector<std::uint64_t>& beatSamples,
                   std::uint64_t lastBlockStart, float baseBpm,
                   const FakeObs& templateObs = {})
{
    Stream s;
    const std::uint64_t endSample = lastBlockStart + kBlock;
    for (std::uint64_t start = 0; start < endSample; start += kBlock)
    {
        s.blockStarts.push_back (start);
        FakeObs o = templateObs;
        o.sample = start;
        o.beat = false;
        o.bpm = baseBpm;
        for (std::uint64_t b : beatSamples)
            if (b >= start && b < start + kBlock) { o.beat = true; o.sample = b; break; }
        s.obs.push_back (o);
    }
    return s;
}

/** Runs a scripted stream through a wrapper, capturing every method record. */
struct RunResult
{
    std::vector<tempo_variant::MethodRecord> records;
};

RunResult drive (FakeTracker* fake, const Stream& stream)
{
    RunResult r;
    tempo_variant::TempoVariantTracker v (
        std::unique_ptr<jam::IRhythmTracker> (fake),
        [&r] (const tempo_variant::MethodRecord& m) { r.records.push_back (m); });
    v.reset (kRate);
    for (std::size_t i = 0; i < stream.obs.size(); ++i)
        v.process (frameAt (stream.blockStarts[i]));
    return r;
}

// ---------------------------------------------------------------------------

void testSteady126QuantisedBase()
{
    const std::uint64_t period = 21000;   // 60/126 s at 44100
    std::vector<std::uint64_t> beats;
    for (int i = 0; i < 10; ++i)
        beats.push_back (1000 + static_cast<std::uint64_t> (i) * period);

    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + kBlock, 123.046875f);
    fake->script = s.obs;

    RunResult r = drive (fake, s);

    // Find the beat blocks.
    std::vector<std::size_t> beatBlocks;
    for (std::size_t i = 0; i < r.records.size(); ++i)
        if (r.records[i].beatEvent) beatBlocks.push_back (i);

    check (beatBlocks.size() == 10, "steady stream has 10 beats");
    // Startup: beats 0..3 forward the base value unchanged.
    for (int i = 0; i < 4 && i < static_cast<int> (beatBlocks.size()); ++i)
        checkNear (r.records[beatBlocks[i]].variantBpm, 123.046875, 1e-6,
                   "startup beat forwards base BPM");
    check (! r.records[beatBlocks[3]].ready, "not ready after 4 beat events");
    // 5th beat event completes 4 intervals -> ready and derived.
    check (r.records[beatBlocks[4]].ready, "ready after 5 beat events");
    checkNear (r.records[beatBlocks[4]].variantBpm, 126.0, 1e-6,
               "steady 126: variant near 126");
    checkNear (r.records[beatBlocks[9]].variantBpm, 126.0, 1e-6,
               "steady 126: later beats still 126");
}

void testDerivedPersistsOnNonBeatBlocks()
{
    const std::uint64_t period = 21000;
    std::vector<std::uint64_t> beats;
    for (int i = 0; i < 6; ++i)
        beats.push_back (1000 + static_cast<std::uint64_t> (i) * period);

    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + 10 * kBlock, 123.046875f);
    fake->script = s.obs;
    RunResult r = drive (fake, s);

    // The final blocks are non-beat; the derived candidate must persist.
    const tempo_variant::MethodRecord& last = r.records.back();
    check (! last.beatEvent, "final block is not a beat");
    check (last.ready, "still ready on non-beat block");
    checkNear (last.variantBpm, 126.0, 1e-6, "derived candidate persists without a beat");
}

void testSingleMissingBeatIsInsideWindowAndMedianRobust()
{
    // intervals: 21000 x3, then one 42000 (one missing beat), then 21000.
    const std::uint64_t p = 21000;
    std::vector<std::uint64_t> beats = {1000, 1000 + p, 1000 + 2 * p, 1000 + 3 * p,
                                        1000 + 5 * p, 1000 + 6 * p, 1000 + 7 * p};
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + kBlock, 123.046875f);
    fake->script = s.obs;
    RunResult r = drive (fake, s);

    std::vector<const tempo_variant::MethodRecord*> beatRecs;
    for (const auto& m : r.records) if (m.beatEvent) beatRecs.push_back (&m);

    // Beat index 4 (0-based) is the one after the missing beat: interval 42000.
    checkNear (beatRecs[4]->intervalSeconds, 42000.0 / kRate, 1e-9,
               "missing beat produces a double interval");
    check (beatRecs[4]->intervalState == tempo_variant::IntervalState::Accepted,
           "double interval is inside the declared window (not a gap)");
    // Ring = {21000,21000,21000,42000}; the 4-median is still 21000 -> 126.
    checkNear (beatRecs[4]->variantBpm, 126.0, 1e-6,
               "single missing beat does not move the 4-median");
}

void testTwoMissingBeatsMoveMedian()
{
    const std::uint64_t p = 21000;
    // intervals 21000, 42000, 42000 -> ring {21000,21000,42000,42000} after 5th beat.
    std::vector<std::uint64_t> beats = {1000, 1000 + p, 1000 + 3 * p, 1000 + 5 * p,
                                        1000 + 6 * p};
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + kBlock, 123.046875f);
    fake->script = s.obs;
    RunResult r = drive (fake, s);

    std::vector<const tempo_variant::MethodRecord*> beatRecs;
    for (const auto& m : r.records) if (m.beatEvent) beatRecs.push_back (&m);
    // ring = {21000,42000,21000,42000} median = 31500 -> 84 BPM (reported raw).
    checkNear (beatRecs[4]->variantBpm, 60.0 / (31500.0 / kRate), 1e-6,
               "two missing beats move the 4-median (raw, no correction)");
}

void testGapResetsRing()
{
    const std::uint64_t p = 21000;
    // A gap of 2 s (> 1.5 s max) resets.
    std::vector<std::uint64_t> beats = {1000, 1000 + p, 1000 + 2 * p, 1000 + 3 * p,
                                        1000 + 4 * p, 1000 + 4 * p + 88200};
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + kBlock, 123.046875f);
    fake->script = s.obs;
    RunResult r = drive (fake, s);

    std::vector<const tempo_variant::MethodRecord*> beatRecs;
    for (const auto& m : r.records) if (m.beatEvent) beatRecs.push_back (&m);
    check (beatRecs.back()->intervalState == tempo_variant::IntervalState::GapReset,
           "2 s gap is a gap reset");
    check (! beatRecs.back()->ready, "gap reset clears readiness");
    checkNear (beatRecs.back()->variantBpm, 123.046875, 1e-6,
               "after a gap reset the base value is forwarded");
}

void testSilenceForwardsAndDoesNotAddIntervals()
{
    const std::uint64_t p = 21000;
    std::vector<std::uint64_t> beats = {1000, 1000 + p, 1000 + 2 * p, 1000 + 3 * p,
                                        1000 + 4 * p};
    FakeTracker* fake = new FakeTracker();
    FakeObs tmpl;
    tmpl.silence = true;
    tmpl.phaseValid = false;
    tmpl.rms = -95.0f;
    tmpl.confidence = 0.0f;
    Stream s = makeStream (beats, beats.back() + 20 * kBlock, 123.046875f, tmpl);
    fake->script = s.obs;
    RunResult r = drive (fake, s);

    // No beats after the 5th -> no interval changes.
    std::vector<const tempo_variant::MethodRecord*> beatRecs;
    for (const auto& m : r.records) if (m.beatEvent) beatRecs.push_back (&m);
    check (beatRecs.size() == 5, "silence stream emits only the scripted beats");
    check (beatRecs.back()->ready, "ready before the silent tail");
    const std::size_t silentTail = r.records.size() - 1;
    check (! r.records[silentTail].beatEvent, "silent tail has no beats");
    checkNear (r.records[silentTail].variantBpm, 126.0, 1e-6,
               "silence retains the stale derived candidate (no new interval)");
}

void testHalfTimeAliasNotCorrected()
{
    // 63 BPM interval = 42000 samples: inside window, reported as 63 (no x2).
    const std::uint64_t p = 42000;
    std::vector<std::uint64_t> beats;
    for (int i = 0; i < 6; ++i) beats.push_back (1000 + static_cast<std::uint64_t> (i) * p);
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + kBlock, 123.046875f);
    fake->script = s.obs;
    RunResult r = drive (fake, s);

    std::vector<const tempo_variant::MethodRecord*> beatRecs;
    for (const auto& m : r.records) if (m.beatEvent) beatRecs.push_back (&m);
    checkNear (beatRecs.back()->variantBpm, 63.0, 1e-6,
               "half-time alias is reported as-is (no octave correction)");
}

void testDoubleTimeAboveWindowResets()
{
    // 252 BPM interval = 10500 samples < 0.25 s min -> malformed reset.
    const std::uint64_t p = 10500;
    std::vector<std::uint64_t> beats;
    for (int i = 0; i < 6; ++i) beats.push_back (1000 + static_cast<std::uint64_t> (i) * p);
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + kBlock, 123.046875f);
    fake->script = s.obs;
    RunResult r = drive (fake, s);

    std::vector<const tempo_variant::MethodRecord*> beatRecs;
    for (const auto& m : r.records) if (m.beatEvent) beatRecs.push_back (&m);
    check (beatRecs.back()->intervalState == tempo_variant::IntervalState::MalformedReset,
           "double-time above the declared window resets");
    check (! beatRecs.back()->ready, "double-time never becomes ready");
    checkNear (beatRecs.back()->variantBpm, 123.046875, 1e-6,
               "double-time falls back to base");
}

void testResetClearsRing()
{
    const std::uint64_t p = 21000;
    std::vector<std::uint64_t> beats;
    for (int i = 0; i < 6; ++i) beats.push_back (1000 + static_cast<std::uint64_t> (i) * p);
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream (beats, beats.back() + kBlock, 123.046875f);
    fake->script = s.obs;

    FakeTracker* raw = new FakeTracker();
    raw->script = s.obs;
    tempo_variant::TempoVariantTracker v {std::unique_ptr<jam::IRhythmTracker> (raw)};
    v.reset (kRate);
    for (std::size_t i = 0; i < s.obs.size(); ++i) v.process (frameAt (s.blockStarts[i]));
    check (v.ready(), "ready before reset");
    v.reset (kRate);
    check (! v.ready(), "reset clears readiness");
    check (v.ringCount() == 0, "reset clears the ring");
}

void testOutOfOrderResets()
{
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream ({1000, 22000, 43000}, 43000 + kBlock, 123.046875f);
    fake->script = s.obs;
    // Explicitly make the third beat go backwards in sample time.
    for (auto& o : fake->script)
        if (o.beat && o.sample == 43000) o.sample = 21000;

    RunResult r = drive (fake, s);
    std::vector<const tempo_variant::MethodRecord*> beatRecs;
    for (const auto& m : r.records) if (m.beatEvent) beatRecs.push_back (&m);
    check (beatRecs.back()->intervalState == tempo_variant::IntervalState::OutOfOrderReset,
           "non-monotonic beat sample resets");
    check (! beatRecs.back()->ready, "out-of-order never becomes ready");
}

void testNonCausalBeatResets()
{
    // Beat reported after the block end cannot anchor an interval.
    FakeTracker* fake = new FakeTracker();
    Stream s = makeStream ({1000, 22000}, 22000 + kBlock, 123.046875f);
    fake->script = s.obs;
    for (auto& o : fake->script)
        if (o.beat && o.sample == 22000) o.sample = 22000 + 10 * kBlock;

    RunResult r = drive (fake, s);
    bool sawMalformed = false;
    for (const auto& m : r.records)
        if (m.beatEvent && m.intervalState == tempo_variant::IntervalState::MalformedReset)
            sawMalformed = true;
    check (sawMalformed, "non-causal beat resets rather than anchoring an interval");
}

void testAllOtherFieldsForwardedUnchanged()
{
    const std::uint64_t p = 21000;
    std::vector<std::uint64_t> beats = {1000, 1000 + p, 1000 + 2 * p, 1000 + 3 * p,
                                        1000 + 4 * p};
    Stream s = makeStream (beats, beats.back() + 2 * kBlock, 123.046875f);

    // Base run: plain FakeTracker.
    FakeTracker* baseFake = new FakeTracker();
    baseFake->script = s.obs;
    std::vector<jam::RhythmObservation> baseOut;
    {
        std::unique_ptr<jam::IRhythmTracker> base (baseFake);
        base->reset (kRate);
        for (std::size_t i = 0; i < s.obs.size(); ++i)
            baseOut.push_back (base->process (frameAt (s.blockStarts[i])));
    }

    // Variant run: wrapper, capturing the raw returned observation.
    FakeTracker* varFake = new FakeTracker();
    varFake->script = s.obs;
    std::vector<jam::RhythmObservation> varOut;
    {
        tempo_variant::TempoVariantTracker v {
            std::unique_ptr<jam::IRhythmTracker> (varFake)};
        v.reset (kRate);
        for (std::size_t i = 0; i < s.obs.size(); ++i)
            varOut.push_back (v.process (frameAt (s.blockStarts[i])));
    }

    bool allEqual = baseOut.size() == varOut.size();
    for (std::size_t i = 0; allEqual && i < baseOut.size(); ++i)
    {
        const auto& a = baseOut[i];
        const auto& b = varOut[i];
        allEqual = a.inputSampleTime == b.inputSampleTime
                   && a.sourceSampleRate == b.sourceSampleRate
                   && a.beatEvent == b.beatEvent
                   && a.silence == b.silence
                   && a.phaseValid == b.phaseValid
                   && std::fabs (a.beatPhase01 - b.beatPhase01) < 1e-9
                   && std::fabs (a.beatConfidence01 - b.beatConfidence01) < 1e-9
                   && std::fabs (a.onsetStrength01 - b.onsetStrength01) < 1e-9
                   && std::fabs (a.energyRmsDbfs - b.energyRmsDbfs) < 1e-9;
    }
    check (allEqual, "every non-bpm field is forwarded unchanged");

    // The bpm field is the only changed one, and only once ready.
    checkNear (varOut[0].bpmCandidate, baseOut[0].bpmCandidate, 1e-9,
               "bpm forwarded before readiness");
    bool sawChanged = false;
    for (std::size_t i = 0; i < baseOut.size(); ++i)
        if (std::fabs (varOut[i].bpmCandidate - baseOut[i].bpmCandidate) > 1e-9)
            sawChanged = true;
    check (sawChanged, "bpm is changed once the ring is ready");
}

void testResetOnRateChange()
{
    // After readiness at 44.1 kHz, reset at 48 kHz clears the ring; the next
    // stream is measured at the new feed rate.
    FakeTracker* fake = new FakeTracker();
    const std::uint64_t p = 21000;
    std::vector<std::uint64_t> beats;
    for (int i = 0; i < 6; ++i) beats.push_back (1000 + static_cast<std::uint64_t> (i) * p);
    fake->script = makeStream (beats, beats.back() + kBlock, 123.046875f).obs;

    tempo_variant::TempoVariantTracker v {std::unique_ptr<jam::IRhythmTracker> (fake)};
    v.reset (kRate);
    for (std::size_t i = 0; i < fake->script.size(); ++i)
        v.process (frameAt (static_cast<std::uint64_t> (i) * kBlock));
    check (v.ready(), "ready at 44.1 kHz before rate change");
    v.reset (48000.0);
    check (! v.ready(), "rate change clears readiness");
    check (v.ringCount() == 0, "rate change clears the ring");
}

void testIdIsDistinct()
{
    FakeTracker* fake = new FakeTracker();
    tempo_variant::TempoVariantTracker v {std::unique_ptr<jam::IRhythmTracker> (fake)};
    check (std::string (v.id()) == "btrack-tempo-variant",
           "variant id is distinct from default 'btrack'");
    check (std::string (v.id()) != "btrack", "variant id is not the default id");
}

} // namespace

int main()
{
    testSteady126QuantisedBase();
    testDerivedPersistsOnNonBeatBlocks();
    testSingleMissingBeatIsInsideWindowAndMedianRobust();
    testTwoMissingBeatsMoveMedian();
    testGapResetsRing();
    testSilenceForwardsAndDoesNotAddIntervals();
    testHalfTimeAliasNotCorrected();
    testDoubleTimeAboveWindowResets();
    testResetClearsRing();
    testResetOnRateChange();
    testOutOfOrderResets();
    testNonCausalBeatResets();
    testAllOtherFieldsForwardedUnchanged();
    testIdIsDistinct();

    std::printf ("TempoVariantTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
