#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
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

class GuitarRigNAMProcessor : public juce::AudioProcessor
{
public:
    GuitarRigNAMProcessor();
    ~GuitarRigNAMProcessor() override;

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
    // Modelos NAM — até 3 rigs AMP+CAB em paralelo (um capture por lane),
    // somados no card Mixer. (message thread)

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
    /// Primeira lane ativa sem capture; -1 se todas ocupadas.
    int firstFreeModelLane() const;
    /// Número de rigs (pares amp+cab) ativos.
    int getRigCount() const;
    /// true se o arquivo está carregado em QUALQUER lane ativa.
    bool isModelFileLoaded (const juce::String& fullPath) const;

    //==========================================================================
    // ECO: par de arquivos do mesmo capture (normal + versão mais leve),
    // por lane. O chip ECO/auto-ECO troca qual dos dois está carregado.

    void setModelPair (int lane, const juce::File& normal, const juce::File& eco);
    juce::String getModelPathNormal (int lane) const;
    juce::String getModelPathEco (int lane) const;
    bool hasEcoVariant() const
    {
        for (int r = 0; r < maxRigs; ++r)
            if (getModelPathEco (r).isNotEmpty())
                return true;
        return false;
    }
    /// "V1", "V2" ou "" (arquitetura do capture da lane).
    juce::String getModelArchLabel (int lane) const;

    //==========================================================================
    // Cab IR — um por lane de rig (message thread)

    static constexpr int maxCabSlots = maxRigs;

    void loadIrAsync (int slot, const juce::File& file);
    juce::String getIrName (int slot) const;
    juce::String getIrPath (int slot) const;
    bool hasIrLoaded (int slot) const noexcept
    {
        return slot >= 0 && slot < maxCabSlots && irLoadedFlags[slot].load();
    }
    /// Primeiro slot vazio dentro do count atual; -1 se todos ocupados.
    int firstFreeIrSlot() const;
    /// true se o arquivo está carregado em QUALQUER slot ativo.
    bool isIrFileLoaded (const juce::String& fullPath) const;
    int getCabCount() const;

    //==========================================================================
    // Presets (message thread)

    juce::File getPresetsDirectory() const;
    juce::Array<juce::File> getPresetFiles() const;
    void savePreset (const juce::File& file);
    void loadPreset (const juce::File& file);
    /// delta = +1 / -1 navega pela lista ordenada de presets (com wrap).
    void loadAdjacentPreset (int delta);
    juce::String getCurrentPresetName() const;

    //==========================================================================
    // Cadeia reordenável: os efeitos podem mudar de posição; o bloco
    // Amp+Cabs ("amp") é âncora fixa mas efeitos podem ficar antes/depois.

    enum class ChainFx : int { gate = 0, od, eq, delay, reverb, ampBlock, comp, preEq, mod };
    static constexpr int numChainFx = 9;
    static constexpr int chainMaxSlots = 16; // expansível para efeitos futuros

    /// Ordem atual como ids ("gate", "od", "amp", "eq", "delay", "reverb").
    juce::StringArray getChainOrder() const;
    /// Aplica nova ordem (message thread). Ids inválidos/faltantes são
    /// normalizados: cada efeito aparece 1x e "amp" sempre presente.
    void setChainOrder (const juce::StringArray& ids);

    static juce::String fxToString (ChainFx);
    static int fxFromString (const juce::String&); // -1 se desconhecido

    /// true quando o estado atual difere do último preset salvo/carregado.
    bool isPresetDirty();
    /// Chamado pelo editor a cada tick: consolida a baseline do preset depois
    /// que um load assíncrono de modelo/IR termina.
    void settlePresetBaseline();

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    /// Fração do tempo de bloco gasta em processBlock (0..1), suavizada.
    std::atomic<float> cpuLoad { 0.0f };

    //==========================================================================
    // Afinador: o processBlock grava o sinal de entrada num ring buffer; o
    // editor lê o trecho mais recente para análise de pitch (corridas de
    // leitura são benignas — no máximo distorcem uma análise descartável).
    static constexpr int tunerRingSize = 8192; // potência de 2
    void readTunerBlock (float* dest, int numSamples) const;

private:
    float tunerRing[tunerRingSize] = {};
    std::atomic<int> tunerWritePos { 0 };

public:

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    using Resampler = dsp::ResamplingContainer<float, 1, 12>;

    // Modelo + (opcional) resampler, montado por completo fora da thread de
    // áudio e trocado como uma unidade.
    struct LoadedModel
    {
        ~LoadedModel();

        std::unique_ptr<nam::DSP> model;
        std::unique_ptr<Resampler> resampler;              // null se SR do host == SR do capture
        std::function<void (float**, float**, int)> func;  // pré-construída (sem alocação no áudio)
        double modelSampleRate = -1.0;
        int latencySamples = 0;
    };

    /// (Re)prepara modelo e resampler para o SR/bloco atuais. Aloca — nunca
    /// chamar na thread de áudio.
    void prepareLoadedModel (LoadedModel&, double hostRate, int blockSize) const;

    void applyState (juce::ValueTree state);
    juce::ValueTree captureState();
    void setCurrentPresetName (const juce::String&);
    juce::int64 stateFingerprint();
    void createFactoryPresetsIfNeeded() const;

    juce::int64 savedFingerprint = 0;            // baseline do preset atual
    std::atomic<bool> baselinePending { false }; // aguardando load assíncrono

    // Troca RT-safe por lane (protocolo pending/retired):
    std::unique_ptr<LoadedModel> activeModels[maxRigs];       // só thread de áudio
    std::atomic<LoadedModel*> pendingModels[maxRigs] = {};    // loader -> áudio
    std::atomic<LoadedModel*> retiredModels[maxRigs] = {};    // áudio -> loader/dtor
    std::atomic<bool> modelIsActive[maxRigs] = {};
    std::atomic<bool> resamplingActive[maxRigs] = {};

    std::atomic<double> hostSampleRate { 48000.0 };
    std::atomic<int> preparedBlockSize { 512 };

    juce::ThreadPool loaderPool { 1 };
    std::atomic<bool> loading { false };

    mutable juce::CriticalSection modelInfoLock;
    juce::String modelNames[maxRigs], modelPaths[maxRigs], loadError;   // sob modelInfoLock
    juce::String modelPathsStd[maxRigs], modelPathsEco[maxRigs];        // par ECO
    juce::String modelArchLabels[maxRigs];                              // "V1"/"V2"
    double modelExpectedSampleRates[maxRigs] = { -1.0, -1.0, -1.0 };
    juce::String currentPresetName;                      // sob modelInfoLock

    juce::AudioBuffer<float> monoScratch;

    juce::dsp::NoiseGate<float> noiseGate;

    // cabs paralelos (filtros por slot ficam junto dos outros biquads, abaixo)
    juce::dsp::Convolution convolutions[maxCabSlots];
    std::atomic<bool> irLoadedFlags[maxCabSlots] {};
    juce::String irNames[maxCabSlots], irPaths[maxCabSlots]; // sob modelInfoLock
    juce::AudioBuffer<float> cabDryBuf, cabAccBuf, cabSlotBuf;
    void updateCabSlotFilters (int slot);

    // ordem da cadeia (RT-safe: atomics lidos por entrada no processBlock)
    std::atomic<int> chainOrder[chainMaxSlots] = {};
    std::atomic<int> chainLen { 0 };
    void writeDefaultChain();

    // um módulo por função — chamados na ordem dinâmica pelo processBlock
    void processGateFx (float* io, int n);
    void processOdFx (float* io, int n);
    void processEqFx (float* io, int n);
    void processDelayFx (float* io, int n);
    void processReverbFx (float* io, int n);
    void processAmpAndCabs (juce::AudioBuffer<float>& buffer, float* io, int n);
    void processCompFx (float* io, int n);
    void processPreEqFx (float* io, int n);
    void processModFx (float* io, int n);

    // Gate "inteligente": follower de envelope com histerese de 6 dB
    // (abre no threshold, só fecha 6 dB abaixo — preserva o sustain),
    // hold configurável e release suave.
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

    // variações de modelo por efeito (escolhidas no cartão)
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
    // knobs do amp POR LANE (lane 0 usa os ids legados "ampGain" etc.;
    // lanes 1/2 usam "amp2Gain"/"amp3Gain" etc.)
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
    std::atomic<float>* pCabLevel = nullptr;   // legado (sem knob) — trim pós-mix
    std::atomic<float>* pCabAir = nullptr;
    std::atomic<float>* pCabCount = nullptr;
    std::atomic<float>* pCabBlend[maxCabSlots] = {};
    std::atomic<float>* pCabLowCut[maxCabSlots] = {};
    std::atomic<float>* pCabHighCut[maxCabSlots] = {};
    std::atomic<float>* pCabPhase[maxCabSlots] = {};
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

    // ---- tone stack do amp (pós-modelo): biquads próprios, sem alocação
    // no caminho de áudio (coeficientes recalculados inline quando os
    // parâmetros mudam — só aritmética).
    struct Biquad
    {
        void setLowShelf (double sr, double freq, double dbGain);
        void setPeak (double sr, double freq, double dbGain, double q);
        void setHighShelf (double sr, double freq, double dbGain);
        void setLowPass (double sr, double freq, double q);
        void setHighPass (double sr, double freq, double q);
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

    // Overdrive (pré-amp): HP -> clip (por variação) -> tone LP -> pós-filtro
    Biquad odHp, odToneLp, odPost;
    float odCachedTone = -1.0f;

    // filtros do caminho de feedback do delay (variações Analog/Tape)
    Biquad delayFbLp, delayFbHp;

    // EQ pós-cab
    Biquad eqLowF, eqMidF, eqHighF;
    float eqCachedLow = -99.0f, eqCachedMid = -99.0f, eqCachedHigh = -99.0f;

    // AIR do cab (high shelf pós-mix)
    Biquad airF;
    float airCached = -1.0f;

    // low/high cut por slot de cab
    Biquad cabLc[maxCabSlots], cabHc[maxCabSlots];
    float cabLcCached[maxCabSlots] = { -1.0f, -1.0f, -1.0f };
    float cabHcCached[maxCabSlots] = { -1.0f, -1.0f, -1.0f };

    // Pré-EQ (antes do NAM — muda como o amp satura)
    Biquad preEqLowF, preEqMidF, preEqHighF;
    float preEqCachedLow = -99.0f, preEqCachedMid = -99.0f, preEqCachedHigh = -99.0f;

    // Delay / Reverb (pós-cadeia)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 * 2 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLineR { 96000 * 2 };
    juce::SmoothedValue<float> delaySmoothedSamples;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    float revCachedDecay = -1.0f;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> preDelayLine { 96000 / 4 };
    juce::AudioBuffer<float> wetScratch, wetScratchR;

    // conteúdo estéreo (diferença R-L) produzido por ping-pong/reverb;
    // somado ao canal direito na montagem final do bloco
    juce::AudioBuffer<float> stereoExtra;

    // modulações (cartão Mod)
    juce::dsp::Chorus<float> chorusFx;   // Chorus e Flanger (delay/feedback distintos)
    juce::dsp::Phaser<float> phaserFx;
    Biquad tremLp, tremHp;               // tremolo harmônico: bandas anti-fase
    double tremPhase = 0.0;
    int modCachedType = -1;
    float modCachedRate = -1.0f, modCachedDepth = -1.0f, modCachedMix = -1.0f;
    std::atomic<float>* pModOn = nullptr;
    std::atomic<float>* pModType = nullptr;
    std::atomic<float>* pModRate = nullptr;
    std::atomic<float>* pModDepth = nullptr;
    std::atomic<float>* pModMix = nullptr;

    // spring reverb: bandpass no caminho wet
    Biquad revSpringHp, revSpringLp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMProcessor)
};
