// tracker-diagnostics — JUCE-free offline causal-trace diagnostic (TRACK-004).
//
// It drives a real IRhythmTracker backend over one fixture at a time and records
// the full per-block causal trace (EVAL-004-style event time AND causal
// availability), replays the EVAL-002/004 acquisition rule against that trace,
// and writes bounded JSON/CSV artifacts. It never scores a new metric, tunes a
// gate, selects a tracker or touches src/, vendor/, CMake or the shared harness.
//
// Modes:
//   corpus (default)  --corpus <dir> --out <dir> --backend-lib <so> --block <n>
//   BPM sweep         --click-sweep 118,120,...,134 --synthetic-rate <rate>
//   grid arithmetic   --grid-out <file>
//
// The backend is loaded by dlopen() through the existing
// jam_rhythm_create()/jam_rhythm_destroy() plugin convention, so this tool
// binary stays free of GPL code (SPEC.md section 25.6). Config experiments use a
// separate shim from tools/tracker-diagnostics and are labelled NOT default
// evidence in the provenance.

#include "AcquisitionReplay.h"
#include "BackendRunner.h"
#include "BtrackGrid.h"
#include "Json.h"
#include "Manifest.h"
#include "Metrics.h"
#include "SyntheticClick.h"
#include "TraceRunner.h"

#include "jam/IRhythmTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <dlfcn.h>
#  define DIAG_HAVE_DLOPEN 1
#else
#  define DIAG_HAVE_DLOPEN 0
#endif

using rhythmeval::Manifest;
using rhythmeval::ManifestFixture;
using rhythmeval::ObservationSeries;
using rhythmeval::RhythmTruth;
using tracker_diag::AcquireReason;
using tracker_diag::FixtureDiagnosis;
using tracker_diag::LockRun;

namespace
{

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

struct Options
{
    std::string corpus = "testdata/rhythm";
    std::string out;
    std::string backend = "btrack";
    std::string backendLib;
    std::size_t blockFrames = 128;
    std::size_t stateMaxRows = 700;
    std::string traceFiles = "all";   // all | beats | none
    std::string label;            // provenance label for the provenance file
    bool variantNotDefault = false;

    std::vector<double> clickSweep;
    double syntheticRate = 48000.0;
    double syntheticSeconds = 12.0;
    std::string gridOut;
    bool gridOnly = false;

    bool corpusMode = true;
};

bool parseNumberList (const std::string& text, std::vector<double>& out)
{
    std::stringstream ss (text);
    std::string item;
    while (std::getline (ss, item, ','))
    {
        if (item.empty())
            continue;
        char* end = nullptr;
        const double v = std::strtod (item.c_str(), &end);
        if (end == nullptr || *end != '\0' || ! std::isfinite (v))
            return false;
        out.push_back (v);
    }
    return ! out.empty();
}

bool parseArgs (int argc, char** argv, Options& o, std::string& error)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto needValue = [&] (std::string& target) -> bool
        {
            if (i + 1 >= argc) { error = "option " + arg + " requires a value"; return false; }
            target = argv[++i];
            return true;
        };
        if (arg == "--corpus") { if (! needValue (o.corpus)) return false; }
        else if (arg == "--out") { if (! needValue (o.out)) return false; }
        else if (arg == "--backend") { if (! needValue (o.backend)) return false; }
        else if (arg == "--backend-lib") { if (! needValue (o.backendLib)) return false; }
        else if (arg == "--label") { if (! needValue (o.label)) return false; }
        else if (arg == "--trace-files")
        {
            if (! needValue (o.traceFiles)) return false;
            if (o.traceFiles != "all" && o.traceFiles != "beats" && o.traceFiles != "none")
            { error = "--trace-files must be all|beats|none"; return false; }
        }
        else if (arg == "--variant-not-default") { o.variantNotDefault = true; }
        else if (arg == "--grid-out") { if (! needValue (o.gridOut)) return false; }
        else if (arg == "--grid-only") { o.gridOnly = true; o.corpusMode = false; }
        else if (arg == "--block")
        {
            std::string v; if (! needValue (v)) return false;
            o.blockFrames = static_cast<std::size_t> (std::strtoul (v.c_str(), nullptr, 10));
            if (o.blockFrames == 0) { error = "--block must be positive"; return false; }
        }
        else if (arg == "--state-max-rows")
        {
            std::string v; if (! needValue (v)) return false;
            o.stateMaxRows = static_cast<std::size_t> (std::strtoul (v.c_str(), nullptr, 10));
            if (o.stateMaxRows == 0) o.stateMaxRows = 1;
        }
        else if (arg == "--click-sweep")
        {
            std::string v; if (! needValue (v)) return false;
            if (! parseNumberList (v, o.clickSweep)) { error = "--click-sweep is a comma list of BPM"; return false; }
            o.corpusMode = false;
        }
        else if (arg == "--synthetic-rate")
        {
            std::string v; if (! needValue (v)) return false;
            o.syntheticRate = std::strtod (v.c_str(), nullptr);
        }
        else if (arg == "--synthetic-seconds")
        {
            std::string v; if (! needValue (v)) return false;
            o.syntheticSeconds = std::strtod (v.c_str(), nullptr);
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout <<
                "usage: tracker-diagnostics --out <dir> [options]\n"
                "  corpus:  --corpus <dir> --backend <name> --backend-lib <so> --block <n>\n"
                "  bpm:     --click-sweep <bpm,...> --synthetic-rate <hz> [--synthetic-seconds <s>]\n"
                "  grid:    --grid-out <file>\n"
                "  provenance: --label <text> [--variant-not-default]\n"
                "  trace files: --trace-files all|beats|none\n"
                "  state trace cap: --state-max-rows <n>\n";
            std::exit (0);
        }
        else { error = "unknown option: " + arg; return false; }
    }
    if (o.out.empty()) { error = "--out <dir> is required"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Backend loading (same convention as tools/rhythm-eval/main.cpp)
// ---------------------------------------------------------------------------

using CreateFn = jam::IRhythmTracker* (*)();
using DestroyFn = void (*) (jam::IRhythmTracker*);

template <typename Fn>
Fn loadSymbol (void* library, const char* name)
{
    void* symbol = ::dlsym (library, name);
    Fn fn = nullptr;
    static_assert (sizeof (Fn) == sizeof (symbol), "size mismatch");
    std::memcpy (&fn, &symbol, sizeof (fn));
    return fn;
}

struct BackendLibrary
{
    void* handle = nullptr;
    CreateFn create = nullptr;
    DestroyFn destroy = nullptr;

    ~BackendLibrary()
    {
#if DIAG_HAVE_DLOPEN
        if (handle != nullptr && destroy != nullptr)
        {
            // Individual instances are destroyed by their owners; closing the
            // handle after all instances are gone is handled by process exit.
            ::dlclose (handle);
        }
#endif
    }

    jam::IRhythmTracker* make() const { return create ? create() : nullptr; }
};

bool loadBackend (const std::string& path, BackendLibrary& lib, std::string& error)
{
#if DIAG_HAVE_DLOPEN
    void* h = ::dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) { error = "dlopen failed for " + path + ": " + ::dlerror(); return false; }
    lib.handle = h;
    lib.create = loadSymbol<CreateFn> (h, "jam_rhythm_create");
    lib.destroy = loadSymbol<DestroyFn> (h, "jam_rhythm_destroy");
    if (lib.create == nullptr || lib.destroy == nullptr)
    {
        error = path + " must export jam_rhythm_create and jam_rhythm_destroy";
        return false;
    }
    return true;
#else
    (void) path; (void) lib;
    error = "dlopen is not supported on this platform";
    return false;
#endif
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

bool writeFile (const std::string& path, const std::string& contents, std::string& error)
{
    std::ofstream out (path.c_str(), std::ios::binary);
    if (! out.good()) { error = "could not write " + path; return false; }
    out << contents;
    if (! out.good()) { error = "failed while writing " + path; return false; }
    return true;
}

std::string num (double v)
{
    char buf[40];
    std::snprintf (buf, sizeof buf, "%.10g", std::isfinite (v) ? v : 0.0);
    return std::string (buf);
}

// ---------------------------------------------------------------------------
// Per-fixture trace artifacts
// ---------------------------------------------------------------------------

std::string beatsCsv (const tracker_diag::TraceResult& tr)
{
    std::string s = "beatIndex,blockIndex,eventSeconds,availabilitySeconds,latencySeconds,"
                    "reportedSample,blockStartSeconds\n";
    const ObservationSeries& series = tr.series;
    std::size_t beatIndex = 0;
    for (std::size_t b = 0; b < tr.blocks.size(); ++b)
    {
        const jam::RhythmObservation& obs = tr.blocks[b].observation;
        if (! obs.beatEvent)
            continue;
        // Find this beat's series entry (order-preserving).
        const double event = (beatIndex < series.beatTimesSeconds.size())
                                 ? series.beatTimesSeconds[beatIndex] : 0.0;
        const double avail = (beatIndex < series.beatAvailabilitySeconds.size())
                                 ? series.beatAvailabilitySeconds[beatIndex]
                                 : tr.blocks[b].blockEndSeconds;
        char buf[220];
        std::snprintf (buf, sizeof buf, "%zu,%zu,%.9f,%.9f,%.9f,%llu,%.9f\n",
                       beatIndex, b, event, avail, avail - event,
                       static_cast<unsigned long long> (obs.inputSampleTime),
                       tr.blocks[b].blockStartSeconds);
        s += buf;
        ++beatIndex;
    }
    return s;
}

std::string stateCsv (const tracker_diag::TraceResult& tr, std::size_t maxRows)
{
    const std::size_t n = tr.blocks.size();
    const std::size_t stride = std::max<std::size_t> (1, (n + maxRows - 1) / maxRows);
    std::string s = "blockIndex,startSeconds,endSeconds,bpm,phaseValid,silence,"
                    "confidence,onsetStrength,energyRmsDbfs,beatEvent,reportedSample\n";
    bool previousSilence = false;
    for (std::size_t b = 0; b < n; ++b)
    {
        const jam::RhythmObservation& obs = tr.blocks[b].observation;
        const bool keyRow = obs.beatEvent
                            || obs.silence != previousSilence
                            || b == 0 || b + 1 == n
                            || (n > maxRows && b % stride == 0);
        previousSilence = obs.silence;
        if (! keyRow)
            continue;
        char buf[240];
        std::snprintf (buf, sizeof buf,
                       "%zu,%.9f,%.9f,%.6f,%d,%d,%.6f,%.6f,%.3f,%d,%llu\n",
                       b, tr.blocks[b].blockStartSeconds, tr.blocks[b].blockEndSeconds,
                       static_cast<double> (obs.bpmCandidate),
                       obs.phaseValid ? 1 : 0, obs.silence ? 1 : 0,
                       static_cast<double> (obs.beatConfidence01),
                       static_cast<double> (obs.onsetStrength01),
                       static_cast<double> (obs.energyRmsDbfs),
                       obs.beatEvent ? 1 : 0,
                       static_cast<unsigned long long> (obs.inputSampleTime));
        s += buf;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Run one backend over one audio buffer
// ---------------------------------------------------------------------------

struct FixtureRun
{
    RhythmTruth truth;
    ObservationSeries series;
    rhythmeval::FixtureMetrics metrics;
    FixtureDiagnosis diagnosis;
    tracker_diag::TraceResult trace;
};

void scoreOne (FixtureRun& fr, double tol)
{
    fr.metrics = rhythmeval::scoreFixture (fr.truth, fr.series, tol, 0.0);
    fr.diagnosis = tracker_diag::diagnoseFixture (fr.truth, fr.series, fr.metrics, tol);
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main (int argc, char** argv)
{
    Options options;
    std::string error;
    if (! parseArgs (argc, argv, options, error))
    {
        std::cerr << "tracker-diagnostics: " << error << "\n";
        return 2;
    }

    std::error_code ec;
    std::filesystem::create_directories (options.out, ec);
    if (ec) { std::cerr << "tracker-diagnostics: cannot create " << options.out << "\n"; return 2; }

    // Grid arithmetic is emitted independent of any backend.
    if (! options.gridOut.empty())
    {
        if (! writeFile (options.gridOut, tracker_diag::btrackGridCsv(), error))
        { std::cerr << "tracker-diagnostics: " << error << "\n"; return 2; }
    }
    if (options.gridOnly)
        return 0;

    BackendLibrary lib;
    if (! options.backendLib.empty())
    {
        if (! loadBackend (options.backendLib, lib, error))
        { std::cerr << "tracker-diagnostics: " << error << "\n"; return 2; }
    }
    else
    {
        std::cerr << "tracker-diagnostics: --backend-lib is required\n";
        return 2;
    }

    // --- BPM sweep mode -----------------------------------------------------
    if (! options.corpusMode)
    {
        if (options.clickSweep.empty())
        { std::cerr << "tracker-diagnostics: no --click-sweep values\n"; return 2; }
        std::string csv = "bpmRequested,reportedBpmLast,reportedBpmMedian,nBeats,"
                          "meanBeatIntervalSeconds,truthIntervalSeconds,intervalRatio,bpmRelError\n";
        for (const double bpm : options.clickSweep)
        {
            tracker_diag::ClickSpec spec;
            spec.bpm = bpm;
            spec.sampleRate = options.syntheticRate;
            spec.seconds = options.syntheticSeconds;
            const rhythmeval::WavData audio = tracker_diag::makeClickTrain (spec);

            std::unique_ptr<jam::IRhythmTracker> backend (lib.make());
            if (backend == nullptr) { std::cerr << "factory returned null\n"; return 2; }
            tracker_diag::TraceResult tr =
                tracker_diag::runTrace (*backend, audio, options.blockFrames);

            std::vector<double> bpms;
            for (const rhythmeval::TempoSample& s : tr.series.tempoSamples)
                if (s.phaseValid && s.bpm > 0.0) bpms.push_back (s.bpm);
            std::vector<double> sorted = bpms;
            std::sort (sorted.begin(), sorted.end());
            const double medianBpm = sorted.empty() ? 0.0
                                      : sorted[sorted.size() / 2];
            const double lastBpm = bpms.empty() ? 0.0 : bpms.back();

            double intervalSum = 0.0; int n = 0;
            for (std::size_t i = 0; i + 1 < tr.series.beatTimesSeconds.size(); ++i)
            {
                const double gap = tr.series.beatTimesSeconds[i + 1]
                                   - tr.series.beatTimesSeconds[i];
                if (gap > 0.0) { intervalSum += gap; ++n; }
            }
            const double meanInterval = n > 0 ? intervalSum / n : 0.0;
            const double truthInterval = 60.0 / bpm;
            const double ratio = truthInterval > 0.0 && meanInterval > 0.0
                                     ? meanInterval / truthInterval : 0.0;
            char buf[260];
            std::snprintf (buf, sizeof buf, "%.6f,%.6f,%.6f,%d,%.9f,%.9f,%.9f,%.9f\n",
                           bpm, lastBpm, medianBpm,
                           static_cast<int> (tr.series.beatTimesSeconds.size()),
                           meanInterval, truthInterval, ratio,
                           bpm > 0.0 ? std::fabs (lastBpm - bpm) / bpm : 0.0);
            csv += buf;
        }
        const std::string path = options.out + "/click_bpm_sweep_rate"
                                 + num (options.syntheticRate) + ".csv";
        if (! writeFile (path, csv, error))
        { std::cerr << "tracker-diagnostics: " << error << "\n"; return 2; }
        std::cout << "wrote " << path << "\n";
        return 0;
    }

    // --- corpus mode --------------------------------------------------------
    Manifest manifest;
    try { manifest = rhythmeval::readManifestFile (options.corpus + "/manifest.json"); }
    catch (const std::exception& e)
    { std::cerr << "tracker-diagnostics: " << e.what() << "\n"; return 2; }

    const std::string beatsDir = options.out + "/beats";
    const std::string stateDir = options.out + "/state";
    std::filesystem::create_directories (beatsDir, ec);
    std::filesystem::create_directories (stateDir, ec);

    const double tol = manifest.beatToleranceSeconds;
    std::vector<FixtureRun> runs;
    std::vector<rhythmeval::FixtureMetrics> metricsList;
    bool hadError = false;

    for (const ManifestFixture& fx : manifest.fixtures)
    {
        FixtureRun fr;
        fr.truth = rhythmeval::toTruth (fx);
        rhythmeval::WavData audio;
        try
        {
            audio = rhythmeval::readWavMono16 (options.corpus + "/" + fx.file);
        }
        catch (const std::exception& e)
        { std::cerr << "tracker-diagnostics: " << e.what() << "\n"; hadError = true; continue; }

        std::unique_ptr<jam::IRhythmTracker> backend (lib.make());
        if (backend == nullptr) { std::cerr << "factory returned null\n"; return 2; }
        fr.trace = tracker_diag::runTrace (*backend, audio, options.blockFrames);
        fr.series = fr.trace.series;
        scoreOne (fr, tol);

        if (options.traceFiles != "none")
        {
            if (! writeFile (beatsDir + "/" + fr.truth.name + ".csv",
                             beatsCsv (fr.trace), error))
            { std::cerr << "tracker-diagnostics: " << error << "\n"; hadError = true; }
        }
        if (options.traceFiles == "all")
        {
            if (! writeFile (stateDir + "/" + fr.truth.name + ".csv",
                             stateCsv (fr.trace, options.stateMaxRows), error))
            { std::cerr << "tracker-diagnostics: " << error << "\n"; hadError = true; }
        }

        metricsList.push_back (fr.metrics);
        runs.push_back (std::move (fr));
    }

    // --- fixtures.csv (per-fixture diagnosis + replay timestamps) ----------
    std::string fx = "name,core,steady,ramp,nominalBpm,predictedBeats,truthBeats,matchedBeats,"
                     "scorerAcquired,scorerAcqBars,scorerAcqSeconds,replayAcquired,"
                     "agrees,lockStartEvent,lockStartAvail,lockConfirmEvent,lockConfirmAvail,"
                     "lockAcqBars,longestMatchRun,longestTempoRun,medianBpm,medianBpmError,"
                     "bpmAgreeFrac,ratioToTruth,octaveSuspect,silenceBlocks,silenceSeconds,"
                     "meanSignedPhaseMs,meanAbsPhaseMs,reason,reasonDetail\n";
    for (const FixtureRun& fr : runs)
    {
        const FixtureDiagnosis& d = fr.diagnosis;
        char buf[900];
        std::snprintf (buf, sizeof buf,
            "%s,%d,%d,%d,%s,%d,%d,%d,%d,%.6f,%.6f,%d,%d,%.9f,%.9f,%.9f,%.9f,%.6f,%zu,%zu,"
            "%.6f,%.6f,%.6f,%.6f,%d,%d,%.6f,%.6f,%.6f,%s,\"%s\"\n",
            d.name.c_str(), d.core ? 1 : 0, d.steady ? 1 : 0, d.ramp ? 1 : 0,
            num (d.nominalBpm).c_str(), d.predictedBeats, d.truthBeats, d.matchedBeats,
            d.scorerAcquired ? 1 : 0, d.scorerAcquisitionBars, d.scorerAcquisitionSeconds,
            d.lock.found ? 1 : 0, d.agreesWithScorer ? 1 : 0,
            d.lock.startEventSeconds, d.lock.startAvailabilitySeconds,
            d.lock.confirmEventSeconds, d.lock.confirmAvailabilitySeconds,
            d.lock.acquisitionBars, d.lock.longestMatchRun, d.lock.longestRunWithTempo,
            d.medianBpm, d.medianBpmError, d.bpmAgreementFractionInWindow, d.ratioToTruth,
            d.octaveSuspect ? 1 : 0, d.silenceBlocks, d.silenceSeconds,
            d.meanSignedPhaseMs, d.meanAbsPhaseMs,
            d.primaryReason().c_str(), d.reasonDetail.c_str());
        fx += buf;
    }

    // --- acquisition.json (lock start/confirmation per fixture) ------------
    rhythmjson::Value acqRoot = rhythmjson::Value::makeObject();
    acqRoot.set ("schemaVersion", rhythmjson::Value::makeNumber (1));
    acqRoot.set ("backend", rhythmjson::Value::makeString (options.backend));
    acqRoot.set ("blockFrames", rhythmjson::Value::makeNumber (
                                   static_cast<double> (options.blockFrames)));
    rhythmjson::Value acqArray = rhythmjson::Value::makeArray();
    for (const FixtureRun& fr : runs)
    {
        const FixtureDiagnosis& d = fr.diagnosis;
        rhythmjson::Value o = rhythmjson::Value::makeObject();
        o.set ("name", rhythmjson::Value::makeString (d.name));
        o.set ("core", rhythmjson::Value::makeBool (d.core));
        o.set ("scorerAcquired", rhythmjson::Value::makeBool (d.scorerAcquired));
        o.set ("scorerAcquisitionSeconds", rhythmjson::Value::makeNumber (d.scorerAcquisitionSeconds));
        o.set ("scorerAcquisitionBars", rhythmjson::Value::makeNumber (d.scorerAcquisitionBars));
        o.set ("replayAcquired", rhythmjson::Value::makeBool (d.lock.found));
        o.set ("agreesWithScorer", rhythmjson::Value::makeBool (d.agreesWithScorer));
        o.set ("startPredIndex", rhythmjson::Value::makeNumber (
                                     static_cast<double> (d.lock.startPredIndex)));
        o.set ("confirmPredIndex", rhythmjson::Value::makeNumber (
                                       static_cast<double> (d.lock.confirmPredIndex)));
        o.set ("truthIndex", rhythmjson::Value::makeNumber (
                                 static_cast<double> (d.lock.truthIndex)));
        o.set ("lockStartEventSeconds", rhythmjson::Value::makeNumber (d.lock.startEventSeconds));
        o.set ("lockStartAvailabilitySeconds", rhythmjson::Value::makeNumber (d.lock.startAvailabilitySeconds));
        o.set ("lockConfirmEventSeconds", rhythmjson::Value::makeNumber (d.lock.confirmEventSeconds));
        o.set ("lockConfirmAvailabilitySeconds", rhythmjson::Value::makeNumber (d.lock.confirmAvailabilitySeconds));
        o.set ("lockAcquisitionBars", rhythmjson::Value::makeNumber (d.lock.acquisitionBars));
        o.set ("longestMatchRun", rhythmjson::Value::makeNumber (
                                      static_cast<double> (d.lock.longestMatchRun)));
        o.set ("longestTempoRun", rhythmjson::Value::makeNumber (
                                      static_cast<double> (d.lock.longestRunWithTempo)));
        o.set ("recoveryFound", rhythmjson::Value::makeBool (d.recovery.found));
        o.set ("recoveryStartEventSeconds", rhythmjson::Value::makeNumber (d.recovery.startEventSeconds));
        o.set ("recoveryConfirmEventSeconds", rhythmjson::Value::makeNumber (d.recovery.confirmEventSeconds));
        o.set ("reason", rhythmjson::Value::makeString (d.primaryReason()));
        o.set ("reasonDetail", rhythmjson::Value::makeString (d.reasonDetail));
        acqArray.push (o);
    }
    acqRoot.set ("fixtures", acqArray);

    // --- summary.json (provenance + scorer aggregate cross-check) ----------
    const rhythmeval::AggregateMetrics agg = rhythmeval::aggregateFixtures (metricsList);
    rhythmjson::Value summary = rhythmjson::Value::makeObject();
    summary.set ("schemaVersion", rhythmjson::Value::makeNumber (1));
    summary.set ("tool", rhythmjson::Value::makeString ("tracker-diagnostics"));
    summary.set ("track004_label", rhythmjson::Value::makeString (
        options.label.empty() ? std::string ("unlabelled") : options.label));
    summary.set ("variantNotDefault", rhythmjson::Value::makeBool (options.variantNotDefault));
    summary.set ("backend", rhythmjson::Value::makeString (options.backend));
    summary.set ("backendLib", rhythmjson::Value::makeString (options.backendLib));
    summary.set ("blockFrames", rhythmjson::Value::makeNumber (
                                   static_cast<double> (options.blockFrames)));
    summary.set ("beatToleranceSeconds", rhythmjson::Value::makeNumber (tol));
    summary.set ("corpusId", rhythmjson::Value::makeString (manifest.corpusId));
    summary.set ("fixtureCount", rhythmjson::Value::makeNumber (
                                    static_cast<double> (runs.size())));
    summary.set ("eventTimeSemantics", rhythmjson::Value::makeString (
        "backend-reported device time when causal, else block start (EVAL-004)"));
    summary.set ("availabilitySemantics", rhythmjson::Value::makeString (
        "device time at which process() returned (block end)"));
    summary.set ("aggregate", rhythmeval::aggregateMetricsToJson (agg));
    if (! writeFile (options.out + "/summary.json", summary.dump() + "\n", error)
        || ! writeFile (options.out + "/fixtures.csv", fx, error)
        || ! writeFile (options.out + "/acquisition.json", acqRoot.dump() + "\n", error))
    { std::cerr << "tracker-diagnostics: " << error << "\n"; return 2; }

    std::cout << "tracker-diagnostics: " << options.backend << " block "
              << options.blockFrames << ": " << runs.size() << " fixtures; "
              << "acq core " << agg.acquisitionCoreWithin2Bars << "/"
              << agg.acquisitionCoreEvaluated << "; worst core BPM err "
              << agg.bpmRelErrorWorstCore << "\n";
    return hadError ? 1 : 0;
}
