// Drives any IRhythmTracker over one fixture WAV and reduces its output to the
// pure ObservationSeries the metrics consume (EVAL-002).
//
// The runner is deliberately backend-agnostic: it knows only
// jam::IRhythmTracker and jam::AnalysisFrame. That is what lets the same
// scoring path evaluate a synthetic backend in a unit test and the real BTrack
// backend in the CLI. The scored path contains no wall-clock or random input;
// the only time measured here is CPU time, which is reported as a resource
// metric and never feeds a score.

#pragma once

#include "Metrics.h"

#include "jam/IRhythmTracker.h"
#include "jam/RhythmTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rhythmeval
{

/** Decoded mono audio, nominal range [-1, 1]. */
struct WavData
{
    std::vector<float> samples;
    double sampleRate = 48000.0;
    std::size_t frames = 0;
};

/** Reads a 16-bit PCM mono RIFF/WAVE file. Throws std::runtime_error with a
    precise message for anything else (wrong format, truncated, stereo, ...). */
WavData readWavMono16 (const std::string& path);

/** Reduces backend evidence to the pure metric input. Beat-event times are the
    block-start times of the observations that set `beatEvent`; the runner owns
    the timeline, so it stamps each observation with the block's sample time so
    a backend that forgets to is still scored on a well-defined clock. */
ObservationSeries toSeries (const std::vector<jam::RhythmObservation>& observations,
                            double audioDurationSeconds,
                            double sampleRate);

/** Drives a backend over decoded audio with a fixed block size. Deterministic:
    fixed block size, no wall-clock in the scored outputs (cpuSeconds is
    resource-only). */
class BackendRunner
{
public:
    explicit BackendRunner (std::size_t blockFrames = 128)
        : blockFrames_ (blockFrames != 0 && blockFrames <= jam::kMaxAnalysisBlock
                            ? blockFrames
                            : jam::kMaxAnalysisBlock) {}

    std::size_t blockFrames() const { return blockFrames_; }

    ObservationSeries run (jam::IRhythmTracker& backend, const WavData& audio);

private:
    std::size_t blockFrames_;
};

} // namespace rhythmeval
