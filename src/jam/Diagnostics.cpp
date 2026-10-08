// Rhythm diagnostics and trace foundation. See Diagnostics.h for the thread roles,
// the lossless provenance policy and the export policy.

#include "Diagnostics.h"

#include <cmath>
#include <cstdio>

namespace jam::diagnostics
{

namespace
{

uint32_t bad (bool ok) noexcept
{
    return ok ? 0u : 1u;
}

bool positiveFinite (double v) noexcept
{
    return std::isfinite (v) && v > 0.0;
}

bool nonNegativeFinite (double v) noexcept
{
    return std::isfinite (v) && v >= 0.0;
}

// One export value. Missing, and non-finite reals, are rendered as an empty CSV
// cell and as JSON null, so the two formats cannot disagree about missingness.
struct Cell
{
    enum class Kind { missing, unsignedInt, signedInt, real32, real64, boolean, text };

    Kind kind = Kind::missing;
    uint64_t u = 0;
    int64_t i = 0;
    double d = 0.0;
    bool b = false;
    const char* t = "";
};

Cell missingCell() noexcept { return {}; }

Cell cellU (uint64_t v) noexcept
{
    Cell c; c.kind = Cell::Kind::unsignedInt; c.u = v; return c;
}

Cell cellI (int v) noexcept
{
    Cell c; c.kind = Cell::Kind::signedInt; c.i = v; return c;
}

Cell cellF (float v) noexcept
{
    Cell c; c.kind = Cell::Kind::real32; c.d = static_cast<double> (v); return c;
}

Cell cellD (double v) noexcept
{
    Cell c; c.kind = Cell::Kind::real64; c.d = v; return c;
}

Cell cellB (bool v) noexcept
{
    Cell c; c.kind = Cell::Kind::boolean; c.b = v; return c;
}

Cell cellText (const char* v) noexcept
{
    Cell c; c.kind = Cell::Kind::text; c.t = v; return c;
}

Cell cellMeasuredU (const MeasuredField<uint64_t>& f) noexcept
{
    return f.measured ? cellU (f.value) : missingCell();
}

Cell cellMeasuredD (const MeasuredField<double>& f) noexcept
{
    return f.measured ? cellD (f.value) : missingCell();
}

// Column order is the export contract. Keep it in step with fillCells().
constexpr const char* kColumnNames[kEventColumnCount] = {
    "session",
    "sequence",
    "stream_generation",
    "input_sample_time",
    "block_start_sample_time",
    "input_horizon_sample_time",
    "source_sample_rate_hz",
    "availability_measured",
    "candidate_bpm",
    "candidate_confidence01",
    "beat_phase01",
    "onset_strength01",
    "energy_rms_dbfs",
    "transient_density01",
    "beat_event",
    "silence",
    "phase_valid",
    "invalid_fields",
    "live_receipt_sample_time",
    "live_receipt_measured",
    "callback_latency_seconds",
    "callback_latency_measured",
    "processing_duration_seconds",
    "processing_duration_measured",
    "ring_overruns",
    "ring_overruns_measured",
    "observation_drops",
    "observation_drops_measured",
    "clock_known",
    "clock_generation",
    "clock_bpm",
    "clock_beat_phase01",
    "clock_bar_phase01",
    "clock_beat_in_bar",
    "clock_beats_per_bar",
    "clock_beat_unit",
    "clock_confidence01",
    "clock_lock_state",
    "clock_tempo_frozen",
};

void fillCells (const DiagnosticsEvent& e, Cell* cells) noexcept
{
    const ObservationEnvelope& env = e.envelope;
    const RhythmObservation& o = env.observation;
    std::size_t n = 0;
    auto put = [&] (Cell c) { cells[n++] = c; };

    put (cellU (e.traceSession));
    put (cellU (env.sequence));
    put (cellU (env.streamGeneration));
    put (cellU (o.inputSampleTime));
    put (cellU (env.blockStartSampleTime));
    put (cellU (env.inputHorizonSampleTime));
    put (cellD (env.sourceSampleRate));
    put (cellB (env.availabilityMeasured));
    put (cellF (o.bpmCandidate));
    put (cellF (o.beatConfidence01));
    put (cellF (o.beatPhase01));
    put (cellF (o.onsetStrength01));
    put (cellF (o.energyRmsDbfs));
    put (cellF (o.transientDensity01));
    put (cellB (o.beatEvent));
    put (cellB (o.silence));
    put (cellB (o.phaseValid));
    put (cellU (e.invalidFields));

    put (cellMeasuredU (e.liveReceiptSampleTime));
    put (cellB (e.liveReceiptSampleTime.measured));
    put (cellMeasuredD (e.callbackLatencySeconds));
    put (cellB (e.callbackLatencySeconds.measured));
    put (cellMeasuredD (e.processingDurationSeconds));
    put (cellB (e.processingDurationSeconds.measured));
    put (cellMeasuredU (e.ringOverruns));
    put (cellB (e.ringOverruns.measured));
    put (cellMeasuredU (e.observationDrops));
    put (cellB (e.observationDrops.measured));

    put (cellB (e.clockKnown));
    if (e.clockKnown)
    {
        put (cellU (e.clock.generation));
        put (cellD (e.clock.bpm));
        put (cellD (e.clock.beatPhase01));
        put (cellD (e.clock.barPhase01));
        put (cellI (e.clock.beatInBar));
        put (cellI (e.clock.beatsPerBar));
        put (cellI (e.clock.beatUnit));
        put (cellF (e.clock.confidence01));
        put (cellText (toString (e.clock.lockState)));
        put (cellB (e.clock.tempoFrozen));
    }
    else
    {
        for (int k = 0; k < 10; ++k)
            put (missingCell());
    }
}

void appendU64 (std::string& out, uint64_t v)
{
    char buf[32];
    const int n = std::snprintf (buf, sizeof buf, "%llu", static_cast<unsigned long long> (v));
    out.append (buf, static_cast<std::size_t> (n));
}

void appendI64 (std::string& out, int64_t v)
{
    char buf[32];
    const int n = std::snprintf (buf, sizeof buf, "%lld", static_cast<long long> (v));
    out.append (buf, static_cast<std::size_t> (n));
}

void appendReal (std::string& out, double v, int digits)
{
    char buf[40];
    const int n = std::snprintf (buf, sizeof buf, "%.*g", digits, v);
    out.append (buf, static_cast<std::size_t> (n));
}

void appendCsvCell (std::string& out, const Cell& c)
{
    switch (c.kind)
    {
        case Cell::Kind::missing:      break;
        case Cell::Kind::unsignedInt:  appendU64 (out, c.u); break;
        case Cell::Kind::signedInt:    appendI64 (out, c.i); break;
        case Cell::Kind::real32:
            if (std::isfinite (c.d)) appendReal (out, c.d, 9);
            break;
        case Cell::Kind::real64:
            if (std::isfinite (c.d)) appendReal (out, c.d, 17);
            break;
        case Cell::Kind::boolean:      out += c.b ? "1" : "0"; break;
        case Cell::Kind::text:         out += csvEscape (c.t); break;
    }
}

void appendJsonCell (std::string& out, const Cell& c)
{
    switch (c.kind)
    {
        case Cell::Kind::missing:      out += "null"; break;
        case Cell::Kind::unsignedInt:  appendU64 (out, c.u); break;
        case Cell::Kind::signedInt:    appendI64 (out, c.i); break;
        case Cell::Kind::real32:
            if (std::isfinite (c.d)) appendReal (out, c.d, 9);
            else out += "null";
            break;
        case Cell::Kind::real64:
            if (std::isfinite (c.d)) appendReal (out, c.d, 17);
            else out += "null";
            break;
        case Cell::Kind::boolean:      out += c.b ? "true" : "false"; break;
        case Cell::Kind::text:         out += '"'; out += jsonEscape (c.t); out += '"'; break;
    }
}

} // namespace

DiagnosticsEvent makeEvent (const ObservationEnvelope& envelope) noexcept
{
    DiagnosticsEvent e;
    e.envelope = envelope;

    const RhythmObservation& o = envelope.observation;
    e.invalidFields = bad (std::isfinite (o.bpmCandidate))
                    + bad (std::isfinite (o.beatPhase01))
                    + bad (std::isfinite (o.beatConfidence01))
                    + bad (std::isfinite (o.onsetStrength01))
                    + bad (std::isfinite (o.energyRmsDbfs))
                    + bad (std::isfinite (o.transientDensity01))
                    + bad (positiveFinite (o.sourceSampleRate))
                    + bad (positiveFinite (envelope.sourceSampleRate));
    return e;
}

void attachClock (DiagnosticsEvent& event, const ClockSnapshot& clock) noexcept
{
    event.clockKnown = true;
    event.clock = clock;
    event.invalidFields += bad (std::isfinite (clock.bpm))
                         + bad (std::isfinite (clock.beatPhase01))
                         + bad (std::isfinite (clock.barPhase01))
                         + bad (std::isfinite (clock.confidence01));
}

void attachRingOverruns (DiagnosticsEvent& event, uint64_t cumulativeOverruns) noexcept
{
    event.ringOverruns = { cumulativeOverruns, true };
}

void attachObservationDrops (DiagnosticsEvent& event, uint64_t cumulativeDrops) noexcept
{
    event.observationDrops = { cumulativeDrops, true };
}

void attachLiveReceipt (DiagnosticsEvent& event, uint64_t receiptSampleTime) noexcept
{
    event.liveReceiptSampleTime = { receiptSampleTime, true };
}

void attachCallbackLatency (DiagnosticsEvent& event, double seconds) noexcept
{
    if (nonNegativeFinite (seconds))
        event.callbackLatencySeconds = { seconds, true };
    else
        event.invalidFields += 1;
}

void attachProcessingDuration (DiagnosticsEvent& event, double seconds) noexcept
{
    if (nonNegativeFinite (seconds))
        event.processingDurationSeconds = { seconds, true };
    else
        event.invalidFields += 1;
}

DiagnosticsTrace::DiagnosticsTrace() noexcept = default;

bool DiagnosticsTrace::publish (const DiagnosticsEvent& event) noexcept
{
    if (! enabled_.load (std::memory_order_acquire))
    {
        // Single publisher, so a relaxed load/store is exact and avoids an RMW.
        skipped_.store (skipped_.load (std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
        return false;
    }

    DiagnosticsEvent stamped = event;
    stamped.traceSession = session_.load (std::memory_order_relaxed);

    latest_.publish (stamped);

    if (! queue_.push (stamped))
        return false;

    published_.store (published_.load (std::memory_order_relaxed) + 1,
                      std::memory_order_relaxed);
    return true;
}

bool DiagnosticsTrace::pop (DiagnosticsEvent& out) noexcept
{
    return queue_.pop (out);
}

void DiagnosticsTrace::setEnabled (bool enabled) noexcept
{
    enabled_.store (enabled, std::memory_order_release);
}

bool DiagnosticsTrace::enabled() const noexcept
{
    return enabled_.load (std::memory_order_acquire);
}

void DiagnosticsTrace::readViewModel (DiagnosticsViewModel& vm) const noexcept
{
    DiagnosticsEvent fresh;
    if (latest_.tryRead (fresh))
    {
        vm.latest = fresh;
        vm.latestSeen = true;
    }

    vm.counters = counters();
    vm.hasLatest = vm.latestSeen && vm.latest.traceSession == vm.counters.session;
    vm.clockLabel = (vm.hasLatest && vm.latest.clockKnown)
                        ? toString (vm.latest.clock.lockState)
                        : "Unknown";
}

DiagnosticsCounters DiagnosticsTrace::counters() const noexcept
{
    DiagnosticsCounters c;
    c.session = session_.load (std::memory_order_relaxed);
    c.tracingEnabled = enabled();
    c.publishedEvents = published_.load (std::memory_order_relaxed);
    c.droppedEvents = queue_.droppedCount() - dropBase_.load (std::memory_order_relaxed);
    c.skippedWhileDisabled = skipped_.load (std::memory_order_relaxed);
    c.discardedAtReset = discardedAtReset_.load (std::memory_order_relaxed);
    return c;
}

std::size_t DiagnosticsTrace::reset() noexcept
{
    // Quiescent by contract, so at most kTraceCapacity events can be queued.
    std::size_t discarded = 0;
    DiagnosticsEvent scratch;
    while (discarded < kTraceCapacity && queue_.pop (scratch))
        ++discarded;

    discardedAtReset_.store (discarded, std::memory_order_relaxed);
    published_.store (0, std::memory_order_relaxed);
    skipped_.store (0, std::memory_order_relaxed);
    dropBase_.store (queue_.droppedCount(), std::memory_order_relaxed);
    session_.store (session_.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    return discarded;
}

DiagnosticsCollector::DiagnosticsCollector (std::size_t maxEvents)
    : maxEvents_ (maxEvents)
{
}

std::size_t DiagnosticsCollector::drain (DiagnosticsTrace& trace)
{
    std::size_t popped = 0;
    DiagnosticsEvent event;
    while (popped < kTraceCapacity && trace.pop (event))
    {
        ++popped;
        if (events_.size() < maxEvents_)
            events_.push_back (event);
        else
            ++collectorDropped_;
    }

    traceDroppedAtDrain_ = trace.counters().droppedEvents;
    return popped;
}

void DiagnosticsCollector::clear() noexcept
{
    events_.clear();
    traceDroppedAtDrain_ = 0;
    collectorDropped_ = 0;
}

std::string toCsv (const DiagnosticsCollector& collector)
{
    std::string out;
    for (std::size_t j = 0; j < kEventColumnCount; ++j)
    {
        if (j > 0)
            out += ',';
        out += kColumnNames[j];
    }
    out += ",trace_dropped_at_drain,collector_dropped\n";

    Cell cells[kEventColumnCount];
    for (const DiagnosticsEvent& event : collector.events())
    {
        fillCells (event, cells);
        for (std::size_t j = 0; j < kEventColumnCount; ++j)
        {
            if (j > 0)
                out += ',';
            appendCsvCell (out, cells[j]);
        }
        out += ',';
        appendU64 (out, collector.traceDroppedAtDrain());
        out += ',';
        appendU64 (out, collector.collectorDropped());
        out += '\n';
    }
    return out;
}

std::string toJson (const DiagnosticsCollector& collector)
{
    std::string out = "{\"schema_version\":";
    appendI64 (out, kSchemaVersion);
    out += ",\"trace_capacity\":";
    appendU64 (out, kTraceCapacity);
    out += ",\"events_retained\":";
    appendU64 (out, collector.events().size());
    out += ",\"trace_dropped_at_drain\":";
    appendU64 (out, collector.traceDroppedAtDrain());
    out += ",\"collector_dropped\":";
    appendU64 (out, collector.collectorDropped());
    out += ",\"events\":[";

    Cell cells[kEventColumnCount];
    bool firstEvent = true;
    for (const DiagnosticsEvent& event : collector.events())
    {
        if (! firstEvent)
            out += ',';
        firstEvent = false;

        fillCells (event, cells);
        out += '{';
        for (std::size_t j = 0; j < kEventColumnCount; ++j)
        {
            if (j > 0)
                out += ',';
            out += '"';
            out += kColumnNames[j];
            out += "\":";
            appendJsonCell (out, cells[j]);
        }
        out += '}';
    }
    out += "]}";
    return out;
}

std::string csvEscape (std::string_view text)
{
    if (text.find_first_of (",\"\r\n") == std::string_view::npos)
        return std::string (text);

    std::string out = "\"";
    for (char ch : text)
    {
        if (ch == '"')
            out += '"';
        out += ch;
    }
    out += '"';
    return out;
}

std::string jsonEscape (std::string_view text)
{
    std::string out;
    out.reserve (text.size());
    for (char ch : text)
    {
        const auto byte = static_cast<unsigned char> (ch);
        switch (ch)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof buf, "\\u%04x", static_cast<unsigned> (byte));
                    out += buf;
                }
                else
                {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

} // namespace jam::diagnostics
