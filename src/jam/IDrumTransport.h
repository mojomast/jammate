// Drum transport abstraction.
//
// DEVPLAN MOD-002. The Jam Director speaks only to this interface, so musical
// policy never leaks into DrumEngine and DrumEngine never learns about beat
// tracking. The concrete adapter (DrumTransportAdapter) wraps the existing
// DrumEngine and is owned by the DRUM-001 integration task.
//
// Real-time rules: methods invoked from the audio thread are marked. Those must
// obey SPEC.md section 7.1 in full. Methods marked [worker] run on the
// Jam-control thread and may allocate.

#pragma once

#include "RhythmTypes.h"

#include <string>

namespace jam
{

struct TransportPosition
{
    double bpm = 0.0;
    uint64_t samplePosition = 0;   // samples since transport start
    int bar = 0;                   // 1-based bar index, 0 when stopped
    int beat = 0;                  // 1-based beat within bar, 0 when stopped
    bool playing = false;
};

/** Musical change requested for a future bar boundary. The Jam Director queues
    these; the transport applies them on the boundary so that no musical change
    ever lands mid-bar. SPEC.md product principle 4. */
struct QueuedBarChange
{
    uint64_t generation = 0;       // clock generation the request was made under
    std::string grooveId;
    std::string fillId;
    float intensity01 = 0.5f;
    float swing01 = 0.0f;
    float humanizeVelocity = 0.25f;
    float humanizeTiming = 0.15f;
    float humanizeRoundRobin = 0.40f;
};

class IDrumTransport
{
public:
    virtual ~IDrumTransport() = default;

    /** [worker] Allocate everything needed for a session. */
    virtual void prepare (double sampleRate, int maximumBlockSize) = 0;

    /** [worker] */
    virtual void startTransport (double bpm, int beatsPerBar, int beatUnit) = 0;

    /** [worker] */
    virtual void stopTransport() = 0;

    /** Applies a *stable, already-smoothed* clock tempo. This is the only
        legitimate path by which tempo reaches the drums. It must not be called
        with raw detector output; see SPEC.md 10.3.
        [worker, applied at the next safe boundary by the adapter] */
    virtual void setClockTempo (double bpm) = 0;

    /** [worker] Re-phases the transport so the next beat lands on `targetTime`.
        Used for explicit Resync. */
    virtual void requestResyncNextBeat (uint64_t targetSampleTime) = 0;

    /** [worker] Re-phases the transport so the next downbeat lands on
        `targetSampleTime`. */
    virtual void requestResyncNextBar (uint64_t targetSampleTime) = 0;

    /** [worker] Queue a groove and/or fill to begin at the next bar boundary. */
    virtual void queueBarChange (const QueuedBarChange& change) = 0;

    /** [worker] Immediate fill at the next bar boundary, bypassing director
        policy. Backs the explicit user Fill button. */
    virtual void requestFillAtNextBar (const std::string& fillId) = 0;

    /** [worker] Stop cleanly on the next bar boundary. */
    virtual void requestStopAtNextBar() = 0;

    /** Latest transport position. Read by diagnostics off the audio thread. */
    virtual TransportPosition position() const = 0;
};

} // namespace jam