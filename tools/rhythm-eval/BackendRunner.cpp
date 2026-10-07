// BackendRunner implementation. See BackendRunner.h.

#include "BackendRunner.h"

#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rhythmeval
{

namespace
{

std::uint16_t readLE16 (const unsigned char* p)
{
    return static_cast<std::uint16_t> (p[0] | (static_cast<unsigned>(p[1]) << 8));
}

std::uint32_t readLE32 (const unsigned char* p)
{
    return static_cast<std::uint32_t> (p[0])
           | (static_cast<std::uint32_t> (p[1]) << 8)
           | (static_cast<std::uint32_t> (p[2]) << 16)
           | (static_cast<std::uint32_t> (p[3]) << 24);
}

} // namespace

WavData readWavMono16 (const std::string& path)
{
    std::ifstream in (path.c_str(), std::ios::binary);
    if (! in.good())
        throw std::runtime_error ("could not open WAV file: " + path);

    in.seekg (0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg (0, std::ios::beg);
    if (size < 44)
        throw std::runtime_error ("WAV file is too short: " + path);

    std::vector<unsigned char> bytes (static_cast<std::size_t> (size));
    in.read (reinterpret_cast<char*> (bytes.data()),
             static_cast<std::streamsize> (size));
    if (in.gcount() != static_cast<std::streamsize> (size))
        throw std::runtime_error ("could not read WAV file: " + path);

    auto tag = [&bytes] (std::size_t offset) -> std::string
    {
        if (offset + 4 > bytes.size())
            return std::string();
        return std::string (reinterpret_cast<const char*> (&bytes[offset]), 4);
    };

    if (tag (0) != "RIFF" || tag (8) != "WAVE")
        throw std::runtime_error ("not a RIFF/WAVE file: " + path);

    WavData out;
    bool haveFmt = false;
    std::size_t offset = 12;
    while (offset + 8 <= bytes.size())
    {
        const std::string id = tag (offset);
        const std::uint32_t chunkSize = readLE32 (&bytes[offset + 4]);
        const std::size_t body = offset + 8;
        if (body + chunkSize > bytes.size())
            throw std::runtime_error ("truncated WAV chunk \"" + id + "\" in " + path);

        if (id == "fmt ")
        {
            if (chunkSize < 16)
                throw std::runtime_error ("WAV fmt chunk is too small: " + path);
            const std::uint16_t audioFormat = readLE16 (&bytes[body]);
            const std::uint16_t channels = readLE16 (&bytes[body + 2]);
            const std::uint32_t sampleRate = readLE32 (&bytes[body + 4]);
            const std::uint16_t bitsPerSample = readLE16 (&bytes[body + 14]);
            if (audioFormat != 1)
                throw std::runtime_error (
                    "WAV must be uncompressed PCM (format 1): " + path);
            if (channels != 1)
                throw std::runtime_error ("WAV must be mono: " + path);
            if (bitsPerSample != 16)
                throw std::runtime_error ("WAV must be 16-bit PCM: " + path);
            out.sampleRate = static_cast<double> (sampleRate);
            haveFmt = true;
        }
        else if (id == "data")
        {
            if (! haveFmt)
                throw std::runtime_error ("WAV data chunk precedes fmt chunk: " + path);
            const std::size_t frames = chunkSize / 2u;
            out.samples.resize (frames);
            for (std::size_t i = 0; i < frames; ++i)
            {
                const std::int16_t s = static_cast<std::int16_t> (
                    readLE16 (&bytes[body + i * 2]));
                out.samples[i] = static_cast<float> (s) / 32768.0f;
            }
            out.frames = frames;
        }

        // Chunks are word-aligned: an odd-size chunk is followed by a pad byte.
        offset = body + chunkSize + (chunkSize % 2u);
    }

    if (! haveFmt || out.frames == 0)
        throw std::runtime_error ("WAV has no fmt/data payload: " + path);
    if (! (out.sampleRate > 0.0))
        throw std::runtime_error ("WAV declares a non-positive sample rate: " + path);

    return out;
}

ObservationSeries toSeries (const std::vector<jam::RhythmObservation>& observations,
                            double audioDurationSeconds,
                            double sampleRate)
{
    ObservationSeries series;
    series.audioDurationSeconds = audioDurationSeconds;
    series.sampleRate = sampleRate;
    if (sampleRate <= 0.0)
        return series;

    series.beatTimesSeconds.reserve (observations.size() / 4 + 1);
    series.tempoSamples.reserve (observations.size());

    for (const jam::RhythmObservation& obs : observations)
    {
        const double rate = obs.sourceSampleRate > 0.0
                                ? obs.sourceSampleRate : sampleRate;
        const double time = static_cast<double> (obs.inputSampleTime) / rate;

        if (obs.beatEvent)
            series.beatTimesSeconds.push_back (time);

        TempoSample s;
        s.timeSeconds = time;
        s.bpm = static_cast<double> (obs.bpmCandidate);
        s.phaseValid = obs.phaseValid;
        s.silence = obs.silence;
        series.tempoSamples.push_back (s);
    }

    return series;
}

ObservationSeries BackendRunner::run (jam::IRhythmTracker& backend, const WavData& audio)
{
    ObservationSeries series;
    series.audioDurationSeconds =
        audio.sampleRate > 0.0
            ? static_cast<double> (audio.frames) / audio.sampleRate : 0.0;
    series.sampleRate = audio.sampleRate;

    if (audio.frames == 0 || ! (audio.sampleRate > 0.0))
        return series;

    backend.reset (audio.sampleRate);

    std::vector<jam::RhythmObservation> observations;
    observations.reserve (audio.frames / blockFrames_ + 2);

    const std::clock_t cpuStart = std::clock();
    for (std::size_t first = 0; first < audio.frames; first += blockFrames_)
    {
        const std::size_t remaining = audio.frames - first;
        const std::size_t count = remaining < blockFrames_ ? remaining : blockFrames_;

        jam::AnalysisFrame frame;
        frame.sampleTime = static_cast<std::uint64_t> (first);
        frame.sourceSampleRate = audio.sampleRate;
        frame.numSamples = static_cast<std::uint32_t> (count);
        for (std::size_t i = 0; i < count; ++i)
            frame.samples[i] = audio.samples[first + i];

        jam::RhythmObservation obs = backend.process (frame);
        // The runner owns the timeline: stamp the block time even if the
        // backend left it at zero, so predicted-beat times are well defined.
        obs.inputSampleTime = frame.sampleTime;
        obs.sourceSampleRate = frame.sourceSampleRate;
        observations.push_back (obs);
    }
    const std::clock_t cpuEnd = std::clock();

    series = toSeries (observations, series.audioDurationSeconds, audio.sampleRate);
    series.cpuSeconds =
        static_cast<double> (cpuEnd - cpuStart) / static_cast<double> (CLOCKS_PER_SEC);
    return series;
}

} // namespace rhythmeval
