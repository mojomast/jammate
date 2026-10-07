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

ObservationSeries toSeries (const std::vector<BlockObservation>& blocks,
                            double audioDurationSeconds,
                            double sampleRate,
                            bool stampBeatsAtBlockStart)
{
    ObservationSeries series;
    series.audioDurationSeconds = audioDurationSeconds;
    series.sampleRate = sampleRate;
    if (sampleRate <= 0.0)
        return series;

    series.beatTimesSeconds.reserve (blocks.size() / 4 + 1);
    series.beatAvailabilitySeconds.reserve (blocks.size() / 4 + 1);
    series.tempoSamples.reserve (blocks.size());

    double reportedLatencySum = 0.0;
    std::size_t reportedLatencyCount = 0;

    for (const BlockObservation& block : blocks)
    {
        const jam::RhythmObservation& obs = block.observation;

        // The runner owns the frame clock, so seconds are always computed on the
        // rate the frames were fed at. A backend that declares a different
        // sourceSampleRate is counted; its declared clock cannot be trusted.
        if (obs.sourceSampleRate > 0.0
            && std::fabs (obs.sourceSampleRate - sampleRate) > 0.5)
            ++series.diagnostics.rateMismatchBlocks;

        const double blockStart = block.blockStartSeconds;
        const double blockEnd = block.blockEndSeconds;
        double eventSeconds = blockStart;

        if (obs.beatEvent)
        {
            ++series.diagnostics.beatEvents;
            const double reported =
                static_cast<double> (obs.inputSampleTime) / sampleRate;

            if (stampBeatsAtBlockStart)
            {
                // Deliberate reproduction of the pre-EVAL-004 defect: discard the
                // backend's device timestamp. Never used for gate evidence.
                eventSeconds = blockStart;
                ++series.diagnostics.beatsAtBlockStart;
            }
            else if (reported > blockEnd + 1.0e-6)
            {
                // Non-causal: the backend claims the beat happened after the
                // audio it was given. Reject and fall back to the block start.
                ++series.diagnostics.beatsRejectedNonCausal;
            }
            else
            {
                eventSeconds = reported;
                if (obs.inputSampleTime == block.blockStartSample)
                    ++series.diagnostics.beatsAtBlockStart;
                else
                    ++series.diagnostics.beatsReportedByBackend;

                const double latency = blockEnd - reported;
                if (latency >= 0.0)
                {
                    reportedLatencySum += latency;
                    ++reportedLatencyCount;
                    if (latency > series.diagnostics.maxReportedAvailabilityLatencySeconds)
                        series.diagnostics.maxReportedAvailabilityLatencySeconds = latency;
                }
            }

            series.beatTimesSeconds.push_back (eventSeconds);
            series.beatAvailabilitySeconds.push_back (blockEnd);
        }

        TempoSample s;
        s.timeSeconds = blockStart;
        s.availabilitySeconds = blockEnd;
        s.hasAvailability = true;
        s.bpm = static_cast<double> (obs.bpmCandidate);
        s.phaseValid = obs.phaseValid;
        s.silence = obs.silence;
        series.tempoSamples.push_back (s);
    }

    if (reportedLatencyCount > 0)
        series.diagnostics.meanReportedAvailabilityLatencySeconds =
            reportedLatencySum
            / static_cast<double> (reportedLatencyCount);

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

    std::vector<BlockObservation> blocks;
    blocks.reserve (audio.frames / blockFrames_ + 2);

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

        // The runner DOES NOT overwrite obs.inputSampleTime: for a beat event it
        // is the backend's reported device time (aubio's sub-hop tactus position,
        // BTrack's analysis-hop start). The runner only records the block timing
        // beside it; toSeries validates causality and applies the fallback.
        BlockObservation block;
        block.observation = obs;
        block.blockStartSample = frame.sampleTime;
        block.blockStartSeconds =
            static_cast<double> (frame.sampleTime) / audio.sampleRate;
        block.blockEndSeconds =
            static_cast<double> (frame.sampleTime + count) / audio.sampleRate;
        blocks.push_back (block);
    }
    const std::clock_t cpuEnd = std::clock();

    series = toSeries (blocks, series.audioDurationSeconds, audio.sampleRate,
                       legacyBlockStampedBeats_);

    series.diagnostics.blocks = blocks.size();
    if (audio.frames % blockFrames_ != 0)
        series.diagnostics.partialFinalBlocks = 1;

    series.cpuSeconds =
        static_cast<double> (cpuEnd - cpuStart) / static_cast<double> (CLOCKS_PER_SEC);
    return series;
}

} // namespace rhythmeval
