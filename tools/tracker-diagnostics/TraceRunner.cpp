// TraceRunner implementation. See TraceRunner.h.

#include "TraceRunner.h"

#include <algorithm>
#include <cstdint>

namespace tracker_diag
{

TraceResult runTrace (jam::IRhythmTracker& backend,
                      const rhythmeval::WavData& audio,
                      std::size_t blockFrames)
{
    TraceResult out;
    if (blockFrames == 0 || blockFrames > jam::kMaxAnalysisBlock)
        blockFrames = jam::kMaxAnalysisBlock;

    out.series.audioDurationSeconds =
        audio.sampleRate > 0.0
            ? static_cast<double> (audio.frames) / audio.sampleRate : 0.0;
    out.series.sampleRate = audio.sampleRate;

    if (audio.frames == 0 || ! (audio.sampleRate > 0.0))
        return out;

    backend.reset (audio.sampleRate);

    out.blocks.reserve (audio.frames / blockFrames + 2);
    for (std::size_t first = 0; first < audio.frames; first += blockFrames)
    {
        const std::size_t remaining = audio.frames - first;
        const std::size_t count = remaining < blockFrames ? remaining : blockFrames;

        jam::AnalysisFrame frame;
        frame.sampleTime = static_cast<std::uint64_t> (first);
        frame.sourceSampleRate = audio.sampleRate;
        frame.numSamples = static_cast<std::uint32_t> (count);
        for (std::size_t i = 0; i < count; ++i)
            frame.samples[i] = audio.samples[first + i];

        jam::RhythmObservation obs = backend.process (frame);

        rhythmeval::BlockObservation block;
        block.observation = obs;
        block.blockStartSample = frame.sampleTime;
        block.blockStartSeconds =
            static_cast<double> (frame.sampleTime) / audio.sampleRate;
        block.blockEndSeconds =
            static_cast<double> (frame.sampleTime + count) / audio.sampleRate;
        out.blocks.push_back (block);
    }

    out.series = rhythmeval::toSeries (out.blocks, out.series.audioDurationSeconds,
                                       audio.sampleRate, /*stampBeatsAtBlockStart=*/false);
    out.series.diagnostics.blocks = out.blocks.size();
    if (audio.frames % blockFrames != 0)
        out.series.diagnostics.partialFinalBlocks = 1;
    return out;
}

} // namespace tracker_diag
