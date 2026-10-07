// Rhythm tracker backend interface.
//
// Frozen by SPEC.md section 9.2. A backend produces RhythmObservation evidence.
// It has no authority over drum tempo; that is the Musical Clock's job.
//
// Real-time rules: `process` runs on the rhythm-analysis worker thread, never on
// the audio callback. It may still allocate, because allocation on the analysis
// thread cannot glitch monitoring audio. Backends must nevertheless avoid
// unbounded blocking, because the analysis thread is the clock's only source
// of evidence.

#pragma once

#include "RhythmTypes.h"

namespace jam
{

class IRhythmTracker
{
public:
    virtual ~IRhythmTracker() = default;

    /** Prepare for a new capture session. Must be deterministic: after reset(),
        the same input sequence must produce the same observation sequence.
        @param sampleRate  rate the backend will be fed at, after any internal
                          resampling. The caller (RhythmAnalyzer) guarantees the
                          backend receives audio at this rate. */
    virtual void reset (double sampleRate) = 0;

    /** Consume one block of mono audio and emit evidence. Never blocks waiting
        for more audio; returns an observation for the block it was given. */
    virtual RhythmObservation process (const AnalysisFrame& frame) = 0;

    /** Stable identifier used by the evaluation harness and diagnostics. */
    virtual const char* id() const noexcept = 0;
};

} // namespace jam