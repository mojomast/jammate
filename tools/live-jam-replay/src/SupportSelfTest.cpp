// EVAL-LIVE-001 replay support logic self-test.
//
// Exercises the exact JSON serializer and synthetic WAV fixture reader the
// actual-processor harness uses (ReplaySupport.h), with no processor involved
// and no stub processor anywhere. This runs on the frozen base even when the
// live pipeline is not merged, so the harness's pure logic is proven before the
// orchestrator performs the actual replay.
#include "LiveJamObserved.h"
#include "ReplayInput.h"
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

std::vector<unsigned char> makeWav (const std::vector<std::int16_t>& samples, std::uint16_t bits = 16,
                                    std::uint32_t rate = 48000)
{
    std::vector<unsigned char> v;
    const std::uint32_t dataBytes = (std::uint32_t) (samples.size() * 2);
    putTag (v, "RIFF"); put32 (v, 36 + dataBytes); putTag (v, "WAVE");
    putTag (v, "fmt "); put32 (v, 16);
    put16 (v, 1); put16 (v, 1); put32 (v, rate);
    put32 (v, rate * 2u); put16 (v, 2); put16 (v, bits);
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
        // A zero sample rate must be rejected, not divided by.
        const auto bytes = makeWav ({ 0, 1, 2 }, 16, 0);
        const std::string p4 = path + ".srzero";
        std::FILE* f = std::fopen (p4.c_str(), "wb");
        if (f) { std::fwrite (bytes.data(), 1, bytes.size(), f); std::fclose (f); }
        replay::WavMono wav;
        check (! replay::loadWavMono (p4, wav), "zero sample rate WAV must be rejected");
        std::remove (p4.c_str());
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

    {
        // N3/narrow: backend identity is captured from a real JamLiveState,
        // independent of any join/playback outcome.
        replay::BackendObservation b;
        jam::JamLiveState s {};
        b.observe (s);                              // unprepared: ignored
        check (! b.observed && std::strcmp (b.label(), "unknown") == 0,
               "unprepared state must not be observed");
        s.prepared = true;
        s.backend = jam::JamLiveBackend::unavailable;
        b.observe (s);
        check (b.observed && std::strcmp (b.label(), "unavailable") == 0,
               "prepared unavailable backend must be recorded as unavailable");

        replay::BackendObservation b2;
        jam::JamLiveState e {};
        e.prepared = true;
        e.backend = jam::JamLiveBackend::experimentalBTrack;
        b2.observe (e);
        check (std::strcmp (b2.label(), "experimentalBTrack") == 0 && ! b2.changed,
               "experimental backend must be recorded without a join");
        jam::JamLiveState inj {};
        inj.prepared = true;
        inj.backend = jam::JamLiveBackend::injectedTest;
        b2.observe (inj);
        check (b2.changed && std::strcmp (b2.label(), "experimentalBTrack") == 0
               && std::strcmp (replay::liveBackendName (b2.last), "injectedTest") == 0,
               "a backend change must fail closed and keep the first identity");
    }

    {
        // Fourth correction: audio-frame input timeline (ReplayInput.h).
        auto mono = [] (std::vector<float> s, double rate)
        {
            replay::WavMono w; w.samples = std::move (s); w.rate = rate; w.ok = true; return w;
        };
        auto maxIndexFrom = [] (const float* p, int n, int from)
        {
            int bi = from; float bv = -1.0f;
            for (int i = from; i < n; ++i) if (std::fabs (p[i]) > bv) { bv = std::fabs (p[i]); bi = i; }
            return bi;
        };

        // Ramp fs=4, device=8: sourcePosition = frame*0.5; exact wrapped interp.
        {
            auto w = mono ({ 0.0f, 0.25f, 0.5f, 0.75f }, 4.0);
            replay::InputGen g; g.wav = &w; g.deviceRate = 8.0; g.reset (1);
            float c0[8] = {}, c1[8] = {}; float* ch[2] = { c0, c1 };
            g.fill (ch, 2, 8);
            const float expect[8] = { 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.375f };
            bool ok = true;
            for (int i = 0; i < 8; ++i)
                if (std::fabs (c0[i] - expect[i]) > 1.0e-6f) ok = false;
            check (ok, "ramp fs=4 device=8 must give the exact wrapped interpolation");
            check (c0[0] == c1[0] && c0[7] == c1[7], "stereo must equal mono (coherent phase)");
            check (g.deviceFrame == 8, "source must advance by N (8), not 2N");
        }

        // 48k source on 96k device: index step 0.5.
        {
            std::vector<float> s (16, 0.0f);
            for (int i = 0; i < 16; ++i) s[(std::size_t) i] = (float) i;
            auto w = mono (s, 48000.0);
            replay::InputGen g; g.wav = &w; g.deviceRate = 96000.0; g.reset (1);
            float c0[4] = {}; float* ch[1] = { c0 };
            g.fill (ch, 1, 4);
            // device frames 0..3 -> source positions 0,0.5,1.0,1.5 -> 0,0.5,1,1.5
            check (std::fabs (c0[0] - 0.0f) < 1.0e-6f && std::fabs (c0[1] - 0.5f) < 1.0e-6f
                   && std::fabs (c0[2] - 1.0f) < 1.0e-6f && std::fabs (c0[3] - 1.5f) < 1.0e-6f,
                   "48k->96k must step the source index by 0.5");
        }

        // Noninteger 44.1k source on 48k device: pos = frame*44100/48000.
        {
            auto w = mono ({ 0.0f, 1.0f }, 44100.0);
            replay::InputGen g; g.wav = &w; g.deviceRate = 48000.0; g.reset (1);
            float c0[1] = {}; float* ch[1] = { c0 };
            g.fill (ch, 1, 1);                       // frame 0 -> pos 0 -> 0
            float c1b[1] = {}; float* ch2[1] = { c1b };
            g.fill (ch2, 1, 1);                      // frame 1 -> pos 0.91875 -> ~0.91875
            check (std::fabs (c1b[0] - 0.91875f) < 1.0e-4f, "44.1k->48k noninteger ratio must interpolate");
        }

        // Repeated wrap preserves the source duration.
        {
            auto w = mono ({ 0.0f, 1.0f }, 2.0);
            replay::InputGen g; g.wav = &w; g.deviceRate = 2.0; g.reset (1);
            float c0[3] = {}; float* ch[1] = { c0 };
            g.fill (ch, 1, 3);                       // pos 0,1,2(wrap->0)
            check (std::fabs (c0[0]) < 1.0e-6f && std::fabs (c0[1] - 1.0f) < 1.0e-6f
                   && std::fabs (c0[2]) < 1.0e-6f, "repeated wrap must preserve the source period");
        }

        // Realistic beat: fs=48 impulse every 24000 -> device 48k beat 24000,
        // device 96k beat 48000.
        {
            std::vector<float> s (48000, 0.0f);
            s[0] = 1.0f; s[24000] = 1.0f;
            auto w = mono (s, 48000.0);
            replay::InputGen g48; g48.wav = &w; g48.deviceRate = 48000.0; g48.reset (1);
            std::vector<float> b48 (48000, 0.0f);
            float* ch48[1] = { b48.data() };
            g48.fill (ch48, 1, 48000);
            check (maxIndexFrom (b48.data(), 48000, 1) == 24000, "48k beat must land at device frame 24000");
            replay::InputGen g96; g96.wav = &w; g96.deviceRate = 96000.0; g96.reset (1);
            std::vector<float> b96 (96000, 0.0f);
            float* ch96[1] = { b96.data() };
            g96.fill (ch96, 1, 96000);
            check (maxIndexFrom (b96.data(), 96000, 1) == 48000, "96k beat must land at device frame 48000");
        }

        // Chunk invariance: 128 in one call equals 64+64.
        {
            auto w = mono ({ 0.0f, 0.25f, 0.5f, 0.75f }, 4.0);
            replay::InputGen a; a.wav = &w; a.deviceRate = 8.0; a.reset (7);
            float whole[128] = {}; float* cha[1] = { whole };
            a.fill (cha, 1, 128);
            replay::InputGen b; b.wav = &w; b.deviceRate = 8.0; b.reset (7);
            float part[128] = {}; float* chb[1] = { part };
            float* chb2[1] = { part + 64 };
            b.fill (chb, 1, 64); b.fill (chb2, 1, 64);
            bool same = true;
            for (int i = 0; i < 128; ++i) if (whole[i] != part[i]) same = false;
            check (same, "chunked fill must equal a single fill");
        }

        // Builtin clean: absolute device time (no per-block reset) and true 120.
        {
            replay::InputGen a; a.kind = replay::InputKind::clean; a.deviceRate = 48000.0; a.reset (3);
            float whole[128] = {}; float* cha[1] = { whole };
            a.fill (cha, 1, 128);
            replay::InputGen b; b.kind = replay::InputKind::clean; b.deviceRate = 48000.0; b.reset (3);
            float part[128] = {}; float* chb[1] = { part };
            float* chb2[1] = { part + 64 };
            b.fill (chb, 1, 64); b.fill (chb2, 1, 64);
            bool same = true;
            for (int i = 0; i < 128; ++i) if (whole[i] != part[i]) same = false;
            check (same, "builtin clean must not reset phase per block");
            check (std::fabs (whole[0] - (float) replay::builtinCleanAt (0.0)) < 1.0e-6f,
                   "builtin clean must use absolute device time");
        }

        // Noise: LCG once per frame, replicated across channels.
        {
            replay::InputGen g; g.kind = replay::InputKind::noise; g.deviceRate = 48000.0; g.reset (9);
            float c0[16] = {}, c1[16] = {}; float* ch[2] = { c0, c1 };
            g.fill (ch, 2, 16);
            bool same = true;
            for (int i = 0; i < 16; ++i) if (c0[i] != c1[i]) same = false;
            check (same, "noise must be identical per frame across channels");
        }

        // Invalid device rate: zeros, not divide-by-zero.
        {
            replay::InputGen g; g.kind = replay::InputKind::clean; g.deviceRate = 0.0; g.reset (1);
            check (! g.validRate(), "zero device rate must be invalid");
            float c0[4] = { 9, 9, 9, 9 }; float* ch[1] = { c0 };
            g.fill (ch, 1, 4);
            check (c0[0] == 0.0f && c0[3] == 0.0f, "invalid rate must write zeros");
        }
    }

    {
        // Fifth correction: coherent state latch (Defect A).
        replay::StateLatch latch;
        jam::JamLiveState good {};
        good.prepared = true; good.sampleRate = 48000.0; good.audioSampleTime = 128;
        latch.updateFrom (true, good);
        check (latch.have && latch.last.prepared && latch.last.sampleRate == 48000.0,
               "latch must store a valid prepared state");
        const bool updated = latch.updateFrom (false, jam::JamLiveState {});
        check (! updated && latch.last.prepared && latch.last.sampleRate == 48000.0,
               "a false read must retain the last coherent state");

        int n = 0;
        replay::StateLatch p;
        jam::JamLiveState s1 {}; s1.prepared = false; s1.sampleRate = 0.0;
        jam::JamLiveState s2 {}; s2.prepared = true; s2.sampleRate = 44100.0;
        const bool okPoll = p.pollPrepared (
            [&] (jam::JamLiveState& out) { ++n; out = (n < 2) ? s1 : s2; return true; }, 5, [] {});
        check (okPoll && p.last.prepared && p.last.sampleRate == 44100.0,
               "pollPrepared must wait for a coherent prepared state");

        replay::StateLatch q;
        const bool okNever = q.pollPrepared ([] (jam::JamLiveState&) { return false; }, 3, [] {});
        check (! okNever && ! q.have, "pollPrepared must fail explicitly when never read");

        replay::StateLatch rl;
        jam::JamLiveState rel {}; rel.prepared = false; rel.drumsPlaying = false;
        const bool okRel = rl.pollReleased (
            [&] (jam::JamLiveState& out) { out = rel; return true; }, 3, [] {});
        check (okRel && ! rl.last.prepared && ! rl.last.drumsPlaying,
               "pollReleased must accept a coherent released payload");
        replay::StateLatch rp;
        jam::JamLiveState pl {}; pl.prepared = false; pl.drumsPlaying = true;
        const bool okPlay = rp.pollReleased (
            [&] (jam::JamLiveState& out) { out = pl; return true; }, 3, [] {});
        check (! okPlay, "a still-playing payload must not count as released");
    }

    if (failures == 0)
    {
        std::fprintf (stdout, "\nREPLAY SUPPORT SELF-TEST PASS\n");
        return 0;
    }
    std::fprintf (stdout, "\nREPLAY SUPPORT SELF-TEST FAIL (%d)\n", failures);
    return 2;
}
