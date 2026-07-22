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

    enum class ChainFx : int { gate = 0, od, eq, delay, reverb, ampBlock, comp, preEq, mod,
                               pitch, looper, limiter, extPlugin,
                               wah, harm, octaver, ringmod, bitcrush, slowgear,
                               exciter, deesser, tape, console };
    static constexpr int numChainFx = 23;
    static constexpr int chainMaxSlots = 32; // expansível para efeitos futuros

    /// Ordem atual como ids ("gate", "od", "amp", "eq", "delay", "reverb").
    juce::StringArray getChainOrder() const;
    /// Aplica nova ordem (message thread). Ids inválidos/faltantes são
    /// normalizados: cada efeito aparece 1x e "amp" sempre presente.
    void setChainOrder (const juce::StringArray& ids);

    static juce::String fxToString (ChainFx);
    static int fxFromString (const juce::String&); // -1 se desconhecido

    //==========================================================================
    // Looper (comandos do editor via atomics; transições aplicadas no
    // processBlock — o buffer é pré-alocado, nada de alocação no áudio)

    enum class LooperState : int { empty = 0, recording, playing, overdub, stopped };
    static constexpr int looperMaxSeconds = 60;

    LooperState getLooperState() const noexcept { return (LooperState) looperState.load(); }
    /// 1 = REC/fecha/overdub · 2 = play/stop · 3 = limpar
    void requestLooperCommand (int cmd) noexcept { looperCmd.store (cmd); }
    double getLooperSeconds() const noexcept;
    double getLooperPosSeconds() const noexcept;
    /// Grava o loop atual em WAV (Documentos\GuitarRig NAM\Loops). Message
    /// thread; retorna o arquivo criado ou {} se não há loop.
    juce::File exportLoopToWav() const;

    /// Redução de ganho atual do limiter em dB (para o cartão).
    float getLimiterGrDb() const noexcept { return limGrDb.load(); }

    //==========================================================================
    // Slot de plugin VST3 externo (hosting JUCE). Toda a gestão acontece na
    // message thread; a troca da instância no áudio usa o mesmo protocolo
    // pending/retired dos modelos NAM.

    /// Carrega um .vst3 do disco (message thread). stateToRestore opcional
    /// aplica o estado salvo do plugin após a instanciação.
    void loadExternalPluginAsync (const juce::File& file,
                                  const juce::MemoryBlock* stateToRestore = nullptr);
    /// Descarrega o plugin do slot (message thread).
    void clearExternalPlugin();
    bool hasExternalPlugin() const noexcept { return extLoaded.load(); }
    juce::String getExternalPluginName() const;
    juce::String getExternalPluginPath() const;
    /// Instância ativa — SÓ para a message thread criar o painel do plugin.
    /// Feche o painel antes de qualquer troca (onExternalPluginWillChange).
    juce::AudioPluginInstance* getExternalInstance() const noexcept { return extUiInstance.load(); }
    /// Chamado (message thread) antes de trocar/descartar a instância —
    /// o editor usa para fechar a janela do painel do plugin.
    std::function<void()> onExternalPluginWillChange;
    /// Coleta a instância aposentada (chamar periodicamente na message thread).
    void collectExternalRetired() { delete extRetired.exchange (nullptr); }

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
    /// includeExtPluginState=false pula o getStateInformation do plugin
    /// hospedado (o fingerprint de preset roda a 2 Hz — seria caro demais).
    juce::ValueTree captureState (bool includeExtPluginState = true);
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
    void processPitchFx (float* io, int n);
    void processLooperFx (float* io, int n);
    void processLimiterFx (float* io, int n);
    void processExtFx (float* io, int n);
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

    // Overdrive (pré-amp): HP -> clip (por variação) -> tone LP -> pós-filtro
    Biquad odHp, odToneLp, odPost;
    float odCachedTone = -1.0f;

    // filtros do caminho de feedback do delay (variações Analog/Tape)
    Biquad delayFbLp, delayFbHp;
    float delayDuckEnv = 0.0f; // follower do Ducking (repetições abaixam ao tocar)

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
    double tremPhase2 = 0.0;             // rotary: corneta gira mais rápido que o tambor
    int modCachedType = -1;
    float modCachedRate = -1.0f, modCachedDepth = -1.0f, modCachedMix = -1.0f;
    std::atomic<float>* pModOn = nullptr;
    std::atomic<float>* pModType = nullptr;
    std::atomic<float>* pModRate = nullptr;
    std::atomic<float>* pModDepth = nullptr;
    std::atomic<float>* pModMix = nullptr;

    // spring reverb: bandpass no caminho wet
    Biquad revSpringHp, revSpringLp;

    // ---- cards P4 (um efeito por card, controles próprios) --------------
    // Wah (Auto/Manual/LFO): bandpass ressonante varrido
    Biquad wahBp;
    float wahEnv = 0.0f;
    double wahLfoPhase = 0.0;
    int wahRecalcCount = 0;
    std::atomic<float>* pWahOn = nullptr;
    std::atomic<float>* pWahMode = nullptr;
    std::atomic<float>* pWahFreq = nullptr;
    std::atomic<float>* pWahRange = nullptr;
    std::atomic<float>* pWahRes = nullptr;

    // Slow Gear: swell automático (ataque some, volume sobe devagar)
    float sgEnv = 0.0f, sgGain = 1.0f, sgEnvPrev = 0.0f;
    std::atomic<float>* pSgOn = nullptr;
    std::atomic<float>* pSgSens = nullptr;
    std::atomic<float>* pSgRise = nullptr;

    // Octaver analógico: flip-flop nos cruzamentos de zero + envelope
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

    // Harmonizer diatônico: detecção de pitch (autocorrelação decimada) +
    // shifter granular com intervalo dentro da escala escolhida
    // (harmShift declarado adiante, após a definição de PitchShifter)
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

    // Exciter: harmônicos de agudos somados de volta
    Biquad excHp;
    float excCachedFreq = -1.0f;
    std::atomic<float>* pExcOn = nullptr;
    std::atomic<float>* pExcFreq = nullptr;
    std::atomic<float>* pExcAmt = nullptr;

    // De-esser/ressonância: corte dinâmico da banda áspera
    Biquad dsBp;
    float dsEnv = 0.0f, dsCachedFreq = -1.0f;
    std::atomic<float>* pDsOn = nullptr;
    std::atomic<float>* pDsFreq = nullptr;
    std::atomic<float>* pDsSens = nullptr;
    std::atomic<float>* pDsAmt = nullptr;

    // Tape: saturação + head bump + rolloff de agudos
    Biquad tapeBumpF, tapeRollF, tapeHpF;
    float tapeCachedBump = -99.0f, tapeCachedRoll = -1.0f;
    std::atomic<float>* pTapeOn = nullptr;
    std::atomic<float>* pTapeDrive = nullptr;
    std::atomic<float>* pTapeBump = nullptr;
    std::atomic<float>* pTapeRoll = nullptr;

    // Console: "cola" sutil (waveshape seno estilo Airwindows Console)
    std::atomic<float>* pCnsOn = nullptr;
    std::atomic<float>* pCnsAmt = nullptr;
    // ---------------------------------------------------------------------

    // ---- Pitch (cartão): shifter granular de 2 cabeças com crossfade
    // seno/cosseno (potência constante) sobre um ring buffer fixo.
    struct PitchShifter
    {
        static constexpr int bufSize = 1 << 14; // 16384 (341 ms @ 48k)
        float buf[bufSize] = {};
        int w = 0;
        double ph = 0.0;
        double win = 2400.0; // amostras da janela (50 ms @ 48k)

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
    PitchShifter revShimmer; // oitava acima no wet do reverb (tipo Shimmer)
    PitchShifter harmShift;  // segunda voz do Harmonizer (card P4)
    std::atomic<float>* pPitchOn = nullptr;
    std::atomic<float>* pPitchType = nullptr;   // Oitava ↓ / Oitava ↑ / Quinta / Detune
    std::atomic<float>* pPitchMix = nullptr;
    std::atomic<float>* pPitchLevel = nullptr;

    // ---- Looper (buffer pré-alocado em prepareToPlay)
    juce::AudioBuffer<float> loopBuf;
    std::atomic<int> looperState { 0 };  // LooperState
    std::atomic<int> looperCmd { 0 };    // 0 = nada; ver requestLooperCommand
    std::atomic<int> looperLen { 0 };    // amostras gravadas
    std::atomic<int> looperPos { 0 };
    std::atomic<float>* pLooperOn = nullptr;
    std::atomic<float>* pLooperLevel = nullptr;

    // ---- Slot VST3 externo (hosting)
    juce::AudioPluginFormatManager extFormatManager;      // VST3 registrado no ctor
    std::unique_ptr<juce::AudioPluginInstance> extActive; // só thread de áudio
    std::atomic<juce::AudioPluginInstance*> extPending { nullptr };
    std::atomic<juce::AudioPluginInstance*> extRetired { nullptr };
    std::atomic<juce::AudioPluginInstance*> extUiInstance { nullptr }; // p/ o painel (message thread)
    std::atomic<bool> extLoaded { false };
    std::atomic<bool> extUnloadRequest { false };
    juce::String extName, extPath;                        // sob modelInfoLock
    juce::AudioBuffer<float> extBuf;                      // mono -> estéreo p/ o hóspede
    juce::MidiBuffer extMidi;
    std::atomic<float>* pExtOn = nullptr;
    std::atomic<float>* pExtMix = nullptr;

    // ---- Limiter (pós-cadeia; brickwall do JUCE)
    juce::dsp::Limiter<float> outLimiter;
    float limCachedThresh = 99.0f, limCachedRelease = -1.0f;
    std::atomic<float> limGrDb { 0.0f };
    std::atomic<float>* pLimOn = nullptr;
    std::atomic<float>* pLimCeiling = nullptr;
    std::atomic<float>* pLimRelease = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMProcessor)
};
