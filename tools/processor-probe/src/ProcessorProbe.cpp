// RT-002 bounded processor runtime probe.
//
// Executes the real, pinned GuitarCompanionAudioProcessor::processBlock from
// the prebuilt shared-code archive (no mock, no stub, no editor) while a local
// instrumentation library counts C++/C allocations, frees and pthread
// mutex/cond operations. Also drives the processor's own scene-ready flag
// through its own juce::Timer on the genuine JUCE message loop, with the
// editor never created.
//
// Scope and honest limits are documented in docs/research/PROCESSOR-RUNTIME-PROBE.md.
#include "PluginProcessor.h"
#include "RtProbeInstrumentation.h"

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
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

const char* const kInputGainId = "inputGain";
const char* const kAmpOnId     = "ampOn";

constexpr int kMinWarm = 1;
constexpr int kMaxWarm = 100000;

// Scene timings. The no-device safety net is sceneFadeOutSec + 250 ms = 262 ms.
constexpr int kSceneAudioWindowMs      = 100;   // audio-driven restore window
constexpr int kSceneNoAudioShortMs     = 150;   // < fallback: must NOT restore
constexpr int kSceneNoAudioLongMs      = 400;   // > fallback: fallback restores
constexpr int kSceneFallbackMs         = 262;
constexpr double kSceneAudioDispatchBoundMs = 200.0;

double msBetween (TimePoint a, TimePoint b)
{
    return std::chrono::duration<double, std::milli> (b - a).count();
}

bool nearValue (float a, float b)
{
    return std::fabs (a - b) < 1.0e-4f;
}

// Deterministic, finite, non-zero input. The cursor is reset per combo from a
// seed that does NOT depend on the drum mode, so a stopped case and the matching
// playing case receive the identical input sequence and can be compared.
void fillSignal (juce::AudioBuffer<float>& buf, int n, std::uint32_t& cursor)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* p = buf.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            cursor = cursor * 1664525u + 1013904223u;
            const float u = (float) ((cursor >> 8) & 0xFFFFu) / 65535.0f;
            p[i] = (0.01f + (u * 0.40f - 0.20f)) * (ch == 0 ? 1.0f : 0.7f);
        }
    }
}

void configureDrums (GuitarCompanionProcessor& proc, bool playing)
{
    auto& e = proc.drumEngine;

    for (int b = 0; b < drum::maxBars; ++b)
        e.clearBar (b);
    for (int v = 0; v < drum::numVoices; ++v)
    {
        e.uiVoiceFlash[v].store (0.0f);
        e.uiVoicePeak[v].store (0.0f);
    }
    e.uiMixPeak.store (0.0f);

    if (playing)
    {
        juce::uint8 p[drum::numVoices][drum::maxStepsPerBar] = {};
        for (int s = 0; s < 16; s += 4)
        {
            p[drum::kick][s]      = 2;   // accent
            p[drum::snare][s + 2] = 1;   // hit
        }
        for (int s = 0; s < 16; s += 2)
            p[drum::hat][s] = (s % 4 == 0) ? 1 : 3;   // hit / ghost

        for (int b = 0; b < drum::barsPerSection; ++b)
            e.setBarPattern (p, b);
        e.numSections.store (1);
        e.bpm.store (120.0f);
    }
    e.playing.store (playing);
}

void dumpAllocRecords (std::FILE* log, const char* label)
{
    const std::uint64_t n = rtprobe::gAllocRecordCount.load (std::memory_order_relaxed);
    const std::uint64_t cap = n < rtprobe::kMaxAllocRecords ? n : rtprobe::kMaxAllocRecords;
    const std::uint64_t printMax = 16;
    for (std::uint64_t i = 0; i < cap && i < printMax; ++i)
    {
        const auto& r = rtprobe::gAllocRecords[i];
        std::fprintf (log, "      %s #%llu %s bytes=%zu ptr=%p caller_vaddr=%p\n",
                      label, (unsigned long long) i, rtprobe::kindName (r.kind),
                      r.bytes, r.ptr, r.caller);
    }
    if (cap > printMax)
        std::fprintf (log, "      %s: ... %llu more records suppressed\n",
                      label, (unsigned long long) (cap - printMax));
    if (n > cap)
        std::fprintf (log, "      %s: +%llu records beyond fixed capacity (overflow)\n",
                      label, (unsigned long long) (n - cap));
}

void dumpLockRecords (std::FILE* log, const char* label)
{
    const std::uint64_t n = rtprobe::gLockRecordCount.load (std::memory_order_relaxed);
    const std::uint64_t cap = n < rtprobe::kMaxLockRecords ? n : rtprobe::kMaxLockRecords;
    for (std::uint64_t i = 0; i < cap; ++i)
    {
        const auto& r = rtprobe::gLockRecords[i];
        std::fprintf (log, "      %s #%llu %s mutex=%p waited=%llu ns caller=%p\n",
                      label, (unsigned long long) i,
                      r.tryOnly ? "trylock" : "lock",
                      r.mutex, (unsigned long long) r.waitedNs, r.caller);
    }
}

std::uint64_t cxxAlloc (const rtprobe::Snapshot& s) noexcept
{
    return s.allocCalls[(std::size_t) rtprobe::Kind::cxxNew]
         + s.allocCalls[(std::size_t) rtprobe::Kind::cxxNewArray]
         + s.allocCalls[(std::size_t) rtprobe::Kind::cxxNewNothrow]
         + s.allocCalls[(std::size_t) rtprobe::Kind::cxxNewAligned];
}
std::uint64_t cAlloc (const rtprobe::Snapshot& s) noexcept
{
    return s.allocCalls[(std::size_t) rtprobe::Kind::cMalloc]
         + s.allocCalls[(std::size_t) rtprobe::Kind::cCalloc]
         + s.allocCalls[(std::size_t) rtprobe::Kind::cRealloc];
}

struct ComboRow
{
    std::string       tag;
    double            rate = 0.0;
    int               block = 0;
    bool              drums = false;
    int               warmBlocks = 0;
    rtprobe::Snapshot cold {};
    rtprobe::Snapshot warm {};
    double            processMs = 0.0;   // armed processBlock time only
    double            totalMs = 0.0;     // whole warm loop incl. unarmed fill/metrics
    double            durationS = 0.0;   // warmBlocks * block / rate
    double            outRms = 0.0;      // mean output RMS over warm blocks
    int               drumActiveBlocks = 0;
    int               voiceEvents = 0;
};

void runCombo (GuitarCompanionProcessor& proc, double rate, int block, bool drums,
               const char* tag, int warmBlocks, std::vector<ComboRow>& rows,
               std::FILE* log)
{
    proc.prepareToPlay (rate, block);
    configureDrums (proc, drums);

    // All input/midi storage is allocated OUTSIDE the measured regions.
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);

    const std::uint32_t seed = (std::uint32_t) (unsigned) rate
                             ^ ((std::uint32_t) block * 2654435761u);
    std::uint32_t cursor = seed;

    ComboRow row;
    row.tag = tag;
    row.rate = rate;
    row.block = block;
    row.drums = drums;
    row.warmBlocks = warmBlocks;

    // Cold: the first processBlock after prepareToPlay, counted explicitly.
    fillSignal (buf, block, cursor);
    rtprobe::resetAll();
    const auto c0 = rtprobe::snapshot();
    {
        const auto t0 = Clock::now();
        rtprobe::arm();
        proc.processBlock (buf, midi);
        rtprobe::disarm();
        const auto t1 = Clock::now();
        row.processMs = msBetween (t0, t1);   // cold time only, extended below
    }
    const auto c1 = rtprobe::snapshot();
    row.cold = rtprobe::delta (c0, c1);
    if (rtprobe::allocCallTotal (row.cold) != 0 || rtprobe::freeCallTotal (row.cold) != 0)
        dumpAllocRecords (log, "cold");
    if (row.cold.lockCalls != 0 || row.cold.trylockCalls != 0 || row.cold.condWaitCalls != 0)
        dumpLockRecords (log, "cold");
    proc.drumEngine.uiMixPeak.exchange (0.0f);
    for (int v = 0; v < drum::numVoices; ++v)
        proc.drumEngine.uiVoiceFlash[v].exchange (0.0f);

    // Warm: every callback gets a freshly filled known input OUTSIDE the armed
    // region; only processBlock is armed. Counters accumulate across the run.
    rtprobe::resetAll();
    const auto w0 = rtprobe::snapshot();
    double processMs = 0.0;
    double outRmsSum = 0.0;
    int drumBlocks = 0;
    int voiceEvents = 0;
    const auto tWarm0 = Clock::now();
    for (int i = 0; i < warmBlocks; ++i)
    {
        fillSignal (buf, block, cursor);      // unarmed
        const auto t0 = Clock::now();
        rtprobe::arm();
        proc.processBlock (buf, midi);
        rtprobe::disarm();
        const auto t1 = Clock::now();
        processMs += msBetween (t0, t1);      // armed processBlock wall time

        outRmsSum += buf.getRMSLevel (0, 0, block);
        if (proc.drumEngine.uiMixPeak.exchange (0.0f) > 1.0e-6f)
            ++drumBlocks;
        for (int v = 0; v < drum::numVoices; ++v)
            if (proc.drumEngine.uiVoiceFlash[v].exchange (0.0f) > 0.0f)
                ++voiceEvents;
    }
    const auto tWarm1 = Clock::now();
    const auto w1 = rtprobe::snapshot();
    row.warm = rtprobe::delta (w0, w1);
    row.processMs += processMs;               // cold + warm armed time
    row.totalMs = msBetween (tWarm0, tWarm1);
    row.durationS = (double) warmBlocks * (double) block / rate;
    row.outRms = warmBlocks > 0 ? outRmsSum / (double) warmBlocks : 0.0;
    row.drumActiveBlocks = drumBlocks;
    row.voiceEvents = voiceEvents;

    if (rtprobe::allocCallTotal (row.warm) != 0 || rtprobe::freeCallTotal (row.warm) != 0)
        dumpAllocRecords (log, "warm");
    if (row.warm.lockCalls != 0 || row.warm.trylockCalls != 0 || row.warm.condWaitCalls != 0)
        dumpLockRecords (log, "warm");

    rows.push_back (row);

    std::fprintf (log,
                  "  [%-9s] rate=%7.0f block=%4d drums=%-8s "
                  "cold(alloc=%llu free=%llu lock=%llu) "
                  "warm(alloc=%llu free=%llu lock=%llu trylock=%llu cond=%llu) "
                  "%d blocks %.3f s  pb-wall %.3f ms (%.1f us/blk, instrumented)  "
                  "drums[active=%d voices=%d] outRms=%.5f\n",
                  tag, rate, block, drums ? "playing" : "stopped",
                  (unsigned long long) rtprobe::allocCallTotal (row.cold),
                  (unsigned long long) rtprobe::freeCallTotal (row.cold),
                  (unsigned long long) row.cold.lockCalls,
                  (unsigned long long) rtprobe::allocCallTotal (row.warm),
                  (unsigned long long) rtprobe::freeCallTotal (row.warm),
                  (unsigned long long) row.warm.lockCalls,
                  (unsigned long long) row.warm.trylockCalls,
                  (unsigned long long) row.warm.condWaitCalls,
                  warmBlocks, row.durationS, row.processMs,
                  row.processMs * 1000.0 / (double) (warmBlocks + 1 > 0 ? warmBlocks + 1 : 1),
                  row.drumActiveBlocks, row.voiceEvents, row.outRms);

    if (row.warm.noopFrees != 0)
    {
        std::fprintf (log, "      no-op frees=%llu sites(offset):",
                      (unsigned long long) row.warm.noopFrees);
        const std::uint64_t n = rtprobe::gNoopFreeCallerCount.load (std::memory_order_relaxed);
        const std::uint64_t cap = n < rtprobe::kMaxNoopFreeSites ? n
                                                                 : rtprobe::kMaxNoopFreeSites;
        std::uint64_t shown = 0;
        for (std::uint64_t i = 0; i < cap && shown < 6; ++i)
        {
            const auto c = rtprobe::gNoopFreeCallers[i].load (std::memory_order_relaxed);
            bool dup = false;
            for (std::uint64_t j = 0; j < i; ++j)
                if (rtprobe::gNoopFreeCallers[j].load (std::memory_order_relaxed) == c)
                { dup = true; break; }
            if (! dup)
            {
                std::fprintf (log, " %p", c);
                ++shown;
            }
        }
        std::fprintf (log, " (free(NULL) no-ops, not heap frees)\n");
    }
}

//==============================================================================
// Scene: flag -> processor-owned Timer via the genuine JUCE message loop
//==============================================================================

constexpr int kPumpMaxTicksBase = 100000;

struct PumpState
{
    TimePoint deadline;
    int maxTicks = kPumpMaxTicksBase;
    std::atomic<int> ticks { 0 };
};

void repostStop (std::shared_ptr<PumpState> st);

struct SceneResult
{
    bool   savedScene = false;
    bool   changed = false;
    bool   notAppliedBefore = false;
    bool   restored = false;
    bool   editorNull = false;
    bool   dispatchWithinBound = false;
    int    blocksDriven = 0;
    int    windowMs = 0;
    std::uint64_t signalAlloc = 0;
    std::uint64_t signalFree = 0;
    std::uint64_t signalLock = 0;
    std::uint64_t signalTrylock = 0;
    std::uint64_t signalCond = 0;
    std::uint64_t signalUnlock = 0;
    double dispatchMs = 0.0;
    double restoreElapsedMs = -1.0;
    double restoreFromLoopMs = -1.0;
    float  savedRaw = 0.0f;
    float  changedRaw = 0.0f;
    float  beforeDispatchRaw = 0.0f;
    float  afterDispatchRaw = 0.0f;
    int    stopSentBefore = -1;
    int    stopSentAfter = -1;
};

struct ObserverState
{
    std::shared_ptr<std::atomic<bool>> alive;
    TimePoint arm {};
    TimePoint loopStart {};
    float target = 0.0f;
    std::atomic<float>* param = nullptr;
    std::atomic<double> restoreElapsedMs { -1.0 };
    std::atomic<double> restoreFromLoopMs { -1.0 };
    std::atomic<int> ticks { 0 };
};

void repostObserve (std::shared_ptr<ObserverState> os);

void repostStop (std::shared_ptr<PumpState> st)
{
    juce::MessageManager::callAsync ([st]
    {
        const int t = st->ticks.fetch_add (1, std::memory_order_relaxed) + 1;
        if (t >= st->maxTicks || Clock::now() >= st->deadline)
            juce::MessageManager::getInstance()->stopDispatchLoop();
        else
            repostStop (st);
    });
}

void repostObserve (std::shared_ptr<ObserverState> os)
{
    juce::MessageManager::callAsync ([os]
    {
        if (! os->alive->load (std::memory_order_relaxed))
            return;
        os->ticks.fetch_add (1, std::memory_order_relaxed);

        const float v = os->param->load();
        if (nearValue (v, os->target)
            && os->restoreElapsedMs.load (std::memory_order_relaxed) < 0.0)
        {
            const auto now = Clock::now();
            os->restoreElapsedMs.store (msBetween (os->arm, now),
                                        std::memory_order_relaxed);
            os->restoreFromLoopMs.store (msBetween (os->loopStart, now),
                                         std::memory_order_relaxed);
            return;   // observed; stop scanning (the stopper keeps the loop alive)
        }
        repostObserve (os);
    });
}

SceneResult runSceneCase (GuitarCompanionProcessor& proc, bool driveAudio,
                          int windowMs, std::FILE* log)
{
    SceneResult r;
    r.windowMs = windowMs;
    r.editorNull = (proc.getActiveEditor() == nullptr);

    proc.prepareToPlay (48000.0, 128);
    proc.scenesOn.store (false);

    auto* gainParam = proc.apvts.getParameter (kInputGainId);
    if (gainParam == nullptr)
    {
        std::fprintf (log, "  scene: inputGain parameter not found\n");
        return r;
    }

    const float savedNorm = gainParam->getValue();
    proc.saveSceneForSection (0);
    r.savedScene = proc.hasScene (0);
    if (! r.savedScene)
    {
        std::fprintf (log, "  scene: section 0 could not be saved\n");
        return r;
    }

    r.savedRaw = proc.apvts.getRawParameterValue (kInputGainId)->load();
    const float newNorm = (savedNorm < 0.5f) ? 0.85f : 0.15f;
    gainParam->setValueNotifyingHost (newNorm);
    r.changedRaw = proc.apvts.getRawParameterValue (kInputGainId)->load();
    r.changed = ! nearValue (r.savedRaw, r.changedRaw);

    proc.applySceneForSection (0);
    const auto armTime = Clock::now();

    juce::AudioBuffer<float> buf (2, 128);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    std::uint32_t cursor = 12345u;

    if (driveAudio)
    {
        const int blocks = (int) std::ceil (48000.0 * 0.012 / 128.0) + 6;
        r.blocksDriven = blocks;
        rtprobe::resetAll();
        const auto s0 = rtprobe::snapshot();
        for (int i = 0; i < blocks; ++i)
        {
            fillSignal (buf, 128, cursor);   // unarmed
            rtprobe::arm();
            proc.processBlock (buf, midi);
            rtprobe::disarm();
        }
        const auto s1 = rtprobe::snapshot();
        const auto d = rtprobe::delta (s0, s1);
        r.signalAlloc   = rtprobe::allocCallTotal (d);
        r.signalFree    = rtprobe::freeCallTotal (d);
        r.signalLock    = d.lockCalls;
        r.signalTrylock = d.trylockCalls;
        r.signalCond    = d.condWaitCalls;
        r.signalUnlock  = d.unlockCalls;
    }

    r.beforeDispatchRaw = proc.apvts.getRawParameterValue (kInputGainId)->load();
    r.notAppliedBefore = nearValue (r.beforeDispatchRaw, r.changedRaw)
                         && ! nearValue (r.changedRaw, r.savedRaw);

    const auto loopStart = Clock::now();
    r.stopSentBefore = (int) juce::MessageManager::getInstance()->hasStopMessageBeenSent();

    auto os = std::make_shared<ObserverState>();
    os->alive = std::make_shared<std::atomic<bool>> (true);
    os->arm = armTime;
    os->loopStart = loopStart;
    os->target = r.savedRaw;
    os->param = proc.apvts.getRawParameterValue (kInputGainId);
    repostObserve (os);

    auto st = std::make_shared<PumpState>();
    st->deadline = loopStart + std::chrono::milliseconds (windowMs);
    st->maxTicks = windowMs * 2000 + kPumpMaxTicksBase;
    repostStop (st);

    const auto t0 = Clock::now();
    juce::MessageManager::getInstance()->runDispatchLoop();
    const auto t1 = Clock::now();
    r.dispatchMs = msBetween (t0, t1);

    os->alive->store (false);
    st->deadline = Clock::now() - std::chrono::seconds (1);   // further reposts stop

    r.afterDispatchRaw = proc.apvts.getRawParameterValue (kInputGainId)->load();
    r.restored = nearValue (r.afterDispatchRaw, r.savedRaw)
                 && ! nearValue (r.savedRaw, r.changedRaw);
    r.restoreElapsedMs = os->restoreElapsedMs.load();
    r.restoreFromLoopMs = os->restoreFromLoopMs.load();
    r.stopSentAfter = (int) juce::MessageManager::getInstance()->hasStopMessageBeenSent();
    r.editorNull = r.editorNull && (proc.getActiveEditor() == nullptr);
    const double dispatchBoundMs = driveAudio ? kSceneAudioDispatchBoundMs
                                             : windowMs + 25.0;
    r.dispatchWithinBound = r.dispatchMs <= dispatchBoundMs;

    std::fprintf (log,
                  "  scene[%s]: saved=%.6f changed=%.6f before=%.6f after=%.6f "
                  "restored=%d restore@%.1f ms (loop@%.1f ms) dispatch=%.1f ms\n",
                  driveAudio ? "audio" : "noaudio", r.savedRaw, r.changedRaw,
                  r.beforeDispatchRaw, r.afterDispatchRaw, (int) r.restored,
                  r.restoreElapsedMs, r.restoreFromLoopMs, r.dispatchMs);
    std::fprintf (log,
                  "  scene[%s]: blocks=%d signal(alloc=%llu free=%llu lock=%llu "
                  "trylock=%llu cond=%llu unlock=%llu) stopBefore=%d stopAfter=%d "
                  "dispatchInBound=%d editorNull=%d\n",
                  driveAudio ? "audio" : "noaudio", r.blocksDriven,
                  (unsigned long long) r.signalAlloc,
                  (unsigned long long) r.signalFree,
                  (unsigned long long) r.signalLock,
                  (unsigned long long) r.signalTrylock,
                  (unsigned long long) r.signalCond,
                  (unsigned long long) r.signalUnlock,
                  r.stopSentBefore, r.stopSentAfter,
                  (int) r.dispatchWithinBound, (int) r.editorNull);
    return r;
}

//==============================================================================
// Reporting
//==============================================================================

bool writeFindings (const char* path, const char* mode, bool selfCheckOk,
                    bool argsOk, const std::vector<ComboRow>& rows,
                    const SceneResult& scene, bool sceneExpectationOk)
{
    if (path == nullptr)
        return true;
    std::FILE* f = std::fopen (path, "w");
    if (f == nullptr)
    {
        std::fprintf (stderr, "error: cannot write findings JSON: %s\n", path);
        return false;
    }

    int dryCases = 0, dryNonZero = 0, namCases = 0, namAllocCases = 0;
    bool dryAllZero = true;
    double namPerHostMin = -1.0, namPerHostMax = 0.0, namPerHost48000 = -1.0;
    for (const auto& row : rows)
    {
        const bool dry = (row.tag == "dry" || row.tag == "dry+drums");
        const bool nam = (row.tag == "nam" || row.tag == "nam+drums");
        const bool zero = rtprobe::allocCallTotal (row.cold) == 0
                       && rtprobe::freeCallTotal (row.cold) == 0
                       && row.cold.lockCalls == 0 && row.cold.trylockCalls == 0
                       && row.cold.condWaitCalls == 0 && row.cold.unlockCalls == 0
                       && rtprobe::allocCallTotal (row.warm) == 0
                       && rtprobe::freeCallTotal (row.warm) == 0
                       && row.warm.lockCalls == 0 && row.warm.trylockCalls == 0
                       && row.warm.condWaitCalls == 0 && row.warm.unlockCalls == 0;
        if (dry)
        {
            ++dryCases;
            if (! zero) { ++dryNonZero; dryAllZero = false; }
        }
        if (nam)
        {
            ++namCases;
            const auto allocs = rtprobe::allocCallTotal (row.warm);
            if (allocs > 0) ++namAllocCases;
            const std::uint64_t samples = (std::uint64_t) row.warmBlocks * (std::uint64_t) row.block;
            if (samples > 0)
            {
                const double per = (double) allocs / (double) samples;
                if (namPerHostMin < 0.0 || per < namPerHostMin) namPerHostMin = per;
                if (per > namPerHostMax) namPerHostMax = per;
                if (std::fabs (row.rate - 48000.0) < 1.0)
                    namPerHost48000 = per;
            }
        }
    }

    const bool sceneSignalZero = scene.signalAlloc == 0 && scene.signalFree == 0
                              && scene.signalLock == 0 && scene.signalTrylock == 0
                              && scene.signalCond == 0;

    std::fprintf (f, "{\n");
    std::fprintf (f, "  \"mode\": \"%s\",\n", mode);
    std::fprintf (f, "  \"selfcheck_pass\": %s,\n", selfCheckOk ? "true" : "false");
    std::fprintf (f, "  \"args_ok\": %s,\n", argsOk ? "true" : "false");
    std::fprintf (f, "  \"dry_cases\": %d,\n", dryCases);
    std::fprintf (f, "  \"dry_cases_nonzero\": %d,\n", dryNonZero);
    std::fprintf (f, "  \"dry_all_zero\": %s,\n", dryAllZero ? "true" : "false");
    std::fprintf (f, "  \"nam_cases\": %d,\n", namCases);
    std::fprintf (f, "  \"nam_cases_with_alloc\": %d,\n", namAllocCases);
    std::fprintf (f, "  \"nam_alloc_per_host_sample_min\": %.4f,\n", namPerHostMin);
    std::fprintf (f, "  \"nam_alloc_per_host_sample_max\": %.4f,\n", namPerHostMax);
    std::fprintf (f, "  \"nam_alloc_per_host_sample_48000\": %.4f,\n", namPerHost48000);
    std::fprintf (f, "  \"scene_saved\": %s,\n", scene.savedScene ? "true" : "false");
    std::fprintf (f, "  \"scene_changed\": %s,\n", scene.changed ? "true" : "false");
    std::fprintf (f, "  \"scene_not_applied_before_dispatch\": %s,\n",
                  scene.notAppliedBefore ? "true" : "false");
    std::fprintf (f, "  \"scene_restored\": %s,\n", scene.restored ? "true" : "false");
    std::fprintf (f, "  \"scene_restore_elapsed_ms\": %.3f,\n", scene.restoreElapsedMs);
    std::fprintf (f, "  \"scene_restore_from_loop_ms\": %.3f,\n", scene.restoreFromLoopMs);
    std::fprintf (f, "  \"scene_dispatch_ms\": %.3f,\n", scene.dispatchMs);
    std::fprintf (f, "  \"scene_dispatch_within_bound\": %s,\n",
                  scene.dispatchWithinBound ? "true" : "false");
    std::fprintf (f, "  \"scene_stop_sent_before\": %d,\n", scene.stopSentBefore);
    std::fprintf (f, "  \"scene_signal_zero\": %s,\n", sceneSignalZero ? "true" : "false");
    std::fprintf (f, "  \"scene_editor_null\": %s,\n", scene.editorNull ? "true" : "false");
    std::fprintf (f, "  \"scene_expectation_ok\": %s\n", sceneExpectationOk ? "true" : "false");
    std::fprintf (f, "}\n");
    const bool written = std::ferror (f) == 0;
    const bool closed = std::fclose (f) == 0;
    return written && closed;
}

void writeCsv (const char* path, const std::vector<ComboRow>& rows, bool& ok)
{
    ok = false;
    if (path == nullptr)
        return;
    std::FILE* f = std::fopen (path, "w");
    if (f == nullptr)
    {
        std::fprintf (stderr, "error: cannot write CSV: %s\n", path);
        return;
    }

    std::fprintf (f, "tag,rate,block,drums,warm_blocks,duration_s,"
                     "cold_cxxnew,cold_cxxnewarr,cold_cxxdel,cold_cxxdelarr,cold_cxxdelsized,"
                     "cold_malloc,cold_calloc,cold_realloc,cold_free,cold_noopfree,"
                     "cold_lock,cold_trylock,cold_cond,cold_unlock,"
                     "cold_alloc_overflow,cold_lock_overflow,"
                     "warm_cxxnew,warm_cxxnewarr,warm_cxxdel,warm_cxxdelarr,warm_cxxdelsized,"
                     "warm_malloc,warm_calloc,warm_realloc,warm_free,warm_noopfree,"
                     "warm_lock,warm_trylock,warm_cond,warm_unlock,"
                     "warm_alloc_overflow,warm_lock_overflow,"
                     "warm_alloc_cxx_total,warm_alloc_c_total,"
                     "process_wall_ms,total_wall_ms,drum_active_blocks,voice_events,out_rms\n");

    const auto k = [] (const rtprobe::Snapshot& s, rtprobe::Kind kind)
    { return s.allocCalls[(std::size_t) kind]; };

    for (const auto& row : rows)
    {
        std::fprintf (f,
            "%s,%.0f,%d,%d,%d,%.4f,"
            "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
            "%llu,%llu,%llu,%llu,%llu,%llu,"
            "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
            "%llu,%llu,%llu,%llu,%llu,%llu,"
            "%llu,%llu,"
            "%.3f,%.3f,%d,%d,%.6f\n",
            row.tag.c_str(), row.rate, row.block, (int) row.drums, row.warmBlocks,
            row.durationS,
            (unsigned long long) k (row.cold, rtprobe::Kind::cxxNew),
            (unsigned long long) k (row.cold, rtprobe::Kind::cxxNewArray),
            (unsigned long long) k (row.cold, rtprobe::Kind::cxxDelete),
            (unsigned long long) k (row.cold, rtprobe::Kind::cxxDeleteArray),
            (unsigned long long) k (row.cold, rtprobe::Kind::cxxDeleteSized),
            (unsigned long long) k (row.cold, rtprobe::Kind::cMalloc),
            (unsigned long long) k (row.cold, rtprobe::Kind::cCalloc),
            (unsigned long long) k (row.cold, rtprobe::Kind::cRealloc),
            (unsigned long long) k (row.cold, rtprobe::Kind::cFree),
            (unsigned long long) row.cold.noopFrees,
            (unsigned long long) row.cold.lockCalls,
            (unsigned long long) row.cold.trylockCalls,
            (unsigned long long) row.cold.condWaitCalls,
            (unsigned long long) row.cold.unlockCalls,
            (unsigned long long) row.cold.allocRecordOverflow,
            (unsigned long long) row.cold.lockRecordOverflow,
            (unsigned long long) k (row.warm, rtprobe::Kind::cxxNew),
            (unsigned long long) k (row.warm, rtprobe::Kind::cxxNewArray),
            (unsigned long long) k (row.warm, rtprobe::Kind::cxxDelete),
            (unsigned long long) k (row.warm, rtprobe::Kind::cxxDeleteArray),
            (unsigned long long) k (row.warm, rtprobe::Kind::cxxDeleteSized),
            (unsigned long long) k (row.warm, rtprobe::Kind::cMalloc),
            (unsigned long long) k (row.warm, rtprobe::Kind::cCalloc),
            (unsigned long long) k (row.warm, rtprobe::Kind::cRealloc),
            (unsigned long long) k (row.warm, rtprobe::Kind::cFree),
            (unsigned long long) row.warm.noopFrees,
            (unsigned long long) row.warm.lockCalls,
            (unsigned long long) row.warm.trylockCalls,
            (unsigned long long) row.warm.condWaitCalls,
            (unsigned long long) row.warm.unlockCalls,
            (unsigned long long) row.warm.allocRecordOverflow,
            (unsigned long long) row.warm.lockRecordOverflow,
            (unsigned long long) cxxAlloc (row.warm),
            (unsigned long long) cAlloc (row.warm),
            row.processMs, row.totalMs, row.drumActiveBlocks, row.voiceEvents, row.outRms);
    }
    const bool written = std::ferror (f) == 0;
    const bool closed = std::fclose (f) == 0;
    ok = written && closed;
}

void printUsage (const char* exe)
{
    std::fprintf (stderr,
        "usage: %s [options]\n"
        "  --warm-blocks N        warm blocks per case (%d..%d, default 256)\n"
        "  --csv PATH             write per-case CSV\n"
        "  --json PATH            write machine-verified findings JSON\n"
        "  --nam-model PATH       NAM model for the NAM case (optional)\n"
        "  --scene-mode MODE      full | scene-noaudio-short | scene-noaudio-long\n",
        exe, kMinWarm, kMaxWarm);
}

} // namespace

int main (int argc, char** argv)
{
    const char* csvPath = nullptr;
    const char* jsonPath = nullptr;
    const char* namModel = nullptr;
    std::string sceneMode = "full";
    int warmBlocks = 256;

    for (int i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        const bool hasNext = (i + 1 < argc);
        if (std::strcmp (a, "--warm-blocks") == 0 && hasNext)
        {
            char* end = nullptr;
            const long v = std::strtol (argv[++i], &end, 10);
            if (end == nullptr || *end != '\0' || v < kMinWarm || v > kMaxWarm)
            {
                std::fprintf (stderr, "error: --warm-blocks must be %d..%d\n", kMinWarm, kMaxWarm);
                return 64;
            }
            warmBlocks = (int) v;
        }
        else if (std::strcmp (a, "--csv") == 0 && hasNext)        csvPath = argv[++i];
        else if (std::strcmp (a, "--json") == 0 && hasNext)       jsonPath = argv[++i];
        else if (std::strcmp (a, "--nam-model") == 0 && hasNext)  namModel = argv[++i];
        else if (std::strcmp (a, "--scene-mode") == 0 && hasNext)
        {
            sceneMode = argv[++i];
            if (sceneMode != "full" && sceneMode != "scene-noaudio-short"
                && sceneMode != "scene-noaudio-long")
            {
                std::fprintf (stderr, "error: unknown --scene-mode '%s'\n", sceneMode.c_str());
                return 64;
            }
        }
        else
        {
            std::fprintf (stderr, "error: unknown or incomplete option '%s'\n", a);
            printUsage (argv[0]);
            return 64;
        }
    }

    std::FILE* log = stdout;

    std::fprintf (log, "RT-002 processor runtime probe\n");
    std::fprintf (log, "==============================\n");
    std::fprintf (log, "mode=%s warm blocks per case=%d\n\n", sceneMode.c_str(), warmBlocks);

    std::fprintf (log, "[instrument self-check]\n");
    if (rtprobe::runSelfCheck (log) != 0)
    {
        std::fprintf (log, "SELFCHECK FAILED - refusing to report processor results\n");
        writeFindings (jsonPath, sceneMode.c_str(), false, true, {}, SceneResult {}, false);
        return 2;
    }
    std::fprintf (log, "  self-check PASS (gate + exact new/delete/malloc/lock detection)\n\n");

    // Scene-only subprocess modes: bounded differential for the fallback.
    if (sceneMode != "full")
    {
        const int windowMs = (sceneMode == "scene-noaudio-short")
                                 ? kSceneNoAudioShortMs : kSceneNoAudioLongMs;
        std::fprintf (log, "[scene differential: %s, %d ms window]\n",
                      sceneMode.c_str(), windowMs);
        juce::ScopedJuceInitialiser_GUI gui;
        GuitarCompanionProcessor proc;
        if (auto* ampOn = proc.apvts.getParameter (kAmpOnId))
            if (ampOn->getValue() > 0.0f)
                ampOn->setValueNotifyingHost (0.0f);

        const SceneResult scene = runSceneCase (proc, false, windowMs, log);

        bool expectOk = false;
        if (sceneMode == "scene-noaudio-short")
            expectOk = ! scene.restored && scene.stopSentBefore == 0
                    && scene.editorNull && scene.notAppliedBefore;
        else
            expectOk = scene.restored && scene.stopSentBefore == 0
                    && scene.editorNull && scene.notAppliedBefore;

        expectOk = expectOk && scene.savedScene && scene.changed && scene.dispatchWithinBound;
        if (sceneMode == "scene-noaudio-long")
            expectOk = expectOk && scene.restoreElapsedMs >= kSceneNoAudioShortMs;
        const bool jsonOk = writeFindings (jsonPath, sceneMode.c_str(), true, true, {}, scene, expectOk);
        std::fprintf (log, "\n[%s] expectation=%s\n", sceneMode.c_str(),
                      expectOk ? "OK" : "FAILED");
        return ! jsonOk ? 5 : (expectOk ? 0 : 4);
    }

    juce::ScopedJuceInitialiser_GUI gui;
    GuitarCompanionProcessor proc;

    // Dry path: amp block bypassed so the built-in guitar chain is exercised
    // without requiring a capture. Re-enabled only for the NAM case below.
    if (auto* ampOn = proc.apvts.getParameter (kAmpOnId))
        if (ampOn->getValue() > 0.0f)
            ampOn->setValueNotifyingHost (0.0f);

    // ---- scene: flag -> processor-owned Timer, editor never created ----
    std::fprintf (log, "[scene flag -> processor Timer via genuine JUCE message loop]\n");
    const SceneResult scene = runSceneCase (proc, true, kSceneAudioWindowMs, log);
    const bool sceneOk = scene.savedScene && scene.changed && scene.notAppliedBefore
                      && scene.restored && scene.editorNull
                      && scene.restoreElapsedMs >= 0.0
                      && scene.restoreElapsedMs < (double) kSceneFallbackMs
                      && scene.dispatchWithinBound
                      && scene.stopSentBefore == 0
                      && scene.signalAlloc == 0 && scene.signalFree == 0
                      && scene.signalLock == 0 && scene.signalTrylock == 0
                      && scene.signalCond == 0;
    std::fprintf (log, "\n");

    // ---- NAM: kick off the real async load after the clean dry start ----
    const std::string namPath = namModel != nullptr ? namModel : "";
    bool namMeasured = false;
    if (! namPath.empty())
    {
        std::fprintf (log, "[nam] requesting async load: %s\n", namPath.c_str());
        proc.loadModelAsync (0, juce::File (namPath));
    }
    else
    {
        std::fprintf (log, "[nam] no model supplied; NAM case recorded unmeasured\n");
    }

    std::vector<ComboRow> rows;

    std::fprintf (log, "[dry/built-in chain: rates x block sizes x drums]\n");
    const double rates[]  = { 44100.0, 48000.0, 96000.0 };
    const int    blocks[] = { 64, 128, 512 };
    bool namActiveSeen = false;
    for (double rate : rates)
        for (int block : blocks)
        {
            runCombo (proc, rate, block, false, "dry", warmBlocks, rows, log);
            runCombo (proc, rate, block, true,  "dry+drums", warmBlocks, rows, log);
            if (! namPath.empty() && ! namActiveSeen && proc.hasModelLoaded (0))
            {
                namActiveSeen = true;
                std::fprintf (log,
                    "  [nam] model became active during the dry run (activation swap "
                    "occurred inside a dry warm block; not separately isolated)\n");
            }
        }

    // ---- NAM case: only if the real processor reports the model active ----
    if (! namPath.empty())
    {
        std::fprintf (log, "\n[nam] post-dry state: loaded=%d resampling=%d error=\"%s\"\n",
                      (int) proc.hasModelLoaded (0),
                      (int) proc.isResampling (0),
                      proc.getLoadError().toRawUTF8());

        if (proc.hasModelLoaded (0))
        {
            if (auto* ampOn = proc.apvts.getParameter (kAmpOnId))
                ampOn->setValueNotifyingHost (1.0f);

            const double namRates[]  = { 48000.0, 96000.0 };
            const int    namBlocks[] = { 128, 512 };
            for (double rate : namRates)
                for (int block : namBlocks)
                {
                    runCombo (proc, rate, block, false, "nam", warmBlocks, rows, log);
                    runCombo (proc, rate, block, true,  "nam+drums", warmBlocks, rows, log);
                }
            namMeasured = true;

            if (auto* ampOn = proc.apvts.getParameter (kAmpOnId))
                ampOn->setValueNotifyingHost (0.0f);
        }
        else
        {
            std::fprintf (log, "[nam] UNMEASURED: model did not become active in the "
                              "bounded run (no polling/sleeps used)\n");
        }
    }

    // ---- outputs ----
    bool csvOk = true;
    writeCsv (csvPath, rows, csvOk);
    if (csvPath != nullptr)
        std::fprintf (log, "\nCSV %s: %s\n", csvOk ? "written" : "FAILED", csvPath);

    std::uint64_t totalColdAlloc = 0, totalWarmAlloc = 0, totalWarmFree = 0;
    std::uint64_t totalWarmLocks = 0, totalWarmNoop = 0;
    int casesWithWarmAlloc = 0, dryCases = 0, dryNonZero = 0;
    bool dryAllZero = true;
    for (const auto& row : rows)
    {
        totalColdAlloc += rtprobe::allocCallTotal (row.cold);
        totalWarmAlloc += rtprobe::allocCallTotal (row.warm);
        totalWarmFree  += rtprobe::freeCallTotal (row.warm);
        totalWarmLocks += row.warm.lockCalls + row.warm.trylockCalls + row.warm.condWaitCalls;
        totalWarmNoop  += row.warm.noopFrees;
        if (rtprobe::allocCallTotal (row.warm) || rtprobe::freeCallTotal (row.warm))
            ++casesWithWarmAlloc;
        const bool dry = (row.tag == "dry" || row.tag == "dry+drums");
        if (dry)
        {
            ++dryCases;
            const bool zero = rtprobe::allocCallTotal (row.cold) == 0
                           && rtprobe::freeCallTotal (row.cold) == 0
                           && row.cold.lockCalls == 0 && row.cold.trylockCalls == 0
                           && row.cold.condWaitCalls == 0
                           && rtprobe::allocCallTotal (row.warm) == 0
                           && rtprobe::freeCallTotal (row.warm) == 0
                           && row.warm.lockCalls == 0 && row.warm.trylockCalls == 0
                           && row.warm.condWaitCalls == 0;
            if (! zero) { ++dryNonZero; dryAllZero = false; }
        }
    }

    std::fprintf (log, "\n[totals across %zu measured cases]\n", rows.size());
    std::fprintf (log, "  dry cases (of %d) nonzero : %d%s\n", dryCases, dryNonZero,
                  dryAllZero ? " (all zero)" : "");
    std::fprintf (log, "  cold allocator calls      : %llu\n", (unsigned long long) totalColdAlloc);
    std::fprintf (log, "  warm allocator calls      : %llu\n", (unsigned long long) totalWarmAlloc);
    std::fprintf (log, "  warm free calls           : %llu\n", (unsigned long long) totalWarmFree);
    std::fprintf (log, "  warm no-op frees (nullptr): %llu\n", (unsigned long long) totalWarmNoop);
    std::fprintf (log, "  warm lock/cond operations : %llu\n", (unsigned long long) totalWarmLocks);
    std::fprintf (log, "  cases with warm alloc/free: %d\n", casesWithWarmAlloc);
    std::fprintf (log, "  NAM measured              : %s\n", namMeasured ? "yes" : "no");
    std::fprintf (log, "  scene restored via dispatch: %s (%.1f ms, bound %d ms)\n",
                  scene.restored ? "yes" : "NO", scene.restoreElapsedMs, kSceneFallbackMs);
    std::fprintf (log, "  editor created            : %s\n",
                  scene.editorNull ? "no" : "YES");

    const bool jsonOk = writeFindings (jsonPath, "full", true, true, rows, scene, sceneOk);

    // Exit status reflects the probe's own contract checks. NAM/dry allocation
    // findings are reported but do not fail the probe (NAM allocating is an
    // expected positive detection, not a probe failure).
    const bool ok = sceneOk && csvOk && jsonOk;
    return ok ? 0 : 5;
}
