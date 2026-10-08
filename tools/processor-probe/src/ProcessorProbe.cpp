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
#include <cstring>
#include <string>
#include <vector>

namespace
{

const char* const kInputGainId = "inputGain";
const char* const kAmpOnId     = "ampOn";

double msBetween (std::chrono::steady_clock::time_point a,
                  std::chrono::steady_clock::time_point b)
{
    return std::chrono::duration<double, std::milli> (b - a).count();
}

bool nearValue (float a, float b)
{
    return std::fabs (a - b) < 1.0e-4f;
}

void fillSignal (juce::AudioBuffer<float>& buf, int n)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* p = buf.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            p[i] = ((i % 64) < 32 ? 0.25f : -0.25f) * (ch == 0 ? 1.0f : 0.8f);
    }
}

void configureDrums (GuitarCompanionProcessor& proc, bool playing)
{
    auto& e = proc.drumEngine;

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
        e.playing.store (true);
    }
    else
    {
        for (int b = 0; b < drum::maxBars; ++b)
            e.clearBar (b);
        e.playing.store (false);
    }
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

struct ComboRow
{
    std::string       tag;
    double            rate = 0.0;
    int               block = 0;
    bool              drums = false;
    int               warmBlocks = 0;
    rtprobe::Snapshot cold {};
    rtprobe::Snapshot warm {};
    double            warmMs = 0.0;
};

void runCombo (GuitarCompanionProcessor& proc, double rate, int block, bool drums,
               const char* tag, int warmBlocks, std::vector<ComboRow>& rows,
               std::FILE* log)
{
    proc.prepareToPlay (rate, block);
    configureDrums (proc, drums);

    // All input/midi storage is allocated OUTSIDE the measured regions; only
    // the processor-owned processBlock is armed.
    juce::AudioBuffer<float> buf (2, block);
    fillSignal (buf, block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);

    {
        // Cold: the first processBlock after prepareToPlay, counted explicitly.
        rtprobe::resetAll();
        const auto c0 = rtprobe::snapshot();
        rtprobe::arm();
        proc.processBlock (buf, midi);
        rtprobe::disarm();
        const auto c1 = rtprobe::snapshot();

        ComboRow row;
        row.tag = tag;
        row.rate = rate;
        row.block = block;
        row.drums = drums;
        row.warmBlocks = warmBlocks;
        row.cold = rtprobe::delta (c0, c1);

        if (rtprobe::allocCallTotal (row.cold) != 0 || rtprobe::freeCallTotal (row.cold) != 0)
            dumpAllocRecords (log, "cold");

        // Warm: a bounded run of consecutive blocks, armed as one region so any
        // single block's allocation is captured in the totals.
        rtprobe::resetAll();
        const auto w0 = rtprobe::snapshot();
        const auto t0 = std::chrono::steady_clock::now();
        rtprobe::arm();
        for (int i = 0; i < warmBlocks; ++i)
            proc.processBlock (buf, midi);
        rtprobe::disarm();
        const auto t1 = std::chrono::steady_clock::now();
        const auto w1 = rtprobe::snapshot();

        row.warm = rtprobe::delta (w0, w1);
        row.warmMs = msBetween (t0, t1);

        if (rtprobe::allocCallTotal (row.warm) != 0 || rtprobe::freeCallTotal (row.warm) != 0)
            dumpAllocRecords (log, "warm");
        if (row.warm.lockCalls != 0 || row.warm.trylockCalls != 0 || row.warm.condWaitCalls != 0)
            dumpLockRecords (log, "warm");

        rows.push_back (row);

        std::fprintf (log,
                      "  [%-9s] rate=%7.0f block=%4d drums=%-8s "
                      "cold(alloc=%llu free=%llu lock=%llu) "
                      "warm(alloc=%llu free=%llu lock=%llu trylock=%llu cond=%llu) "
                      "over %d blocks %.2f ms (%.1f us/block)\n",
                      tag, rate, block, drums ? "playing" : "stopped",
                      (unsigned long long) rtprobe::allocCallTotal (row.cold),
                      (unsigned long long) rtprobe::freeCallTotal (row.cold),
                      (unsigned long long) row.cold.lockCalls,
                      (unsigned long long) rtprobe::allocCallTotal (row.warm),
                      (unsigned long long) rtprobe::freeCallTotal (row.warm),
                      (unsigned long long) row.warm.lockCalls,
                      (unsigned long long) row.warm.trylockCalls,
                      (unsigned long long) row.warm.condWaitCalls,
                      warmBlocks, row.warmMs,
                      row.warmMs * 1000.0 / (double) (warmBlocks > 0 ? warmBlocks : 1));

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
            std::fprintf (log, "\n");
        }
    }
}

struct SceneResult
{
    bool   savedScene = false;
    bool   notAppliedBeforeDispatch = false;
    bool   appliedAfterDispatch = false;
    bool   editorNeverCreated = false;
    int    blocksDriven = 0;
    double dispatchMs = 0.0;
    std::uint64_t signalAllocCalls = 0;
    std::uint64_t signalFrees = 0;
    std::uint64_t signalLocks = 0;
    float  savedRaw = 0.0f;
    float  changedRaw = 0.0f;
    float  beforeDispatchRaw = 0.0f;
    float  afterDispatchRaw = 0.0f;
    int    stopSentBefore = -1;
    int    stopSentAfter = -1;
    int    stopperTicks = -1;
};

SceneResult runSceneCase (GuitarCompanionProcessor& proc, std::FILE* log)
{
    SceneResult r;
    r.editorNeverCreated = (proc.getActiveEditor() == nullptr);

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

    // Change the live parameter so applying the stored scene is observable.
    const float newNorm = (savedNorm < 0.5f) ? 0.85f : 0.15f;
    gainParam->setValueNotifyingHost (newNorm);
    r.changedRaw = proc.apvts.getRawParameterValue (kInputGainId)->load();

    // Arm the processor-owned scene envelope. This is the public deferred path:
    // the audio thread fades out and raises its atomic flag once silent.
    proc.applySceneForSection (0);

    juce::AudioBuffer<float> buf (2, 128);
    fillSignal (buf, 128);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);

    // Enough blocks to complete the 12 ms fade-out: ceil(48000*0.012/128)+6.
    const int blocks = (int) std::ceil (48000.0 * 0.012 / 128.0) + 6;
    r.blocksDriven = blocks;

    rtprobe::resetAll();
    const auto s0 = rtprobe::snapshot();
    rtprobe::arm();
    for (int i = 0; i < blocks; ++i)
        proc.processBlock (buf, midi);
    rtprobe::disarm();
    const auto s1 = rtprobe::snapshot();
    const auto signalSnap = rtprobe::delta (s0, s1);
    r.signalAllocCalls = rtprobe::allocCallTotal (signalSnap);
    r.signalFrees = rtprobe::freeCallTotal (signalSnap);
    r.signalLocks = signalSnap.lockCalls + signalSnap.trylockCalls + signalSnap.condWaitCalls;

    r.beforeDispatchRaw = proc.apvts.getRawParameterValue (kInputGainId)->load();
    // No message loop has run: the safety-net delayed callback cannot fire and
    // the flag can only have been raised, not consumed.
    r.notAppliedBeforeDispatch = nearValue (r.beforeDispatchRaw, r.changedRaw)
                                 && ! nearValue (r.changedRaw, r.savedRaw);

    // The ONLY delivery is the processor-owned juce::Timer started in its
    // constructor. Pump the genuine JUCE message queue for a bounded wall-clock
    // window (< 262 ms, well under the no-device safety net). The stop is
    // driven by re-posted message callbacks rather than a juce::Timer, whose
    // internal elapsed accounting can fire a freshly started long timer early
    // once the thread has been idle for a while.
    struct AsyncStopper
    {
        std::chrono::steady_clock::time_point deadline;
        std::atomic<int> ticks { 0 };

        void arm (int ms)
        {
            deadline = std::chrono::steady_clock::now()
                       + std::chrono::milliseconds (ms);
            repost();
        }

        void repost()
        {
            juce::MessageManager::callAsync ([this]
            {
                ticks.fetch_add (1, std::memory_order_relaxed);
                if (std::chrono::steady_clock::now() >= deadline)
                    juce::MessageManager::getInstance()->stopDispatchLoop();
                else
                    repost();
            });
        }
    } stopper;
    stopper.arm (100);

    r.stopSentBefore = (int) juce::MessageManager::getInstance()->hasStopMessageBeenSent();

    const auto t0 = std::chrono::steady_clock::now();
    juce::MessageManager::getInstance()->runDispatchLoop();
    const auto t1 = std::chrono::steady_clock::now();
    r.dispatchMs = msBetween (t0, t1);
    r.stopSentAfter = (int) juce::MessageManager::getInstance()->hasStopMessageBeenSent();
    r.stopperTicks = stopper.ticks.load (std::memory_order_relaxed);

    r.afterDispatchRaw = proc.apvts.getRawParameterValue (kInputGainId)->load();
    r.appliedAfterDispatch = nearValue (r.afterDispatchRaw, r.savedRaw)
                             && ! nearValue (r.savedRaw, r.changedRaw);
    r.editorNeverCreated = r.editorNeverCreated && (proc.getActiveEditor() == nullptr);

    std::fprintf (log,
                  "  scene: saved=%.6f changed=%.6f beforeDispatch=%.6f afterDispatch=%.6f\n",
                  r.savedRaw, r.changedRaw, r.beforeDispatchRaw, r.afterDispatchRaw);
    std::fprintf (log,
                  "  scene: blocks=%d signalAlloc=%llu signalFrees=%llu signalLocks=%llu "
                  "dispatch=%.1f ms stopSentBefore=%d stopSentAfter=%d stopperTicks=%d "
                  "appliedBefore=%d appliedAfter=%d editorNull=%d\n",
                  r.blocksDriven,
                  (unsigned long long) r.signalAllocCalls,
                  (unsigned long long) r.signalFrees,
                  (unsigned long long) r.signalLocks,
                  r.dispatchMs,
                  r.stopSentBefore,
                  r.stopSentAfter,
                  r.stopperTicks,
                  (int) r.notAppliedBeforeDispatch,
                  (int) r.appliedAfterDispatch,
                  (int) r.editorNeverCreated);
    return r;
}

} // namespace

int main (int argc, char** argv)
{
    const char* csvPath   = nullptr;
    const char* namModel  = nullptr;
    int         warmBlocks = 256;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--csv") == 0 && i + 1 < argc)
            csvPath = argv[++i];
        else if (std::strcmp (argv[i], "--nam-model") == 0 && i + 1 < argc)
            namModel = argv[++i];
        else if (std::strcmp (argv[i], "--warm-blocks") == 0 && i + 1 < argc)
            warmBlocks = std::atoi (argv[++i]);
    }

    std::FILE* log = stdout;

    std::fprintf (log, "RT-002 processor runtime probe\n");
    std::fprintf (log, "==============================\n");
    std::fprintf (log, "warm blocks per case: %d\n\n", warmBlocks);

    std::fprintf (log, "[instrument self-check]\n");
    if (rtprobe::runSelfCheck (log) != 0)
    {
        std::fprintf (log, "SELFCHECK FAILED - refusing to report processor results\n");
        return 2;
    }
    std::fprintf (log, "  self-check PASS (gating + known new/delete/malloc/lock detection)\n\n");

    juce::ScopedJuceInitialiser_GUI gui;
    GuitarCompanionProcessor proc;

    // Dry path: amp block bypassed so the built-in guitar chain is exercised
    // without requiring a capture. Re-enabled only for the NAM case below.
    if (auto* ampOn = proc.apvts.getParameter (kAmpOnId))
        if (ampOn->getValue() > 0.0f)
            ampOn->setValueNotifyingHost (0.0f);

    // ---- scene-ready flag -> processor-owned Timer, editor never created ----
    // Run this while the process is young so the bounded message-loop window is
    // deterministic; the audio thread raises the flag and only the real JUCE
    // dispatch loop plus the processor-owned Timer can consume it.
    std::fprintf (log, "[scene jump, editor absent, public scene API]\n");
    const SceneResult scene = runSceneCase (proc, log);
    std::fprintf (log, "\n");

    // Kick off the real asynchronous NAM load early, so the bounded combo run
    // below gives the loader thread time to publish it. The probe never sleeps
    // or polls a fabricated status; the model is consumed by the real
    // processBlock swap and is only reported when the processor says it is live.
    std::string namPath = namModel != nullptr ? namModel : "";
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
    for (double rate : rates)
        for (int block : blocks)
        {
            runCombo (proc, rate, block, false, "dry", warmBlocks, rows, log);
            runCombo (proc, rate, block, true,  "dry+drums", warmBlocks, rows, log);
        }

    // ---- NAM case: only if the real processor reports the model active ----
    bool namMeasured = false;
    if (! namPath.empty())
    {
        std::fprintf (log, "\n[nam] post-combo state: loaded=%d resampling=%d error=\"%s\"\n",
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
                              "bounded dry run (no polling/sleeps used)\n");
        }
    }

    // ---- optional CSV ----
    if (csvPath != nullptr)
    {
        if (std::FILE* csv = std::fopen (csvPath, "w"))
        {
            std::fprintf (csv, "tag,rate,block,drums,warm_blocks,cold_alloc,cold_free,"
                               "cold_locks,warm_alloc,warm_free,warm_locks,warm_bytes,warm_ms\n");
            for (const auto& row : rows)
            {
                std::uint64_t warmBytes = 0;
                for (std::size_t k = 0; k < rtprobe::kKindCount; ++k)
                    if (k != (std::size_t) rtprobe::Kind::cxxDelete
                        && k != (std::size_t) rtprobe::Kind::cxxDeleteArray
                        && k != (std::size_t) rtprobe::Kind::cxxDeleteSized
                        && k != (std::size_t) rtprobe::Kind::cxxDeleteAligned
                        && k != (std::size_t) rtprobe::Kind::cFree)
                        warmBytes += row.warm.allocBytes[k];

                std::fprintf (csv, "%s,%.0f,%d,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.3f\n",
                              row.tag.c_str(), row.rate, row.block, (int) row.drums,
                              row.warmBlocks,
                              (unsigned long long) rtprobe::allocCallTotal (row.cold),
                              (unsigned long long) rtprobe::freeCallTotal (row.cold),
                              (unsigned long long) row.cold.lockCalls,
                              (unsigned long long) rtprobe::allocCallTotal (row.warm),
                              (unsigned long long) rtprobe::freeCallTotal (row.warm),
                              (unsigned long long) row.warm.lockCalls,
                              (unsigned long long) warmBytes,
                              row.warmMs);
            }
            std::fclose (csv);
            std::fprintf (log, "\nwrote CSV: %s\n", csvPath);
        }
        else
        {
            std::fprintf (log, "\ncould not open CSV: %s\n", csvPath);
        }
    }

    // ---- totals ----
    std::uint64_t totalColdAlloc = 0, totalWarmAlloc = 0, totalWarmFree = 0, totalWarmLocks = 0;
    std::uint64_t totalWarmNoopFrees = 0;
    int casesWithWarmAlloc = 0;
    for (const auto& row : rows)
    {
        totalColdAlloc += rtprobe::allocCallTotal (row.cold);
        totalWarmAlloc += rtprobe::allocCallTotal (row.warm);
        totalWarmFree  += rtprobe::freeCallTotal (row.warm);
        totalWarmLocks += row.warm.lockCalls + row.warm.trylockCalls + row.warm.condWaitCalls;
        totalWarmNoopFrees += row.warm.noopFrees;
        if (rtprobe::allocCallTotal (row.warm) != 0 || rtprobe::freeCallTotal (row.warm) != 0)
            ++casesWithWarmAlloc;
    }

    std::fprintf (log, "\n[totals across %zu measured cases]\n", rows.size());
    std::fprintf (log, "  cold allocator calls      : %llu\n", (unsigned long long) totalColdAlloc);
    std::fprintf (log, "  warm allocator calls      : %llu\n", (unsigned long long) totalWarmAlloc);
    std::fprintf (log, "  warm free calls           : %llu\n", (unsigned long long) totalWarmFree);
    std::fprintf (log, "  warm no-op frees (nullptr): %llu\n", (unsigned long long) totalWarmNoopFrees);
    std::fprintf (log, "  warm lock/cond operations : %llu\n", (unsigned long long) totalWarmLocks);
    std::fprintf (log, "  cases with warm alloc/free: %d\n", casesWithWarmAlloc);
    std::fprintf (log, "  NAM measured              : %s\n", namMeasured ? "yes" : "no");
    std::fprintf (log, "  scene flag delivered to processor Timer: %s\n",
                  scene.appliedAfterDispatch ? "yes" : "NO");
    std::fprintf (log, "  editor created            : %s\n",
                  scene.editorNeverCreated ? "no" : "YES");

    // This probe reports bounded measurements. A clean run is not a whole-
    // program allocation proof: NAM/hosted plugins, host transport and audio
    // device timing are separate evidence.
    const bool primaryEvidence =
        (scene.savedScene && scene.notAppliedBeforeDispatch
         && scene.appliedAfterDispatch && scene.editorNeverCreated);

    return primaryEvidence ? 0 : 3;
}
