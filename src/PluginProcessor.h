#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DrumEngine.h"
#include "rt/RtSignal.h"
#include "jam/JamLiveInterface.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace nam
{
class DSP;
}

namespace dsp
{
template <typename T, int NCHANS, size_t A>
class ResamplingContainer;
}

namespace jam
{
// INT-LIVE-001: defined in jam/LiveJamSession.h, which stays out of this header
// so the JUCE plugin does not pull the whole live pipeline into every editor TU.
class LiveJamSession;
class IRhythmTracker;
}

class GuitarCompanionProcessor : public juce::AudioProcessor,
                                public jam::IJamLiveControl,
                              private juce::Timer
{
public:
    GuitarCompanionProcessor();
    ~GuitarCompanionProcessor() override;

    bool submitJamCommand (const jam::JamLiveCommand&) noexcept override;
    bool readJamLiveState (jam::JamLiveState&) const noexcept override;

    /** Additive, non-facade test/replay seam: hand the live pipeline a
        deterministic tracker for the NEXT prepare. Must be called while the
        processor is quiescent (no audio callback, before prepareToPlay); it
        replaces any tracker this build would otherwise create. The frozen
        IJamLiveControl facade above is unchanged. Ownership is taken and
        released with the session. */
    void setJamTrackerForTesting (std::unique_ptr<jam::IRhythmTracker> tracker) noexcept;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // NAM models - up to 3 AMP+CAB rigs in parallel (one capture per lane),
    // summed in the Mixer card. (message thread)

    static constexpr int maxRigs = 3;

    void loadModelAsync (int lane, const juce::File& file);
    bool isLoadingModel() const noexcept { return loading.load(); }
    juce::String getModelName (int lane) const;
    juce::String getModelPath (int lane) const;
    juce::String getLoadError() const;
    double getModelExpectedSampleRate (int lane) const;
    bool hasModelLoaded (int lane) const noexcept
    {
        return lane >= 0 && lane < maxRigs && modelIsActive[lane].load();
    }
    bool anyModelLoaded() const noexcept
    {
        for (int r = 0; r < maxRigs; ++r)
            if (modelIsActive[r].load())
                return true;
        return false;
    }
    bool isResampling (int lane) const noexcept
    {
        return lane >= 0 && lane < maxRigs && resamplingActive[lane].load();
    }
    /// First active lane without a capture; -1 if all are busy.
    int firstFreeModelLane() const;
    /// Number of active rigs (amp+cab pairs).
    int getRigCount() const;
    /// true if the file is loaded in ANY active lane.
    bool isModelFileLoaded (const juce::String& fullPath) const;

    //==========================================================================
    // ECO: pair of files from the same capture (normal + lighter version),
    // per lane. The ECO/auto-ECO chip swaps which of the two is loaded.

    void setModelPair (int lane, const juce::File& normal, const juce::File& eco);
    /// vNext: unloads a lane RT-safely (publishes the static unload sentinel).
    void unloadModelLane (int lane);
    juce::String getModelPathNormal (int lane) const;
    juce::String getModelPathEco (int lane) const;
    bool hasEcoVariant() const
    {
        for (int r = 0; r < maxRigs; ++r)
            if (getModelPathEco (r).isNotEmpty())
                return true;
        return false;
    }
    /// "A1", "A2" or "" (architecture of the lane's capture).
    juce::String getModelArchLabel (int lane) const;

    //==========================================================================
    // Capture metadata read from the .nam itself (message thread)

    /// Loudness in dB as measured by the NAM trainer. Captures are published
    /// anywhere from -30 to -10 dB, which is why parallel rigs rarely match.
    static constexpr double kUnknownLoudness = 1.0e9;
    /// What ALIGN levels every lane to.
    ///
    /// NOT -18 dB, which is where this started. -18 is the Neural Amp Modeler
    /// reference for AVERAGE level, and a guitar peaks 12-18 dB above its
    /// average - aiming the average at -18 puts the transients on top of
    /// 0 dBFS and the output clips. -30 leaves that peak headroom.
    static constexpr double kAlignTargetDb = -30.0;

    /// Raw "gear_type" ("amp", "amp_cab", "full-rig"...); empty when the
    /// capture carries no such field - roughly a third of them do not.
    juce::String getModelGearType (int lane) const;
    /// true only when the gear type SAYS a cabinet is baked in. Unknown gear
    /// is never guessed: a false positive here silently kills someone's IR.
    bool modelIncludesCab (int lane) const;
    /// kUnknownLoudness when the capture has no loudness field.
    double getModelLoudnessDb (int lane) const;
    /// Fills every loaded lane's trim so the captures meet at kAlignTargetDb.
    /// Returns how many lanes it could align.
    int alignRigLevels();

    //==========================================================================
    // Cab IR - one per rig lane (message thread)

    static constexpr int maxCabSlots = maxRigs;

    void loadIrAsync (int slot, const juce::File& file);
    /// Empties the slot (a preset that names it and leaves it blank).
    void unloadIrSlot (int slot);
    juce::String getIrName (int slot) const;
    juce::String getIrPath (int slot) const;
    bool hasIrLoaded (int slot) const noexcept
    {
        return slot >= 0 && slot < maxCabSlots && irLoadedFlags[slot].load();
    }
    /// First empty slot within the current count; -1 if all are busy.
    int firstFreeIrSlot() const;
    /// true if the file is loaded in ANY active slot.
    bool isIrFileLoaded (const juce::String& fullPath) const;
    int getCabCount() const;

    //==========================================================================
    // Presets (message thread)

    juce::File getPresetsDirectory() const;
    juce::Array<juce::File> getPresetFiles() const;
    void savePreset (const juce::File& file);
    void loadPreset (const juce::File& file);
    /// delta = +1 / -1 navigates the sorted preset list (with wrap).
    void loadAdjacentPreset (int delta);
    juce::String getCurrentPresetName() const;

    //==========================================================================
    // Reorderable chain: effects can change position; the Amp+Cabs block
    // ("amp") is a fixed anchor but effects can sit before/after.

    enum class ChainFx : int { gate = 0, od, eq, delay, reverb, ampBlock, comp, preEq, mod,
                               pitch, looper, limiter, extPlugin,
                               wah, harm, octaver, ringmod, bitcrush, slowgear,
                               exciter, deesser, tape, console, analyzer,
                               extPlugin2, extPlugin3, extPlugin4, extPlugin5,
                               extPlugin6, extPlugin7, extPlugin8 };
    static constexpr int numChainFx = 31;
    static constexpr int chainMaxSlots = 48; // expandable for future effects

    /// Current order as ids ("gate", "od", "amp", "eq", "delay", "reverb").
    juce::StringArray getChainOrder() const;
    /// Applies a new order (message thread). Invalid/missing ids are
    /// normalized: each effect appears once and "amp" is always present.
    void setChainOrder (const juce::StringArray& ids);

    static juce::String fxToString (ChainFx);
    static int fxFromString (const juce::String&); // -1 if unknown
    /// Position of the effect in the canonical order (to insert from the drawer in the right place).
    static int canonicalRank (int fx);
    static int canonicalRank (const juce::String& id);

    //==========================================================================
    // Looper (editor commands via atomics; transitions applied in
    // processBlock - the buffer is pre-allocated, no allocation on the audio thread)

    enum class LooperState : int { empty = 0, recording, playing, overdub, stopped };
    static constexpr int looperMaxSeconds = 60;

    LooperState getLooperState() const noexcept { return (LooperState) looperState.load(); }
    /// 1 = REC/close/overdub, 2 = play/stop, 3 = clear
    void requestLooperCommand (int cmd) noexcept { looperCmd.store (cmd); }
    double getLooperSeconds() const noexcept;
    double getLooperPosSeconds() const noexcept;
    /// Saves the current loop to WAV (Documents\Guitar Companion\Loops). Message
    /// thread; returns the created file or {} if there is no loop.
    juce::File exportLoopToWav() const;

    /// Current limiter gain reduction in dB (for the card).
    float getLimiterGrDb() const noexcept { return limGrDb.load(); }

    //==========================================================================
    // External VST3 plugin slots (JUCE hosting) - up to 3 in the chain. All
    // management happens on the message thread; swapping the audio instance uses
    // the same pending/retired protocol as the NAM models.

    // 8 slots in series - in practice the limit is CPU, not the count
    static constexpr int maxExtSlots = 8;

    /// Loads a .vst3 from disk (message thread). Optional stateToRestore
    /// applies the plugin's saved state after instantiation.
    void loadExternalPluginAsync (int slot, const juce::File& file,
                                  const juce::MemoryBlock* stateToRestore = nullptr);
    /// Unloads the plugin from the slot (message thread).
    void clearExternalPlugin (int slot);
    bool hasExternalPlugin (int slot) const noexcept
    {
        return slot >= 0 && slot < maxExtSlots && extLoaded[slot].load();
    }
    juce::String getExternalPluginName (int slot) const;
    juce::String getExternalPluginPath (int slot) const;
    /// Active instance - ONLY for the message thread to create the plugin panel.
    /// Close the panel before any swap (onExternalPluginWillChange).
    juce::AudioPluginInstance* getExternalInstance (int slot) const noexcept
    {
        return slot >= 0 && slot < maxExtSlots ? extUiInstance[slot].load() : nullptr;
    }
    /// Called (message thread) with the slot, before swapping/discarding the
    /// instance - the editor uses it to close the plugin panel window.
    std::function<void (int)> onExternalPluginWillChange;
    /// Collects retired instances (call periodically on the message thread).
    void collectExternalRetired()
    {
        for (auto& r : extRetired)
            delete r.exchange (nullptr);
        delete drumRetired.exchange (nullptr);
    }

    //==========================================================================
    // Drums module (phase 18): sequencer + internal sampler in DrumEngine;
    // optionally a hosted drum VST3 (same pending/retired protocol as the
    // effect slots). The drums play on their own bus summed into the master -
    // they never pass through the guitar chain.
    DrumEngine drumEngine;

    void loadDrumPluginAsync (const juce::File&,
                              const juce::MemoryBlock* stateToRestore = nullptr);
    void clearDrumPlugin();
    bool hasDrumPlugin() const noexcept { return drumLoaded.load(); }
    juce::String getDrumPluginName() const;
    juce::String getDrumPluginPath() const;
    /// Active instance - ONLY for the message thread to create the panel.
    juce::AudioPluginInstance* getDrumInstance() const noexcept
    {
        return drumUiInstance.load();
    }
    /// Called (message thread) before swapping/discarding the drum
    /// instance - the editor closes the panel window.
    std::function<void()> onDrumPluginWillChange;

    /// true when the current state differs from the last saved/loaded preset.
    bool isPresetDirty();
    /// Called by the editor each tick: consolidates the preset baseline after
    /// an async model/IR load finishes.
    void settlePresetBaseline();

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    /// Fraction of the block time spent in processBlock (0..1), smoothed.
    std::atomic<float> cpuLoad { 0.0f };

    //==========================================================================
    // Tuner: processBlock writes the input signal to a ring buffer; the
    // editor reads the most recent chunk for pitch analysis (read races are
    // benign - at worst they distort a throwaway analysis).
    static constexpr int tunerRingSize = 8192; // power of 2
    void readTunerBlock (float* dest, int numSamples) const;

    /// Tuner mute: silences the OUTPUT (detection continues, the tap is
    /// pre-chain). Set by the editor when the tuner is on + MUTE chip.
    void setTunerMuted (bool m) noexcept { tunerMute.store (m); }

    //==========================================================================
    // Spectrum analyzer: same scheme as the tuner - the card writes the
    // signal at that point in the chain; the editor reads and draws the spectrum.
    static constexpr int analyzerRingSize = 4096; // power of 2
    void readAnalyzerBlock (float* dest, int numSamples) const;

    //==========================================================================
    // Quick recorder: writes the OUTPUT to WAV via ThreadedWriter (RT-safe).
    // Besides the mix, it writes separate stems - "(guitar)" (post-chain, before
    // the drum sum) and "(drums)" (drum bus scaled by the drum level).
    // (message thread for start/stop)
    juce::File startRecording();
    void stopRecording();
    bool isRecording() const noexcept { return recActive.load() != nullptr; }

    //==========================================================================
    // vNext F6 - Song/Scenes: one full guitar-rig snapshot per drum SECTION.
    // With scenesOn, entering a section (at the bar start) applies its scene
    // with a short output fade-in to mask parameter/model jumps.
    // (save/clear/apply: message thread; hasScene is thread-safe enough for UI)
    void saveSceneForSection (int sec);
    void clearSceneForSection (int sec);
    bool hasScene (int sec) const
    {
        return sec >= 0 && sec < drum::maxSections && sceneXml[sec].isNotEmpty();
    }
    /// Applies a section's scene. The swap is DEFERRED on purpose: the guitar
    /// bus fades out first, the state lands while it is silent, and the fade-in
    /// waits until the new capture has really loaded. Same path as the
    /// automatic switch at the bar boundary. (message thread)
    void applySceneForSection (int sec);
    /// Keeps scenes aligned when a section is removed (shifts left from sec).
    void shiftScenesOnSectionRemove (int sec);
    /// Short human summary of a scene ("Mesa Dual... · 6 fx"); "" if none.
    juce::String sceneSummary (int sec) const;
    /// User-editable scene name ("RHYTHM", "LEAD"...); "" = unnamed.
    juce::String getSceneName (int sec) const
    {
        return sec >= 0 && sec < drum::maxSections ? sceneNames[sec] : juce::String();
    }
    void setSceneName (int sec, const juce::String& n)
    {
        if (sec >= 0 && sec < drum::maxSections)
            sceneNames[sec] = n;
    }
    std::atomic<bool> scenesOn { false };

    //==========================================================================
    // A/B: two full state snapshots; toggling saves the current one into the
    // active slot and loads the other. (message thread)
    void toggleAB();
    int getABIndex() const noexcept { return abCurrent; }

private:
    float tunerRing[tunerRingSize] = {};
    std::atomic<int> tunerWritePos { 0 };
    std::atomic<bool> tunerMute { false };

    float anRing[analyzerRingSize] = {};
    std::atomic<int> anWritePos { 0 };
    std::atomic<float>* pAnOn = nullptr;

    juce::TimeSliceThread recThread { "recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> recActive { nullptr };
    // stems: guitar (pre-drum-sum) and drums (drum bus)
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recWriterGtr, recWriterDrm;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> recActiveGtr { nullptr },
        recActiveDrm { nullptr };
    juce::AudioBuffer<float> recDrumScratch;   // drum bus scaled by level (RT)

    // ---- scenes (vNext F6) --------------------------------------------------
    // The audio thread never posts a message and never waits on this thread. It
    // raises sceneReadyToApply (one release store) once the guitar bus is
    // silent; the processor-owned timer polls that flag at 25 Hz on the message
    // thread and runs the swap. This replaces a juce::AsyncUpdater whose
    // triggerAsyncUpdate() was reachable from processBlock, and therefore took a
    // blocking CriticalSection, posted a system message, and could allocate.
    // See docs/research/RT-REACHABILITY.md finding F1.
    void applyPendingScene();                 // the swap; message thread only
    void timerCallback() override;            // polls sceneReadyToApply
    static constexpr int sceneSignalPollIntervalMs = 40;   // 25 Hz

    juce::ValueTree captureRigScene();         // captureState minus drums/UI prefs
    juce::String sceneXml[drum::maxSections];  // "" = section without a scene
    juce::String sceneNames[drum::maxSections];
    bool applyingSceneNow = false;             // guards the scene-restore in applyState
    jam::rt::SignalFlag sceneReadyToApply;     // audio thread sets, timer consumes
    std::atomic<int> scenePendingSection { -1 };
    int sceneLastSection = -1;                 // audio thread only

    // Scene change envelope. A scene swaps every parameter at once (no
    // smoothing) and its capture only lands tens/hundreds of ms later, on the
    // loader thread - a plain fade-in was over long before the new model
    // arrived, leaving the swap exposed. So the guitar bus now runs
    //     fadeOut -> hold (silent) -> fadeIn
    // the state is applied at the START of the hold (the audio thread asks for
    // it once it is actually silent) and the hold only ends when every async
    // model load fired by that state has published. sceneHoldMaxSec is the
    // safety net: a capture that never loads must not mute the rig forever.
    enum class SceneEnv { idle = 0, fadeOut, hold, fadeIn };
    static constexpr double sceneFadeOutSec = 0.012;
    static constexpr double sceneFadeInSec  = 0.030;
    static constexpr double sceneHoldMaxSec = 1.5;

    std::atomic<int> sceneEnvState { (int) SceneEnv::idle };
    std::atomic<int> sceneHoldLeft { 0 };          // samples left on the safety net
    // Generation, not a flag: a scene armed WHILE the message thread is still
    // inside applySceneNow (a long IR/VST3 load, say) would otherwise have its
    // "not applied yet" overwritten by the previous scene finishing, and the
    // hold would release before the new state ever landed.
    std::atomic<int> sceneGen { 0 };               // bumped by every arm
    std::atomic<int> sceneAppliedGen { 0 };        // generation actually applied
    std::atomic<int> sceneLoadsPending { 0 };      // scene's own async loads in flight
    std::atomic<bool> sceneArmed[drum::maxSections] = {};  // hasScene() for the audio thread
    float sceneEnvGain = 1.0f;                     // audio thread only
    std::shared_ptr<int> sceneLifetime { std::make_shared<int> (0) };  // late-callback guard

    void armSceneEnvelope (int sec);   // any thread: queue sec and start the fade out
    void applySceneNow (int sec);      // the real swap; only from applyPendingScene
    void refreshSceneFlags();          // message thread: sceneXml[] -> sceneArmed[]

    juce::ValueTree abSlots[2];
    int abCurrent = 0;

public:
    // vNext: DAW sync - the drum engine follows the host BPM when enabled
    // (persisted; no-op in the standalone, which has no play head)
    std::atomic<bool> drumHostSync { true };
    float getHostBpm() const { return hostBpm.load(); }

private:
    std::atomic<float> hostBpm { 0.0f };   // 0 = host BPM unknown

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    using Resampler = dsp::ResamplingContainer<float, 1, 12>;

    // Model + (optional) resampler, fully assembled off the audio thread
    // and swapped as a unit.
    struct LoadedModel
    {
        ~LoadedModel();

        std::unique_ptr<nam::DSP> model;
        std::unique_ptr<Resampler> resampler;              // null if host SR == capture SR
        std::function<void (float**, float**, int)> func;  // pre-built (no allocation on the audio thread)
        double modelSampleRate = -1.0;
        int latencySamples = 0;
    };

    /// (Re)prepares model and resampler for the current SR/block. Allocates - never
    /// call on the audio thread.
    void prepareLoadedModel (LoadedModel&, double hostRate, int blockSize) const;

    /// "Unload lane" sentinel: a static, never-owned LoadedModel with
    /// model == nullptr. Publishing it through pendingModels tells the audio
    /// thread to drop the lane WITHOUT any delete on the RT path (vNext P0).
    static LoadedModel* unloadSentinel();

    void applyState (juce::ValueTree state);
    /// includeExtPluginState=false skips getStateInformation of the hosted
    /// plugin (the preset fingerprint runs at 2 Hz - it would be too costly).
    juce::ValueTree captureState (bool includeExtPluginState = true);
    void setCurrentPresetName (const juce::String&);
    juce::int64 stateFingerprint();
    void createFactoryPresetsIfNeeded() const;

    juce::int64 savedFingerprint = 0;            // baseline of the current preset
    std::atomic<bool> baselinePending { false }; // awaiting async load

    // RT-safe swap per lane (pending/retired protocol):
    std::unique_ptr<LoadedModel> activeModels[maxRigs];       // audio thread only
    std::atomic<LoadedModel*> pendingModels[maxRigs] = {};    // loader -> audio
    std::atomic<LoadedModel*> retiredModels[maxRigs] = {};    // audio -> loader/dtor
    std::atomic<bool> modelIsActive[maxRigs] = {};
    std::atomic<bool> resamplingActive[maxRigs] = {};

    std::atomic<double> hostSampleRate { 48000.0 };
    std::atomic<int> preparedBlockSize { 512 };

    juce::ThreadPool loaderPool { 1 };
    std::atomic<bool> loading { false };

    mutable juce::CriticalSection modelInfoLock;
    juce::String modelNames[maxRigs], modelPaths[maxRigs], loadError;   // under modelInfoLock
    juce::String modelPathsStd[maxRigs], modelPathsEco[maxRigs];        // ECO pair
    juce::String modelArchLabels[maxRigs];                              // "A1"/"A2"
    juce::String modelGearTypes[maxRigs];                               // under modelInfoLock
    double modelLoudnessDb[maxRigs] = { kUnknownLoudness, kUnknownLoudness,
                                        kUnknownLoudness };             // under modelInfoLock
    double modelExpectedSampleRates[maxRigs] = { -1.0, -1.0, -1.0 };
    /// Set while applyState is restoring a preset/DAW session, so the
    /// "capture already has a cab" detection never overwrites a saved IR
    /// switch with its own opinion.
    std::atomic<bool> restoringState { false };
    juce::String currentPresetName;                      // under modelInfoLock

    juce::AudioBuffer<float> monoScratch;

    juce::dsp::NoiseGate<float> noiseGate;

    // parallel cabs (per-slot filters live with the other biquads, below)
    juce::dsp::Convolution convolutions[maxCabSlots];
    std::atomic<bool> irLoadedFlags[maxCabSlots] {};
    juce::String irNames[maxCabSlots], irPaths[maxCabSlots]; // under modelInfoLock
    juce::AudioBuffer<float> cabDryBuf, cabAccBuf, cabSlotBuf;
    void updateCabSlotFilters (int slot);

    // chain order (RT-safe: atomics read per entry in processBlock)
    std::atomic<int> chainOrder[chainMaxSlots] = {};
    std::atomic<int> chainLen { 0 };
    void writeDefaultChain();

    // one module per function - called in dynamic order by processBlock
    void processGateFx (float* io, int n);
    void processOdFx (float* io, int n);
    void processEqFx (float* io, int n);
    void processDelayFx (float* io, int n);
    void processReverbFx (float* io, int n);
    void processAmpAndCabs (juce::AudioBuffer<float>& buffer, float* io, int n);
    void processCompFx (float* io, int n);
    void processPreEqFx (float* io, int n);
    void processModFx (float* io, int n);
    void processPitchFx (float* io, int n);
    void processLooperFx (float* io, int n);
    void processLimiterFx (float* io, int n);
    void processExtFx (int slot, float* io, int n);
    void processWahFx (float* io, int n);
    void processHarmFx (float* io, int n);
    void processOctaverFx (float* io, int n);
    void processRingModFx (float* io, int n);
    void processBitcrushFx (float* io, int n);
    void processSlowGearFx (float* io, int n);
    void processExciterFx (float* io, int n);
    void processDeesserFx (float* io, int n);
    void processTapeFx (float* io, int n);
    void processConsoleFx (float* io, int n);
    void processAnalyzerFx (float* io, int n);

    // "Smart" gate: envelope follower with 6 dB hysteresis
    // (opens at the threshold, only closes 6 dB below - preserves sustain),
    // configurable hold and smooth release.
    struct SmartGate
    {
        void prepare (double sampleRate);
        void process (float* io, int n, float threshDb, float holdMs, float releaseMs);
        void reset() noexcept { env = 0.0f; gain = 0.0f; holdCounter = 0; isOpen = false; }

        double sr = 48000.0;
        float env = 0.0f, gain = 0.0f;
        float envAttackCoef = 0.0f, envReleaseCoef = 0.0f, gainAttackCoef = 0.0f;
        int holdCounter = 0;
        bool isOpen = false;
    };
    SmartGate smartGate;

    juce::dsp::Compressor<float> pedalComp;
    float compCachedSustain = -1.0f, compCachedAttack = -1.0f;
    int compCachedType = -1;
    void updatePreEqIfNeeded();

    // per-effect model variations (chosen on the card)
    std::atomic<float>* pOdType = nullptr;     // Screamer/Blues/Distortion/Fuzz
    std::atomic<float>* pCompType = nullptr;   // Dyna/Optical/Studio
    std::atomic<float>* pDelayType = nullptr;  // Digital/Analog/Tape
    std::atomic<float>* pRevType = nullptr;    // Hall/Room/Plate
    int odCachedType = -1, delayCachedType = -1, revCachedType = -1;
    bool odPostActive = false;
    double delayLfoPhase = 0.0;

    std::atomic<float>* pInputGain = nullptr;
    std::atomic<float>* pOutputGain = nullptr;
    std::atomic<float>* pAmpOn = nullptr;
    std::atomic<float>* pAmpEco = nullptr;
    std::atomic<float>* pAutoEco = nullptr;
    // amp knobs PER LANE (lane 0 uses the legacy ids "ampGain" etc.;
    // lanes 1/2 use "amp2Gain"/"amp3Gain" etc.)
    std::atomic<float>* pAmpGain[maxRigs] = {};
    std::atomic<float>* pAmpBass[maxRigs] = {};
    std::atomic<float>* pAmpMid[maxRigs] = {};
    std::atomic<float>* pAmpTreble[maxRigs] = {};
    std::atomic<float>* pAmpPresence[maxRigs] = {};
    std::atomic<float>* pAmpMaster[maxRigs] = {};
    std::atomic<float>* pGateOn = nullptr;
    std::atomic<float>* pGateThresh = nullptr;
    std::atomic<float>* pGateRelease = nullptr;
    std::atomic<float>* pGateHold = nullptr;
    std::atomic<float>* pCompOn = nullptr;
    std::atomic<float>* pCompSustain = nullptr;
    std::atomic<float>* pCompAttack = nullptr;
    std::atomic<float>* pCompBlend = nullptr;
    std::atomic<float>* pCompLevel = nullptr;
    std::atomic<float>* pPreEqOn = nullptr;
    std::atomic<float>* pPreEqLow = nullptr;
    std::atomic<float>* pPreEqMid = nullptr;
    std::atomic<float>* pPreEqHigh = nullptr;
    std::atomic<float>* pCabOn = nullptr;
    std::atomic<float>* pCabLevel = nullptr;   // legacy (no knob) - post-mix trim
    std::atomic<float>* pCabAir = nullptr;
    std::atomic<float>* pCabCount = nullptr;
    std::atomic<float>* pCabBlend[maxCabSlots] = {};
    std::atomic<float>* pCabLowCut[maxCabSlots] = {};
    std::atomic<float>* pCabHighCut[maxCabSlots] = {};
    std::atomic<float>* pCabPhase[maxCabSlots] = {};
    std::atomic<float>* pRigOn[maxCabSlots] = {};
    std::atomic<float>* pCabIrOn[maxCabSlots] = {};
    std::atomic<float>* pCabTrim[maxCabSlots] = {};
    std::atomic<float>* pOdOn = nullptr;
    std::atomic<float>* pOdDrive = nullptr;
    std::atomic<float>* pOdTone = nullptr;
    std::atomic<float>* pOdLevel = nullptr;
    std::atomic<float>* pEqOn = nullptr;
    std::atomic<float>* pEqLow = nullptr;
    std::atomic<float>* pEqMid = nullptr;
    std::atomic<float>* pEqHigh = nullptr;
    std::atomic<float>* pDelayOn = nullptr;
    std::atomic<float>* pDelayTime = nullptr;
    std::atomic<float>* pDelayFb = nullptr;
    std::atomic<float>* pDelayMix = nullptr;
    std::atomic<float>* pRevOn = nullptr;
    std::atomic<float>* pRevDecay = nullptr;
    std::atomic<float>* pRevMix = nullptr;
    std::atomic<float>* pRevPre = nullptr;

    // ---- amp tone stack (post-model): own biquads, no allocation
    // on the audio path (coefficients recomputed inline when the
    // parameters change - just arithmetic).
    struct Biquad
    {
        void setLowShelf (double sr, double freq, double dbGain);
        void setPeak (double sr, double freq, double dbGain, double q);
        void setHighShelf (double sr, double freq, double dbGain);
        void setLowPass (double sr, double freq, double q);
        void setHighPass (double sr, double freq, double q);
        void setBandPass (double sr, double freq, double q);
        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    void updateToneStackIfNeeded (int lane);
    void updateOdIfNeeded();
    void updateEqIfNeeded();
    void updateAirIfNeeded();

    Biquad tsBass[maxRigs], tsMid[maxRigs], tsTreble[maxRigs], tsPresence[maxRigs];
    float tsCachedBass[maxRigs] = { -1.0f, -1.0f, -1.0f };
    float tsCachedMid[maxRigs] = { -1.0f, -1.0f, -1.0f };
    float tsCachedTreble[maxRigs] = { -1.0f, -1.0f, -1.0f };
    float tsCachedPresence[maxRigs] = { -1.0f, -1.0f, -1.0f };

    // Overdrive (pre-amp): HP -> clip (per variation) -> tone LP -> post-filter
    Biquad odHp, odToneLp, odPost;
    float odCachedTone = -1.0f;

    // delay feedback path filters (Analog/Tape variations)
    Biquad delayFbLp, delayFbHp;
    float delayDuckEnv = 0.0f; // Ducking follower (repeats dip while you play)

    // post-cab EQ
    Biquad eqLowF, eqMidF, eqHighF;
    float eqCachedLow = -99.0f, eqCachedMid = -99.0f, eqCachedHigh = -99.0f;

    // cab AIR (post-mix high shelf)
    Biquad airF;
    float airCached = -1.0f;

    // low/high cut per cab slot
    Biquad cabLc[maxCabSlots], cabHc[maxCabSlots];
    float cabLcCached[maxCabSlots] = { -1.0f, -1.0f, -1.0f };
    float cabHcCached[maxCabSlots] = { -1.0f, -1.0f, -1.0f };

    // Pre-EQ (before the NAM - changes how the amp saturates)
    Biquad preEqLowF, preEqMidF, preEqHighF;
    float preEqCachedLow = -99.0f, preEqCachedMid = -99.0f, preEqCachedHigh = -99.0f;

    // Delay / Reverb (post-chain)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 * 2 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLineR { 96000 * 2 };
    juce::SmoothedValue<float> delaySmoothedSamples;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    float revCachedDecay = -1.0f;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> preDelayLine { 96000 / 4 };
    juce::AudioBuffer<float> wetScratch, wetScratchR;

    // stereo content (R-L difference) produced by ping-pong/reverb;
    // summed into the right channel in the final block assembly
    juce::AudioBuffer<float> stereoExtra;

    // modulations (Mod card)
    juce::dsp::Chorus<float> chorusFx;   // Chorus and Flanger (different delay/feedback)
    juce::dsp::Phaser<float> phaserFx;
    Biquad tremLp, tremHp;               // harmonic tremolo: anti-phase bands
    double tremPhase = 0.0;
    double tremPhase2 = 0.0;             // rotary: horn spins faster than the drum
    int modCachedType = -1;
    float modCachedRate = -1.0f, modCachedDepth = -1.0f, modCachedMix = -1.0f;
    std::atomic<float>* pModOn = nullptr;
    std::atomic<float>* pModType = nullptr;
    std::atomic<float>* pModRate = nullptr;
    std::atomic<float>* pModDepth = nullptr;
    std::atomic<float>* pModMix = nullptr;

    // spring reverb: bandpass on the wet path
    Biquad revSpringHp, revSpringLp;

    // ---- P4 cards (one effect per card, own controls) --------------
    // Wah (Auto/Manual/LFO): swept resonant bandpass
    Biquad wahBp;
    float wahEnv = 0.0f;
    double wahLfoPhase = 0.0;
    int wahRecalcCount = 0;
    std::atomic<float>* pWahOn = nullptr;
    std::atomic<float>* pWahMode = nullptr;
    std::atomic<float>* pWahFreq = nullptr;
    std::atomic<float>* pWahRange = nullptr;
    std::atomic<float>* pWahRes = nullptr;

    // Slow Gear: automatic swell (attack disappears, volume rises slowly)
    float sgEnv = 0.0f, sgGain = 1.0f, sgEnvPrev = 0.0f;
    std::atomic<float>* pSgOn = nullptr;
    std::atomic<float>* pSgSens = nullptr;
    std::atomic<float>* pSgRise = nullptr;

    // Analog octaver: flip-flop at zero crossings + envelope
    bool octFlip = false;
    float octPrev = 0.0f, octEnv = 0.0f, octToneCached = -1.0f;
    Biquad octLp;
    std::atomic<float>* pOctOn = nullptr;
    std::atomic<float>* pOctSub = nullptr;
    std::atomic<float>* pOctDirect = nullptr;
    std::atomic<float>* pOctTone = nullptr;

    // Ring modulator
    double rmPhase = 0.0;
    std::atomic<float>* pRmOn = nullptr;
    std::atomic<float>* pRmFreq = nullptr;
    std::atomic<float>* pRmMix = nullptr;

    // Bitcrusher (bits + sample rate reduction)
    float bcHold = 0.0f;
    float bcCount = 0.0f;
    std::atomic<float>* pBcOn = nullptr;
    std::atomic<float>* pBcBits = nullptr;
    std::atomic<float>* pBcRate = nullptr;
    std::atomic<float>* pBcMix = nullptr;

    // Diatonic harmonizer: pitch detection (decimated autocorrelation) +
    // granular shifter with the interval inside the chosen scale
    // (harmShift declared later, after the PitchShifter definition)
    static constexpr int harmDecimSize = 512;
    float harmDecim[harmDecimSize] = {};
    int harmDecimPos = 0;
    float harmAccum = 0.0f;
    int harmAccumCount = 0;
    int harmDetectCounter = 0;
    double harmRatioCur = 1.0;
    std::atomic<float>* pHarmOn = nullptr;
    std::atomic<float>* pHarmKey = nullptr;
    std::atomic<float>* pHarmScale = nullptr;
    std::atomic<float>* pHarmInterval = nullptr;
    std::atomic<float>* pHarmMix = nullptr;
    std::atomic<float>* pHarmLevel = nullptr;

    // Exciter: treble harmonics summed back in
    Biquad excHp;
    float excCachedFreq = -1.0f;
    std::atomic<float>* pExcOn = nullptr;
    std::atomic<float>* pExcFreq = nullptr;
    std::atomic<float>* pExcAmt = nullptr;

    // De-esser/resonance: dynamic cut of the harsh band
    Biquad dsBp;
    float dsEnv = 0.0f, dsCachedFreq = -1.0f;
    std::atomic<float>* pDsOn = nullptr;
    std::atomic<float>* pDsFreq = nullptr;
    std::atomic<float>* pDsSens = nullptr;
    std::atomic<float>* pDsAmt = nullptr;

    // Tape: saturation + head bump + treble rolloff
    Biquad tapeBumpF, tapeRollF, tapeHpF;
    float tapeCachedBump = -99.0f, tapeCachedRoll = -1.0f;
    std::atomic<float>* pTapeOn = nullptr;
    std::atomic<float>* pTapeDrive = nullptr;
    std::atomic<float>* pTapeBump = nullptr;
    std::atomic<float>* pTapeRoll = nullptr;

    // Console: subtle "glue" (sine waveshape, Airwindows Console style)
    std::atomic<float>* pCnsOn = nullptr;
    std::atomic<float>* pCnsAmt = nullptr;
    // ---------------------------------------------------------------------

    // ---- Pitch (card): 2-head granular shifter with sine/cosine crossfade
    // (constant power) over a fixed ring buffer.
    struct PitchShifter
    {
        static constexpr int bufSize = 1 << 14; // 16384 (341 ms @ 48k)
        float buf[bufSize] = {};
        int w = 0;
        double ph = 0.0;
        double win = 2400.0; // window samples (50 ms @ 48k)

        void prepare (double sr)
        {
            win = juce::jlimit (256.0, (double) bufSize / 2.0, 0.05 * sr);
            ph = 0.0;
            w = 0;
            std::fill (std::begin (buf), std::end (buf), 0.0f);
        }
        float readInterp (double delaySamples) const noexcept
        {
            double pos = (double) w - 1.0 - delaySamples;
            while (pos < 0.0)
                pos += bufSize;
            const int i0 = (int) pos & (bufSize - 1);
            const int i1 = (i0 + 1) & (bufSize - 1);
            const float frac = (float) (pos - std::floor (pos));
            return buf[i0] * (1.0f - frac) + buf[i1] * frac;
        }
        void process (float* io, int n, double ratio, float mix, float outGain) noexcept;
    };
    PitchShifter pitchShift;
    PitchShifter revShimmer; // octave up in the reverb wet (Shimmer-like)
    PitchShifter harmShift;  // Harmonizer second voice (P4 card)
    std::atomic<float>* pPitchOn = nullptr;
    std::atomic<float>* pPitchType = nullptr;   // Octave down / Octave up / Fifth / Detune
    std::atomic<float>* pPitchMix = nullptr;
    std::atomic<float>* pPitchLevel = nullptr;

    // ---- Looper (buffer pre-allocated in prepareToPlay)
    juce::AudioBuffer<float> loopBuf;
    std::atomic<int> looperState { 0 };  // LooperState
    std::atomic<int> looperCmd { 0 };    // 0 = none; see requestLooperCommand
    std::atomic<int> looperLen { 0 };    // recorded samples
    std::atomic<int> looperPos { 0 };
    std::atomic<float>* pLooperOn = nullptr;
    std::atomic<float>* pLooperLevel = nullptr;

    // ---- External VST3 slots (hosting), one set per slot
    juce::AudioPluginFormatManager extFormatManager;      // VST3 registered in the ctor
    std::unique_ptr<juce::AudioPluginInstance> extActive[maxExtSlots]; // audio thread only
    std::atomic<juce::AudioPluginInstance*> extPending[maxExtSlots] = {};
    std::atomic<juce::AudioPluginInstance*> extRetired[maxExtSlots] = {};
    std::atomic<juce::AudioPluginInstance*> extUiInstance[maxExtSlots] = {}; // for the panel
    std::atomic<bool> extLoaded[maxExtSlots] = {};
    std::atomic<bool> extUnloadRequest[maxExtSlots] = {};
    juce::String extName[maxExtSlots], extPath[maxExtSlots]; // under modelInfoLock
    juce::AudioBuffer<float> extBuf;                      // mono -> stereo for the guest
    juce::MidiBuffer extMidi;
    std::atomic<float>* pExtOn[maxExtSlots] = {};
    std::atomic<float>* pExtMix[maxExtSlots] = {};

    // ---- Drums: hosted VST3 instrument + own bus
    std::unique_ptr<juce::AudioPluginInstance> drumActive; // audio thread only
    std::atomic<juce::AudioPluginInstance*> drumPending { nullptr };
    std::atomic<juce::AudioPluginInstance*> drumRetired { nullptr };
    std::atomic<juce::AudioPluginInstance*> drumUiInstance { nullptr };
    std::atomic<bool> drumLoaded { false };
    std::atomic<bool> drumUnloadRequest { false };
    juce::String drumVstName, drumVstPath;                // under modelInfoLock
    juce::AudioBuffer<float> drumBuf;                     // stereo from the kit
    juce::MidiBuffer drumMidi;
    void processDrums (juce::AudioBuffer<float>& buffer, int numOut, int n);

    // ---- Live Jam pipeline (INT-LIVE-001) -----------------------------------
    // The JUCE-free control core owns the analysis ring, the analyzer worker,
    // the MusicalClock, the join policy and the DrumClockBridge. This processor
    // only provides the audio tap, the session-relative absolute sample cursor
    // and the drum-playback echo, and exposes the frozen IJamLiveControl facade.
    // The session is created once and persists across device re-prepares, so UI
    // readers never see the latest-value slot destroyed under them.
    std::unique_ptr<jam::LiveJamSession> jamSession_;
    std::unique_ptr<jam::IRhythmTracker> jamTestTracker_;  // injected for replay
    bool jamTrackerConfigured = false;                     // one-shot ownership handover

    // Session-relative absolute uint64 audio sample counter. It advances by the
    // actual callback size on every callback (including while Jam is stopped)
    // and shares the origin the DrumEngine is attached at.
    std::atomic<std::uint64_t> jamAudioSampleTime { 0 };

    // ---- Limiter (post-chain; JUCE brickwall)
    juce::dsp::Limiter<float> outLimiter;
    float limCachedThresh = 99.0f, limCachedRelease = -1.0f;
    std::atomic<float> limGrDb { 0.0f };
    std::atomic<float>* pLimOn = nullptr;
    std::atomic<float>* pLimCeiling = nullptr;
    std::atomic<float>* pLimRelease = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarCompanionProcessor)
};
