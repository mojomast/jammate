// Tests for jam::BTrackBackend — the GPL BTrack 1.0.7 adapter behind
// IRhythmTracker (SPEC.md sections 9.2, 12, 17, 19; DEVPLAN.md TRACK-001).
//
// These tests deliberately live on the *evidence* side of the contract. Each
// case states the musical or algorithmic property it pins, not the call it
// makes. The tempo cases are the ones that catch the sample-rate trap described
// in third_party/BTrack/VENDORED-PATCHES.md: BTrack::calculateTempo() hard-codes
// 44100.0, so 48 kHz audio fed without correction reports about -8.8 % (slow).
//
// Determinism policy (matches tests/jam/JamTest.h): no sleeping, no wall clock,
// no threads, no file I/O. The only pseudo-randomness is the fixed hash used to
// build the synthetic click noise, which is a pure function of the sample index
// so every block framing sees the identical waveform.
//
// NOTE ON THE VENDOR: BTrack as vendored currently cannot estimate tempo at all
// because vendor patch 1 also disabled the causal call to
// resampleOnsetDetectionFunction() (see task-notes/TRACK-001.md). The tempo and
// drift cases below therefore fail red until that 3-line vendor fix is applied.
// They are written to the contract, not to the defect: with the fix they pass.
//
// BUILD DISCRIMINATOR. jam-core/CMakeLists.txt globs every tests/jam/*.cpp into
// the dependency-free jamTests target AND discovers ctest suites by grepping for
// the JamTest registration macro. It separately builds this file into
// jamBTrackTests, which is the only target that may link the GPL backend. Left
// alone, this file would both fail to link in jamTests (BTrackBackend lives in
// jam-btrack) and register jam.BTrackBackend twice. The CMake is frozen by the
// task, so the discriminator is USE_KISS_FFT: the btrack target defines it
// PUBLIC (third_party/BTrack/CMakeLists.txt), so it reaches jamBTrackTests but
// never jamTests, which links jam-core only. Outside that target this file
// compiles to nothing; the explicit add_test(jam.BTrackBackend) in
// jam-core/CMakeLists.txt still runs jamBTrackTests, so the suite cannot
// silently disappear (runAll fails on an empty filter).
#ifdef USE_KISS_FFT

#include "JamTest.h"

#include "jam/RhythmTypes.h"
#include "btrack/BTrackBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

//==============================================================================
// Scoped heap-allocation counter. IRhythmTracker.h permits allocation on the
// analysis worker, but this backend deliberately preallocates everything in
// reset(); the allocation test below measures that. Same global-operator
// technique as tests/jam/AnalysisAudioRingTests.cpp. This translation unit is
// only linked into jamBTrackTests.

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
// allocation-measurement idiom (tests/jam/AnalysisAudioRingTests.cpp). GCC's
// -Wmismatched-new-delete cannot see through the malloc call and reports a
// false positive at the deallocation sites; suppressed here so this file is
// clean under -Wall -Wextra -Wpedantic.
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
#define BT_TEST(suite_name, test_name)                                          \
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
// waveform (test 4 depends on this).

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
    stream is not classified as digital silence between attacks. */
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
RunResult runSignal (jam::BTrackBackend& backend, double rate, uint64_t total,
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

/** Tolerance for the synthetic-click BPM assertions. Measured against a
    corrected BTrack: the estimator quantises to a ~2 BPM grid and carries a
    small systematic negative bias, worst case about -2.9 % across 90..140 BPM
    click trains at 44.1 kHz. 4 % covers that with headroom while still
    catching the 8.8 % sample-rate trap by a wide margin. */
constexpr double kClickTempoToleranceRel = 0.04;

} // namespace

//==============================================================================
// 1. The sample-rate trap, at this product's reference rate.

BT_TEST (BTrackBackend, tempoIsUnbiasedOnAClickTrainAt48k)
{
    // A 120 BPM click train must be read as ~120 BPM when captured at 48 kHz.
    // This is the test that catches the hard-coded 44100.0 in
    // BTrack::calculateTempo(): an uncorrected adapter reports ~109 BPM.
    jam::BTrackBackend backend;
    backend.reset (48000.0);

    const RunResult r = runSignal (backend, 48000.0, 48000ull * 24, 128, 120.0);

    CHECK_NEAR (lastPositiveBpm (r), 120.0, 120.0 * kClickTempoToleranceRel);
}

//==============================================================================
// 2. The adapter must not be accidentally 48 kHz-specific.

BT_TEST (BTrackBackend, tempoIsUnbiasedOnAClickTrainAt44k1)
{
    // Same property at BTrack's own reference rate: the adapter must adapt to
    // whatever rate reset() promises rather than assume a device rate.
    jam::BTrackBackend backend;
    backend.reset (44100.0);

    const RunResult r = runSignal (backend, 44100.0, static_cast<uint64_t> (44100.0 * 24), 128, 120.0);

    CHECK_NEAR (lastPositiveBpm (r), 120.0, 120.0 * kClickTempoToleranceRel);
}

//==============================================================================
// 3. reset() is a true reset.

BT_TEST (BTrackBackend, resetIsDeterministicFieldForField)
{
    // Identical input after reset() must yield an identical observation
    // sequence, every field. This pins the no-hidden-state / deterministic
    // requirement in IRhythmTracker.h and SPEC.md section 21.2.
    jam::BTrackBackend backend;
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

BT_TEST (BTrackBackend, beatStreamIsIndependentOfBlockFraming)
{
    // The device hands the analysis worker 128-frame blocks, but the ring can
    // legally deliver anything from 1 to 2048 samples. The same underlying
    // audio must therefore produce the same beat events at the same device
    // sample times no matter how it was framed: if the accumulator were off by
    // one sample, the beat stream would shift or drop beats for some sizes.
    const int blockSizes[] = { 1, 127, 128, 512, 1024, 2048 };
    constexpr int kCount = static_cast<int> (sizeof (blockSizes) / sizeof (blockSizes[0]));

    std::vector<std::vector<uint64_t>> streams;

    for (int i = 0; i < kCount; ++i)
    {
        jam::BTrackBackend backend;
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

BT_TEST (BTrackBackend, trailingPartialBlockIsCarriedNotFabricated)
{
    // A block shorter than a BTrack hop must not complete a hop on its own and
    // must not invent a beat, and the remainder must survive so that the next
    // block resumes exactly where it left off.
    const uint64_t full = 48000ull * 4;   // an exact number of 128-blocks

    jam::BTrackBackend a;
    a.reset (48000.0);
    const RunResult baseline = runSignal (a, 48000.0, full, 128, 120.0);

    jam::BTrackBackend b;
    b.reset (48000.0);
    RunResult withTail = runSignal (b, 48000.0, full, 128, 120.0);

    // Exactly one extra sample: strictly less than one BTrack frame (1024) and
    // less than the ~557-sample device hop at 48 kHz.
    const float one = clickSample (full, 48000.0, 120.0, 0.1, 0.003);
    const jam::RhythmObservation tail = b.process (makeFrame (full, 48000.0, &one, 1));

    CHECK (! tail.beatEvent);
    CHECK_EQ (tail.bpmCandidate, withTail.obs.back().bpmCandidate);

    // The phase is allowed to advance by exactly the extra block's duration; it
    // must stay bounded and must not jump as though a whole hop had completed.
    const float previousPhase = withTail.obs.back().beatPhase01;
    CHECK (tail.beatPhase01 >= previousPhase);
    CHECK (tail.beatPhase01 < previousPhase + 0.01f);
    CHECK (tail.beatPhase01 >= 0.0f && tail.beatPhase01 < 1.0f);

    // The beat timeline is unchanged: the extra sample was carried, not
    // mistaken for a completed BTrack frame.
    const std::vector<uint64_t> before = beatStream (baseline);
    const std::vector<uint64_t> after = beatStream (withTail);
    REQUIRE (after.size() == before.size());
    for (std::size_t k = 0; k < before.size(); ++k)
        CHECK_EQ (after[k], before[k]);
}

//==============================================================================
// 6. Stable identity.

BT_TEST (BTrackBackend, idIsStableAndNonEmpty)
{
    jam::BTrackBackend backend;
    const char* id = backend.id();

    REQUIRE (id != nullptr);
    CHECK (std::strlen (id) > 0);
    CHECK_EQ (std::string (id), std::string ("btrack"));
    CHECK_EQ (std::string (backend.id()), std::string (id));
}

//==============================================================================
// 7. Silence does not manufacture evidence.

BT_TEST (BTrackBackend, digitalSilenceProducesNoBeatsAndNoTempo)
{
    // SPEC.md section 19: "silence does not create false acceleration". At the
    // tracker layer that means: silence==true, no beatEvent, and a tempo
    // candidate that is either absent (0) or inside the supported range.
    jam::BTrackBackend backend;
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
// 8. Phase: BTrack has none, so it is derived — and the derivation is tested.

BT_TEST (BTrackBackend, derivedPhaseIsBoundedAndReArmsOnBeat)
{
    // BTrack exposes beatDueInCurrentFrame() but no phase whatsoever
    // (VENDORED-PATCHES.md, "NOT patched, but load-bearing"). The adapter
    // derives phase from the timing of beat events relative to the estimated
    // beat period; the Musical Clock needs phase to lock (SPEC.md section 9.3),
    // so the contract is: phaseValid is false until a beat has established a
    // tempo, beatPhase01 is always in [0,1), and a beat re-arms it to 0.
    jam::BTrackBackend backend;
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
// 9. Beat alignment must not drift.

BT_TEST (BTrackBackend, beatTimingDoesNotAccumulateLag)
{
    // Over a long steady stream, every beat event's device sample time must
    // sit within one BTrack analysis hop of the true onset, and the residual
    // must not grow from the start of the run to the end. A frame counter
    // leaking into inputSampleTime, or an off-by-one accumulator, shows up here
    // as a growing offset.
    const double rate = 48000.0;
    const double bpm = 120.0;
    const double firstBeat = 0.1;
    const double periodSeconds = 60.0 / bpm;
    const double hopSeconds = 512.0 / 44100.0;   // one analysis hop

    jam::BTrackBackend backend;
    backend.reset (rate);
    const RunResult r = runSignal (backend, rate, 48000ull * 40, 128, bpm, firstBeat);

    // True onset nearest to a reported beat time, in seconds.
    auto nearestOnsetError = [&] (uint64_t deviceTime) -> double
    {
        const double t = static_cast<double> (deviceTime) / rate;
        const double k = std::round ((t - firstBeat) / periodSeconds);
        const double onset = firstBeat + k * periodSeconds;
        return std::fabs (t - onset);
    };

    std::vector<double> errors;
    for (const jam::RhythmObservation& o : r.obs)
    {
        if (! o.beatEvent)
            continue;
        const double t = static_cast<double> (o.inputSampleTime) / rate;
        if (t < 8.0)   // skip acquisition
            continue;
        errors.push_back (nearestOnsetError (o.inputSampleTime));
    }

    REQUIRE (errors.size() > 20);

    double maxError = 0.0;
    for (double e : errors)
        maxError = std::max (maxError, e);
    CHECK_LE (maxError, hopSeconds);

    // No drift: the mean error of the last fifth must not exceed the mean of
    // the first fifth by more than one hop either.
    const std::size_t fifth = errors.size() / 5;
    double early = 0.0, late = 0.0;
    for (std::size_t i = 0; i < fifth; ++i)
        early += errors[i];
    for (std::size_t i = errors.size() - fifth; i < errors.size(); ++i)
        late += errors[i];
    early /= static_cast<double> (fifth);
    late /= static_cast<double> (fifth);

    CHECK_LE (std::fabs (late - early), hopSeconds);
}

//==============================================================================
// 10. Allocation behaviour.

BT_TEST (BTrackBackend, allocationsComeOnlyFromBTrackAndAreFramingIndependent)
{
    // IRhythmTracker.h permits allocation on the analysis worker, and this
    // backend preallocates its own state in reset(). Upstream BTrack, however,
    // passes its cumulative-score CircularBuffer BY VALUE into
    // calculateNewCumulativeScoreValue(), so it heap-copies that buffer once
    // per analysis hop (~4 KiB at hop 512). That is a BTrack property, out of
    // this adapter's control and safe on the analysis thread. What this test
    // pins is that the adapter's own framing and resampling add NOTHING: the
    // same audio processed as 768 blocks of 128 and as 192 blocks of 512 must
    // allocate exactly the same number of times (one per hop).
    auto measuredAllocations = [] (int blockSize) -> uint64_t
    {
        jam::BTrackBackend backend;
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

    CHECK (fine > 0);                 // BTrack really does allocate (documented)
    CHECK_EQ (fine, coarse);          // the adapter adds no per-block allocation
}

#endif // USE_KISS_FFT
