// Rhythm diagnostics and trace foundation. See Diagnostics.h for the thread roles,
// the domain policy, the lossless provenance policy and the export policy.

#include "Diagnostics.h"

#include <charconv>
#include <cstdio>

// Locale-independent floating-point formatting is a correctness requirement here:
// a comma decimal point would produce invalid JSON and a broken CSV row. The
// standard to_chars conversion never consults the C locale.
#if ! defined(__cpp_lib_to_chars) || (__cpp_lib_to_chars < 201611L)
#error "jam diagnostics export requires std::to_chars for floating point (locale independence)"
#endif

namespace jam::diagnostics
{

namespace
{

void setInvalid (DiagnosticsEvent& e, DiagnosticsField f, bool invalid = true) noexcept
{
    const uint32_t bit = static_cast<uint32_t> (f);
    if (invalid)
        e.invalidFieldMask |= bit;
    else
        e.invalidFieldMask &= ~bit;
}

/** Marks a field invalid if `ok` is false. Set-only by design: validate() clears
    the evidence range before it runs, so this stays a correct recomputation
    rather than an accumulation. */
void badField (bool ok, DiagnosticsEvent& e, DiagnosticsField f) noexcept
{
    if (! ok)
        setInvalid (e, f);
}

// One export value. Missing, and domain-invalid or non-finite reals, are rendered
// as an empty CSV cell and as JSON null, so the two formats cannot disagree
// about missingness.
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
    "observation_source_sample_rate_hz",
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
    "invalid_field_count",
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

    // Plain column.
    auto put = [&] (Cell c) { cells[n++] = c; };

    // Domain-validated column: the raw value is always what the record holds, and
    // an invalid field is masked to missing here, never overwritten.
    auto putField = [&] (Cell c, DiagnosticsField f) {
        if (e.isInvalid (f))
            cells[n] = missingCell();
        else
            cells[n] = c;
        ++n;
    };

    put (cellU (e.traceSession));
    put (cellU (env.sequence));
    put (cellU (env.streamGeneration));
    put (cellU (o.inputSampleTime));
    put (cellU (env.blockStartSampleTime));
    put (cellU (env.inputHorizonSampleTime));
    putField (cellD (env.sourceSampleRate), DiagnosticsField::envelopeRate);
    putField (cellD (o.sourceSampleRate), DiagnosticsField::observationRate);
    put (cellB (env.availabilityMeasured));
    putField (cellF (o.bpmCandidate), DiagnosticsField::candidateBpm);
    putField (cellF (o.beatConfidence01), DiagnosticsField::observationConfidence01);
    putField (cellF (o.beatPhase01), DiagnosticsField::observationBeatPhase01);
    putField (cellF (o.onsetStrength01), DiagnosticsField::onsetStrength01);
    putField (cellF (o.energyRmsDbfs), DiagnosticsField::energyRmsDbfs);
    putField (cellF (o.transientDensity01), DiagnosticsField::transientDensity01);
    put (cellB (o.beatEvent));
    put (cellB (o.silence));
    put (cellB (o.phaseValid));
    put (cellU (e.invalidFieldCount()));

    put (cellMeasuredU (e.liveReceiptSampleTime));
    put (cellB (e.liveReceiptSampleTime.measured));
    putField (cellMeasuredD (e.callbackLatencySeconds), DiagnosticsField::callbackLatency);
    put (cellB (e.callbackLatencySeconds.measured));
    putField (cellMeasuredD (e.processingDurationSeconds), DiagnosticsField::processingDuration);
    put (cellB (e.processingDurationSeconds.measured));
    put (cellMeasuredU (e.ringOverruns));
    put (cellB (e.ringOverruns.measured));
    put (cellMeasuredU (e.observationDrops));
    put (cellB (e.observationDrops.measured));

    put (cellB (e.clockKnown));
    if (e.clockKnown)
    {
        put (cellU (e.clock.generation));
        putField (cellD (e.clock.bpm), DiagnosticsField::clockBpm);
        putField (cellD (e.clock.beatPhase01), DiagnosticsField::clockBeatPhase01);
        putField (cellD (e.clock.barPhase01), DiagnosticsField::clockBarPhase01);
        putField (cellI (e.clock.beatInBar), DiagnosticsField::clockBeatInBar);
        putField (cellI (e.clock.beatsPerBar), DiagnosticsField::clockBeatsPerBar);
        putField (cellI (e.clock.beatUnit), DiagnosticsField::clockBeatUnit);
        putField (cellF (e.clock.confidence01), DiagnosticsField::clockConfidence01);
        put (cellText (toString (e.clock.lockState)));
        put (cellB (e.clock.tempoFrozen));
    }
    else
    {
        for (int k = 0; k < 10; ++k)
            put (missingCell());
    }
}

// Locale-independent numeric conversion. std::to_chars never consults the C
// locale, so the decimal point is always '.' and the output is valid JSON.
template <typename T>
bool appendNumber (std::string& out, T value)
{
    char buffer[48];
    const auto result = std::to_chars (buffer, buffer + sizeof buffer, value);
    if (result.ec != std::errc {})
        return false;
    out.append (buffer, static_cast<std::size_t> (result.ptr - buffer));
    return true;
}

void appendU64 (std::string& out, uint64_t v)
{
    appendNumber (out, v);
}

void appendI64 (std::string& out, int64_t v)
{
    appendNumber (out, v);
}

void appendReal32 (std::string& out, float v)
{
    appendNumber (out, v);
}

void appendReal64 (std::string& out, double v)
{
    appendNumber (out, v);
}

void appendMetadataLine (std::string& out, const char* key, uint64_t value)
{
    out += kCsvMetadataPrefix;
    out += key;
    out += '=';
    appendU64 (out, value);
    out += '\n';
}

void appendCsvCell (std::string& out, const Cell& c)
{
    switch (c.kind)
    {
        case Cell::Kind::missing:      break;
        case Cell::Kind::unsignedInt:  appendU64 (out, c.u); break;
        case Cell::Kind::signedInt:    appendI64 (out, c.i); break;
        case Cell::Kind::real32:
            if (std::isfinite (c.d)) appendReal32 (out, static_cast<float> (c.d));
            break;
        case Cell::Kind::real64:
            if (std::isfinite (c.d)) appendReal64 (out, c.d);
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
            if (std::isfinite (c.d)) appendReal32 (out, static_cast<float> (c.d));
            else out += "null";
            break;
        case Cell::Kind::real64:
            if (std::isfinite (c.d)) appendReal64 (out, c.d);
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
    validate (e);
    return e;
}

void validate (DiagnosticsEvent& event) noexcept
{
    // A recomputation, not an accumulation. Clearing the evidence bits up front is
    // what makes "valid replaces invalid" work: without it, badField could only
    // ever set a bit, so a rate corrected from -48000 back to 48000 (or a clock
    // re-attached with valid values) would keep masking a now-valid field.
    // Duration bits are excluded on purpose; see kDurationInvalidFieldMask.
    event.invalidFieldMask &= ~kEvidenceInvalidFieldMask;

    const RhythmObservation& o = event.envelope.observation;

    badField (isPositiveRate (o.sourceSampleRate), event, DiagnosticsField::observationRate);
    badField (isPositiveRate (event.envelope.sourceSampleRate), event, DiagnosticsField::envelopeRate);
    badField (isTempoValue (o.bpmCandidate), event, DiagnosticsField::candidateBpm);
    badField (isUnitInterval (o.beatConfidence01), event, DiagnosticsField::observationConfidence01);
    badField (isUnitInterval (o.beatPhase01), event, DiagnosticsField::observationBeatPhase01);
    badField (isUnitInterval (o.onsetStrength01), event, DiagnosticsField::onsetStrength01);
    badField (isUnitInterval (o.transientDensity01), event, DiagnosticsField::transientDensity01);
    badField (isDecibelFullScale (o.energyRmsDbfs), event, DiagnosticsField::energyRmsDbfs);

    if (event.clockKnown)
    {
        const ClockSnapshot& c = event.clock;
        badField (isTempoValue (c.bpm), event, DiagnosticsField::clockBpm);
        badField (isUnitInterval (c.beatPhase01), event, DiagnosticsField::clockBeatPhase01);
        badField (isUnitInterval (c.barPhase01), event, DiagnosticsField::clockBarPhase01);
        badField (isUnitInterval (c.confidence01), event, DiagnosticsField::clockConfidence01);
        badField (isPositiveMeter (c.beatsPerBar), event, DiagnosticsField::clockBeatsPerBar);
        badField (isPositiveMeter (c.beatUnit), event, DiagnosticsField::clockBeatUnit);
        // 0 means "not yet known" and is accepted as-is.
        badField (isKnownBeatInBar (c.beatInBar, c.beatsPerBar < 1 ? 1 : c.beatsPerBar),
                  event, DiagnosticsField::clockBeatInBar);
    }
}

void attachClock (DiagnosticsEvent& event, const ClockSnapshot& clock) noexcept
{
    event.clockKnown = true;
    event.clock = clock;
    validate (event);
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
    if (isNonNegativeSeconds (seconds))
    {
        event.callbackLatencySeconds = { seconds, true };
        setInvalid (event, DiagnosticsField::callbackLatency, false);
    }
    else
    {
        // Clear the measurement: a rejected value must never leave the previous
        // valid reading in place, which would look like an accepted measurement.
        event.callbackLatencySeconds = {};
        setInvalid (event, DiagnosticsField::callbackLatency);
    }
}

void attachProcessingDuration (DiagnosticsEvent& event, double seconds) noexcept
{
    if (isNonNegativeSeconds (seconds))
    {
        event.processingDurationSeconds = { seconds, true };
        setInvalid (event, DiagnosticsField::processingDuration, false);
    }
    else
    {
        event.processingDurationSeconds = {};
        setInvalid (event, DiagnosticsField::processingDuration);
    }
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
    // Quiescent by contract: publisher, consumer, UI reader and counters readers
    // must all be stopped, because dropBase_ and dropped_ are read independently.
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

    // Metadata preamble. Written before any row, and written even when nothing
    // was retained, so a zero-event export still reports its losses.
    out += kCsvMetadataPrefix;
    out += "jam-diagnostics-csv\n";
    appendMetadataLine (out, "schema_version", static_cast<uint64_t> (kSchemaVersion));
    appendMetadataLine (out, "trace_capacity", static_cast<uint64_t> (kTraceCapacity));
    appendMetadataLine (out, "events_retained", collector.events().size());
    appendMetadataLine (out, "trace_dropped_at_drain", collector.traceDroppedAtDrain());
    appendMetadataLine (out, "collector_dropped", collector.collectorDropped());

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