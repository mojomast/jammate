// rhythm-eval — one-command beat-tracker comparison (DEVPLAN EVAL-002).
//
//   rhythm-eval --corpus testdata/rhythm --out <dir> [--backend <name>]
//               [--backend-lib <path>] [--block <frames>]
//
// Runs a backend over every fixture in the corpus manifest, scores the resulting
// beat/tempo evidence with the pure metrics in Metrics.cpp, and writes
// machine-readable JSON and per-fixture CSV plus a Markdown summary. SPEC.md
// 21.3 requires JSON/CSV and a Markdown summary; SPEC.md 12.3 requires the
// metrics; SPEC.md 19 supplies the numeric gates.
//
// With no real backend available the tool still runs against built-in,
// deterministic synthetic backends (an "ideal" tracker that knows the ground
// truth and a "degraded" one with injected latency/drops/double-time). Those
// validate the scoring end-to-end: a perfect input must score perfectly, and a
// known degradation must produce the known metric. Without them a bug in the
// metrics would be indistinguishable from a bug in a real backend.
//
// A real backend plugs in through IRhythmTracker and is loaded from a shared
// library exporting jam_rhythm_create()/jam_rhythm_destroy(). See
// task-notes/EVAL-002.md ("Integration notes").

#include "BackendRunner.h"
#include "Json.h"
#include "Manifest.h"
#include "Metrics.h"

#include "jam/IRhythmTracker.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <dlfcn.h>
#  define RHYTHM_EVAL_HAVE_DLOPEN 1
#else
#  define RHYTHM_EVAL_HAVE_DLOPEN 0
#endif

// ---------------------------------------------------------------------------
// Allocation counting (SPEC 12.3 "allocation count")
// ---------------------------------------------------------------------------
//
// Overriding global new/delete lets the CLI report how many allocations a
// backend performed during its run. This is whole-process counting within the
// measured window, which is the honest number for "what does this backend
// allocate". It is not on any scored path; the scored metrics are deterministic.

namespace
{

std::atomic<std::size_t> g_allocationCount { 0 };

} // namespace

void* operator new (std::size_t size)
{
    g_allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    g_allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    g_allocationCount.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size != 0 ? size : 1);
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    g_allocationCount.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size != 0 ? size : 1);
}
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

namespace rhythmeval
{

namespace
{

// ---------------------------------------------------------------------------
// Synthetic backends
// ---------------------------------------------------------------------------

/** Ideal tracker: every ground-truth beat, exact local tempo. Its purpose is to
    prove the scorer gives a perfect score on perfect input. */
class SyntheticIdealBackend : public jam::IRhythmTracker
{
public:
    explicit SyntheticIdealBackend (const RhythmTruth& truth) : truth_ (truth) {}

    void reset (double sampleRate) override
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        cursor_ = 0;
    }

    const char* id() const noexcept override { return "synthetic-ideal"; }

    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        jam::RhythmObservation obs;
        obs.inputSampleTime = frame.sampleTime;
        obs.sourceSampleRate = sampleRate_;
        const double t0 = static_cast<double> (frame.sampleTime) / sampleRate_;
        const double t1 = static_cast<double> (frame.sampleTime + frame.numSamples)
                          / sampleRate_;
        obs.bpmCandidate = static_cast<float> (
            localTruthBpmAtTime (truth_, 0.5 * (t0 + t1)));
        obs.phaseValid = true;
        obs.beatConfidence01 = 1.0f;
        obs.onsetStrength01 = 1.0f;

        bool fired = false;
        while (cursor_ < truth_.beats.size() && truth_.beats[cursor_] < t1)
        {
            if (truth_.beats[cursor_] >= t0)
                fired = true;
            ++cursor_;
        }
        obs.beatEvent = fired;
        return obs;
    }

private:
    RhythmTruth truth_;
    double sampleRate_ = 48000.0;
    std::size_t cursor_ = 0;
};

/** Deliberately degraded tracker: a constant latency, every fourth beat
    dropped, a small deterministic BPM wobble, and double-time locks on two
    fixtures. Used to confirm the metrics move for the right reasons. */
class SyntheticDegradedBackend : public jam::IRhythmTracker
{
public:
    explicit SyntheticDegradedBackend (const RhythmTruth& truth) : truth_ (truth)
    {
        doubleTime_ = (truth_.name == "clean_sixteenths"
                       || truth_.name == "arpeggio");
        buildSchedule();
    }

    void reset (double sampleRate) override
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        cursor_ = 0;
        lcg_ = 0x2545f4914f6cdd1du;
    }

    const char* id() const noexcept override { return "synthetic-degraded"; }

    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        jam::RhythmObservation obs;
        obs.inputSampleTime = frame.sampleTime;
        obs.sourceSampleRate = sampleRate_;
        const double t0 = static_cast<double> (frame.sampleTime) / sampleRate_;
        const double t1 = static_cast<double> (frame.sampleTime + frame.numSamples)
                          / sampleRate_;

        double bpm = localTruthBpmAtTime (truth_, 0.5 * (t0 + t1));
        if (doubleTime_)
            bpm *= 2.0;
        // +/-1.5% deterministic wobble, so the median is still the nominal BPM.
        const double wobble = (nextUnit() - 0.5) * 0.03;
        obs.bpmCandidate = static_cast<float> (bpm * (1.0 + wobble));
        obs.phaseValid = true;
        obs.beatConfidence01 = 0.7f;
        obs.onsetStrength01 = 0.7f;

        bool fired = false;
        while (cursor_ < schedule_.size() && schedule_[cursor_] < t1)
        {
            if (schedule_[cursor_] >= t0)
                fired = true;
            ++cursor_;
        }
        obs.beatEvent = fired;
        return obs;
    }

private:
    void buildSchedule()
    {
        const double latency = 0.030;   // 30 ms system latency
        for (std::size_t i = 0; i < truth_.beats.size(); ++i)
        {
            if (i % 4 == 3)             // drop every fourth beat
                continue;
            const double t = truth_.beats[i] + latency;
            schedule_.push_back (t);
            if (doubleTime_)
            {
                const double bpm = localTruthBpmAtTime (truth_, t);
                if (bpm > 0.0)
                    schedule_.push_back (t + 0.5 * 60.0 / bpm);
            }
        }
        std::sort (schedule_.begin(), schedule_.end());
    }

    double nextUnit()
    {
        lcg_ = lcg_ * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double> ((lcg_ >> 11) & 0x1fffff) / 2097152.0;
    }

    RhythmTruth truth_;
    std::vector<double> schedule_;
    double sampleRate_ = 48000.0;
    std::size_t cursor_ = 0;
    bool doubleTime_ = false;
    unsigned long long lcg_ = 0x2545f4914f6cdd1du;
};

// ---------------------------------------------------------------------------
// Real backend loading
// ---------------------------------------------------------------------------

using CreateFn = jam::IRhythmTracker* (*)();
using DestroyFn = void (*) (jam::IRhythmTracker*);

#if RHYTHM_EVAL_HAVE_DLOPEN
/** Loads a function pointer from a shared library without the
    pointer-to-function cast that -Wpedantic rejects. */
template <typename Fn>
Fn loadSymbol (void* library, const char* name)
{
    void* symbol = ::dlsym (library, name);
    Fn fn = nullptr;
    static_assert (sizeof (Fn) == sizeof (symbol),
                   "function pointers and void* must be the same size");
    std::memcpy (&fn, &symbol, sizeof (fn));
    return fn;
}
#endif

struct LoadedBackend
{
    jam::IRhythmTracker* raw = nullptr;
    DestroyFn destroy = nullptr;
    void* library = nullptr;

    ~LoadedBackend()
    {
        if (destroy != nullptr && raw != nullptr)
            destroy (raw);
        if (library != nullptr)
        {
#if RHYTHM_EVAL_HAVE_DLOPEN
            ::dlclose (library);
#endif
        }
    }
};

// ---------------------------------------------------------------------------
// Option parsing
// ---------------------------------------------------------------------------

struct Options
{
    std::string corpus = "testdata/rhythm";
    std::string out;
    std::string backend = "synthetic-ideal";
    std::string backendLib;
    std::size_t blockFrames = 128;

    /** Backend latency compensations to score, seconds, in request order. The
        uncompensated run (0.0) is always scored; each requested value adds one
        more variant in the same run, so the effect is measured, not asserted. */
    std::vector<double> latencyCompensations;

    std::string jsonFile = "results.json";
    std::string summaryFile = "summary.md";
    std::string combinedCsvFile = "fixtures.csv";
    std::string perFixtureDirName = "per_fixture";
};

std::string compensationLabel (double seconds)
{
    char buf[48];
    std::snprintf (buf, sizeof buf, "compensated-%gms", seconds * 1000.0);
    return std::string (buf);
}

bool parseArgs (int argc, char** argv, Options& options, std::string& error)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto needValue = [&] (std::string& target) -> bool
        {
            if (i + 1 >= argc)
            {
                error = "option " + arg + " requires a value";
                return false;
            }
            target = argv[++i];
            return true;
        };

        if (arg == "--corpus")
        {
            if (! needValue (options.corpus)) return false;
        }
        else if (arg == "--out")
        {
            if (! needValue (options.out)) return false;
        }
        else if (arg == "--backend")
        {
            if (! needValue (options.backend)) return false;
        }
        else if (arg == "--backend-lib")
        {
            if (! needValue (options.backendLib)) return false;
        }
        else if (arg == "--json-file")
        {
            if (! needValue (options.jsonFile)) return false;
        }
        else if (arg == "--summary-file")
        {
            if (! needValue (options.summaryFile)) return false;
        }
        else if (arg == "--csv-file")
        {
            if (! needValue (options.combinedCsvFile)) return false;
        }
        else if (arg == "--per-fixture-dir")
        {
            if (! needValue (options.perFixtureDirName)) return false;
        }
        else if (arg == "--compensate-latency")
        {
            std::string value;
            if (! needValue (value)) return false;
            char* end = nullptr;
            const double seconds = std::strtod (value.c_str(), &end);
            if (end == nullptr || *end != '\0' || ! (seconds >= 0.0) || seconds > 5.0)
            {
                error = "--compensate-latency must be seconds in [0, 5]";
                return false;
            }
            if (seconds != 0.0)
                options.latencyCompensations.push_back (seconds);
        }
        else if (arg == "--block")
        {
            std::string value;
            if (! needValue (value)) return false;
            options.blockFrames = static_cast<std::size_t> (
                std::strtoul (value.c_str(), nullptr, 10));
            if (options.blockFrames == 0)
            {
                error = "--block must be a positive integer";
                return false;
            }
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout <<
                "usage: rhythm-eval --corpus <dir> --out <dir>\n"
                "                   [--backend synthetic-ideal|synthetic-degraded]\n"
                "                   [--backend-lib <shared-library>] [--block <frames>]\n"
                "                   [--compensate-latency <seconds>]  (repeatable)\n"
                "                   [--json-file <name>] [--summary-file <name>]\n"
                "                   [--csv-file <name>] [--per-fixture-dir <name>]\n"
                "\n"
                "Latency compensation is applied to predicted beat times before scoring\n"
                "and defaults to off. Pass it once per value to score several settings in\n"
                "the same run; the uncompensated result is always included.\n";
            std::exit (0);
        }
        else
        {
            error = "unknown option: " + arg;
            return false;
        }
    }

    if (options.out.empty())
    {
        error = "--out <dir> is required";
        return false;
    }
    return true;
}

bool writeFile (const std::string& path, const std::string& contents, std::string& error)
{
    std::ofstream out (path.c_str(), std::ios::binary);
    if (! out.good())
    {
        error = "could not write " + path;
        return false;
    }
    out << contents;
    if (! out.good())
    {
        error = "failed while writing " + path;
        return false;
    }
    return true;
}

} // namespace
} // namespace rhythmeval

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main (int argc, char** argv)
{
    using namespace rhythmeval;

    Options options;
    std::string error;
    if (! parseArgs (argc, argv, options, error))
    {
        std::cerr << "rhythm-eval: " << error << "\n";
        return 2;
    }

    Manifest manifest;
    try
    {
        manifest = readManifestFile (options.corpus + "/manifest.json");
    }
    catch (const std::exception& e)
    {
        std::cerr << "rhythm-eval: " << e.what() << "\n";
        return 2;
    }

    // Load the real backend once, when requested. The factory convention is
    // documented in task-notes/EVAL-002.md.
    LoadedBackend realBackend;
    std::string backendId = options.backend;
#if RHYTHM_EVAL_HAVE_DLOPEN
    if (! options.backendLib.empty())
    {
        void* lib = ::dlopen (options.backendLib.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (lib == nullptr)
        {
            std::cerr << "rhythm-eval: dlopen failed for " << options.backendLib
                      << ": " << ::dlerror() << "\n";
            return 2;
        }
        CreateFn create = loadSymbol<CreateFn> (lib, "jam_rhythm_create");
        DestroyFn destroy = loadSymbol<DestroyFn> (lib, "jam_rhythm_destroy");
        if (create == nullptr || destroy == nullptr)
        {
            std::cerr << "rhythm-eval: " << options.backendLib
                      << " must export jam_rhythm_create and jam_rhythm_destroy\n";
            ::dlclose (lib);
            return 2;
        }
        realBackend.library = lib;
        realBackend.raw = create();
        realBackend.destroy = destroy;
        if (realBackend.raw == nullptr)
        {
            std::cerr << "rhythm-eval: backend factory returned null\n";
            return 2;
        }
        if (options.backend == "synthetic-ideal")
            backendId = "library";   // caller did not name it
    }
#else
    if (! options.backendLib.empty())
    {
        std::cerr << "rhythm-eval: --backend-lib is not supported on this platform\n";
        return 2;
    }
#endif

    BackendRunner runner (options.blockFrames);

    // Variants: the uncompensated run is always present; each requested
    // compensation adds one more. Every variant scores the SAME observation
    // series, so the backend runs once per fixture and only the scoring clock
    // changes. That is what makes the latency effect measurable rather than
    // asserted: same evidence, one explicit knob.
    std::vector<ScoringVariant> variants;
    {
        ScoringVariant base;
        base.label = "uncompensated";
        base.latencyCompensationSeconds = 0.0;
        variants.push_back (std::move (base));
        for (const double seconds : options.latencyCompensations)
        {
            ScoringVariant v;
            v.label = compensationLabel (seconds);
            v.latencyCompensationSeconds = seconds;
            variants.push_back (std::move (v));
        }
    }

    bool hadError = false;
    std::string writeError;

    for (const ManifestFixture& fixture : manifest.fixtures)
    {
        const RhythmTruth truth = toTruth (fixture);
        const std::string wavPath = options.corpus + "/" + fixture.file;

        WavData audio;
        try
        {
            audio = readWavMono16 (wavPath);
        }
        catch (const std::exception& e)
        {
            std::cerr << "rhythm-eval: " << e.what() << "\n";
            hadError = true;
            continue;
        }

        std::unique_ptr<jam::IRhythmTracker> synthetic;
        jam::IRhythmTracker* backend = realBackend.raw;
        if (backend == nullptr)
        {
            if (backendId == "synthetic-degraded" || backendId == "degraded")
                synthetic.reset (new SyntheticDegradedBackend (truth));
            else
                synthetic.reset (new SyntheticIdealBackend (truth));
            backend = synthetic.get();
        }

        ObservationSeries series;
        const std::size_t before = g_allocationCount.load (std::memory_order_relaxed);
        try
        {
            series = runner.run (*backend, audio);
        }
        catch (const std::exception& e)
        {
            std::cerr << "rhythm-eval: backend failed on " << fixture.name
                      << ": " << e.what() << "\n";
            hadError = true;
            continue;
        }
        series.allocationCount =
            g_allocationCount.load (std::memory_order_relaxed) - before;

        for (ScoringVariant& v : variants)
            v.fixtures.push_back (scoreFixture (truth, series,
                                                manifest.beatToleranceSeconds,
                                                v.latencyCompensationSeconds));
    }

    for (ScoringVariant& v : variants)
        v.aggregate = aggregateFixtures (v.fixtures);

    // --- write outputs ------------------------------------------------------
    const std::string perFixtureDir = options.out + "/" + options.perFixtureDirName;
    const std::string resultsPath = options.out + "/" + options.jsonFile;
    const std::string summaryPath = options.out + "/" + options.summaryFile;
    const std::string combinedCsvPath = options.out + "/" + options.combinedCsvFile;

    std::error_code ec;
    std::filesystem::create_directories (options.out, ec);
    std::filesystem::create_directories (perFixtureDir, ec);
    if (ec)
        std::cerr << "rhythm-eval: warning: could not create output directories: "
                  << ec.message() << "\n";

    {
        rhythmjson::Value root = rhythmjson::Value::makeObject();
        root.set ("schemaVersion", rhythmjson::Value::makeNumber (2));
        root.set ("backend", rhythmjson::Value::makeString (backendId));
        root.set ("blockFrames", rhythmjson::Value::makeNumber (
                                     static_cast<double> (options.blockFrames)));
        root.set ("beatToleranceSeconds", rhythmjson::Value::makeNumber (
                                             manifest.beatToleranceSeconds));
        root.set ("latencyCompensationAppliedToPredictedBeats",
                  rhythmjson::Value::makeBool (true));

        rhythmjson::Value corpus = rhythmjson::Value::makeObject();
        corpus.set ("id", rhythmjson::Value::makeString (manifest.corpusId));
        corpus.set ("fixtureCount", rhythmjson::Value::makeNumber (
                                        static_cast<double> (manifest.fixtures.size())));
        root.set ("corpus", corpus);

        rhythmjson::Value variantArray = rhythmjson::Value::makeArray();
        for (const ScoringVariant& v : variants)
            variantArray.push (scoringVariantToJson (v));
        root.set ("variants", variantArray);

        // The uncompensated variant is repeated under flat top-level keys so a
        // simple consumer does not have to know about variants; the authoritative
        // full record is `variants`.
        if (! variants.empty())
        {
            rhythmjson::Value fixtureArray = rhythmjson::Value::makeArray();
            for (const FixtureMetrics& m : variants.front().fixtures)
                fixtureArray.push (fixtureMetricsToJson (m));
            root.set ("fixtures", fixtureArray);
            root.set ("aggregate", aggregateMetricsToJson (variants.front().aggregate));
        }

        if (! writeFile (resultsPath, root.dump() + "\n", writeError))
        {
            std::cerr << "rhythm-eval: " << writeError << "\n";
            hadError = true;
        }
    }

    std::string combinedCsv = fixtureMetricsCsvHeaderWithVariant();
    const std::size_t fixtureCount =
        variants.empty() ? 0 : variants.front().fixtures.size();
    for (std::size_t i = 0; i < fixtureCount; ++i)
    {
        const std::string name = variants.front().fixtures[i].name;

        rhythmjson::Value one = rhythmjson::Value::makeObject();
        one.set ("name", rhythmjson::Value::makeString (name));
        rhythmjson::Value variantArray = rhythmjson::Value::makeArray();
        std::string fixtureCsv = fixtureMetricsCsvHeaderWithVariant();
        for (const ScoringVariant& v : variants)
        {
            if (i >= v.fixtures.size())
                continue;
            const FixtureMetrics& m = v.fixtures[i];
            combinedCsv += fixtureMetricsCsvRowWithVariant (v, m);
            fixtureCsv += fixtureMetricsCsvRowWithVariant (v, m);

            rhythmjson::Value entry = rhythmjson::Value::makeObject();
            entry.set ("label", rhythmjson::Value::makeString (v.label));
            entry.set ("latencyCompensationSeconds",
                       rhythmjson::Value::makeNumber (v.latencyCompensationSeconds));
            entry.set ("metrics", fixtureMetricsToJson (m));
            variantArray.push (entry);
        }
        one.set ("variants", variantArray);

        if (! writeFile (perFixtureDir + "/" + name + ".json",
                         one.dump() + "\n", writeError))
        {
            std::cerr << "rhythm-eval: " << writeError << "\n";
            hadError = true;
        }
        if (! writeFile (perFixtureDir + "/" + name + ".csv", fixtureCsv, writeError))
        {
            std::cerr << "rhythm-eval: " << writeError << "\n";
            hadError = true;
        }
    }
    if (! writeFile (combinedCsvPath, combinedCsv, writeError))
    {
        std::cerr << "rhythm-eval: " << writeError << "\n";
        hadError = true;
    }

    const std::string md = markdownSummary (backendId, manifest.corpusId,
                                            manifest.beatToleranceSeconds,
                                            variants);
    if (! writeFile (summaryPath, md, writeError))
    {
        std::cerr << "rhythm-eval: " << writeError << "\n";
        hadError = true;
    }

    std::cout << md;
    std::cout << "wrote " << resultsPath << ", " << combinedCsvPath << ", "
              << summaryPath << ", and per-fixture JSON/CSV in "
              << perFixtureDir << "\n";

    return hadError ? 1 : 0;
}
