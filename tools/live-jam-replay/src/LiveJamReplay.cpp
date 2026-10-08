// EVAL-LIVE-001 — actual-processor first-live-Jam replay harness.
//
// WHAT THIS IS
//   A bounded, non-device replay that constructs the real
//   `GuitarCompanionProcessor`, calls its real
//   `prepareToPlay` / `processBlock` / `releaseResources`, and drives the frozen
//   `submitJamCommand` / `readJamLiveState` facade. While replayed, the copied RT
//   probe instrumentation counts C++/C allocations, frees and pthread
//   lock/cond operations *inside the real callback*. No processor, editor,
//   engine or frozen-interface source is modified; no stub processor is ever
//   substituted for callback evidence.
//
// WHAT THIS IS NOT
//   It is not latency, dropout, device or Windows/ASIO evidence. It is not a
//   whole-program safety proof. It does not claim the >=95% within-two-bar
//   useful-lock target; device/ASIO deadlines and real-guitar trials are
//   separate gates. Synthetic strum/click fixtures are explicitly NOT guitar
//   recordings and carry their own identity.
//
// BUILD GATING
//   This translation unit is compiled (object -c) against the frozen headers on
//   every base. It is *linked and run* only when the product shared archive
//   actually defines the live facade (`tools/live-jam-replay/run_replay.py`
//   enforces this fail-closed). On a base whose product has no Jam definitions
//   the tool reports "harness ready, awaiting actual product" and never invokes
//   a stale binary as if it were clean.
#include "PluginProcessor.h"
#include "ReplaySupport.h"
#include "RtProbeInstrumentation.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

using replay::jsonNumber;
using replay::jsonU64;
using replay::jsonI64;
using replay::jsonBool;
using replay::WavMono;
using replay::loadWavMono;

constexpr int kMinWarmBlocks = 16;
constexpr int kMaxWarmBlocks = 200000;
constexpr int kMaxStateSamples = 64;

//------------------------------------------------------------------------------
// Reporting is done through the shared replay::json* helpers (see
// ReplaySupport.h) so a non-finite or unmeasured value becomes an explicit JSON
// null, never NaN.
//------------------------------------------------------------------------------

const char* backendName (jam::JamLiveBackend b)
{
    switch (b)
    {
        case jam::JamLiveBackend::unavailable:      return "unavailable";
        case jam::JamLiveBackend::experimentalBTrack: return "experimentalBTrack";
        case jam::JamLiveBackend::injectedTest:     return "injectedTest";
    }
    return "unknown";
}

const char* failureName (jam::JamLiveFailure x)
{
    switch (x)
    {
        case jam::JamLiveFailure::none:             return "none";
        case jam::JamLiveFailure::unavailableBackend: return "unavailableBackend";
        case jam::JamLiveFailure::invalidDevice:    return "invalidDevice";
        case jam::JamLiveFailure::workerFailure:    return "workerFailure";
    }
    return "unknown";
}

void writeState (std::FILE* f, const jam::JamLiveState& s)
{
    std::fprintf (f, "{");
    std::fprintf (f, "\"sessionGeneration\":"); jsonU64 (f, s.sessionGeneration);
    std::fprintf (f, ",\"prepared\":");        jsonBool (f, s.prepared);
    std::fprintf (f, ",\"requestedRunning\":");jsonBool (f, s.requestedRunning);
    std::fprintf (f, ",\"joinPending\":");     jsonBool (f, s.joinPending);
    std::fprintf (f, ",\"drumsPlaying\":");    jsonBool (f, s.drumsPlaying);
    std::fprintf (f, ",\"backend\":\"%s\"", backendName (s.backend));
    std::fprintf (f, ",\"failure\":\"%s\"", failureName (s.failure));
    std::fprintf (f, ",\"mode\":\"%s\"", jam::toString (s.mode));
    std::fprintf (f, ",\"clock\":{");
    std::fprintf (f, "\"generation\":"); jsonU64 (f, s.clock.generation);
    std::fprintf (f, ",\"bpm\":");       jsonNumber (f, s.clock.bpm);
    std::fprintf (f, ",\"beatPhase01\":"); jsonNumber (f, s.clock.beatPhase01);
    std::fprintf (f, ",\"barPhase01\":");  jsonNumber (f, s.clock.barPhase01);
    std::fprintf (f, ",\"beatInBar\":");   std::fprintf (f, "%d", s.clock.beatInBar);
    std::fprintf (f, ",\"beatsPerBar\":"); std::fprintf (f, "%d", s.clock.beatsPerBar);
    std::fprintf (f, ",\"beatUnit\":");    std::fprintf (f, "%d", s.clock.beatUnit);
    std::fprintf (f, ",\"confidence01\":"); jsonNumber (f, (double) s.clock.confidence01);
    std::fprintf (f, ",\"lockState\":\"%s\"", jam::toString (s.clock.lockState));
    std::fprintf (f, ",\"tempoFrozen\":"); jsonBool (f, s.clock.tempoFrozen);
    std::fprintf (f, "}");
    std::fprintf (f, ",\"candidateBpm\":"); jsonNumber (f, (double) s.candidateBpm);
    std::fprintf (f, ",\"inputPeak\":");    jsonNumber (f, (double) s.inputPeak);
    std::fprintf (f, ",\"sampleRate\":");   jsonNumber (f, s.sampleRate);
    std::fprintf (f, ",\"audioSampleTime\":"); jsonU64 (f, s.audioSampleTime);
    std::fprintf (f, ",\"lastEventSampleTime\":"); jsonU64 (f, s.lastEventSampleTime);
    std::fprintf (f, ",\"lastInputHorizonSampleTime\":"); jsonU64 (f, s.lastInputHorizonSampleTime);
    std::fprintf (f, ",\"lastReceiptSampleTime\":"); jsonU64 (f, s.lastReceiptSampleTime);
    std::fprintf (f, ",\"receiptMeasured\":"); jsonBool (f, s.receiptMeasured);
    std::fprintf (f, ",\"analysisDrops\":");  jsonU64 (f, s.analysisDrops);
    std::fprintf (f, ",\"observationDrops\":"); jsonU64 (f, s.observationDrops);
    std::fprintf (f, ",\"userCommandDrops\":"); jsonU64 (f, s.userCommandDrops);
    std::fprintf (f, ",\"drumCommandDrops\":"); jsonU64 (f, s.drumCommandDrops);
    std::fprintf (f, ",\"discontinuities\":"); jsonU64 (f, s.discontinuities);
    std::fprintf (f, "}");
}

//------------------------------------------------------------------------------
// Deterministic synthetic input. Never a guitar recording; the four generators
// are distinct and their identity is recorded in the evidence by name.
//------------------------------------------------------------------------------
enum class InputKind { clean, noise, silence };

const char* inputName (InputKind k)
{
    switch (k)
    {
        case InputKind::clean:   return "clean";
        case InputKind::noise:   return "noise";
        case InputKind::silence: return "silence";
    }
    return "unknown";
}

struct InputSet
{
    const WavMono* wav[3] = { nullptr, nullptr, nullptr };
    const std::string* path[3] = { nullptr, nullptr, nullptr };

    const WavMono* wavFor (InputKind k) const { return wav[(int) k]; }
    const std::string* pathFor (InputKind k) const { return path[(int) k]; }
};

struct InputGen
{
    InputKind kind = InputKind::clean;
    std::uint32_t cursor = 1u;
    const WavMono* wav = nullptr;   // when set, drives the callback input
    std::size_t wpos = 0;

    void reset (std::uint32_t seed) noexcept { cursor = seed ? seed : 1u; wpos = 0; }

    void fill (juce::AudioBuffer<float>& buf, int n) noexcept
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* p = buf.getWritePointer (ch);
            if (wav != nullptr && wav->ok && ! wav->samples.empty())
            {
                const std::size_t total = wav->samples.size();
                for (int i = 0; i < n; ++i)
                {
                    p[i] = wav->samples[wpos % total];
                    ++wpos;
                }
                continue;
            }
            if (kind == InputKind::silence)
            {
                for (int i = 0; i < n; ++i) p[i] = 0.0f;
                continue;
            }
            for (int i = 0; i < n; ++i)
            {
                cursor = cursor * 1664525u + 1013904223u;
                const float u = (float) ((cursor >> 8) & 0xFFFFu) / 65535.0f;
                if (kind == InputKind::noise)
                {
                    p[i] = (u * 0.5f - 0.25f) * (ch == 0 ? 1.0f : 0.7f);
                }
                else
                {
                    // Deterministic strum-ish click train: a decaying pluck
                    // envelope every 16th of a 120 BPM beat plus a low bed.
                    const int period = 6000;
                    const int phase = (int) ((cursor >> 3) % (std::uint32_t) period);
                    const float env = std::exp (-6.0f * (float) phase / (float) period);
                    const float tone = std::sin (6.2831853f * 220.0f * (float) i / 48000.0f);
                    p[i] = (0.05f + 0.35f * env * tone + 0.02f * (u - 0.5f))
                           * (ch == 0 ? 1.0f : 0.7f);
                }
            }
        }
    }
};

//------------------------------------------------------------------------------
// Per-cell measured record.
//------------------------------------------------------------------------------
struct Cell
{
    std::string id;
    double rate = 0.0;
    int block = 0;
    std::string pipeline;   // disabled | enabled | enabled_pressure
    InputKind input = InputKind::clean;
    std::string inputSource;   // builtin:<kind> or wav:<path>
    int warmBlocks = 0;
    bool realtimePaced = false;

    bool measured = false;
    std::string unmeasuredReason;

    rtprobe::Snapshot cold {};
    rtprobe::Snapshot warm {};

    double callbackWallSumMs = 0.0;
    std::uint64_t callbackCount = 0;
    double elapsedWallS = 0.0;

    double outRmsMean = 0.0;
    double outRmsMax = 0.0;
    float outPeak = 0.0f;
    std::uint64_t nonzeroBlocks = 0;

    // State progression.
    jam::JamLiveState stateStart {};
    jam::JamLiveState stateEnd {};
    std::uint64_t audioSampleStart = 0;
    std::uint64_t audioSampleEnd = 0;
    bool audioMonotonic = true;
    bool audioDeltaOk = true;
    std::uint64_t audioDeltaMismatches = 0;
    bool preparedSeen = false;
    bool requestedRunningSeen = false;
    bool joinPendingSeen = false;
    bool drumsPlayingSeen = false;
    std::uint64_t generationChanges = 0;
    std::uint64_t candidateBpmNonzero = 0;
    std::uint64_t receiptCount = 0;
    std::uint64_t receiptMeasuredCount = 0;
    std::uint64_t receiptBeforeHorizon = 0;
    std::uint64_t eventAfterHorizon = 0;
    std::uint64_t maxReceiptLagSamples = 0;
    std::uint64_t lastReceiptSample = 0;

    std::vector<std::string> commandLog;   // bounded text lines
};

void fillSnapshotJson (std::FILE* f, const rtprobe::Snapshot& s)
{
    auto k = [&] (rtprobe::Kind kind) { return s.allocCalls[(std::size_t) kind]; };
    std::fprintf (f, "{");
    std::fprintf (f, "\"cxx_new\":");          jsonU64 (f, k (rtprobe::Kind::cxxNew));
    std::fprintf (f, ",\"cxx_new_array\":");   jsonU64 (f, k (rtprobe::Kind::cxxNewArray));
    std::fprintf (f, ",\"cxx_new_nothrow\":"); jsonU64 (f, k (rtprobe::Kind::cxxNewNothrow));
    std::fprintf (f, ",\"cxx_new_aligned\":"); jsonU64 (f, k (rtprobe::Kind::cxxNewAligned));
    std::fprintf (f, ",\"cxx_delete\":");      jsonU64 (f, k (rtprobe::Kind::cxxDelete));
    std::fprintf (f, ",\"cxx_delete_array\":");jsonU64 (f, k (rtprobe::Kind::cxxDeleteArray));
    std::fprintf (f, ",\"cxx_delete_sized\":");jsonU64 (f, k (rtprobe::Kind::cxxDeleteSized));
    std::fprintf (f, ",\"cxx_delete_aligned\":"); jsonU64 (f, k (rtprobe::Kind::cxxDeleteAligned));
    std::fprintf (f, ",\"c_malloc\":");        jsonU64 (f, k (rtprobe::Kind::cMalloc));
    std::fprintf (f, ",\"c_calloc\":");        jsonU64 (f, k (rtprobe::Kind::cCalloc));
    std::fprintf (f, ",\"c_realloc\":");       jsonU64 (f, k (rtprobe::Kind::cRealloc));
    std::fprintf (f, ",\"c_free\":");          jsonU64 (f, k (rtprobe::Kind::cFree));
    std::fprintf (f, ",\"noop_frees\":");      jsonU64 (f, s.noopFrees);
    std::fprintf (f, ",\"lock\":");            jsonU64 (f, s.lockCalls);
    std::fprintf (f, ",\"trylock\":");         jsonU64 (f, s.trylockCalls);
    std::fprintf (f, ",\"cond\":");            jsonU64 (f, s.condWaitCalls);
    std::fprintf (f, ",\"unlock\":");          jsonU64 (f, s.unlockCalls);
    std::fprintf (f, ",\"blocked_lock\":");    jsonU64 (f, s.blockedLockCalls);
    std::fprintf (f, ",\"locked_ns\":");       jsonU64 (f, s.lockedNanos);
    std::fprintf (f, ",\"max_lock_ns\":");     jsonU64 (f, s.maxLockNs);
    std::fprintf (f, ",\"alloc_overflow\":");  jsonU64 (f, s.allocRecordOverflow);
    std::fprintf (f, ",\"lock_overflow\":");   jsonU64 (f, s.lockRecordOverflow);
    std::fprintf (f, ",\"alloc_cxx_total\":"); jsonU64 (f, k (rtprobe::Kind::cxxNew)
        + k (rtprobe::Kind::cxxNewArray) + k (rtprobe::Kind::cxxNewNothrow)
        + k (rtprobe::Kind::cxxNewAligned));
    std::fprintf (f, ",\"alloc_c_total\":");   jsonU64 (f, k (rtprobe::Kind::cMalloc)
        + k (rtprobe::Kind::cCalloc) + k (rtprobe::Kind::cRealloc));
    std::fprintf (f, ",\"free_total\":");      jsonU64 (f, rtprobe::freeCallTotal (s));
    std::fprintf (f, "}");
}

//------------------------------------------------------------------------------
// Argument parsing.
//------------------------------------------------------------------------------
struct Options
{
    std::string mode = "full";              // preflight | smoke | full
    std::string productBuild;
    std::string source;
    std::string out;
    std::string predeclared;
    std::string fixturesDir;
    std::string pipeline = "all";           // disabled | enabled | enabled_pressure | all
    std::string fixtureClean;
    std::string fixtureNoise;
    std::string fixtureSilence;
    std::vector<double> rates { 48000.0, 96000.0 };
    std::vector<int> blocks { 128, 512, 4096 };
    std::vector<std::string> inputs { "clean", "noise", "silence" };
    int warmBlocksOverride = 0;
    double targetSeconds = 4.0;
    std::uint32_t seed = 20261008u;
    bool realtime = true;
    bool listMatrix = false;
    bool allowUnavailable = false;
};

bool parseDoubleList (const char* text, std::vector<double>& out)
{
    out.clear();
    std::string s (text);
    std::size_t pos = 0;
    while (pos < s.size())
    {
        const std::size_t comma = s.find (',', pos);
        const std::string tok = s.substr (pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (! tok.empty()) out.push_back (std::strtod (tok.c_str(), nullptr));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return ! out.empty();
}

bool parseIntList (const char* text, std::vector<int>& out)
{
    std::vector<double> d;
    if (! parseDoubleList (text, d)) return false;
    out.clear();
    for (double v : d) out.push_back ((int) v);
    return true;
}

bool parseStringList (const char* text, std::vector<std::string>& out)
{
    out.clear();
    std::string s (text);
    std::size_t pos = 0;
    while (pos < s.size())
    {
        const std::size_t comma = s.find (',', pos);
        const std::string tok = s.substr (pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (! tok.empty()) out.push_back (tok);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return ! out.empty();
}

InputKind parseInput (const std::string& s)
{
    if (s == "noise") return InputKind::noise;
    if (s == "silence") return InputKind::silence;
    return InputKind::clean;
}

bool parseOptions (int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool hasNext = (i + 1 < argc);
        auto next = [&]() -> const char* { return argv[++i]; };
        if (a == "--mode" && hasNext) o.mode = next();
        else if (a == "--product-build" && hasNext) o.productBuild = next();
        else if (a == "--source" && hasNext) o.source = next();
        else if (a == "--out" && hasNext) o.out = next();
        else if (a == "--predeclared" && hasNext) o.predeclared = next();
        else if (a == "--fixtures-dir" && hasNext) o.fixturesDir = next();
        else if (a == "--fixture-clean" && hasNext) o.fixtureClean = next();
        else if (a == "--fixture-noise" && hasNext) o.fixtureNoise = next();
        else if (a == "--fixture-silence" && hasNext) o.fixtureSilence = next();
        else if (a == "--pipeline" && hasNext) o.pipeline = next();
        else if (a == "--rates" && hasNext) { if (! parseDoubleList (next(), o.rates)) return false; }
        else if (a == "--blocks" && hasNext) { if (! parseIntList (next(), o.blocks)) return false; }
        else if (a == "--inputs" && hasNext) { if (! parseStringList (next(), o.inputs)) return false; }
        else if (a == "--warm-blocks" && hasNext)
        {
            const long v = std::strtol (next(), nullptr, 10);
            if (v < kMinWarmBlocks || v > kMaxWarmBlocks) return false;
            o.warmBlocksOverride = (int) v;
        }
        else if (a == "--target-seconds" && hasNext) o.targetSeconds = std::strtod (next(), nullptr);
        else if (a == "--seed" && hasNext) o.seed = (std::uint32_t) std::strtoul (next(), nullptr, 10);
        else if (a == "--fast") o.realtime = false;
        else if (a == "--realtime") o.realtime = true;
        else if (a == "--allow-unavailable-backend") o.allowUnavailable = true;
        else if (a == "--list-matrix") o.listMatrix = true;
        else
        {
            std::fprintf (stderr, "error: unknown or incomplete option '%s'\n", a.c_str());
            return false;
        }
    }
    return true;
}

int computeWarmBlocks (const Options& o, double rate, int block)
{
    if (o.warmBlocksOverride > 0) return o.warmBlocksOverride;
    const double want = o.targetSeconds * rate / (double) block;
    int v = (int) std::lround (want);
    if (v < kMinWarmBlocks) v = kMinWarmBlocks;
    if (v > kMaxWarmBlocks) v = kMaxWarmBlocks;
    return v;
}

std::vector<std::string> pipelineValues (const Options& o)
{
    if (o.pipeline == "disabled") return { "disabled" };
    if (o.pipeline == "enabled") return { "enabled" };
    if (o.pipeline == "enabled_pressure") return { "enabled_pressure" };
    return { "disabled", "enabled", "enabled_pressure" };
}

std::vector<Cell> buildMatrix (const Options& o)
{
    std::vector<Cell> cells;
    for (double rate : o.rates)
        for (int block : o.blocks)
            for (const auto& pipe : pipelineValues (o))
                for (const auto& in : o.inputs)
                {
                    Cell c;
                    c.rate = rate;
                    c.block = block;
                    c.pipeline = pipe;
                    c.input = parseInput (in);
                    c.warmBlocks = computeWarmBlocks (o, rate, block);
                    c.realtimePaced = (pipe == "enabled");
                    char id[160];
                    std::snprintf (id, sizeof (id), "%s_r%.0f_b%d_%s",
                                   pipe.c_str(), rate, block, inputName (c.input));
                    c.id = id;
                    cells.push_back (std::move (c));
                }
    return cells;
}

//------------------------------------------------------------------------------
// One cell replay.
//------------------------------------------------------------------------------
double cellOutputRms (const juce::AudioBuffer<float>& buf, int n)
{
    double sum = 0.0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const auto* p = buf.getReadPointer (ch);
        for (int i = 0; i < n; ++i) sum += (double) p[i] * (double) p[i];
    }
    const double count = (double) n * (double) buf.getNumChannels();
    return count > 0.0 ? std::sqrt (sum / count) : 0.0;
}

void recordCommand (Cell& c, const char* name, bool accepted, int atBlock)
{
    char line[96];
    std::snprintf (line, sizeof (line), "%s@%d:%s", name, atBlock, accepted ? "accepted" : "rejected");
    if (c.commandLog.size() < 64) c.commandLog.push_back (line);
}

bool runCell (GuitarCompanionProcessor& proc, const Options& o, Cell& c, std::FILE* log,
              const InputSet& inputs)
{
    const bool enabled = (c.pipeline != "disabled");

    proc.prepareToPlay (c.rate, c.block);

    juce::AudioBuffer<float> buf (2, c.block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);

    InputGen gen;
    gen.kind = c.input;
    gen.wav = inputs.wavFor (c.input);
    if (gen.wav != nullptr && gen.wav->ok)
    {
        const std::string* p = inputs.pathFor (c.input);
        c.inputSource = std::string ("wav:") + (p != nullptr ? *p : "?");
    }
    else
    {
        c.inputSource = std::string ("builtin:") + inputName (c.input);
    }
    gen.reset (o.seed ^ (std::uint32_t) c.block ^ (std::uint32_t) (unsigned) c.rate);

    // Initial telemetry read: must be zero/unavailable before any callback.
    if (! proc.readJamLiveState (c.stateStart))
        c.stateStart = jam::JamLiveState {};

    // Cold callback (armed).
    gen.fill (buf, c.block);
    rtprobe::resetAll();
    const auto c0 = rtprobe::snapshot();
    {
        rtprobe::arm();
        proc.processBlock (buf, midi);
        rtprobe::disarm();
    }
    c.cold = rtprobe::delta (c0, rtprobe::snapshot());
    c.callbackCount = 1;
    c.audioSampleStart = c.stateStart.audioSampleTime;

    // Enabled Pressure is the intentional offline fast-loop starvation cell: it
    // exercises queue pressure instead of worker/clock receipt. Enabled is
    // real-time paced so the analyzer worker and control worker get wall time.
    const bool pace = o.realtime && (c.pipeline == "enabled");

    if (enabled)
    {
        jam::JamLiveCommand start;
        start.type = jam::JamLiveCommandType::Start;
        const bool accepted = proc.submitJamCommand (start);
        recordCommand (c, "Start", accepted, 0);
        if (! accepted)
        {
            c.measured = false;
            c.unmeasuredReason = "start_rejected_backend_unavailable";
            std::fprintf (log, "  [%s] Start rejected (backend unavailable)\n", c.id.c_str());
            proc.releaseResources();
            return false;
        }
    }

    jam::JamLiveState prev {};
    proc.readJamLiveState (prev);
    c.audioSampleStart = prev.audioSampleTime;
    std::uint64_t expectedSample = prev.audioSampleTime;
    std::uint64_t lastGeneration = prev.clock.generation;
    std::uint64_t lastReceipt = 0;

    // Warm loop.
    rtprobe::resetAll();
    const auto w0 = rtprobe::snapshot();
    double rmsSum = 0.0, rmsMax = 0.0;
    float peak = 0.0f;
    std::uint64_t nonzero = 0;

    const auto startWall = Clock::now();
    TimePoint nextDeadline = startWall;

    for (int i = 0; i < c.warmBlocks; ++i)
    {
        gen.fill (buf, c.block);      // outside the armed region

        if (pace)
        {
            nextDeadline += std::chrono::nanoseconds ((std::int64_t) (1e9 * (double) c.block / c.rate));
            const auto now = Clock::now();
            if (nextDeadline > now)
                std::this_thread::sleep_until (nextDeadline);
        }

        const auto t0 = Clock::now();
        rtprobe::arm();
        proc.processBlock (buf, midi);
        rtprobe::disarm();
        const auto t1 = Clock::now();
        c.callbackWallSumMs += std::chrono::duration<double, std::milli> (t1 - t0).count();
        ++c.callbackCount;

        const double rms = cellOutputRms (buf, c.block);
        rmsSum += rms;
        if (rms > rmsMax) rmsMax = rms;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float m = buf.getMagnitude (ch, 0, c.block);
            if (m > peak) peak = m;
        }
        if (rms > 1.0e-7) ++nonzero;

        // Single-consumer read after the callback (the replay owner is the one
        // reader; the producer is the callback/worker side).
        jam::JamLiveState s {};
        const bool ok = proc.readJamLiveState (s);
        if (! ok) continue;

        if (s.audioSampleTime < expectedSample) c.audioMonotonic = false;
        if (s.audioSampleTime != expectedSample + (std::uint64_t) c.block)
        {
            ++c.audioDeltaMismatches;
            c.audioDeltaOk = false;
        }
        expectedSample = s.audioSampleTime;

        if (s.prepared) c.preparedSeen = true;
        if (s.requestedRunning) c.requestedRunningSeen = true;
        if (s.joinPending) c.joinPendingSeen = true;
        if (s.drumsPlaying) c.drumsPlayingSeen = true;
        if (s.clock.generation != lastGeneration)
        {
            ++c.generationChanges;
            lastGeneration = s.clock.generation;
        }
        if (s.candidateBpm > 0.0f) ++c.candidateBpmNonzero;

        ++c.receiptCount;
        if (s.receiptMeasured)
        {
            ++c.receiptMeasuredCount;
            c.lastReceiptSample = s.lastReceiptSampleTime;
            if (s.lastReceiptSampleTime < s.lastInputHorizonSampleTime)
                ++c.receiptBeforeHorizon;
            if (s.lastEventSampleTime > s.lastInputHorizonSampleTime)
                ++c.eventAfterHorizon;
            if (s.lastReceiptSampleTime >= s.audioSampleTime)
            {
                const std::uint64_t lag = s.lastReceiptSampleTime - s.audioSampleTime;
                if (lag > c.maxReceiptLagSamples) c.maxReceiptLagSamples = lag;
            }
            if (lastReceipt != 0 && s.lastReceiptSampleTime < lastReceipt)
                c.receiptBeforeHorizon = c.receiptBeforeHorizon; // keep monotonic receipt tracked below
            if (s.lastReceiptSampleTime > lastReceipt) lastReceipt = s.lastReceiptSampleTime;
        }

        // Bounded command sequence for the enabled (paced) cells so join/resync/
        // next-bar-stop semantics are exercised, not just Start.
        if (c.pipeline == "enabled")
        {
            const int third = c.warmBlocks / 3;
            if (i == third)
                recordCommand (c, "TapTempo", proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::TapTempo, 0.0 }), i);
            if (i == 2 * third)
                recordCommand (c, "ResyncNextBar", proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::ResyncNextBar, 0.0 }), i);
            if (i == c.warmBlocks - 2)
                recordCommand (c, "StopAtNextBar", proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::StopAtNextBar, 0.0 }), i);
        }
    }

    c.elapsedWallS = std::chrono::duration<double> (Clock::now() - startWall).count();
    c.warm = rtprobe::delta (w0, rtprobe::snapshot());
    c.outRmsMean = c.warmBlocks > 0 ? rmsSum / (double) c.warmBlocks : 0.0;
    c.outRmsMax = rmsMax;
    c.outPeak = peak;
    c.nonzeroBlocks = nonzero;

    proc.readJamLiveState (c.stateEnd);
    c.audioSampleEnd = c.stateEnd.audioSampleTime;

    if (enabled)
    {
        recordCommand (c, "Stop", proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Stop, 0.0 }), c.warmBlocks);
        recordCommand (c, "Reset", proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Reset, 0.0 }), c.warmBlocks);
    }

    proc.releaseResources();
    c.measured = true;

    std::fprintf (log,
                  "  [%-36s] cold(alloc_cxx=%llu alloc_c=%llu lock=%llu) "
                  "warm(alloc_cxx=%llu alloc_c=%llu free=%llu lock=%llu try=%llu cond=%llu) "
                  "%d blocks %.2fs %s rms=%.6f peak=%.4f gen_changes=%llu receipts=%llu/%llu "
                  "before_horizon=%llu\n",
                  c.id.c_str(),
                  (unsigned long long) (rtprobe::allocCallTotal (c.cold)),
                  (unsigned long long) (c.cold.allocCalls[(std::size_t) rtprobe::Kind::cMalloc]
                       + c.cold.allocCalls[(std::size_t) rtprobe::Kind::cCalloc]
                       + c.cold.allocCalls[(std::size_t) rtprobe::Kind::cRealloc]),
                  (unsigned long long) c.cold.lockCalls,
                  (unsigned long long) (c.warm.allocCalls[(std::size_t) rtprobe::Kind::cxxNew]
                       + c.warm.allocCalls[(std::size_t) rtprobe::Kind::cxxNewArray]
                       + c.warm.allocCalls[(std::size_t) rtprobe::Kind::cxxNewNothrow]
                       + c.warm.allocCalls[(std::size_t) rtprobe::Kind::cxxNewAligned]),
                  (unsigned long long) (c.warm.allocCalls[(std::size_t) rtprobe::Kind::cMalloc]
                       + c.warm.allocCalls[(std::size_t) rtprobe::Kind::cCalloc]
                       + c.warm.allocCalls[(std::size_t) rtprobe::Kind::cRealloc]),
                  (unsigned long long) rtprobe::freeCallTotal (c.warm),
                  (unsigned long long) c.warm.lockCalls,
                  (unsigned long long) c.warm.trylockCalls,
                  (unsigned long long) c.warm.condWaitCalls,
                  c.warmBlocks, c.elapsedWallS, c.realtimePaced ? "rt" : "fast",
                  c.outRmsMean, (double) c.outPeak,
                  (unsigned long long) c.generationChanges,
                  (unsigned long long) c.receiptMeasuredCount,
                  (unsigned long long) c.receiptCount,
                  (unsigned long long) c.receiptBeforeHorizon);

    return true;
}

//------------------------------------------------------------------------------
// Dedicated semantics sequence (next-bar vs immediate stop, re-prepare, reset).
//------------------------------------------------------------------------------
struct SemanticsResult
{
    bool ran = false;
    bool startAccepted = false;
    std::uint64_t generationBeforeReprepare = 0;
    std::uint64_t generationAfterReprepare = 0;
    bool stopAtNextBarWasDeferred = false;
    bool stopWasImmediate = false;
    int blocksToStopAtNextBar = -1;
    int blocksToImmediateStop = -1;
    std::uint64_t audioSampleBeforeRelease = 0;
    std::uint64_t audioSampleAfterReprepareFirstBlock = 0;
    bool silentStartNoLock = false;
};

void runSemantics (GuitarCompanionProcessor& proc, double rate, int block, std::FILE* /*log*/,
                   SemanticsResult& r)
{
    r.ran = true;
    proc.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    InputGen gen;
    gen.kind = InputKind::clean;
    gen.reset (777);

    auto step = [&] { gen.fill (buf, block); proc.processBlock (buf, midi); };

    r.startAccepted = proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Start, 0.0 });

    // Advance a couple of bars.
    const std::uint64_t grace = (std::uint64_t) (rate * 2.0);
    int blocks = 0;
    while ((std::uint64_t) blocks * (std::uint64_t) block < grace && blocks < 4096)
    {
        step(); ++blocks;
    }

    jam::JamLiveState s {};
    proc.readJamLiveState (s);

    // StopAtNextBar: the audio owner must keep rendering until the boundary.
    proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::StopAtNextBar, 0.0 });
    jam::JamLiveState afterReq {};
    proc.readJamLiveState (afterReq);
    r.stopAtNextBarWasDeferred = afterReq.drumsPlaying;
    int blocksToStop = 0;
    while (blocksToStop < 4096)
    {
        step(); ++blocksToStop;
        jam::JamLiveState now {};
        proc.readJamLiveState (now);
        if (! now.drumsPlaying) break;
    }
    r.blocksToStopAtNextBar = blocksToStop;

    // Restart then immediate Stop.
    proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Start, 0.0 });
    for (int i = 0; i < 8; ++i) step();
    proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Stop, 0.0 });
    int blocksToImmediate = 0;
    jam::JamLiveState imm {};
    proc.readJamLiveState (imm);
    r.stopWasImmediate = ! imm.drumsPlaying;
    while (blocksToImmediate < 32)
    {
        step(); ++blocksToImmediate;
        jam::JamLiveState now {};
        proc.readJamLiveState (now);
        if (! now.drumsPlaying) break;
    }
    r.blocksToImmediateStop = blocksToImmediate;

    proc.readJamLiveState (s);
    r.generationBeforeReprepare = s.sessionGeneration;
    r.audioSampleBeforeRelease = s.audioSampleTime;

    // Quiescent release, then re-prepare: a new generation/origin is expected.
    proc.releaseResources();
    proc.prepareToPlay (rate, block);
    for (int i = 0; i < 4; ++i) step();
    jam::JamLiveState after {};
    proc.readJamLiveState (after);
    r.generationAfterReprepare = after.sessionGeneration;
    r.audioSampleAfterReprepareFirstBlock = after.audioSampleTime;
    r.silentStartNoLock = (after.clock.lockState == jam::ClockLockState::Acquiring
                           && after.clock.confidence01 == 0.0f
                           && ! after.requestedRunning);

    proc.releaseResources();
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    Options o;
    if (! parseOptions (argc, argv, o))
        return 64;

    const std::vector<Cell> matrix = buildMatrix (o);

    if (o.listMatrix)
    {
        std::printf ("%zu cells\n", matrix.size());
        for (const auto& c : matrix)
            std::printf ("%s warm=%d %s\n", c.id.c_str(), c.warmBlocks,
                         c.realtimePaced ? "realtime" : "fast");
        return 0;
    }

    std::FILE* log = stdout;

    if (o.out.empty())
    {
        std::fprintf (stderr, "error: --out is required\n");
        return 64;
    }
    if (o.productBuild.empty() || o.source.empty())
    {
        std::fprintf (stderr, "error: --product-build and --source are required\n");
        return 64;
    }

    std::fprintf (log, "EVAL-LIVE-001 actual-processor live-Jam replay\n");
    std::fprintf (log, "=============================================\n");
    std::fprintf (log, "source=%s\nproduct=%s\nmode=%s cells=%zu realtime=%d\n\n",
                  o.source.c_str(), o.productBuild.c_str(), o.mode.c_str(),
                  matrix.size(), (int) o.realtime);

    std::fprintf (log, "[instrument self-check]\n");
    if (rtprobe::runSelfCheck (log) != 0)
    {
        std::fprintf (log, "SELFCHECK FAILED - refusing to report processor results\n");
        return 2;
    }
    std::fprintf (log, "  self-check PASS\n\n");

    juce::ScopedJuceInitialiser_GUI gui;
    GuitarCompanionProcessor proc;

    // Optional synthetic WAV fixtures, loaded OUTSIDE any measured region. An
    // explicitly supplied fixture that fails to load is a hard error; the
    // built-in generators are used only when no fixture path is given.
    WavMono wavClean, wavNoise, wavSilence;
    InputSet inputs;
    auto loadFixture = [&] (const std::string& path, WavMono& wav, const std::string*& pathRef,
                            const WavMono*& wavRef)
    {
        if (path.empty()) return true;
        if (! loadWavMono (path, wav))
        {
            std::fprintf (stderr, "error: fixture '%s': %s\n", path.c_str(), wav.error.c_str());
            return false;
        }
        pathRef = &path;
        wavRef = &wav;
        return true;
    };
    const std::string* pClean = nullptr, *pNoise = nullptr, *pSilence = nullptr;
    if (! loadFixture (o.fixtureClean, wavClean, pClean, inputs.wav[0])
        || ! loadFixture (o.fixtureNoise, wavNoise, pNoise, inputs.wav[1])
        || ! loadFixture (o.fixtureSilence, wavSilence, pSilence, inputs.wav[2]))
        return 6;
    inputs.path[0] = pClean;
    inputs.path[1] = pNoise;
    inputs.path[2] = pSilence;

    // Probe the facade once before replaying so a facade-bearing but
    // backend-less product is reported honestly instead of as clean.
    jam::JamLiveState probe {};
    const bool probeOk = proc.readJamLiveState (probe);
    const bool backendUsable = probeOk && probe.backend != jam::JamLiveBackend::unavailable;

    std::vector<Cell> results;
    results.reserve (matrix.size());
    int anyMeasured = 0;
    int enabledRejected = 0;

    for (const auto& base : matrix)
    {
        Cell c = base;
        const bool ok = runCell (proc, o, c, log, inputs);
        if (ok) ++anyMeasured;
        else if (c.pipeline != "disabled") ++enabledRejected;
        results.push_back (std::move (c));
    }

    SemanticsResult sem;
    runSemantics (proc, o.rates.front(), o.blocks.front(), log, sem);

    // ---- Evidence JSON (measured cells; identity/protocol added by the runner).
    std::string jsonPath = o.out + "/cells.json";
    std::FILE* f = std::fopen (jsonPath.c_str(), "w");
    if (f == nullptr)
    {
        std::fprintf (stderr, "error: cannot write %s\n", jsonPath.c_str());
        return 5;
    }

    std::fprintf (f, "{\n");
    std::fprintf (f, "  \"schema\": \"live-jam-replay/cells/1.0\",\n");
    std::fprintf (f, "  \"harness\": \"live-jam-replay\",\n");
    std::fprintf (f, "  \"harness_version\": \"1.0\",\n");
    std::fprintf (f, "  \"processor_constructed\": true,\n");
    std::fprintf (f, "  \"instrument_selfcheck_pass\": true,\n");
    std::fprintf (f, "  \"backend_usable_at_start\": "); jsonBool (f, backendUsable);
    std::fprintf (f, ",\n  \"realtime_paced\": "); jsonBool (f, o.realtime);
    std::fprintf (f, ",\n  \"seed\": "); jsonU64 (f, o.seed);
    std::fprintf (f, ",\n  \"initial_state\": "); writeState (f, probe);
    std::fprintf (f, ",\n  \"semantics\": {");
    std::fprintf (f, "\"ran\":");              jsonBool (f, sem.ran);
    std::fprintf (f, ",\"startAccepted\":");   jsonBool (f, sem.startAccepted);
    std::fprintf (f, ",\"stopAtNextBarWasDeferred\":"); jsonBool (f, sem.stopAtNextBarWasDeferred);
    std::fprintf (f, ",\"stopWasImmediate\":"); jsonBool (f, sem.stopWasImmediate);
    std::fprintf (f, ",\"blocksToStopAtNextBar\":"); jsonI64 (f, sem.blocksToStopAtNextBar);
    std::fprintf (f, ",\"blocksToImmediateStop\":"); jsonI64 (f, sem.blocksToImmediateStop);
    std::fprintf (f, ",\"generationBeforeReprepare\":"); jsonU64 (f, sem.generationBeforeReprepare);
    std::fprintf (f, ",\"generationAfterReprepare\":");  jsonU64 (f, sem.generationAfterReprepare);
    std::fprintf (f, ",\"generationChangedOnReprepare\":");
    jsonBool (f, sem.generationAfterReprepare != sem.generationBeforeReprepare);
    std::fprintf (f, ",\"audioSampleBeforeRelease\":"); jsonU64 (f, sem.audioSampleBeforeRelease);
    std::fprintf (f, ",\"audioSampleAfterReprepareFirstBlock\":");
    jsonU64 (f, sem.audioSampleAfterReprepareFirstBlock);
    std::fprintf (f, ",\"silentStartNoLock\":"); jsonBool (f, sem.silentStartNoLock);
    std::fprintf (f, "},\n");

    std::fprintf (f, "  \"cells\": [\n");
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const Cell& c = results[i];
        std::fprintf (f, "    {");
        std::fprintf (f, "\"id\":\"%s\"", c.id.c_str());
        std::fprintf (f, ",\"rate\":"); jsonNumber (f, c.rate);
        std::fprintf (f, ",\"block\":%d", c.block);
        std::fprintf (f, ",\"pipeline\":\"%s\"", c.pipeline.c_str());
        std::fprintf (f, ",\"input\":\"%s\"", inputName (c.input));
        std::fprintf (f, ",\"input_source\":\"%s\"", c.inputSource.c_str());
        std::fprintf (f, ",\"warm_blocks\":%d", c.warmBlocks);
        std::fprintf (f, ",\"realtime_paced\":"); jsonBool (f, c.realtimePaced);
        std::fprintf (f, ",\"measured\":"); jsonBool (f, c.measured);
        std::fprintf (f, ",\"unmeasured_reason\":");
        if (c.measured) std::fprintf (f, "null");
        else std::fprintf (f, "\"%s\"", c.unmeasuredReason.c_str());

        std::fprintf (f, ",\"cold\":"); fillSnapshotJson (f, c.cold);
        std::fprintf (f, ",\"warm\":"); fillSnapshotJson (f, c.warm);

        std::fprintf (f, ",\"timing\":{");
        std::fprintf (f, "\"callback_count\":"); jsonU64 (f, c.callbackCount);
        std::fprintf (f, ",\"callback_wall_ms_sum\":"); jsonNumber (f, c.callbackWallSumMs);
        std::fprintf (f, ",\"callback_us_per_block\":");
        jsonNumber (f, c.callbackCount > 0 ? c.callbackWallSumMs * 1000.0 / (double) c.callbackCount : 0.0);
        std::fprintf (f, ",\"audio_deadline_ms\":");
        jsonNumber (f, (double) c.block * 1000.0 / c.rate);
        std::fprintf (f, ",\"elapsed_wall_s\":"); jsonNumber (f, c.elapsedWallS);
        std::fprintf (f, "}");

        std::fprintf (f, ",\"output\":{");
        std::fprintf (f, "\"rms_mean\":"); jsonNumber (f, c.outRmsMean);
        std::fprintf (f, ",\"rms_max\":"); jsonNumber (f, c.outRmsMax);
        std::fprintf (f, ",\"peak\":");    jsonNumber (f, (double) c.outPeak);
        std::fprintf (f, ",\"nonzero_blocks\":"); jsonU64 (f, c.nonzeroBlocks);
        std::fprintf (f, "}");

        std::fprintf (f, ",\"state_start\":"); writeState (f, c.stateStart);
        std::fprintf (f, ",\"state_end\":");   writeState (f, c.stateEnd);

        std::fprintf (f, ",\"progression\":{");
        std::fprintf (f, "\"audio_sample_start\":"); jsonU64 (f, c.audioSampleStart);
        std::fprintf (f, ",\"audio_sample_end\":");   jsonU64 (f, c.audioSampleEnd);
        std::fprintf (f, ",\"audio_sample_monotonic\":"); jsonBool (f, c.audioMonotonic);
        std::fprintf (f, ",\"audio_sample_delta_ok\":"); jsonBool (f, c.audioDeltaOk);
        std::fprintf (f, ",\"audio_sample_delta_mismatches\":"); jsonU64 (f, c.audioDeltaMismatches);
        std::fprintf (f, ",\"prepared_seen\":"); jsonBool (f, c.preparedSeen);
        std::fprintf (f, ",\"requested_running_seen\":"); jsonBool (f, c.requestedRunningSeen);
        std::fprintf (f, ",\"join_pending_seen\":"); jsonBool (f, c.joinPendingSeen);
        std::fprintf (f, ",\"drums_playing_seen\":"); jsonBool (f, c.drumsPlayingSeen);
        std::fprintf (f, ",\"generation_changes\":"); jsonU64 (f, c.generationChanges);
        std::fprintf (f, ",\"candidate_bpm_nonzero\":"); jsonU64 (f, c.candidateBpmNonzero);
        std::fprintf (f, ",\"receipt_count\":"); jsonU64 (f, c.receiptCount);
        std::fprintf (f, ",\"receipt_measured_count\":"); jsonU64 (f, c.receiptMeasuredCount);
        std::fprintf (f, ",\"receipt_before_horizon\":"); jsonU64 (f, c.receiptBeforeHorizon);
        std::fprintf (f, ",\"event_after_horizon\":"); jsonU64 (f, c.eventAfterHorizon);
        std::fprintf (f, ",\"max_receipt_lag_samples\":"); jsonU64 (f, c.maxReceiptLagSamples);
        std::fprintf (f, ",\"last_receipt_sample\":"); jsonU64 (f, c.lastReceiptSample);
        std::fprintf (f, "}");

        std::fprintf (f, ",\"commands\":[");
        for (std::size_t k = 0; k < c.commandLog.size(); ++k)
            std::fprintf (f, "%s\"%s\"", k ? "," : "", c.commandLog[k].c_str());
        std::fprintf (f, "]}");
        std::fprintf (f, "%s\n", i + 1 < results.size() ? "," : "");
    }
    std::fprintf (f, "  ],\n");
    std::fprintf (f, "  \"counts\": {");
    std::fprintf (f, "\"cells\":%zu", results.size());
    std::fprintf (f, ",\"measured\":%d", anyMeasured);
    std::fprintf (f, ",\"enabled_rejected\":%d", enabledRejected);
    std::fprintf (f, "}\n");
    std::fprintf (f, "}\n");

    const bool written = std::ferror (f) == 0;
    const bool closed = std::fclose (f) == 0;
    if (! written || ! closed)
    {
        std::fprintf (stderr, "error: failed writing %s\n", jsonPath.c_str());
        return 5;
    }

    std::fprintf (log, "\ncells.json: %s (%zu cells, %d measured, %d enabled rejected)\n",
                  jsonPath.c_str(), results.size(), anyMeasured, enabledRejected);

    // Fail closed: if the live backend is unavailable, enabled evidence is not
    // measurable. The runner treats exit 3 as "harness ready, awaiting a
    // backend-capable product".
    if (! backendUsable && ! o.allowUnavailable)
    {
        std::fprintf (log, "backend unavailable: enabled cells recorded unmeasured (exit 3)\n");
        return 3;
    }
    return 0;
}
