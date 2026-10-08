// Rhythm diagnostics and trace foundation (SPEC.md 22, DEVPLAN DIAG-001).
//
// This is the developer-only trace seam for "the drummer felt wrong" reports.
// It is platform-neutral (no JUCE, no audio device) and it is INJECTED-CORE
// ONLY for now: nothing in the plugin, the analyzer or the clock calls it yet.
// The production processor/analyzer integration is still pending (INT-ANALYSIS-001).
//
// Thread roles (one of each, enforced by documentation and by the lifecycle):
//   - Publisher: DiagnosticsTrace::publish(). Bounded, non-blocking, no allocation,
//     no lock. Intended for the analysis/control thread that drains the
//     RhythmAnalyzer output queue, never for a device callback.
//   - Consumer: DiagnosticsTrace::pop() and DiagnosticsCollector::drain(). The
//     non-real-time exporter.
//   - Reader: DiagnosticsTrace::readViewModel() for the UI/message thread. It is
//     a latest-value telemetry path ONLY. It never stands in for trace history.
//   - Lifecycle: DiagnosticsTrace::reset() requires publisher AND consumer to be
//     quiescent, and the caller plays the consumer role while it discards queued
//     events. setEnabled() and counters() may be called from any thread.
//
// Two channels, deliberately separate:
//   1. Ordered trace evidence: rt::CommandQueue (SPSC, drop-INCOMING-and-count on
//      full). Every accepted event is delivered in publication order, or the
//      loss is counted. Nothing already queued is overwritten.
//   2. Latest-value UI telemetry: rt::LatestValue. A reader may miss intermediate
//      values and gets at most one attempt per read. Only whole, unraced events
//      are returned.
//
// Tracing off: publish() reads one flag and returns. It copies nothing into
// either channel and performs no atomic read-modify-write, only a relaxed
// count of skipped calls.
//
// Provenance policy (lossless):
//   - Every uint64_t sample time (event, block start, input horizon, receipt,
//     stream generation, sequence) is stored and serialised as an exact
//     unsigned integer. Wrap of the device clock is preserved, not normalised.
//   - The input horizon is the audio-data HORIZON, a lower bound on
//     causality. It is NEVER a live-availability time. Live availability is only
//     recorded through attachLiveReceipt(), which takes an externally measured
//     receipt time. The default is unmeasured.
//   - Callback/device latency and processing duration are unmeasured unless an
//     explicit measured value is attached. Unmeasured values serialise as null
//     (JSON) or an empty cell (CSV), never as 0.
//   - Non-finite or out-of-range inputs are kept as the raw evidence, counted in
//     DiagnosticsEvent::invalidFields, and serialised as null/empty. A NaN or Inf
//     never reaches the output text.
//
// Export policy (non-RT, after the publisher has stopped):
//   - DiagnosticsCollector is the ONLY growable store. It is bounded by the
//     maxEvents passed at construction. Events beyond that bound are dropped and
//     counted, never silently discarded.
//   - toCsv()/toJson() allocate and must only be called by the consumer role.
//   - Drops are reported in every export: the trace queue drops as of the last
//     drain, and the collector drops. CSV repeats them per row; JSON carries
//     them once at the top level.
//   - Drain cadence: drain at least once per kTraceCapacity published events, or
//     accept that the trace queue drops the excess. Each drain() call pops at most
//     kTraceCapacity events, so one call is bounded.

#pragma once

#include "RhythmAnalyzer.h"
#include "RhythmTypes.h"
#include "rt/RtSignal.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jam::diagnostics
{

/** Version of the CSV/JSON export layout. SPEC.md 23: version all persisted
    structures. Bump on any column or key change. */
inline constexpr int kSchemaVersion = 1;

/** Fixed capacity of the ordered trace queue. The event record is roughly 260
    bytes, so the queue is about 64 KiB. Allocate DiagnosticsTrace on the heap
    (for example with std::make_unique), never on the stack. */
inline constexpr std::size_t kTraceCapacity = 256;

/** Number of per-event export columns. Each CSV row adds two per-export drop
    columns: see toCsv(). */
inline constexpr std::size_t kEventColumnCount = 39;

/** A value that exists only when someone measured it. */
template <typename T>
struct MeasuredField
{
    T value {};
    bool measured = false;
};

/** One publication: the raw evidence and its provenance, plus the optional
    fields the integrator may attach. Trivially copyable, so it can cross the
    SPSC queue as bytes. */
struct DiagnosticsEvent
{
    ObservationEnvelope envelope {};        // evidence + lossless provenance (unaltered)
    uint64_t traceSession = 0;              // set by publish(); scopes LatestValue reads

    bool clockKnown = false;                // false: no ClockSnapshot was attached
    ClockSnapshot clock {};

    MeasuredField<uint64_t> ringOverruns {};          // cumulative AnalysisAudioRing overruns
    MeasuredField<uint64_t> observationDrops {};      // cumulative RhythmAnalyzer output drops
    MeasuredField<uint64_t> liveReceiptSampleTime {}; // externally measured consumer receipt
    MeasuredField<double> callbackLatencySeconds {};  // externally measured
    MeasuredField<double> processingDurationSeconds {}; // externally measured

    uint32_t invalidFields = 0;             // non-finite or out-of-range inputs captured as-is
};

/** Capture an analyzer envelope unaltered. Every optional field starts unmeasured
    and the input horizon is NOT copied into any availability field. */
DiagnosticsEvent makeEvent (const ObservationEnvelope& envelope) noexcept;

/** Attach a clock snapshot. Non-finite doubles are counted in invalidFields and
    serialise as null. */
void attachClock (DiagnosticsEvent& event, const ClockSnapshot& clock) noexcept;

void attachRingOverruns (DiagnosticsEvent& event, uint64_t cumulativeOverruns) noexcept;
void attachObservationDrops (DiagnosticsEvent& event, uint64_t cumulativeDrops) noexcept;

/** Explicit, externally measured consumer receipt time only. Never derived from
    the input horizon. */
void attachLiveReceipt (DiagnosticsEvent& event, uint64_t receiptSampleTime) noexcept;

/** Rejects a non-finite or negative duration: the field stays unmeasured and
    invalidFields is incremented. */
void attachCallbackLatency (DiagnosticsEvent& event, double seconds) noexcept;
void attachProcessingDuration (DiagnosticsEvent& event, double seconds) noexcept;

struct DiagnosticsCounters
{
    uint64_t session = 0;               // increments on every reset()
    bool tracingEnabled = false;
    uint64_t publishedEvents = 0;       // accepted into the trace queue this session
    uint64_t droppedEvents = 0;         // incoming events rejected by a full trace queue this session
    uint64_t skippedWhileDisabled = 0;  // publish() calls made while tracing was off
    uint64_t discardedAtReset = 0;      // queued events discarded by the most recent reset()
};

/** Caller-owned UI state. readViewModel() updates it only when a new whole event
    is available, so a raced or repeated read keeps the previous value. */
struct DiagnosticsViewModel
{
    DiagnosticsCounters counters {};
    bool hasLatest = false;             // latest is a whole event from the current session
    DiagnosticsEvent latest {};
    const char* clockLabel = "Unknown"; // ClockLockState text, or "Unknown" when no clock was attached
    bool latestSeen = false;            // reader-owned cache state; callers should not set it
};

class DiagnosticsTrace
{
public:
    DiagnosticsTrace() noexcept;
    DiagnosticsTrace (const DiagnosticsTrace&) = delete;
    DiagnosticsTrace& operator= (const DiagnosticsTrace&) = delete;

    /** Publisher only. Returns false when tracing is off or the trace queue is full.
        Bounded, no allocation, no lock, no retry. */
    bool publish (const DiagnosticsEvent& event) noexcept;

    /** Consumer only. Pops the oldest queued event in publication order. */
    bool pop (DiagnosticsEvent& out) noexcept;

    /** Any thread. Stop = setEnabled(false): queued events stay for the final drain. */
    void setEnabled (bool enabled) noexcept;
    bool enabled() const noexcept;

    /** Reader only (one UI reader). Refreshes the latest-value telemetry in `vm`. */
    void readViewModel (DiagnosticsViewModel& vm) const noexcept;

    /** Relaxed multi-field sample. Each counter is exact. The set is not one atomic snapshot. */
    DiagnosticsCounters counters() const noexcept;

    /** Lifecycle only: publisher and consumer must be quiescent. The caller plays
        the consumer role while queued events are discarded. Returns the count
        discarded. Drop counts restart from zero, and the session increments so
        stale latest-value telemetry is ignored. */
    std::size_t reset() noexcept;

    static constexpr std::size_t capacity() noexcept { return kTraceCapacity; }

private:
    rt::CommandQueue<DiagnosticsEvent, kTraceCapacity> queue_;
    rt::LatestValue<DiagnosticsEvent> latest_;

    std::atomic<bool> enabled_ { false };
    std::atomic<uint64_t> session_ { 1 };

    // Publisher-written, relaxed. Reset rewrites them only while quiescent.
    std::atomic<uint64_t> published_ { 0 };
    std::atomic<uint64_t> skipped_ { 0 };
    std::atomic<uint64_t> dropBase_ { 0 };
    std::atomic<uint64_t> discardedAtReset_ { 0 };
};

/** Non-RT growable store. Consumer role only. Bounded by maxEvents. */
class DiagnosticsCollector
{
public:
    explicit DiagnosticsCollector (std::size_t maxEvents);

    /** Pops at most kTraceCapacity events. Records the trace drop count as of
        this drain. Excess events beyond maxEvents are counted, not stored. */
    std::size_t drain (DiagnosticsTrace& trace);

    const std::vector<DiagnosticsEvent>& events() const noexcept { return events_; }
    uint64_t traceDroppedAtDrain() const noexcept { return traceDroppedAtDrain_; }
    uint64_t collectorDropped() const noexcept { return collectorDropped_; }
    std::size_t maxEvents() const noexcept { return maxEvents_; }

    /** Clears retained events and collector drops. Call together with reset(). */
    void clear() noexcept;

private:
    std::size_t maxEvents_;
    std::vector<DiagnosticsEvent> events_;
    uint64_t traceDroppedAtDrain_ = 0;
    uint64_t collectorDropped_ = 0;
};

/** Export of the retained events. Non-RT only. The CSV header is the first line,
    rows are LF-separated, and missing or non-finite values are empty cells. */
std::string toCsv (const DiagnosticsCollector& collector);

/** Export as one JSON document with a top-level summary and an events array.
    Missing or non-finite values are null. Unsigned 64-bit values are written as
    exact decimal integers. A JavaScript consumer must parse them as BigInt or
    string, because doubles lose precision above 2^53. */
std::string toJson (const DiagnosticsCollector& collector);

/** RFC 4180 field quoting. Quotes the field when it contains comma, quote or newline. */
std::string csvEscape (std::string_view text);

/** JSON string escaping for quotes, backslash and control characters (\u00XX). */
std::string jsonEscape (std::string_view text);

} // namespace jam::diagnostics
