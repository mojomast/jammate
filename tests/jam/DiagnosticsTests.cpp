// Unit tests for jam::diagnostics (DEVPLAN DIAG-001, SPEC.md 22).
//
// What is asserted here is the contract, not the convenience:
//   - provenance is lossless, including uint64_t wrap;
//   - the input horizon is never presented as live availability;
//   - unmeasured values stay unmeasured (null / empty), never fabricated;
//   - tracing off never copies or enqueues;
//   - the ordered trace queue drops the INCOMING event and counts it;
//   - the latest-value UI path never stands in for trace history;
//   - CSV and JSON exports parse back to the same values and never emit NaN/Inf;
//   - lifecycle reset isolates sessions;
//   - publish() performs no heap allocation (glibc mallinfo2 probe, self-checked).
//
// Policy: no file I/O, no sleeping, no wall-clock. Threads are used only for the
// SPSC and latest-value concurrency tests, and their loops are bounded.

#include "JamTest.h"

#include "jam/Diagnostics.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// The allocation probe reads glibc's allocator. Sanitizers replace malloc, so the
// probe is meaningless under them and is skipped (the TSAN/ASAN builds still run
// every other test).
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)) \
    && ! defined(__SANITIZE_THREAD__) && ! defined(__SANITIZE_ADDRESS__)
#include <malloc.h>
#define DIAG_TEST_HAVE_MALLINFO2 1
#endif

using namespace jam;
using namespace jam::diagnostics;

namespace
{

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

ObservationEnvelope makeEnvelope (uint64_t sequence, uint64_t eventTime, uint64_t blockStart,
                                  uint64_t horizon, uint64_t generation)
{
    ObservationEnvelope env;
    env.observation.inputSampleTime = eventTime;
    env.observation.sourceSampleRate = 48000.0;
    env.observation.bpmCandidate = 120.5f;
    env.observation.beatPhase01 = 0.25f;
    env.observation.beatConfidence01 = 0.75f;
    env.observation.onsetStrength01 = 0.5f;
    env.observation.energyRmsDbfs = -18.25f;
    env.observation.transientDensity01 = 0.125f;
    env.observation.beatEvent = true;
    env.observation.silence = false;
    env.observation.phaseValid = true;
    env.streamGeneration = generation;
    env.blockStartSampleTime = blockStart;
    env.inputHorizonSampleTime = horizon;
    env.sourceSampleRate = 48000.0;
    env.sequence = sequence;
    env.availabilityMeasured = false;
    return env;
}

ClockSnapshot makeClock()
{
    ClockSnapshot c;
    c.generation = 3;
    c.bpm = 118.0;
    c.beatPhase01 = 0.5;
    c.barPhase01 = 0.125;
    c.beatInBar = 2;
    c.beatsPerBar = 4;
    c.beatUnit = 4;
    c.confidence01 = 0.875f;
    c.lockState = ClockLockState::Locked;
    c.tempoFrozen = false;
    return c;
}

// ---------------------------------------------------------------------------
// Minimal CSV parser (RFC 4180 quoting), used to read back the exporter.
std::vector<std::vector<std::string>> parseCsv (const std::string& text)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool haveField = false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (quoted)
        {
            if (ch == '"')
            {
                if (i + 1 < text.size() && text[i + 1] == '"')
                {
                    field += '"';
                    ++i;
                }
                else
                {
                    quoted = false;
                }
            }
            else
            {
                field += ch;
            }
            continue;
        }

        if (ch == '"')
        {
            quoted = true;
            haveField = true;
        }
        else if (ch == ',')
        {
            row.push_back (field);
            field.clear();
            haveField = true;
        }
        else if (ch == '\n')
        {
            row.push_back (field);
            field.clear();
            rows.push_back (row);
            row.clear();
            haveField = false;
        }
        else
        {
            field += ch;
            haveField = true;
        }
    }

    if (haveField || ! row.empty())
    {
        row.push_back (field);
        rows.push_back (row);
    }
    return rows;
}

// ---------------------------------------------------------------------------
// Strict JSON parser. It rejects NaN, Infinity, trailing commas and bad escapes,
// so it proves the exporter emits valid JSON.
struct Json
{
    enum class Kind { null, boolean, number, string, array, object };

    Kind kind = Kind::null;
    bool b = false;
    std::string text;               // number: raw token, string: decoded value
    std::vector<Json> items;        // array elements, or object values
    std::vector<std::string> keys;  // object keys, parallel to items

    const Json* get (const std::string& key) const
    {
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return &items[i];
        return nullptr;
    }
};

class JsonParser
{
public:
    explicit JsonParser (const std::string& s) : s_ (s) {}

    bool parse (Json& out)
    {
        skipWs();
        if (! value (out))
            return false;
        skipWs();
        return pos_ == s_.size();
    }

private:
    void skipWs()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\t' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool literal (const char* word)
    {
        const std::size_t n = std::strlen (word);
        if (s_.compare (pos_, n, word) != 0)
            return false;
        pos_ += n;
        return true;
    }

    bool value (Json& out)
    {
        if (pos_ >= s_.size())
            return false;
        const char c = s_[pos_];
        if (c == '{') return object (out);
        if (c == '[') return array (out);
        if (c == '"') { out.kind = Json::Kind::string; return string (out.text); }
        if (c == 't') { out.kind = Json::Kind::boolean; out.b = true; return literal ("true"); }
        if (c == 'f') { out.kind = Json::Kind::boolean; out.b = false; return literal ("false"); }
        if (c == 'n') { out.kind = Json::Kind::null; return literal ("null"); }
        return number (out);
    }

    bool number (Json& out)
    {
        const std::size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-')
            ++pos_;
        if (pos_ >= s_.size() || ! std::isdigit (static_cast<unsigned char> (s_[pos_])))
            return false;
        while (pos_ < s_.size() && std::isdigit (static_cast<unsigned char> (s_[pos_])))
            ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.')
        {
            ++pos_;
            if (pos_ >= s_.size() || ! std::isdigit (static_cast<unsigned char> (s_[pos_])))
                return false;
            while (pos_ < s_.size() && std::isdigit (static_cast<unsigned char> (s_[pos_])))
                ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-'))
                ++pos_;
            if (pos_ >= s_.size() || ! std::isdigit (static_cast<unsigned char> (s_[pos_])))
                return false;
            while (pos_ < s_.size() && std::isdigit (static_cast<unsigned char> (s_[pos_])))
                ++pos_;
        }
        out.kind = Json::Kind::number;
        out.text = s_.substr (start, pos_ - start);
        return true;
    }

    bool string (std::string& out)
    {
        if (s_[pos_] != '"')
            return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size())
        {
            const char c = s_[pos_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char> (c) < 0x20)
                return false;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= s_.size())
                return false;
            const char e = s_[pos_++];
            switch (e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    if (pos_ + 4 > s_.size())
                        return false;
                    const unsigned code = static_cast<unsigned> (std::strtoul (s_.substr (pos_, 4).c_str(), nullptr, 16));
                    pos_ += 4;
                    out += static_cast<char> (code);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool array (Json& out)
    {
        out.kind = Json::Kind::array;
        ++pos_;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']')
        {
            ++pos_;
            return true;
        }
        while (true)
        {
            skipWs();
            Json element;
            if (! value (element))
                return false;
            out.items.push_back (element);
            skipWs();
            if (pos_ >= s_.size())
                return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }

    bool object (Json& out)
    {
        out.kind = Json::Kind::object;
        ++pos_;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}')
        {
            ++pos_;
            return true;
        }
        while (true)
        {
            skipWs();
            std::string key;
            if (pos_ >= s_.size() || ! string (key))
                return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':')
                return false;
            ++pos_;
            skipWs();
            Json member;
            if (! value (member))
                return false;
            out.keys.push_back (key);
            out.items.push_back (member);
            skipWs();
            if (pos_ >= s_.size())
                return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }

    const std::string& s_;
    std::size_t pos_ = 0;
};

std::string lowerCase (std::string s)
{
    for (char& ch : s)
        ch = static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
    return s;
}

// Helper: drain a trace into a collector and return its events.
std::vector<DiagnosticsEvent> drainAll (DiagnosticsTrace& trace)
{
    DiagnosticsCollector collector (kTraceCapacity * 4);
    collector.drain (trace);
    return collector.events();
}

} // namespace

// ---------------------------------------------------------------------------

JAM_TEST(Diagnostics, provenanceLosslessNearWrap)
{
    // Block starts 5 samples before the uint64 wrap and runs 2048 samples, so the
    // horizon wraps to a small value. The event sits just before the wrap.
    const uint64_t blockStart = kMax - 5;
    const uint64_t horizon = blockStart + 2048;
    const uint64_t eventTime = kMax - 1;
    REQUIRE (horizon < blockStart);

    ObservationEnvelope env = makeEnvelope (7, eventTime, blockStart, horizon, 9);
    REQUIRE (env.observationWithinInputHorizon());

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    CHECK (trace.publish (makeEvent (env)));

    DiagnosticsEvent out;
    REQUIRE (trace.pop (out));
    CHECK_EQ (out.envelope.observation.inputSampleTime, eventTime);
    CHECK_EQ (out.envelope.blockStartSampleTime, blockStart);
    CHECK_EQ (out.envelope.inputHorizonSampleTime, horizon);
    CHECK_EQ (out.envelope.streamGeneration, uint64_t { 9 });
    CHECK_EQ (out.envelope.sequence, uint64_t { 7 });
    CHECK (out.envelope.observationWithinInputHorizon());

    DiagnosticsCollector collector (16);
    DiagnosticsEvent again = out;
    trace.setEnabled (true);
    CHECK (trace.publish (again));
    collector.drain (trace);
    REQUIRE (collector.events().size() == 1);

    const std::string csv = toCsv (collector);
    CHECK (csv.find ("18446744073709551614") != std::string::npos);
    CHECK (csv.find (std::to_string (horizon)) != std::string::npos);

    const std::string json = toJson (collector);
    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    const Json* events = doc.get ("events");
    REQUIRE (events != nullptr);
    REQUIRE (events->items.size() == 1);
    CHECK_EQ (events->items[0].get ("input_sample_time")->text, std::string ("18446744073709551614"));
    CHECK_EQ (events->items[0].get ("input_horizon_sample_time")->text, std::to_string (horizon));
}

JAM_TEST(Diagnostics, horizonIsNotLiveAvailability)
{
    ObservationEnvelope env = makeEnvelope (1, 1000, 1000, 3048, 1);
    DiagnosticsEvent e = makeEvent (env);

    CHECK (! e.liveReceiptSampleTime.measured);
    CHECK_EQ (e.envelope.inputHorizonSampleTime, uint64_t { 3048 });

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    DiagnosticsCollector collector (8);
    collector.drain (trace);

    const std::string json = toJson (collector);
    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    const Json& row = doc.get ("events")->items[0];
    CHECK ((row.get ("live_receipt_sample_time"))->kind == Json::Kind::null);
    CHECK_EQ (row.get ("live_receipt_measured")->b, false);
    CHECK_EQ (row.get ("input_horizon_sample_time")->text, std::string ("3048"));

    // Only an explicitly measured receipt may populate availability.
    attachLiveReceipt (e, 4096);
    CHECK (e.liveReceiptSampleTime.measured);
    CHECK_EQ (e.liveReceiptSampleTime.value, uint64_t { 4096 });
    CHECK_EQ (e.envelope.inputHorizonSampleTime, uint64_t { 3048 });
    CHECK (! e.envelope.availabilityMeasured);
}

JAM_TEST(Diagnostics, unmeasuredFieldsAreNullNotFabricated)
{
    DiagnosticsEvent e = makeEvent (makeEnvelope (2, 10, 10, 522, 0));

    CHECK (! e.callbackLatencySeconds.measured);
    CHECK (! e.processingDurationSeconds.measured);
    CHECK (! e.ringOverruns.measured);
    CHECK (! e.observationDrops.measured);
    CHECK (! e.clockKnown);

    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const std::string csv = toCsv (collector);
    const auto rows = parseCsv (csv);
    REQUIRE (rows.size() == 2);
    const auto& header = rows[0];
    const auto& data = rows[1];
    REQUIRE (header.size() == data.size());

    auto cell = [&] (const char* name) {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == name)
                return data[i];
        return std::string ("<missing column>");
    };
    CHECK_EQ (cell ("callback_latency_seconds"), std::string (""));
    CHECK_EQ (cell ("callback_latency_measured"), std::string ("0"));
    CHECK_EQ (cell ("processing_duration_seconds"), std::string (""));
    CHECK_EQ (cell ("ring_overruns"), std::string (""));
    CHECK_EQ (cell ("observation_drops"), std::string (""));
    CHECK_EQ (cell ("clock_bpm"), std::string (""));
    CHECK_EQ (cell ("clock_lock_state"), std::string (""));
    CHECK_EQ (cell ("clock_known"), std::string ("0"));

    const std::string json = toJson (collector);
    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    const Json& row = doc.get ("events")->items[0];
    CHECK ((row.get ("callback_latency_seconds"))->kind == Json::Kind::null);
    CHECK ((row.get ("processing_duration_seconds"))->kind == Json::Kind::null);
    CHECK ((row.get ("clock_bpm"))->kind == Json::Kind::null);
    CHECK ((row.get ("clock_lock_state"))->kind == Json::Kind::null);
}

JAM_TEST(Diagnostics, measuredFieldsRoundTripWhenAttached)
{
    DiagnosticsEvent e = makeEvent (makeEnvelope (3, 50, 50, 562, 2));
    attachClock (e, makeClock());
    attachRingOverruns (e, 7);
    attachObservationDrops (e, 3);
    attachProcessingDuration (e, 0.00025);
    attachCallbackLatency (e, 0.0125);

    CHECK (e.clockKnown);
    CHECK (e.ringOverruns.measured);
    CHECK_EQ (e.ringOverruns.value, uint64_t { 7 });
    CHECK (e.observationDrops.measured);
    CHECK_EQ (e.observationDrops.value, uint64_t { 3 });
    CHECK (e.processingDurationSeconds.measured);
    CHECK_NEAR (e.processingDurationSeconds.value, 0.00025, 1e-12);
    CHECK (e.callbackLatencySeconds.measured);
    CHECK_NEAR (e.callbackLatencySeconds.value, 0.0125, 1e-12);
    CHECK_EQ (e.invalidFields, uint32_t { 0 });

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    DiagnosticsCollector collector (4);
    collector.drain (trace);
    const auto rows = parseCsv (toCsv (collector));
    REQUIRE (rows.size() == 2);
    const auto& header = rows[0];
    const auto& data = rows[1];
    auto cell = [&] (const char* name) {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == name)
                return data[i];
        return std::string ("<missing column>");
    };
    CHECK_EQ (cell ("ring_overruns"), std::string ("7"));
    CHECK_EQ (cell ("ring_overruns_measured"), std::string ("1"));
    CHECK_EQ (cell ("observation_drops"), std::string ("3"));
    CHECK_EQ (cell ("clock_lock_state"), std::string ("Locked"));
    CHECK_EQ (cell ("clock_known"), std::string ("1"));
    CHECK_EQ (cell ("clock_beat_in_bar"), std::string ("2"));
    CHECK_EQ (cell ("clock_tempo_frozen"), std::string ("0"));
    CHECK_EQ (cell ("trace_dropped_at_drain"), std::string ("0"));
    CHECK_EQ (cell ("collector_dropped"), std::string ("0"));
}

JAM_TEST(Diagnostics, nonFiniteInputsCountedAndSerialisedAsNull)
{
    ObservationEnvelope env = makeEnvelope (4, 100, 100, 612, 0);
    env.observation.bpmCandidate = std::numeric_limits<float>::quiet_NaN();
    env.observation.energyRmsDbfs = std::numeric_limits<float>::infinity();
    DiagnosticsEvent e = makeEvent (env);
    CHECK_EQ (e.invalidFields, uint32_t { 2 });

    ClockSnapshot clock = makeClock();
    clock.bpm = std::numeric_limits<double>::quiet_NaN();
    attachClock (e, clock);
    CHECK_EQ (e.invalidFields, uint32_t { 3 });

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    DiagnosticsCollector collector (4);
    collector.drain (trace);

    const std::string json = toJson (collector);
    const std::string lower = lowerCase (json);
    CHECK (lower.find ("nan") == std::string::npos);
    CHECK (lower.find ("inf") == std::string::npos);

    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    const Json& row = doc.get ("events")->items[0];
    CHECK ((row.get ("candidate_bpm"))->kind == Json::Kind::null);
    CHECK ((row.get ("energy_rms_dbfs"))->kind == Json::Kind::null);
    CHECK ((row.get ("clock_bpm"))->kind == Json::Kind::null);
    CHECK_EQ (row.get ("invalid_fields")->text, std::string ("3"));
    CHECK ((row.get ("candidate_confidence01"))->kind == Json::Kind::number);

    const auto rows = parseCsv (toCsv (collector));
    REQUIRE (rows.size() == 2);
    const auto& header = rows[0];
    for (std::size_t i = 0; i < header.size(); ++i)
        if (header[i] == "candidate_bpm")
            CHECK_EQ (rows[1][i], std::string (""));
    CHECK (lowerCase (toCsv (collector)).find ("nan") == std::string::npos);
}

JAM_TEST(Diagnostics, invalidMeasurementsAreRejectedNotStored)
{
    DiagnosticsEvent e = makeEvent (makeEnvelope (5, 0, 0, 512, 0));
    const uint32_t before = e.invalidFields;

    attachProcessingDuration (e, std::numeric_limits<double>::quiet_NaN());
    CHECK (! e.processingDurationSeconds.measured);
    CHECK_EQ (e.invalidFields, before + 1);

    attachCallbackLatency (e, -0.001);
    CHECK (! e.callbackLatencySeconds.measured);
    CHECK_EQ (e.invalidFields, before + 2);

    attachCallbackLatency (e, std::numeric_limits<double>::infinity());
    CHECK (! e.callbackLatencySeconds.measured);
    CHECK_EQ (e.invalidFields, before + 3);

    attachProcessingDuration (e, 0.0);
    CHECK (e.processingDurationSeconds.measured);
    CHECK_EQ (e.invalidFields, before + 3);
}

JAM_TEST(Diagnostics, disabledTraceNeverEnqueues)
{
    DiagnosticsTrace trace;
    CHECK (! trace.enabled());

    const DiagnosticsEvent e = makeEvent (makeEnvelope (1, 1, 1, 513, 0));
    for (int i = 0; i < 10; ++i)
        CHECK (! trace.publish (e));

    const DiagnosticsCounters c = trace.counters();
    CHECK_EQ (c.publishedEvents, uint64_t { 0 });
    CHECK_EQ (c.droppedEvents, uint64_t { 0 });
    CHECK_EQ (c.skippedWhileDisabled, uint64_t { 10 });
    CHECK (! c.tracingEnabled);

    DiagnosticsEvent out;
    CHECK (! trace.pop (out));

    DiagnosticsViewModel vm;
    trace.readViewModel (vm);
    CHECK (! vm.hasLatest);
    CHECK (! vm.latestSeen);
    CHECK_EQ (vm.clockLabel, std::string ("Unknown"));
}

JAM_TEST(Diagnostics, queuePressureDropsIncomingInOrder)
{
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    const uint64_t total = kTraceCapacity + 44;

    for (uint64_t k = 1; k <= total; ++k)
    {
        const bool accepted = trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));
        CHECK_EQ (accepted, k <= kTraceCapacity);
    }

    const DiagnosticsCounters c = trace.counters();
    CHECK_EQ (c.publishedEvents, static_cast<uint64_t> (kTraceCapacity));
    CHECK_EQ (c.droppedEvents, uint64_t { 44 });

    // Already queued events are intact and ordered. Incoming events were dropped.
    for (uint64_t k = 1; k <= kTraceCapacity; ++k)
    {
        DiagnosticsEvent out;
        REQUIRE (trace.pop (out));
        CHECK_EQ (out.envelope.sequence, k);
    }
    DiagnosticsEvent tail;
    CHECK (! trace.pop (tail));
}

JAM_TEST(Diagnostics, latestValueIsTelemetryNotHistory)
{
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    for (uint64_t k = 1; k <= 3; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k * 10, k * 10, k * 10 + 512, 0)));

    DiagnosticsViewModel vm;
    trace.readViewModel (vm);
    REQUIRE (vm.hasLatest);
    CHECK_EQ (vm.latest.envelope.sequence, uint64_t { 3 });
    CHECK_EQ (vm.counters.publishedEvents, uint64_t { 3 });

    // The UI reader saw only the newest value, but the trace keeps all three in order.
    for (uint64_t k = 1; k <= 3; ++k)
    {
        DiagnosticsEvent out;
        REQUIRE (trace.pop (out));
        CHECK_EQ (out.envelope.sequence, k);
    }

    // A second read with no new publication keeps the previous valid value.
    DiagnosticsViewModel again = vm;
    trace.readViewModel (again);
    CHECK (again.hasLatest);
    CHECK_EQ (again.latest.envelope.sequence, uint64_t { 3 });
    CHECK (vm.latestSeen);
}

JAM_TEST(Diagnostics, latestValueAndQueueStayConsistentLoosely)
{
    // A latest-value read is either the newest whole event or none, never a mix,
    // and the sequence it returns never goes backwards.
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    std::atomic<bool> done { false };
    constexpr uint64_t kEvents = 20000;

    std::thread producer ([&] {
        for (uint64_t k = 1; k <= kEvents; ++k)
        {
            DiagnosticsEvent e = makeEvent (makeEnvelope (k, k * 4096, k * 4096 + 7, k * 4096 + 2055, k));
            trace.publish (e);
        }
        done.store (true, std::memory_order_release);
    });

    DiagnosticsViewModel vm;
    uint64_t lastSeq = 0;
    bool torn = false;
    bool regressed = false;
    std::size_t reads = 0;
    while (! done.load (std::memory_order_acquire) && reads < 2000000)
    {
        trace.readViewModel (vm);
        ++reads;
        if (! vm.latestSeen)
            continue;
        const uint64_t seq = vm.latest.envelope.sequence;
        if (vm.latest.envelope.observation.inputSampleTime != seq * 4096)
            torn = true;
        if (vm.latest.envelope.blockStartSampleTime != seq * 4096 + 7)
            torn = true;
        if (seq < lastSeq)
            regressed = true;
        lastSeq = seq;
    }
    producer.join();

    // Final read after the producer finished.
    trace.readViewModel (vm);
    if (vm.latestSeen)
    {
        CHECK_EQ (vm.latest.envelope.sequence, kEvents);
    }
    CHECK (! torn);
    CHECK (! regressed);
}

JAM_TEST(Diagnostics, collectorBoundedAndCountsEveryLoss)
{
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    for (uint64_t k = 1; k <= 20; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));

    DiagnosticsCollector collector (10);
    const std::size_t popped = collector.drain (trace);
    CHECK_EQ (popped, std::size_t { 20 });
    CHECK_EQ (collector.events().size(), std::size_t { 10 });
    CHECK_EQ (collector.collectorDropped(), uint64_t { 10 });
    CHECK_EQ (collector.events()[0].envelope.sequence, uint64_t { 1 });
    CHECK_EQ (collector.events()[9].envelope.sequence, uint64_t { 10 });

    // Overflow the trace queue, then drain: the trace drop count is reported.
    for (uint64_t k = 1; k <= kTraceCapacity + 5; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));
    collector.drain (trace);
    CHECK_EQ (collector.traceDroppedAtDrain(), uint64_t { 5 });

    const std::string json = toJson (collector);
    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    CHECK_EQ (doc.get ("trace_dropped_at_drain")->text, std::string ("5"));
    CHECK_EQ (doc.get ("collector_dropped")->text, std::to_string (collector.collectorDropped()));
    CHECK_EQ (doc.get ("events_retained")->text, std::string ("10"));
}

JAM_TEST(Diagnostics, resetIsolatesSessions)
{
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    DiagnosticsCollector collector (16);

    for (uint64_t k = 1; k <= 5; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));
    const uint64_t firstSession = trace.counters().session;

    // Lifecycle: the consumer role discards queued events during reset().
    const std::size_t discarded = trace.reset();
    collector.clear();
    CHECK_EQ (discarded, std::size_t { 5 });

    const DiagnosticsCounters c = trace.counters();
    CHECK_EQ (c.discardedAtReset, uint64_t { 5 });
    CHECK_EQ (c.publishedEvents, uint64_t { 0 });
    CHECK_EQ (c.droppedEvents, uint64_t { 0 });
    CHECK_EQ (c.skippedWhileDisabled, uint64_t { 0 });
    CHECK (c.session > firstSession);
    CHECK (c.tracingEnabled);

    // Latest telemetry from the old session must not be presented as current.
    DiagnosticsViewModel vm;
    trace.readViewModel (vm);
    CHECK (! vm.hasLatest);

    trace.publish (makeEvent (makeEnvelope (100, 100, 100, 612, 1)));
    trace.readViewModel (vm);
    REQUIRE (vm.hasLatest);
    CHECK_EQ (vm.latest.envelope.sequence, uint64_t { 100 });
    CHECK_EQ (vm.latest.traceSession, vm.counters.session);

    collector.drain (trace);
    REQUIRE (collector.events().size() == 1);
    CHECK_EQ (collector.events()[0].envelope.sequence, uint64_t { 100 });
}

JAM_TEST(Diagnostics, resetRestartsDropAccounting)
{
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    for (uint64_t k = 1; k <= kTraceCapacity + 10; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));
    CHECK_EQ (trace.counters().droppedEvents, uint64_t { 10 });

    trace.reset();
    CHECK_EQ (trace.counters().droppedEvents, uint64_t { 0 });
    trace.publish (makeEvent (makeEnvelope (1, 1, 1, 513, 0)));
    CHECK_EQ (trace.counters().droppedEvents, uint64_t { 0 });
    CHECK_EQ (trace.counters().publishedEvents, uint64_t { 1 });
}

JAM_TEST(Diagnostics, stopKeepsQueuedEventsForFinalDrain)
{
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    for (uint64_t k = 1; k <= 3; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));

    trace.setEnabled (false);
    trace.publish (makeEvent (makeEnvelope (4, 4, 4, 516, 0)));
    trace.publish (makeEvent (makeEnvelope (5, 5, 5, 517, 0)));

    CHECK_EQ (trace.counters().skippedWhileDisabled, uint64_t { 2 });
    CHECK_EQ (trace.counters().publishedEvents, uint64_t { 3 });

    const auto events = drainAll (trace);
    REQUIRE (events.size() == 3);
    CHECK_EQ (events[0].envelope.sequence, uint64_t { 1 });
    CHECK_EQ (events[2].envelope.sequence, uint64_t { 3 });
}

JAM_TEST(Diagnostics, csvEscapingAndRoundTrip)
{
    CHECK_EQ (csvEscape ("plain"), std::string ("plain"));
    CHECK_EQ (csvEscape ("a,b"), std::string ("\"a,b\""));
    CHECK_EQ (csvEscape ("say \"hi\""), std::string ("\"say \"\"hi\"\"\""));
    CHECK_EQ (csvEscape ("line\nbreak"), std::string ("\"line\nbreak\""));

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    DiagnosticsEvent withClock = makeEvent (makeEnvelope (1, 1000, 1000, 2048, 4));
    attachClock (withClock, makeClock());
    trace.publish (withClock);
    trace.publish (makeEvent (makeEnvelope (2, 2000, 2000, 3048, 4)));

    DiagnosticsCollector collector (8);
    collector.drain (trace);
    const auto rows = parseCsv (toCsv (collector));
    REQUIRE (rows.size() == 3);

    const auto& header = rows[0];
    CHECK_EQ (header.size(), kEventColumnCount + 2);
    CHECK_EQ (header.back(), std::string ("collector_dropped"));
    for (std::size_t r = 1; r < rows.size(); ++r)
        CHECK_EQ (rows[r].size(), header.size());

    auto col = [&] (std::size_t row, const char* name) {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == name)
                return rows[row][i];
        return std::string ("<missing column>");
    };
    CHECK_EQ (col (1, "sequence"), std::string ("1"));
    CHECK_EQ (col (1, "clock_lock_state"), std::string ("Locked"));
    CHECK_EQ (col (1, "clock_bpm"), std::string ("118"));
    CHECK_EQ (col (2, "sequence"), std::string ("2"));
    CHECK_EQ (col (2, "clock_known"), std::string ("0"));
    CHECK_EQ (col (2, "clock_lock_state"), std::string (""));
    CHECK_EQ (col (2, "input_horizon_sample_time"), std::string ("3048"));
}

JAM_TEST(Diagnostics, jsonParsesAndEscapesStrings)
{
    CHECK_EQ (jsonEscape ("a\"b\\c"), std::string ("a\\\"b\\\\c"));
    CHECK_EQ (jsonEscape ("x\ny\x01"), std::string ("x\\ny\\u0001"));

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    DiagnosticsEvent e = makeEvent (makeEnvelope (1, 5, 5, 517, 0));
    attachClock (e, makeClock());
    trace.publish (e);

    DiagnosticsCollector collector (4);
    collector.drain (trace);
    const std::string json = toJson (collector);

    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    CHECK_EQ (doc.get ("schema_version")->text, std::string ("1"));
    CHECK_EQ (doc.get ("trace_capacity")->text, std::to_string (kTraceCapacity));
    const Json* events = doc.get ("events");
    REQUIRE (events != nullptr);
    REQUIRE (events->items.size() == 1);
    const Json& row = events->items[0];
    CHECK_EQ (row.get ("clock_lock_state")->text, std::string ("Locked"));
    CHECK_EQ (row.get ("clock_known")->b, true);
    CHECK_EQ (row.get ("availability_measured")->b, false);
    CHECK_EQ (row.get ("beat_event")->b, true);
    CHECK_EQ (row.get ("silence")->b, false);
}

JAM_TEST(Diagnostics, jsonEmptyCollectorIsValid)
{
    DiagnosticsCollector collector (4);
    const std::string json = toJson (collector);
    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    CHECK_EQ (doc.get ("events")->items.size(), std::size_t { 0 });
    CHECK_EQ (doc.get ("events_retained")->text, std::string ("0"));

    const auto rows = parseCsv (toCsv (collector));
    REQUIRE (rows.size() == 1);
    CHECK_EQ (rows[0].size(), kEventColumnCount + 2);
}

JAM_TEST(Diagnostics, twoThreadSpscDeliveryIsOrderedAndAccounted)
{
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    constexpr uint64_t kAttempts = 50000;
    std::atomic<bool> done { false };

    std::thread producer ([&] {
        for (uint64_t k = 1; k <= kAttempts; ++k)
            trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));
        done.store (true, std::memory_order_release);
    });

    DiagnosticsCollector collector (kAttempts);
    DiagnosticsEvent out;
    uint64_t lastSeq = 0;
    bool ordered = true;
    uint64_t delivered = 0;
    while (true)
    {
        if (trace.pop (out))
        {
            if (out.envelope.sequence <= lastSeq)
                ordered = false;
            lastSeq = out.envelope.sequence;
            ++delivered;
            continue;
        }
        if (done.load (std::memory_order_acquire))
        {
            if (! trace.pop (out))
                break;
            if (out.envelope.sequence <= lastSeq)
                ordered = false;
            lastSeq = out.envelope.sequence;
            ++delivered;
        }
    }
    producer.join();

    const DiagnosticsCounters c = trace.counters();
    CHECK (ordered);
    CHECK_EQ (delivered, c.publishedEvents);
    CHECK_EQ (c.publishedEvents + c.droppedEvents, kAttempts);
    CHECK (delivered > 0);
}

JAM_TEST(Diagnostics, publishDoesNotAllocate)
{
#if defined(DIAG_TEST_HAVE_MALLINFO2)
    auto inUse = [] () { return static_cast<uint64_t> (mallinfo2().uordblks); };

    // Probe self-check: an allocation must be visible, or the zero-delta result
    // below means nothing.
    {
        const uint64_t before = inUse();
        std::unique_ptr<std::vector<uint64_t>> probe (new std::vector<uint64_t> (4096, 1));
        const uint64_t after = inUse();
        CHECK (after > before);
        CHECK (probe->size() == 4096);
    }

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    const DiagnosticsEvent e = makeEvent (makeEnvelope (1, 1, 1, 513, 0));
    DiagnosticsEvent out;

    // Warm-up so any lazy first-use allocation is excluded from the measured window.
    trace.publish (e);
    trace.pop (out);

    const uint64_t before = inUse();
    for (uint64_t k = 0; k < 10000; ++k)
    {
        trace.publish (e);
        trace.pop (out);
    }
    const uint64_t after = inUse();
    CHECK_EQ (after, before);

    // Disabled publish must also be allocation-free.
    trace.setEnabled (false);
    const uint64_t beforeDisabled = inUse();
    for (uint64_t k = 0; k < 10000; ++k)
        trace.publish (e);
    CHECK_EQ (inUse(), beforeDisabled);
#else
    std::printf ("    note: mallinfo2 probe unavailable on this platform; allocation check skipped\n");
#endif
}

JAM_TEST(Diagnostics, traceCapacityIsFixedAndSmall)
{
    CHECK_EQ (DiagnosticsTrace::capacity(), kTraceCapacity);
    CHECK (sizeof (DiagnosticsEvent) <= 320);
    CHECK (std::is_trivially_copyable<DiagnosticsEvent>::value);
}
