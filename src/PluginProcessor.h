#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace nam
{
class DSP;
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
    // Modelo NAM — API para o editor (message thread)

    /// Carrega um .nam numa thread de fundo e troca no áudio sem glitch.
    void loadModelAsync (const juce::File& file);

    bool isLoadingModel() const noexcept { return loading.load(); }

    /// Nome do capture carregado ("" se nenhum).
    juce::String getModelName() const;
    /// Caminho completo do .nam carregado ("" se nenhum).
    juce::String getModelPath() const;
    /// Último erro de carregamento ("" se ok).
    juce::String getLoadError() const;
    /// Sample rate que o modelo espera (-1 se desconhecido/sem modelo).
    double getModelExpectedSampleRate() const;

    bool hasModelLoaded() const noexcept { return modelIsActive.load(); }

    juce::AudioProcessorValueTreeState apvts;

    // Picos do último bloco (lidos pelo editor; decaimento é feito lá).
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ------------------------------------------------------------------
    // Troca de modelo real-time-safe (protocolo comentado no .cpp):
    //   loader ──publica──▶ pendingModel ──consome──▶ activeModel (áudio)
    //   áudio ──aposenta──▶ retiredModel ──coleta/deleta──▶ loader/dtor
    std::unique_ptr<nam::DSP> activeModel;           // tocado SÓ pela thread de áudio
    std::atomic<nam::DSP*> pendingModel { nullptr }; // loader -> áudio
    std::atomic<nam::DSP*> retiredModel { nullptr }; // áudio -> loader/dtor
    std::atomic<bool> modelIsActive { false };

    std::atomic<double> hostSampleRate { 48000.0 };
    std::atomic<int> preparedBlockSize { 512 };

    juce::ThreadPool loaderPool { 1 };
    std::atomic<bool> loading { false };

    mutable juce::CriticalSection modelInfoLock;
    juce::String modelName, modelPath, loadError;    // sob modelInfoLock
    double modelExpectedSampleRate = -1.0;           // sob modelInfoLock

    juce::AudioBuffer<float> monoScratch;            // saída do NAM (pré-alocado)

    std::atomic<float>* pInputGain = nullptr;
    std::atomic<float>* pOutputGain = nullptr;
    std::atomic<float>* pAmpOn = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMProcessor)
};
