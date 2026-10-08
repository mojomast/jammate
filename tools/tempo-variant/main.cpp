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

#include "ClickTrain.h"
#include "MethodLog.h"
#include "TempoVariant.h"

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

bool loadBackend (const std::string& path, BackendLibrary& lib)
{
#if CMD_HAVE_DLOPEN
    void* h = ::dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr)
    { std::cerr << "dlopen failed for " << path << ": " << ::dlerror() << "\n"; return false; }
    lib.handle = h;
    lib.create = loadSymbol<CreateFn> (h, "jam_rhythm_create");
    lib.destroy = loadSymbol<DestroyFn> (h, "jam_rhythm_destroy");
    if (lib.create == nullptr || lib.destroy == nullptr)
    { std::cerr << path << " lacks jam_rhythm_create/destroy\n"; return false; }
    return true;
#else
    (void) path; (void) lib;
    std::cerr << "dlopen unsupported\n";
    return false;
#endif
}

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

template <typename Backend>
void runRows (Backend& backend, const rhythmeval::WavData& audio,
              std::size_t blockFrames, std::vector<Row>& rows)
{
    backend.reset (audio.sampleRate);
    std::uint64_t blockIndex = 0;
    for (std::size_t first = 0; first < audio.frames; first += blockFrames)
    {
        const std::size_t remaining = audio.frames - first;
        const std::size_t count = remaining < blockFrames ? remaining : blockFrames;

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
}

std::vector<double> parseList (const std::string& text)
{
    std::vector<double> out;
    std::stringstream ss (text);
    std::string item;
    while (std::getline (ss, item, ','))
    {
        if (item.empty()) continue;
        out.push_back (std::strtod (item.c_str(), nullptr));
    }
    return out;
}

std::string label (double v)
{
    char buf[32]; std::snprintf (buf, sizeof buf, "%.10g", v);
    return std::string (buf);
}

bool writeFile (const std::string& path, const std::string& contents)
{
    std::ofstream out (path.c_str(), std::ios::binary);
    if (! out.good()) return false;
    out << contents;
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
    std::string backendLib;
    std::string out;
    std::string mode;
    double rate = 48000.0;
    double seconds = 24.0;
    std::size_t blockFrames = 128;
    std::vector<double> sweep;
    double stepFrom = 126.0, stepTo = 132.0, stepSeconds = 12.0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&] () -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (arg == "--backend-lib") backendLib = next();
        else if (arg == "--out") out = next();
        else if (arg == "--mode") mode = next();
        else if (arg == "--rate") rate = std::strtod (next().c_str(), nullptr);
        else if (arg == "--seconds") seconds = std::strtod (next().c_str(), nullptr);
        else if (arg == "--block") blockFrames = static_cast<std::size_t> (std::strtoul (next().c_str(), nullptr, 10));
        else if (arg == "--sweep") sweep = parseList (next());
        else if (arg == "--step-from") stepFrom = std::strtod (next().c_str(), nullptr);
        else if (arg == "--step-to") stepTo = std::strtod (next().c_str(), nullptr);
        else if (arg == "--step-seconds") stepSeconds = std::strtod (next().c_str(), nullptr);
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "usage: tempo-variant-click --backend-lib <so> --out <dir> "
                         "--mode sweep|step [--rate hz] [--block n]\n";
            return 0;
        }
        else { std::cerr << "unknown option: " << arg << "\n"; return 2; }
    }

    if (backendLib.empty() || out.empty() || mode.empty())
    { std::cerr << "tempo-variant-click: --backend-lib, --out and --mode are required\n"; return 2; }

    std::error_code ec;
    std::filesystem::create_directories (out, ec);
    if (ec) { std::cerr << "cannot create " << out << "\n"; return 2; }

    BackendLibrary lib;
    if (! loadBackend (backendLib, lib)) return 2;

    if (mode == "sweep")
    {
        if (sweep.empty()) sweep = {118, 120, 122, 123, 124, 125, 126, 127, 128, 130, 132, 134};
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
                if (base == nullptr) { std::cerr << "null factory\n"; return 2; }
                runRows (*base, audio, blockFrames, baseRows);
            }
            std::vector<Row> varRows;
            {
                std::vector<double> ignored;
                tempo_variant::TempoVariantTracker variant (lib.makeOwned(), {});
                runRows (variant, audio, blockFrames, varRows);
            }

            auto stats = [] (const std::vector<Row>& rows, std::vector<double>& bpms,
                             double& meanInterval, int& nBeats)
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
                // Convert the accumulated SAMPLE sum to seconds below via caller.
                meanInterval = (n > 0) ? sum / n : 0.0;
            };
            std::vector<double> baseBpms, varBpms;
            double baseIntervalSamples = 0.0, varIntervalSamples = 0.0;
            int baseBeats = 0, varBeats = 0;
            stats (baseRows, baseBpms, baseIntervalSamples, baseBeats);
            stats (varRows, varBpms, varIntervalSamples, varBeats);
            (void) baseIntervalSamples;
            (void) baseBeats;
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

    if (mode == "step")
    {
        tempo_variant::ClickSpec spec;
        spec.sampleRate = rate; spec.seconds = seconds;
        spec.bpm = stepFrom; spec.stepToBpm = stepTo; spec.stepSeconds = stepSeconds;
        const rhythmeval::WavData audio = tempo_variant::makeClickTrain (spec);

        std::vector<Row> varRows;
        std::ofstream methodOut ((out + "/click_step_rate" + label (rate) + ".csv").c_str(),
                                 std::ios::binary);
        if (! methodOut.good()) { std::cerr << "cannot open method csv\n"; return 2; }
        methodOut << "blockIndex,blockStartSeconds,blockEndSeconds,beatEvent,"
                     "eventSeconds,intervalSeconds,intervalState,ringCount,ready,"
                     "baseBpm,variantBpm\n";
        {
            tempo_variant::TempoVariantTracker variant (
                lib.makeOwned(),
                [&methodOut] (const tempo_variant::MethodRecord& m)
                {
                    char buf[256];
                    std::snprintf (buf, sizeof buf,
                                   "%llu,%.9f,%.9f,%d,%.9f,%.9f,%s,%zu,%d,%.9f,%.9f\n",
                                   static_cast<unsigned long long> (m.blockIndex),
                                   m.blockStartSeconds, m.blockEndSeconds,
                                   m.beatEvent ? 1 : 0, m.eventSeconds,
                                   m.intervalSeconds, tempo_variant::toString (m.intervalState),
                                   m.ringCount, m.ready ? 1 : 0, m.baseBpm, m.variantBpm);
                    methodOut << buf;
                });
            runRows (variant, audio, blockFrames, varRows);
        }
        std::cout << "wrote " << out << "/click_step_rate" << label (rate) << ".csv\n";
        return 0;
    }

    std::cerr << "unknown mode: " << mode << "\n";
    return 2;
}
