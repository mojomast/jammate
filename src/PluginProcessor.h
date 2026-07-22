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
    // Modelo NAM (message thread)

    void loadModelAsync (const juce::File& file);
    bool isLoadingModel() const noexcept { return loading.load(); }
    juce::String getModelName() const;
    juce::String getModelPath() const;
    juce::String getLoadError() const;
    double getModelExpectedSampleRate() const;
    bool hasModelLoaded() const noexcept { return modelIsActive.load(); }
    /// true quando o modelo ativo roda via resampler (SR do capture != host).
    bool isResampling() const noexcept { return resamplingActive.load(); }

    //==========================================================================
    // Cab IR — até 3 slots em paralelo (message thread)

    static constexpr int maxCabSlots = 3;

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

    enum class ChainFx : int { gate = 0, od, eq, delay, reverb, ampBlock };
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

    // Troca RT-safe (mesmo protocolo da Fase 2, agora com LoadedModel):
    std::unique_ptr<LoadedModel> activeModel;            // só thread de áudio
    std::atomic<LoadedModel*> pendingModel { nullptr };  // loader -> áudio
    std::atomic<LoadedModel*> retiredModel { nullptr };  // áudio -> loader/dtor
    std::atomic<bool> modelIsActive { false };
    std::atomic<bool> resamplingActive { false };

    std::atomic<double> hostSampleRate { 48000.0 };
    std::atomic<int> preparedBlockSize { 512 };

    juce::ThreadPool loaderPool { 1 };
    std::atomic<bool> loading { false };

    mutable juce::CriticalSection modelInfoLock;
    juce::String modelName, modelPath, loadError;        // sob modelInfoLock
    juce::String irName, irPath;                         // sob modelInfoLock
    juce::String currentPresetName;                      // sob modelInfoLock
    double modelExpectedSampleRate = -1.0;               // sob modelInfoLock

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

    std::atomic<float>* pInputGain = nullptr;
    std::atomic<float>* pOutputGain = nullptr;
    std::atomic<float>* pAmpOn = nullptr;
    std::atomic<float>* pAmpGain = nullptr;
    std::atomic<float>* pAmpBass = nullptr;
    std::atomic<float>* pAmpMid = nullptr;
    std::atomic<float>* pAmpTreble = nullptr;
    std::atomic<float>* pAmpPresence = nullptr;
    std::atomic<float>* pAmpMaster = nullptr;
    std::atomic<float>* pGateOn = nullptr;
    std::atomic<float>* pGateThresh = nullptr;
    std::atomic<float>* pGateRelease = nullptr;
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

    void updateToneStackIfNeeded();
    void updateOdIfNeeded();
    void updateEqIfNeeded();
    void updateAirIfNeeded();

    Biquad tsBass, tsMid, tsTreble, tsPresence;
    float tsCachedBass = -1.0f, tsCachedMid = -1.0f,
          tsCachedTreble = -1.0f, tsCachedPresence = -1.0f;

    // Overdrive (pré-amp): HP fixo -> tanh -> tone LP -> level
    Biquad odHp, odToneLp;
    float odCachedTone = -1.0f;

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

    // Delay / Reverb (pós-cadeia)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 * 2 };
    juce::SmoothedValue<float> delaySmoothedSamples;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    float revCachedDecay = -1.0f;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> preDelayLine { 96000 / 4 };
    juce::AudioBuffer<float> wetScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMProcessor)
};
