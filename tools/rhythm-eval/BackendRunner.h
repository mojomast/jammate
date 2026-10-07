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

/** One backend observation plus the runner's block timing. Keeping the two
    clocks separate is the whole point of EVAL-004: the block end is the time at
    which the evidence became causally available, and the backend's reported
    device time is when the event happened. */
struct BlockObservation
{
    jam::RhythmObservation observation;
    /** Device time of the block's first sample. */
    double blockStartSeconds = 0.0;
    /** Device time at which `process()` returned, i.e. the block end. This is
        the causal availability of `observation` in a real-time consumer. */
    double blockEndSeconds = 0.0;
    /** Device sample index of the block start, to identify a backend that
        reports a beat exactly at the block boundary. */
    std::uint64_t blockStartSample = 0;
};

/** Reduces backend evidence to the pure metric input (EVAL-004).

    Event times: for a block that set `beatEvent`, the backend's
    `inputSampleTime` is preserved as the reported device time when it is
    causal (at or before the block end). A non-causal reported time (after the
    block end) is rejected and the event falls back to the block start; zeros are
    NOT treated as "unset", because a beat at device sample 0 is a valid event.
    The declared `sourceSampleRate` is checked against the rate the frames were
    fed at; a mismatch is counted (and the fed clock is still used) because the
    backend's declared clock cannot then be trusted.

    Availability times: every beat and every tempo sample carries the block end
    as its causal availability. `toSeries` never invents availability; when there
    is no block timing, the array is left empty.

    @param blocks  one entry per processed block
    @param stampBeatsAtBlockStart  reproduce the pre-EVAL-004 defect (overwrite
                   every event time with the block start) so its effect can be
                   measured. Diagnostic only; never used for a gate. */
ObservationSeries toSeries (const std::vector<BlockObservation>& blocks,
                            double audioDurationSeconds,
                            double sampleRate,
                            bool stampBeatsAtBlockStart = false);

/** Drives a backend over decoded audio with a fixed block size. Deterministic:
    fixed block size, no wall-clock in the scored outputs (cpuSeconds is
    resource-only). */
class BackendRunner
{
public:
    explicit BackendRunner (std::size_t blockFrames = 128,
                            bool legacyBlockStampedBeats = false)
        : blockFrames_ (blockFrames != 0 && blockFrames <= jam::kMaxAnalysisBlock
                            ? blockFrames
                            : jam::kMaxAnalysisBlock),
          legacyBlockStampedBeats_ (legacyBlockStampedBeats) {}

    std::size_t blockFrames() const { return blockFrames_; }
    bool legacyBlockStampedBeats() const { return legacyBlockStampedBeats_; }

    ObservationSeries run (jam::IRhythmTracker& backend, const WavData& audio);

private:
    std::size_t blockFrames_;
    bool legacyBlockStampedBeats_;
};

} // namespace rhythmeval
