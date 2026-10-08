// EVAL-007 silence-coverage identity tests: suite RhythmSilenceCoverage.
//
// Before EVAL-007 the shared scorer excluded a fixture from the true-silence
// false-beat metric by NAME (`isCorpusDefectiveSilenceFixture` hard-coded
// `sustained_chords` and `tapping_muting_only`). EVAL-006 invalidated that: it
// repaired `sustained_chords` under the same name (new WAV hash, genuine
// independently measured 1.43 s of silence) and its acoustic review found
// `tapping_muting_only` to be rhythmically usable, not a defect. Exclusion by
// name therefore censored a good fixture and mislabelled a repaired one.
//
// This suite pins the replacement contract:
//   * coverage that is a property of the exact recording is keyed on the
//     declared WAV sha256, carried into RhythmTruth as audio identity — never
//     on the fixture name;
//   * the original fast-decay bytes stay CorpusDefect even if renamed, while the
//     repaired same-name render is Measured;
//   * a derived-noise clip whose trueSilenceSpans are inherited is reported as
//     NotAssessedStructuralNoise with its raw counts preserved;
//   * no spans is NoTrueSilence regardless of the registry;
//   * an empty/unknown hash is never excluded (synthetic/unknown compatibility);
//   * the coverage and raw counts propagate to JSON/CSV/aggregate output
//     without a misleading "measured" flag.
//
// The declared hash is only an identity string. This suite additionally
// RE-HASHES the three reviewed WAVs from bytes so the registry cannot drift from
// the audio on disk. Hash comparison here is corpus bookkeeping, not a
// copyright or authenticity conclusion.
//
// Wiring note (same as RhythmEvalMetricsTests): jam-core/CMakeLists.txt is
// frozen and globs tests/jam/*.cpp. Exactly one test translation unit
// (RhythmEvalMetricsTests.cpp) includes Metrics.cpp/Manifest.cpp so the scorer
// is compiled into this binary; this suite includes the HEADERS only and links
// against those definitions, so the algorithm exists once and no ODR clash is
// possible. Test code only: it allocates and reads files and is never linked
// into the audio path.

#include "JamTest.h"

#include "../../tools/rhythm-eval/Metrics.h"
#include "../../tools/rhythm-eval/Manifest.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using namespace jamtest;
using namespace rhythmeval;

// The reviewed identities. Kept here (and re-hashed below) so the test fails if
// the scorer's registry or the committed audio drifts.
const char* const kOriginalSustainedSha =
    "e4b9297fca341e70a639fc6a51fbd9e884c1321ee4fc802c5c3497feeefe6442";
const char* const kRepairedSustainedSha =
    "23b8cf21eab22949abe0745b46e98e17847052c3372f4f69cc00bfee6bc30061";
const char* const kTappingSha =
    "5e3b516af5c65315e9315f5e5d0e2d6cced2f9a45ed0cd9285226abc4a013cb0";

// ---------------------------------------------------------------------------
// Path discovery (same policy as the sibling suites)
// ---------------------------------------------------------------------------

std::string parentOf (const std::string& path)
{
    const std::size_t slash = path.find_last_of ("/\\");
    if (slash == std::string::npos)
        return std::string();
    if (slash == 0)
        return std::string ("/");
    return path.substr (0, slash);
}

std::string joinPath (const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
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
        if (up == dir) break;
        dir = up;
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), recomputed over committed bytes
// ---------------------------------------------------------------------------
//
// Duplicated from RhythmCorpusTests by design (each suite is self-contained and
// no shared test header exists); the self-test below pins it against the
// published vectors.

class Sha256
{
public:
    Sha256() { reset(); }

    void reset()
    {
        state_[0] = 0x6a09e667u; state_[1] = 0xbb67ae85u;
        state_[2] = 0x3c6ef372u; state_[3] = 0xa54ff53au;
        state_[4] = 0x510e527fu; state_[5] = 0x9b05688cu;
        state_[6] = 0x1f83d9abu; state_[7] = 0x5be0cd19u;
        bitLength_ = 0;
        bufferUsed_ = 0;
    }

    void update (const unsigned char* data, std::size_t length)
    {
        bitLength_ += static_cast<uint64_t> (length) * 8u;
        while (length > 0)
        {
            const std::size_t take = (64 - bufferUsed_) < length
                                         ? (64 - bufferUsed_) : length;
            for (std::size_t i = 0; i < take; ++i)
                buffer_[bufferUsed_ + i] = data[i];
            bufferUsed_ += take;
            data += take;
            length -= take;
            if (bufferUsed_ == 64) { transform (buffer_); bufferUsed_ = 0; }
        }
    }

    std::string hexDigest()
    {
        std::size_t i = bufferUsed_;
        if (bufferUsed_ < 56)
        {
            buffer_[i++] = 0x80;
            while (i < 56) buffer_[i++] = 0x00;
        }
        else
        {
            buffer_[i++] = 0x80;
            while (i < 64) buffer_[i++] = 0x00;
            transform (buffer_);
            i = 0;
            while (i < 56) buffer_[i++] = 0x00;
        }
        for (int b = 7; b >= 0; --b)
            buffer_[i++] = static_cast<unsigned char> ((bitLength_ >> (b * 8)) & 0xffu);
        transform (buffer_);

        static const char* kHex = "0123456789abcdef";
        std::string out;
        out.reserve (64);
        for (int w = 0; w < 8; ++w)
            for (int b = 3; b >= 0; --b)
            {
                const unsigned char byte =
                    static_cast<unsigned char> ((state_[w] >> (b * 8)) & 0xffu);
                out += kHex[byte >> 4];
                out += kHex[byte & 0x0fu];
            }
        return out;
    }

private:
    static uint32_t rotr (uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void transform (const unsigned char block[64])
    {
        static const uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
            0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
            0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
            0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
            0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
            0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
            0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
            0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
            0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t> (block[i * 4]) << 24)
                 | (static_cast<uint32_t> (block[i * 4 + 1]) << 16)
                 | (static_cast<uint32_t> (block[i * 4 + 2]) << 8)
                 | (static_cast<uint32_t> (block[i * 4 + 3]));
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = h + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    uint32_t state_[8];
    uint64_t bitLength_;
    unsigned char buffer_[64];
    std::size_t bufferUsed_;
};

bool sha256SelfTest()
{
    struct Case { const char* input; const char* expected; };
    static const Case cases[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" }
    };
    for (std::size_t i = 0; i < sizeof (cases) / sizeof (cases[0]); ++i)
    {
        Sha256 h;
        h.update (reinterpret_cast<const unsigned char*> (cases[i].input),
                  std::string (cases[i].input).size());
        if (h.hexDigest() != std::string (cases[i].expected))
            return false;
    }
    return true;
}

std::string sha256OfFile (const std::string& path, bool& ok)
{
    std::ifstream in (path.c_str(), std::ios::binary);
    if (! in.good()) { ok = false; return std::string(); }
    Sha256 h;
    std::vector<char> chunk (64 * 1024);
    while (in.good())
    {
        in.read (&chunk[0], static_cast<std::streamsize> (chunk.size()));
        const std::streamsize got = in.gcount();
        if (got > 0)
            h.update (reinterpret_cast<const unsigned char*> (&chunk[0]),
                      static_cast<std::size_t> (got));
    }
    ok = true;
    return h.hexDigest();
}

// ---------------------------------------------------------------------------
// Synthetic builders
// ---------------------------------------------------------------------------

RhythmTruth steadyTruth (const std::string& name, const std::string& sha,
                         double first = 0.5, int count = 16)
{
    RhythmTruth t;
    t.name = name;
    t.sha256 = sha;
    t.nominalBpm = 120.0;
    t.hasNominalBpm = true;
    t.tempoProfile = "constant";
    t.beatsPerBar = 4;
    t.meterNumerator = 4;
    t.meterDenominator = 4;
    const double period = 60.0 / 120.0;
    for (int i = 0; i < count; ++i)
        t.beats.push_back (first + static_cast<double> (i) * period);
    t.onsets = t.beats;
    t.durationSeconds = t.beats.empty() ? period : t.beats.back() + period;
    return t;
}

/** One prediction inside the (single) declared true-silence span, so the raw
    false-beat count is non-zero and must survive every non-measured status. */
ObservationSeries withBeatInSpan (const RhythmTruth& truth, double t)
{
    ObservationSeries obs;
    obs.audioDurationSeconds = truth.durationSeconds;
    obs.sampleRate = truth.sampleRate;
    obs.beatTimesSeconds.push_back (t);
    TempoSample s;
    s.timeSeconds = t;
    s.bpm = 120.0;
    s.phaseValid = true;
    obs.tempoSamples.push_back (s);
    return obs;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Hash identity: the same name means different things before/after repair.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmSilenceCoverage, originalHashIsCorpusDefectRepairedSameNameIsMeasured)
{
    REQUIRE (sha256SelfTest());

    // Original fast-decay bytes, name `sustained_chords`: CorpusDefect, with a
    // reviewed citation and the raw count still carried.
    {
        RhythmTruth t = steadyTruth ("sustained_chords", kOriginalSustainedSha);
        t.trueSilenceSpans.push_back (SilenceSpan { 2.45, 2.95 });
        const ObservationSeries obs = withBeatInSpan (t, 2.70);
        const FixtureMetrics m = scoreFixture (t, obs, kBeatMatchToleranceSeconds);

        CHECK (m.falseBeatCoverage == FalseBeatCoverage::CorpusDefect);
        CHECK (! m.falseBeatMetricInformative);
        CHECK (m.trueSilenceMeasured);           // raw spans present
        CHECK_EQ (m.falseBeatsInTrueSilence, 1); // raw count not erased
        CHECK (m.falseBeatsInTrueSilencePerSecond > 0.0);
        CHECK (! m.falseBeatCoverageCitation.empty());
        CHECK (std::string (knownSilenceDefectCitation (t)) == m.falseBeatCoverageCitation);
    }

    // Repaired long-decay render: SAME name, different hash, its own valid
    // spans. It must be Measured, not excluded by name.
    {
        RhythmTruth t = steadyTruth ("sustained_chords", kRepairedSustainedSha);
        t.trueSilenceSpans.push_back (SilenceSpan { 10.22, 11.30 });
        const ObservationSeries obs = withBeatInSpan (t, 10.80);
        const FixtureMetrics m = scoreFixture (t, obs, kBeatMatchToleranceSeconds);

        CHECK (m.falseBeatCoverage == FalseBeatCoverage::Measured);
        CHECK (m.falseBeatMetricInformative);
        CHECK_EQ (m.falseBeatsInTrueSilence, 1);
        CHECK (m.falseBeatCoverageCitation.empty());
    }
}

// ---------------------------------------------------------------------------
// 2. Hash identity survives a rename; unknown identities are never excluded.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmSilenceCoverage, renamedOriginalHashStaysCorpusDefect)
{
    RhythmTruth t = steadyTruth ("sustained_chords_repaired_but_not_really",
                                 kOriginalSustainedSha);
    t.trueSilenceSpans.push_back (SilenceSpan { 1.0, 1.4 });
    const FixtureMetrics m =
        scoreFixture (t, withBeatInSpan (t, 1.2), kBeatMatchToleranceSeconds);
    CHECK (m.falseBeatCoverage == FalseBeatCoverage::CorpusDefect);
    CHECK (! m.falseBeatCoverageCitation.empty());
}

JAM_TEST (RhythmSilenceCoverage, unknownAndMissingHashAreMeasuredCompatibility)
{
    // A fixture named like the old defect but with no declared hash (the shape of
    // every pre-EVAL-007 synthetic unit test) must NOT be excluded.
    {
        RhythmTruth t = steadyTruth ("sustained_chords", "");
        t.trueSilenceSpans.push_back (SilenceSpan { 1.0, 1.4 });
        CHECK (scoreFixture (t, withBeatInSpan (t, 1.2), kBeatMatchToleranceSeconds)
                   .falseBeatCoverage == FalseBeatCoverage::Measured);
        CHECK (knownSilenceDefectCitation (t)[0] == '\0');
    }
    // An unknown 64-hex hash is not in the registry: Measured by identity.
    {
        RhythmTruth t = steadyTruth ("some_other_fixture",
                                     std::string (64, 'a'));
        t.trueSilenceSpans.push_back (SilenceSpan { 1.0, 1.4 });
        CHECK (scoreFixture (t, withBeatInSpan (t, 1.2), kBeatMatchToleranceSeconds)
                   .falseBeatCoverage == FalseBeatCoverage::Measured);
    }
}

// ---------------------------------------------------------------------------
// 3. `tapping_muting_only` is Measured from its independent onset-spacing audit.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmSilenceCoverage, tappingHashIsMeasuredFromOnsetAudit)
{
    RhythmTruth t = steadyTruth ("tapping_muting_only", kTappingSha);
    t.trueSilenceSpans.push_back (SilenceSpan { 0.55, 0.85 });
    const FixtureMetrics m =
        scoreFixture (t, withBeatInSpan (t, 0.70), kBeatMatchToleranceSeconds);
    CHECK (m.falseBeatCoverage == FalseBeatCoverage::Measured);
    CHECK (m.falseBeatMetricInformative);
    CHECK_EQ (m.falseBeatsInTrueSilence, 1);
    // No registry entry: no citation.
    CHECK (m.falseBeatCoverageCitation.empty());
}

// ---------------------------------------------------------------------------
// 4. A derived noise clip's inherited spans are structural, not measured.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmSilenceCoverage, derivedNoiseStructuralSpansNotAssessedButRawCountsKept)
{
    RhythmTruth t = steadyTruth ("clean_eighths__noise_snr10db",
                                 std::string (64, 'b'));
    t.tags.push_back ("noise");
    t.tags.push_back ("derived");
    t.tags.push_back ("amplitude_only");
    t.trueSilenceSpans.push_back (SilenceSpan { 0.0, 0.352 });
    // A tracker holding the grid through the inherited span: raw counts exist.
    const FixtureMetrics m =
        scoreFixture (t, withBeatInSpan (t, 0.20), kBeatMatchToleranceSeconds);

    CHECK (m.falseBeatCoverage == FalseBeatCoverage::NotAssessedStructuralNoise);
    CHECK (! m.falseBeatMetricInformative);
    CHECK (m.trueSilenceMeasured);            // raw denominator retained
    CHECK_EQ (m.falseBeatsInTrueSilence, 1);  // raw count NOT erased
    CHECK (m.falseBeatsInTrueSilencePerSecond > 0.0);

    // The base `noisy_microphone` uses the qualifier `noisy`, not `noise`, and
    // must stay Measured: only the derived-noise provenance triggers the status.
    RhythmTruth base = steadyTruth ("noisy_microphone", std::string (64, 'c'));
    base.tags.push_back ("noisy");
    base.tags.push_back ("microphone");
    base.trueSilenceSpans.push_back (SilenceSpan { 0.0, 0.35 });
    CHECK (scoreFixture (base, withBeatInSpan (base, 0.2), kBeatMatchToleranceSeconds)
               .falseBeatCoverage == FalseBeatCoverage::Measured);
}

// ---------------------------------------------------------------------------
// 5. No declared spans is NoTrueSilence regardless of the registry.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmSilenceCoverage, noSpansIsNoTrueSilenceEvenForKnownDefectHash)
{
    RhythmTruth t = steadyTruth ("sustained_chords", kOriginalSustainedSha);
    // no trueSilenceSpans
    const FixtureMetrics m =
        scoreFixture (t, ObservationSeries(), kBeatMatchToleranceSeconds);
    CHECK (m.falseBeatCoverage == FalseBeatCoverage::NoTrueSilence);
    CHECK (! m.trueSilenceMeasured);
    CHECK (! m.falseBeatMetricInformative);
    CHECK_EQ (m.falseBeatsInTrueSilence, 0);
    CHECK (m.falseBeatCoverageCitation.empty());
}

// ---------------------------------------------------------------------------
// 6. Aggregate counts and JSON/CSV propagation, with no misleading flag.
// ---------------------------------------------------------------------------

JAM_TEST (RhythmSilenceCoverage, coverageAndRawCountsPropagateWithoutMisleadingMeasuredFlag)
{
    auto score = [] (const std::string& name, const std::string& sha,
                     const std::vector<std::string>& tags,
                     bool withSpans)
    {
        RhythmTruth t = steadyTruth (name, sha);
        for (const std::string& tag : tags) t.tags.push_back (tag);
        if (withSpans)
        {
            t.trueSilenceSpans.push_back (SilenceSpan { 2.45, 2.95 });
            return scoreFixture (t, withBeatInSpan (t, 2.70), kBeatMatchToleranceSeconds);
        }
        return scoreFixture (t, ObservationSeries(), kBeatMatchToleranceSeconds);
    };

    const FixtureMetrics measured = score ("measured", std::string (64, 'd'), {}, true);
    const FixtureMetrics defect = score ("sustained_chords", kOriginalSustainedSha, {}, true);
    const FixtureMetrics noise = score ("clean_eighths__noise_snr10db",
                                        std::string (64, 'e'), { "derived", "noise" }, true);
    const FixtureMetrics none = score ("no_silence", std::string (64, 'f'), {}, false);

    const std::vector<FixtureMetrics> all { measured, defect, noise, none };
    const AggregateMetrics a = aggregateFixtures (all);
    CHECK_EQ (a.trueSilenceMeasuredFixtures, 1);
    CHECK_EQ (a.trueSilenceCorpusDefectFixtures, 1);
    CHECK_EQ (a.trueSilenceStructuralNoiseFixtures, 1);
    CHECK_EQ (a.trueSilenceNoSilenceFixtures, 1);
    CHECK_EQ (a.trueSilenceInformativeFixtures, 1);
    // Raw counts from every fixture that declares spans are still aggregated.
    CHECK_EQ (a.silenceFixtures, 3);
    CHECK (a.falseBeatsInTrueSilencePerSecondWorst > 0.0);
    // Only the Measured fixture contributes the "informative" worst.
    CHECK_NEAR (a.falseBeatsInTrueSilencePerSecondWorstInformative,
                measured.falseBeatsInTrueSilencePerSecond, 1e-12);

    // Per-fixture JSON carries identity, coverage and the citation; the
    // measured flag is false for every non-Measured status.
    const std::string defectJson = fixtureMetricsToJson (defect).dump();
    CHECK (defectJson.find ("\"sourceSha256\":\"" + std::string (kOriginalSustainedSha) + "\"")
           != std::string::npos);
    CHECK (defectJson.find ("\"falseBeatCoverage\":\"CorpusDefect\"") != std::string::npos);
    CHECK (defectJson.find ("\"falseBeatMetricInformative\":false") != std::string::npos);
    CHECK (defectJson.find ("\"falseBeatsInTrueSilence\":1") != std::string::npos);
    CHECK (defectJson.find ("\"falseBeatCoverageCitation\":\"\"") == std::string::npos);

    const std::string noiseJson = fixtureMetricsToJson (noise).dump();
    CHECK (noiseJson.find ("\"falseBeatCoverage\":\"NotAssessedStructuralNoise\"")
           != std::string::npos);
    CHECK (noiseJson.find ("\"falseBeatMetricInformative\":false") != std::string::npos);
    CHECK (noiseJson.find ("\"falseBeatsInTrueSilence\":1") != std::string::npos);

    // CSV row: coverage name and raw count present; flag 0 for non-Measured.
    const std::string defectCsv = fixtureMetricsCsvRow (defect);
    CHECK (defectCsv.find ("CorpusDefect") != std::string::npos);
    CHECK (defectCsv.find (kOriginalSustainedSha) != std::string::npos);
    CHECK (defectCsv.find ("\"" + defect.falseBeatCoverageCitation + "\"")
           != std::string::npos); // the registry citation contains commas
    const std::string noiseCsv = fixtureMetricsCsvRow (noise);
    CHECK (noiseCsv.find ("NotAssessedStructuralNoise") != std::string::npos);

    // Header names the new raw-count column so the CSV is self-describing.
    CHECK (std::string (fixtureMetricsCsvHeader()).find ("falseBeatsInTrueSilence,")
           != std::string::npos);
    CHECK (std::string (fixtureMetricsCsvHeader()).find ("falseBeatCoverageCitation,")
           != std::string::npos);
    FixtureMetrics escaped = defect;
    escaped.falseBeatCoverageCitation = "review, \"quoted\"\nsecond line";
    CHECK (fixtureMetricsCsvRow (escaped).find ("\"review, \"\"quoted\"\"\nsecond line\"")
           != std::string::npos);
}

// ---------------------------------------------------------------------------
// 7. The registry hash is the hash of the audio on disk (evidence re-hash).
// ---------------------------------------------------------------------------

JAM_TEST (RhythmSilenceCoverage, registryHashesMatchTheCommittedAudio)
{
    REQUIRE (sha256SelfTest());
    const std::string dir = corpusDir();
    REQUIRE (! dir.empty());

    bool ok = false;
    const std::string original =
        sha256OfFile (joinPath (dir, "wav/sustained_chords.wav"), ok);
    REQUIRE (ok);
    CHECK_EQ (original, std::string (kOriginalSustainedSha));

    const std::string tapping =
        sha256OfFile (joinPath (dir, "wav/tapping_muting_only.wav"), ok);
    REQUIRE (ok);
    CHECK_EQ (tapping, std::string (kTappingSha));

    const std::string repaired =
        sha256OfFile (joinPath (dir, "repaired-sustain/sustained_chords.wav"), ok);
    REQUIRE (ok);
    CHECK_EQ (repaired, std::string (kRepairedSustainedSha));

    // The registry cites exactly the original hash.
    RhythmTruth t = steadyTruth ("whatever", original);
    CHECK (knownSilenceDefectCitation (t)[0] != '\0');
}
