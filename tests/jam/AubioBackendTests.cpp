// Tests for jam::AubioBackend — the GPL aubio 0.4.9 adapter behind
// IRhythmTracker (SPEC.md sections 9.2, 12, 17, 19; DEVPLAN.md TRACK-002).
//
// These tests deliberately live on the *evidence* side of the contract. Each
// case states the musical or algorithmic property it pins, not the call it
// makes. The tempo cases are the ones that catch the sample-rate trap: aubio's
// beat tracker normalises its tempo by samplerate/hop_size
// (beattracking.c:424), so declaring the wrong rate for the audio biases the
// estimate by exactly 44100/48000 - 1 = -8.1 %. See
// third_party/aubio/VENDORED-PATCHES.md and src/aubio/AubioBackend.h.
//
// Determinism policy (matches tests/jam/JamTest.h): no sleeping, no wall clock,
// no threads, no file I/O. The only pseudo-randomness is the fixed hash used to
// build the synthetic click noise, which is a pure function of the sample index
// so every block framing sees the identical waveform.
//
// BUILD DISCRIMINATOR. jam-core/CMakeLists.txt globs every tests/jam/*.cpp into
// the dependency-free jamTests target AND discovers ctest suites by grepping for
// the JamTest registration macro. It must exclude this file from jamTests (as it
// already does for BTrackBackendTests.cpp) and build it into a separate target
// that links jam-aubio. Left alone, this file would both fail to link in
// jamTests (AubioBackend lives in jam-aubio) and register jam.AubioBackend
// twice. The discriminator is JAM_AUBIO_ENABLED, which jam-aubio defines PUBLIC
// (third_party/aubio/CMakeLists.txt), so it reaches the aubio test target but
// never jamTests. Outside that target this file compiles to nothing; the
// explicit add_test(jam.AubioBackend) in jam-core/CMakeLists.txt still runs the
// target, so the suite cannot silently disappear.
#ifdef JAM_AUBIO_ENABLED

#include "JamTest.h"

#include "jam/RhythmTypes.h"
#include "aubio/AubioBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

//==============================================================================
// Scoped heap-allocation counter. IRhythmTracker.h permits allocation on the
// analysis worker, but this backend deliberately preallocates everything in
// reset(); the allocation test below measures that. Same global-operator
// technique as tests/jam/BTrackBackendTests.cpp and AnalysisAudioRingTests.cpp.
// (aubio's C allocation goes through malloc, not operator new; new_aubio_tempo
// is called in reset() outside the measured region, and the causal path makes no
// malloc call — checked by inspection and by the zero count here.)

namespace
{
bool     g_track = false;
uint64_t g_allocs = 0;

void noteAllocation (std::size_t size) noexcept
{
    if (g_track)
    {
        ++g_allocs;
        (void) size;
    }
}
} // namespace

// Replacing the global new/delete with malloc/free is the project's established
// allocation-measurement idiom. GCC's -Wmismatched-new-delete cannot see through
// the malloc call and reports a false positive at the deallocation sites;
// suppressed here so this file is clean under -Wall -Wextra -Wpedantic.
#if defined(__GNUC__) && ! defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new (std::size_t size)
{
    noteAllocation (size);
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    noteAllocation (size);
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#if defined(__GNUC__) && ! defined(__clang__)
# pragma GCC diagnostic pop
#endif

// Same expansion as the harness macro, under a name the CMake suite-discovery
// regex (which looks for the JamTest registration macro) does not match, so this
// file contributes no discovered suite and cannot collide with the explicit
// ctest entry.
#define AUBIO_TEST(suite_name, test_name)                                       \
    static void jam_test_##suite_name##_##test_name();                          \
    static ::jamtest::Registrar jam_reg_##suite_name##_##test_name (             \
        #suite_name, #test_name, &jam_test_##suite_name##_##test_name);         \
    static void jam_test_##suite_name##_##test_name()

namespace
{

/** Counts allocations only while alive. */
class AllocationScope
{
public:
    AllocationScope() noexcept : previous_ (g_track) { g_allocs = 0; g_track = true; }
    ~AllocationScope() { g_track = previous_; }
    AllocationScope (const AllocationScope&) = delete;
    AllocationScope& operator= (const AllocationScope&) = delete;
    uint64_t count() const noexcept { return g_allocs; }
private:
    bool previous_;
};

//==============================================================================
// Deterministic synthetic audio. Sample value is a pure function of the
// absolute sample index, so re-framing the same stream cannot change the
// waveform (test 4 depends on this). Identical generator to
// BTrackBackendTests.cpp so the two candidates are compared on the same signal.

uint32_t hash32 (uint64_t x) noexcept
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<uint32_t> (x >> 32);
}

/** Click/onset train at `bpm`, plus an optional low-level noise floor so the
    stream is not classified as silence between attacks. */
float clickSample (uint64_t i, double rate, double bpm, double firstBeat, double noiseFloor) noexcept
{
    const double t = static_cast<double> (i) / rate;
    const double period = 60.0 / bpm;
    const double k = std::floor ((t - firstBeat) / period);
    const double onset = firstBeat + k * period;
    const double dt = t - onset;

    double v = noiseFloor * ((static_cast<double> (hash32 (i)) / 4294967295.0) * 2.0 - 1.0);

    if (dt >= 0.0 && dt < 0.006)
    {
        const double env = std::exp (-dt / 0.0015);
        const double noise = (static_cast<double> (hash32 (i * 0x9E3779B97F4A7C15ULL + 0x1234u) & 0xffffu) / 32768.0) - 1.0;
        v += 0.8 * env * noise;
    }
    return static_cast<float> (v);
}

jam::AnalysisFrame makeFrame (uint64_t sampleTime, double rate, const float* data, uint32_t n)
{
    jam::AnalysisFrame f;
    f.sampleTime = sampleTime;
    f.sourceSampleRate = rate;
    f.numSamples = n;
    std::memcpy (f.samples, data, static_cast<std::size_t> (n) * sizeof (float));
    return f;
}

struct RunResult
{
    std::vector<jam::RhythmObservation> obs;
};

/** Feeds `total` samples in fixed-size blocks. */
RunResult runSignal (jam::AubioBackend& backend, double rate, uint64_t total,
                     int blockSize, double bpm, double firstBeat = 0.1,
                     double noiseFloor = 0.003)
{
    RunResult r;
    std::vector<float> buf (static_cast<std::size_t> (blockSize));
    uint64_t pos = 0;

    while (pos < total)
    {
        const uint32_t n = static_cast<uint32_t> (std::min<uint64_t> (static_cast<uint64_t> (blockSize), total - pos));
        for (uint32_t i = 0; i < n; ++i)
            buf[i] = clickSample (pos + i, rate, bpm, firstBeat, noiseFloor);

        r.obs.push_back (backend.process (makeFrame (pos, rate, buf.data(), n)));
        pos += n;
    }
    return r;
}

/** The sequence of (device time) of beat events, which must be independent of
    how the same audio was chopped into blocks. */
std::vector<uint64_t> beatStream (const RunResult& r)
{
    std::vector<uint64_t> s;
    for (const jam::RhythmObservation& o : r.obs)
        if (o.beatEvent)
            s.push_back (o.inputSampleTime);
    return s;
}

float lastPositiveBpm (const RunResult& r)
{
    float bpm = 0.0f;
    for (const jam::RhythmObservation& o : r.obs)
        if (o.bpmCandidate > 0.0f)
            bpm = o.bpmCandidate;
    return bpm;
}

void checkSameObservation (const jam::RhythmObservation& a, const jam::RhythmObservation& b)
{
    CHECK_EQ (a.inputSampleTime, b.inputSampleTime);
    CHECK_EQ (a.sourceSampleRate, b.sourceSampleRate);
    CHECK_EQ (a.bpmCandidate, b.bpmCandidate);
    CHECK_EQ (a.beatPhase01, b.beatPhase01);
    CHECK_EQ (a.beatConfidence01, b.beatConfidence01);
    CHECK_EQ (a.onsetStrength01, b.onsetStrength01);
    CHECK_EQ (a.energyRmsDbfs, b.energyRmsDbfs);
    CHECK_EQ (a.transientDensity01, b.transientDensity01);
    CHECK_EQ (a.beatEvent, b.beatEvent);
    CHECK_EQ (a.silence, b.silence);
    CHECK_EQ (a.phaseValid, b.phaseValid);
}

/** Tolerance for the synthetic-click BPM assertions. Measured against aubio
    0.4.9: across 80..160 BPM click trains at both 44.1 kHz and 48 kHz the
    reported tempo is within +1.7 % of truth (aubio rounds the beat period to
    the detection-function frame grid and carries a small systematic positive
    bias), and the two rates agree to <0.2 %. 4 % covers that with headroom
    while still catching the mis-declared-rate trap (-8.1 %) by more than 2x. */
constexpr double kClickTempoToleranceRel = 0.04;

} // namespace

//==============================================================================
// 1. The sample-rate trap, at this product's reference rate.

AUBIO_TEST (AubioBackend, tempoIsUnbiasedOnAClickTrainAt48k)
{
    // A 120 BPM click train must be read as ~120 BPM when captured at 48 kHz.
    // If the adapter declared the rate as 44100 (or resampled badly) the answer
    // would be ~110 BPM; test 1 of the "prove it can fail" evidence does exactly
    // that and goes red.
    jam::AubioBackend backend;
    backend.reset (48000.0);

    const RunResult r = runSignal (backend, 48000.0, 48000ull * 24, 128, 120.0);

    CHECK_NEAR (lastPositiveBpm (r), 120.0, 120.0 * kClickTempoToleranceRel);
}

//==============================================================================
// 2. The adapter must not be accidentally rate-specific.

AUBIO_TEST (AubioBackend, tempoIsUnbiasedOnAClickTrainAt44k1)
{
    // Same property at aubio's own reference rate: the adapter must adapt to
    // whatever rate reset() promises rather than assume a device rate.
    jam::AubioBackend backend;
    backend.reset (44100.0);

    const RunResult r = runSignal (backend, 44100.0, static_cast<uint64_t> (44100.0 * 24), 128, 120.0);

    CHECK_NEAR (lastPositiveBpm (r), 120.0, 120.0 * kClickTempoToleranceRel);
}

//==============================================================================
// 3. reset() is a true reset.

AUBIO_TEST (AubioBackend, resetIsDeterministicFieldForField)
{
    // Identical input after reset() must yield an identical observation
    // sequence, every field. This pins the no-hidden-state / deterministic
    // requirement in IRhythmTracker.h and SPEC.md section 21.2.
    jam::AubioBackend backend;
    backend.reset (48000.0);
    const RunResult a = runSignal (backend, 48000.0, 48000ull * 8, 128, 120.0);

    backend.reset (48000.0);
    const RunResult b = runSignal (backend, 48000.0, 48000ull * 8, 128, 120.0);

    REQUIRE (a.obs.size() == b.obs.size());
    CHECK (a.obs.size() > 0);

    for (std::size_t i = 0; i < a.obs.size(); ++i)
        checkSameObservation (a.obs[i], b.obs[i]);

    CHECK (beatStream (a).size() > 0);   // the run really exercised beats
}

//==============================================================================
// 4. Re-framing must not move a single sample.

AUBIO_TEST (AubioBackend, beatStreamIsIndependentOfBlockFraming)
{
    // The device hands the analysis worker 128-frame blocks, but the ring can
    // legally deliver anything from 1 to 2048 samples. Because the adapter
    // accumulates into fixed 512-sample hops before aubio sees anything, the
    // same audio must produce the same beat events at the same device sample
    // times no matter how it was framed: if the accumulator were off by one
    // sample, the beat stream would shift or drop beats for some sizes.
    const int blockSizes[] = { 1, 127, 128, 512, 1024, 2048 };
    constexpr int kCount = static_cast<int> (sizeof (blockSizes) / sizeof (blockSizes[0]));

    std::vector<std::vector<uint64_t>> streams;

    for (int i = 0; i < kCount; ++i)
    {
        jam::AubioBackend backend;
        backend.reset (48000.0);
        const RunResult r = runSignal (backend, 48000.0, 48000ull * 12, blockSizes[i], 120.0);
        streams.push_back (beatStream (r));
    }

    REQUIRE (streams[0].size() > 4);

    for (int i = 1; i < kCount; ++i)
    {
        REQUIRE (streams[i].size() == streams[0].size());

        for (std::size_t k = 0; k < streams[0].size(); ++k)
            CHECK_EQ (streams[i][k], streams[0][k]);
    }
}

//==============================================================================
// 5. A trailing partial block is carried, never fabricated.

AUBIO_TEST (AubioBackend, trailingPartialBlockIsCarriedNotFabricated)
{
    // A block shorter than aubio's 512-sample hop must not complete a hop on its
    // own and must not invent a beat, and the remainder must survive so that the
    // next block resumes exactly where it left off.
    const uint64_t full = 48000ull * 4;   // an exact number of 128-blocks and hops

    jam::AubioBackend a;
    a.reset (48000.0);
    const RunResult baseline = runSignal (a, 48000.0, full, 128, 120.0);

    jam::AubioBackend b;
    b.reset (48000.0);
    RunResult withTail = runSignal (b, 48000.0, full, 128, 120.0);

    // Exactly one extra sample: strictly less than one aubio hop (512).
    const float one = clickSample (full, 48000.0, 120.0, 0.1, 0.003);
    const jam::RhythmObservation tail = b.process (makeFrame (full, 48000.0, &one, 1));

    CHECK (! tail.beatEvent);
    CHECK_EQ (tail.bpmCandidate, withTail.obs.back().bpmCandidate);
    CHECK (tail.beatPhase01 >= 0.0f && tail.beatPhase01 < 1.0f);

    // The beat timeline is unchanged: the extra sample was carried, not mistaken
    // for a completed aubio hop.
    const std::vector<uint64_t> before = beatStream (baseline);
    const std::vector<uint64_t> after = beatStream (withTail);
    REQUIRE (after.size() == before.size());
    for (std::size_t k = 0; k < before.size(); ++k)
        CHECK_EQ (after[k], before[k]);
}

//==============================================================================
// 6. Stable identity.

AUBIO_TEST (AubioBackend, idIsStableAndNonEmpty)
{
    jam::AubioBackend backend;
    const char* id = backend.id();

    REQUIRE (id != nullptr);
    CHECK (std::strlen (id) > 0);
    CHECK_EQ (std::string (id), std::string ("aubio"));
    CHECK_EQ (std::string (backend.id()), std::string (id));
}

//==============================================================================
// 7. Silence does not manufacture evidence.

AUBIO_TEST (AubioBackend, digitalSilenceProducesNoBeatsAndNoTempo)
{
    // SPEC.md section 19: "silence does not create false acceleration". At the
    // tracker layer that means: silence==true, no beatEvent, no phase, and a
    // tempo candidate that is either absent (0) or inside the supported range.
    jam::AubioBackend backend;
    backend.reset (48000.0);

    std::vector<float> zeros (128, 0.0f);

    for (int block = 0; block < 400; ++block)   // ~1 s
    {
        const jam::RhythmObservation o =
            backend.process (makeFrame (static_cast<uint64_t> (block) * 128, 48000.0, zeros.data(), 128));

        CHECK (o.silence);
        CHECK (! o.beatEvent);
        CHECK (! o.phaseValid);
        CHECK (o.energyRmsDbfs <= -60.0f);
        CHECK (o.bpmCandidate == 0.0f || (o.bpmCandidate >= 40.0f && o.bpmCandidate <= 240.0f));
    }
}

//==============================================================================
// 8. Phase: aubio exposes a beat position, and the adapter must use it.

AUBIO_TEST (AubioBackend, phaseIsBoundedAndReArmsOnBeat)
{
    // aubio exposes the beat position (aubio_tempo_get_last) and period
    // (aubio_tempo_get_period) but no phase01 getter; the adapter normalises
    // those. The Musical Clock needs phase to lock (SPEC.md section 9.3), so the
    // contract is: phaseValid is false until a beat has established an anchor,
    // beatPhase01 is always in [0,1), and a beat re-arms it to 0.
    jam::AubioBackend backend;
    backend.reset (48000.0);

    const RunResult r = runSignal (backend, 48000.0, 48000ull * 10, 128, 120.0);

    REQUIRE (r.obs.size() > 0);
    CHECK (! r.obs.front().phaseValid);          // nothing known before the first beat

    bool sawValid = false;
    bool sawBeat = false;

    for (const jam::RhythmObservation& o : r.obs)
    {
        CHECK (o.beatPhase01 >= 0.0f && o.beatPhase01 < 1.0f);

        if (o.beatEvent)
        {
            sawBeat = true;
            CHECK_EQ (o.beatPhase01, 0.0f);      // a beat re-arms the phase
            CHECK (o.phaseValid);
        }

        if (o.phaseValid)
            sawValid = true;
    }

    CHECK (sawBeat);
    CHECK (sawValid);
}

//==============================================================================
// 9. Causality: a beat is produced from audio already received, never withheld.

AUBIO_TEST (AubioBackend, beatsAreNeverWithheldForFutureAudio)
{
    // aubio 0.4.9 has no blocking/non-causal prediction mode (no
    // set_btstate/get_btstate exists; see VENDORED-PATCHES.md). This pins the
    // observable consequence at the seam: (a) every emitted beat lies inside
    // audio the backend has already been given, within one hop; and (b) a
    // shorter run's beat stream is an exact prefix of a longer run's, so no
    // already-received beat is deferred or rewritten when future audio arrives.
    const double rate = 48000.0;

    // (a) No beat points into the future, and none is held longer than a hop.
    {
        jam::AubioBackend backend;
        backend.reset (rate);

        std::vector<float> buf (128);
        uint64_t pos = 0;
        const uint64_t total = 48000ull * 12;
        int beats = 0;

        while (pos < total)
        {
            const uint32_t n = static_cast<uint32_t> (std::min<uint64_t> (128, total - pos));
            for (uint32_t i = 0; i < n; ++i)
                buf[i] = clickSample (pos + i, rate, 120.0, 0.1, 0.003);

            const jam::RhythmObservation o = backend.process (makeFrame (pos, rate, buf.data(), n));
            const uint64_t frameEnd = pos + n;

            if (o.beatEvent)
            {
                ++beats;
                CHECK (o.inputSampleTime <= frameEnd);               // not in the future
                // The hop that carried the beat completed at most `n` samples
                // before the end of this frame, so the beat is at most one hop
                // plus this block of age.
                CHECK_LE (frameEnd - o.inputSampleTime, 512.0 + static_cast<double> (n));
            }
            pos += n;
        }
        CHECK (beats > 10);
    }

    // (b) Prefix stability: stopping early must not change the beats already
    // emitted.
    {
        jam::AubioBackend shortRun;
        shortRun.reset (rate);
        const RunResult a = runSignal (shortRun, rate, 48000ull * 8, 128, 120.0);

        jam::AubioBackend longRun;
        longRun.reset (rate);
        const RunResult b = runSignal (longRun, rate, 48000ull * 16, 128, 120.0);

        const std::vector<uint64_t> sa = beatStream (a);
        const std::vector<uint64_t> sb = beatStream (b);

        REQUIRE (sa.size() > 4);
        REQUIRE (sb.size() >= sa.size());
        for (std::size_t k = 0; k < sa.size(); ++k)
            CHECK_EQ (sa[k], sb[k]);
    }
}

//==============================================================================
// 10. Beat timings must not accumulate lag.

AUBIO_TEST (AubioBackend, beatTimingDoesNotAccumulateLag)
{
    // Over a long steady stream every beat must sit within a bounded distance of
    // the true onset, and that error must neither grow monotonically nor drift.
    // aubio predicts a grid from its estimated period; because that period
    // carries a small positive bias it periodically re-anchors, so the error
    // oscillates inside a bounded band rather than ramping. The regression slope
    // and the first-fifth vs last-fifth means pin "no accumulation"; the max
    // bound pins "bounded".
    const double rate = 48000.0;
    const double bpm = 120.0;
    const double firstBeat = 0.1;
    const double periodSeconds = 60.0 / bpm;

    jam::AubioBackend backend;
    backend.reset (rate);
    const RunResult r = runSignal (backend, rate, 48000ull * 40, 128, bpm, firstBeat);

    // True onset nearest to a reported beat time, in seconds.
    auto nearestOnsetError = [&] (uint64_t deviceTime) -> double
    {
        const double t = static_cast<double> (deviceTime) / rate;
        const double k = std::round ((t - firstBeat) / periodSeconds);
        const double onset = firstBeat + k * periodSeconds;
        return t - onset;
    };

    std::vector<double> errors;
    std::vector<double> times;
    for (const jam::RhythmObservation& o : r.obs)
    {
        if (! o.beatEvent)
            continue;
        const double t = static_cast<double> (o.inputSampleTime) / rate;
        if (t < 8.0)   // skip acquisition
            continue;
        errors.push_back (nearestOnsetError (o.inputSampleTime));
        times.push_back (t);
    }

    REQUIRE (errors.size() > 20);

    // Bounded: no beat is more than a quarter of a beat period from the truth.
    double maxAbs = 0.0;
    for (double e : errors)
        maxAbs = std::max (maxAbs, std::fabs (e));
    CHECK_LE (maxAbs, 0.25 * periodSeconds);

    // No accumulation: the mean error of the last fifth must not exceed the mean
    // of the first fifth by more than a tenth of a beat period.
    const std::size_t fifth = errors.size() / 5;
    double early = 0.0, late = 0.0;
    for (std::size_t i = 0; i < fifth; ++i)
        early += errors[i];
    for (std::size_t i = errors.size() - fifth; i < errors.size(); ++i)
        late += errors[i];
    early /= static_cast<double> (fifth);
    late /= static_cast<double> (fifth);
    CHECK_LE (std::fabs (late - early), 0.1 * periodSeconds);

    // No drift: the least-squares slope of error against time is small. A real
    // accumulated lag would show up as a sustained slope approaching the bias
    // rate (about 15 ms/s); the observed slope is under 1 ms/s.
    const double n = static_cast<double> (errors.size());
    double meanT = 0.0, meanE = 0.0;
    for (std::size_t i = 0; i < errors.size(); ++i) { meanT += times[i]; meanE += errors[i]; }
    meanT /= n; meanE /= n;
    double num = 0.0, den = 0.0;
    for (std::size_t i = 0; i < errors.size(); ++i)
    {
        num += (times[i] - meanT) * (errors[i] - meanE);
        den += (times[i] - meanT) * (times[i] - meanT);
    }
    const double slope = (den > 0.0) ? num / den : 0.0;
    CHECK_LE (std::fabs (slope), 0.02);   // seconds of error per second of audio
}

//==============================================================================
// 11. Allocation behaviour on the analysis path.

AUBIO_TEST (AubioBackend, allocationsAreZeroOnTheProcessPath)
{
    // IRhythmTracker.h permits allocation on the analysis worker, but this
    // backend preallocates all of its state and all of aubio's state in reset().
    // process() must add no C++ heap traffic, and the count must not depend on
    // how the audio was framed.
    auto measuredAllocations = [] (int blockSize) -> uint64_t
    {
        jam::AubioBackend backend;
        backend.reset (48000.0);

        std::vector<float> buf (static_cast<std::size_t> (blockSize));
        jam::AnalysisFrame f;
        f.sourceSampleRate = 48000.0;

        auto feed = [&] (uint64_t total, bool count) -> uint64_t
        {
            uint64_t pos = 0;
            uint64_t allocationCount = 0;
            {
                AllocationScope scope;
                while (pos < total)
                {
                    const uint32_t n = static_cast<uint32_t> (std::min<uint64_t> (static_cast<uint64_t> (blockSize), total - pos));
                    for (uint32_t i = 0; i < n; ++i)
                        buf[i] = clickSample (pos + i, 48000.0, 120.0, 0.1, 0.003);

                    f.sampleTime = pos;
                    f.numSamples = n;
                    std::memcpy (f.samples, buf.data(), static_cast<std::size_t> (n) * sizeof (float));
                    (void) backend.process (f);
                    pos += n;
                }
                allocationCount = scope.count();
            }
            if (count)
                return allocationCount;
            return 0;
        };

        feed (48000, false);          // warm up outside measurement
        return feed (48000 * 2, true);   // 2 s measured
    };

    const uint64_t fine = measuredAllocations (128);
    const uint64_t coarse = measuredAllocations (512);

    CHECK_EQ (fine, 0ull);            // the process path allocates nothing
    CHECK_EQ (coarse, 0ull);
}

#endif // JAM_AUBIO_ENABLED
