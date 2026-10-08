// TraceRunner — per-block causal trace of an IRhythmTracker (TRACK-004).
//
// This is a DIAGNOSTIC. It drives a backend over one decoded WAV with a fixed
// block size and keeps EVERY block's raw jam::RhythmObservation next to the
// block's start/end device clock, then reduces the same blocks with the
// existing, unmodified rhythmeval::toSeries() so the trace and the scorer can
// never disagree about what was scored.
//
// It exists because BackendRunner::run() collapses the per-block stream to the
// ObservationSeries before returning: BPM confidence, onset strength, RMS and
// the silence flag of non-beat blocks are not recoverable from the series. The
// diagnosis in TRACK-004 needs those to explain WHY acquisition did or did not
// happen, without changing the scored runner.
//
// The block loop intentionally mirrors tools/rhythm-eval/BackendRunner.cpp
// (same clock arithmetic, same frame fields) so that
// `TraceRunner::runTrace(...).series` is byte-identical to
// `BackendRunner::run(...)`; tests/TraceReplayTests.cpp asserts that equality.
// No shared file is edited.

#pragma once

#include "BackendRunner.h"   // rhythmeval::BlockObservation, WavData, toSeries

#include <cstddef>
#include <vector>

namespace tracker_diag
{

struct TraceResult
{
    /** One entry per processed block, in emission order. */
    std::vector<rhythmeval::BlockObservation> blocks;
    /** The same blocks reduced by the unmodified EVAL-004 reducer. */
    rhythmeval::ObservationSeries series;
};

/** Drives `backend` over `audio` in `blockFrames`-frame blocks and records the
    full per-block trace plus the scored series. Deterministic; no wall clock. */
TraceResult runTrace (jam::IRhythmTracker& backend,
                      const rhythmeval::WavData& audio,
                      std::size_t blockFrames);

} // namespace tracker_diag
