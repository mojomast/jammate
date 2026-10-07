// Drum transport adapter — the policy-free, boundary-quantising scheduler seam.
//
// DEVPLAN MOD-002. The Jam Director speaks only to `IDrumTransport`; this class
// is its concrete implementation and is the *only* thing that stands between the
// director's musical decisions and whatever renders drums underneath. It exists
// so DrumEngine never learns that beat tracking exists and so the director never
// learns how drums are rendered (SPEC.md 2.3, 6, 15, 16).
//
// WHAT THIS CLASS DOES
//   - Holds pending musical changes and applies each exactly once, on a musical
//     bar boundary, never mid-bar (SPEC.md product principle 4).
//   - Derives bar/beat position from an explicit *sample clock*, never from an
//     accumulated float, so a 30-minute run cannot drift (requirement 2).
//   - Applies exactly the tempo it is handed. It does not select, smooth,
//     predict or second-guess tempo; the only tempo path is applyClock().
//
// WHAT THIS CLASS DELIBERATELY DOES NOT DO
//   - No music-selection policy. Which groove, when a fill happens and when to
//     join are Jam Director decisions (DEVPLAN 19 / DRUM-001). There is no
//     random choice, no fill probability, no style knowledge here at all.
//
// THREADING
//   Driven from the Jam-control worker thread, not the audio callback. Even so
//   it is bounded: fixed-capacity storage, no allocation after construction, no
//   blocking, no I/O, no unbounded growth. The audio thread never calls it.
//
// HEADER HYGIENE (SPEC.md 7.1 / 8.1): only <cstdint>/<cstddef> and the frozen
// jam headers. No STL containers, no std::string, no JUCE, no DrumEngine (which
// would drag JUCE in and break the platform-neutral jam-core build).

#pragma once

#include <cstddef>
#include <cstdint>

#include "IDrumTransport.h"
#include "JamConfig.h"
#include "RhythmTypes.h"

namespace jam
{

/** Compile-time size of the pending-change buffer. Structural, not a tunable:
 *  the buffer is a member array so nothing allocates after construction
 *  (SPEC.md 8.1/8.3). Runtime capacity is TransportConfig::maxPendingChanges,
 *  clamped to this maximum. */
inline constexpr int kMaxPendingChanges = 16;

/** Compile-time size of the deterministic event log. Structural: the log is a
 *  member array, and it is bounded so a long session cannot grow it without
 *  limit. It stores the first kMaxTransportEvents events; totalEventCount()
 *  keeps counting beyond that. */
inline constexpr std::size_t kMaxTransportEvents = 64;

/** What a logged scheduling event was. Tests assert ordering and timing from
 *  these; the adapter itself never reads them back. */
enum class TransportEventType : std::uint8_t
{
    Start = 1,
    Stop = 2,
    BarChangeApplied = 3,
    FillApplied = 4,
    ResyncBeat = 5,
    ResyncBar = 6,
    TempoApplied = 7
};

/** One entry of the bounded, inspectable scheduling log. `value` carries the
 *  applied BPM for TempoApplied, `index` carries the applied LibraryIndex for
 *  groove/fill events. */
struct TransportEvent
{
    std::uint64_t      sampleTime = 0;
    double             value = 0.0;
    std::int32_t       index = kNoLibraryEntry;
    TransportEventType type = TransportEventType::Start;
};

class DrumTransportAdapter final : public IDrumTransport
{
public:
    explicit DrumTransportAdapter (const TransportConfig& config = {});

    // --- IDrumTransport -----------------------------------------------------

    /** [worker] Sets the sample rate the sample clock runs at. The adapter
     *  learns about time only here and in advance(); nothing else changes it. */
    void prepare (double sampleRate, int maximumBlockSize) override;

    /** [worker] Begins a session at the sample clock's zero. Any change held
     *  while stopped is applied immediately, on the bar-1 downbeat. */
    void startTransport (double bpm, int beatsPerBar, int beatUnit) override;

    /** [worker] Stops immediately. Position stays inspectable. */
    void stopTransport() override;

    /** [worker] Tempo-only entry point. Internally routed through applyClock()
     *  so there is exactly one tempo path. It does *not* touch the lock state. */
    void setClockTempo (double bpm) override;

    void requestResyncNextBeat (std::uint64_t targetSampleTime) override;
    void requestResyncNextBar (std::uint64_t targetSampleTime) override;
    void queueBarChange (const QueuedBarChange& change) override;
    void requestFillAtNextBar (LibraryIndex fill) override;
    void requestStopAtNextBar() override;
    TransportPosition position() const override;

    // --- concrete scheduling surface (not part of IDrumTransport) -----------

    /** Advance the sample clock by `numSamples`. The host calls this once per
     *  processed block, on the control thread. This is how time passes: position
     *  is a pure function of this integer counter, which is why it cannot drift.
     *  While stopped, time does not pass. */
    void advance (std::uint64_t numSamples);

    /** Apply the band's stable belief. This is the ONLY path by which tempo and
     *  lock state enter the adapter. The bpm is clamped to the configured
     *  absolute rails and is never smoothed or inferred. */
    void applyClock (const ClockSnapshot& snapshot);

    // --- diagnostics / test surface -----------------------------------------

    /** Last clamped BPM accepted from the clock (the "belief"), regardless of
     *  whether a tempo change is still pending application. */
    double targetBpm() const noexcept { return clockBpm_; }

    /** Currently applied musical change (groove/fill/humanize). */
    const QueuedBarChange& appliedChange() const noexcept { return applied_; }

    int pendingChangeCount() const noexcept { return pendingCount_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int maximumBlockSize() const noexcept { return maximumBlockSize_; }
    std::uint64_t samplePosition() const noexcept { return samplePosition_; }
    std::uint64_t lastBoundarySample() const noexcept { return lastBoundarySample_; }

    /** Continuous beat position since start, from the sample clock. */
    double beatsElapsed() const noexcept { return beatsAt (samplePosition_); }
    double beatPhase01() const noexcept;
    double barPhase01() const noexcept;

    int beatsPerBar() const noexcept { return beatsPerBar_; }
    int beatUnit() const noexcept { return beatUnit_; }

    ClockLockState clockLockState() const noexcept { return lockState_; }
    bool tempoFrozen() const noexcept { return tempoFrozen_; }

    std::uint64_t appliedBarChangeCount() const noexcept { return appliedBarChangeCount_; }
    std::uint64_t appliedFillCount() const noexcept { return appliedFillCount_; }
    std::uint64_t consumedChangeCount() const noexcept { return consumedChangeCount_; }
    std::uint64_t overflowCount() const noexcept { return overflowCount_; }
    std::uint64_t staleDropCount() const noexcept { return staleDropCount_; }
    std::uint64_t deferredBoundaryCount() const noexcept { return deferredBoundaryCount_; }
    std::uint64_t tempoIgnoredCount() const noexcept { return tempoIgnoredCount_; }
    std::uint64_t droppedAtStopCount() const noexcept { return droppedAtStopCount_; }

    std::size_t eventCount() const noexcept { return eventCount_; }
    const TransportEvent& event (std::size_t index) const noexcept { return events_[index]; }
    std::uint64_t totalEventCount() const noexcept { return totalEventCount_; }

private:
    struct PendingChange
    {
        std::uint64_t generation = 0;
        LibraryIndex  groove = kNoLibraryEntry;
        LibraryIndex  fill = kNoLibraryEntry;
        float intensity01 = 0.5f;
        float swing01 = 0.0f;
        float humanizeVelocity = 0.25f;
        float humanizeTiming = 0.15f;
        float humanizeRoundRobin = 0.40f;
        std::uint8_t origin = 0; // 0 = queueBarChange, 1 = requestFillAtNextBar
    };

    // --- time base ----------------------------------------------------------
    double samplesPerBeat() const noexcept;
    double beatsAt (std::uint64_t sample) const noexcept;
    long long barIndexAt (std::uint64_t sample) const noexcept;
    std::uint64_t boundarySampleFor (long long barBefore) const noexcept;
    void reanchorAt (std::uint64_t sample) noexcept;

    // --- scheduling ---------------------------------------------------------
    bool enqueue (const PendingChange& change) noexcept;
    void consumePending (std::uint64_t boundarySample) noexcept;
    bool handleBarBoundary (std::uint64_t boundarySample) noexcept;
    void applyResyncAt (std::uint64_t targetSample) noexcept;
    double clampBpm (double bpm) const noexcept;

    // --- diagnostics --------------------------------------------------------
    void record (TransportEventType type, std::uint64_t sampleTime,
                 double value = 0.0, std::int32_t index = kNoLibraryEntry) noexcept;

    TransportConfig config_;

    double sampleRate_ = 48000.0;
    int    maximumBlockSize_ = 0;

    // Transport state. bpm_ is the *active* grid tempo; it is re-anchored, never
    // accumulated. anchorSample_/anchorBeat_ define the exact linear map from
    // sample time to beats in effect since the last tempo/resync change.
    bool   playing_ = false;
    double bpm_ = 0.0;
    int    beatsPerBar_ = 4;
    int    beatUnit_ = 4;

    std::uint64_t samplePosition_ = 0;
    std::uint64_t anchorSample_ = 0;
    double        anchorBeat_ = 0.0;

    std::uint64_t lastBoundarySample_ = 0;

    // Clock belief.
    double         clockBpm_ = 0.0;
    bool           havePendingTempo_ = false;
    double         pendingTempo_ = 0.0;
    ClockLockState lockState_ = ClockLockState::Acquiring;
    bool           tempoFrozen_ = false;
    std::uint64_t  clockGeneration_ = 0;

    // Explicit resync (last request wins).
    bool           havePendingResync_ = false;
    bool           pendingResyncIsBar_ = false;
    std::uint64_t  pendingResyncTarget_ = 0;

    // Pending musical changes, FIFO, fixed capacity.
    PendingChange pending_[kMaxPendingChanges] = {};
    int           pendingCount_ = 0;
    std::uint64_t lastBarGeneration_ = 0;
    bool          pendingStop_ = false;

    QueuedBarChange applied_;

    // Counters (cumulative diagnostics, never reset by the scheduler).
    std::uint64_t appliedBarChangeCount_ = 0;
    std::uint64_t appliedFillCount_ = 0;
    std::uint64_t consumedChangeCount_ = 0;
    std::uint64_t overflowCount_ = 0;
    std::uint64_t staleDropCount_ = 0;
    std::uint64_t deferredBoundaryCount_ = 0;
    std::uint64_t tempoIgnoredCount_ = 0;
    std::uint64_t droppedAtStopCount_ = 0;

    // Bounded event log.
    TransportEvent events_[kMaxTransportEvents] = {};
    std::size_t    eventCount_ = 0;
    std::uint64_t  totalEventCount_ = 0;
};

} // namespace jam
