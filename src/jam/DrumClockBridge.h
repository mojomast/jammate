// Drum clock bridge — the worker-side seam that turns the band's stable Musical
// Clock belief into bounded, coherent POD commands for the audio-thread drum
// engine (DEVPLAN DRUM-001 / INT-DRUM-001).
//
// WHAT THIS CLASS IS
//   The missing link between `jam::MusicalClock` (worker/control thread) and the
//   *actual* `DrumEngine` (audio thread). It consumes `ClockSnapshot`s, derives
//   the musical grid from an EXPLICIT absolute audio sample time, and publishes
//   fixed-boundary events (join, tempo, stop, resync, clear) through a
//   fixed-capacity SPSC command queue. The engine consumes that queue on the
//   audio thread and renders the pattern at the exact sample the command names.
//
// RELATIONSHIP TO DrumTransportAdapter (MOD-002)
//   The boundary semantics are deliberately the same as the adapter:
//     * position is a pure re-anchored function of an integer sample clock;
//     * a musical change is quantised to a boundary and applied exactly once;
//     * stale requests are dropped and counted; the command queue drops the
//       INCOMING command on overflow and counts it (SPEC.md 8.3).
//   The difference is the time source, and it is the whole point of this task:
//   `DrumTransportAdapter::advance(numSamples)` accumulates a free-running block
//   counter. This bridge has no such counter. `setClockSample(explicitSample)`
//   is handed the absolute sample position from the audio device timeline and
//   re-anchors from it, so the worker cannot drift away from what is rendered,
//   and a discontinuity is detected instead of silently absorbed.
//
// TEMPO AUTHORITY
//   Exactly one path accepts tempo: `applySnapshot()`. The clock is the sole
//   tempo authority; the bridge never smooths, predicts or re-derives it. A new
//   tempo is STAGED and only takes effect at the next bar boundary, so the grid
//   never changes mid-bar. This is deliberately stricter than the adapter,
//   whose tempo path is block-quantised (MOD-002 judgement call 5/6).
//
// THREADING
//   Worker/control thread: prepare, setClockSample, applySnapshot, request*.
//   Audio thread: popCommand() only. Publication and consumption are bounded,
//   allocate nothing and never block. The bridge owns the queue; the engine is
//   handed a pointer to it and is the single consumer.
//
// HEADER HYGIENE (SPEC.md 7.1 / 8.1): no JUCE, no STL containers, no
// std::string. Only the frozen jam headers and the header-only RT signal
// primitives live here, so the bridge stays buildable in the platform-neutral
// jam-core.

#pragma once

#include <cstddef>
#include <cstdint>

#include "IDrumTransport.h"
#include "RhythmTypes.h"
#include "rt/RtSignal.h"

namespace jam
{

/** The verb of one injected drum-clock command. POD, trivially copyable. */
enum class DrumClockCommandType : std::uint8_t
{
    None = 0,
    JoinAtBar,    // begin the prepared groove exactly at `sampleTime`
    SetTempo,     // change the grid tempo exactly at `sampleTime` (bar boundary)
    StopAtBar,    // stop the injected transport exactly at `sampleTime`
    ResyncBeat,   // a beat onset lands exactly on `sampleTime`
    ResyncBar,    // a downbeat lands exactly on `sampleTime`
    Clear,        // discontinuity / lifecycle: stop now and forget the grid
    // DRUM-ADAPT-002: adaptive musical change (groove and/or one-bar fill plus
    // intensity/swing/humanization) applied exactly at a bar boundary. Appended
    // after Clear deliberately so the numeric values of every pre-existing verb
    // are unchanged for any caller that ever compared raw bytes.
    BarChange
};

inline const char* toString (DrumClockCommandType type) noexcept
{
    switch (type)
    {
        case DrumClockCommandType::None:       return "None";
        case DrumClockCommandType::JoinAtBar:  return "JoinAtBar";
        case DrumClockCommandType::SetTempo:   return "SetTempo";
        case DrumClockCommandType::StopAtBar:  return "StopAtBar";
        case DrumClockCommandType::ResyncBeat: return "ResyncBeat";
        case DrumClockCommandType::ResyncBar:  return "ResyncBar";
        case DrumClockCommandType::Clear:      return "Clear";
        case DrumClockCommandType::BarChange:  return "BarChange";
    }
    return "Unknown";
}

/** Which musical fields of a `BarChange` command are authoritative.
 *
 *  The payload is deliberately granular: a convenience fill request must not
 *  silently reset the director's intensity/swing/humanization, and a groove-only
 *  change must not cancel a fill that the director did not mention. Each bit is
 *  an "authoritative" flag, not a "changed" flag; an unset bit means the engine
 *  keeps its current value. Smooth built-in defaults therefore survive across
 *  unrelated changes. */
enum class DrumChangeField : std::uint8_t
{
    None   = 0,
    Groove = 1u << 0,  // `groove` is authoritative (kNoLibraryEntry = keep)
    Fill   = 1u << 1,  // `fill` is authoritative (kNoLibraryEntry = no fill)
    Params = 1u << 2   // intensity01/swing01/humanize* are authoritative
};

inline std::uint8_t operator| (DrumChangeField a, DrumChangeField b) noexcept
{
    return static_cast<std::uint8_t> (
        static_cast<std::uint8_t> (a) | static_cast<std::uint8_t> (b));
}

inline std::uint8_t operator| (std::uint8_t a, DrumChangeField b) noexcept
{
    return static_cast<std::uint8_t> (a | static_cast<std::uint8_t> (b));
}

inline std::uint8_t operator| (DrumChangeField a, std::uint8_t b) noexcept
{
    return static_cast<std::uint8_t> (static_cast<std::uint8_t> (a) | b);
}

/** Low bits of `value` set in `fields`. */
inline bool hasField (std::uint8_t fields, DrumChangeField field) noexcept
{
    return (fields & static_cast<std::uint8_t> (field)) != 0;
}

/** One bounded, coherent clock command.
 *
 * `sampleTime` is an ABSOLUTE audio sample, in the same timeline the worker
 * advances with setClockSample(). The engine maps it to an exact within-block
 * offset, so a join/tempo/stop that falls mid-block is not rounded to the block
 * start. Every field is a plain value; the command is trivially copyable and its
 * publication copy-constructs nothing (SPEC.md 8.3).
 */
struct DrumClockCommand
{
    DrumClockCommandType type = DrumClockCommandType::None;
    std::uint64_t sampleTime = 0;    // absolute audio sample the command applies at
    std::uint64_t generation = 0;    // ClockSnapshot generation that produced it
    std::uint64_t sequence = 0;      // bridge-assigned monotonic publication order
    double bpm = 0.0;                // SetTempo / JoinAtBar (effective at sampleTime)
    std::int32_t groove = kNoLibraryEntry; // JoinAtBar
    std::int32_t beatsPerBar = 4;
    std::int32_t beatUnit = 4;
    // ResyncBeat / ResyncBar: the step index within the bar (0..barSteps-1) that
    // must land on `sampleTime`. This is the single absolute phase statement
    // both roles apply, so the worker grid and the rendered grid cannot drift
    // into a permanent beat/bar offset.
    std::int32_t phaseStep = -1;

    // --- DRUM-ADAPT-002 BarChange payload ------------------------------------
    // `groove` (above), `fill`, the parameter fields and `changeFields` together
    // are one bounded, trivially-copyable musical instruction. `changeFields`
    // says which of them are authoritative (see DrumChangeField). The engine
    // validates `groove`/`fill` against its prepared bank; the bridge cannot,
    // because the pattern library is not JUCE-free.
    std::int32_t fill = kNoLibraryEntry;      // BarChange: one-bar fill, or no fill
    std::uint8_t changeFields = 0;            // BarChange: DrumChangeField mask
    float intensity01 = 0.5f;                 // BarChange: 0..1 (0.5 = neutral)
    float swing01 = 0.0f;                     // BarChange: 0..1 sixteenth swing
    float humanizeVelocity = 0.25f;           // BarChange: 0..1
    float humanizeTiming = 0.15f;             // BarChange: 0..1
    float humanizeRoundRobin = 0.40f;         // BarChange: 0..1
};

/** Compile-time capacity of the worker -> audio command queue. Structural, not
 *  a tunable: the queue stores its slots inline, so nothing allocates after
 *  construction (SPEC.md 8.1/8.3). */
inline constexpr std::size_t kDrumClockCommandCapacity = 16;

using DrumClockCommandQueue =
    rt::CommandQueue<DrumClockCommand, kDrumClockCommandCapacity>;

/** Configuration for the explicit-clock bridge. Mechanical bounds and clock
 *  reactions only — no musical policy (SPEC.md 10.2/15). */
struct DrumClockBridgeConfig
{
    // Absolute safety rails on the tempo the bridge will forward. Deliberately
    // wider than the musical ClockConfig range (50-220) so a legitimate,
    // already-smoothed belief always passes through unchanged; only nonsense is
    // bounded. Mirrors TransportConfig.
    double minBpm = 20.0;
    double maxBpm = 400.0;

    // Tempo used before the first coherent snapshot exists. Not a belief: it is
    // replaced at the next bar boundary by the first clock tempo.
    double initialBpm = 100.0;

    // Rate assumed before prepare() runs, so the deterministic device-free
    // tests see a stable grid.
    double defaultSampleRate = 48000.0;

    // Meter. Fixed at the session boundary; the bridge ignores meter fields in
    // ClockSnapshot on purpose (a live meter change would reinterpret the grid,
    // and taking it from two authorities is a double-authority bug).
    int beatsPerBar = 4;
    int beatUnit = 4;

    // Explicit-clock sanity. A forward jump larger than this between two
    // setClockSample() calls is treated as a discontinuity rather than elapsed
    // time. One minute at 48 kHz; large enough never to fire on a real callback
    // gap (a block is milliseconds), small enough to catch a session restart.
    std::uint64_t maxForwardJumpSamples = 60ull * 48000ull;

    // Domain ceiling for the explicit sample. Above 2^53 a double can no longer
    // represent every integer, so the sample->beat map silently loses samples.
    // setClockSample() refuses (and counts) any sample beyond this instead of
    // publishing an unrepresentable grid. At 48 kHz this is ~5900 years, so it
    // only ever fires on a corrupt/overflowed caller. uint64 sample wrap itself
    // is out of scope: reaching it would take 2^64 samples.
    std::uint64_t maxExplicitSample = 1ull << 53;
};

class DrumClockBridge
{
public:
    explicit DrumClockBridge (const DrumClockBridgeConfig& config = {}) noexcept;

    DrumClockBridge (const DrumClockBridge&) = delete;
    DrumClockBridge& operator= (const DrumClockBridge&) = delete;

    // --- worker/control side -------------------------------------------------

    /** [worker] Fix the sample rate and clear the grid. Invalid domain values
     *  (rate <= 0, non-finite) leave the bridge unprepared; every later request
     *  is then refused and counted instead of publishing a bogus grid.
     *
     *  A re-prepare at a new rate is a full session reset: anchors, staged
     *  tempo/resync, stop state, snapshot bookkeeping and counters are cleared,
     *  and the command queue is drained. All callers/roles must be quiescent
     *  (no audio consumer mid-pop) while this runs. */
    void prepare (double sampleRate, int maximumBlockSize) noexcept;

    bool isPrepared() const noexcept { return prepared_; }

    /** [worker] Advance to an explicit absolute audio sample.
     *
     *  This is the bridge's ONLY time source. There is no block accumulator.
     *  Returns false when the sample is a discontinuity (it moved backwards, or
     *  jumped forward past maxForwardJumpSamples): the grid is re-anchored at the
     *  new origin, staged changes are discarded, and a Clear command is
     *  published so the audio side cannot keep rendering a stale grid. */
    bool setClockSample (std::uint64_t explicitSample) noexcept;

    /** [worker] The single tempo/lock authority. Stale generations are ignored
     *  and counted. A new tempo is staged for the next bar boundary; meter
     *  fields are ignored (see DrumClockBridgeConfig). */
    void applySnapshot (const ClockSnapshot& snapshot) noexcept;

    /** [worker] Queue the prepared groove to begin at the next bar boundary. */
    bool requestJoinAtNextBar (LibraryIndex groove = kNoLibraryEntry) noexcept;

    /** [worker] DRUM-ADAPT-002: publish a bounded adaptive musical change for
     *  the next STRICTLY FUTURE bar boundary and return whether it was accepted.
     *
     *  `change.groove` / `change.fill` are library indices (kNoLibraryEntry =
     *  "no change" / "no fill"), and they are matched by the engine against the
     *  immutable bank prepared off the audio callback. The call rejects, and
     *  publishes nothing, when:
     *    - the bridge is unprepared;
     *    - any parameter is NaN / infinite;
     *    - an index is below kNoLibraryEntry;
     *    - the generation is older than the newest accepted bar-change
     *      generation (stale decision); or
     *    - the bounded command queue is full.
     *  A rejection returns false and leaves the previous staged change exactly
     *  as it was, so a later tick can retry; acceptance is the only thing that
     *  commits the director's decision. Repeated identical calls coalesce into
     *  the one staged command and do not flood the queue.
     *
     *  Groove/fill KIND and METER are validated by the engine at bank-prepare
     *  time (only existing 4/4 library entries are admitted), which is the only
     *  place the JUCE library is available; the engine rejects a command that
     *  names an entry it was not given and counts it. */
    bool requestBarChange (const QueuedBarChange& change) noexcept;

    /** [worker] DRUM-ADAPT-002 convenience wrapper: play `fill` for exactly one
     *  bar at the next strictly future bar, then return to the currently
     *  selected groove, WITHOUT disturbing the director's intensity, swing or
     *  humanization. Rejects a negative index and an unprepared bridge. */
    bool requestFillAtNextBar (LibraryIndex fill) noexcept;

    /** [worker] Stop the injected transport at the next bar boundary. */
    bool requestStopAtNextBar() noexcept;

    /** [worker] Bounded stop at the next SERVICED audio block (not the next
     *  bar). Publishes a cancel/clear command that, when the engine services
     *  it, discards all pending join/tempo/resync events, releases any active
     *  voices and leaves injected mode, so the legacy manual transport is
     *  usable again without a device prepare. Returns false (and counts) when
     *  the command queue is full, leaving the worker grid state untouched so a
     *  later retry can still stop. */
    bool requestStopNow() noexcept;

    /** [worker] Re-phase so the next beat onset lands exactly on `targetSample`. */
    bool requestResyncNextBeat (std::uint64_t targetSample) noexcept;

    /** [worker] Re-phase so the next downbeat lands exactly on `targetSample`.
     *
     *  COLLISION ORDER WITH A STAGED TEMPO. A resync may be requested while a
     *  tempo is already staged for a future bar boundary B. The bridge resolves
     *  the two staged transitions in chronological order when a later
     *  setClockSample crosses both: if the resync target T precedes B, the
     *  resync re-anchors in the old-tempo region first, then the tempo applies at
     *  B; if B precedes T, the tempo re-anchors first and the resync reads the
     *  new grid. One `setClockSample` crossing both therefore produces the same
     *  grid as crossing them stepwise, and the engine (which applies the same
     *  absolute-target commands in order) stays on the worker grid. The clock
     *  remains the sole tempo authority; the resync never invents a tempo. */
    bool requestResyncNextBar (std::uint64_t targetSample) noexcept;

    /** [worker] Session reset. All roles (producer, audio consumer and readers)
     *  must be quiescent and joined: this drains the command queue, rebaselines
     *  the per-session drop counter, clears the grid and publishes one Clear for
     *  a still-attached engine. */
    void resetForNewSession (double bpm) noexcept;

    // --- audio side ----------------------------------------------------------

    /** [audio] Consume one command. Bounded, lock-free, allocation-free. */
    bool popCommand (DrumClockCommand& out) noexcept { return queue_.pop (out); }

    DrumClockCommandQueue& commandQueue() noexcept { return queue_; }
    const DrumClockCommandQueue& commandQueue() const noexcept { return queue_; }

    // --- diagnostics / state (worker side) -----------------------------------

    bool playing() const noexcept { return playing_; }
    bool stopPending() const noexcept { return stopPending_; }
    std::uint64_t pendingStopBoundary() const noexcept { return stopBoundary_; }
    double bpm() const noexcept { return bpm_; }
    int beatsPerBar() const noexcept { return beatsPerBar_; }
    int beatUnit() const noexcept { return beatUnit_; }
    std::uint64_t samplePosition() const noexcept { return now_; }

    /** Continuous beats since the current anchor, from the explicit clock. */
    double beatsAt (std::uint64_t sample) const noexcept;

    /** The next bar boundary strictly after the current sample. */
    std::uint64_t nextBarBoundarySample() const noexcept;

    /** A snapshot of the current grid position (0/0 and not playing when the
     *  injected transport is stopped). */
    TransportPosition position() const noexcept;

    std::uint64_t publishedCount() const noexcept { return publishedCount_; }
    std::uint64_t staleSnapshotCount() const noexcept { return staleSnapshotCount_; }
    std::uint64_t staleBarChangeCount() const noexcept { return staleBarChangeCount_; }
    std::uint64_t discontinuityCount() const noexcept { return discontinuityCount_; }
    std::uint64_t invalidRequestCount() const noexcept { return invalidRequestCount_; }
    /** True while a BarChange/Fill is staged for a future bar boundary. */
    bool barChangePending() const noexcept { return haveStagedChange_; }
    std::uint64_t barChangeBoundarySample() const noexcept
    {
        return haveStagedChange_ ? stagedChangeBoundary_ : 0;
    }
    /** Drops since the current session began (prepare() rebaselines it). The
     *  underlying queue is never reset, which is only safe while quiescent. */
    std::uint64_t queueDropCount() const noexcept
    {
        return queue_.droppedCount() - dropBaseline_;
    }

private:
    void publish (DrumClockCommand command) noexcept;
    void stageTempo (double bpm) noexcept;
    void applyStagedState (std::uint64_t atSample) noexcept;
    void applyStagedResync (std::uint64_t atSample) noexcept;

    /** Shared BarChange staging used by requestBarChange()/requestFillAtNextBar().
     *  `fields` is a DrumChangeField mask; `change` carries the payload. */
    bool stageBarChange (std::uint8_t fields, const QueuedBarChange& change) noexcept;
    bool validBarChangePayload (const QueuedBarChange& change) const noexcept;
    void applyStagedBarChange (std::uint64_t atSample) noexcept;
    void clearStagedBarChange() noexcept;

    double clampBpm (double bpm) const noexcept;
    double samplesPerBeat() const noexcept;
    double samplesPerBar() const noexcept;
    std::uint64_t boundaryForBeat (double targetBeat) const noexcept;
    /** Beats at `sample` accounting for a tempo staged for a boundary at or
     *  before it: the old rate up to that boundary, the staged rate after. This
     *  keeps a resync phase statement correct when its target lies beyond a
     *  pending tempo change. */
    double beatsAtAccountingStaged (std::uint64_t sample) const noexcept;

    DrumClockBridgeConfig config_;
    DrumClockCommandQueue queue_;

    bool   prepared_ = false;
    double sampleRate_ = 48000.0;
    int    maximumBlockSize_ = 0;

    bool   playing_ = false;
    bool   haveClockSample_ = false;

    // A stop is a pending commitment, not an immediate state change: the engine
    // keeps rendering until the boundary, so the worker's position must too.
    bool          stopPending_ = false;
    std::uint64_t stopBoundary_ = 0;

    double bpm_ = 100.0;
    int    beatsPerBar_ = 4;
    int    beatUnit_ = 4;

    // Exact re-anchored sample -> beat map. bpm_ is re-anchored at tempo and
    // resync boundaries; position is recomputed, never accumulated.
    std::uint64_t now_ = 0;
    std::uint64_t anchorSample_ = 0;
    double        anchorBeat_ = 0.0;

    // Tempo staged for a future bar boundary. The engine is told immediately so
    // it receives the command with lead time; the bridge re-anchors when the
    // boundary is crossed, keeping its own grid and the engine's in lockstep.
    bool          haveStagedTempo_ = false;
    double        stagedBpm_ = 0.0;
    std::uint64_t stagedBoundary_ = 0;

    // Resync staged to re-anchor the worker grid at the same instant the engine
    // re-phases, using the same absolute phase statement (phaseStep).
    bool          haveStagedResync_ = false;
    bool          stagedResyncBar_ = false;
    std::uint64_t stagedResyncTarget_ = 0;

    // BarChange staged for a future bar boundary (DRUM-ADAPT-002). Coalesced
    // exactly like tempo so a director polling every tick cannot flood the
    // bounded command queue. The payload is latched only after the bounded
    // publish is accepted; on a drop the previous staging is untouched.
    bool          haveStagedChange_ = false;
    std::uint64_t stagedChangeBoundary_ = 0;
    std::uint8_t  stagedChangeFields_ = 0;
    std::int32_t  stagedChangeGroove_ = kNoLibraryEntry;
    std::int32_t  stagedChangeFill_ = kNoLibraryEntry;
    float         stagedIntensity_ = 0.5f;
    float         stagedSwing_ = 0.0f;
    float         stagedHumanVel_ = 0.25f;
    float         stagedHumanTime_ = 0.15f;
    float         stagedHumanRR_ = 0.40f;

    // Stale bar-change generation guard (mirrors the snapshot generation guard).
    bool          haveBarChangeGeneration_ = false;
    std::uint64_t lastBarChangeGeneration_ = 0;

    // Clock observation bookkeeping (stale detection).
    bool          haveSnapshot_ = false;
    std::uint64_t lastSnapshotGeneration_ = 0;

    // Diagnostics.
    std::uint64_t commandSeq_ = 0;
    std::uint64_t publishedCount_ = 0;
    std::uint64_t staleSnapshotCount_ = 0;
    std::uint64_t staleBarChangeCount_ = 0;
    std::uint64_t discontinuityCount_ = 0;
    std::uint64_t invalidRequestCount_ = 0;
    // Queue drop count is cumulative in the shared primitive; this records the
    // value at the start of the current session so queueDropCount() is per-session.
    std::uint64_t dropBaseline_ = 0;
};

} // namespace jam
