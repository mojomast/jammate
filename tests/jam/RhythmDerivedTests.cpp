// Integrity and semantic validation for the EVAL-003 derived perturbations.
//
// DEVPLAN.md 14 asks for robustness scenarios, and SPEC.md 19 phrases the gates
// against failure modes. A tracker comparison is only meaningful if the derived
// clips really are the base audio under exactly one documented change, with a
// ground truth that was transformed by the same amount the audio was. This suite
// re-derives that claim from the committed bytes; it does not trust the
// generator's manifest.
//
// Design rules, matching tests/jam/RhythmCorpusTests.cpp:
//   - JSON is parsed by a small hand-written reader, so the manifest is the one
//     contract and there is no second index that can drift from it.
//   - SHA-256 is recomputed over the committed WAVs, not read from the manifest,
//     so a corrupted or substituted file is a red test.
//   - `testdata/rhythm/derived/` is located via JAM_RHYTHM_DERIVED, the path
//     recorded in __FILE__, and the working directory; failure to locate it is a
//     failed test, never a silently empty run.
//   - The suite deliberately does NOT re-run the generator. Determinism is a
//     property of generation (proven by `make_derived.py --check`); this suite
//     checks that what was generated is self-consistent and acoustically what it
//     claims to be.
//
// Test code only: it allocates, reads files and is never linked into the audio
// path (SPEC.md 7.1 forbids JSON/wave parsing on the audio thread; nothing here
// runs there).

#include "JamTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// Path discovery
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

/** Finds the corpus directory (the one holding manifest.json).

    `envVar` may point straight at that directory. Otherwise `relativeDir` is a
    repository-relative path to it, searched from ancestors of __FILE__ and then
    of the working directory. */
std::string findCorpusRoot (const char* envVar, const std::string& relativeDir)
{
    if (const char* env = std::getenv (envVar))
    {
        const std::string dir = env;
        if (fileExists (joinPath (dir, "manifest.json")))
            return dir;
    }

    const std::string rel = relativeDir + "/manifest.json";
    {
        std::string dir = parentOf (parentOf (__FILE__));  // <root>/tests
        for (int i = 0; i < 8 && ! dir.empty(); ++i)
        {
            if (fileExists (joinPath (dir, rel)))
                return joinPath (dir, relativeDir);
            dir = parentOf (dir);
        }
    }
    {
        std::string dir = ".";
        for (int i = 0; i < 10; ++i)
        {
            if (fileExists (joinPath (dir, rel)))
                return joinPath (dir, relativeDir);
            const std::string up = parentOf (dir);
            if (up == dir)
                break;
            dir = up;
        }
    }
    return std::string();
}

std::string requireDerivedDir()
{
    const std::string dir = findCorpusRoot ("JAM_RHYTHM_DERIVED",
                                            "testdata/rhythm/derived");
    if (dir.empty())
        jamtest::fail (__FILE__, __LINE__,
                       "could not locate testdata/rhythm/derived/manifest.json. "
                       "Set JAM_RHYTHM_DERIVED to the derived directory.");
    return dir;
}

std::string requireBaseDir()
{
    const std::string dir = findCorpusRoot ("JAM_RHYTHM_CORPUS",
                                            "testdata/rhythm");
    if (dir.empty())
        jamtest::fail (__FILE__, __LINE__,
                       "could not locate testdata/rhythm/manifest.json. Set "
                       "JAM_RHYTHM_CORPUS to the base corpus directory.");
    return dir;
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), recomputed over committed bytes
// ---------------------------------------------------------------------------

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
// Minimal JSON reader (objects, arrays, strings, numbers, bool, null)
// ---------------------------------------------------------------------------

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
    bool isBool() const { return type == Type::Bool; }

    const JsonValue* find (const std::string& key) const
    {
        if (type != Type::Object)
            return nullptr;
        for (std::size_t i = 0; i < members.size(); ++i)
            if (members[i].key == key)
                return members[i].value;
        return nullptr;
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
            if (items[i].type == Type::Number)
                out.push_back (items[i].number);
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

    char peek() const { return i_ >= s_.size() ? '\0' : s_[i_]; }

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
        switch (s_[i_])
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
        JsonValue v;
        v.type = JsonValue::Type::Number;
        try
        {
            v.number = std::stod (s_.substr (start, i_ - start));
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
// Minimal RIFF/WAVE PCM reader (mono or multi-channel 16-bit)
// ---------------------------------------------------------------------------

struct Wav
{
    int sampleRate = 0;
    int channels = 0;
    int bits = 0;
    std::vector<short> pcm;  // interleaved
    std::size_t frames() const
    {
        return channels > 0 ? pcm.size() / static_cast<std::size_t> (channels) : 0;
    }
};

uint32_t readU32 (const unsigned char* p)
{
    return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8)
         | (static_cast<uint32_t> (p[2]) << 16) | (static_cast<uint32_t> (p[3]) << 24);
}

uint16_t readU16 (const unsigned char* p)
{
    return static_cast<uint16_t> (p[0]) | (static_cast<uint16_t> (p[1]) << 8);
}

bool readWav (const std::string& path, Wav& out, std::string& err)
{
    std::ifstream in (path.c_str(), std::ios::binary);
    if (! in.good())
    {
        err = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string bytes = ss.str();
    const unsigned char* p =
        reinterpret_cast<const unsigned char*> (bytes.data());
    const std::size_t n = bytes.size();
    if (n < 12 || std::memcmp (p, "RIFF", 4) != 0 || std::memcmp (p + 8, "WAVE", 4) != 0)
    {
        err = "not a RIFF/WAVE file: " + path;
        return false;
    }

    std::size_t i = 12;
    bool haveFmt = false;
    std::size_t dataOffset = 0, dataSize = 0;
    while (i + 8 <= n)
    {
        const char* id = reinterpret_cast<const char*> (p + i);
        const uint32_t size = readU32 (p + i + 4);
        const std::size_t body = i + 8;
        if (std::memcmp (id, "fmt ", 4) == 0 && body + 16 <= n)
        {
            const uint16_t format = readU16 (p + body);
            out.channels = readU16 (p + body + 2);
            out.sampleRate = static_cast<int> (readU32 (p + body + 4));
            out.bits = readU16 (p + body + 14);
            if (format != 1)
            {
                err = "WAVE is not PCM (format " + std::to_string (format) + ")";
                return false;
            }
            haveFmt = true;
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            dataOffset = body;
            dataSize = size;
            if (dataOffset + dataSize > n)
                dataSize = n - dataOffset;
        }
        i = body + size + (size % 2);
    }

    if (! haveFmt || dataSize == 0 || out.bits != 16)
    {
        err = "missing fmt/data or not 16-bit: " + path;
        return false;
    }
    const std::size_t count = dataSize / 2;
    out.pcm.resize (count);
    for (std::size_t k = 0; k < count; ++k)
        out.pcm[k] = static_cast<short> (
            static_cast<uint16_t> (p[dataOffset + 2 * k])
            | (static_cast<uint16_t> (p[dataOffset + 2 * k + 1]) << 8));
    return true;
}

// ---------------------------------------------------------------------------
// Parsed view of the derived manifest
// ---------------------------------------------------------------------------

struct CFixture
{
    std::string name, file, sha, parent, parentSha, baseline, policy;
    std::vector<std::string> tags;
    long long bytes = 0;
    long long sampleRate = 0;
    long long channels = 0;
    long long bitDepth = 0;
    double duration = 0.0;
    std::vector<double> beats, onsets;
    long long truncStart = 0, truncFrames = 0;

    bool hasOffset = false;      double offsetSeconds = 0.0;
    bool hasSilence = false;     double silenceSeconds = 0.0;
    bool hasRatio = false;       double ratio = 0.0;   double stepTime = 0.0;
    bool hasInjected = false;    long long injectedCount = 0;
    bool hasDropped = false;     long long droppedCount = 0;
    bool hasThreshold = false;   double threshold = 0.0;
    bool hasGain = false;        double gainDb = 0.0;
    bool hasSnr = false;         double snrDb = 0.0;
    bool hasMeasuredSnr = false; double measuredSnr = 0.0;
    double clippedFraction = 0.0;
    double peakDbfs = 0.0, rmsDbfs = 0.0;
    std::vector<double> injectedOnsets, droppedOnsets, gapSpan;
};

double num (const JsonValue& v, const char* key, double fallback)
{
    const JsonValue* p = v.find (key);
    return p != nullptr ? p->numberOr (fallback) : fallback;
}

bool has (const JsonValue& v, const char* key)
{
    return v.find (key) != nullptr;
}

std::string str (const JsonValue& v, const char* key)
{
    const JsonValue* p = v.find (key);
    return p != nullptr ? p->stringOr ("") : std::string();
}

CFixture parseFixture (const JsonValue& item)
{
    CFixture f;
    f.name = str (item, "name");
    f.file = str (item, "file");
    f.sha = str (item, "sha256");
    f.parent = str (item, "parentFixture");
    f.parentSha = str (item, "parentSha256");
    f.baseline = str (item, "pairedBaseline");
    f.bytes = static_cast<long long> (num (item, "bytes", -1));
    f.sampleRate = static_cast<long long> (num (item, "sampleRate", 0));
    f.channels = static_cast<long long> (num (item, "channels", 0));
    f.bitDepth = static_cast<long long> (num (item, "bitDepth", 0));
    f.duration = num (item, "durationSeconds", -1);
    if (const JsonValue* b = item.find ("beats"))
        f.beats = b->numbers();
    if (const JsonValue* o = item.find ("onsets"))
        f.onsets = o->numbers();
    if (const JsonValue* t = item.find ("scenarioTags"))
        f.tags = t->strings();
    if (const JsonValue* tr = item.find ("truncation"))
    {
        f.truncStart = static_cast<long long> (num (*tr, "sourceStartFrame", 0));
        f.truncFrames = static_cast<long long> (num (*tr, "frames", 0));
    }
    if (const JsonValue* gt = item.find ("groundTruth"))
        f.policy = str (*gt, "policy");
    if (const JsonValue* t = item.find ("transformation"))
    {
        const std::string kind = str (*t, "kind");
        if (kind == "onset_offset")
        {
            f.hasOffset = true;
            f.offsetSeconds = num (*t, "offsetSeconds", 0);
        }
        else if (kind == "leading_silence" || kind == "trailing_silence")
        {
            f.hasSilence = true;
            f.silenceSeconds = num (*t, "silenceSeconds", 0);
        }
        else if (kind == "tempo_step")
        {
            f.hasRatio = true;
            f.ratio = num (*t, "ratio", 0);
            f.stepTime = num (*t, "stepTimeSeconds", 0);
        }
        else if (kind == "clipping")
        {
            f.hasThreshold = true;
            f.threshold = num (*t, "thresholdFraction", 0);
        }
        else if (kind == "level")
        {
            f.hasGain = true;
            f.gainDb = num (*t, "gainDb", 0);
        }
        else if (kind == "noise")
        {
            f.hasSnr = true;
            f.snrDb = num (*t, "snrDb", 0);
        }
        else if (kind == "syncopation_burst")
        {
            f.hasInjected = true;
            f.injectedCount = static_cast<long long> (num (*t, "burstCount", -1));
            if (const JsonValue* io = t->find ("injectedOnsets"))
                f.injectedOnsets = io->numbers();
        }
        else if (kind == "drop_onset")
        {
            f.hasDropped = true;
            if (const JsonValue* dof = t->find ("droppedOnsets"))
                f.droppedOnsets = dof->numbers();
            f.droppedCount = static_cast<long long> (f.droppedOnsets.size());
        }
        else if (kind == "silence_gap")
        {
            if (const JsonValue* gs = t->find ("gapSpanSeconds"))
                f.gapSpan = gs->numbers();
        }
    }
    if (const JsonValue* s = item.find ("signal"))
    {
        f.clippedFraction = num (*s, "clippedSampleFraction", 0);
        f.peakDbfs = num (*s, "peakDbfs", 0);
        f.rmsDbfs = num (*s, "rmsDbfs", 0);
        if (has (*s, "measuredSnrDb"))
        {
            f.hasMeasuredSnr = true;
            f.measuredSnr = num (*s, "measuredSnrDb", 0);
        }
    }
    return f;
}

struct Corpus
{
    std::vector<CFixture> fixtures;
    std::map<std::string, const CFixture*> byName;
    std::vector<std::string> perturbationVocab, qualifierVocab;
    std::string baseManifestSha;
    bool parsed = false;
    std::string error;

    const CFixture* get (const std::string& name) const
    {
        const auto it = byName.find (name);
        return it == byName.end() ? nullptr : it->second;
    }
};

bool loadCorpus (const std::string& dir, Corpus& corpus)
{
    bool ok = false;
    const std::string text = readFile (joinPath (dir, "manifest.json"), ok);
    if (! ok)
    {
        corpus.error = "cannot read derived manifest.json";
        jamtest::fail (__FILE__, __LINE__, corpus.error);
        return false;
    }
    try
    {
        JsonParser parser (text);
        const JsonValue root = parser.parse();
        if (! root.isObject())
        {
            corpus.error = "manifest root is not an object";
            jamtest::fail (__FILE__, __LINE__, corpus.error);
            return false;
        }
        if (const JsonValue* corpusObj = root.find ("corpus"))
            corpus.baseManifestSha = str (*corpusObj, "baseManifestSha256");
        if (const JsonValue* tv = root.find ("tagVocabulary"))
        {
            if (const JsonValue* p = tv->find ("perturbation"))
                corpus.perturbationVocab = p->strings();
            if (const JsonValue* q = tv->find ("qualifier"))
                corpus.qualifierVocab = q->strings();
        }
        const JsonValue* fixtures = root.find ("fixtures");
        if (fixtures == nullptr || ! fixtures->isArray())
        {
            corpus.error = "manifest has no fixtures array";
            jamtest::fail (__FILE__, __LINE__, corpus.error);
            return false;
        }
        for (const JsonValue& item : fixtures->items)
            corpus.fixtures.push_back (parseFixture (item));
    }
    catch (const std::exception& e)
    {
        corpus.error = e.what();
        jamtest::fail (__FILE__, __LINE__, corpus.error);
        return false;
    }
    for (const CFixture& f : corpus.fixtures)
        corpus.byName[f.name] = &f;
    corpus.parsed = true;
    return true;
}

// Loads the base manifest and returns fixture name -> sha256.
bool loadBaseHashes (const std::string& dir, std::map<std::string, std::string>& out,
                     std::string& error)
{
    bool ok = false;
    const std::string text = readFile (joinPath (dir, "manifest.json"), ok);
    if (! ok)
    {
        error = "cannot read base manifest.json";
        return false;
    }
    try
    {
        JsonParser parser (text);
        const JsonValue root = parser.parse();
        const JsonValue* fixtures = root.find ("fixtures");
        if (fixtures == nullptr || ! fixtures->isArray())
        {
            error = "base manifest has no fixtures array";
            return false;
        }
        for (const JsonValue& item : fixtures->items)
            out[str (item, "name")] = str (item, "sha256");
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Small numeric helpers
// ---------------------------------------------------------------------------

bool tagsContain (const CFixture& f, const std::string& tag)
{
    return std::find (f.tags.begin(), f.tags.end(), tag) != f.tags.end();
}

std::string primaryTag (const CFixture& f)
{
    return f.tags.empty() ? std::string() : f.tags.front();
}

std::vector<double> readPcmFloats (const std::string& path, std::string& err)
{
    Wav w;
    std::vector<double> out;
    if (! readWav (path, w, err))
        return out;
    out.reserve (w.pcm.size());
    for (short s : w.pcm)
        out.push_back (s / 32767.0);
    return out;
}

double rmsOf (const std::vector<double>& v)
{
    if (v.empty())
        return 0.0;
    double acc = 0.0;
    for (double x : v)
        acc += x * x;
    return std::sqrt (acc / static_cast<double> (v.size()));
}

double rmsOfRange (const std::vector<double>& v, std::size_t a, std::size_t b)
{
    if (b > v.size())
        b = v.size();
    if (a >= b)
        return 0.0;
    double acc = 0.0;
    for (std::size_t i = a; i < b; ++i)
        acc += v[i] * v[i];
    return std::sqrt (acc / static_cast<double> (b - a));
}

double timeToSample (double t, int sampleRate)
{
    return t * static_cast<double> (sampleRate);
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

JAM_TEST (RhythmDerived, manifestIntegrity)
{
    REQUIRE (sha256SelfTest());

    const std::string derivedDir = requireDerivedDir();
    const std::string baseDir = requireBaseDir();
    REQUIRE (! derivedDir.empty());
    REQUIRE (! baseDir.empty());

    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));
    CHECK (corpus.parsed);
    CHECK (corpus.fixtures.size() >= 20);
    CHECK (! corpus.baseManifestSha.empty());

    // The declared base manifest hash must match the base corpus actually on
    // disk: this is the "derived from *this* corpus" link.
    bool ok = false;
    const std::string actualBase = sha256OfFile (
        joinPath (baseDir, "manifest.json"), ok);
    REQUIRE (ok);
    CHECK_EQ (corpus.baseManifestSha, actualBase);

    std::map<std::string, std::string> baseHashes;
    std::string baseError;
    REQUIRE (loadBaseHashes (baseDir, baseHashes, baseError));

    std::set<std::string> perturbationVocab (corpus.perturbationVocab.begin(),
                                             corpus.perturbationVocab.end());
    std::set<std::string> qualifierVocab (corpus.qualifierVocab.begin(),
                                          corpus.qualifierVocab.end());
    CHECK (! perturbationVocab.empty());
    CHECK (! qualifierVocab.empty());

    for (const CFixture& f : corpus.fixtures)
    {
        const std::string label = "fixture " + f.name;

        // Required shape.
        CHECK (! f.name.empty());
        CHECK (! f.file.empty());
        CHECK (! f.sha.empty());
        CHECK (f.bytes > 44);
        CHECK_EQ (f.sampleRate, 48000);
        CHECK_EQ (f.channels, 1);
        CHECK_EQ (f.bitDepth, 16);
        CHECK (f.duration > 0.0);
        CHECK (! f.parent.empty());
        CHECK (! f.parentSha.empty());
        CHECK (! f.policy.empty());
        CHECK (! f.beats.empty());

        // Tag closure: first tag is a perturbation kind, the rest qualifiers.
        REQUIRE (! f.tags.empty());
        CHECK (perturbationVocab.count (primaryTag (f)) == 1);
        for (std::size_t i = 1; i < f.tags.size(); ++i)
            CHECK (qualifierVocab.count (f.tags[i]) == 1);

        // File bytes, then the hash recomputed over those bytes.
        const std::string path = joinPath (derivedDir, f.file);
        std::ifstream in (path.c_str(), std::ios::binary | std::ios::ate);
        if (! in.good())
        {
            CHECK (false);
            continue;
        }
        const long long size = static_cast<long long> (in.tellg());
        CHECK_EQ (size, f.bytes);

        bool hok = false;
        const std::string sha = sha256OfFile (path, hok);
        CHECK (hok);
        CHECK_EQ (sha, f.sha);

        // Parent inheritance.
        const auto ph = baseHashes.find (f.parent);
        REQUIRE (ph != baseHashes.end());
        CHECK_EQ (f.parentSha, ph->second);
        CHECK (! f.baseline.empty());
    }
}

JAM_TEST (RhythmDerived, pairedBaselinePerParent)
{
    const std::string derivedDir = requireDerivedDir();
    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));

    std::map<std::string, std::vector<const CFixture*>> byParent;
    for (const CFixture& f : corpus.fixtures)
        byParent[f.parent].push_back (&f);

    for (const auto& entry : byParent)
    {
        const std::string& parent = entry.first;
        const CFixture* baseline = nullptr;
        for (const CFixture* f : entry.second)
        {
            if (primaryTag (*f) == "baseline")
                baseline = f;
        }
        REQUIRE (baseline != nullptr);
        CHECK (tagsContain (*baseline, "paired_baseline"));
        CHECK_EQ (baseline->policy, std::string ("inherit"));

        // Every sibling points at the baseline and shares the same parent hash.
        for (const CFixture* f : entry.second)
        {
            CHECK_EQ (f->baseline, baseline->name);
            CHECK_EQ (f->parentSha, baseline->parentSha);
            CHECK_EQ (f->parent, parent);
        }

        // The unextended clips (amplitude perturbations) have exactly the
        // baseline window's frame count; the padding clips are longer.
        for (const CFixture* f : entry.second)
        {
            if (primaryTag (*f) == "leading_silence"
                || primaryTag (*f) == "trailing_silence")
                continue;
            CHECK_EQ (f->truncFrames, baseline->truncFrames);
        }
    }

    // Two parents, so the axis is not a single-recording artefact.
    CHECK (byParent.size() >= 2);
}

JAM_TEST (RhythmDerived, amplitudePerturbationsInheritTruthExactly)
{
    const std::string derivedDir = requireDerivedDir();
    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));

    int checked = 0;
    for (const CFixture& f : corpus.fixtures)
    {
        const std::string kind = primaryTag (f);
        const bool amplitude = (kind == "noise" || kind == "level"
                                || kind == "clipping" || kind == "baseline");
        if (! amplitude)
            continue;
        const CFixture* base = corpus.get (f.baseline);
        REQUIRE (base != nullptr);
        CHECK_EQ (f.policy, std::string ("inherit"));
        REQUIRE (f.beats.size() == base->beats.size());
        REQUIRE (f.onsets.size() == base->onsets.size());
        for (std::size_t i = 0; i < f.beats.size(); ++i)
            CHECK_NEAR (f.beats[i], base->beats[i], 1e-9);
        for (std::size_t i = 0; i < f.onsets.size(); ++i)
            CHECK_NEAR (f.onsets[i], base->onsets[i], 1e-9);
        ++checked;
    }
    // noise x3 + level x3 + clipping x3 + baseline x2
    CHECK (checked >= 11);
}

JAM_TEST (RhythmDerived, timingPerturbationsTransformTruth)
{
    const std::string derivedDir = requireDerivedDir();
    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));

    int shifts = 0, warps = 0;
    for (const CFixture& f : corpus.fixtures)
    {
        const std::string kind = primaryTag (f);
        const CFixture* base = corpus.get (f.baseline);
        REQUIRE (base != nullptr);

        if (kind == "onset_offset" || kind == "leading_silence")
        {
            const double shift = f.hasOffset ? f.offsetSeconds : f.silenceSeconds;
            CHECK (shift > 0.0);
            CHECK (f.policy == "shift");
            CHECK (f.beats.size() == base->beats.size());
            for (std::size_t i = 0; i < f.beats.size(); ++i)
                CHECK_NEAR (f.beats[i], base->beats[i] + shift, 1e-9);
            for (std::size_t i = 0; i < f.onsets.size(); ++i)
                CHECK_NEAR (f.onsets[i], base->onsets[i] + shift, 1e-9);
            ++shifts;
        }
        else if (kind == "trailing_silence")
        {
            CHECK (f.policy == "append-silence");
            CHECK (f.beats.size() == base->beats.size());
            for (std::size_t i = 0; i < f.beats.size(); ++i)
                CHECK_NEAR (f.beats[i], base->beats[i], 1e-9);
            CHECK (f.duration > base->duration);
        }
        else if (kind == "tempo_step")
        {
            CHECK (f.policy == "warp");
            REQUIRE (f.hasRatio);
            CHECK (f.ratio > 0.0);
            REQUIRE (f.beats.size() == base->beats.size());
            const double Ts = f.stepTime;
            for (std::size_t i = 0; i < f.beats.size(); ++i)
            {
                const double t = base->beats[i];
                const double expected =
                    t <= Ts ? t : Ts + (t - Ts) / f.ratio;
                CHECK_NEAR (f.beats[i], expected, 1e-6);
            }
            // Non-uniform: the spacing before the anchor differs from after by
            // exactly the ratio, which is what makes this a coherent warp rather
            // than a label shuffle.
            for (std::size_t i = 1; i + 1 < base->beats.size(); ++i)
            {
                const double before = base->beats[i] - base->beats[i - 1];
                const double after = f.beats[i + 1] - f.beats[i];
                if (base->beats[i] > Ts + 1e-9)
                    CHECK_NEAR (after, before / f.ratio, 1e-6);
            }
            ++warps;
        }
    }
    CHECK (shifts >= 3);  // offset x2 + leading x1
    CHECK (warps >= 1);
}

JAM_TEST (RhythmDerived, evidenceEditsKeepGridEditOnsets)
{
    const std::string derivedDir = requireDerivedDir();
    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));

    int edits = 0;
    for (const CFixture& f : corpus.fixtures)
    {
        const std::string kind = primaryTag (f);
        const bool edit = (kind == "drop_onset" || kind == "syncopation_burst"
                           || kind == "silence_gap");
        if (! edit)
            continue;
        const CFixture* base = corpus.get (f.baseline);
        REQUIRE (base != nullptr);

        // The player's pulse did not move: the metric grid is inherited exactly.
        REQUIRE (f.beats.size() == base->beats.size());
        for (std::size_t i = 0; i < f.beats.size(); ++i)
            CHECK_NEAR (f.beats[i], base->beats[i], 1e-9);

        if (kind == "drop_onset")
        {
            CHECK (f.droppedCount > 0);
            CHECK (f.onsets.size() + static_cast<std::size_t> (f.droppedCount)
                   == base->onsets.size());
            // Every surviving onset is one of the baseline onsets.
            for (double o : f.onsets)
            {
                bool found = false;
                for (double b : base->onsets)
                    if (std::fabs (o - b) < 1e-9) { found = true; break; }
                CHECK (found);
            }
        }
        else if (kind == "syncopation_burst")
        {
            CHECK (f.hasInjected);
            CHECK (f.injectedCount > 0);
            CHECK (f.onsets.size()
                   == base->onsets.size() + static_cast<std::size_t> (f.injectedCount));
            // Every baseline onset survives, plus the injected offbeats.
            for (double b : base->onsets)
            {
                bool found = false;
                for (double o : f.onsets)
                    if (std::fabs (o - b) < 1e-9) { found = true; break; }
                CHECK (found);
            }
        }
        else
        {
            // silence_gap removes onsets inside the carved span and adds none.
            REQUIRE (f.gapSpan.size() == 2);
            CHECK (f.onsets.size() <= base->onsets.size());
            for (double o : f.onsets)
            {
                CHECK (! (o > f.gapSpan[0] && o < f.gapSpan[1]));
            }
        }
        ++edits;
    }
    // drop x2 + burst x2 + gap x1
    CHECK (edits >= 5);
}

JAM_TEST (RhythmDerived, baselineBytesMatchParentSlice)
{
    const std::string derivedDir = requireDerivedDir();
    const std::string baseDir = requireBaseDir();
    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));

    int baselines = 0;
    for (const CFixture& f : corpus.fixtures)
    {
        if (primaryTag (f) != "baseline")
            continue;
        // Locate the parent file via the base manifest.
        std::string parentFile;
        {
            bool ok = false;
            const std::string text = readFile (joinPath (baseDir, "manifest.json"), ok);
            REQUIRE (ok);
            JsonParser parser (text);
            const JsonValue root = parser.parse();
            for (const JsonValue& item : root.find ("fixtures")->items)
                if (str (item, "name") == f.parent)
                    parentFile = str (item, "file");
        }
        REQUIRE (! parentFile.empty());

        std::string err;
        Wav derived, parent;
        REQUIRE (readWav (joinPath (derivedDir, f.file), derived, err));
        REQUIRE (readWav (joinPath (baseDir, parentFile), parent, err));
        REQUIRE (derived.channels == parent.channels);
        REQUIRE (derived.frames() == static_cast<std::size_t> (f.truncFrames));
        REQUIRE (static_cast<std::size_t> (f.truncStart) + derived.frames()
                 <= parent.frames());
        bool equal = true;
        for (std::size_t i = 0; i < derived.pcm.size(); ++i)
            if (derived.pcm[i]
                != parent.pcm[static_cast<std::size_t> (f.truncStart)
                             * static_cast<std::size_t> (derived.channels) + i])
            {
                equal = false;
                break;
            }
        CHECK (equal);
        ++baselines;
    }
    CHECK_EQ (baselines, 2);
}

JAM_TEST (RhythmDerived, independentAcousticProperties)
{
    const std::string derivedDir = requireDerivedDir();
    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));

    std::map<std::string, std::vector<double>> pcmCache;

    auto pcmOf = [&] (const std::string& file, std::vector<double>& out) -> bool
    {
        const auto it = pcmCache.find (file);
        if (it != pcmCache.end())
        {
            out = it->second;
            return true;
        }
        std::string err;
        out = readPcmFloats (joinPath (derivedDir, file), err);
        if (out.empty())
            return false;
        pcmCache[file] = out;
        return true;
    };

    int measured = 0;
    for (const CFixture& f : corpus.fixtures)
    {
        const std::string kind = primaryTag (f);
        const CFixture* base = corpus.get (f.baseline);
        REQUIRE (base != nullptr);

        std::vector<double> audio, baselinePcm;
        REQUIRE (pcmOf (f.file, audio));
        REQUIRE (pcmOf (base->file, baselinePcm));
        REQUIRE (audio.size() == baselinePcm.size() || kind == "leading_silence"
                 || kind == "trailing_silence" || kind == "tempo_step");

        if (kind == "noise")
        {
            REQUIRE (f.hasSnr);
            REQUIRE (f.hasMeasuredSnr);
            // Recompute SNR independently from the committed bytes.
            REQUIRE (audio.size() == baselinePcm.size());
            std::vector<double> noise (audio.size());
            for (std::size_t i = 0; i < audio.size(); ++i)
                noise[i] = audio[i] - baselinePcm[i];
            const double snr = 20.0 * std::log10 (rmsOf (baselinePcm) / rmsOf (noise));
            CHECK_NEAR (snr, f.snrDb, 0.15);
            CHECK_NEAR (f.measuredSnr, f.snrDb, 0.15);
            ++measured;
        }
        else if (kind == "level")
        {
            REQUIRE (f.hasGain);
            REQUIRE (audio.size() == baselinePcm.size());
            // Exact per-sample mapping, not a dB tolerance: the derived sample
            // must be the baseline sample scaled by the declared gain. A dB
            // tolerance would be dominated by 16-bit quantisation at -60 dB and
            // would not notice a wrong gain that happened to be close.
            const double gain = std::pow (10.0, f.gainDb / 20.0);
            long long mismatches = 0;
            for (std::size_t i = 0; i < audio.size(); ++i)
            {
                const long long got = std::llround (audio[i] * 32767.0);
                const long long want = std::llround (baselinePcm[i] * gain * 32767.0);
                if (std::llabs (got - want) > 1)
                    ++mismatches;
            }
            CHECK_EQ (mismatches, 0LL);
            ++measured;
        }
        else if (kind == "clipping")
        {
            REQUIRE (f.hasThreshold);
            const int ceiling = static_cast<int> (
                std::floor (f.threshold * 32767.0 + 0.5));
            long long hits = 0;
            for (std::size_t i = 0; i < audio.size(); ++i)
            {
                const long long q = static_cast<long long> (
                    std::floor (audio[i] * 32767.0 + 0.5));
                const long long a = q < 0 ? -q : q;
                if (a >= ceiling - 1 && a <= ceiling + 1)
                    ++hits;
            }
            const double frac = static_cast<double> (hits)
                              / static_cast<double> (audio.size());
            CHECK_NEAR (frac, f.clippedFraction, 1e-6);
            CHECK (f.clippedFraction > 0.0);
            ++measured;
        }
        else if (kind == "onset_offset")
        {
            REQUIRE (f.hasOffset);
            const long long n = static_cast<long long> (
                std::llround (f.offsetSeconds * 48000.0));
            REQUIRE (n > 0);
            REQUIRE (audio.size() == baselinePcm.size());
            bool headSilent = true, tailShifted = true;
            for (long long i = 0; i < n && i < static_cast<long long> (audio.size()); ++i)
                if (std::fabs (audio[static_cast<std::size_t> (i)]) > 1e-9)
                    headSilent = false;
            for (std::size_t i = static_cast<std::size_t> (n); i < audio.size(); ++i)
                if (audio[i] != baselinePcm[i - static_cast<std::size_t> (n)])
                {
                    tailShifted = false;
                    break;
                }
            CHECK (headSilent);
            CHECK (tailShifted);
            ++measured;
        }
        else if (kind == "drop_onset")
        {
            REQUIRE (! f.droppedOnsets.empty());
            // Every dropped attack's local energy is lower than the baseline's.
            long long lower = 0;
            for (double t : f.droppedOnsets)
            {
                const std::size_t a = static_cast<std::size_t> (
                    std::max (0.0, timeToSample (t, 48000) - 240.0));
                const std::size_t b = static_cast<std::size_t> (
                    timeToSample (t, 48000) + 2880.0);
                if (rmsOfRange (audio, a, b) < rmsOfRange (baselinePcm, a, b))
                    ++lower;
            }
            CHECK_EQ (lower, static_cast<long long> (f.droppedOnsets.size()));
            ++measured;
        }
        else if (kind == "syncopation_burst")
        {
            REQUIRE (! f.injectedOnsets.empty());
            long long louder = 0;
            for (double t : f.injectedOnsets)
            {
                const std::size_t a = static_cast<std::size_t> (
                    timeToSample (t, 48000));
                const std::size_t b = a + 2880;
                if (rmsOfRange (audio, a, b) > rmsOfRange (baselinePcm, a, b))
                    ++louder;
            }
            CHECK_EQ (louder, static_cast<long long> (f.injectedOnsets.size()));
            ++measured;
        }
        else if (kind == "silence_gap")
        {
            REQUIRE (f.gapSpan.size() == 2);
            const std::size_t a = static_cast<std::size_t> (
                timeToSample (f.gapSpan[0], 48000) + 480.0);  // inside the taper
            const std::size_t b = static_cast<std::size_t> (
                timeToSample (f.gapSpan[1], 48000) - 480.0);
            CHECK (rmsOfRange (audio, a, b) < 1e-4);
            ++measured;
        }
    }
    CHECK (measured >= 12);
}

JAM_TEST (RhythmDerived, requiredAxesPresent)
{
    const std::string derivedDir = requireDerivedDir();
    Corpus corpus;
    REQUIRE (loadCorpus (derivedDir, corpus));

    std::map<std::string, int> count;
    for (const CFixture& f : corpus.fixtures)
        count[primaryTag (f)] += 1;

    // Controlled noise / level / clipping curves need at least three points to
    // be a curve rather than a single level.
    CHECK (count["noise"] >= 3);
    CHECK (count["level"] >= 3);
    CHECK (count["clipping"] >= 3);
    // Meaningful timing perturbations.
    CHECK (count["onset_offset"] >= 2);
    CHECK (count["tempo_step"] >= 1);
    CHECK (count["leading_silence"] >= 1);
    CHECK (count["trailing_silence"] >= 1);
    CHECK (count["drop_onset"] >= 1);
    CHECK (count["syncopation_burst"] >= 1);
}
