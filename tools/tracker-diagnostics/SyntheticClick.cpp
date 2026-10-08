// SyntheticClick implementation. See SyntheticClick.h.

#include "SyntheticClick.h"

#include <cmath>
#include <cstdint>

namespace tracker_diag
{

namespace
{

std::uint32_t hash32 (std::uint64_t i) noexcept
{
    std::uint64_t x = i + 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return static_cast<std::uint32_t> (x >> 32);
}

float clickSample (std::uint64_t i, double rate, double bpm,
                   double firstBeat, double noiseFloor) noexcept
{
    const double t = static_cast<double> (i) / rate;
    const double period = 60.0 / bpm;
    const double k = std::floor ((t - firstBeat) / period);
    const double onset = firstBeat + k * period;
    const double dt = t - onset;

    double v = noiseFloor
               * ((static_cast<double> (hash32 (i)) / 4294967295.0) * 2.0 - 1.0);

    if (dt >= 0.0 && dt < 0.006)
    {
        const double env = std::exp (-dt / 0.0015);
        const double noise =
            (static_cast<double> (hash32 (i * 0x9E3779B97F4A7C15ULL + 0x1234u) & 0xffffu)
             / 32768.0) - 1.0;
        v += 0.8 * env * noise;
    }
    return static_cast<float> (v);
}

} // namespace

rhythmeval::WavData makeClickTrain (const ClickSpec& spec)
{
    rhythmeval::WavData out;
    out.sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    const double seconds = spec.seconds > 0.0 ? spec.seconds : 1.0;
    const std::size_t frames =
        static_cast<std::size_t> (std::llround (seconds * out.sampleRate));
    out.samples.resize (frames);
    for (std::size_t i = 0; i < frames; ++i)
        out.samples[i] = clickSample (static_cast<std::uint64_t> (i), out.sampleRate,
                                      spec.bpm, spec.firstBeat, spec.noiseFloor);
    out.frames = frames;
    return out;
}

} // namespace tracker_diag
