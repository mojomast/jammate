// analysis-worker-benchmark — throughput harness for jam::RhythmAnalyzer.
//
// This is NOT a quality or latency gate. It answers one question: when audio is
// enqueued in bounded bursts as fast as the bounded ring allows, does the
// analysis worker sustain more than real time? It reports BOTH the steady wall
// time and the CPU time and derives a realtime factor from the wall time; it
// never asserts a pass/fail against latency.
//
// The real tracker backends are the pinned, read-only dlopen() plugins built by
// EVAL-005 (`librhythm-eval-{btrack,aubio}.so`). No GPL backend is compiled into
// this tool, matching SPEC.md 25.6. The worker and ring are compiled from the
// jam-core sources in the worktree, so the measurement is of the ANALYSIS-001
// code, not of a stale archive.
//
// Determinism: the click train is generated with an integer-indexed, fixed
// formula; no recording, no randomness, no wall-clock input. The generator and
// the per-sample loop are precomputed once so their cost does not pollute the
// measured region.

#include "jam/AnalysisAudioRing.h"
#include "jam/IRhythmTracker.h"
#include "jam/RhythmAnalyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

struct PluginSpec
{
    std::string name;
    std::string path;
    std::string sha256;
};

struct Options
{
    std::vector<PluginSpec> plugins;
    std::vector<double> rates;
    double bpm = 120.0;
    double seconds = 5.0;
    std::size_t blockFrames = 128;
    std::size_t ringCapacity = 64;
    std::size_t burst = 16;
    bool throughputMode = true;
    bool pressureMode = true;
    std::string outDir = ".";
    std::string sourceSha;
};

struct RunResult
{
    std::string backend;
    std::string pluginSha;
    double rate = 0.0;
    std::size_t blockFrames = 0;
    std::size_t ringCapacity = 0;
    std::size_t burst = 0;
    std::string mode;
    bool ready = false;

    uint64_t blocks = 0;
    uint64_t frames = 0;
    double wallSeconds = 0.0;
    double cpuSeconds = 0.0;
    double audioSeconds = 0.0;
    double realtimeFactor = 0.0;
    double framesPerSecond = 0.0;
    uint64_t observations = 0;
    uint64_t beats = 0;
    uint64_t droppedObservations = 0;
    uint64_t ringOverruns = 0;
    uint64_t pushBackoffs = 0;
};

std::pair<std::string, std::string> splitPair (const std::string& text)
{
    const std::size_t at = text.find ('=');
    if (at == std::string::npos)
        throw std::runtime_error ("expected name=value, got: " + text);
    return { text.substr (0, at), text.substr (at + 1) };
}

Options parseArgs (int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto value = [&] (const char* key) -> std::string {
            const std::string prefix = std::string (key) + "=";
            if (arg.rfind (prefix, 0) != 0)
                throw std::runtime_error (std::string ("missing value for ") + key);
            return arg.substr (prefix.size());
        };

        if (arg.rfind ("--plugin=", 0) == 0)
        {
            const auto [name, path] = splitPair (value ("--plugin"));
            o.plugins.push_back ({ name, path, "" });
        }
        else if (arg.rfind ("--plugin-sha=", 0) == 0)
        {
            const auto [name, sha] = splitPair (value ("--plugin-sha"));
            bool found = false;
            for (auto& p : o.plugins)
                if (p.name == name) { p.sha256 = sha; found = true; }
            if (! found)
                throw std::runtime_error ("--plugin-sha for unknown plugin: " + name);
        }
        else if (arg.rfind ("--rate=", 0) == 0)
            o.rates.push_back (std::stod (value ("--rate")));
        else if (arg.rfind ("--bpm=", 0) == 0)
            o.bpm = std::stod (value ("--bpm"));
        else if (arg.rfind ("--seconds=", 0) == 0)
            o.seconds = std::stod (value ("--seconds"));
        else if (arg.rfind ("--block-frames=", 0) == 0)
            o.blockFrames = static_cast<std::size_t> (std::stoul (value ("--block-frames")));
        else if (arg.rfind ("--ring-capacity=", 0) == 0)
            o.ringCapacity = static_cast<std::size_t> (std::stoul (value ("--ring-capacity")));
        else if (arg.rfind ("--burst=", 0) == 0)
            o.burst = static_cast<std::size_t> (std::stoul (value ("--burst")));
        else if (arg.rfind ("--mode=", 0) == 0)
        {
            const std::string m = value ("--mode");
            o.throughputMode = (m == "throughput" || m == "both");
            o.pressureMode = (m == "pressure" || m == "both");
        }
        else if (arg.rfind ("--out=", 0) == 0)
            o.outDir = value ("--out");
        else if (arg.rfind ("--source-sha=", 0) == 0)
            o.sourceSha = value ("--source-sha");
        else
            throw std::runtime_error ("unknown argument: " + arg);
    }

    if (o.plugins.empty())
        throw std::runtime_error ("at least one --plugin=name=path is required");
    if (o.rates.empty())
        throw std::runtime_error ("at least one --rate is required");
    if (o.blockFrames == 0 || o.blockFrames > jam::kMaxAnalysisBlock)
        throw std::runtime_error ("--block-frames must be in [1, kMaxAnalysisBlock]");

    return o;
}

/** Deterministic click train: one exponentially decaying impulse per beat. */
std::vector<float> makeClickTrain (uint64_t frames, double rate, double bpm)
{
    std::vector<float> out (static_cast<std::size_t> (frames), 0.0f);
    const double period = rate * 60.0 / bpm;
    if (period <= 1.0)
        return out;

    for (uint64_t beat = 0;; ++beat)
    {
        const double at = static_cast<double> (beat) * period;
        const auto start = static_cast<uint64_t> (at);
        if (start >= frames)
            break;
        const uint64_t end = std::min<uint64_t> (frames, start + 24);
        for (uint64_t g = start; g < end; ++g)
        {
            const double age = static_cast<double> (g) - at;
            out[static_cast<std::size_t> (g)] = static_cast<float> (0.7 * std::exp (-age / 3.0));
        }
    }
    return out;
}

struct LoadedPlugin
{
    void* handle = nullptr;
    jam::IRhythmTracker* (*create)() = nullptr;
    void (*destroy) (jam::IRhythmTracker*) = nullptr;

    bool valid() const noexcept { return handle != nullptr && create != nullptr && destroy != nullptr; }
};

LoadedPlugin loadPlugin (const std::string& path)
{
    LoadedPlugin plugin;
    plugin.handle = dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (plugin.handle == nullptr)
        return plugin;

    plugin.create = reinterpret_cast<jam::IRhythmTracker* (*)()> (
        dlsym (plugin.handle, "jam_rhythm_create"));
    plugin.destroy = reinterpret_cast<void (*) (jam::IRhythmTracker*)> (
        dlsym (plugin.handle, "jam_rhythm_destroy"));

    if (! plugin.valid())
    {
        dlclose (plugin.handle);
        plugin.handle = nullptr;
    }
    return plugin;
}

uint64_t drainObservations (jam::RhythmAnalyzer& analyzer, uint64_t& beats)
{
    uint64_t consumed = 0;
    jam::ObservationEnvelope e;
    while (analyzer.popObservation (e))
    {
        ++consumed;
        if (e.observation.beatEvent)
            ++beats;
    }
    return consumed;
}

RunResult runOne (const LoadedPlugin& plugin, const PluginSpec& spec, double rate,
                  const Options& o, bool pressure)
{
    RunResult r;
    r.backend = spec.name;
    r.pluginSha = spec.sha256;
    r.rate = rate;
    r.blockFrames = o.blockFrames;
    r.ringCapacity = o.ringCapacity;
    r.burst = o.burst;
    r.mode = pressure ? "pressure" : "throughput";

    const bool loaded = plugin.valid();
    if (! loaded)
        return r;

    const uint64_t totalFrames = static_cast<uint64_t> (o.seconds * rate);
    const uint64_t totalBlocks = totalFrames / o.blockFrames;
    if (totalBlocks == 0)
        return r;

    const std::vector<float> clicks = makeClickTrain (totalBlocks * o.blockFrames, rate, o.bpm);

    jam::AnalysisAudioRing ring (o.ringCapacity);
    jam::IRhythmTracker* rawTracker = plugin.create();
    if (rawTracker == nullptr)
        return r;

    jam::RhythmAnalyzer analyzer (ring, rawTracker, plugin.destroy);
    if (analyzer.start (rate) != jam::AnalyzerStartResult::started)
        return r;

    uint64_t pushed = 0;
    uint64_t backoffs = 0;
    uint64_t consumed = 0;
    uint64_t beats = 0;

    const auto wallStart = Clock::now();
    const std::clock_t cpuStart = std::clock();

    jam::AnalysisFrame frame;
    frame.sourceSampleRate = rate;
    frame.numSamples = static_cast<uint32_t> (o.blockFrames);

    for (uint64_t b = 0; b < totalBlocks; ++b)
    {
        frame.sampleTime = b * o.blockFrames;
        std::memcpy (frame.samples, clicks.data() + b * o.blockFrames, o.blockFrames * sizeof (float));

        while (! ring.push (frame.samples, frame.numSamples, frame.sampleTime, frame.sourceSampleRate))
        {
            ++backoffs;
            if (! pressure)
                consumed += drainObservations (analyzer, beats);
            std::this_thread::yield();
        }
        ++pushed;

        // Keep the evidence queue drained as we go: a consumer that keeps up is
        // what makes this a throughput measurement rather than a drop test.
        if (! pressure)
            consumed += drainObservations (analyzer, beats);

        const bool burstBoundary = ((b + 1) % o.burst == 0) || (b + 1 == totalBlocks);

        if (burstBoundary && ! pressure)
        {
            // Bounded burst submitted; wait for the worker to drain exactly that
            // burst before submitting the next. Under nominal load the ring never
            // overruns, so this measures worker throughput, not backpressure.
            const uint64_t target = b + 1;
            const auto burstDeadline = Clock::now() + std::chrono::seconds (60);
            while (analyzer.stats().processedFrames < target && Clock::now() < burstDeadline)
            {
                consumed += drainObservations (analyzer, beats);
                std::this_thread::yield();
            }
            consumed += drainObservations (analyzer, beats);
        }
    }

    // Wait (bounded) for the worker to have fed every block, then drain.
    const auto deadline = Clock::now() + std::chrono::seconds (60);
    while (analyzer.stats().processedFrames < totalBlocks && Clock::now() < deadline)
    {
        if (! pressure)
            consumed += drainObservations (analyzer, beats);
        std::this_thread::yield();
    }
    if (! pressure)
        consumed += drainObservations (analyzer, beats);

    const auto wallEnd = Clock::now();
    const std::clock_t cpuEnd = std::clock();

    const auto stats = analyzer.stats();
    analyzer.stop();

    r.frames = pushed * o.blockFrames;
    r.blocks = pushed;
    r.wallSeconds = std::chrono::duration<double> (wallEnd - wallStart).count();
    r.cpuSeconds = static_cast<double> (cpuEnd - cpuStart) / static_cast<double> (CLOCKS_PER_SEC);
    r.audioSeconds = static_cast<double> (r.frames) / rate;
    r.realtimeFactor = r.wallSeconds > 0.0 ? r.audioSeconds / r.wallSeconds : 0.0;
    r.framesPerSecond = r.wallSeconds > 0.0 ? static_cast<double> (r.frames) / r.wallSeconds : 0.0;
    r.observations = consumed;
    r.beats = beats;
    r.droppedObservations = stats.droppedObservations;
    r.ringOverruns = stats.ringOverruns;
    r.pushBackoffs = backoffs;

    // "ready" only means the pinned backend loaded and the worker fed it every
    // block. It is not a quality result and asserts no backend selection.
    r.ready = loaded && stats.processedFrames == totalBlocks;
    return r;
}

std::string escapeJson (const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

} // namespace

int main (int argc, char** argv)
{
    Options options;
    try
    {
        options = parseArgs (argc, argv);
    }
    catch (const std::exception& e)
    {
        std::fprintf (stderr, "error: %s\n", e.what());
        return 2;
    }

    std::vector<RunResult> results;
    for (const auto& spec : options.plugins)
    {
        const LoadedPlugin plugin = loadPlugin (spec.path);
        if (! plugin.valid())
            std::fprintf (stderr, "warning: could not load backend %s from %s: %s\n",
                          spec.name.c_str(), spec.path.c_str(),
                          dlerror() != nullptr ? dlerror() : "unknown");

        for (double rate : options.rates)
        {
            if (options.throughputMode)
                results.push_back (runOne (plugin, spec, rate, options, false));
            if (options.pressureMode)
                results.push_back (runOne (plugin, spec, rate, options, true));
        }

        // All analyzers for this plugin have been destroyed by now, so the
        // tracker deleter no longer points into the shared object.
        if (plugin.handle != nullptr)
            dlclose (plugin.handle);
    }

    std::ofstream csv (options.outDir + "/throughput.csv");
    if (! csv)
    {
        std::fprintf (stderr, "error: cannot write %s/throughput.csv\n", options.outDir.c_str());
        return 3;
    }

    csv << "backend,plugin_sha256,rate,block_frames,ring_capacity,burst,mode,ready,"
           "blocks,frames,wall_seconds,cpu_seconds,audio_seconds,realtime_factor,"
           "frames_per_second,observations,beats,dropped_observations,ring_overruns,"
           "push_backoffs\n";

    for (const auto& r : results)
    {
        csv << r.backend << ',' << r.pluginSha << ',' << r.rate << ','
            << r.blockFrames << ',' << r.ringCapacity << ',' << r.burst << ','
            << r.mode << ',' << (r.ready ? 1 : 0) << ','
            << r.blocks << ',' << r.frames << ','
            << r.wallSeconds << ',' << r.cpuSeconds << ',' << r.audioSeconds << ','
            << r.realtimeFactor << ',' << r.framesPerSecond << ','
            << r.observations << ',' << r.beats << ',' << r.droppedObservations << ','
            << r.ringOverruns << ',' << r.pushBackoffs << '\n';
    }

    std::ofstream json (options.outDir + "/benchmark.json");
    if (! json)
    {
        std::fprintf (stderr, "error: cannot write %s/benchmark.json\n", options.outDir.c_str());
        return 3;
    }

    json << "{\n";
    json << "  \"tool\": \"analysis-worker-benchmark\",\n";
    json << "  \"sourceSha256\": \"" << escapeJson (options.sourceSha) << "\",\n";
    json << "  \"bpm\": " << options.bpm << ",\n";
    json << "  \"seconds\": " << options.seconds << ",\n";
    json << "  \"blockFrames\": " << options.blockFrames << ",\n";
    json << "  \"ringCapacity\": " << options.ringCapacity << ",\n";
    json << "  \"burst\": " << options.burst << ",\n";
    json << "  \"note\": \"wall and CPU are reported for throughput; no latency gate is applied\",\n";
    json << "  \"runs\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const auto& r = results[i];
        json << "    {"
             << "\"backend\": \"" << escapeJson (r.backend) << "\", "
             << "\"pluginSha256\": \"" << escapeJson (r.pluginSha) << "\", "
             << "\"rate\": " << r.rate << ", "
             << "\"blockFrames\": " << r.blockFrames << ", "
             << "\"mode\": \"" << r.mode << "\", "
             << "\"ready\": " << (r.ready ? "true" : "false") << ", "
             << "\"blocks\": " << r.blocks << ", "
             << "\"frames\": " << r.frames << ", "
             << "\"wallSeconds\": " << r.wallSeconds << ", "
             << "\"cpuSeconds\": " << r.cpuSeconds << ", "
             << "\"audioSeconds\": " << r.audioSeconds << ", "
             << "\"realtimeFactor\": " << r.realtimeFactor << ", "
             << "\"framesPerSecond\": " << r.framesPerSecond << ", "
             << "\"observations\": " << r.observations << ", "
             << "\"beats\": " << r.beats << ", "
             << "\"droppedObservations\": " << r.droppedObservations << ", "
             << "\"ringOverruns\": " << r.ringOverruns << ", "
             << "\"pushBackoffs\": " << r.pushBackoffs
             << "}" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    json << "  ]\n";
    json << "}\n";

    std::printf ("wrote %s/throughput.csv and %s/benchmark.json (%zu runs)\n",
                 options.outDir.c_str(), options.outDir.c_str(), results.size());
    return 0;
}
