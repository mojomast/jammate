// tempo-variant click CLI — deterministic click/tempo-step runs (TRACK-005).
//
// It dlopen()s the pinned btrack evaluation plugin through the existing
// jam_rhythm_create()/jam_rhythm_destroy() convention, drives it bare (base)
// and wrapped in the compiled-in TempoVariantTracker (variant), and writes
// bounded CSV trajectories. No tracker source is compiled into this binary.
//
// Modes:
//   --click-sweep  constant 24 s click at each requested BPM, per device rate
//   --click-step   24 s click with a phase-continuous tempo step 126 -> 132
//
// The method only reads intervals of beat events ALREADY emitted, so every
// sample below is causally available at its block end by construction.
//
// All numeric arguments are validated strictly and in full BEFORE any output
// directory, library load or audio allocation; a rejected argument exits 2.

#include "ClickTrain.h"
#include "MethodLog.h"
#include "TempoVariant.h"

#include "CliValidate.h"   // tracker_diag::parseBlockFrames (reused, unmodified)

#include "jam/IRhythmTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
#  define CMD_HAVE_DLOPEN 1
#else
#  define CMD_HAVE_DLOPEN 0
#endif

namespace
{

using CreateFn = jam::IRhythmTracker* (*)();
using DestroyFn = void (*) (jam::IRhythmTracker*);

// Bounded numeric windows for the CLI (not tuned to any corpus).
constexpr double kMinRate = 8000.0;
constexpr double kMaxRate = 192000.0;
constexpr double kMinCliBpm = 20.0;
constexpr double kMaxCliBpm = 400.0;
constexpr double kMaxSeconds = 120.0;
constexpr double kMaxSamples = 24.0e6;   // rate * seconds cap (~96 MB of float)

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
#if CMD_HAVE_DLOPEN
        if (handle != nullptr) ::dlclose (handle);
#endif
    }

    struct Deleter
    {
        DestroyFn destroy = nullptr;
        void operator() (jam::IRhythmTracker* p) const
        { if (p != nullptr && destroy != nullptr) destroy (p); }
    };

    using Ptr = std::unique_ptr<jam::IRhythmTracker, Deleter>;

    Ptr makeOwned() const { return Ptr (create ? create() : nullptr, Deleter {destroy}); }
};

bool loadBackend (const std::string& path, BackendLibrary& lib, std::string& error)
{
#if CMD_HAVE_DLOPEN
    void* h = ::dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) { error = std::string ("dlopen failed for ") + path + ": " + ::dlerror(); return false; }
    lib.handle = h;
    lib.create = loadSymbol<CreateFn> (h, "jam_rhythm_create");
    lib.destroy = loadSymbol<DestroyFn> (h, "jam_rhythm_destroy");
    if (lib.create == nullptr || lib.destroy == nullptr)
    { error = path + " lacks jam_rhythm_create/destroy"; return false; }
    return true;
#else
    (void) path; (void) lib; error = "dlopen unsupported"; return false;
#endif
}

/** Strict full-string finite double parse; rejects trailing junk, NaN and Inf. */
bool parseDoubleStrict (const std::string& text, double& out)
{
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod (text.c_str(), &end);
    if (end == nullptr || end == text.c_str() || *end != '\0') return false;
    if (! std::isfinite (v)) return false;
    out = v;
    return true;
}

bool inRange (double v, double lo, double hi) { return v >= lo && v <= hi; }

struct Row
{
    std::uint64_t block = 0;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    bool beat = false;
    std::uint64_t eventSample = 0;
    float bpm = 0.0f;
    bool phaseValid = false;
    bool silence = false;
};

/** Drives a backend with a fixed, validated block size. Returns false on an
    invalid frame parameter (fail closed, never an infinite loop). */
template <typename Backend>
bool runRows (Backend& backend, const rhythmeval::WavData& audio,
              std::size_t blockFrames, std::vector<Row>& rows)
{
    if (blockFrames == 0 || blockFrames > jam::kMaxAnalysisBlock)
        return false;
    backend.reset (audio.sampleRate);
    std::uint64_t blockIndex = 0;
    for (std::size_t first = 0; first < audio.frames; first += blockFrames)
    {
        const std::size_t remaining = audio.frames - first;
        const std::size_t count = remaining < blockFrames ? remaining : blockFrames;
        if (count > jam::kMaxAnalysisBlock)
            return false;

        jam::AnalysisFrame frame;
        frame.sampleTime = static_cast<std::uint64_t> (first);
        frame.sourceSampleRate = audio.sampleRate;
        frame.numSamples = static_cast<std::uint32_t> (count);
        for (std::size_t i = 0; i < count; ++i)
            frame.samples[i] = audio.samples[first + i];

        const jam::RhythmObservation obs = backend.process (frame);
        Row r;
        r.block = blockIndex++;
        r.startSeconds = static_cast<double> (frame.sampleTime) / audio.sampleRate;
        r.endSeconds = static_cast<double> (frame.sampleTime + count) / audio.sampleRate;
        r.beat = obs.beatEvent;
        r.eventSample = obs.inputSampleTime;
        r.bpm = obs.bpmCandidate;
        r.phaseValid = obs.phaseValid;
        r.silence = obs.silence;
        rows.push_back (r);
    }
    return true;
}

std::string label (double v)
{
    char buf[32]; std::snprintf (buf, sizeof buf, "%.10g", v);
    return std::string (buf);
}

/** Write and verify flush/close; returns false on any write failure (e.g.
    /dev/full), so the caller can fail closed. */
bool writeFile (const std::string& path, const std::string& contents)
{
    std::ofstream out (path.c_str(), std::ios::binary | std::ios::trunc);
    if (! out.good()) return false;
    out << contents;
    out.flush();
    if (! out.good()) return false;
    out.close();
    return out.good();
}

double medianOf (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

int main (int argc, char** argv)
{
    std::string backendLib, out, mode;
    std::string rateText, secondsText, blockText, sweepText;
    std::string stepFromText, stepToText, stepSecondsText;
    std::string error;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&] (std::string& target) -> bool
        {
            if (i + 1 >= argc) { error = "option " + arg + " requires a value"; return false; }
            target = argv[++i];
            return true;
        };
        if (arg == "--backend-lib") { if (! next (backendLib)) break; }
        else if (arg == "--out") { if (! next (out)) break; }
        else if (arg == "--mode") { if (! next (mode)) break; }
        else if (arg == "--rate") { if (! next (rateText)) break; }
        else if (arg == "--seconds") { if (! next (secondsText)) break; }
        else if (arg == "--block") { if (! next (blockText)) break; }
        else if (arg == "--sweep") { if (! next (sweepText)) break; }
        else if (arg == "--step-from") { if (! next (stepFromText)) break; }
        else if (arg == "--step-to") { if (! next (stepToText)) break; }
        else if (arg == "--step-seconds") { if (! next (stepSecondsText)) break; }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "usage: tempo-variant-click --backend-lib <so> --out <dir> "
                         "--mode sweep|step [--rate hz] [--block n]\n";
            return 0;
        }
        else { error = "unknown option: " + arg; break; }
    }
    if (! error.empty()) { std::cerr << "tempo-variant-click: " << error << "\n"; return 2; }

    // --- strict validation BEFORE any output dir, library load or allocation --
    if (backendLib.empty() || out.empty() || mode.empty())
    { std::cerr << "tempo-variant-click: --backend-lib, --out and --mode are required\n"; return 2; }
    if (mode != "sweep" && mode != "step")
    { std::cerr << "tempo-variant-click: --mode must be sweep|step\n"; return 2; }

    double rate = 48000.0, seconds = 24.0;
    std::size_t blockFrames = 128;
    if (! rateText.empty() && (! parseDoubleStrict (rateText, rate) || ! inRange (rate, kMinRate, kMaxRate)))
    { std::cerr << "tempo-variant-click: --rate must be finite in [8000,192000]\n"; return 2; }
    if (! secondsText.empty() && (! parseDoubleStrict (secondsText, seconds) || seconds <= 0.0 || seconds > kMaxSeconds))
    { std::cerr << "tempo-variant-click: --seconds must be finite in (0,120]\n"; return 2; }
    if (rate * seconds > kMaxSamples)
    { std::cerr << "tempo-variant-click: rate*seconds exceeds the bounded sample budget\n"; return 2; }
    if (! blockText.empty() && ! tracker_diag::parseBlockFrames (blockText, jam::kMaxAnalysisBlock, blockFrames))
    { std::cerr << "tempo-variant-click: --block must be an integer in [1,"
                   << jam::kMaxAnalysisBlock << "]\n"; return 2; }

    std::vector<double> sweep;
    if (mode == "sweep")
    {
        if (sweepText.empty())
            sweep = {118, 120, 122, 123, 124, 125, 126, 127, 128, 130, 132, 134};
        else
        {
            std::stringstream ss (sweepText);
            std::string item;
            while (std::getline (ss, item, ','))
            {
                if (item.empty()) continue;
                double v = 0.0;
                if (! parseDoubleStrict (item, v) || ! inRange (v, kMinCliBpm, kMaxCliBpm))
                { std::cerr << "tempo-variant-click: --sweep values must be finite in ["
                               << kMinCliBpm << "," << kMaxCliBpm << "]\n"; return 2; }
                sweep.push_back (v);
            }
            if (sweep.empty())
            { std::cerr << "tempo-variant-click: --sweep has no values\n"; return 2; }
        }
    }

    double stepFrom = 126.0, stepTo = 132.0, stepSeconds = 12.0;
    if (mode == "step")
    {
        if (! stepFromText.empty() && (! parseDoubleStrict (stepFromText, stepFrom) || ! inRange (stepFrom, kMinCliBpm, kMaxCliBpm)))
        { std::cerr << "tempo-variant-click: --step-from invalid\n"; return 2; }
        if (! stepToText.empty() && (! parseDoubleStrict (stepToText, stepTo) || ! inRange (stepTo, kMinCliBpm, kMaxCliBpm)))
        { std::cerr << "tempo-variant-click: --step-to invalid\n"; return 2; }
        if (! stepSecondsText.empty() && (! parseDoubleStrict (stepSecondsText, stepSeconds) || stepSeconds <= 0.0))
        { std::cerr << "tempo-variant-click: --step-seconds invalid\n"; return 2; }
        if (std::fabs (stepTo - stepFrom) < 1e-9)
        { std::cerr << "tempo-variant-click: --step-from and --step-to must differ\n"; return 2; }
        // The step must fall inside the rendered clip, strictly after the first
        // beat, so an anchor actually exists.
        if (! (stepSeconds > 0.1 && stepSeconds < seconds))
        { std::cerr << "tempo-variant-click: --step-seconds must be inside (0.1, seconds)\n"; return 2; }
    }

    // --- now it is safe to touch the filesystem / load the library ------------
    std::error_code ec;
    std::filesystem::create_directories (out, ec);
    if (ec) { std::cerr << "cannot create " << out << "\n"; return 2; }

    BackendLibrary lib;
    if (! loadBackend (backendLib, lib, error))
    { std::cerr << "tempo-variant-click: " << error << "\n"; return 2; }

    if (mode == "sweep")
    {
        std::string csv = "bpmRequested,baseBpmLast,baseBpmMedian,variantBpmLast,"
                          "variantBpmMedian,nBeats,meanIntervalSeconds,truthIntervalSeconds,"
                          "intervalRatio,variantRelError\n";
        for (double bpm : sweep)
        {
            tempo_variant::ClickSpec spec;
            spec.sampleRate = rate; spec.seconds = seconds; spec.bpm = bpm;
            const rhythmeval::WavData audio = tempo_variant::makeClickTrain (spec);

            std::vector<Row> baseRows;
            {
                BackendLibrary::Ptr base = lib.makeOwned();
                if (base == nullptr) { std::cerr << "factory returned null\n"; return 2; }
                if (! runRows (*base, audio, blockFrames, baseRows)) return 2;
            }
            std::vector<Row> varRows;
            {
                BackendLibrary::Ptr inner = lib.makeOwned();
                if (inner == nullptr) { std::cerr << "factory returned null\n"; return 2; }
                tempo_variant::TempoVariantTracker variant (std::move (inner), {});
                if (! runRows (variant, audio, blockFrames, varRows)) return 2;
            }

            auto stats = [] (const std::vector<Row>& rows, std::vector<double>& bpms,
                             double& meanIntervalSamples, int& nBeats)
            {
                for (const Row& r : rows)
                    if (r.phaseValid && r.bpm > 0.0f) bpms.push_back (r.bpm);
                double sum = 0.0; int n = 0;
                double prev = -1.0;
                for (const Row& r : rows)
                {
                    if (! r.beat) continue;
                    ++nBeats;
                    const double ev = static_cast<double> (r.eventSample);
                    if (prev >= 0.0) { sum += (ev - prev); ++n; }
                    prev = ev;
                }
                meanIntervalSamples = (n > 0) ? sum / n : 0.0;
            };
            std::vector<double> baseBpms, varBpms;
            double baseIntervalSamples = 0.0, varIntervalSamples = 0.0;
            int baseBeats = 0, varBeats = 0;
            stats (baseRows, baseBpms, baseIntervalSamples, baseBeats);
            stats (varRows, varBpms, varIntervalSamples, varBeats);
            (void) baseIntervalSamples; (void) baseBeats;
            const double meanInterval = varIntervalSamples / rate;
            const double truthInterval = 60.0 / bpm;
            const double ratio = truthInterval > 0.0 && meanInterval > 0.0
                                     ? meanInterval / truthInterval : 0.0;
            const double varLast = varBpms.empty() ? 0.0 : varBpms.back();
            char buf[320];
            std::snprintf (buf, sizeof buf, "%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.9f,%.9f,%.9f,%.9f\n",
                           bpm,
                           baseBpms.empty() ? 0.0 : baseBpms.back(), medianOf (baseBpms),
                           varLast, medianOf (varBpms),
                           varBeats, meanInterval, truthInterval, ratio,
                           bpm > 0.0 ? std::fabs (varLast - bpm) / bpm : 0.0);
            csv += buf;
        }
        const std::string path = out + "/click_sweep_rate" + label (rate) + ".csv";
        if (! writeFile (path, csv)) { std::cerr << "write failed " << path << "\n"; return 2; }
        std::cout << "wrote " << path << "\n";
        return 0;
    }

    // mode == "step"
    {
        tempo_variant::ClickSpec spec;
        spec.sampleRate = rate; spec.seconds = seconds;
        spec.bpm = stepFrom; spec.stepToBpm = stepTo; spec.stepSeconds = stepSeconds;
        const rhythmeval::WavData audio = tempo_variant::makeClickTrain (spec);

        const std::string path = out + "/click_step_rate" + label (rate) + ".csv";
        std::ofstream methodOut (path.c_str(), std::ios::binary | std::ios::trunc);
        if (! methodOut.good()) { std::cerr << "cannot open " << path << "\n"; return 2; }
        methodOut << "blockIndex,blockStartSeconds,blockEndSeconds,beatEvent,"
                     "eventSeconds,intervalMeasured,intervalSeconds,intervalState,"
                     "ringCount,ready,baseBpm,variantBpm\n";
        {
            BackendLibrary::Ptr inner = lib.makeOwned();
            if (inner == nullptr) { std::cerr << "factory returned null\n"; return 2; }
            tempo_variant::TempoVariantTracker variant (
                std::move (inner),
                [&methodOut] (const tempo_variant::MethodRecord& m)
                {
                    char interval[32];
                    if (m.intervalMeasured)
                        std::snprintf (interval, sizeof interval, "%.9f", m.intervalSeconds);
                    else
                        interval[0] = '\0';
                    char buf[320];
                    std::snprintf (buf, sizeof buf,
                                   "%llu,%.9f,%.9f,%d,%.9f,%d,%s,%s,%zu,%d,%.9f,%.9f\n",
                                   static_cast<unsigned long long> (m.blockIndex),
                                   m.blockStartSeconds, m.blockEndSeconds,
                                   m.beatEvent ? 1 : 0, m.eventSeconds,
                                   m.intervalMeasured ? 1 : 0, interval,
                                   tempo_variant::toString (m.intervalState),
                                   m.ringCount, m.ready ? 1 : 0, m.baseBpm, m.variantBpm);
                    methodOut << buf;
                });
            std::vector<Row> varRows;
            if (! runRows (variant, audio, blockFrames, varRows))
            { std::cerr << "invalid run parameters\n"; return 2; }
        }
        methodOut.flush();
        if (! methodOut.good()) { std::cerr << "write failed " << path << "\n"; return 2; }
        methodOut.close();
        if (! methodOut.good()) { std::cerr << "close failed " << path << "\n"; return 2; }

        // Sidecars: the generator's actual truth anchor/periods and beat times,
        // so the response lag can be measured from the real step, not the
        // nominal instant.
        const tempo_variant::ClickStepInfo info = tempo_variant::clickStepInfo (spec);
        const std::vector<double> truthBeats = tempo_variant::clickBeatTimes (spec);
        char ibuf[160];
        std::snprintf (ibuf, sizeof ibuf, "%.9f,%.9f,%.9f,%.9f\n",
                       info.nominalStepSeconds, info.anchorBeatSeconds,
                       info.prePeriodSeconds, info.postPeriodSeconds);
        const std::string infoPath = out + "/click_step_rate" + label (rate) + "_info.csv";
        if (! writeFile (infoPath,
                         "nominalStepSeconds,anchorBeatSeconds,prePeriodSeconds,postPeriodSeconds\n"
                         + std::string (ibuf)))
        { std::cerr << "write failed " << infoPath << "\n"; return 2; }

        std::string truthCsv = "beatIndex,beatSeconds\n";
        for (std::size_t i = 0; i < truthBeats.size(); ++i)
        {
            char b[48];
            std::snprintf (b, sizeof b, "%zu,%.9f\n", i, truthBeats[i]);
            truthCsv += b;
        }
        const std::string truthPath = out + "/click_step_rate" + label (rate) + "_truth.csv";
        if (! writeFile (truthPath, truthCsv))
        { std::cerr << "write failed " << truthPath << "\n"; return 2; }

        std::cout << "wrote " << path << "\n";
        return 0;
    }
}
