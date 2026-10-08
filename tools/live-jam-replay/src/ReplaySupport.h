// EVAL-LIVE-001 replay support helpers shared by the harness and its
// standalone logic self-test. Header-only so the self-test can exercise the
// exact serializer and fixture reader the harness uses, without constructing
// the processor (no stub processor is ever involved).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace replay
{

inline void jsonNumber (std::FILE* f, double v)
{
    if (std::isfinite (v))
        std::fprintf (f, "%.10g", v);
    else
        std::fprintf (f, "null");
}

inline void jsonU64 (std::FILE* f, std::uint64_t v)
{
    std::fprintf (f, "%llu", (unsigned long long) v);
}

inline void jsonI64 (std::FILE* f, std::int64_t v)
{
    std::fprintf (f, "%lld", (long long) v);
}

inline void jsonBool (std::FILE* f, bool v)
{
    std::fprintf (f, "%s", v ? "true" : "false");
}

//------------------------------------------------------------------------------
// Minimal, self-contained 16-bit PCM WAV reader. The synthetic fixture
// generator writes exactly that format; anything else fails closed.
//------------------------------------------------------------------------------
struct WavMono
{
    std::vector<float> samples;
    double rate = 0.0;
    bool ok = false;
    std::string error;
};

inline std::uint32_t rdU32 (const unsigned char* p)
{
    return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8)
         | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24);
}

inline std::uint16_t rdU16 (const unsigned char* p)
{
    return (std::uint16_t) ((std::uint16_t) p[0] | ((std::uint16_t) p[1] << 8));
}

inline bool loadWavMono (const std::string& path, WavMono& out)
{
    out = WavMono {};
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) { out.error = "cannot open"; return false; }
    unsigned char hdr[12];
    if (std::fread (hdr, 1, 12, f) != 12 || std::memcmp (hdr, "RIFF", 4) != 0
        || std::memcmp (hdr + 8, "WAVE", 4) != 0)
    {
        out.error = "not a RIFF/WAVE file"; std::fclose (f); return false;
    }
    std::uint16_t channels = 0, bits = 0, audioFormat = 0;
    std::uint32_t sampleRate = 0;
    std::vector<unsigned char> data;
    bool haveFmt = false, haveData = false;
    for (;;)
    {
        unsigned char ch[8];
        if (std::fread (ch, 1, 8, f) != 8) break;
        const std::uint32_t size = rdU32 (ch + 4);
        if (std::memcmp (ch, "fmt ", 4) == 0)
        {
            std::vector<unsigned char> fmt (size);
            if (std::fread (fmt.data(), 1, size, f) != size) break;
            if (size >= 16)
            {
                audioFormat = rdU16 (fmt.data());
                channels = rdU16 (fmt.data() + 2);
                sampleRate = rdU32 (fmt.data() + 4);
                bits = rdU16 (fmt.data() + 14);
            }
            haveFmt = true;
            if (size % 2) std::fgetc (f);
        }
        else if (std::memcmp (ch, "data", 4) == 0)
        {
            data.resize (size);
            if (size > 0 && std::fread (data.data(), 1, size, f) != size) break;
            haveData = true;
            if (size % 2) std::fgetc (f);
        }
        else
        {
            if (std::fseek (f, (long) (size + (size % 2)), SEEK_CUR) != 0) break;
        }
    }
    std::fclose (f);
    if (! haveFmt || ! haveData || audioFormat != 1 || bits != 16 || channels < 1 || sampleRate == 0)
    {
        out.error = "unsupported WAV (need 16-bit PCM, >=1 channel)"; return false;
    }
    const std::size_t frames = data.size() / (2u * channels);
    out.samples.resize (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        double acc = 0.0;
        for (std::uint16_t c = 0; c < channels; ++c)
        {
            const std::size_t off = (i * channels + c) * 2u;
            const std::int16_t v = (std::int16_t) rdU16 (data.data() + off);
            acc += (double) v / 32768.0;
        }
        out.samples[i] = (float) (acc / (double) channels);
    }
    out.rate = (double) sampleRate;
    out.ok = true;
    return true;
}

} // namespace replay
