// Corpus manifest reader implementation. See Manifest.h.

#include "Manifest.h"
#include "Json.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace rhythmeval
{

namespace
{

const rhythmjson::Value* requireObject (const rhythmjson::Value& parent,
                                        const std::string& key,
                                        const std::string& context)
{
    const rhythmjson::Value* v = parent.find (key);
    if (v == nullptr)
        throw ManifestError (context + ": missing required field \"" + key + "\"");
    if (! v->isObject())
        throw ManifestError (context + ": field \"" + key + "\" must be an object");
    return v;
}

const rhythmjson::Value* requireArray (const rhythmjson::Value& parent,
                                       const std::string& key,
                                       const std::string& context)
{
    const rhythmjson::Value* v = parent.find (key);
    if (v == nullptr)
        throw ManifestError (context + ": missing required field \"" + key + "\"");
    if (! v->isArray())
        throw ManifestError (context + ": field \"" + key + "\" must be an array");
    return v;
}

std::string requireString (const rhythmjson::Value& parent,
                           const std::string& key,
                           const std::string& context)
{
    const rhythmjson::Value* v = parent.find (key);
    if (v == nullptr)
        throw ManifestError (context + ": missing required field \"" + key + "\"");
    if (! v->isString())
        throw ManifestError (context + ": field \"" + key + "\" must be a string");
    return v->text();
}

double requireNumber (const rhythmjson::Value& parent,
                      const std::string& key,
                      const std::string& context)
{
    const rhythmjson::Value* v = parent.find (key);
    if (v == nullptr)
        throw ManifestError (context + ": missing required field \"" + key + "\"");
    if (! v->isNumber())
        throw ManifestError (context + ": field \"" + key + "\" must be a number");
    return v->number();
}

std::vector<double> numberArrayStrict (const rhythmjson::Value& parent,
                                       const std::string& key,
                                       const std::string& context)
{
    const rhythmjson::Value* arr = requireArray (parent, key, context);
    std::vector<double> out;
    out.reserve (arr->items().size());
    for (const rhythmjson::Value& item : arr->items())
    {
        if (! item.isNumber())
            throw ManifestError (context + ": array \"" + key
                                 + "\" must contain only numbers");
        out.push_back (item.number());
    }
    return out;
}

/** Parses an array of [start, end] spans. `required` selects whether a missing
    field is an error (the corpus promises `trueSilenceSpans` on every fixture,
    so its absence is a manifest bug, not an empty list). */
std::vector<SilenceSpan> spanArray (const rhythmjson::Value& parent,
                                    const std::string& key,
                                    const std::string& context,
                                    bool required)
{
    const rhythmjson::Value* spans = parent.find (key);
    if (spans == nullptr)
    {
        if (required)
            throw ManifestError (context + ": missing required field \"" + key + "\"");
        return {};
    }
    if (! spans->isArray())
        throw ManifestError (context + ": \"" + key + "\" must be an array");

    std::vector<SilenceSpan> out;
    for (const rhythmjson::Value& span : spans->items())
    {
        if (! span.isArray() || span.items().size() != 2
            || ! span.items()[0].isNumber() || ! span.items()[1].isNumber())
            throw ManifestError (context + ": each " + key + " span must be [start, end]");
        SilenceSpan s;
        s.startSeconds = span.items()[0].number();
        s.endSeconds = span.items()[1].number();
        if (! (s.endSeconds >= s.startSeconds))
            throw ManifestError (context + ": " + key + " span end < start");
        out.push_back (s);
    }
    return out;
}

} // namespace

Manifest parseManifest (const std::string& jsonText)
{
    rhythmjson::Value root;
    try
    {
        root = rhythmjson::parse (jsonText);
    }
    catch (const std::exception& e)
    {
        throw ManifestError (std::string ("manifest is not valid JSON: ") + e.what());
    }
    if (! root.isObject())
        throw ManifestError ("manifest root must be a JSON object");

    Manifest manifest;
    manifest.schemaVersion =
        static_cast<int> (requireNumber (root, "schemaVersion", "manifest"));

    const rhythmjson::Value* corpus = root.find ("corpus");
    if (corpus != nullptr && corpus->isObject())
        manifest.corpusId = corpus->stringOr ("id", "");

    const rhythmjson::Value* conventions = root.find ("conventions");
    if (conventions != nullptr && conventions->isObject())
    {
        const rhythmjson::Value* tol = conventions->find ("beatToleranceSeconds");
        if (tol != nullptr && tol->isNumber() && tol->number() > 0.0)
            manifest.beatToleranceSeconds = tol->number();
    }

    const rhythmjson::Value* fixtureArray = requireArray (root, "fixtures", "manifest");
    for (const rhythmjson::Value& item : fixtureArray->items())
    {
        if (! item.isObject())
            throw ManifestError ("manifest: every fixture must be an object");

        ManifestFixture f;
        f.name = requireString (item, "name", "fixture");
        const std::string ctx = "fixture \"" + f.name + "\"";
        f.file = requireString (item, "file", ctx);
        f.sha256 = item.stringOr ("sha256", "");
        f.license = item.stringOr ("license", "");
        f.provenance = item.stringOr ("provenance", "");
        f.notes = item.stringOr ("notes", "");
        f.tempoProfile = requireString (item, "tempoProfile", ctx);
        f.durationSeconds = requireNumber (item, "durationSeconds", ctx);
        f.sampleRate = requireNumber (item, "sampleRate", ctx);

        const rhythmjson::Value* meter = requireObject (item, "meter", ctx);
        f.meterNumerator = static_cast<int> (requireNumber (*meter, "numerator", ctx + " meter"));
        f.meterDenominator = static_cast<int> (requireNumber (*meter, "denominator", ctx + " meter"));
        f.beatsPerBar = static_cast<int> (requireNumber (*meter, "beatsPerBar", ctx + " meter"));
        if (f.beatsPerBar <= 0)
            throw ManifestError (ctx + ": meter.beatsPerBar must be positive");

        const rhythmjson::Value* nominal = item.find ("nominalBpm");
        if (nominal != nullptr && nominal->isNumber())
        {
            f.hasNominalBpm = true;
            f.nominalBpm = nominal->number();
        }
        f.bpmStart = item.numberOr ("bpmStart", 0.0);
        f.bpmEnd = item.numberOr ("bpmEnd", 0.0);
        f.rampStartSeconds = item.numberOr ("rampStartSeconds", 0.0);
        f.rampEndSeconds = item.numberOr ("rampEndSeconds", 0.0);

        f.beats = numberArrayStrict (item, "beats", ctx);
        f.onsets = numberArrayStrict (item, "onsets", ctx);
        if (f.beats.empty())
            throw ManifestError (ctx + ": \"beats\" must not be empty");

        f.silenceSpans = spanArray (item, "silenceSpans", ctx, false);
        f.trueSilenceSpans = spanArray (item, "trueSilenceSpans", ctx, true);

        const rhythmjson::Value* silent = item.find ("silentBeats");
        if (silent != nullptr)
        {
            if (! silent->isArray())
                throw ManifestError (ctx + ": \"silentBeats\" must be an array");
            for (const rhythmjson::Value& b : silent->items())
            {
                if (! b.isNumber())
                    throw ManifestError (ctx + ": silentBeats must contain only numbers");
                f.silentBeats.push_back (static_cast<int> (b.number()));
            }
        }

        f.tags = item.stringArray ("scenarioTags");
        if (f.tags.empty())
            throw ManifestError (ctx + ": \"scenarioTags\" must not be empty");

        manifest.fixtures.push_back (std::move (f));
    }

    if (manifest.fixtures.empty())
        throw ManifestError ("manifest contains no fixtures");

    return manifest;
}

Manifest readManifestFile (const std::string& path)
{
    std::ifstream in (path.c_str(), std::ios::binary);
    if (! in.good())
        throw ManifestError ("could not open manifest file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseManifest (ss.str());
}

RhythmTruth toTruth (const ManifestFixture& fixture)
{
    RhythmTruth t;
    t.name = fixture.name;
    t.beats = fixture.beats;
    t.onsets = fixture.onsets;
    t.silenceSpans = fixture.silenceSpans;
    t.trueSilenceSpans = fixture.trueSilenceSpans;
    t.silentBeats = fixture.silentBeats;
    t.tempoProfile = fixture.tempoProfile;
    t.hasNominalBpm = fixture.hasNominalBpm;
    t.nominalBpm = fixture.nominalBpm;
    t.bpmStart = fixture.bpmStart;
    t.bpmEnd = fixture.bpmEnd;
    t.rampStartSeconds = fixture.rampStartSeconds;
    t.rampEndSeconds = fixture.rampEndSeconds;
    t.beatsPerBar = fixture.beatsPerBar;
    t.meterNumerator = fixture.meterNumerator;
    t.meterDenominator = fixture.meterDenominator;
    t.tags = fixture.tags;
    t.durationSeconds = fixture.durationSeconds;
    t.sampleRate = fixture.sampleRate;
    return t;
}

} // namespace rhythmeval
