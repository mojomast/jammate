// Deterministic method tests for the TRACK-008 confirmation-gated median.
//
// These tests exercise the decorator in isolation with a scripted inner backend
// (no audio, no scorer, no corpus). They pin: startup fallback, the exact
// confirmation arithmetic (post-ready jitter), median/outlier behaviour, missing
// vs measured intervals, malformed / gap / out-of-order / non-causal / invalid
// frame resets, sample-clock and rate semantics, confirmation persistence until a
// reset, and bit-exact forwarding of every other observation field.
//
// Build: tools/tempo-stability/build.sh; run: <out>/TempoStableTests.

#include "TempoStable.h"

#include "jam/IRhythmTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

using namespace tempo_stable;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check (bool condition, const char* message)
{
    ++g_checks;
    if (! condition)
    {
        ++g_failures;
        std::printf ("FAIL: %s\n", message);
    }
}

bool approx (double a, double b, double tol = 1.0e-6)
{
    return std::fabs (a - b) <= tol * std::max (1.0, std::fabs (b));
}

/** Scripted inner backend. The decorator owns it through a no-op deleter so the
    test keeps a raw pointer for scripting. */
struct FakeBackend : jam::IRhythmTracker
{
    bool     beat = false;
    double   baseBpm = 0.0;
    bool     overrideInput = false;
    std::uint64_t inputSampleTime = 0;
    float    transientDensity = 0.0f;
    float    confidence = 0.0f;
    bool     silence = false;
    bool     phaseValid = true;
    double   sourceRate = 48000.0;

    int resets = 0;
    double lastRate = 0.0;

    void reset (double rate) override { ++resets; lastRate = rate; }
    const char* id() const noexcept override { return "fake-btrack"; }

    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        jam::RhythmObservation o;
        o.inputSampleTime = overrideInput ? inputSampleTime : frame.sampleTime;
        o.sourceSampleRate = sourceRate;
        o.bpmCandidate = static_cast<float> (baseBpm);
        o.beatEvent = beat;
        o.silence = silence;
        o.phaseValid = phaseValid;
        o.transientDensity01 = transientDensity;
        o.beatConfidence01 = confidence;
        o.onsetStrength01 = transientDensity;
        return o;
    }
};

struct Harness
{
    FakeBackend* fake = nullptr;
    std::unique_ptr<TempoStableTracker> tracker;

    Harness()
    {
        fake = new FakeBackend();
        using Ptr = std::unique_ptr<jam::IRhythmTracker, void (*) (jam::IRhythmTracker*)>;
        Ptr up (fake, [] (jam::IRhythmTracker*) {});
        tracker.reset (new TempoStableTracker (std::move (up)));
    }
    ~Harness() { tracker.reset(); delete fake; }

    jam::AnalysisFrame frame (std::uint64_t sampleTime, std::uint32_t n = 128) const
    {
        jam::AnalysisFrame f;
        f.sampleTime = sampleTime;
        f.sourceSampleRate = 48000.0;
        f.numSamples = n;
        return f;
    }

    /** Feed one block whose inner backend emits a beat at `eventSample`. */
    double beat (std::uint64_t eventSample, double baseBpm, std::uint32_t n = 128)
    {
        fake->beat = true;
        fake->baseBpm = baseBpm;
        fake->overrideInput = true;
        fake->inputSampleTime = eventSample;
        const jam::RhythmObservation o = tracker->process (frame (eventSample, n));
        return static_cast<double> (o.bpmCandidate);
    }

    /** Feed one block with no beat. */
    double quiet (std::uint64_t sampleTime, double baseBpm, std::uint32_t n = 128)
    {
        fake->beat = false;
        fake->baseBpm = baseBpm;
        fake->overrideInput = false;
        const jam::RhythmObservation o = tracker->process (frame (sampleTime, n));
        return static_cast<double> (o.bpmCandidate);
    }
};

constexpr std::uint64_t kBeat = 24000;   // 0.5 s at 48 kHz -> 120 BPM

// ---------------------------------------------------------------------------
// Startup, ring fill and confirmation timing
// ---------------------------------------------------------------------------
void testStartupAndConfirmationTiming()
{
    Harness h;
    h.tracker->reset (48000.0);
    const double base = 100.0;

    // Beats 1..4: ring not full -> base forwarded.
    check (h.beat (0, base) == base, "pre-ring beat1 forwards base");
    check (h.beat (kBeat, base) == base, "ring1 forwards base");
    check (h.beat (2 * kBeat, base) == base, "ring2 forwards base");
    check (h.beat (3 * kBeat, base) == base, "ring3 forwards base");

    // Beat 5: ring full, first derived, no confirmation yet -> base.
    check (h.beat (4 * kBeat, base) == base, "first full-ring beat forwards base");
    check (h.tracker->ringFull(), "ring full at beat5");
    check (h.tracker->stableCount() == 0, "no agreement yet at beat5");
    check (! h.tracker->confirmed(), "not confirmed at beat5");

    // Beat 6: one agreement.
    check (h.beat (5 * kBeat, base) == base, "beat6 forwards base");
    check (h.tracker->stableCount() == 1, "stableCount 1 at beat6");
    check (! h.tracker->confirmed(), "not confirmed at beat6");

    // Beat 7: two agreements.
    check (h.beat (6 * kBeat, base) == base, "beat7 forwards base");
    check (h.tracker->stableCount() == 2, "stableCount 2 at beat7");
    check (! h.tracker->confirmed(), "not confirmed at beat7");

    // Beat 8: three agreements -> confirmed, derived emitted (120).
    const double e8 = h.beat (7 * kBeat, base);
    check (h.tracker->confirmed(), "confirmed at beat8");
    check (approx (e8, 120.0), "beat8 emits derived 120");
    const double e9 = h.beat (8 * kBeat, base);
    check (approx (e9, 120.0), "beat9 emits derived 120");
}

// ---------------------------------------------------------------------------
// Post-ready jitter: an agreement-breaking update resets the counter
// ---------------------------------------------------------------------------
void testJitterResetsConfirmationCounter()
{
    Harness h;
    h.tracker->reset (48000.0);
    const double base = 100.0;

    h.beat (0, base);
    h.beat (kBeat, base);
    h.beat (2 * kBeat, base);
    h.beat (3 * kBeat, base);
    h.beat (4 * kBeat, base);          // ring full, prev=120, count0
    h.beat (5 * kBeat, base);          // count1
    check (h.tracker->stableCount() == 1, "one agreement before jitter");

    // First 0.6 s interval does not move the median of four yet -> agrees.
    h.beat (5 * kBeat + 28800, base);  // ring [.5,.5,.5,.6], d=120 -> count2
    check (h.tracker->stableCount() == 2, "first shifted interval still agrees");

    // Second 0.6 s interval moves the median to 0.55 s (109.09) -> >2 % jump.
    const double e = h.beat (5 * kBeat + 2 * 28800, base);
    check (h.tracker->stableCount() == 0, "disagreement resets stableCount");
    check (! h.tracker->confirmed(), "still not confirmed after jitter");
    check (e == base, "jittered beat forwards base");
}

// ---------------------------------------------------------------------------
// Median is robust to a single in-window outlier
// ---------------------------------------------------------------------------
void testMedianRobustToOutlier()
{
    Harness h;
    h.tracker->reset (48000.0);
    const double base = 100.0;

    h.beat (0, base);
    h.beat (kBeat, base);
    h.beat (2 * kBeat, base);
    h.beat (3 * kBeat, base);
    // Add a short interval 0.30 s (accepted, >= 0.25) as the newest sample.
    // Ring becomes [0.5,0.5,0.5,0.30]; median is still 0.5 -> derived 120.
    const double e = h.beat (3 * kBeat + 14400, base);
    check (e == base, "outlier before confirmation forwards base");
    // One interval should not move the median.
    // Ring [0.5,0.5,0.3,0.5] and [0.5,0.3,0.5,0.5] etc. all median 0.5.
    h.beat (3 * kBeat + 14400 + kBeat, base);
    h.beat (3 * kBeat + 14400 + 2 * kBeat, base);
    check (h.tracker->stableCount() >= 1, "outlier does not force a disagreement");
}

// ---------------------------------------------------------------------------
// Missing vs measured intervals
// ---------------------------------------------------------------------------
void testFirstBeatIsMissing()
{
    Harness h;
    h.tracker->reset (48000.0);
    h.beat (5000, 100.0);
    // No public accessor for the record; assert via behaviour: the next interval
    // is computed from the first beat, so ring becomes 1 only if measured.
    check (h.tracker->ringCount() == 0, "first beat adds no interval");
    h.beat (5000 + kBeat, 100.0);
    check (h.tracker->ringCount() == 1, "second beat adds one interval");
}

void testDuplicateAndNonMonotonicReset()
{
    Harness h;
    h.tracker->reset (48000.0);
    h.beat (0, 100.0);
    h.beat (kBeat, 100.0);
    check (h.tracker->ringCount() == 1, "one interval after two beats");

    // Duplicate sample time -> out-of-order reset (checked before subtraction).
    h.beat (kBeat, 100.0);
    check (h.tracker->ringCount() == 0, "duplicate resets ring");

    // Rebuild then go backwards.
    h.beat (2 * kBeat, 100.0);
    check (h.tracker->ringCount() == 1, "rebuilt one interval");
    h.beat (kBeat + 100, 100.0);       // backwards
    check (h.tracker->ringCount() == 0, "non-monotonic resets ring");
}

// ---------------------------------------------------------------------------
// Malformed / gap resets
// ---------------------------------------------------------------------------
void testGapReset()
{
    Harness h;
    h.tracker->reset (48000.0);
    h.beat (0, 100.0);
    h.beat (kBeat, 100.0);
    h.beat (2 * kBeat, 100.0);
    check (h.tracker->ringCount() == 2, "two intervals before gap");
    // 2.0 s > 1.50 s max -> gap reset.
    h.beat (2 * kBeat + 2 * 48000, 100.0);
    check (h.tracker->ringCount() == 0, "gap resets ring");
}

void testMalformedSubMinimumReset()
{
    Harness h;
    h.tracker->reset (48000.0);
    h.beat (0, 100.0);
    h.beat (kBeat, 100.0);
    check (h.tracker->ringCount() == 1, "one interval before malformed");
    // 0.10 s < 0.25 s min -> malformed reset.
    h.beat (kBeat + 4800, 100.0);
    check (h.tracker->ringCount() == 0, "sub-minimum resets ring");
}

void testNonCausalReset()
{
    Harness h;
    h.tracker->reset (48000.0);
    h.beat (0, 100.0);
    // Event sample beyond the block end (blockStart + numSamples) -> malformed.
    h.fake->beat = true;
    h.fake->baseBpm = 100.0;
    h.fake->overrideInput = true;
    h.fake->inputSampleTime = 5000;         // frame is [1000,1128)
    const jam::RhythmObservation o = h.tracker->process (h.frame (1000));
    check (static_cast<double> (o.bpmCandidate) == 100.0, "non-causal beat forwards base");
    check (h.tracker->ringCount() == 0, "non-causal beat cannot anchor an interval");
}

// ---------------------------------------------------------------------------
// Invalid frame metadata
// ---------------------------------------------------------------------------
void testFrameInvalidReset()
{
    Harness h;
    h.tracker->reset (48000.0);
    h.beat (0, 100.0);
    h.beat (kBeat, 100.0);
    check (h.tracker->ringCount() == 1, "one interval before invalid frame");

    h.fake->beat = true;
    h.fake->baseBpm = 100.0;
    h.fake->overrideInput = true;
    h.fake->inputSampleTime = kBeat + 24000;
    const std::uint32_t oversize =
        static_cast<std::uint32_t> (jam::kMaxAnalysisBlock) + 1u;
    h.tracker->process (h.frame (kBeat + 24000, oversize));
    check (h.tracker->ringCount() == 0, "invalid frame resets ring");
}

// ---------------------------------------------------------------------------
// Sample-clock and rate semantics
// ---------------------------------------------------------------------------
void testRateAffectsInterval()
{
    Harness h;
    h.tracker->reset (44100.0);
    check (h.fake->lastRate == 44100.0, "reset forwards the rate to inner");
    h.beat (0, 100.0);
    h.beat (22050, 100.0);          // 0.5 s at 44.1 kHz
    check (h.tracker->ringCount() == 1, "44.1 kHz interval accepted");
    // Fill and confirm; derived should be 120.
    h.beat (44100, 100.0);
    h.beat (66150, 100.0);
    h.beat (88200, 100.0);
    h.beat (110250, 100.0);
    h.beat (132300, 100.0);
    const double e = h.beat (154350, 100.0);
    check (approx (e, 120.0), "44.1 kHz derived is 120");
}

void testInvalidRateFallsBackButStillForwards()
{
    Harness h;
    h.tracker->reset (-1.0);
    check (h.fake->lastRate == -1.0, "invalid rate still forwarded to inner");
    // Wrapper arithmetic falls back to 48 kHz, so 0.5 s at 48 kHz.
    h.beat (0, 100.0);
    h.beat (24000, 100.0);
    check (h.tracker->ringCount() == 1, "invalid-rate fallback computes interval");
}

// ---------------------------------------------------------------------------
// No octave correction: both half and double time are reported as measured
// ---------------------------------------------------------------------------
void testNoOctaveCorrection()
{
    Harness h;
    h.tracker->reset (48000.0);
    const double base = 100.0;
    // Constant 0.25 s intervals -> 240 BPM, accepted at the boundary.
    for (int i = 0; i < 8; ++i)
        h.beat (static_cast<std::uint64_t> (i) * 12000, base);
    const double e = h.beat (8 * 12000, base);
    check (h.tracker->confirmed(), "240 BPM train confirms");
    check (approx (e, 240.0), "240 BPM reported without halving");
}

// ---------------------------------------------------------------------------
// Confirmation persists until a reset, then fallback resumes
// ---------------------------------------------------------------------------
void testConfirmationPersistsUntilReset()
{
    Harness h;
    h.tracker->reset (48000.0);
    const double base = 100.0;
    for (int i = 0; i < 8; ++i)
        h.beat (static_cast<std::uint64_t> (i) * kBeat, base);
    check (h.tracker->confirmed(), "confirmed on steady train");

    // A one-off short interval (aubio-like jitter) while confirmed: the emitted
    // value still follows the derived median (confirmation is not dropped).
    h.beat (8 * kBeat, base);
    const double e = h.beat (8 * kBeat + 14400, base);   // 0.3 s newest
    check (h.tracker->confirmed(), "stays confirmed through one outlier");
    check (e != base, "confirmed emits a derived value, not stale base");

    // A gap reset drops confirmation and returns to fallback.
    h.beat (8 * kBeat + 14400 + 2 * 48000, base);
    check (! h.tracker->confirmed(), "gap drops confirmation");
    check (h.tracker->ringCount() == 0, "gap clears ring");
    const double e2 = h.beat (8 * kBeat + 14400 + 2 * 48000 + kBeat, base);
    check (e2 == base, "after reset base is forwarded again");
}

// ---------------------------------------------------------------------------
// reset() clears everything and forwards the rate
// ---------------------------------------------------------------------------
void testResetClearsState()
{
    Harness h;
    h.tracker->reset (48000.0);
    const double base = 100.0;
    for (int i = 0; i < 8; ++i)
        h.beat (static_cast<std::uint64_t> (i) * kBeat, base);
    check (h.tracker->confirmed(), "confirmed before reset");
    h.tracker->reset (48000.0);
    check (h.tracker->ringCount() == 0, "reset clears ring");
    check (! h.tracker->confirmed(), "reset clears confirmation");
    check (h.fake->resets == 2, "reset forwarded to inner each time");
    const double e = h.beat (0, base);
    check (e == base, "after reset the first beat forwards base");
}

// ---------------------------------------------------------------------------
// Bit-exact forwarding of every other field
// ---------------------------------------------------------------------------
void testBitExactForwarding()
{
    Harness h;
    h.tracker->reset (48000.0);
    h.fake->beat = true;
    h.fake->baseBpm = 0.0;
    h.fake->overrideInput = true;
    h.fake->inputSampleTime = 777;
    h.fake->transientDensity = 0.625f;
    h.fake->confidence = 0.375f;
    h.fake->silence = true;
    h.fake->phaseValid = false;
    const jam::RhythmObservation o = h.tracker->process (h.frame (777));

    check (o.inputSampleTime == 777, "inputSampleTime forwarded");
    check (o.transientDensity01 == 0.625f, "transientDensity01 forwarded bit-exact");
    check (o.beatConfidence01 == 0.375f, "beatConfidence01 forwarded bit-exact");
    check (o.silence == true, "silence forwarded");
    check (o.phaseValid == false, "phaseValid forwarded");
    // Startup fallback: the inner's own bpm (0.0) is forwarded unchanged.
    check (o.bpmCandidate == 0.0f, "unconfirmed candidate forwarded bit-exact");
}

} // namespace

int main()
{
    testStartupAndConfirmationTiming();
    testJitterResetsConfirmationCounter();
    testMedianRobustToOutlier();
    testFirstBeatIsMissing();
    testDuplicateAndNonMonotonicReset();
    testGapReset();
    testMalformedSubMinimumReset();
    testNonCausalReset();
    testFrameInvalidReset();
    testRateAffectsInterval();
    testInvalidRateFallsBackButStillForwards();
    testNoOctaveCorrection();
    testConfirmationPersistsUntilReset();
    testResetClearsState();
    testBitExactForwarding();

    std::printf ("TempoStableTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
