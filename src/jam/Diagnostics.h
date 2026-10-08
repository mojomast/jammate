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
//   - Lifecycle: setEnabled(), reset(), clear(). See "Lifecycle contract".
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
// Allocation scope (measured evidence, see docs/research/JAM-DIAGNOSTICS.md):
//   - publish() and pop() perform no heap work; a net-heap probe over repeated
//     publish/pop cycles shows no change in allocator in-use bytes. That is net
//     evidence, NOT a proof that no malloc/free pair occurs.
//   - DiagnosticsCollector::drain() MAY allocate: its std::vector grows when an
//     event is retained. That is the consumer role and it is off the audio path.
//   - toCsv()/toJson() allocate and are off the RT path.
//
// Locking: the publish/pop path contains no mutex and no lock primitive; it uses
// only the lock-free atomics of rt::CommandQueue and rt::LatestValue. This is a
// source-level statement about the code, not a runtime lock measurement.
//
// Domain policy (the validity rules used for counting and for export masking).
// Raw evidence is ALWAYS kept in the record; only the EXPORT masks an invalid
// field to null/empty. A masked field is still visible as invalid_field_count.
//   - sourceSampleRate (observation and envelope): finite and > 0.
//   - BPM (candidate and clock): finite and >= 0. Zero means "unknown" and is
//     accepted; a negative or non-finite BPM is invalid.
//   - 01 unit-interval fields (beatPhase01, beatConfidence01, onsetStrength01,
//     transientDensity01, clock beat/bar phase, clock confidence): finite and
//     within [0, 1].
//   - energyRmsDbfs: finite and <= 0. A positive dBFS value is out of domain.
//   - clock meter: beatsPerBar >= 1 and beatUnit >= 1.
//   - clock beatInBar: 0 means "not yet known" and is accepted; otherwise it
//     must be within [1, beatsPerBar].
//   - Durations (callback latency, processing duration): finite and >= 0 when
//     measured. A rejected attachment CLEARS the measurement (see below).
//
// Repeated attachments: validity is per FIELD, not per attempt. Attaching an
// invalid value twice counts once, and attaching a valid value after an invalid
// one clears the field's invalid mark.
//
// Measurement rejection: attachCallbackLatency() and attachProcessingDuration()
// do not keep a previously measured value when the new value is rejected. The
// field returns to unmeasured, so an export can never show a stale valid reading
// next to a rejected one.
//
// Provenance policy (lossless):
//   - Every uint64_t sample time (event, block start, input horizon, receipt,
//     stream generation, sequence) is stored and serialised as an exact
//     unsigned integer. Wrap of the device clock is preserved, not normalised.
//   - BOTH the observation's sourceSampleRate and the envelope's sourceSampleRate
//     are exported, so a disagreement between them stays visible instead of one
//     silently replacing the other.
//   - The input horizon is the audio-data HORIZON, a lower bound on
//     causality. It is NEVER a live-availability time. Live availability is only
//     recorded through attachLiveReceipt(), which takes an externally measured
//     receipt time. The default is unmeasured.
//   - Callback/device latency and processing duration are unmeasured unless an
//     explicit measured value is attached. Unmeasured values serialise as null
//     (JSON) or an empty cell (CSV), never as 0.
//
// Numeric serialisation is locale-independent: the exporter converts through
// std::to_chars, which is specified never to consult the C locale. A comma
// decimal-point locale can therefore never emit invalid JSON or a broken CSV row.
//
// Lifecycle contract:
//   - setEnabled(false) stops ACCEPTING new events but is NOT publisher
//     quiescence: a publish() already in flight may still enqueue afterwards.
//     The owner must stop and JOIN the publisher (the analyzer worker) BEFORE
//     calling setEnabled(false), and only then perform the final drain and
//     export. Queued events are never discarded by disabling.
//   - reset() and DiagnosticsCollector::clear() require ALL roles to be
//     quiescent: publisher, consumer, UI reader and any counters() reader.
//     counters() samples relaxed atomics and a drop baseline independently, so a
//     reader crossing a reset could observe a pre-reset drop count against a
//     post-reset baseline, which underflows when subtracted.
//   - readViewModel() supports a single reader; its cache fields are owned by
//     that reader.

#pragma once

#include "RhythmAnalyzer.h"
#include "RhythmTypes.h"
#include "rt/RtSignal.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace jam::diagnostics
{

/** Version of the CSV/JSON export layout. SPEC.md 23: version all persisted
    structures. Bump on any column or key change.

    This value was FROZEN at 1 before any first integration existed: no consumer
    has parsed this format, because the trace is not wired into the processor
    yet. The layout is finalised in this commit (metadata preamble, both source
    rates, versioned schema) while it is still uninhabited, which is the cheapest
    moment to settle it. Any later layout change MUST bump this. */
inline constexpr int kSchemaVersion = 1;

/** Fixed capacity of the ordered trace queue. The event record is roughly 260
    bytes, so the queue is about 64 KiB. Allocate DiagnosticsTrace on the heap
    (for example with std::make_unique), never on the stack. */
inline constexpr std::size_t kTraceCapacity = 256;

/** Number of per-event export columns. Each CSV row adds two per-export drop
    columns: see toCsv(). */
inline constexpr std::size_t kEventColumnCount = 40;

/** Prefix of the CSV metadata preamble lines. See toCsv() for the grammar. */
inline constexpr const char* kCsvMetadataPrefix = "# ";

// --- domain policy ------------------------------------------------------------

inline bool isPositiveRate (double v) noexcept { return std::isfinite (v) && v > 0.0; }

/** Zero means "unknown" and is accepted; negative or non-finite is invalid. */
inline bool isTempoValue (double v) noexcept { return std::isfinite (v) && v >= 0.0; }

inline bool isUnitInterval (double v) noexcept
{
    return std::isfinite (v) && v >= 0.0 && v <= 1.0;
}

inline bool isDecibelFullScale (double v) noexcept { return std::isfinite (v) && v <= 0.0; }

inline bool isNonNegativeSeconds (double v) noexcept
{
    return std::isfinite (v) && v >= 0.0;
}

inline bool isPositiveMeter (int v) noexcept { return v >= 1; }

/** Zero means "not yet known" and is accepted; otherwise the beat must lie
    within the bar. */
inline bool isKnownBeatInBar (int beatInBar, int beatsPerBar) noexcept
{
    if (beatInBar == 0)
        return true;
    return beatInBar >= 1 && beatInBar <= beatsPerBar;
}

/** One bit per exported, domain-validated field. A set bit means the raw value
    stays in the record but the export masks it to null/empty. */
enum class DiagnosticsField : uint32_t
{
    none                     = 0u,

    observationRate          = 1u << 0,
    envelopeRate             = 1u << 1,
    candidateBpm             = 1u << 2,
    observationConfidence01  = 1u << 3,
    observationBeatPhase01   = 1u << 4,
    onsetStrength01          = 1u << 5,
    transientDensity01       = 1u << 6,
    energyRmsDbfs            = 1u << 7,

    clockBpm                 = 1u << 8,
    clockBeatPhase01         = 1u << 9,
    clockBarPhase01          = 1u << 10,
    clockConfidence01        = 1u << 11,
    clockBeatInBar           = 1u << 12,
    clockBeatsPerBar         = 1u << 13,
    clockBeatUnit            = 1u << 14,

    callbackLatency          = 1u << 15,
    processingDuration       = 1u << 16
};

// The evidence bits (observation, envelope and clock) must occupy a contiguous
// low range, because validate() clears exactly that range before recomputing it.
static_assert (static_cast<uint32_t> (DiagnosticsField::energyRmsDbfs) == (1u << 7),
               "evidence field layout changed; update kEvidenceInvalidFieldMask");
static_assert (static_cast<uint32_t> (DiagnosticsField::clockBeatUnit) == (1u << 14),
               "evidence field layout changed; update kEvidenceInvalidFieldMask");
static_assert (static_cast<uint32_t> (DiagnosticsField::callbackLatency) == (1u << 15),
               "duration field layout changed; update kDurationInvalidFieldMask");

/** Bits that validate() RECOMPUTES from the raw evidence: observation, envelope
    and clock fields. validate() clears all of these first, so replacing an
    invalid value with a valid one clears the mark instead of leaving a stale
    mask. Because the clock bits are part of this set, a record whose clock is no
    longer known has its clock marks cleared as well. */
inline constexpr uint32_t kEvidenceInvalidFieldMask = (1u << 15) - 1u;

/** Bits OWNED by attachCallbackLatency()/attachProcessingDuration(). validate()
    must not clear these: they record that an explicit measurement was rejected,
    which is not derivable from the stored value (a rejected attachment stores an
    unmeasured field, which is indistinguishable from never having attached one). */
inline constexpr uint32_t kDurationInvalidFieldMask =
    static_cast<uint32_t> (DiagnosticsField::callbackLatency)
  | static_cast<uint32_t> (DiagnosticsField::processingDuration);

static_assert ((kEvidenceInvalidFieldMask & kDurationInvalidFieldMask) == 0u,
               "evidence and duration invalid-field masks must not overlap");

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

    /** Bit per invalid field; see DiagnosticsField. Populated by makeEvent() and
        attachClock(), and updated by the duration attachments. */
    uint32_t invalidFieldMask = 0;

    bool isInvalid (DiagnosticsField field) const noexcept
    {
        return (invalidFieldMask & static_cast<uint32_t> (field)) != 0u;
    }

    /** Number of DISTINCT invalid fields. Repeated invalid attachments to the
        same field count once, because validity is a property of the field. */
    std::size_t invalidFieldCount() const noexcept
    {
        std::size_t count = 0;
        for (uint32_t bits = invalidFieldMask; bits != 0u; bits &= bits - 1u)
            ++count;
        return count;
    }
};

/** Capture an analyzer envelope unaltered. Every optional field starts unmeasured
    and the input horizon is NOT copied into any availability field. */
DiagnosticsEvent makeEvent (const ObservationEnvelope& envelope) noexcept;

/** Recompute the invalid-field mask for the observation and clock evidence from
    the domain policy above. This is a true recomputation: the evidence bits are
    cleared first, so raw evidence that has since been replaced by a valid value
    no longer masks, and a record whose clock is no longer known loses its clock
    marks. The duration bits are left untouched, because they belong to the
    duration attachments. Attaches are automatic; call this directly after
    mutating raw evidence in place. */
void validate (DiagnosticsEvent& event) noexcept;

/** Attach a clock snapshot. Values outside the domain policy are kept raw and
    masked in the export. */
void attachClock (DiagnosticsEvent& event, const ClockSnapshot& clock) noexcept;

void attachRingOverruns (DiagnosticsEvent& event, uint64_t cumulativeOverruns) noexcept;
void attachObservationDrops (DiagnosticsEvent& event, uint64_t cumulativeDrops) noexcept;

/** Explicit, externally measured consumer receipt time only. Never derived from
    the input horizon. */
void attachLiveReceipt (DiagnosticsEvent& event, uint64_t receiptSampleTime) noexcept;

/** Rejects a non-finite or negative duration. On rejection the measurement is
    CLEARED, so no previously measured value survives, and the field's invalid
    bit is set. */
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

static_assert (std::is_trivially_copyable<MeasuredField<uint64_t>>::value,
               "MeasuredField must stay trivially copyable for the SPSC queue");

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

    /** Any thread. NOT publisher quiescence: a publish() already in flight can
        still enqueue after this returns. Stop and join the publisher first. */
    void setEnabled (bool enabled) noexcept;
    bool enabled() const noexcept;

    /** Reader only (one UI reader). Refreshes the latest-value telemetry in `vm`. */
    void readViewModel (DiagnosticsViewModel& vm) const noexcept;

    /** Relaxed multi-field sample. Each counter is exact. The set is not one atomic
        snapshot, and it must not be called concurrently with reset(). */
    DiagnosticsCounters counters() const noexcept;

    /** Lifecycle only: ALL roles must be quiescent, including counters() readers.
        The caller plays the consumer role while queued events are discarded.
        Returns the count discarded. Drop counts restart from zero, and the session
        increments so stale latest-value telemetry is ignored. */
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

/** Non-RT growable store. Consumer role only. Bounded by maxEvents. A collector
    constructed with maxEvents == 0 retains nothing and still accounts for every
    dropped event, which the export reports from its metadata. */
class DiagnosticsCollector
{
public:
    explicit DiagnosticsCollector (std::size_t maxEvents);

    /** Pops at most kTraceCapacity events. Records the trace drop count as of
        this drain. Events beyond maxEvents are counted, not stored. MAY allocate
        (vector growth) on the consumer thread. */
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

/** CSV export. Non-RT only.
    Grammar:
      - Metadata preamble: one line per key, each beginning with kCsvMetadataPrefix
        ("# "), then "key=value". The first line is the format marker
        "# jam-diagnostics-csv". The preamble ALWAYS carries schema_version,
        trace_capacity, events_retained, trace_dropped_at_drain and
        collector_dropped, so a collector that retained zero events still reports
        its losses.
      - Then one header line of column names, then one row per retained event.
        The header has kEventColumnCount + 2 columns: the event columns plus
        trace_dropped_at_drain and collector_dropped, repeated per row.
      - Rows are LF-separated. Missing, unknown or domain-invalid values are
        empty cells. Text fields use RFC 4180 quoting. Booleans are 1/0.
    A reader should ignore '#' lines and use the preamble as the summary. */
std::string toCsv (const DiagnosticsCollector& collector);

/** JSON export. Non-RT only. One document with the summary at the top level
    (schema_version, trace_capacity, events_retained, trace_dropped_at_drain,
    collector_dropped) and an events array whose keys match the CSV column names.
    Missing, unknown or domain-invalid values are null; booleans are true/false;
    strings are escaped. Unsigned 64-bit values are written as exact decimal
    integers: a JavaScript consumer must parse them as BigInt or string, because
    doubles lose precision above 2^53. */
std::string toJson (const DiagnosticsCollector& collector);

/** RFC 4180 field quoting. Quotes the field when it contains comma, quote or newline. */
std::string csvEscape (std::string_view text);

/** JSON string escaping for quotes, backslash and control characters (\u00XX). */
std::string jsonEscape (std::string_view text);

} // namespace jam::diagnostics