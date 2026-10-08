// ClickTrain implementation. See ClickTrain.h.

#include "ClickTrain.h"

#include <cmath>
#include <cstdint>

namespace tempo_variant
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

} // namespace

std::vector<double> clickBeatTimes (const ClickSpec& spec)
{
    std::vector<double> beats;
    const double seconds = spec.seconds > 0.0 ? spec.seconds : 1.0;
    const double basePeriod = 60.0 / spec.bpm;

    double t = spec.firstBeat;
    while (t < seconds)
    {
        beats.push_back (t);
        const double period =
            (spec.stepToBpm > 0.0 && t >= spec.stepSeconds)
                ? 60.0 / spec.stepToBpm : basePeriod;
        t += period;
    }
    return beats;
}

rhythmeval::WavData makeClickTrain (const ClickSpec& spec)
{
    rhythmeval::WavData out;
    out.sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    const double seconds = spec.seconds > 0.0 ? spec.seconds : 1.0;
    const std::size_t frames =
        static_cast<std::size_t> (std::llround (seconds * out.sampleRate));
    out.samples.resize (frames);

    const std::vector<double> beats = clickBeatTimes (spec);
    std::size_t nextBeat = 0;
    double onset = beats.empty() ? 1.0e9 : beats[0];

    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double> (i) / out.sampleRate;
        while (nextBeat + 1 < beats.size() && beats[nextBeat + 1] <= t)
            onset = beats[++nextBeat];

        double v = spec.noiseFloor
                   * ((static_cast<double> (hash32 (i)) / 4294967295.0) * 2.0 - 1.0);

        const double dt = t - onset;
        if (dt >= 0.0 && dt < 0.006)
        {
            const double env = std::exp (-dt / 0.0015);
            const double noise =
                (static_cast<double> (hash32 (i * 0x9E3779B97F4A7C15ULL + 0x1234u) & 0xffffu)
                 / 32768.0) - 1.0;
            v += 0.8 * env * noise;
        }
        out.samples[i] = static_cast<float> (v);
    }

    out.frames = frames;
    return out;
}

} // namespace tempo_variant
