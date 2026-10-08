// EVAL-LIVE-001 replay support logic self-test.
//
// Exercises the exact JSON serializer and synthetic WAV fixture reader the
// actual-processor harness uses (ReplaySupport.h), with no processor involved
// and no stub processor anywhere. This runs on the frozen base even when the
// live pipeline is not merged, so the harness's pure logic is proven before the
// orchestrator performs the actual replay.
#include "ReplaySupport.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        ++failures;
        std::fprintf (stdout, "  FAIL: %s\n", what);
    }
}

std::string capture (void (*fn) (std::FILE*, void*), void* arg)
{
    std::FILE* f = std::tmpfile();
    fn (f, arg);
    std::fflush (f);
    std::fseek (f, 0, SEEK_END);
    const long n = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);
    std::string out ((std::size_t) (n > 0 ? n : 0), '\0');
    if (n > 0) std::fread (&out[0], 1, (std::size_t) n, f);
    std::fclose (f);
    return out;
}

void writeNum (std::FILE* f, void* arg) { replay::jsonNumber (f, *static_cast<double*> (arg)); }
void writeU64 (std::FILE* f, void* arg) { replay::jsonU64 (f, *static_cast<std::uint64_t*> (arg)); }
void writeBool (std::FILE* f, void* arg) { replay::jsonBool (f, *static_cast<bool*> (arg)); }

void put16 (std::vector<unsigned char>& v, std::uint16_t x)
{
    v.push_back ((unsigned char) (x & 0xFF));
    v.push_back ((unsigned char) ((x >> 8) & 0xFF));
}
void put32 (std::vector<unsigned char>& v, std::uint32_t x)
{
    v.push_back ((unsigned char) (x & 0xFF));
    v.push_back ((unsigned char) ((x >> 8) & 0xFF));
    v.push_back ((unsigned char) ((x >> 16) & 0xFF));
    v.push_back ((unsigned char) ((x >> 24) & 0xFF));
}
void putTag (std::vector<unsigned char>& v, const char* t) { v.insert (v.end(), t, t + 4); }

std::vector<unsigned char> makeWav (const std::vector<std::int16_t>& samples, std::uint16_t bits = 16)
{
    std::vector<unsigned char> v;
    const std::uint32_t dataBytes = (std::uint32_t) (samples.size() * 2);
    putTag (v, "RIFF"); put32 (v, 36 + dataBytes); putTag (v, "WAVE");
    putTag (v, "fmt "); put32 (v, 16);
    put16 (v, 1); put16 (v, 1); put32 (v, 48000);
    put32 (v, 48000u * 2u); put16 (v, 2); put16 (v, bits);
    putTag (v, "data"); put32 (v, dataBytes);
    for (auto s : samples) put16 (v, (std::uint16_t) s);
    return v;
}
} // namespace

int main (int argc, char** argv)
{
    std::fprintf (stdout, "EVAL-LIVE-001 replay support self-test\n");
    std::fprintf (stdout, "======================================\n");

    double nan = std::nan ("");
    double inf = std::numeric_limits<double>::infinity();
    double one = 1.5;
    check (capture (writeNum, &nan) == "null", "NaN must serialize as JSON null");
    check (capture (writeNum, &inf) == "null", "Inf must serialize as JSON null");
    check (capture (writeNum, &one) == "1.5", "finite number must serialize");
    std::uint64_t big = 18446744073709551615ull;
    check (capture (writeU64, &big) == "18446744073709551615", "u64 max must serialize");
    bool t = true, f = false;
    check (capture (writeBool, &t) == "true" && capture (writeBool, &f) == "false",
           "bools must serialize as true/false");

    const std::string path = argc > 1 ? argv[1] : "/home/mojo/projects/guitars-build-resume/tmp/replay-selftest.wav";
    const std::vector<std::int16_t> truth = { 0, 16384, -16384, 32767, -32768, 8192, -8192, 1 };
    {
        const auto bytes = makeWav (truth);
        std::FILE* f = std::fopen (path.c_str(), "wb");
        check (f != nullptr, "cannot open self-test WAV for writing");
        if (f)
        {
            std::fwrite (bytes.data(), 1, bytes.size(), f);
            std::fclose (f);
        }
    }
    {
        replay::WavMono wav;
        check (replay::loadWavMono (path, wav), "valid 16-bit WAV must load");
        check (wav.ok && wav.rate == 48000.0, "WAV rate must be 48000");
        check (wav.samples.size() == truth.size(), "WAV frame count must match");
        bool samplesOk = wav.samples.size() == truth.size();
        for (std::size_t i = 0; i < wav.samples.size() && i < truth.size(); ++i)
        {
            const float expect = (float) ((double) truth[i] / 32768.0);
            if (std::fabs (wav.samples[i] - expect) > 1.0e-6f) samplesOk = false;
        }
        check (samplesOk, "WAV samples must round-trip exactly (within 1 LSB)");
    }
    {
        // Unsupported bit depth must fail closed.
        const auto bytes = makeWav ({ 0, 1, 2 }, 8);
        const std::string p2 = path + ".bad";
        std::FILE* f = std::fopen (p2.c_str(), "wb");
        if (f) { std::fwrite (bytes.data(), 1, bytes.size(), f); std::fclose (f); }
        replay::WavMono wav;
        check (! replay::loadWavMono (p2, wav), "8-bit WAV must be rejected");
        std::remove (p2.c_str());
    }
    {
        const std::string p3 = path + ".trunc";
        std::FILE* f = std::fopen (p3.c_str(), "wb");
        if (f) { const char* junk = "not a wav"; std::fwrite (junk, 1, 9, f); std::fclose (f); }
        replay::WavMono wav;
        check (! replay::loadWavMono (p3, wav), "non-WAV bytes must be rejected");
        std::remove (p3.c_str());
    }

    {
        // N6 cursor tracker: first reported recorded before the have-flag; a
        // genuinely cold zero is valid, and coalescing/skip/future are distinct.
        replay::CursorTracker t;
        t.observe (true, 128, 0);
        check (t.have && t.first == 0, "cold zero first cursor must be recorded");
        t.observe (true, 256, 0);
        check (t.coalesced == 1 && t.skipped == 1, "coalesced/skipped must count repeats");
        t.observe (true, 384, 256);
        check (t.monotonic && t.future == 0, "monotone advance must not be future");
        t.observe (true, 400, 512);
        check (t.future == 1, "reported beyond produced must be future");
        t.observe (true, 500, 300);
        check (! t.monotonic, "backwards report must clear monotonic");

        replay::CursorTracker nz;
        nz.observe (true, 128, 128);
        check (nz.first == 128, "nonzero first cursor must be recorded");
    }
    {
        // N1 bounded readiness poll: stops on first ready; reports attempts.
        int n = 0;
        auto pr = replay::pollUntil ([&] { ++n; return n >= 4; }, 10, [] {});
        check (pr.ready && pr.attempts == 4, "pollUntil must stop on first ready");
        auto pr2 = replay::pollUntil ([] { return false; }, 5, [] {});
        check (! pr2.ready && pr2.attempts == 5, "pollUntil must report exhaustion");
    }

    if (failures == 0)
    {
        std::fprintf (stdout, "\nREPLAY SUPPORT SELF-TEST PASS\n");
        return 0;
    }
    std::fprintf (stdout, "\nREPLAY SUPPORT SELF-TEST FAIL (%d)\n", failures);
    return 2;
}
