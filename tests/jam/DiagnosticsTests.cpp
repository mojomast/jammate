// Unit tests for jam::diagnostics (DEVPLAN DIAG-001, SPEC.md 22).
//
// What is asserted here is the contract, not the convenience:
//   - provenance is lossless, including uint64_t wrap, and BOTH source rates are
//     exported so a mismatch stays visible;
//   - the input horizon is never presented as live availability;
//   - unmeasured values stay unmeasured (null / empty), never fabricated;
//   - domain-invalid values are counted once per field, preserved raw in the
//     record, and masked to null/empty in the export;
//   - numeric export is locale-independent;
//   - CSV carries a versioned metadata preamble, so drops are reported even with
//     zero retained events;
//   - a rejected measurement clears the field;
//   - tracing off never copies or enqueues;
//   - the ordered trace queue drops the INCOMING event and counts it;
//   - the latest-value UI path never stands in for trace history;
//   - CSV and JSON exports parse back to the same values and never emit NaN/Inf;
//   - lifecycle reset isolates sessions;
//   - publish() shows no NET heap growth (net evidence, not a zero-malloc proof).
//
// Policy: no file I/O, no sleeping, no wall-clock. Threads are used only for the
// SPSC and latest-value concurrency tests, and their loops are bounded. The only
// external fixture is the C locale, which the locale test may re-point at a
// comma-decimal locale if one happens to be installed.

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

#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)) \
    && ! defined(__SANITIZE_THREAD__) && ! defined(__SANITIZE_ADDRESS__)
#include <malloc.h>
#define DIAG_TEST_HAVE_MALLINFO2 1
#endif

#if defined(__GLIBC__) && defined(__linux__)
#include <locale.h>
#define DIAG_TEST_HAVE_USELOCALE 1
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

// The '#' metadata preamble, keyed by name.
struct CsvExport
{
    std::vector<std::pair<std::string, std::string>> metadata;
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;

    const std::string* find (const std::string& key) const
    {
        for (const auto& kv : metadata)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }

    bool hasMetadata (const std::string& key) const { return find (key) != nullptr; }

    /** Value of `name` in `row`, or a marker if the column is absent. */
    std::string cell (std::size_t row, const char* name) const
    {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == name)
                return rows[row][i];
        return "<missing column>";
    }
};

/** Parse the documented CSV grammar: '# key=value' preamble lines, then a header
    line, then event rows. */
CsvExport parseCsvExport (const std::string& text)
{
    CsvExport parsed;
    const std::vector<std::vector<std::string>> lines = parseCsv (text);
    const std::string prefix = kCsvMetadataPrefix;

    std::size_t i = 0;
    for (; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        if (line.size() == 1 && line[0].size() > prefix.size()
            && line[0].compare (0, prefix.size(), prefix) == 0)
        {
            const std::string body = line[0].substr (prefix.size());
            if (body == "jam-diagnostics-csv")
                continue; // format marker
            const auto eq = body.find ('=');
            if (eq != std::string::npos)
                parsed.metadata.push_back ({ body.substr (0, eq), body.substr (eq + 1) });
            continue;
        }
        break;
    }

    if (i < lines.size())
        parsed.header = lines[i++];
    for (; i < lines.size(); ++i)
        parsed.rows.push_back (lines[i]);
    return parsed;
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
    // The parsed text is COPIED, not referenced: callers routinely pass a
    // temporary such as toJson(collector), and a held reference would dangle.
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
        while (pos_ < s_.size()
               && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\t' || s_[pos_] == '\r'))
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
                    const unsigned code = static_cast<unsigned> (
                        std::strtoul (s_.substr (pos_, 4).c_str(), nullptr, 16));
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

    const std::string s_;
    std::size_t pos_ = 0;
};

std::string lowerCase (std::string s)
{
    for (char& ch : s)
        ch = static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
    return s;
}

/** Install a comma-decimal LC_NUMERIC for THIS thread, if one can be created.
    Thread-local (uselocale) so no global locale state is touched. Returns a
    locale_t that the caller restores, or (locale_t) 0. */
#if defined(DIAG_TEST_HAVE_USELOCALE)
locale_t installCommaNumericLocale()
{
    static const char* candidates[] = {
        "de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8", "fr_FR"
    };
    for (const char* name : candidates)
    {
        locale_t candidate = newlocale (LC_NUMERIC_MASK, name, (locale_t) 0);
        if (candidate == (locale_t) 0)
            continue;
        // Only accept it if it really uses a comma, otherwise it proves nothing.
        if (uselocale (candidate) == (locale_t) 0)
        {
            freelocale (candidate);
            continue;
        }
        char probe[32];
        std::snprintf (probe, sizeof probe, "%.1f", 1.5);
        const bool comma = std::strchr (probe, ',') != nullptr;
        (void) uselocale (LC_GLOBAL_LOCALE);
        if (! comma)
        {
            freelocale (candidate);
            continue;
        }
        return candidate;
    }
    return (locale_t) 0;
}
#endif

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
    CHECK_EQ (events->items[0].get ("input_sample_time")->text,
              std::string ("18446744073709551614"));
    CHECK_EQ (events->items[0].get ("input_horizon_sample_time")->text,
              std::to_string (horizon));
}

JAM_TEST(Diagnostics, bothSourceRatesExportedIndependently)
{
    // A rate change between the observation and the envelope must stay visible.
    ObservationEnvelope env = makeEnvelope (1, 1000, 1000, 3048, 0);
    env.observation.sourceSampleRate = 44100.0;
    env.sourceSampleRate = 48000.0;

    DiagnosticsEvent e = makeEvent (env);
    CHECK (isPositiveRate (e.envelope.sourceSampleRate));
    CHECK (isPositiveRate (e.envelope.observation.sourceSampleRate));
    CHECK (! e.isInvalid (DiagnosticsField::envelopeRate));
    CHECK (! e.isInvalid (DiagnosticsField::observationRate));

    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "source_sample_rate_hz"), std::string ("48000"));
    CHECK_EQ (csv.cell (0, "observation_source_sample_rate_hz"), std::string ("44100"));

    Json doc;
    JsonParser parser (toJson (collector));
    REQUIRE (parser.parse (doc));
    const Json& row = doc.get ("events")->items[0];
    CHECK_EQ (row.get ("source_sample_rate_hz")->text, std::string ("48000"));
    CHECK_EQ (row.get ("observation_source_sample_rate_hz")->text, std::string ("44100"));
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
    CHECK (row.get ("live_receipt_sample_time")->kind == Json::Kind::null);
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

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "callback_latency_seconds"), std::string (""));
    CHECK_EQ (csv.cell (0, "callback_latency_measured"), std::string ("0"));
    CHECK_EQ (csv.cell (0, "processing_duration_seconds"), std::string (""));
    CHECK_EQ (csv.cell (0, "ring_overruns"), std::string (""));
    CHECK_EQ (csv.cell (0, "observation_drops"), std::string (""));
    CHECK_EQ (csv.cell (0, "clock_bpm"), std::string (""));
    CHECK_EQ (csv.cell (0, "clock_lock_state"), std::string (""));
    CHECK_EQ (csv.cell (0, "clock_known"), std::string ("0"));

    const std::string json = toJson (collector);
    Json doc;
    JsonParser parser (json);
    REQUIRE (parser.parse (doc));
    const Json& row = doc.get ("events")->items[0];
    CHECK (row.get ("callback_latency_seconds")->kind == Json::Kind::null);
    CHECK (row.get ("processing_duration_seconds")->kind == Json::Kind::null);
    CHECK (row.get ("clock_bpm")->kind == Json::Kind::null);
    CHECK (row.get ("clock_lock_state")->kind == Json::Kind::null);
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
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 0 });

    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    DiagnosticsCollector collector (4);
    collector.drain (trace);
    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "ring_overruns"), std::string ("7"));
    CHECK_EQ (csv.cell (0, "ring_overruns_measured"), std::string ("1"));
    CHECK_EQ (csv.cell (0, "observation_drops"), std::string ("3"));
    CHECK_EQ (csv.cell (0, "clock_lock_state"), std::string ("Locked"));
    CHECK_EQ (csv.cell (0, "clock_known"), std::string ("1"));
    CHECK_EQ (csv.cell (0, "clock_beat_in_bar"), std::string ("2"));
    CHECK_EQ (csv.cell (0, "clock_tempo_frozen"), std::string ("0"));
    CHECK_EQ (csv.cell (0, "trace_dropped_at_drain"), std::string ("0"));
    CHECK_EQ (csv.cell (0, "collector_dropped"), std::string ("0"));
}

// --- domain policy ----------------------------------------------------------

JAM_TEST(Diagnostics, domainPolicyHelpersAreExplicit)
{
    // Rates must be finite and positive.
    CHECK (isPositiveRate (48000.0));
    CHECK (! isPositiveRate (0.0));
    CHECK (! isPositiveRate (-1.0));
    CHECK (! isPositiveRate (std::numeric_limits<double>::infinity()));
    CHECK (! isPositiveRate (std::numeric_limits<double>::quiet_NaN()));

    // Zero BPM is "unknown" and acceptable; negative is not.
    CHECK (isTempoValue (0.0));
    CHECK (isTempoValue (120.0));
    CHECK (! isTempoValue (-0.5));
    CHECK (! isTempoValue (std::numeric_limits<double>::quiet_NaN()));

    // Unit intervals are closed at both ends.
    CHECK (isUnitInterval (0.0));
    CHECK (isUnitInterval (1.0));
    CHECK (! isUnitInterval (1.0001));
    CHECK (! isUnitInterval (-0.0001));
    CHECK (! isUnitInterval (std::numeric_limits<double>::infinity()));

    // dBFS is not positive.
    CHECK (isDecibelFullScale (0.0));
    CHECK (isDecibelFullScale (-120.0));
    CHECK (! isDecibelFullScale (0.5));

    CHECK (isPositiveMeter (1));
    CHECK (isPositiveMeter (4));
    CHECK (! isPositiveMeter (0));
    CHECK (! isPositiveMeter (-1));

    // beatInBar 0 means "not yet known" and is accepted.
    CHECK (isKnownBeatInBar (0, 4));
    CHECK (isKnownBeatInBar (4, 4));
    CHECK (! isKnownBeatInBar (5, 4));
    CHECK (! isKnownBeatInBar (-1, 4));
}

JAM_TEST(Diagnostics, finiteOutOfRangeValuesAreCountedAndMasked)
{
    ObservationEnvelope env = makeEnvelope (4, 100, 100, 612, 0);
    // All finite, all outside the documented domain.
    env.sourceSampleRate = 0.0;
    env.observation.sourceSampleRate = -48000.0;
    env.observation.bpmCandidate = -12.0f;
    env.observation.beatConfidence01 = 1.5f;
    env.observation.beatPhase01 = -0.25f;
    env.observation.onsetStrength01 = 2.0f;
    env.observation.transientDensity01 = -1.0f;
    env.observation.energyRmsDbfs = 3.5f;

    DiagnosticsEvent e = makeEvent (env);
    CHECK (e.isInvalid (DiagnosticsField::envelopeRate));
    CHECK (e.isInvalid (DiagnosticsField::observationRate));
    CHECK (e.isInvalid (DiagnosticsField::candidateBpm));
    CHECK (e.isInvalid (DiagnosticsField::observationConfidence01));
    CHECK (e.isInvalid (DiagnosticsField::observationBeatPhase01));
    CHECK (e.isInvalid (DiagnosticsField::onsetStrength01));
    CHECK (e.isInvalid (DiagnosticsField::transientDensity01));
    CHECK (e.isInvalid (DiagnosticsField::energyRmsDbfs));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 8 });

    // Raw evidence is preserved in the record even though the export masks it.
    CHECK_EQ (e.envelope.sourceSampleRate, 0.0);
    CHECK_EQ (e.envelope.observation.sourceSampleRate, -48000.0);
    CHECK_EQ (e.envelope.observation.bpmCandidate, -12.0f);

    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "source_sample_rate_hz"), std::string (""));
    CHECK_EQ (csv.cell (0, "observation_source_sample_rate_hz"), std::string (""));
    CHECK_EQ (csv.cell (0, "candidate_bpm"), std::string (""));
    CHECK_EQ (csv.cell (0, "candidate_confidence01"), std::string (""));
    CHECK_EQ (csv.cell (0, "beat_phase01"), std::string (""));
    CHECK_EQ (csv.cell (0, "onset_strength01"), std::string (""));
    CHECK_EQ (csv.cell (0, "transient_density01"), std::string (""));
    CHECK_EQ (csv.cell (0, "energy_rms_dbfs"), std::string (""));
    CHECK_EQ (csv.cell (0, "invalid_field_count"), std::string ("8"));
    // Values outside the masked set are unaffected.
    CHECK_EQ (csv.cell (0, "sequence"), std::string ("4"));

    Json doc;
    JsonParser parser (toJson (collector));
    REQUIRE (parser.parse (doc));
    const Json& row = doc.get ("events")->items[0];
    CHECK (row.get ("candidate_bpm")->kind == Json::Kind::null);
    CHECK (row.get ("source_sample_rate_hz")->kind == Json::Kind::null);
    CHECK (row.get ("energy_rms_dbfs")->kind == Json::Kind::null);
    CHECK_EQ (row.get ("invalid_field_count")->text, std::string ("8"));
    CHECK_EQ (row.get ("sequence")->text, std::string ("4"));

    // Re-validating the same raw evidence is idempotent, not additive.
    validate (e);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 8 });
}

JAM_TEST(Diagnostics, invalidClockFieldsAreCountedAndMasked)
{
    ClockSnapshot clock = makeClock();
    clock.beatInBar = 9;         // beyond beatsPerBar
    clock.beatsPerBar = 0;       // no meter
    clock.beatUnit = -4;
    clock.beatPhase01 = 1.5;
    clock.bpm = -1.0;

    DiagnosticsEvent e = makeEvent (makeEnvelope (5, 100, 100, 612, 0));
    attachClock (e, clock);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 5 });
    CHECK (e.isInvalid (DiagnosticsField::clockBeatInBar));
    CHECK (e.isInvalid (DiagnosticsField::clockBeatsPerBar));
    CHECK (e.isInvalid (DiagnosticsField::clockBeatUnit));
    CHECK (e.isInvalid (DiagnosticsField::clockBeatPhase01));
    CHECK (e.isInvalid (DiagnosticsField::clockBpm));

    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "clock_beat_in_bar"), std::string (""));
    CHECK_EQ (csv.cell (0, "clock_beats_per_bar"), std::string (""));
    CHECK_EQ (csv.cell (0, "clock_beat_unit"), std::string (""));
    CHECK_EQ (csv.cell (0, "clock_beat_phase01"), std::string (""));
    CHECK_EQ (csv.cell (0, "clock_bpm"), std::string (""));
    // Non-domain clock columns stay exported: they are text/discrete identity.
    CHECK_EQ (csv.cell (0, "clock_lock_state"), std::string ("Locked"));
    CHECK_EQ (csv.cell (0, "clock_generation"), std::string ("3"));
    CHECK_EQ (csv.cell (0, "clock_bar_phase01"), std::string ("0.125"));
}

JAM_TEST(Diagnostics, zeroBpmAndUnknownBeatRemainValid)
{
    ClockSnapshot clock = makeClock();
    clock.bpm = 0.0;      // "unknown" is an accepted state
    clock.beatInBar = 0;  // "not yet known" is an accepted state

    DiagnosticsEvent e = makeEvent (makeEnvelope (6, 100, 100, 612, 0));
    attachClock (e, clock);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 0 });

    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "clock_bpm"), std::string ("0"));
    CHECK_EQ (csv.cell (0, "clock_beat_in_bar"), std::string ("0"));
}

JAM_TEST(Diagnostics, nonFiniteInputsCountedAndSerialisedAsNull)
{
    ObservationEnvelope env = makeEnvelope (4, 100, 100, 612, 0);
    env.observation.bpmCandidate = std::numeric_limits<float>::quiet_NaN();
    env.observation.energyRmsDbfs = std::numeric_limits<float>::infinity();
    DiagnosticsEvent e = makeEvent (env);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });

    ClockSnapshot clock = makeClock();
    clock.bpm = std::numeric_limits<double>::quiet_NaN();
    attachClock (e, clock);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 3 });

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
    CHECK (row.get ("candidate_bpm")->kind == Json::Kind::null);
    CHECK (row.get ("energy_rms_dbfs")->kind == Json::Kind::null);
    CHECK (row.get ("clock_bpm")->kind == Json::Kind::null);
    CHECK_EQ (row.get ("invalid_field_count")->text, std::string ("3"));
    CHECK (row.get ("candidate_confidence01")->kind == Json::Kind::number);

    const std::string csvText = toCsv (collector);
    CHECK (lowerCase (csvText).find ("nan") == std::string::npos);
    const CsvExport csv = parseCsvExport (csvText);
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "candidate_bpm"), std::string (""));
}

JAM_TEST(Diagnostics, invalidMeasurementIsRejectedAndClearsField)
{
    DiagnosticsEvent e = makeEvent (makeEnvelope (5, 0, 0, 512, 0));

    attachProcessingDuration (e, std::numeric_limits<double>::quiet_NaN());
    CHECK (! e.processingDurationSeconds.measured);
    CHECK (e.isInvalid (DiagnosticsField::processingDuration));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 1 });

    attachCallbackLatency (e, -0.001);
    CHECK (! e.callbackLatencySeconds.measured);
    CHECK (e.isInvalid (DiagnosticsField::callbackLatency));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });

    attachCallbackLatency (e, std::numeric_limits<double>::infinity());
    CHECK (! e.callbackLatencySeconds.measured);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });

    attachProcessingDuration (e, 0.0);
    CHECK (e.processingDurationSeconds.measured);
    CHECK (! e.isInvalid (DiagnosticsField::processingDuration));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 1 });
}

JAM_TEST(Diagnostics, invalidMeasurementAfterValidClearsTheOldValue)
{
    DiagnosticsEvent e = makeEvent (makeEnvelope (7, 0, 0, 512, 0));

    // valid -> invalid must not leave the old reading visible as measured.
    attachProcessingDuration (e, 0.004);
    REQUIRE (e.processingDurationSeconds.measured);
    CHECK_NEAR (e.processingDurationSeconds.value, 0.004, 1e-12);

    attachProcessingDuration (e, std::numeric_limits<double>::quiet_NaN());
    CHECK (! e.processingDurationSeconds.measured);
    CHECK_EQ (e.processingDurationSeconds.value, 0.0);
    CHECK (e.isInvalid (DiagnosticsField::processingDuration));

    attachCallbackLatency (e, 0.02);
    REQUIRE (e.callbackLatencySeconds.measured);
    attachCallbackLatency (e, -1.0);
    CHECK (! e.callbackLatencySeconds.measured);
    CHECK_EQ (e.callbackLatencySeconds.value, 0.0);
    CHECK (e.isInvalid (DiagnosticsField::callbackLatency));

    // valid -> invalid -> valid restores a clean measurement.
    attachProcessingDuration (e, 0.005);
    CHECK (e.processingDurationSeconds.measured);
    CHECK_NEAR (e.processingDurationSeconds.value, 0.005, 1e-12);
    CHECK (! e.isInvalid (DiagnosticsField::processingDuration));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 1 });

    // The cleared field exports as empty/null, never as the stale 0.004.
    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "callback_latency_seconds"), std::string (""));
    CHECK_EQ (csv.cell (0, "callback_latency_measured"), std::string ("0"));
    CHECK_EQ (csv.cell (0, "processing_duration_seconds"), std::string ("0.005"));
}

JAM_TEST(Diagnostics, repeatedInvalidAttachmentCountsOncePerField)
{
    DiagnosticsEvent e = makeEvent (makeEnvelope (8, 0, 0, 512, 0));
    for (int i = 0; i < 4; ++i)
        attachProcessingDuration (e, -1.0);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 1 });

    attachCallbackLatency (e, std::numeric_limits<double>::quiet_NaN());
    attachCallbackLatency (e, std::numeric_limits<double>::quiet_NaN());
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });
}

JAM_TEST(Diagnostics, invalidToValidObservationClearsTheMark)
{
    ObservationEnvelope env = makeEnvelope (1, 1000, 1000, 3048, 0);
    env.sourceSampleRate = -48000.0;
    env.observation.sourceSampleRate = -44100.0;
    env.observation.beatPhase01 = 1.5f;

    DiagnosticsEvent e = makeEvent (env);
    CHECK (e.isInvalid (DiagnosticsField::envelopeRate));
    CHECK (e.isInvalid (DiagnosticsField::observationRate));
    CHECK (e.isInvalid (DiagnosticsField::observationBeatPhase01));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 3 });

    // Correct the raw evidence in place, exactly as an integrator would, then
    // recompute. The stale marks must NOT survive.
    e.envelope.sourceSampleRate = 48000.0;
    e.envelope.observation.sourceSampleRate = 44100.0;
    e.envelope.observation.beatPhase01 = 0.25f;
    validate (e);

    CHECK (! e.isInvalid (DiagnosticsField::envelopeRate));
    CHECK (! e.isInvalid (DiagnosticsField::observationRate));
    CHECK (! e.isInvalid (DiagnosticsField::observationBeatPhase01));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 0 });

    // And the export now carries the valid values instead of masking them.
    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "source_sample_rate_hz"), std::string ("48000"));
    CHECK_EQ (csv.cell (0, "observation_source_sample_rate_hz"), std::string ("44100"));
    CHECK_EQ (csv.cell (0, "beat_phase01"), std::string ("0.25"));
    CHECK_EQ (csv.cell (0, "invalid_field_count"), std::string ("0"));
}

JAM_TEST(Diagnostics, invalidToValidClockReattachClearsTheMark)
{
    ObservationEnvelope env = makeEnvelope (2, 1000, 1000, 3048, 0);
    DiagnosticsEvent e = makeEvent (env);
    REQUIRE (e.invalidFieldCount() == 0u);

    ClockSnapshot bad = makeClock();
    bad.bpm = -1.0;
    bad.beatPhase01 = 1.5;
    bad.beatsPerBar = 0;
    attachClock (e, bad);
    // Four marks, not three: a zero-length bar also puts beatInBar 2 out of range.
    CHECK (e.isInvalid (DiagnosticsField::clockBpm));
    CHECK (e.isInvalid (DiagnosticsField::clockBeatPhase01));
    CHECK (e.isInvalid (DiagnosticsField::clockBeatsPerBar));
    CHECK (e.isInvalid (DiagnosticsField::clockBeatInBar));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 4 });

    // Re-attaching a valid clock must clear every clock mark, not accumulate.
    attachClock (e, makeClock());
    CHECK (! e.isInvalid (DiagnosticsField::clockBpm));
    CHECK (! e.isInvalid (DiagnosticsField::clockBeatPhase01));
    CHECK (! e.isInvalid (DiagnosticsField::clockBeatsPerBar));
    CHECK (! e.isInvalid (DiagnosticsField::clockBeatInBar));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 0 });

    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    CHECK_EQ (csv.cell (0, "clock_bpm"), std::string ("118"));
    CHECK_EQ (csv.cell (0, "clock_beat_phase01"), std::string ("0.5"));
    CHECK_EQ (csv.cell (0, "clock_beats_per_bar"), std::string ("4"));
    CHECK_EQ (csv.cell (0, "invalid_field_count"), std::string ("0"));
}

JAM_TEST(Diagnostics, clockMarksClearWhenClockNoLongerKnown)
{
    ObservationEnvelope env = makeEnvelope (3, 1000, 1000, 3048, 0);
    DiagnosticsEvent e = makeEvent (env);

    ClockSnapshot bad = makeClock();
    bad.bpm = -1.0;
    bad.beatUnit = 0;
    attachClock (e, bad);
    CHECK (e.isInvalid (DiagnosticsField::clockBpm));
    CHECK (e.isInvalid (DiagnosticsField::clockBeatUnit));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });

    // Withdrawing the clock must not leave clock fields marked invalid.
    e.clockKnown = false;
    validate (e);
    CHECK (! e.isInvalid (DiagnosticsField::clockBpm));
    CHECK (! e.isInvalid (DiagnosticsField::clockBeatUnit));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 0 });
}

JAM_TEST(Diagnostics, validateRetainsRejectedDurationBits)
{
    ObservationEnvelope env = makeEnvelope (4, 1000, 1000, 3048, 0);
    DiagnosticsEvent e = makeEvent (env);

    attachProcessingDuration (e, std::numeric_limits<double>::quiet_NaN());
    attachCallbackLatency (e, -1.0);
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });

    // A rejected measurement is not derivable from the stored value (the field is
    // simply unmeasured), so validate() must NOT clear these bits.
    validate (e);
    CHECK (e.isInvalid (DiagnosticsField::processingDuration));
    CHECK (e.isInvalid (DiagnosticsField::callbackLatency));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });

    // Same when a clock is attached afterwards, since attachClock() validates.
    attachClock (e, makeClock());
    CHECK (e.isInvalid (DiagnosticsField::processingDuration));
    CHECK (e.isInvalid (DiagnosticsField::callbackLatency));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 2 });

    // The masks must not overlap, which is what makes that retention sound.
    CHECK_EQ (kEvidenceInvalidFieldMask & kDurationInvalidFieldMask, uint32_t { 0 });
    CHECK ((kEvidenceInvalidFieldMask
            & static_cast<uint32_t> (DiagnosticsField::processingDuration)) == 0u);
    CHECK ((kEvidenceInvalidFieldMask
            & static_cast<uint32_t> (DiagnosticsField::clockBeatUnit)) != 0u);

    // Only the duration attachments can clear them again.
    attachProcessingDuration (e, 0.001);
    CHECK (! e.isInvalid (DiagnosticsField::processingDuration));
    CHECK_EQ (e.invalidFieldCount(), std::size_t { 1 });
}

// --- export format ----------------------------------------------------------

JAM_TEST(Diagnostics, csvCarriesVersionedMetadataPreamble)
{
    DiagnosticsCollector collector (4);
    const CsvExport csv = parseCsvExport (toCsv (collector));

    REQUIRE (csv.hasMetadata ("schema_version"));
    CHECK_EQ (*csv.find ("schema_version"), std::to_string (kSchemaVersion));
    CHECK_EQ (*csv.find ("trace_capacity"), std::to_string (kTraceCapacity));
    CHECK_EQ (*csv.find ("events_retained"), std::string ("0"));
    CHECK_EQ (*csv.find ("trace_dropped_at_drain"), std::string ("0"));
    CHECK_EQ (*csv.find ("collector_dropped"), std::string ("0"));

    // The preamble comes before the header, and the header is still present.
    CHECK (! csv.header.empty());
    CHECK_EQ (csv.header.size(), kEventColumnCount + 2);
    CHECK (csv.header[0] == std::string ("session"));
    CHECK_EQ (csv.header.back(), std::string ("collector_dropped"));
    CHECK_EQ (csv.rows.size(), std::size_t { 0 });
}

JAM_TEST(Diagnostics, dropsReportedWithZeroRetainedEvents)
{
    // A collector that retains nothing must still account for every loss.
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    for (uint64_t k = 1; k <= 6; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));

    // Overflow the trace queue as well, so both drop sources are non-zero.
    for (uint64_t k = 7; k <= kTraceCapacity + 3; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));

    DiagnosticsCollector collector (0);
    // drain() is bounded at kTraceCapacity per call, so a full queue needs more
    // than one call to empty. That bound is the documented drain cadence.
    std::size_t popped = 0;
    for (int round = 0; round < 4; ++round)
        popped += collector.drain (trace);

    // Only kTraceCapacity events were ever queued; the other 3 were rejected by
    // the trace queue. Every delivered event is then discarded by this collector
    // because it retains nothing, and all of that must be visible in the export.
    CHECK_EQ (popped, static_cast<std::size_t> (kTraceCapacity));
    CHECK_EQ (collector.events().size(), std::size_t { 0 });
    CHECK_EQ (collector.collectorDropped(), static_cast<uint64_t> (kTraceCapacity));
    CHECK_EQ (collector.traceDroppedAtDrain(), uint64_t { 3 });

    // CSV: the preamble survives even though there is not a single event row.
    const CsvExport csv = parseCsvExport (toCsv (collector));
    CHECK_EQ (csv.rows.size(), std::size_t { 0 });
    REQUIRE (csv.hasMetadata ("events_retained"));
    CHECK_EQ (*csv.find ("events_retained"), std::string ("0"));
    REQUIRE (csv.hasMetadata ("trace_dropped_at_drain"));
    CHECK_EQ (*csv.find ("trace_dropped_at_drain"), std::string ("3"));
    REQUIRE (csv.hasMetadata ("collector_dropped"));
    CHECK_EQ (*csv.find ("collector_dropped"), std::to_string (kTraceCapacity));

    // JSON: same guarantee.
    Json doc;
    JsonParser parser (toJson (collector));
    REQUIRE (parser.parse (doc));
    CHECK_EQ (doc.get ("events")->items.size(), std::size_t { 0 });
    CHECK_EQ (doc.get ("events_retained")->text, std::string ("0"));
    CHECK_EQ (doc.get ("trace_dropped_at_drain")->text, std::string ("3"));
    CHECK_EQ (doc.get ("collector_dropped")->text, std::to_string (kTraceCapacity));
}

JAM_TEST(Diagnostics, numericExportIsLocaleIndependent)
{
    // Values chosen so a %g-style formatter would be visibly wrong or ambiguous.
    ObservationEnvelope env = makeEnvelope (1, 0, 0, 0, 0);
    env.observation.bpmCandidate = 1.0f / 3.0f;   // needs full precision
    env.observation.beatPhase01 = 0.1f;
    env.observation.energyRmsDbfs = -0.00025f;
    env.sourceSampleRate = 192000.0;

    ClockSnapshot clock = makeClock();
    clock.bpm = 0.1 + 0.2; // 0.30000000000000004 exactly
    clock.beatPhase01 = 1.0 / 3.0;
    clock.confidence01 = 0.875f;

    DiagnosticsEvent e = makeEvent (env);
    attachClock (e, clock);

    DiagnosticsCollector collector (4);
    DiagnosticsTrace trace;
    trace.setEnabled (true);
    trace.publish (e);
    collector.drain (trace);

#if defined(DIAG_TEST_HAVE_USELOCALE)
    locale_t comma = installCommaNumericLocale();
    const bool haveCommaLocale = comma != (locale_t) 0;
    if (haveCommaLocale)
        REQUIRE (uselocale (comma) != (locale_t) 0);
#else
    const bool haveCommaLocale = false;
#endif

    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 1);
    // Exact shortest round-trip forms, always with a '.' decimal separator.
    CHECK_EQ (csv.cell (0, "clock_bpm"), std::string ("0.30000000000000004"));
    CHECK_EQ (csv.cell (0, "clock_beat_phase01"), std::string ("0.3333333333333333"));
    CHECK_EQ (csv.cell (0, "clock_confidence01"), std::string ("0.875"));
    CHECK_EQ (csv.cell (0, "source_sample_rate_hz"), std::string ("192000"));
    CHECK_EQ (csv.cell (0, "energy_rms_dbfs"), std::string ("-0.00025"));

    // Numeric cells must never contain a comma, which is what a comma-decimal
    // locale would inject into them.
    for (std::size_t i = 0; i < csv.header.size(); ++i)
    {
        const std::string& name = csv.header[i];
        const bool numeric = name == "clock_bpm" || name == "clock_beat_phase01"
                          || name == "source_sample_rate_hz" || name == "sequence";
        if (numeric)
            CHECK (csv.rows[0][i].find (',') == std::string::npos);
    }

    // The strict JSON parser rejects a comma decimal separator outright, so a
    // locale-sensitive exporter would fail this parse.
    Json doc;
    JsonParser parser (toJson (collector));
    REQUIRE (parser.parse (doc));
    const Json& row = doc.get ("events")->items[0];
    CHECK_EQ (row.get ("clock_bpm")->text, std::string ("0.30000000000000004"));
    CHECK_EQ (row.get ("clock_beat_phase01")->text, std::string ("0.3333333333333333"));

#if defined(DIAG_TEST_HAVE_USELOCALE)
    if (haveCommaLocale)
    {
        uselocale (LC_GLOBAL_LOCALE);
        freelocale (comma);
    }
    else
    {
        std::printf ("    note: no comma-decimal LC_NUMERIC locale installed; "
                     "locale fixture skipped (exact-string assertions still ran)\n");
    }
#else
    std::printf ("    note: uselocale unavailable; locale fixture skipped\n");
#endif
}

// --- channels and lifecycle -------------------------------------------------

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
            DiagnosticsEvent e = makeEvent (makeEnvelope (k, k * 4096, k * 4096 + 7,
                                                          k * 4096 + 2055, k));
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

    trace.readViewModel (vm);
    if (vm.latestSeen)
        CHECK_EQ (vm.latest.envelope.sequence, kEvents);
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

    // A fresh collector for the overflow phase, so the numbers stay readable.
    DiagnosticsCollector overflow (10);
    for (uint64_t k = 1; k <= kTraceCapacity + 5; ++k)
        trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0)));

    std::size_t poppedSecond = 0;
    for (int round = 0; round < 4; ++round)
        poppedSecond += overflow.drain (trace);

    // 256 queued events were delivered; 5 were dropped by the trace queue; the
    // collector retained 10 and counted the remaining 246 as its own loss.
    CHECK_EQ (poppedSecond, static_cast<std::size_t> (kTraceCapacity));
    CHECK_EQ (overflow.events().size(), std::size_t { 10 });
    CHECK_EQ (overflow.traceDroppedAtDrain(), uint64_t { 5 });
    CHECK_EQ (overflow.collectorDropped(), static_cast<uint64_t> (kTraceCapacity - 10));

    const CsvExport csv = parseCsvExport (toCsv (overflow));
    REQUIRE (csv.hasMetadata ("trace_dropped_at_drain"));
    CHECK_EQ (*csv.find ("trace_dropped_at_drain"), std::string ("5"));
    CHECK_EQ (*csv.find ("collector_dropped"), std::to_string (kTraceCapacity - 10));
    CHECK_EQ (csv.cell (0, "trace_dropped_at_drain"), std::string ("5"));
    CHECK_EQ (csv.cell (0, "collector_dropped"), std::to_string (kTraceCapacity - 10));

    Json doc;
    JsonParser parser (toJson (overflow));
    REQUIRE (parser.parse (doc));
    CHECK_EQ (doc.get ("trace_dropped_at_drain")->text, std::string ("5"));
    CHECK_EQ (doc.get ("collector_dropped")->text, std::to_string (kTraceCapacity - 10));
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

    // Lifecycle: ALL roles quiescent. The caller plays the consumer role while
    // discarding queued events.
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

JAM_TEST(Diagnostics, finalDrainAfterPublisherStopsKeepsEveryEvent)
{
    // The supported sequence: the publisher is stopped and joined FIRST, then
    // tracing is disabled, then the final drain runs. setEnabled(false) alone is
    // not quiescence, so it is deliberately not what this test relies on.
    DiagnosticsTrace trace;
    trace.setEnabled (true);

    // Large enough that a full queue is retained in full, so the assertions below
    // can require that nothing was lost.
    DiagnosticsCollector collector (kTraceCapacity + 8);
    std::atomic<bool> stop { false };
    std::atomic<uint64_t> attempts { 0 };

    std::thread publisher ([&] {
        uint64_t k = 1;
        while (! stop.load (std::memory_order_acquire))
        {
            attempts.fetch_add (1, std::memory_order_relaxed);
            if (! trace.publish (makeEvent (makeEnvelope (k, k, k, k + 512, 0))))
                continue; // refused while the queue is full, and counted as a drop
            ++k;
        }
    });

    // Wait until the trace queue has actually refused an event, which can only
    // happen once all kTraceCapacity slots are occupied. Nothing drains during
    // this wait, so the queue reaches capacity on its own and the wait terminates
    // without sleeping. This is what makes the test a real full-queue test
    // instead of a two-event sample.
    for (int spin = 0;
         spin < 1000000000 && trace.counters().droppedEvents == 0; ++spin)
    {
        std::this_thread::yield();
    }

    stop.store (true, std::memory_order_release);
    publisher.join();          // actual publisher quiescence

    trace.setEnabled (false);  // only now is disabling meaningful
    const std::size_t drained = collector.drain (trace);

    const DiagnosticsCounters counters = trace.counters();
    const uint64_t tried = attempts.load (std::memory_order_relaxed);
    const uint64_t published = static_cast<uint64_t> (drained);

    // The queue really did fill: every accepted event, and only those, is
    // delivered by the post-join final drain.
    CHECK_EQ (counters.droppedEvents > 0, true);
    CHECK_EQ (published, static_cast<uint64_t> (kTraceCapacity));
    CHECK_EQ (drained, published);
    CHECK_EQ (collector.events().size(), published);
    CHECK_EQ (collector.collectorDropped(), uint64_t { 0 });

    // Refused attempts while the queue was full are counted, not silently lost:
    // every attempt is either accepted or reported as a drop.
    CHECK_EQ (published + counters.droppedEvents, tried);
    CHECK_EQ (collector.traceDroppedAtDrain(), counters.droppedEvents);

    // Ordered and complete, so the export after the final drain is trustworthy.
    for (std::size_t i = 0; i < collector.events().size(); ++i)
        CHECK_EQ (collector.events()[i].envelope.sequence, static_cast<uint64_t> (i + 1));
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
    const CsvExport csv = parseCsvExport (toCsv (collector));
    REQUIRE (csv.rows.size() == 2);
    CHECK_EQ (csv.header.size(), kEventColumnCount + 2);
    for (std::size_t r = 0; r < csv.rows.size(); ++r)
        CHECK_EQ (csv.rows[r].size(), csv.header.size());

    CHECK_EQ (csv.cell (0, "sequence"), std::string ("1"));
    CHECK_EQ (csv.cell (0, "clock_lock_state"), std::string ("Locked"));
    CHECK_EQ (csv.cell (0, "clock_bpm"), std::string ("118"));
    CHECK_EQ (csv.cell (1, "sequence"), std::string ("2"));
    CHECK_EQ (csv.cell (1, "clock_known"), std::string ("0"));
    CHECK_EQ (csv.cell (1, "clock_lock_state"), std::string (""));
    CHECK_EQ (csv.cell (1, "input_horizon_sample_time"), std::string ("3048"));
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

    const CsvExport csv = parseCsvExport (toCsv (collector));
    CHECK_EQ (csv.header.size(), kEventColumnCount + 2);
    CHECK_EQ (csv.rows.size(), std::size_t { 0 });
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

JAM_TEST(Diagnostics, publishShowsNoNetHeapGrowth)
{
#if defined(DIAG_TEST_HAVE_MALLINFO2)
    // Net evidence only. mallinfo2().uordblks counts bytes IN USE, so a matching
    // malloc/free inside the measured window would be invisible here. What this
    // rules out is retained growth (a per-event heap buffer, a growing
    // container), which is the realistic failure mode for a publication path.
    auto inUse = [] () { return static_cast<uint64_t> (mallinfo2().uordblks); };

    // Probe self-check: a real allocation must be visible, or the zero delta
    // below would mean nothing at all.
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

    // Warm-up so any lazy first-use allocation is outside the measured window.
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

    // Disabled publish must not retain anything either.
    trace.setEnabled (false);
    const uint64_t beforeDisabled = inUse();
    for (uint64_t k = 0; k < 10000; ++k)
        trace.publish (e);
    CHECK_EQ (inUse(), beforeDisabled);

    // And the queue-filling path keeps all 256 events with no retained growth
    // beyond the already-constructed queue.
    trace.setEnabled (true);
    for (uint64_t k = 0; k < kTraceCapacity * 2; ++k)
        trace.publish (e);
    CHECK_EQ (trace.counters().droppedEvents, static_cast<uint64_t> (kTraceCapacity));
    CHECK_EQ (inUse(), before);
#else
    std::printf ("    note: mallinfo2 probe unavailable on this platform; "
                 "net heap check skipped\n");
#endif
}

JAM_TEST(Diagnostics, traceCapacityIsFixedAndSmall)
{
    CHECK_EQ (DiagnosticsTrace::capacity(), kTraceCapacity);
    CHECK (sizeof (DiagnosticsEvent) <= 320);
    CHECK (std::is_trivially_copyable<DiagnosticsEvent>::value);
}