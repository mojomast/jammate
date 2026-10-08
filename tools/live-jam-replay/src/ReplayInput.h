// EVAL-LIVE-001 shared input timeline (JUCE-free).
//
// Corrects the observed fixture-timeline bug: one source position per audio
// frame, independent of channel count; sourcePosition = deviceFrame *
// wavSampleRate / deviceSampleRate; coherent mono replication; absolute device
// frame clock across cold/warm blocks. Shared by the actual-processor harness and
// the support self-test so the tested arithmetic is the executed arithmetic.
//
// This header is deliberately JUCE-free: it writes raw channel pointers, so the
// support self-test links without JUCE and the harness passes
// AudioBuffer::getArrayOfWritePointers().
#pragma once

#include "ReplaySupport.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace replay
{

enum class InputKind { clean, noise, silence };

inline const char* inputKindName (InputKind k) noexcept
{
    switch (k)
    {
        case InputKind::clean:   return "clean";
        case InputKind::noise:   return "noise";
        case InputKind::silence: return "silence";
    }
    return "unknown";
}

// Bounded deterministic linear interpolation with wrap. A fractional position
// beyond the last sample interpolates toward the first (loop), which preserves
// the source duration and onset period in device time.
inline float sampleLinearWrapped (const std::vector<float>& s, double pos) noexcept
{
    if (s.empty()) return 0.0f;
    const double total = (double) s.size();
    double p = std::fmod (pos, total);
    if (p < 0.0) p += total;
    const std::size_t i0 = (std::size_t) std::floor (p);
    const std::size_t i1 = (i0 + 1) % s.size();
    const double frac = p - (double) i0;
    return (float) ((1.0 - frac) * (double) s[i0] + frac * (double) s[i1]);
}

// True deterministic 120 BPM strum in DEVICE time: 0.5 s period, exponential
// decay, harmonic tone. No per-block phase reset and no per-channel randomness.
inline double builtinCleanAt (double t) noexcept
{
    constexpr double kTwoPi = 6.283185307179586;
    const double period = 0.5;               // 120 BPM
    double phase = std::fmod (t, period);
    if (phase < 0.0) phase += period;
    const double env = std::exp (-8.0 * phase / period);
    const double tone = std::sin (kTwoPi * 220.0 * t);
    return 0.05 + 0.35 * env * tone;
}

struct InputGen
{
    InputKind kind = InputKind::clean;
    const WavMono* wav = nullptr;      // when set and valid, drives the timeline
    double deviceRate = 48000.0;       // set by the cell every prepare
    std::uint64_t deviceFrame = 0;     // absolute audio frames, begins at 0
    std::uint32_t lcg = 1u;

    void reset (std::uint32_t seed) noexcept
    {
        lcg = seed ? seed : 1u;
        deviceFrame = 0;
    }

    bool validRate() const noexcept
    {
        return std::isfinite (deviceRate) && deviceRate > 0.0;
    }

    // Writes `n` device frames into `numChannels` channel pointers. The source
    // position advances by n ONCE, after all channels are filled.
    void fill (float* const* channels, int numChannels, int n) noexcept
    {
        if (channels == nullptr || numChannels <= 0 || n <= 0) return;
        if (! validRate())
        {
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < n; ++i) channels[ch][i] = 0.0f;
            return;
        }

        if (wav != nullptr && wav->ok && ! wav->samples.empty())
        {
            const double srcRate = wav->rate;
            for (int i = 0; i < n; ++i)
            {
                const double pos = (double) (deviceFrame + (std::uint64_t) i) * srcRate / deviceRate;
                const float v = sampleLinearWrapped (wav->samples, pos);
                for (int ch = 0; ch < numChannels; ++ch) channels[ch][i] = v;
            }
            deviceFrame += (std::uint64_t) n;
            return;
        }

        if (kind == InputKind::silence)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < n; ++i) channels[ch][i] = 0.0f;
            deviceFrame += (std::uint64_t) n;
            return;
        }

        if (kind == InputKind::noise)
        {
            // LCG advanced once per FRAME; the value is replicated across
            // channels (declared uniform nonrhythmic control, no tempo claim).
            for (int i = 0; i < n; ++i)
            {
                lcg = lcg * 1664525u + 1013904223u;
                const float u = (float) ((lcg >> 8) & 0xFFFFu) / 65535.0f;
                const float v = u * 0.5f - 0.25f;
                for (int ch = 0; ch < numChannels; ++ch) channels[ch][i] = v;
            }
            deviceFrame += (std::uint64_t) n;
            return;
        }

        // Built-in clean: absolute device time, shared across channels.
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (deviceFrame + (std::uint64_t) i) / deviceRate;
            const float v = (float) builtinCleanAt (t);
            for (int ch = 0; ch < numChannels; ++ch) channels[ch][i] = v;
        }
        deviceFrame += (std::uint64_t) n;
    }
};

} // namespace replay
