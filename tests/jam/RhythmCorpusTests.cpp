// Validation for the committed guitar rhythm evaluation corpus.
//
// SPEC.md 12 forbids choosing a rhythm tracker permanently until it has been
// tested on guitar-specific material, and DEVPLAN.md's EVAL-001 / gate G3 need
// that corpus with ground truth before BTrack, aubio and BeatNet can be
// compared. This suite VALIDATES the corpus. It does not generate it --
// generation is `testdata/rhythm/tools/gen_fixtures.py` -- and it deliberately
// re-derives rather than trusts: it recomputes SHA-256 over the actual bytes on
// disk, and it checks that the declared beat grid agrees with the declared
// tempo, because a generator bug that silently mis-stated the grid would
// otherwise produce a confident, wrong tracker comparison instead of a test
// failure.
//
// The most important check here is the last one. A fixture whose declared beats
// do not match its declared BPM is worse than a missing fixture: EVAL-002 would
// score every candidate against a ground truth that is itself wrong, and the
// resulting ADR would be evidence for a decision that was never measured.
//
// JSON is parsed by a small hand-written reader at the bottom of this file. The
// alternative was a companion CSV index, which would have been less code but
// strictly worse: the manifest is the contract, and a second file duplicating
// its fields is a second thing that can drift from it. This reader is ~150
// lines and has no failure modes a dependency would not also have.
//
// Real-time rules: none of this matters, but for the record this file is test
// code only. It is never linked into the audio path, allocates freely, and
// touches the filesystem. SPEC.md 7.1 only forbids JSON parsing on the audio
// thread, and nothing here runs there.

#include "JamTest.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using namespace jamtest;

// ---------------------------------------------------------------------------
// Repository root discovery
// ---------------------------------------------------------------------------
//
// jam-core is configured both standalone (`cmake -S jam-core`) and in-tree, and
// this file may be built out-of-tree into an arbitrary build directory, so the
// corpus cannot be located relative to the working directory. Options:
//
//   1. JAM_RHYTHM_CORPUS env var pointing at the corpus directory. Lets a
//      reviewer point the suite at a scratch copy -- which is how the "can this
//      fail" evidence in the task note is produced, without mutating the repo.
//   2. An explicit override macro for builds that know the layout.
//   3. Walking up from the source file's recorded path (__FILE__), which is
//      stable because CMake passes absolute paths.
//   4. Walking up from the working directory, for in-tree builds.
//
// None of these can fail silently: `requireCorpusDir` throws if every candidate
// misses, and the tests REQUIRE it, so a relocated corpus is a red test rather
// than a suite that quietly validates nothing.

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
    if (last == '/' || last == '\\')
        return a + b;
    return a + "/" + b;
}

bool fileExists (const std::string& path)
{
    std::ifstream in (path.c_str(), std::ios::binary);
    return in.good();
}

std::string readFile (const std::string& path, bool& ok)
{
    std::ifstream in (path.c_str(), std::ios::binary);
    if (! in.good())
    {
        ok = false;
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

std::vector<std::string> candidatesForRoot (const std::string& root)
{
    std::vector<std::string> out;
    if (! root.empty())
        out.push_back (joinPath (root, "testdata/rhythm"));
    return out;
}

std::string findCorpusDir()
{
    const std::string manifest = "manifest.json";

    if (const char* env = std::getenv ("JAM_RHYTHM_CORPUS"))
    {
        const std::string dir = env;
        if (fileExists (joinPath (dir, manifest)))
            return dir;
    }

#ifdef JAM_RHYTHM_CORPUS_DIR
    {
        const std::string dir = JAM_RHYTHM_CORPUS_DIR;
        if (fileExists (joinPath (dir, manifest)))
            return dir;
    }
#endif

    // This file lives at <root>/tests/jam/RhythmCorpusTests.cpp. CMake passes
    // absolute source paths, so __FILE__ yields the repository root two levels
    // up. Compile-time __FILE__ is the only location guaranteed to be right for
    // an out-of-tree build.
    {
        std::string dir = parentOf (parentOf (__FILE__));  // <root>/tests
        for (int i = 0; i < 6 && ! dir.empty(); ++i)
        {
            const std::vector<std::string> cands = candidatesForRoot (dir);
            for (std::size_t k = 0; k < cands.size(); ++k)
                if (fileExists (joinPath (cands[k], manifest)))
                    return cands[k];
            dir = parentOf (dir);
        }
    }

    // In-tree build: the working directory is inside the repository.
    {
        std::string dir = ".";
        for (int i = 0; i < 8; ++i)
        {
            const std::vector<std::string> cands = candidatesForRoot (dir);
            for (std::size_t k = 0; k < cands.size(); ++k)
                if (fileExists (joinPath (cands[k], manifest)))
                    return cands[k];
            const std::string up = parentOf (dir);
            if (up == dir)
                break;
            dir = up;
        }
    }

    return std::string();
}

std::string requireCorpusDir()
{
    const std::string dir = findCorpusDir();
    if (dir.empty())
        jamtest::fail (__FILE__, __LINE__,
                       "could not locate testdata/rhythm/manifest.json. Tried the "
                       "JAM_RHYTHM_CORPUS environment variable, the path recorded "
                       "in __FILE__, and every ancestor of the working directory. "
                       "Set JAM_RHYTHM_CORPUS to the corpus directory.");
    return dir;
}

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------
//
// Needed to prove the committed bytes still match the manifest. A ~70 line
// implementation is preferable here to a dependency on a crypto library for a
// test that only hashes a handful of small files, and it removes any question
// of what a shared library does on the path.

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
            if (bufferUsed_ == 64)
            {
                transform (buffer_);
                bufferUsed_ = 0;
            }
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
        {
            for (int b = 3; b >= 0; --b)
            {
                const unsigned char byte =
                    static_cast<unsigned char> ((state_[w] >> (b * 8)) & 0xffu);
                out += kHex[byte >> 4];
                out += kHex[byte & 0x0fu];
            }
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

// Self-check against the published FIPS 180-4 vectors, so a broken
// implementation cannot make every hash comparison pass or fail spuriously.
bool sha256SelfTest()
{
    struct Case { const char* input; const char* expected; };
    static const Case cases[] = {
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" }
    };
    for (std::size_t i = 0; i < sizeof (cases) / sizeof (cases[0]); ++i)
    {
        Sha256 h;
        h.reset();
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
    if (! in.good())
    {
        ok = false;
        return std::string();
    }
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
// Minimal JSON reader
// ---------------------------------------------------------------------------
//
// Sufficient for this manifest and nothing more: objects, arrays, strings with
// the standard escapes, numbers, true/false/null. Rejects anything it does not
// understand rather than guessing, so a future field type shows up as a red
// test instead of a silently mis-parsed value.

struct JsonValue;

struct JsonMember
{
    std::string key;
    JsonValue* value;
};

struct JsonValue
{
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<JsonMember> members;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }

    const JsonValue* find (const std::string& key) const
    {
        if (type != Type::Object)
            return nullptr;
        for (std::size_t i = 0; i < members.size(); ++i)
            if (members[i].key == key)
                return members[i].value;
        return nullptr;
    }

    const JsonValue& at (const std::string& key) const
    {
        static const JsonValue kNull;
        const JsonValue* v = find (key);
        return v != nullptr ? *v : kNull;
    }

    double numberOr (double fallback) const
    {
        return type == Type::Number ? number : fallback;
    }

    std::string stringOr (const std::string& fallback) const
    {
        return type == Type::String ? text : fallback;
    }

    std::vector<double> numbers() const
    {
        std::vector<double> out;
        if (type != Type::Array)
            return out;
        out.reserve (items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
            out.push_back (items[i].type == Type::Number ? items[i].number : 0.0);
        return out;
    }

    std::vector<std::string> strings() const
    {
        std::vector<std::string> out;
        if (type != Type::Array)
            return out;
        out.reserve (items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].type == Type::String)
                out.push_back (items[i].text);
        return out;
    }
};

class JsonParser
{
public:
    explicit JsonParser (const std::string& source) : s_ (source), i_ (0) {}

    JsonValue parse()
    {
        skipWhitespace();
        JsonValue v = parseValue();
        skipWhitespace();
        if (i_ != s_.size())
            fail ("trailing content after the top-level value");
        return v;
    }

private:
    [[noreturn]] void fail (const char* what) const
    {
        std::ostringstream ss;
        ss << "JSON parse error at byte " << i_ << ": " << what;
        throw std::runtime_error (ss.str());
    }

    void skipWhitespace()
    {
        while (i_ < s_.size())
        {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++i_;
            else
                break;
        }
    }

    char peek() const
    {
        if (i_ >= s_.size())
            return '\0';
        return s_[i_];
    }

    void expect (char c)
    {
        if (peek() != c)
            fail ("unexpected character");
        ++i_;
    }

    JsonValue parseValue()
    {
        if (i_ >= s_.size())
            fail ("unexpected end of input");
        const char c = s_[i_];
        switch (c)
        {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { JsonValue v; v.type = JsonValue::Type::String;
                        v.text = parseString(); return v; }
            case 't': { expectLiteral ("true");  JsonValue v;
                        v.type = JsonValue::Type::Bool; v.boolean = true; return v; }
            case 'f': { expectLiteral ("false"); JsonValue v;
                        v.type = JsonValue::Type::Bool; v.boolean = false; return v; }
            case 'n': { expectLiteral ("null");  return JsonValue(); }
            default:  return parseNumber();
        }
    }

    void expectLiteral (const char* lit)
    {
        const std::size_t n = std::string (lit).size();
        if (s_.compare (i_, n, lit) != 0)
            fail ("bad literal");
        i_ += n;
    }

    JsonValue parseObject()
    {
        JsonValue v;
        v.type = JsonValue::Type::Object;
        expect ('{');
        skipWhitespace();
        if (peek() == '}')
        {
            ++i_;
            return v;
        }
        for (;;)
        {
            skipWhitespace();
            JsonMember m;
            m.key = parseString();
            skipWhitespace();
            expect (':');
            skipWhitespace();
            m.value = new JsonValue (parseValue());
            v.members.push_back (m);
            skipWhitespace();
            if (peek() == ',')
            {
                ++i_;
                continue;
            }
            expect ('}');
            return v;
        }
    }

    JsonValue parseArray()
    {
        JsonValue v;
        v.type = JsonValue::Type::Array;
        expect ('[');
        skipWhitespace();
        if (peek() == ']')
        {
            ++i_;
            return v;
        }
        for (;;)
        {
            skipWhitespace();
            v.items.push_back (parseValue());
            skipWhitespace();
            if (peek() == ',')
            {
                ++i_;
                continue;
            }
            expect (']');
            return v;
        }
    }

    static void appendUtf8 (std::string& out, unsigned int cp)
    {
        if (cp < 0x80u)
            out += static_cast<char> (cp);
        else if (cp < 0x800u)
        {
            out += static_cast<char> (0xc0u | (cp >> 6));
            out += static_cast<char> (0x80u | (cp & 0x3fu));
        }
        else
        {
            out += static_cast<char> (0xe0u | (cp >> 12));
            out += static_cast<char> (0x80u | ((cp >> 6) & 0x3fu));
            out += static_cast<char> (0x80u | (cp & 0x3fu));
        }
    }

    std::string parseString()
    {
        expect ('"');
        std::string out;
        while (i_ < s_.size())
        {
            const char c = s_[i_++];
            if (c == '"')
                return out;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (i_ >= s_.size())
                break;
            const char e = s_[i_++];
            switch (e)
            {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    if (i_ + 4 > s_.size())
                        fail ("truncated \\u escape");
                    unsigned int cp = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        const char h = s_[i_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')      cp |= static_cast<unsigned> (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned> (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned> (h - 'A' + 10);
                        else fail ("bad hex digit in \\u escape");
                    }
                    appendUtf8 (out, cp);
                    break;
                }
                default: fail ("unknown escape");
            }
        }
        fail ("unterminated string");
    }

    JsonValue parseNumber()
    {
        const std::size_t start = i_;
        if (peek() == '-' || peek() == '+')
            ++i_;
        while (i_ < s_.size() && (std::isdigit (static_cast<unsigned char> (s_[i_]))
                                  || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E'
                                  || s_[i_] == '-' || s_[i_] == '+'))
            ++i_;
        if (i_ == start)
            fail ("expected a value");
        const std::string token = s_.substr (start, i_ - start);
        JsonValue v;
        v.type = JsonValue::Type::Number;
        try
        {
            v.number = std::stod (token);
        }
        catch (const std::exception&)
        {
            fail ("malformed number");
        }
        return v;
    }

    const std::string& s_;
    std::size_t i_;
};

// ---------------------------------------------------------------------------
// Corpus access
// ---------------------------------------------------------------------------

// The 19 required SPEC.md 12.2 cases, spelled exactly as the manifest's tag
// vocabulary spells them. Each must appear exactly once as a fixture's primary
// scenario tag.
//
// Written out rather than derived from the manifest on purpose: if the generator
// ever dropped a case, deriving the expected list from its own output would
// happily agree with the omission, and the test would pass while the corpus was
// incomplete. Note the tag `meter_6_8` is used rather than `compound_6_8`, so
// the test pins the manifest's spelling rather than accepting any of them.
const char* const kRequiredScenarios[] = {
    "clean_eighths", "clean_sixteenths", "distorted_power_chords", "palm_mute",
    "blues_shuffle", "syncopated_funk", "sparse_single_notes", "arpeggio",
    "sustained_chords", "missing_downbeats", "stop_start", "accelerando",
    "ritardando", "meter_3_4", "meter_6_8", "noisy_microphone",
    "line_input_low_level", "line_input_clipping", "tapping_muting_only"
};

constexpr std::size_t kRequiredScenarioCount =
    sizeof (kRequiredScenarios) / sizeof (kRequiredScenarios[0]);

struct Fixture
{
    std::string name;
    std::string file;
    std::string sha256;
    double durationSeconds = 0.0;
    double sampleRate = 0.0;
    int meterNumerator = 0;
    int meterDenominator = 0;
    int beatsPerBar = 0;
    std::string tempoProfile;
    bool hasNominalBpm = false;
    double nominalBpm = 0.0;
    double bpmStart = 0.0;
    double bpmEnd = 0.0;
    std::vector<double> beats;
    std::vector<double> onsets;
    std::vector<std::string> tags;
    std::string license;
    std::string provenance;
    std::string notes;

    // `trueSilenceSpans` are the regions where the guitar is genuinely not
    // sounding. `silenceSpans` (narrow windows around deliberately unplayed
    // beats) is a different thing and is read separately below.
    std::vector<std::pair<double, double>> trueSilenceSpans;
    bool hasTrueSilenceSpans = false;
};

struct Corpus
{
    int schemaVersion = 0;
    std::string generatorName;
    int generatorVersion = 0;
    std::string seedPolicy;
    std::vector<Fixture> fixtures;
    std::vector<std::string> scenarioVocabulary;
    std::vector<std::string> qualifierVocabulary;
    // Explicit fixture-name membership for SPEC 19's "core fixtures"
    // denominator. Present and reconciled against the per-fixture `core` tag by
    // coreMembershipListAgreesWithTheCoreTag.
    std::vector<std::string> coreFixtures;
    std::string manifestText;
    std::string dir;
};

// Reads the `data` chunk of a 16-bit mono PCM RIFF/WAVE file into normalised
// floats in [-1, 1]. Returns false if the file is not that shape.
//
// This parses the chunk list rather than assuming a 44-byte header. That is not
// pedantry: an earlier version of this test read the whole file as samples and
// interpreted the ASCII "RIFF"/"WAVE" header as audio, which put a spurious
// 0 dBFS transient at the start of every file and failed all 19 fixtures for a
// reason that had nothing to do with the corpus. Real captures carry LIST or
// fact chunks, so the offset is not a constant either.
bool readWavSamples (const std::string& path, std::vector<double>& out,
                     int& channels)
{
    out.clear();
    channels = 0;
    std::ifstream in (path.c_str(), std::ios::binary);
    if (! in.good())
        return false;

    std::vector<char> raw ((std::istreambuf_iterator<char> (in)),
                           std::istreambuf_iterator<char>());
    in.close();
    if (raw.size() < 12)
        return false;

    const unsigned char* p = reinterpret_cast<const unsigned char*> (&raw[0]);
    if (std::memcmp (p, "RIFF", 4) != 0 || std::memcmp (p + 8, "WAVE", 4) != 0)
        return false;

    uint32_t fmtChannels = 0;
    uint32_t fmtBits = 0;
    bool haveFmt = false;
    std::size_t dataOff = 0;
    std::size_t dataLen = 0;

    std::size_t pos = 12;
    while (pos + 8 <= raw.size())
    {
        char id[5] = { 0, 0, 0, 0, 0 };
        std::memcpy (id, p + pos, 4);
        uint32_t size = static_cast<uint32_t> (p[pos + 4])
                      | (static_cast<uint32_t> (p[pos + 5]) << 8)
                      | (static_cast<uint32_t> (p[pos + 6]) << 16)
                      | (static_cast<uint32_t> (p[pos + 7]) << 24);
        const std::size_t body = pos + 8;
        if (body + size > raw.size())
            size = static_cast<uint32_t> (raw.size() - body);
        if (std::memcmp (id, "fmt ", 4) == 0 && size >= 16)
        {
            fmtChannels = static_cast<uint32_t> (p[body + 2])
                        | (static_cast<uint32_t> (p[body + 3]) << 8);
            fmtBits = static_cast<uint32_t> (p[body + 14])
                    | (static_cast<uint32_t> (p[body + 15]) << 8);
            haveFmt = true;
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            dataOff = body;
            dataLen = size;
        }
        pos = body + size + (size & 1u);   // chunks are word-aligned
    }

    if (! haveFmt || dataLen == 0 || fmtBits != 16)
        return false;
    channels = static_cast<int> (fmtChannels);
    const std::size_t frames = dataLen / (2u * (fmtChannels != 0 ? fmtChannels : 1u));
    out.resize (frames);
    for (std::size_t k = 0; k < frames; ++k)
    {
        // Mono only: a stereo corpus would need a downmix, and the manifest
        // declares mono. Downmixing silently would be worse than refusing.
        if (fmtChannels != 1)
            return false;
        const std::size_t off = dataOff + k * 2;
        if (off + 1 >= raw.size())
            return false;
        const int16_t v = static_cast<int16_t> (
            static_cast<uint16_t> (static_cast<unsigned char> (p[off]))
            | (static_cast<uint16_t> (static_cast<unsigned char> (p[off + 1])) << 8));
        out[k] = static_cast<double> (v) / 32768.0;
    }
    return true;
}

// Reads an array of [start, end] pairs. Returns false and leaves `out` empty
// when the shape is wrong, so callers can report the field's absence rather than
// silently treating it as "no silence".
bool readSpanArray (const JsonValue& v, std::vector<std::pair<double, double>>& out)
{
    out.clear();
    if (! v.isArray())
        return false;
    for (std::size_t i = 0; i < v.items.size(); ++i)
    {
        const JsonValue& pair = v.items[i];
        if (! pair.isArray() || pair.items.size() != 2)
            return false;
        if (! pair.items[0].isNumber() || ! pair.items[1].isNumber())
            return false;
        out.push_back (std::make_pair (pair.items[0].number, pair.items[1].number));
    }
    return true;
}

Corpus loadCorpus()
{
    Corpus c;
    c.dir = requireCorpusDir();

    bool ok = false;
    const std::string manifestPath = joinPath (c.dir, "manifest.json");
    c.manifestText = readFile (manifestPath, ok);
    if (! ok)
    {
        jamtest::fail (__FILE__, __LINE__,
                       "could not read " + manifestPath);
        return c;
    }

    JsonParser parser (c.manifestText);
    JsonValue root;
    try
    {
        root = parser.parse();
    }
    catch (const std::exception& e)
    {
        jamtest::fail (__FILE__, __LINE__, e.what());
        return c;
    }

    // CHECK, not REQUIRE: REQUIRE expands to a bare `return`, which cannot be
    // used in a non-void function, and a malformed manifest already recorded a
    // failure above, so carrying on to report every other problem is more useful
    // than stopping at the first.
    CHECK (root.isObject());
    if (! root.isObject())
        return c;

    c.schemaVersion = static_cast<int> (root.at ("schemaVersion").numberOr (0.0));

    const JsonValue& gen = root.at ("generator");
    c.generatorName = gen.at ("name").stringOr ("");
    c.generatorVersion = static_cast<int> (gen.at ("version").numberOr (0.0));
    c.seedPolicy = gen.at ("seedPolicy").stringOr ("");

    const JsonValue& vocab = root.at ("tagVocabulary");
    c.scenarioVocabulary = vocab.at ("scenario").strings();
    c.qualifierVocabulary = vocab.at ("qualifier").strings();
    c.coreFixtures = vocab.at ("core").strings();

    const JsonValue& list = root.at ("fixtures");
    CHECK (list.isArray());
    if (! list.isArray())
        return c;

    for (std::size_t i = 0; i < list.items.size(); ++i)
    {
        const JsonValue& v = list.items[i];
        Fixture f;
        f.name = v.at ("name").stringOr ("");
        f.file = v.at ("file").stringOr ("");
        f.sha256 = v.at ("sha256").stringOr ("");
        f.durationSeconds = v.at ("durationSeconds").numberOr (0.0);
        f.sampleRate = v.at ("sampleRate").numberOr (0.0);
        f.notes = v.at ("notes").stringOr ("");
        f.license = v.at ("license").stringOr ("");
        f.provenance = v.at ("provenance").stringOr ("");
        f.tempoProfile = v.at ("tempoProfile").stringOr ("");

        const JsonValue& meter = v.at ("meter");
        f.meterNumerator = static_cast<int> (meter.at ("numerator").numberOr (0.0));
        f.meterDenominator = static_cast<int> (meter.at ("denominator").numberOr (0.0));
        f.beatsPerBar = static_cast<int> (meter.at ("beatsPerBar").numberOr (0.0));

        const JsonValue* nominal = v.find ("nominalBpm");
        if (nominal != nullptr && nominal->isNumber())
        {
            f.hasNominalBpm = true;
            f.nominalBpm = nominal->number;
        }
        f.bpmStart = v.at ("bpmStart").numberOr (0.0);
        f.bpmEnd = v.at ("bpmEnd").numberOr (0.0);

        f.beats = v.at ("beats").numbers();
        f.onsets = v.at ("onsets").numbers();
        f.tags = v.at ("scenarioTags").strings();

        const JsonValue* tss = v.find ("trueSilenceSpans");
        if (tss != nullptr)
        {
            f.hasTrueSilenceSpans = readSpanArray (*tss, f.trueSilenceSpans);
            if (! f.hasTrueSilenceSpans)
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": trueSilenceSpans is not an array of "
                               "[startSeconds, endSeconds] pairs");
        }

        c.fixtures.push_back (f);
    }

    return c;
}

// Replaces the contents of every JSON string literal with 'x', leaving
// structure and all non-string bytes intact. Used to assert the manifest is
// compactly serialised without tripping over legitimate punctuation inside the
// human-readable `notes` and `provenance` strings.
std::string blankStringLiterals (const std::string& s)
{
    std::string out;
    out.reserve (s.size());
    bool inString = false;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (inString)
        {
            if (c == '\\' && i + 1 < s.size())
            {
                out += 'x';
                out += 'x';
                ++i;
                continue;
            }
            if (c == '"')
                inString = false;
            else
                out += 'x';
            continue;
        }
        if (c == '"')
        {
            inString = true;
            out += c;
            continue;
        }
        out += c;
    }
    return out;
}

// Returns the index just past the JSON value starting at `i`, tracking string
// state and bracket depth so nested commas and braces are not mistaken for the
// end of the value.
std::size_t skipValue (const std::string& s, std::size_t i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r'
                            || s[i] == '\t'))
        ++i;
    if (i >= s.size())
        return i;

    if (s[i] == '"')
    {
        ++i;
        while (i < s.size())
        {
            if (s[i] == '\\')
            {
                i += 2;
                continue;
            }
            if (s[i] == '"')
                return i + 1;
            ++i;
        }
        return i;
    }

    if (s[i] == '{' || s[i] == '[')
    {
        int depth = 0;
        bool inString = false;
        while (i < s.size())
        {
            const char c = s[i];
            if (inString)
            {
                if (c == '\\')
                {
                    i += 2;
                    continue;
                }
                if (c == '"')
                    inString = false;
            }
            else if (c == '"')
                inString = true;
            else if (c == '{' || c == '[')
                ++depth;
            else if (c == '}' || c == ']')
            {
                --depth;
                if (depth == 0)
                    return i + 1;
            }
            ++i;
        }
        return i;
    }

    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']')
        ++i;
    return i;
}

// True when the keys of the top-level object appear in ascending order, which
// is the property that keeps `git diff` quiet when fields are added.
bool topLevelKeysAreSorted (const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size() && s[i] != '{')
        ++i;
    if (i == s.size())
        return false;
    ++i;

    std::string previous;
    for (;;)
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == ','))
            ++i;
        if (i >= s.size() || s[i] == '}')
            return true;
        if (s[i] != '"')
            return false;
        ++i;
        std::string key;
        while (i < s.size() && s[i] != '"')
        {
            if (s[i] == '\\')
                return false;   // escaped key: not expected, do not guess
            key += s[i];
            ++i;
        }
        if (i >= s.size())
            return false;
        ++i;   // closing quote
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r'
                                || s[i] == '\t'))
            ++i;
        if (i >= s.size() || s[i] != ':')
            return false;
        if (! previous.empty() && key <= previous)
            return false;
        previous = key;
        ++i;   // the ':' itself

        // Skip exactly one value. Scanning to the next ',' is not enough: a
        // nested object or array contains commas of its own, and stopping at one
        // of those reads an inner key as a top-level one. That bug made this
        // check report a false failure on a correctly sorted manifest.
        i = skipValue (s, i);
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
}

// Shared checks run by most tests so a failure names the fixture.
void checkFixtureBasics (const Corpus& c, const Fixture& f)
{
    CHECK (! f.name.empty());
    CHECK (! f.file.empty());
    CHECK (! f.sha256.empty());
    CHECK_EQ (f.sha256.size(), static_cast<std::size_t> (64));
    CHECK_NEAR (f.sampleRate, 48000.0, 0.0);   // SPEC 17 reference rate
    CHECK (f.durationSeconds > 0.0);
    CHECK (f.meterNumerator > 0);
    CHECK (f.meterDenominator > 0);
    CHECK (f.beatsPerBar > 0);
    CHECK (! f.license.empty());
    CHECK (! f.provenance.empty());
    CHECK (! f.notes.empty());
    CHECK (! f.tags.empty());
    CHECK (fileExists (joinPath (c.dir, f.file)));
}

} // namespace

// ---------------------------------------------------------------------------
// manifestParsesAndDescribesTheWholeCorpus
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, manifestParsesAndDescribesTheWholeCorpus)
{
    REQUIRE (sha256SelfTest());

    const Corpus c = loadCorpus();
    REQUIRE (! c.dir.empty());
    REQUIRE (! c.fixtures.empty());

    CHECK_EQ (c.schemaVersion, 1);
    CHECK (! c.generatorName.empty());
    CHECK (c.generatorVersion > 0);
    // The seed policy must be recorded, because byte-identical regeneration is
    // what makes the committed hashes meaningful rather than decorative.
    CHECK (! c.seedPolicy.empty());

    // One fixture per required SPEC 12.2 case. DEVPLAN EVAL-001 lists 19 and
    // SPEC 12.2 also lists 19 (the two line-input variants are separate cases),
    // so there is no duplicate to collapse.
    CHECK_EQ (c.fixtures.size(), kRequiredScenarioCount);

    std::set<std::string> names;
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        checkFixtureBasics (c, f);
        CHECK (names.insert (f.name).second);
        // `file` must be relative to the corpus directory and stay inside it.
        CHECK (! f.file.empty() && f.file[0] != '/');
        CHECK (f.file.find ("..") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// everySha256MatchesTheFileOnDisk
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, everySha256MatchesTheFileOnDisk)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        const std::string path = joinPath (c.dir, f.file);
        REQUIRE (fileExists (path));

        bool ok = false;
        const std::string actual = sha256OfFile (path, ok);
        REQUIRE (ok);
        // This is the drift check: if a fixture is regenerated by hand with a
        // different generator version, or edited in place, this fails.
        CHECK_EQ (actual, f.sha256);
    }
}

// ---------------------------------------------------------------------------
// beatsArePresentStrictlyIncreasingAndInsideTheFile
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, beatsArePresentStrictlyIncreasingAndInsideTheFile)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];

        // A fixture with no beats cannot be scored for acquisition, BPM error or
        // phase, which is every metric SPEC 12.3 asks for.
        REQUIRE (! f.beats.empty());
        REQUIRE (f.beats.size() >= static_cast<std::size_t> (f.beatsPerBar));

        for (std::size_t k = 1; k < f.beats.size(); ++k)
        {
            if (! (f.beats[k] > f.beats[k - 1]))
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": beats must be strictly increasing");
        }

        // Every beat must lie inside the file, or a tracker cannot be scored on
        // it at all. A small tolerance covers sample quantisation only.
        for (std::size_t k = 0; k < f.beats.size(); ++k)
            CHECK (f.beats[k] >= 0.0);
        CHECK (f.beats.back() <= f.durationSeconds);

        // Onsets, when declared, are real events and must be ordered and in
        // range too. (An empty onset list is allowed only for silence.)
        for (std::size_t k = 1; k < f.onsets.size(); ++k)
        {
            if (! (f.onsets[k] > f.onsets[k - 1]))
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": onsets must be strictly increasing");
        }
        for (std::size_t k = 0; k < f.onsets.size(); ++k)
            CHECK (f.onsets[k] >= 0.0);
        if (! f.onsets.empty())
            CHECK (f.onsets.back() <= f.durationSeconds);
    }
}

// ---------------------------------------------------------------------------
// steadyFixturesMatchTheirDeclaredNominalBpm
// ---------------------------------------------------------------------------
//
// This is the test that catches the generator bug worth catching. If `beats`
// and `nominalBpm` disagree, EVAL-002 scores every tracker against a ground
// truth that does not describe the audio, and the ADR inherits the error. The
// tolerance is 0.05%, tight because a constant-tempo grid is exactly uniform:
// anything looser would let a real off-by-one in the pattern builder pass.

JAM_TEST (RhythmCorpus, steadyFixturesMatchTheirDeclaredNominalBpm)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    constexpr double kToleranceFraction = 0.0005;   // 0.05 %
    int steadyChecked = 0;

    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        if (f.tempoProfile != "constant")
            continue;
        REQUIRE (f.hasNominalBpm);
        REQUIRE (f.nominalBpm > 0.0);

        const double expected = 60.0 / f.nominalBpm;
        for (std::size_t k = 0; k + 1 < f.beats.size(); ++k)
        {
            const double gap = f.beats[k + 1] - f.beats[k];
            if (std::fabs (gap - expected) > expected * kToleranceFraction)
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": beat gap " + jamtest::describe (gap)
                               + "s does not match nominalBpm "
                               + jamtest::describe (f.nominalBpm)
                               + " (expected " + jamtest::describe (expected) + "s)");
        }
        ++steadyChecked;
    }

    // The corpus must actually contain steady fixtures for this to mean
    // anything; an empty pass here would be a hollow test.
    CHECK (steadyChecked >= 15);
}

// ---------------------------------------------------------------------------
// rampFixturesHaveGenuinelyNonUniformBeats
// ---------------------------------------------------------------------------
//
// A ramp whose beat times are uniformly spaced is worse than a missing ramp
// case: it declares gradual tempo change in its metadata while presenting a
// constant-tempo grid, so every tempo-drift and ramp-following metric would be
// measured against the wrong thing. These are checked against the analytic
// integral of the tempo function, which is what the generator claims to derive
// them from, so a change in the integration is caught too.

JAM_TEST (RhythmCorpus, rampFixturesHaveGenuinelyNonUniformBeats)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    JsonValue root;
    {
        JsonParser parser (c.manifestText);
        try { root = parser.parse(); }
        catch (const std::exception& e)
        {
            jamtest::fail (__FILE__, __LINE__, e.what());
            return;
        }
    }

    int rampsChecked = 0;
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        if (f.tempoProfile != "linear-ramp")
            continue;

        REQUIRE (f.bpmStart > 0.0);
        REQUIRE (f.bpmEnd > 0.0);
        REQUIRE (f.bpmStart != f.bpmEnd);
        REQUIRE (f.beats.size() >= 8);

        // Pull the ramp window out of the raw manifest: the beat times are only
        // interpretable against where the ramp starts and ends.
        const JsonValue& raw = root.at ("fixtures").items[i];
        const double rampStart = raw.at ("rampStartSeconds").numberOr (-1.0);
        const double rampEnd = raw.at ("rampEndSeconds").numberOr (-1.0);
        REQUIRE (rampStart > 0.0);
        REQUIRE (rampEnd > rampStart);

        double minGap = 1e30;
        double maxGap = 0.0;
        for (std::size_t k = 0; k + 1 < f.beats.size(); ++k)
        {
            const double gap = f.beats[k + 1] - f.beats[k];
            if (gap < minGap) minGap = gap;
            if (gap > maxGap) maxGap = gap;

            // Re-integrate the tempo function across this gap. With
            // bpm(t) = b0 + s*(t - t0), the integral over [a, b] is
            //   ( b0*d + s*d*d/2 ) / 60,  d = b - t0,
            // which must equal exactly one beat.
            const double slope = (f.bpmEnd - f.bpmStart) / (rampEnd - rampStart);
            const double da = f.beats[k] - rampStart;
            const double db = f.beats[k + 1] - rampStart;
            const double phase = (f.bpmStart * (db - da)
                                  + 0.5 * slope * (db * db - da * da)) / 60.0;
            if (std::fabs (phase - 1.0) > 1e-6)
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": beat gap " + jamtest::describe (k)
                               + " spans " + jamtest::describe (phase)
                               + " beats of tempo function, not 1.000000");
        }

        // And the spacing must visibly vary. A 1.2x ratio is a deliberate
        // floor: anything smaller is not a ramp a tracker could be tested on.
        CHECK (maxGap / minGap > 1.20);
        ++rampsChecked;
    }

    CHECK_EQ (rampsChecked, 2);   // accelerando and ritardando
}

// ---------------------------------------------------------------------------
// allNineteenRequiredScenarioTagsArePresentExactlyOnce
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, allNineteenRequiredScenarioTagsArePresentExactlyOnce)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    std::map<std::string, int> primaryCounts;

    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        REQUIRE (! f.tags.empty());
        // The first tag is the primary SPEC 12.2 case; the rest are qualifiers.
        primaryCounts[f.tags[0]] += 1;
    }

    for (std::size_t i = 0; i < kRequiredScenarioCount; ++i)
    {
        const std::string required = kRequiredScenarios[i];
        const std::map<std::string, int>::const_iterator it =
            primaryCounts.find (required);
        if (it == primaryCounts.end())
        {
            jamtest::fail (__FILE__, __LINE__,
                           "required SPEC 12.2 scenario is missing: " + required);
            continue;
        }
        CHECK_EQ (it->second, 1);
    }

    // No unexpected primary tags: every fixture must map to a known case.
    CHECK_EQ (primaryCounts.size(), kRequiredScenarioCount);
}

// ---------------------------------------------------------------------------
// tagVocabularyIsClosed
// ---------------------------------------------------------------------------
//
// Tag vocabularies are declared closed so a harness can switch on them without
// tolerating typos, and so a typo cannot silently create a category that
// nothing consumes.

JAM_TEST (RhythmCorpus, tagVocabularyIsClosed)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());
    REQUIRE (! c.scenarioVocabulary.empty());
    REQUIRE (! c.qualifierVocabulary.empty());

    std::set<std::string> scenarios (c.scenarioVocabulary.begin(),
                                     c.scenarioVocabulary.end());
    std::set<std::string> qualifiers (c.qualifierVocabulary.begin(),
                                      c.qualifierVocabulary.end());

    CHECK_EQ (scenarios.size(), kRequiredScenarioCount);
    for (std::size_t i = 0; i < kRequiredScenarioCount; ++i)
        CHECK (scenarios.count (kRequiredScenarios[i]) == 1);

    // Every tag that carries MEMBERSHIP must have a declared membership list.
    // Vocabulary membership alone was the gap that let `core` drift: `core` is
    // not just a label, it defines the denominator of every SPEC 19 gate, so
    // "the tag exists" is not sufficient -- the list behind it must exist and be
    // reconciled with the tag. Agreement itself is checked by
    // coreMembershipListAgreesWithTheCoreTag.
    const char* const kMembershipTags[] = { "core" };
    for (std::size_t t = 0; t < sizeof (kMembershipTags) / sizeof (const char*); ++t)
    {
        const std::string tag = kMembershipTags[t];
        // Present in the qualifier vocabulary?
        if (qualifiers.count (tag) == 0)
            jamtest::fail (__FILE__, __LINE__,
                           "tag '" + tag + "' is not declared in "
                           "tagVocabulary.qualifier");
        // Used by at least one fixture?
        std::size_t users = 0;
        for (std::size_t i = 0; i < c.fixtures.size(); ++i)
        {
            const Fixture& f = c.fixtures[i];
            if (std::find (f.tags.begin(), f.tags.end(), tag) != f.tags.end())
                ++users;
        }
        CHECK_GE (static_cast<double> (users), 1.0);
        // And have a non-empty membership list in the manifest?
        if (tag == "core")
            CHECK (! c.coreFixtures.empty());
    }

    // No fixture may use a tag outside the declared vocabulary.
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        for (std::size_t k = 0; k < f.tags.size(); ++k)
        {
            const bool known = scenarios.count (f.tags[k]) > 0
                            || qualifiers.count (f.tags[k]) > 0;
            if (! known)
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": tag outside the declared vocabulary: "
                               + f.tags[k]);
        }
    }
}

// ---------------------------------------------------------------------------
// meterLicenseAndProvenanceAreDeclaredEverywhere
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, meterLicenseAndProvenanceAreDeclaredEverywhere)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];

        // SPEC 12.2 permits only owned/created or clearly redistributable audio.
        // Every fixture is synthesis, so CC0-1.0 is both the licence and the
        // proof that no third-party recording is being committed.
        CHECK_EQ (f.license, std::string ("CC0-1.0"));
        CHECK (f.provenance.find ("ynthes") != std::string::npos);
        CHECK (f.provenance.find ("third-party") != std::string::npos);

        // Meter must be self-consistent with the declared beat count.
        CHECK (f.meterNumerator > 0 && f.meterDenominator > 0);
        CHECK (f.beatsPerBar > 0);
        // beatsPerBar is the meter's own count, except for compound meters where
        // the beat unit is the dotted quarter (2 per bar of 6/8).
        const bool compound = f.meterDenominator == 8;
        const int expectedPerBar = compound ? 2 : f.meterNumerator;
        CHECK_EQ (f.beatsPerBar, expectedPerBar);

        // Enough material for SPEC 19's "acquire within 2 bars" gate plus at
        // least two bars of steady state to measure against.
        CHECK (static_cast<int> (f.beats.size()) >= 4 * f.beatsPerBar);
    }
}

// ---------------------------------------------------------------------------
// meterSpecificFixturesUseTheIntendedMeter
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, meterSpecificFixturesUseTheIntendedMeter)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    std::map<std::string, const Fixture*> byName;
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
        byName[c.fixtures[i].name] = &c.fixtures[i];

    const std::map<std::string, const Fixture*>::const_iterator waltz =
        byName.find ("waltz_3_4");
    REQUIRE (waltz != byName.end());
    CHECK_EQ (waltz->second->meterNumerator, 3);
    CHECK_EQ (waltz->second->meterDenominator, 4);
    CHECK_EQ (waltz->second->beatsPerBar, 3);

    const std::map<std::string, const Fixture*>::const_iterator six =
        byName.find ("compound_6_8");
    REQUIRE (six != byName.end());
    CHECK_EQ (six->second->meterNumerator, 6);
    CHECK_EQ (six->second->meterDenominator, 8);
    CHECK_EQ (six->second->beatsPerBar, 2);

    // The 3/4 grid must actually have three beats per bar spacing, which is what
    // distinguishes it from a mislabelled 4/4.
    const double waltzBeat = 60.0 / waltz->second->nominalBpm;
    CHECK_NEAR (waltz->second->beats[3] - waltz->second->beats[0],
                3.0 * waltzBeat, 1e-6);
}

// ---------------------------------------------------------------------------
// coreTagMarksTheFixturesSpec19ScoresAgainst
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, coreTagMarksTheFixturesSpec19ScoresAgainst)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    // SPEC 19 phrases its gates against "core fixtures", so the corpus has to
    // say which ones those are rather than leaving the denominator implicit.
    int core = 0;
    int steady = 0;
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        const bool isCore =
            std::find (f.tags.begin(), f.tags.end(), std::string ("core"))
            != f.tags.end();
        const bool isSteady =
            std::find (f.tags.begin(), f.tags.end(), std::string ("steady_tempo"))
            != f.tags.end();
        if (isCore)
        {
            ++core;
            // A core fixture must be steady tempo, or "locked BPM relative
            // error <= 2%" (SPEC 19) has no defined meaning on it.
            CHECK (isSteady);
        }
        if (isSteady)
            ++steady;
    }

    CHECK_GE (core, 8);
    CHECK_GE (steady, 15);
}

// ---------------------------------------------------------------------------
// trueSilenceSpansAreDeclaredAndSelfConsistent
// ---------------------------------------------------------------------------
//
// The `RhythmCorpus.corpusSilenceIsDeclaredEverywhere` split: this checks the
// SHAPE and INTERNAL CONSISTENCY of the field. Whether the audio inside a span
// really is quiet is checked against the WAVs by
// `trueSilenceSpansMatchTheAudioOnDisk`.
//
// SPEC 19 requires "silence does not create false acceleration" and SPEC 12.3 a
// "false beat rate in silence". Both need to know where the guitar stopped
// playing, which is what this field says. The pre-existing `silenceSpans` does
// not: it names narrow windows around beats that were deliberately not played,
// and every beat of a maintained grid falls inside one, so scoring a tracker
// against it counts correct behaviour as 16.7 false beats per second.

JAM_TEST (RhythmCorpus, trueSilenceSpansAreDeclaredAndSelfConsistent)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());

    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];

        // Present on EVERY fixture, even when empty. An absent field cannot be
        // told apart from a bug, and this corpus is only half committed.
        if (! f.hasTrueSilenceSpans)
        {
            jamtest::fail (__FILE__, __LINE__,
                           f.name + ": trueSilenceSpans is missing or malformed");
            continue;
        }

        double previousEnd = -1.0;
        for (std::size_t k = 0; k < f.trueSilenceSpans.size(); ++k)
        {
            const double a = f.trueSilenceSpans[k].first;
            const double b = f.trueSilenceSpans[k].second;
            const std::string where =
                " (span " + jamtest::describe (k) + " [" + jamtest::describe (a)
                + ", " + jamtest::describe (b) + "])";

            if (! (a >= 0.0))
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": span starts before the file" + where);
            if (! (b <= f.durationSeconds))
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": span ends after the file" + where);
            if (! (a < b))
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": span is empty or inverted" + where);
            if (k > 0 && a < previousEnd)
                jamtest::fail (__FILE__, __LINE__,
                               f.name + ": spans are unsorted or overlapping" + where);
            previousEnd = b;

            // A silence span that swallows an attack is the one error that
            // makes the field actively harmful: a tracker beating there is
            // scored as fabricating a beat when it was playing.
            for (std::size_t o = 0; o < f.onsets.size(); ++o)
            {
                const double onset = f.onsets[o];
                if (onset > a && onset < b)
                {
                    jamtest::fail (__FILE__, __LINE__,
                                   f.name + ": span swallows onset "
                                   + jamtest::describe (onset) + where);
                    break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// trueSilenceSpansMatchTheAudioOnDisk
// ---------------------------------------------------------------------------
//
// The half of the contract that needs the WAVs: a declared span is a claim about
// the audio, so the audio gets to disagree.
//
// This is deliberately NOT how the field is produced. The generator derives the
// spans from its own event list and decay model and only then compares against
// the audio, because a field reverse-engineered from the file it describes
// cannot catch a synthesis bug -- it would faithfully describe the bug. This test
// is the independent second opinion on the result, not the source of it.

JAM_TEST (RhythmCorpus, trueSilenceSpansMatchTheAudioOnDisk)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());
    REQUIRE (sha256SelfTest());

    // 50 ms analysis window, 10 ms hop.
    const std::size_t hop = 480;
    const std::size_t win = 2400;

    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        if (! f.hasTrueSilenceSpans || f.trueSilenceSpans.empty())
            continue;

        const std::string path = joinPath (c.dir, f.file);
        REQUIRE (fileExists (path));

        std::vector<double> samples;
        int channels = 0;
        REQUIRE (readWavSamples (path, samples, channels));
        REQUIRE (channels == 1);   // the corpus is mono by declaration
        const std::size_t nSamples = samples.size();
        REQUIRE (nSamples > win);

        // Short-time RMS in dB, plus the file's noise floor estimated as the
        // 5th percentile of that envelope. The floor is the right reference: in
        // a genuine silence the guitar is gone and whatever remains IS the floor.
        std::vector<double> envDb;
        std::vector<double> envT;
        for (std::size_t s = 0; s + win <= nSamples; s += hop)
        {
            double acc = 0.0;
            for (std::size_t k = s; k < s + win; ++k)
                acc += samples[k] * samples[k];
            const double rms = std::sqrt (acc / static_cast<double> (win));
            envDb.push_back (rms > 1e-15 ? 20.0 * std::log10 (rms) : -300.0);
            envT.push_back (static_cast<double> (s + win / 2)
                            / static_cast<double> (f.sampleRate));
        }
        REQUIRE (! envDb.empty());

        std::vector<double> sorted = envDb;
        std::sort (sorted.begin(), sorted.end());
        const double floorDb = sorted[sorted.size() / 20];
        const double halfWin = 0.5 * static_cast<double> (win) / f.sampleRate;

        int windowsInside = 0;
        double loudestInside = -300.0;
        for (std::size_t k = 0; k < envT.size(); ++k)
        {
            const double t = envT[k];
            bool inside = false;
            for (std::size_t q = 0; q < f.trueSilenceSpans.size(); ++q)
            {
                if (f.trueSilenceSpans[q].first <= t - halfWin
                    && t + halfWin <= f.trueSilenceSpans[q].second)
                {
                    inside = true;
                    break;
                }
            }
            if (! inside)
                continue;
            // Skip windows straddling an attack: the boundary is where the
            // attack that ends the span lives, and scoring that as "loud inside
            // the span" would report the span's own edge as a defect.
            bool touchesAttack = false;
            for (std::size_t o = 0; o < f.onsets.size(); ++o)
            {
                if (f.onsets[o] >= t - halfWin - 0.030
                    && f.onsets[o] <= t + halfWin + 0.030)
                {
                    touchesAttack = true;
                    break;
                }
            }
            if (touchesAttack)
                continue;
            ++windowsInside;
            if (envDb[k] > loudestInside)
                loudestInside = envDb[k];
        }

        if (windowsInside == 0)
            continue;   // spans too short to hold a whole window

        // 15 dB of headroom above the floor. A decaying string 60 dB below the
        // peak is inaudible and is not "the guitarist still playing", but it can
        // sit a few dB above a -81 dBFS floor, and the fixtures span a 43 dB
        // range of noise floors, so the test has to be floor-relative. A
        // peak-relative bound would be wrong: on noisy_microphone even silence
        // is only 35 dB below the peak because the hiss floor is -34 dBFS.
        if (loudestInside > floorDb + 15.0)
            jamtest::fail (__FILE__, __LINE__,
                           f.name + ": a declared trueSilenceSpans window reaches "
                           + jamtest::describe (loudestInside) + " dBFS, "
                           + jamtest::describe (loudestInside - floorDb)
                           + " dB above the measured noise floor "
                           + jamtest::describe (floorDb));
        CHECK (windowsInside > 0);
    }
}

// ---------------------------------------------------------------------------
// coreMembershipListAgreesWithTheCoreTag
// ---------------------------------------------------------------------------
//
// This is the test whose absence let the second defect through. The old
// `tagVocabularyIsClosed` only checked that every tag in use appeared in the
// vocabulary; it never checked that the `core` membership list agreed with the
// `core` tag, so the two disagreed by five fixtures for a full wave. SPEC 19 is
// phrased entirely against "core fixtures", so which fixtures those are is a
// load-bearing definition for release gates, and an ambiguous count is not
// acceptable.

JAM_TEST (RhythmCorpus, coreMembershipListAgreesWithTheCoreTag)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.fixtures.empty());
    REQUIRE (! c.coreFixtures.empty());

    std::set<std::string> listed (c.coreFixtures.begin(), c.coreFixtures.end());
    CHECK_EQ (listed.size(), c.coreFixtures.size());   // no duplicates

    std::set<std::string> tagged;
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        if (std::find (f.tags.begin(), f.tags.end(), std::string ("core"))
            != f.tags.end())
            tagged.insert (f.name);
    }

    // Every listed name must be a fixture that exists, and vice versa.
    std::set<std::string> names;
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
        names.insert (c.fixtures[i].name);

    for (std::set<std::string>::const_iterator it = listed.begin();
         it != listed.end(); ++it)
    {
        if (names.count (*it) == 0)
            jamtest::fail (__FILE__, __LINE__,
                           "tagVocabulary.core names '" + *it
                           + "', which is not a fixture. Membership must be "
                             "fixture names, not scenario tags: the two "
                             "namespaces only partly overlap.");
    }
    for (std::set<std::string>::const_iterator it = tagged.begin();
         it != tagged.end(); ++it)
    {
        if (listed.count (*it) == 0)
            jamtest::fail (__FILE__, __LINE__,
                           "fixture '" + *it + "' carries the `core` tag but is "
                           "absent from tagVocabulary.core");
    }
    for (std::set<std::string>::const_iterator it = listed.begin();
         it != listed.end(); ++it)
    {
        if (tagged.count (*it) == 0)
            jamtest::fail (__FILE__, __LINE__,
                           "tagVocabulary.core lists '" + *it
                           + "' which does not carry the `core` tag");
    }

    CHECK_EQ (tagged.size(), listed.size());
    // SPEC 19 phrases its gates against "core fixtures"; a non-trivial,
    // unambiguous denominator is the point.
    CHECK_GE (static_cast<double> (tagged.size()), 10.0);

    // A core fixture must be steady tempo, or "locked BPM relative error <=
    // 2%" (SPEC 19) has no defined meaning on it.
    for (std::size_t i = 0; i < c.fixtures.size(); ++i)
    {
        const Fixture& f = c.fixtures[i];
        if (tagged.count (f.name) == 0)
            continue;
        CHECK (std::find (f.tags.begin(), f.tags.end(),
                          std::string ("steady_tempo")) != f.tags.end());
        // ...and its declared BPM must actually match its declared grid, which
        // steadyFixturesMatchTheirDeclaredNominalBpm checks for every fixture.
        CHECK (f.tempoProfile == "constant");
    }
}

// ---------------------------------------------------------------------------
// manifestIsCanonicallySerialised
// ---------------------------------------------------------------------------

JAM_TEST (RhythmCorpus, manifestIsCanonicallySerialised)
{
    const Corpus c = loadCorpus();
    REQUIRE (! c.dir.empty());
    REQUIRE (! c.manifestText.empty());

    // One line, trailing newline, no CR: the generator writes with sorted keys
    // and fixed separators so the file does not churn in git diffs. Any change
    // to that has to be a deliberate, reviewed one.
    CHECK_EQ (c.manifestText.find ('\n'), c.manifestText.size() - 1);
    CHECK (c.manifestText.find ("\r") == std::string::npos);

    // Compact separators, checked on the JSON skeleton with all string literals
    // blanked out. Testing the raw text would be wrong: a note or tag may
    // legitimately contain '", "' or '": "' inside a string value, and the first
    // version of this check failed for exactly that reason.
    const std::string skeleton = blankStringLiterals (c.manifestText);
    CHECK (skeleton.find (": ") == std::string::npos);
    CHECK (skeleton.find (", ") == std::string::npos);
    CHECK (skeleton.find (' ') == std::string::npos);
    CHECK (skeleton.find ('\t') == std::string::npos);

    // Keys are sorted, which is what actually stops spurious diffs when a field
    // is added. Verified on the top-level object only; that is where an editing
    // tool is most likely to reorder.
    CHECK (topLevelKeysAreSorted (c.manifestText));

    // A pretty-printed manifest would be ~400 kB for 19 fixtures; this one is a
    // single line. The bound catches an accidental reformat while leaving ample
    // room for the manifest to grow.
    CHECK (static_cast<double> (c.manifestText.size()) < 400000.0);
}