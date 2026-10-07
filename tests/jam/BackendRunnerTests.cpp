// EVAL-004 BackendRunner timing tests: suite BackendRunner.
//
// These pin the two defects the EVAL-004 brief calls out:
//
//   1. The pre-EVAL-004 runner OVERWROTE `obs.inputSampleTime` with the frame
//      start (BackendRunner.cpp old lines 180-185), destroying a backend's
//      reported beat device time (aubio's sub-hop tactus position, BTrack's
//      analysis-hop start). The runner must instead preserve a causal reported
//      device timestamp — including zero — and fall back to the block start only
//      when the report is non-causal.
//   2. The causal availability (when `process()` returned, i.e. the block end)
//      is a different quantity from the event time and must be carried as its
//      own series, never invented from the event time.
//
// The runner is pure and takes a `jam::IRhythmTracker`, so a scripted backend
// can place an event at any exact sample and the resulting series can be
// inspected without touching audio. `Metrics.cpp` is compiled into the
// RhythmEvalMetrics test translation unit in the same `jamTests` binary; this
// file only includes `BackendRunner.cpp` and links against those symbols.

#include "JamTest.h"

#include "../../tools/rhythm-eval/BackendRunner.cpp"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace
{

using namespace jamtest;
using namespace rhythmeval;

/** A backend whose per-block output is supplied by a scripted function. */
struct ScriptedBackend : jam::IRhythmTracker
{
    using Fn = std::function<jam::RhythmObservation (const jam::AnalysisFrame&,
                                                     std::size_t blockIndex)>;

    explicit ScriptedBackend (Fn fn, double rate = 48000.0)
        : fn_ (std::move (fn)), rate_ (rate) {}

    void reset (double) override { index_ = 0; }

    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        jam::RhythmObservation obs = fn_ (frame, index_);
        ++index_;
        if (! (obs.sourceSampleRate > 0.0))
            obs.sourceSampleRate = rate_;
        return obs;
    }

    const char* id() const noexcept override { return "scripted"; }

private:
    Fn fn_;
    double rate_ = 48000.0;
    std::size_t index_ = 0;
};

WavData makeAudio (std::size_t frames, double rate = 48000.0)
{
    WavData audio;
    audio.samples.assign (frames, 0.0f);
    audio.frames = frames;
    audio.sampleRate = rate;
    return audio;
}

/** A block with no beat event and a valid tempo estimate. */
jam::RhythmObservation quietBlock (double rate, double bpm = 120.0)
{
    jam::RhythmObservation obs;
    obs.sourceSampleRate = rate;
    obs.bpmCandidate = static_cast<float> (bpm);
    obs.phaseValid = true;
    obs.beatEvent = false;
    return obs;
}

const double kRate = 48000.0;

} // namespace

// ---------------------------------------------------------------------------
// 1. An exact backend-reported device time survives; it is not replaced by the
//    block start.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, reportedDeviceTimeIsPreservedNotStampedAtBlockStart)
{
    // Two 128-frame blocks. The second block reports a beat at device sample
    // 100 (inside the first block, so a causal "one hop late" report) while its
    // own block starts at 128. The old runner would have scored 128.
    ScriptedBackend backend ([&] (const jam::AnalysisFrame&, std::size_t block)
    {
        jam::RhythmObservation obs = quietBlock (kRate);
        if (block == 1)
        {
            obs.beatEvent = true;
            obs.inputSampleTime = 100;
        }
        return obs;
    });

    BackendRunner runner (128);
    const ObservationSeries series = runner.run (backend, makeAudio (256));

    REQUIRE (series.beatTimesSeconds.size() == 1);
    CHECK_NEAR (series.beatTimesSeconds[0], 100.0 / kRate, 1e-12);
    CHECK (series.beatTimesSeconds[0] != 128.0 / kRate);

    // Availability is the block end (sample 256), NOT the event time and NOT the
    // block start.
    REQUIRE (series.beatAvailabilitySeconds.size() == 1);
    CHECK_NEAR (series.beatAvailabilitySeconds[0], 256.0 / kRate, 1e-12);

    CHECK_EQ (series.diagnostics.beatEvents, static_cast<std::size_t> (1));
    CHECK_EQ (series.diagnostics.beatsReportedByBackend, static_cast<std::size_t> (1));
    CHECK_EQ (series.diagnostics.beatsAtBlockStart, static_cast<std::size_t> (0));
    CHECK_EQ (series.diagnostics.beatsRejectedNonCausal, static_cast<std::size_t> (0));
    CHECK_NEAR (series.diagnostics.maxReportedAvailabilityLatencySeconds,
                (256.0 - 100.0) / kRate, 1e-12);
}

// ---------------------------------------------------------------------------
// 2. Device sample zero is a valid event time, not an "unset" sentinel.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, sampleZeroIsAValidEventTime)
{
    ScriptedBackend backend ([&] (const jam::AnalysisFrame&, std::size_t block)
    {
        jam::RhythmObservation obs = quietBlock (kRate);
        if (block == 0)
        {
            obs.beatEvent = true;
            obs.inputSampleTime = 0;   // default value, but a real event at t=0
        }
        return obs;
    });

    BackendRunner runner (128);
    const ObservationSeries series = runner.run (backend, makeAudio (256));

    REQUIRE (series.beatTimesSeconds.size() == 1);
    CHECK_NEAR (series.beatTimesSeconds[0], 0.0, 1e-12);
    // It is accepted (not rejected non-causal) and is exactly at the block
    // boundary, so it lands in the block-start bucket, not the reported bucket.
    CHECK_EQ (series.diagnostics.beatsRejectedNonCausal, static_cast<std::size_t> (0));
    CHECK_EQ (series.diagnostics.beatsAtBlockStart, static_cast<std::size_t> (1));
    CHECK_NEAR (series.diagnostics.meanReportedAvailabilityLatencySeconds,
                128.0 / kRate, 1e-12);
}

JAM_TEST (BackendRunner, availabilityMeanIncludesBlockBoundaryEvents)
{
    ScriptedBackend backend ([] (const jam::AnalysisFrame&, std::size_t block)
    {
        auto obs = quietBlock (kRate);
        obs.beatEvent = true;
        obs.inputSampleTime = block == 0 ? 0 : 200;
        return obs;
    });
    const auto series = BackendRunner (128).run (backend, makeAudio (256));
    CHECK_EQ (series.diagnostics.beatsAtBlockStart, static_cast<std::size_t> (1));
    CHECK_EQ (series.diagnostics.beatsReportedByBackend, static_cast<std::size_t> (1));
    CHECK_NEAR (series.diagnostics.meanReportedAvailabilityLatencySeconds,
                (128.0 + 56.0) / (2.0 * kRate), 1e-12);
}

// ---------------------------------------------------------------------------
// 3. The final partial block sets availability to the real end of audio, not a
//    full block past it.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, partialFinalBlockUsesActualAudioEnd)
{
    // 200 frames with 128-frame blocks: block 0 is [0,128), block 1 is [128,200).
    ScriptedBackend backend ([&] (const jam::AnalysisFrame&, std::size_t block)
    {
        jam::RhythmObservation obs = quietBlock (kRate);
        if (block == 1)
        {
            obs.beatEvent = true;
            obs.inputSampleTime = 150;
        }
        return obs;
    });

    BackendRunner runner (128);
    const ObservationSeries series = runner.run (backend, makeAudio (200));

    REQUIRE (series.beatTimesSeconds.size() == 1);
    CHECK_NEAR (series.beatTimesSeconds[0], 150.0 / kRate, 1e-12);
    REQUIRE (series.beatAvailabilitySeconds.size() == 1);
    CHECK_NEAR (series.beatAvailabilitySeconds[0], 200.0 / kRate, 1e-12);
    CHECK_EQ (series.diagnostics.partialFinalBlocks, static_cast<std::size_t> (1));

    // Tempo samples carry both clocks: event = block start, availability = end.
    REQUIRE (series.tempoSamples.size() == 2);
    CHECK (series.tempoSamples[1].hasAvailability);
    CHECK_NEAR (series.tempoSamples[1].timeSeconds, 128.0 / kRate, 1e-12);
    CHECK_NEAR (series.tempoSamples[1].availabilitySeconds, 200.0 / kRate, 1e-12);
}

// ---------------------------------------------------------------------------
// 4. Causality: a beat reported AFTER the block that produced it is rejected.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, nonCausalReportFallsBackToBlockStart)
{
    ScriptedBackend backend ([&] (const jam::AnalysisFrame&, std::size_t block)
    {
        jam::RhythmObservation obs = quietBlock (kRate);
        if (block == 0)
        {
            obs.beatEvent = true;
            obs.inputSampleTime = 200;   // > blockEnd 128: in the future
        }
        return obs;
    });

    BackendRunner runner (128);
    const ObservationSeries series = runner.run (backend, makeAudio (256));

    REQUIRE (series.beatTimesSeconds.size() == 1);
    CHECK_NEAR (series.beatTimesSeconds[0], 0.0, 1e-12);   // fallback to block start
    CHECK_EQ (series.diagnostics.beatsRejectedNonCausal, static_cast<std::size_t> (1));
    CHECK_EQ (series.diagnostics.beatsReportedByBackend, static_cast<std::size_t> (0));
}

// ---------------------------------------------------------------------------
// 5. A declared source rate that does not match the fed rate is counted.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, rateMismatchIsDiagnosed)
{
    ScriptedBackend backend ([&] (const jam::AnalysisFrame&, std::size_t block)
    {
        jam::RhythmObservation obs = quietBlock (kRate);
        obs.sourceSampleRate = 44100.0;   // wrong: frames were fed at 48 kHz
        if (block == 0)
        {
            obs.beatEvent = true;
            obs.inputSampleTime = 64;
        }
        return obs;
    });

    BackendRunner runner (128);
    const ObservationSeries series = runner.run (backend, makeAudio (256));

    CHECK_EQ (series.diagnostics.rateMismatchBlocks, static_cast<std::size_t> (2));
    // The fed clock is authoritative, so the event time is still 64/48000.
    REQUIRE (series.beatTimesSeconds.size() == 1);
    CHECK_NEAR (series.beatTimesSeconds[0], 64.0 / kRate, 1e-12);
}

// ---------------------------------------------------------------------------
// 6. The pre-EVAL-004 defect is reproducible on demand, so its effect can be
//    measured rather than asserted. Default is off.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, legacyBlockStampedBeatsReproducesTheDefect)
{
    auto script = [] (const jam::AnalysisFrame&, std::size_t block)
    {
        jam::RhythmObservation obs = quietBlock (kRate);
        if (block == 1)
        {
            obs.beatEvent = true;
            obs.inputSampleTime = 100;
        }
        return obs;
    };

    ScriptedBackend fixedBackend (script);
    BackendRunner fixed (128, /*legacyBlockStampedBeats=*/false);
    const ObservationSeries fixedSeries = fixed.run (fixedBackend, makeAudio (256));

    ScriptedBackend legacyBackend (script);
    BackendRunner legacy (128, /*legacyBlockStampedBeats=*/true);
    const ObservationSeries legacySeries = legacy.run (legacyBackend, makeAudio (256));

    REQUIRE (fixedSeries.beatTimesSeconds.size() == 1);
    REQUIRE (legacySeries.beatTimesSeconds.size() == 1);
    CHECK_NEAR (fixedSeries.beatTimesSeconds[0], 100.0 / kRate, 1e-12);
    CHECK_NEAR (legacySeries.beatTimesSeconds[0], 128.0 / kRate, 1e-12);
    CHECK (fixedSeries.beatTimesSeconds[0] != legacySeries.beatTimesSeconds[0]);
}

// ---------------------------------------------------------------------------
// 7. Availability is a scoring-independent series: it must not change any
//    metric. This is the "toSeries has no availability knowledge" contract.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, availabilityDoesNotAffectScoring)
{
    RhythmTruth truth;
    truth.name = "avail";
    truth.nominalBpm = 120.0;
    truth.hasNominalBpm = true;
    truth.tempoProfile = "constant";
    truth.beatsPerBar = 4;
    truth.durationSeconds = 4.0;
    for (int i = 0; i < 8; ++i)
        truth.beats.push_back (0.5 + 0.5 * i);

    ObservationSeries a;
    a.audioDurationSeconds = truth.durationSeconds;
    a.sampleRate = kRate;
    for (const double b : truth.beats)
    {
        a.beatTimesSeconds.push_back (b);
        a.beatAvailabilitySeconds.push_back (b + 0.010);   // arbitrary, real
        TempoSample s;
        s.timeSeconds = b;
        s.bpm = 120.0;
        s.phaseValid = true;
        a.tempoSamples.push_back (s);
    }

    ObservationSeries b = a;
    for (double& t : b.beatAvailabilitySeconds)
        t += 5.0;   // wildly different availability

    const FixtureMetrics ma = scoreFixture (truth, a, kBeatMatchToleranceSeconds);
    const FixtureMetrics mb = scoreFixture (truth, b, kBeatMatchToleranceSeconds);

    CHECK_EQ (ma.fMeasure, mb.fMeasure);
    CHECK_EQ (ma.phaseMeanMs, mb.phaseMeanMs);
    CHECK_EQ (ma.acquisitionBars, mb.acquisitionBars);
    CHECK_EQ (ma.falseBeatsInTrueSilence, mb.falseBeatsInTrueSilence);

    // With no block timing there is no availability series, and the consumer
    // must not see a fabricated one.
    ObservationSeries handBuilt;
    CHECK (handBuilt.beatAvailabilitySeconds.empty());
    TempoSample noTiming;
    CHECK (! noTiming.hasAvailability);
}

// ---------------------------------------------------------------------------
// 8. toSeries is deterministic and reports the same diagnostics twice.
// ---------------------------------------------------------------------------

JAM_TEST (BackendRunner, seriesReductionIsDeterministic)
{
    auto script = [] (const jam::AnalysisFrame&, std::size_t block)
    {
        jam::RhythmObservation obs = quietBlock (kRate);
        if (block % 2 == 1)
        {
            obs.beatEvent = true;
            obs.inputSampleTime = static_cast<std::uint64_t> (block) * 128 + 3;
        }
        return obs;
    };

    BackendRunner runner (128);
    ScriptedBackend first (script);
    ScriptedBackend second (script);

    const ObservationSeries a = runner.run (first, makeAudio (128 * 10));
    const ObservationSeries b = runner.run (second, makeAudio (128 * 10));

    CHECK_EQ (a.beatTimesSeconds.size(), b.beatTimesSeconds.size());
    for (std::size_t i = 0; i < a.beatTimesSeconds.size(); ++i)
        CHECK_EQ (a.beatTimesSeconds[i], b.beatTimesSeconds[i]);
    CHECK_EQ (a.diagnostics.beatEvents, b.diagnostics.beatEvents);
    CHECK_EQ (a.diagnostics.beatsReportedByBackend, b.diagnostics.beatsReportedByBackend);
}
