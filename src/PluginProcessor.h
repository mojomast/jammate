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
    // Cab IR (message thread)

    void loadIrAsync (const juce::File& file);
    juce::String getIrName() const;
    juce::String getIrPath() const;
    bool hasIrLoaded() const noexcept { return irIsLoaded.load(); }

    //==========================================================================
    // Presets (message thread)

    juce::File getPresetsDirectory() const;
    juce::Array<juce::File> getPresetFiles() const;
    void savePreset (const juce::File& file);
    void loadPreset (const juce::File& file);
    /// delta = +1 / -1 navega pela lista ordenada de presets (com wrap).
    void loadAdjacentPreset (int delta);
    juce::String getCurrentPresetName() const;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };

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
    juce::dsp::Convolution convolution;
    std::atomic<bool> irIsLoaded { false };

    std::atomic<float>* pInputGain = nullptr;
    std::atomic<float>* pOutputGain = nullptr;
    std::atomic<float>* pAmpOn = nullptr;
    std::atomic<float>* pGateOn = nullptr;
    std::atomic<float>* pGateThresh = nullptr;
    std::atomic<float>* pGateRelease = nullptr;
    std::atomic<float>* pCabOn = nullptr;
    std::atomic<float>* pCabLevel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMProcessor)
};
