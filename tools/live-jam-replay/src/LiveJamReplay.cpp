// EVAL-LIVE-001 — actual-processor first-live-Jam replay harness (corrected).
//
// Corrections applied under docs/research/live-jam-replay/CORRECTION-CONTRACT.md
// and tools/live-jam-replay/protocol-amendment.json:
//
//   C1  Per-callback advancement is measured from the AUDIO-OWNER plain engine
//       getter DrumEngine::injectedSamplePosition(), read on the callback-owner
//       thread immediately after processBlock and outside the instrumented
//       region. readJamLiveState is a worker-owned coherent latest-value slot
//       and is treated as a coalescing-tolerant report, never as a synchronous
//       per-callback cursor. The expected cursor is never seeded from it.
//   M8  Counters are the CALLBACK-THREAD PATH ONLY (thread-local arming);
//       worker allocations are explicitly unmeasured and no pipeline-wide zero
//       is claimed. Legitimate callback allocations become positive findings.
//   M9  Lag metrics are receipt-horizon, receipt-event and produced-reported;
//       null when no receipt is measured; repeated identical observations are
//       not counted as new receipts.
//   M5  Scope is explicit (smoke | full | diagnostic) and never hides cells.
//   C2  Backend identity is exact; a non-experimentalBTrack default is
//       awaiting-backend, never measured clean.
//
// No processor/editor/engine/frozen-interface source is modified and no stub
// processor is created.
#include "PluginProcessor.h"
#include "LiveJamObserved.h"
#include "ReplayInput.h"
#include "ReplaySupport.h"
#include "RtProbeInstrumentation.h"
#include "jam/IRhythmTracker.h"
#include "jam/JamConfig.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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

void jsonMaybeU64 (std::FILE* f, bool measured, std::uint64_t v)
{
    if (measured) jsonU64 (f, v);
    else std::fprintf (f, "null");
}

const char* backendName (jam::JamLiveBackend b)
{
    return replay::liveBackendName (b);
}

const char* failureName (jam::JamLiveFailure x)
{
    switch (x)
    {
        case jam::JamLiveFailure::none:               return "none";
        case jam::JamLiveFailure::unavailableBackend: return "unavailableBackend";
        case jam::JamLiveFailure::invalidDevice:      return "invalidDevice";
        case jam::JamLiveFailure::workerFailure:      return "workerFailure";
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
// Synthetic input: the shared audio-frame timeline lives in ReplayInput.h.
//------------------------------------------------------------------------------
using replay::InputKind;
using replay::InputGen;

const char* inputName (InputKind k) { return replay::inputKindName (k); }

struct InputSet
{
    const WavMono* wav[3] = { nullptr, nullptr, nullptr };
    const std::string* path[3] = { nullptr, nullptr, nullptr };
    const WavMono* wavFor (InputKind k) const { return wav[(int) k]; }
    const std::string* pathFor (InputKind k) const { return path[(int) k]; }
};

//------------------------------------------------------------------------------
// Per-cell record.
//------------------------------------------------------------------------------
struct Cell
{
    std::string id;
    double rate = 0.0;
    int block = 0;
    std::string pipeline;
    InputKind input = InputKind::clean;
    std::string inputSource;
    std::string inputSignalKind;
    double inputSourceRate = 0.0;
    double deviceRate = 0.0;
    std::string channelMapping;
    int warmBlocks = 0;
    bool realtimePaced = false;

    bool measured = false;
    std::string unmeasuredReason;

    rtprobe::Snapshot cold {};
    rtprobe::Snapshot warm {};

    double callbackWallSumMs = 0.0;
    std::uint64_t callbackCount = 0;
    double elapsedWallS = 0.0;

    double outRmsMean = 0.0, outRmsMax = 0.0;
    float outPeak = 0.0f;
    std::uint64_t nonzeroBlocks = 0;

    jam::JamLiveState stateStart {}, stateEnd {};
    bool baselinePrepared = false;

    // Audio-owner cursor (true per-callback advancement).
    bool audioOwnerMeasured = false;
    bool audioOwnerDeltaOk = true;
    std::uint64_t audioOwnerDeltaMismatches = 0;
    std::uint64_t audioOwnerBackward = 0;
    std::uint64_t audioOwnerStart = 0, audioOwnerEnd = 0;

    // Reported (facade) cursor, coalescing tolerant.
    bool reportedMonotonic = true;
    std::uint64_t reportedFuture = 0;
    std::uint64_t coalescedReads = 0;
    std::uint64_t skippedPublications = 0;
    std::uint64_t reportedCursorStart = 0, reportedCursorEnd = 0;

    // Receipts and lag (measured-flag gated).
    std::uint64_t receiptReads = 0;
    std::uint64_t receiptMeasuredReads = 0;
    std::uint64_t newReceipts = 0;
    std::uint64_t repeatedReceiptReads = 0;
    bool receiptAnyMeasured = false;
    bool receiptOrderViolation = false;
    std::uint64_t receiptAvailLagLast = 0, receiptAvailLagMax = 0;
    std::uint64_t eventDelayLast = 0, eventDelayMax = 0;
    bool workerCursorLagMeasured = false;
    std::uint64_t workerCursorLagLast = 0, workerCursorLagMax = 0;

    bool preparedSeen = false, requestedRunningSeen = false;
    bool joinPendingSeen = false, drumsPlayingSeen = false;
    std::uint64_t generationChanges = 0;
    std::uint64_t candidateBpmNonzero = 0;
    std::uint64_t analysisDrops = 0, observationDrops = 0, userCommandDrops = 0;
    std::uint64_t drumCommandDrops = 0, discontinuities = 0;

    std::vector<std::string> commandLog;
};

struct Finding
{
    std::string cell;
    std::string kind;
    std::string detail;
};

void recordCommand (Cell& c, const char* name, bool accepted, int atBlock)
{
    char line[96];
    std::snprintf (line, sizeof (line), "%s@%d:%s", name, atBlock, accepted ? "accepted" : "rejected");
    if (c.commandLog.size() < 64) c.commandLog.push_back (line);
}

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
// Options.
//------------------------------------------------------------------------------
struct Options
{
    std::string scope = "full";     // smoke | full | diagnostic
    std::string scopeReason;
    std::string productBuild, source, out, predeclared, fixturesDir;
    std::string fixtureClean, fixtureNoise, fixtureSilence;
    std::string pipeline = "all";
    std::vector<double> rates { 48000.0, 96000.0 };
    std::vector<int> blocks { 128, 512, 4096 };
    std::vector<std::string> inputs { "clean", "noise", "silence" };
    int warmBlocksOverride = 0;
    double targetSeconds = 4.0;
    std::uint32_t seed = 20261008u;
    bool realtime = true;
    bool supplemental = true;
    double supplementalSeconds = 16.0;
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
        if (a == "--scope" && hasNext) o.scope = next();
        else if (a == "--scope-reason" && hasNext) o.scopeReason = next();
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
        else if (a == "--no-supplemental") o.supplemental = false;
        else if (a == "--supplemental-seconds" && hasNext) o.supplementalSeconds = std::strtod (next(), nullptr);
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

std::string makeCellId (const std::string& pipe, double rate, int block, InputKind in)
{
    char id[160];
    std::snprintf (id, sizeof (id), "%s_r%.0f_b%d_%s", pipe.c_str(), rate, block, inputName (in));
    return id;
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
                    c.rate = rate; c.block = block; c.pipeline = pipe;
                    c.input = parseInput (in);
                    c.warmBlocks = computeWarmBlocks (o, rate, block);
                    c.realtimePaced = (pipe == "enabled");
                    c.id = makeCellId (pipe, rate, block, c.input);
                    cells.push_back (std::move (c));
                }
    return cells;
}

const char* const kSmokeIds[] = {
    "disabled_r48000_b128_silence",
    "disabled_r48000_b128_noise",
    "enabled_r48000_b128_clean",
    "enabled_r48000_b128_silence",
};

std::vector<Cell> selectScope (const Options& o, std::vector<Cell> all)
{
    if (o.scope == "full") return all;
    if (o.scope == "smoke")
    {
        std::vector<Cell> out;
        for (auto& c : all)
            for (const char* id : kSmokeIds)
                if (c.id == id) out.push_back (c);
        return out;
    }
    return all;   // diagnostic: filters already applied by --pipeline/--blocks/--inputs
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
    gen.deviceRate = c.rate;
    c.deviceRate = c.rate;
    c.inputSignalKind = inputName (c.input);
    c.channelMapping = "mono-replicated";
    if (gen.wav != nullptr && gen.wav->ok)
    {
        const std::string* p = inputs.pathFor (c.input);
        c.inputSource = std::string ("wav:") + (p != nullptr ? *p : "?");
        c.inputSourceRate = gen.wav->rate;
    }
    else
    {
        c.inputSource = std::string ("builtin:") + inputName (c.input);
        c.inputSourceRate = 0.0;
    }
    gen.reset (o.seed ^ (std::uint32_t) c.block ^ (std::uint32_t) (unsigned) c.rate);

    // Defect A: bounded off-callback prepared poll for the baseline coherent
    // state (non-RT, unarmed). Initialize state_start and state_end from it; a
    // later false latest-value read must never reset them.
    replay::StateLatch latch;
    const bool baselinePrepared = latch.pollPrepared (
        [&] (jam::JamLiveState& s) { return proc.readJamLiveState (s); },
        2000,
        [] { std::this_thread::sleep_for (std::chrono::milliseconds (1)); });
    c.baselinePrepared = baselinePrepared;
    c.stateStart = latch.last;
    c.stateEnd = latch.last;

    // Cold callback (armed). The audio-owner cursor is read after the callback on
    // this same callback-owner thread, outside the armed region.
    gen.fill (buf.getArrayOfWritePointers(), buf.getNumChannels(), c.block);
    rtprobe::resetAll();
    const auto c0 = rtprobe::snapshot();
    {
        rtprobe::arm();
        proc.processBlock (buf, midi);
        rtprobe::disarm();
    }
    c.cold = rtprobe::delta (c0, rtprobe::snapshot());
    c.callbackCount = 1;

    std::uint64_t prevActual = proc.drumEngine.injectedSamplePosition();
    c.audioOwnerStart = prevActual;
    if (prevActual != 0) c.audioOwnerMeasured = true;

    if (enabled)
    {
        const bool accepted = proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Start, 0.0 });
        recordCommand (c, "Start", accepted, 0);
        if (! accepted)
        {
            c.measured = false;
            c.unmeasuredReason = "start_rejected_backend_unavailable";
            proc.releaseResources();
            std::fprintf (log, "  [%s] Start rejected (backend unavailable)\n", c.id.c_str());
            return false;
        }
    }

    const bool pace = o.realtime && (c.pipeline == "enabled");

    rtprobe::resetAll();
    const auto w0 = rtprobe::snapshot();
    double rmsSum = 0.0, rmsMax = 0.0;
    float peak = 0.0f;
    std::uint64_t nonzero = 0;

    bool haveLastKey = false;
    replay::ReceiptKey lastReceiptKey {};
    std::uint64_t lastGeneration = 0;
    replay::CursorTracker cursors;

    const auto startWall = Clock::now();
    TimePoint nextDeadline = startWall;

    for (int i = 0; i < c.warmBlocks; ++i)
    {
        gen.fill (buf.getArrayOfWritePointers(), buf.getNumChannels(), c.block);

        if (pace)
        {
            nextDeadline += std::chrono::nanoseconds ((std::int64_t) (1e9 * (double) c.block / c.rate));
            const auto now = Clock::now();
            if (nextDeadline > now) std::this_thread::sleep_until (nextDeadline);
        }

        const auto t0 = Clock::now();
        rtprobe::arm();
        proc.processBlock (buf, midi);
        rtprobe::disarm();
        const auto t1 = Clock::now();
        c.callbackWallSumMs += std::chrono::duration<double, std::milli> (t1 - t0).count();
        ++c.callbackCount;

        // TRUE audio-owner per-callback advancement.
        const std::uint64_t actual = proc.drumEngine.injectedSamplePosition();
        if (actual != 0) c.audioOwnerMeasured = true;
        if (c.audioOwnerMeasured)
        {
            if (actual < prevActual) ++c.audioOwnerBackward;
            else if (actual != prevActual + (std::uint64_t) c.block)
            {
                ++c.audioOwnerDeltaMismatches;
                c.audioOwnerDeltaOk = false;
            }
        }
        prevActual = actual;

        const double rms = cellOutputRms (buf, c.block);
        rmsSum += rms;
        if (rms > rmsMax) rmsMax = rms;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float m = buf.getMagnitude (ch, 0, c.block);
            if (m > peak) peak = m;
        }
        if (rms > 1.0e-7) ++nonzero;

        jam::JamLiveState s {};
        if (! proc.readJamLiveState (s)) continue;   // retain stateEnd on false
        c.stateEnd = s;                               // coherent update only on true

        const std::uint64_t reported = s.audioSampleTime;
        cursors.observe (c.audioOwnerMeasured, actual, reported);
        c.reportedMonotonic = cursors.monotonic;
        c.reportedFuture = cursors.future;
        c.coalescedReads = cursors.coalesced;
        c.skippedPublications = cursors.skipped;
        c.reportedCursorStart = cursors.first;
        c.reportedCursorEnd = reported;

        if (s.prepared) c.preparedSeen = true;
        if (s.requestedRunning) c.requestedRunningSeen = true;
        if (s.joinPending) c.joinPendingSeen = true;
        if (s.drumsPlaying) c.drumsPlayingSeen = true;
        if (s.clock.generation != lastGeneration) { ++c.generationChanges; lastGeneration = s.clock.generation; }
        if (s.candidateBpm > 0.0f) ++c.candidateBpmNonzero;

        c.analysisDrops = s.analysisDrops;
        c.observationDrops = s.observationDrops;
        c.userCommandDrops = s.userCommandDrops;
        c.drumCommandDrops = s.drumCommandDrops;
        c.discontinuities = s.discontinuities;

        ++c.receiptReads;
        if (s.receiptMeasured)
        {
            c.receiptAnyMeasured = true;
            ++c.receiptMeasuredReads;

            const auto receiptKey = replay::ReceiptKey::from (s);
            const bool sameKey = haveLastKey && receiptKey == lastReceiptKey;
            if (sameKey)
            {
                ++c.repeatedReceiptReads;
            }
            else
            {
                ++c.newReceipts;
                lastReceiptKey = receiptKey;
                haveLastKey = true;

                if (s.lastReceiptSampleTime < s.lastInputHorizonSampleTime
                    || s.lastReceiptSampleTime < s.lastEventSampleTime)
                    c.receiptOrderViolation = true;
                else
                {
                    const std::uint64_t avail = s.lastReceiptSampleTime - s.lastInputHorizonSampleTime;
                    const std::uint64_t ev = s.lastReceiptSampleTime - s.lastEventSampleTime;
                    c.receiptAvailLagLast = avail;
                    c.eventDelayLast = ev;
                    if (avail > c.receiptAvailLagMax) c.receiptAvailLagMax = avail;
                    if (ev > c.eventDelayMax) c.eventDelayMax = ev;
                }
            }
        }

        if (c.audioOwnerMeasured && cursors.have)
        {
            c.workerCursorLagMeasured = true;
            if (actual >= reported)
            {
                const std::uint64_t lag = actual - reported;
                c.workerCursorLagLast = lag;
                if (lag > c.workerCursorLagMax) c.workerCursorLagMax = lag;
            }
        }

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

    c.audioOwnerEnd = proc.drumEngine.injectedSamplePosition();

    // Defect A: final read is a temp that replaces state_end ONLY on true; a
    // false latest-value read must never reset the latched coherent state.
    {
        jam::JamLiveState tmp {};
        if (proc.readJamLiveState (tmp)) c.stateEnd = tmp;
    }

    if (enabled)
    {
        recordCommand (c, "Stop", proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Stop, 0.0 }), c.warmBlocks);
        recordCommand (c, "Reset", proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Reset, 0.0 }), c.warmBlocks);
    }

    proc.releaseResources();
    c.measured = true;

    std::fprintf (log,
                  "  [%-36s] cold(cxx=%llu c=%llu lock=%llu) "
                  "warm(cxx=%llu c=%llu free=%llu lock=%llu) %d blocks %.2fs %s "
                  "ao=%s(delta_bad=%llu back=%llu) rep(coalesced=%llu skipped=%llu future=%llu) "
                  "new_receipts=%llu repeat=%llu rms=%.6f\n",
                  c.id.c_str(),
                  (unsigned long long) (c.cold.allocCalls[(std::size_t) rtprobe::Kind::cxxNew]
                       + c.cold.allocCalls[(std::size_t) rtprobe::Kind::cxxNewArray]
                       + c.cold.allocCalls[(std::size_t) rtprobe::Kind::cxxNewNothrow]
                       + c.cold.allocCalls[(std::size_t) rtprobe::Kind::cxxNewAligned]),
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
                  c.warmBlocks, c.elapsedWallS, c.realtimePaced ? "rt" : "fast",
                  c.audioOwnerMeasured ? "measured" : "unmeasured",
                  (unsigned long long) c.audioOwnerDeltaMismatches,
                  (unsigned long long) c.audioOwnerBackward,
                  (unsigned long long) c.coalescedReads,
                  (unsigned long long) c.skippedPublications,
                  (unsigned long long) c.reportedFuture,
                  (unsigned long long) c.newReceipts,
                  (unsigned long long) c.repeatedReceiptReads,
                  c.outRmsMean);

    return true;
}

//------------------------------------------------------------------------------
// Supplemental scenarios.
//------------------------------------------------------------------------------
struct ScenarioResult
{
    std::string id;
    bool ran = false;
    bool injected = false;
    std::string backendKind;
    std::string backendFirst;
    std::string backendLast;
    bool backendChanged = false;
    std::string unmeasuredReason;
    std::string unmeasuredReasonCode;
    bool startAccepted = false;
    bool joinObserved = false;
    bool firstJoinObserved = false;
    bool secondJoinObserved = false;
    bool enginePlayingObserved = false;
    double configuredAcquisitionSeconds = 0.0;
    double firstJoinBudgetSeconds = 0.0;
    std::uint64_t blocksToJoin = 0;
    std::uint64_t callbacks = 0;
    std::uint64_t stepsFired = 0;
    double outputRms = 0.0;
    std::uint64_t outputNonzeroBlocks = 0;
    bool stopAtNextBarDeferred = false;
    std::uint64_t blocksToStopAtNextBar = 0;
    bool stopNowStopped = false;
    bool stopNowAccepted = false;
    std::uint64_t blocksToStopNow = 0;
    bool resyncAccepted = false;
    bool resyncEffectObserved = false;
    int resyncPhaseBefore = -1;
    int resyncPhaseAfter = -1;
    std::uint64_t resyncStepSample = 0;
    std::uint64_t resyncSubmitCursor = 0;
    std::uint64_t resyncObservedEnd = 0;
    std::uint64_t resyncOwnerCommandDelta = 0;
    // Drum-only zero-input window (seventh correction)
    std::string drumOnlyInputKind = "silence";
    double drumOnlyWarmupSeconds = 0.0;
    double drumOnlyMeasuredSeconds = 0.0;
    std::uint64_t drumOnlySampleCount = 0;
    double drumOnlySampleRate = 0.0;
    double drumOnlyRms = 0.0;
    double drumOnlyPeak = 0.0;
    std::uint64_t drumOnlyNonzeroBlocks = 0;
    std::uint64_t drumOnlyStepsDelta = 0;
    bool drumOnlyEnginePlaying = false;
    bool drumOnlySamplerLoaded = false;
    bool drumOnlyUseVst = true;
    bool drumOnlyZeroInput = false;
    std::string drumOnlyAllocatorCoverage = "unmeasured";
    std::uint64_t generationBeforeReprepare = 0;
    std::uint64_t generationAfterReprepare = 0;
    bool generationChangedOnReprepare = false;
    bool sessionGenerationChanged = false;
    bool releasedConfirmed = false;
    bool paced = false;
    double wallSeconds = 0.0;
    bool shutdownReleased = false;
    std::uint64_t callbackAllocCxx = 0, callbackAllocC = 0, callbackFree = 0, callbackLocks = 0;
    bool audioOwnerDeltaOk = true;
    std::uint64_t audioOwnerDeltaMismatches = 0;
    double audioOwnerObservedS = 0.0;
};

void runDefaultCleanLong (GuitarCompanionProcessor& proc, const Options& o, ScenarioResult& r,
                          std::FILE* log)
{
    r.id = "default_clean_long";
    r.injected = false;
    r.ran = true;
    const double rate = 48000.0;
    const int block = 512;
    proc.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    InputGen gen; gen.kind = InputKind::clean; gen.deviceRate = rate; gen.reset (4242);

    // Capture the actual default backend from the first coherent prepared state,
    // independent of join. The backend identity is never derived from playback.
    replay::BackendObservation backend;
    {
        jam::JamLiveState s {};
        if (proc.readJamLiveState (s)) backend.observe (s);
    }

    r.startAccepted = proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Start, 0.0 });

    const std::uint64_t totalBlocks = (std::uint64_t) (o.supplementalSeconds * rate / block);
    std::uint64_t prev = proc.drumEngine.injectedSamplePosition();
    bool measured = prev != 0;
    const auto startWall = Clock::now();
    TimePoint nextDeadline = startWall;

    rtprobe::resetAll();
    const auto s0 = rtprobe::snapshot();
    for (std::uint64_t i = 0; i < totalBlocks; ++i)
    {
        gen.fill (buf.getArrayOfWritePointers(), buf.getNumChannels(), block);
        nextDeadline += std::chrono::nanoseconds ((std::int64_t) (1e9 * (double) block / rate));
        if (nextDeadline > Clock::now()) std::this_thread::sleep_until (nextDeadline);

        rtprobe::arm(); proc.processBlock (buf, midi); rtprobe::disarm();
        ++r.callbacks;
        const double rms = cellOutputRms (buf, block);
        r.outputRms += rms;
        if (rms > 1.0e-7) ++r.outputNonzeroBlocks;

        const std::uint64_t actual = proc.drumEngine.injectedSamplePosition();
        if (actual != 0) measured = true;
        if (measured && actual != prev + (std::uint64_t) block) { ++r.audioOwnerDeltaMismatches; r.audioOwnerDeltaOk = false; }
        prev = actual;

        jam::JamLiveState s {};
        if (proc.readJamLiveState (s))
        {
            backend.observe (s);
            if (! r.joinObserved && s.drumsPlaying)
            {
                r.joinObserved = true;
                r.blocksToJoin = (std::uint64_t) (i + 1);
            }
        }
    }
    r.audioOwnerObservedS = std::chrono::duration<double> (Clock::now() - startWall).count();
    r.backendKind = backend.label();
    r.backendFirst = replay::liveBackendName (backend.first);
    r.backendLast = replay::liveBackendName (backend.last);
    r.backendChanged = backend.changed;
    r.stepsFired = proc.drumEngine.injectedStepsFired();
    r.outputRms = r.callbacks > 0 ? r.outputRms / (double) r.callbacks : 0.0;
    const auto s1 = rtprobe::snapshot();
    const auto d = rtprobe::delta (s0, s1);
    r.callbackAllocCxx = d.allocCalls[(std::size_t) rtprobe::Kind::cxxNew]
        + d.allocCalls[(std::size_t) rtprobe::Kind::cxxNewArray]
        + d.allocCalls[(std::size_t) rtprobe::Kind::cxxNewNothrow]
        + d.allocCalls[(std::size_t) rtprobe::Kind::cxxNewAligned];
    r.callbackAllocC = d.allocCalls[(std::size_t) rtprobe::Kind::cMalloc]
        + d.allocCalls[(std::size_t) rtprobe::Kind::cCalloc]
        + d.allocCalls[(std::size_t) rtprobe::Kind::cRealloc];
    r.callbackFree = rtprobe::freeCallTotal (d);
    r.callbackLocks = d.lockCalls + d.trylockCalls + d.condWaitCalls;
    proc.releaseResources();
    std::fprintf (log, "  scenario[%s] start=%d join=%d blocks_to_join=%llu %.1fs\n",
                  r.id.c_str(), (int) r.startAccepted, (int) r.joinObserved,
                  (unsigned long long) r.blocksToJoin, r.audioOwnerObservedS);
}

#ifdef LIVE_JAM_HAVE_TRACKER_INJECTION
class ScriptedInjectedTracker : public jam::IRhythmTracker
{
public:
    void reset (double sampleRate) override { rate_ = sampleRate; }
    jam::RhythmObservation process (const jam::AnalysisFrame& frame) override
    {
        jam::RhythmObservation o;
        o.inputSampleTime = frame.sampleTime;
        o.sourceSampleRate = rate_;
        o.bpmCandidate = 120.0f;
        o.beatConfidence01 = 1.0f;
        o.onsetStrength01 = 0.8f;
        o.energyRmsDbfs = -20.0f;
        o.transientDensity01 = 0.5f;
        o.phaseValid = true;
        const double samplesPerBeat = rate_ * 60.0 / 120.0;
        const double pos = std::fmod ((double) frame.sampleTime, samplesPerBeat);
        o.beatPhase01 = (float) (pos / samplesPerBeat);
        o.beatEvent = (frame.sampleTime % (std::uint64_t) samplesPerBeat) < frame.numSamples;
        o.silence = false;
        return o;
    }
    const char* id() const noexcept override { return "scripted-injected-120"; }
private:
    double rate_ = 48000.0;
};
#endif

void runInjectedJoinStop (GuitarCompanionProcessor& proc, const Options& o, ScenarioResult& r,
                          std::FILE* log)
{
    r.id = "injected_join_stop_resync";
    r.injected = true;
#ifdef LIVE_JAM_HAVE_TRACKER_INJECTION
    (void) o;
    r.ran = true;
    if (! proc.setJamTrackerForTesting (std::make_unique<ScriptedInjectedTracker>()))
    {
        r.ran = false;
        r.unmeasuredReason = "setJamTrackerForTesting rejected (session prepared)";
        r.unmeasuredReasonCode = "set_tracker_rejected";
        return;
    }
    const double rate = 48000.0;
    const int block = 512;
    proc.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    InputGen gen; gen.kind = InputKind::clean; gen.deviceRate = rate; gen.reset (999);

    replay::StateLatch latch;
    latch.pollPrepared ([&] (jam::JamLiveState& s) { return proc.readJamLiveState (s); },
                        2000, [] { std::this_thread::sleep_for (std::chrono::milliseconds (1)); });
    replay::BackendObservation backend;
    backend.observe (latch.last);

    const auto startWall = Clock::now();
    TimePoint nextDeadline = startWall;
    auto pacedStep = [&] {
        nextDeadline += std::chrono::nanoseconds ((std::int64_t) (1e9 * (double) block / rate));
        if (nextDeadline > Clock::now()) std::this_thread::sleep_until (nextDeadline);
        gen.fill (buf.getArrayOfWritePointers(), buf.getNumChannels(), block);
        proc.processBlock (buf, midi);
        ++r.callbacks;
        const double rms = cellOutputRms (buf, block);
        r.outputRms += rms;
        if (rms > 1.0e-7) ++r.outputNonzeroBlocks;
    };
    auto readState = [&] (jam::JamLiveState& s) -> bool {
        if (proc.readJamLiveState (s)) { latch.updateFrom (true, s); backend.observe (s); return true; }
        s = latch.last;   // retain the last coherent snapshot on false
        return false;
    };
    auto enginePlaying = [&] { return proc.drumEngine.injectedPlaying(); };
    auto stepsFired = [&] { return proc.drumEngine.injectedStepsFired(); };

    r.paced = true;
    r.startAccepted = proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Start, 0.0 });

    // Allow acquisition plus two 4/4 bars at the scripted 120 BPM. The clock
    // configuration is unchanged; this is a transport test, not the guitar
    // acquisition-quality gate.
    r.configuredAcquisitionSeconds = jam::ClockConfig {}.acquireWindowSeconds;
    r.firstJoinBudgetSeconds = r.configuredAcquisitionSeconds + 4.0;
    const std::uint64_t steps0 = stepsFired();
    const int maxJoinBlocks = (int) std::ceil (r.firstJoinBudgetSeconds * rate / block);
    for (int i = 0; i < maxJoinBlocks; ++i)
    {
        pacedStep();
        jam::JamLiveState s {};
        readState (s);
        if ((s.drumsPlaying || enginePlaying()) && stepsFired() > steps0)
        {
            r.joinObserved = true; r.firstJoinObserved = true; r.enginePlayingObserved = true;
            r.blocksToJoin = (std::uint64_t) (i + 1);
            break;
        }
    }
    r.stepsFired = stepsFired();

    // StopAtNextBar: deferred if the engine is actually playing at submit and
    // keeps playing for at least the next serviced block.
    proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::StopAtNextBar, 0.0 });
    const bool playingAtSubmit = enginePlaying();
    if (playingAtSubmit) pacedStep();
    r.stopAtNextBarDeferred = playingAtSubmit && enginePlaying();
    const int maxStopNext = (int) (4.0 * rate / block);
    for (int i = 0; i < maxStopNext; ++i)
    {
        pacedStep();
        if (! enginePlaying()) { r.blocksToStopAtNextBar = (std::uint64_t) (i + 1); break; }
    }

    // Second actual join before StopNow.
    proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Start, 0.0 });
    const std::uint64_t steps1 = stepsFired();
    const int maxJoin2 = (int) (8.0 * rate / block);
    for (int i = 0; i < maxJoin2; ++i)
    {
        pacedStep();
        jam::JamLiveState s {};
        readState (s);
        if ((s.drumsPlaying || enginePlaying()) && stepsFired() > steps1)
        {
            r.secondJoinObserved = true;
            break;
        }
    }
    r.stepsFired = stepsFired();

    // StopNow: only meaningful after a second join; measured in serviced blocks.
    if (r.secondJoinObserved)
    {
        r.stopNowAccepted = proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Stop, 0.0 });
        const int maxStopNow = (int) (2.0 * rate / block);
        for (int i = 0; i < maxStopNow; ++i)
        {
            pacedStep();
            if (! enginePlaying()) { r.stopNowStopped = true; r.blocksToStopNow = (std::uint64_t) (i + 1); break; }
        }
    }

    // Resync phase proof (sixth correction): a THIRD actual join with no resync,
    // then ResyncNextBar alone, then an engine-phase assertion. An ordinary join
    // must not be able to satisfy this.
    proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::Start, 0.0 });
    const std::uint64_t steps2 = stepsFired();
    const int maxJoin3 = (int) (8.0 * rate / block);
    for (int i = 0; i < maxJoin3; ++i)
    {
        pacedStep();
        jam::JamLiveState s {};
        readState (s);
        if ((s.drumsPlaying || enginePlaying()) && stepsFired() > steps2)
            break;
    }

    // Wait for a mid-bar baseline phase: injectedNextStep in [2,14] (not the
    // downbeat 1), engine playing.
    const int maxPhase = (int) (2.0 * rate / block);
    for (int i = 0; i < maxPhase; ++i)
    {
        const int ns = proc.drumEngine.injectedNextStep();
        if (enginePlaying() && ns >= 2 && ns <= 14) break;
        pacedStep();
    }

    const int phaseBefore = proc.drumEngine.injectedNextStep();
    const std::uint64_t cmdBefore = proc.drumEngine.injectedCommandCount();
    const std::uint64_t submitCursor = proc.drumEngine.injectedSamplePosition();
    const bool baselinePlaying = enginePlaying();
    r.resyncPhaseBefore = phaseBefore;
    r.resyncSubmitCursor = submitCursor;

    r.resyncAccepted = proc.submitJamCommand (jam::JamLiveCommand { jam::JamLiveCommandType::ResyncNextBar, 0.0 });
    const std::uint64_t steps3 = stepsFired();
    const int maxResync = (int) (2.0 * rate / block);
    for (int i = 0; i < maxResync; ++i)
    {
        pacedStep();
        jam::JamLiveState s {};
        readState (s);
        const std::uint64_t cmdNow = proc.drumEngine.injectedCommandCount();
        if (cmdNow > cmdBefore && stepsFired() > steps3 && enginePlaying())
        {
            r.resyncObservedEnd = proc.drumEngine.injectedSamplePosition();
            r.resyncPhaseAfter = proc.drumEngine.injectedNextStep();
            r.resyncStepSample = proc.drumEngine.injectedLastStepSample();
            r.resyncOwnerCommandDelta = cmdNow - cmdBefore;
            break;
        }
    }
    if (r.resyncObservedEnd == 0)
        r.resyncObservedEnd = proc.drumEngine.injectedSamplePosition();
    r.resyncEffectObserved = baselinePlaying
        && r.resyncPhaseBefore >= 2 && r.resyncPhaseBefore <= 14
        && r.resyncPhaseAfter == 1
        && r.resyncOwnerCommandDelta >= 1
        && r.resyncStepSample >= r.resyncSubmitCursor
        && r.resyncStepSample < r.resyncObservedEnd;
    r.stepsFired = stepsFired();

    // Seventh correction: drum-only zero-input window on the actual processor
    // output. Every callback buffer is written exact-zero by the caller; the
    // embedded kit (use_vst=false, samplesLoaded=true) is the only source.
    gen.kind = InputKind::silence;
    gen.wav = nullptr;
    r.drumOnlyInputKind = "silence";
    r.drumOnlyWarmupSeconds = 0.5;
    r.drumOnlyMeasuredSeconds = 1.0;
    r.drumOnlySampleRate = rate;
    r.drumOnlyZeroInput = true;
    r.drumOnlyAllocatorCoverage = "unmeasured";
    const int warmupBlocks = (int) std::ceil (r.drumOnlyWarmupSeconds * rate / block);
    r.drumOnlyWarmupSeconds = (double) warmupBlocks * block / rate;
    for (int i = 0; i < warmupBlocks; ++i) pacedStep();
    const std::uint64_t winStepsBefore = stepsFired();
    const int measureBlocks = (int) std::ceil (r.drumOnlyMeasuredSeconds * rate / block);
    r.drumOnlyMeasuredSeconds = (double) measureBlocks * block / rate;
    double winRmsSum = 0.0;
    double winPeak = 0.0;
    std::uint64_t winNonzero = 0;
    for (int i = 0; i < measureBlocks; ++i)
    {
        pacedStep();
        const double rms = cellOutputRms (buf, block);
        winRmsSum += rms;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float m = buf.getMagnitude (ch, 0, block);
            if (m > winPeak) winPeak = m;
        }
        if (rms > 1.0e-7) ++winNonzero;
    }
    r.drumOnlySampleCount = (std::uint64_t) measureBlocks * (std::uint64_t) block;
    r.drumOnlyRms = measureBlocks > 0 ? winRmsSum / (double) measureBlocks : 0.0;
    r.drumOnlyPeak = winPeak;
    r.drumOnlyNonzeroBlocks = winNonzero;
    r.drumOnlyStepsDelta = stepsFired() - winStepsBefore;
    r.drumOnlyEnginePlaying = enginePlaying();
    r.drumOnlySamplerLoaded = proc.drumEngine.samplesLoaded();
    r.drumOnlyUseVst = proc.drumEngine.useVst.load();
    r.stepsFired = stepsFired();

    // Session generation via prepare cold state (not the per-tick clock gen).
    jam::JamLiveState s {};
    readState (s);
    r.generationBeforeReprepare = s.sessionGeneration;

    proc.releaseResources();
    const bool releasedOk = latch.pollReleased (
        [&] (jam::JamLiveState& x) { return proc.readJamLiveState (x); },
        2000, [] { std::this_thread::sleep_for (std::chrono::milliseconds (1)); });
    r.releasedConfirmed = releasedOk && ! latch.last.prepared && ! latch.last.drumsPlaying;

    proc.prepareToPlay (rate, block);
    latch.pollPrepared ([&] (jam::JamLiveState& x) { return proc.readJamLiveState (x); },
                        2000, [] { std::this_thread::sleep_for (std::chrono::milliseconds (1)); });
    r.generationAfterReprepare = latch.last.sessionGeneration;
    r.generationChangedOnReprepare = (r.generationAfterReprepare != r.generationBeforeReprepare);
    r.sessionGenerationChanged = r.generationChangedOnReprepare;

    r.backendKind = backend.label();
    r.backendFirst = replay::liveBackendName (backend.first);
    r.backendLast = replay::liveBackendName (backend.last);
    r.backendChanged = backend.changed;
    r.outputRms = r.callbacks > 0 ? r.outputRms / (double) r.callbacks : 0.0;

    proc.releaseResources();
    const bool shutOk = latch.pollReleased (
        [&] (jam::JamLiveState& x) { return proc.readJamLiveState (x); },
        2000, [] { std::this_thread::sleep_for (std::chrono::milliseconds (1)); });
    r.shutdownReleased = shutOk && ! latch.last.prepared && ! latch.last.drumsPlaying;
    r.wallSeconds = std::chrono::duration<double> (Clock::now() - startWall).count();

    std::fprintf (log, "  scenario[%s] firstJoin=%d secondJoin=%d steps=%llu stopNextDeferred=%d "
                       "stopNow=%d resyncEffect=%d genChanged=%d released=%d shut=%d %.1fs\n",
                  r.id.c_str(), (int) r.firstJoinObserved, (int) r.secondJoinObserved,
                  (unsigned long long) r.stepsFired, (int) r.stopAtNextBarDeferred,
                  (int) r.stopNowStopped, (int) r.resyncEffectObserved,
                  (int) r.sessionGenerationChanged, (int) r.releasedConfirmed,
                  (int) r.shutdownReleased, r.wallSeconds);
#else
    r.ran = false;
    r.unmeasuredReason = "pipeline setJamTrackerForTesting seam absent at build time";
    r.unmeasuredReasonCode = "seam_absent";
    (void) proc; (void) o; (void) log;
#endif
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    Options o;
    if (! parseOptions (argc, argv, o))
        return 64;

    if (o.scope != "smoke" && o.scope != "full" && o.scope != "diagnostic")
    {
        std::fprintf (stderr, "error: --scope must be smoke|full|diagnostic\n");
        return 64;
    }
    if (o.scope == "diagnostic" && o.scopeReason.empty())
    {
        std::fprintf (stderr, "error: --scope diagnostic requires --scope-reason\n");
        return 64;
    }

    const std::vector<Cell> matrix = selectScope (o, buildMatrix (o));

    if (o.listMatrix)
    {
        std::printf ("scope=%s cells=%zu\n", o.scope.c_str(), matrix.size());
        for (const auto& c : matrix)
            std::printf ("%s warm=%d %s\n", c.id.c_str(), c.warmBlocks,
                         c.realtimePaced ? "realtime" : "fast");
        return 0;
    }

    std::FILE* log = stdout;

    if (o.out.empty() || o.productBuild.empty() || o.source.empty())
    {
        std::fprintf (stderr, "error: --out, --product-build and --source are required\n");
        return 64;
    }

    std::fprintf (log, "EVAL-LIVE-001 actual-processor live-Jam replay (corrected)\n");
    std::fprintf (log, "=========================================================\n");
    std::fprintf (log, "source=%s\nproduct=%s\nscope=%s cells=%zu realtime=%d\n\n",
                  o.source.c_str(), o.productBuild.c_str(), o.scope.c_str(),
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

    WavMono wavClean, wavNoise, wavSilence;
    InputSet inputs;
    auto loadFixture = [&] (const std::string& path, WavMono& wav, const std::string*& pathRef,
                            const WavMono*& wavRef) -> bool
    {
        if (path.empty()) return true;
        if (! loadWavMono (path, wav))
        {
            std::fprintf (stderr, "error: fixture '%s': %s\n", path.c_str(), wav.error.c_str());
            return false;
        }
        pathRef = &path; wavRef = &wav;
        return true;
    };
    const std::string* pClean = nullptr, *pNoise = nullptr, *pSilence = nullptr;
    if (! loadFixture (o.fixtureClean, wavClean, pClean, inputs.wav[0])
        || ! loadFixture (o.fixtureNoise, wavNoise, pNoise, inputs.wav[1])
        || ! loadFixture (o.fixtureSilence, wavSilence, pSilence, inputs.wav[2]))
        return 6;
    inputs.path[0] = pClean; inputs.path[1] = pNoise; inputs.path[2] = pSilence;

    // N1 readiness bootstrap: prepare FIRST (proper cold publish), then poll the
    // latest-value slot off-callback for a coherent prepared tag. This avoids a
    // single-sequence race that would falsely report awaiting-backend on a good
    // product. Bootstrap is outside the armed region and excluded from counters.
    jam::JamLiveState boot {};
    bool bootstrapReady = false;
    int bootstrapAttempts = 0;
    {
        proc.prepareToPlay (48000.0, 512);
        const auto pr = replay::pollUntil (
            [&]
            {
                jam::JamLiveState s {};
                if (! proc.readJamLiveState (s)) return false;
                if (! s.prepared) return false;
                boot = s;
                return true;
            },
            2000,
            [] { std::this_thread::sleep_for (std::chrono::milliseconds (1)); });
        bootstrapReady = pr.ready;
        bootstrapAttempts = pr.attempts;
        if (! bootstrapReady)
        {
            jam::JamLiveState s {};
            if (proc.readJamLiveState (s)) boot = s;
        }
        proc.releaseResources();
    }
    const std::string backendKind = backendName (boot.backend);
    const bool backendUsable = (boot.backend == jam::JamLiveBackend::experimentalBTrack);

    std::vector<Cell> results;
    results.reserve (matrix.size());
    int anyMeasured = 0, enabledRejected = 0;
    for (const auto& base : matrix)
    {
        Cell c = base;
        const bool ok = runCell (proc, o, c, log, inputs);
        if (ok) ++anyMeasured;
        else if (c.pipeline != "disabled") ++enabledRejected;
        results.push_back (std::move (c));
    }

    // Findings: callback-path allocations/frees/locks that actually occurred.
    std::vector<Finding> findings;
    for (const auto& c : results)
    {
        if (! c.measured) continue;
        auto add = [&] (const rtprobe::Snapshot& s, const char* phase)
        {
            const std::uint64_t a = rtprobe::allocCallTotal (s);
            const std::uint64_t fr = rtprobe::freeCallTotal (s);
            const std::uint64_t lk = s.lockCalls + s.trylockCalls + s.unlockCalls + s.condWaitCalls;
            if (a || fr || lk)
            {
                char d[192];
                std::snprintf (d, sizeof (d), "alloc=%llu free=%llu lock=%llu",
                               (unsigned long long) a, (unsigned long long) fr, (unsigned long long) lk);
                findings.push_back ({ c.id, std::string (phase) + "_callback_rt_ops", d });
            }
        };
        add (c.cold, "cold");
        add (c.warm, "warm");
    }

    std::vector<ScenarioResult> scenarios;
    if (o.supplemental)
    {
        std::fprintf (log, "\n[supplemental scenarios]\n");
        ScenarioResult def;
        runDefaultCleanLong (proc, o, def, log);
        scenarios.push_back (def);
        GuitarCompanionProcessor injectedProc;
        ScenarioResult inj;
        runInjectedJoinStop (injectedProc, o, inj, log);
        scenarios.push_back (inj);
    }

    std::string jsonPath = o.out + "/cells.json";
    std::FILE* f = std::fopen (jsonPath.c_str(), "w");
    if (f == nullptr)
    {
        std::fprintf (stderr, "error: cannot write %s\n", jsonPath.c_str());
        return 5;
    }

    std::fprintf (f, "{\n");
    std::fprintf (f, "  \"schema\": \"live-jam-replay/cells/1.1\",\n");
    std::fprintf (f, "  \"harness\": \"live-jam-replay\",\n");
    std::fprintf (f, "  \"harness_version\": \"1.1\",\n");
    std::fprintf (f, "  \"processor_constructed\": true,\n");
    std::fprintf (f, "  \"instrument_selfcheck_pass\": true,\n");
    std::fprintf (f, "  \"scope\": \"%s\",\n", o.scope.c_str());
    std::fprintf (f, "  \"scope_reason\": \"%s\",\n", o.scopeReason.c_str());
    std::fprintf (f, "  \"allocation_scope\": \"callback-thread-path-only\",\n");
    std::fprintf (f, "  \"worker_allocations\": {\"measured\": false, \"reason\": \"thread-local arming counts only the replay/callback thread; analyzer/worker allocations are unmeasured\"},\n");
    std::fprintf (f, "  \"backend_kind\": \"%s\",\n", backendKind.c_str());
    std::fprintf (f, "  \"backend_usable_at_start\": "); jsonBool (f, backendUsable);
    std::fprintf (f, ",\n  \"realtime_paced\": "); jsonBool (f, o.realtime);
    std::fprintf (f, ",\n  \"seed\": "); jsonU64 (f, o.seed);
    std::fprintf (f, ",\n  \"bootstrap\": {\"attempts\":%d,\"ready\":", bootstrapAttempts);
    jsonBool (f, bootstrapReady);
    std::fprintf (f, ",\"backend_kind\":\"%s\"", backendKind.c_str());
    std::fprintf (f, ",\"backend_usable\":"); jsonBool (f, backendUsable);
    std::fprintf (f, "}");
    std::fprintf (f, ",\n  \"initial_state\": "); writeState (f, boot);
    std::fprintf (f, ",\n  \"expected_cell_ids\": [");
    for (std::size_t i = 0; i < matrix.size(); ++i)
        std::fprintf (f, "%s\"%s\"", i ? "," : "", matrix[i].id.c_str());
    std::fprintf (f, "],\n");

    std::fprintf (f, "  \"findings\": [");
    for (std::size_t i = 0; i < findings.size(); ++i)
        std::fprintf (f, "%s{\"cell\":\"%s\",\"kind\":\"%s\",\"detail\":\"%s\"}",
                      i ? "," : "", findings[i].cell.c_str(),
                      findings[i].kind.c_str(), findings[i].detail.c_str());
    std::fprintf (f, "],\n");

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
        std::fprintf (f, ",\"input_signal_kind\":\"%s\"", c.inputSignalKind.c_str());
        std::fprintf (f, ",\"input_source_rate\":"); jsonNumber (f, c.inputSourceRate);
        std::fprintf (f, ",\"device_rate\":"); jsonNumber (f, c.deviceRate);
        std::fprintf (f, ",\"channel_mapping\":\"%s\"", c.channelMapping.c_str());
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
        std::fprintf (f, ",\"baseline_prepared\":"); jsonBool (f, c.baselinePrepared);

        std::fprintf (f, ",\"progression\":{");
        std::fprintf (f, "\"audio_owner_measured\":"); jsonBool (f, c.audioOwnerMeasured);
        std::fprintf (f, ",\"audio_owner_delta_ok\":"); jsonBool (f, c.audioOwnerDeltaOk);
        std::fprintf (f, ",\"audio_owner_delta_mismatches\":"); jsonU64 (f, c.audioOwnerDeltaMismatches);
        std::fprintf (f, ",\"audio_owner_backward\":"); jsonU64 (f, c.audioOwnerBackward);
        std::fprintf (f, ",\"audio_owner_start\":"); jsonU64 (f, c.audioOwnerStart);
        std::fprintf (f, ",\"audio_owner_end\":"); jsonU64 (f, c.audioOwnerEnd);
        std::fprintf (f, ",\"reported_monotonic\":"); jsonBool (f, c.reportedMonotonic);
        std::fprintf (f, ",\"reported_future\":"); jsonU64 (f, c.reportedFuture);
        std::fprintf (f, ",\"coalesced_reads\":"); jsonU64 (f, c.coalescedReads);
        std::fprintf (f, ",\"skipped_publications\":"); jsonU64 (f, c.skippedPublications);
        std::fprintf (f, ",\"reported_cursor_start\":"); jsonU64 (f, c.reportedCursorStart);
        std::fprintf (f, ",\"reported_cursor_end\":"); jsonU64 (f, c.reportedCursorEnd);
        std::fprintf (f, ",\"receipt_reads\":"); jsonU64 (f, c.receiptReads);
        std::fprintf (f, ",\"receipt_measured_reads\":"); jsonU64 (f, c.receiptMeasuredReads);
        std::fprintf (f, ",\"new_receipts\":"); jsonU64 (f, c.newReceipts);
        std::fprintf (f, ",\"repeated_receipt_reads\":"); jsonU64 (f, c.repeatedReceiptReads);
        std::fprintf (f, ",\"receipt_any_measured\":"); jsonBool (f, c.receiptAnyMeasured);
        std::fprintf (f, ",\"receipt_order_violation\":"); jsonBool (f, c.receiptOrderViolation);
        std::fprintf (f, ",\"receipt_availability_lag_last\":");
        jsonMaybeU64 (f, c.receiptAnyMeasured, c.receiptAvailLagLast);
        std::fprintf (f, ",\"receipt_availability_lag_max\":");
        jsonMaybeU64 (f, c.receiptAnyMeasured, c.receiptAvailLagMax);
        std::fprintf (f, ",\"event_delay_last\":");
        jsonMaybeU64 (f, c.receiptAnyMeasured, c.eventDelayLast);
        std::fprintf (f, ",\"event_delay_max\":");
        jsonMaybeU64 (f, c.receiptAnyMeasured, c.eventDelayMax);
        std::fprintf (f, ",\"worker_cursor_lag_measured\":"); jsonBool (f, c.workerCursorLagMeasured);
        std::fprintf (f, ",\"worker_cursor_lag_last\":");
        jsonMaybeU64 (f, c.workerCursorLagMeasured, c.workerCursorLagLast);
        std::fprintf (f, ",\"worker_cursor_lag_max\":");
        jsonMaybeU64 (f, c.workerCursorLagMeasured, c.workerCursorLagMax);
        std::fprintf (f, ",\"prepared_seen\":"); jsonBool (f, c.preparedSeen);
        std::fprintf (f, ",\"requested_running_seen\":"); jsonBool (f, c.requestedRunningSeen);
        std::fprintf (f, ",\"join_pending_seen\":"); jsonBool (f, c.joinPendingSeen);
        std::fprintf (f, ",\"drums_playing_seen\":"); jsonBool (f, c.drumsPlayingSeen);
        std::fprintf (f, ",\"generation_changes\":"); jsonU64 (f, c.generationChanges);
        std::fprintf (f, ",\"candidate_bpm_nonzero\":"); jsonU64 (f, c.candidateBpmNonzero);
        std::fprintf (f, ",\"analysis_drops\":"); jsonU64 (f, c.analysisDrops);
        std::fprintf (f, ",\"observation_drops\":"); jsonU64 (f, c.observationDrops);
        std::fprintf (f, ",\"user_command_drops\":"); jsonU64 (f, c.userCommandDrops);
        std::fprintf (f, ",\"drum_command_drops\":"); jsonU64 (f, c.drumCommandDrops);
        std::fprintf (f, ",\"discontinuities\":"); jsonU64 (f, c.discontinuities);
        std::fprintf (f, "}");

        std::fprintf (f, ",\"commands\":[");
        for (std::size_t k = 0; k < c.commandLog.size(); ++k)
            std::fprintf (f, "%s\"%s\"", k ? "," : "", c.commandLog[k].c_str());
        std::fprintf (f, "]}");
        std::fprintf (f, "%s\n", i + 1 < results.size() ? "," : "");
    }
    std::fprintf (f, "  ],\n");

    std::fprintf (f, "  \"scenarios\": [");
    for (std::size_t i = 0; i < scenarios.size(); ++i)
    {
        const auto& r = scenarios[i];
        std::fprintf (f, "%s{", i ? "," : "");
        std::fprintf (f, "\"id\":\"%s\",\"ran\":", r.id.c_str()); jsonBool (f, r.ran);
        std::fprintf (f, ",\"injected\":"); jsonBool (f, r.injected);
        std::fprintf (f, ",\"backend_kind\":\"%s\"", r.backendKind.c_str());
        std::fprintf (f, ",\"backend_first\":\"%s\"", r.backendFirst.c_str());
        std::fprintf (f, ",\"backend_last\":\"%s\"", r.backendLast.c_str());
        std::fprintf (f, ",\"backend_changed\":"); jsonBool (f, r.backendChanged);
        std::fprintf (f, ",\"input_signal_kind\":\"clean\"");
        std::fprintf (f, ",\"input_source_rate\":0");
        std::fprintf (f, ",\"device_rate\":48000");
        std::fprintf (f, ",\"channel_mapping\":\"mono-replicated\"");
        std::fprintf (f, ",\"unmeasured_reason\":");
        if (r.ran) std::fprintf (f, "null");
        else std::fprintf (f, "\"%s\"", r.unmeasuredReason.c_str());
        std::fprintf (f, ",\"unmeasured_reason_code\":");
        if (r.ran) std::fprintf (f, "null");
        else std::fprintf (f, "\"%s\"", r.unmeasuredReasonCode.c_str());
        std::fprintf (f, ",\"callbacks\":"); jsonU64 (f, r.callbacks);
        std::fprintf (f, ",\"steps_fired\":"); jsonU64 (f, r.stepsFired);
        std::fprintf (f, ",\"output_rms\":"); jsonNumber (f, r.outputRms);
        std::fprintf (f, ",\"output_nonzero_blocks\":"); jsonU64 (f, r.outputNonzeroBlocks);
        std::fprintf (f, ",\"start_accepted\":"); jsonBool (f, r.startAccepted);
        std::fprintf (f, ",\"join_observed\":"); jsonBool (f, r.joinObserved);
        std::fprintf (f, ",\"first_join_observed\":"); jsonBool (f, r.firstJoinObserved);
        std::fprintf (f, ",\"second_join_observed\":"); jsonBool (f, r.secondJoinObserved);
        std::fprintf (f, ",\"engine_playing_observed\":"); jsonBool (f, r.enginePlayingObserved);
        std::fprintf (f, ",\"configured_acquisition_seconds\":"); jsonNumber (f, r.configuredAcquisitionSeconds);
        std::fprintf (f, ",\"first_join_budget_seconds\":"); jsonNumber (f, r.firstJoinBudgetSeconds);
        std::fprintf (f, ",\"stop_now_accepted\":"); jsonBool (f, r.stopNowAccepted);
        std::fprintf (f, ",\"resync_effect_observed\":"); jsonBool (f, r.resyncEffectObserved);
        std::fprintf (f, ",\"resync_phase_before\":"); std::fprintf (f, "%d", r.resyncPhaseBefore);
        std::fprintf (f, ",\"resync_phase_after\":"); std::fprintf (f, "%d", r.resyncPhaseAfter);
        std::fprintf (f, ",\"resync_step_sample\":"); jsonU64 (f, r.resyncStepSample);
        std::fprintf (f, ",\"resync_submit_cursor\":"); jsonU64 (f, r.resyncSubmitCursor);
        std::fprintf (f, ",\"resync_observed_end\":"); jsonU64 (f, r.resyncObservedEnd);
        std::fprintf (f, ",\"resync_owner_command_delta\":"); jsonU64 (f, r.resyncOwnerCommandDelta);
        std::fprintf (f, ",\"drum_only_window\":{");
        std::fprintf (f, "\"input_kind\":\"%s\"", r.drumOnlyInputKind.c_str());
        std::fprintf (f, ",\"warmup_seconds\":"); jsonNumber (f, r.drumOnlyWarmupSeconds);
        std::fprintf (f, ",\"measured_seconds\":"); jsonNumber (f, r.drumOnlyMeasuredSeconds);
        std::fprintf (f, ",\"sample_count\":"); jsonU64 (f, r.drumOnlySampleCount);
        std::fprintf (f, ",\"sample_rate\":"); jsonNumber (f, r.drumOnlySampleRate);
        std::fprintf (f, ",\"rms\":"); jsonNumber (f, r.drumOnlyRms);
        std::fprintf (f, ",\"peak\":"); jsonNumber (f, r.drumOnlyPeak);
        std::fprintf (f, ",\"nonzero_blocks\":"); jsonU64 (f, r.drumOnlyNonzeroBlocks);
        std::fprintf (f, ",\"steps_delta\":"); jsonU64 (f, r.drumOnlyStepsDelta);
        std::fprintf (f, ",\"engine_playing\":"); jsonBool (f, r.drumOnlyEnginePlaying);
        std::fprintf (f, ",\"sampler_loaded\":"); jsonBool (f, r.drumOnlySamplerLoaded);
        std::fprintf (f, ",\"use_vst\":"); jsonBool (f, r.drumOnlyUseVst);
        std::fprintf (f, ",\"zero_input_declared\":"); jsonBool (f, r.drumOnlyZeroInput);
        std::fprintf (f, ",\"allocator_coverage\":\"%s\"", r.drumOnlyAllocatorCoverage.c_str());
        std::fprintf (f, "}");
        std::fprintf (f, ",\"session_generation_changed\":"); jsonBool (f, r.sessionGenerationChanged);
        std::fprintf (f, ",\"released_confirmed\":"); jsonBool (f, r.releasedConfirmed);
        std::fprintf (f, ",\"paced\":"); jsonBool (f, r.paced);
        std::fprintf (f, ",\"wall_seconds\":"); jsonNumber (f, r.wallSeconds);
        std::fprintf (f, ",\"blocks_to_join\":"); jsonU64 (f, r.blocksToJoin);
        std::fprintf (f, ",\"stop_at_next_bar_deferred\":"); jsonBool (f, r.stopAtNextBarDeferred);
        std::fprintf (f, ",\"blocks_to_stop_at_next_bar\":"); jsonU64 (f, r.blocksToStopAtNextBar);
        std::fprintf (f, ",\"stop_now_stopped\":"); jsonBool (f, r.stopNowStopped);
        std::fprintf (f, ",\"blocks_to_stop_now\":"); jsonU64 (f, r.blocksToStopNow);
        std::fprintf (f, ",\"resync_accepted\":"); jsonBool (f, r.resyncAccepted);
        std::fprintf (f, ",\"generation_before_reprepare\":"); jsonU64 (f, r.generationBeforeReprepare);
        std::fprintf (f, ",\"generation_after_reprepare\":"); jsonU64 (f, r.generationAfterReprepare);
        std::fprintf (f, ",\"generation_changed_on_reprepare\":"); jsonBool (f, r.generationChangedOnReprepare);
        std::fprintf (f, ",\"shutdown_released\":"); jsonBool (f, r.shutdownReleased);
        std::fprintf (f, ",\"audio_owner_delta_ok\":"); jsonBool (f, r.audioOwnerDeltaOk);
        std::fprintf (f, ",\"audio_owner_delta_mismatches\":"); jsonU64 (f, r.audioOwnerDeltaMismatches);
        std::fprintf (f, ",\"audio_owner_observed_s\":"); jsonNumber (f, r.audioOwnerObservedS);
        std::fprintf (f, ",\"callback_alloc_cxx\":"); jsonU64 (f, r.callbackAllocCxx);
        std::fprintf (f, ",\"callback_alloc_c\":"); jsonU64 (f, r.callbackAllocC);
        std::fprintf (f, ",\"callback_free\":"); jsonU64 (f, r.callbackFree);
        std::fprintf (f, ",\"callback_locks\":"); jsonU64 (f, r.callbackLocks);
        std::fprintf (f, "}");
    }
    std::fprintf (f, "],\n");

    std::fprintf (f, "  \"counts\": {");
    std::fprintf (f, "\"cells\":%zu", results.size());
    std::fprintf (f, ",\"measured\":%d", anyMeasured);
    std::fprintf (f, ",\"enabled_rejected\":%d", enabledRejected);
    std::fprintf (f, ",\"findings\":%zu", findings.size());
    std::fprintf (f, "}\n");
    std::fprintf (f, "}\n");

    const bool written = std::ferror (f) == 0;
    const bool closed = std::fclose (f) == 0;
    if (! written || ! closed)
    {
        std::fprintf (stderr, "error: failed writing %s\n", jsonPath.c_str());
        return 5;
    }

    std::fprintf (log, "\ncells.json: %s (scope=%s cells=%zu measured=%d findings=%zu)\n",
                  jsonPath.c_str(), o.scope.c_str(), results.size(), anyMeasured, findings.size());

    if (! backendUsable && ! o.allowUnavailable)
    {
        std::fprintf (log, "backend not experimentalBTrack (%s): awaiting-backend (exit 3)\n", backendKind.c_str());
        return 3;
    }
    return 0;
}
