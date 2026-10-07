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

#include <cstdint>

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

/** Identity of an entry in the compiled groove/fill library.
 *
 * WHY THIS IS AN INDEX AND NOT A STRING ID. SPEC.md section 13.2 illustrates a
 * style descriptor with string groove ids such as "rock.basic.01", and this
 * header originally carried `std::string grooveId` to match. That was wrong, and
 * the library was measured to prove it:
 *
 *   - `src/DrumLibrary.cpp` contains 570 entries across 16 genres and **no
 *     string identifier of any kind**; entries are anonymous values in a
 *     `static const std::vector<Groove>`.
 *   - `drum::Groove` identifies an entry by `genre` + `name` only.
 *   - **`(genre, name)` is not unique: 16 pairs collide**, and every collision
 *     is a genuinely different pattern, not a restatement. `("ROCK","Half-time
 *     16")` appears twice with different hat/kick content; `("FUNK","Linear
 *     funk")` appears twice and is a *groove* at index 63 and a *fill* at index
 *     187.
 *
 * A slug derived from genre + name would therefore resolve to the wrong pattern
 * for those 16 entries, and for `FUNK/Linear funk` to the wrong *kind* as well.
 * A musically wrong result that looks correct is the worst failure mode
 * available here, so the derived-ID approach is rejected rather than patched.
 *
 * The positional index is unique by construction, and it is already what the
 * shipping UI uses: `src/DrumOverlay.cpp` identifies library rows by the drag id
 * `"f:" + index` and resolves them through `library()[i]`. This interface
 * therefore adopts the key the product already ships with, rather than
 * inventing a second one.
 *
 * Persistence caveat (PERSIST-001): an index is only stable while the compiled
 * library is unchanged. It is stable for the frozen fork this project builds on,
 * and the library is treated as read-only — styles overlay it, nothing rewrites
 * it. Anything persisted must additionally record genre, name, fill flag and a
 * hash of the pattern spec so a load against a changed library can detect the
 * mismatch instead of silently playing a different groove.
 */
using LibraryIndex = int32_t;

/** "No entry" sentinel. Distinct from every real index, which are >= 0. */
inline constexpr LibraryIndex kNoLibraryEntry = -1;

/** Musical change requested for a future bar boundary. The Jam Director queues
    these; the transport applies them on the boundary so that no musical change
    ever lands mid-bar. SPEC.md product principle 4. */
struct QueuedBarChange
{
    uint64_t generation = 0;       // clock generation the request was made under
    LibraryIndex groove = kNoLibraryEntry;
    LibraryIndex fill = kNoLibraryEntry;
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
    virtual void requestFillAtNextBar (LibraryIndex fill) = 0;

    /** [worker] Stop cleanly on the next bar boundary. */
    virtual void requestStopAtNextBar() = 0;

    /** Latest transport position. Read by diagnostics off the audio thread. */
    virtual TransportPosition position() const = 0;
};

} // namespace jam