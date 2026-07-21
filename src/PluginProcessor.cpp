#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>

#include <filesystem>

namespace
{
constexpr auto* kParamInputGain = "inputGain";
constexpr auto* kParamOutputGain = "outputGain";
constexpr auto* kParamAmpOn = "ampOn";
constexpr auto* kStateModelPath = "modelPath";
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout GuitarRigNAMProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamInputGain, 1 }, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kParamOutputGain, 1 }, "Output Level",
        juce::NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kParamAmpOn, 1 }, "Amp On", true));

    return layout;
}

GuitarRigNAMProcessor::GuitarRigNAMProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GuitarRigNAM", createParameterLayout())
{
    pInputGain = apvts.getRawParameterValue (kParamInputGain);
    pOutputGain = apvts.getRawParameterValue (kParamOutputGain);
    pAmpOn = apvts.getRawParameterValue (kParamAmpOn);
}

GuitarRigNAMProcessor::~GuitarRigNAMProcessor()
{
    loaderPool.removeAllJobs (true, 5000);
    delete pendingModel.exchange (nullptr);
    delete retiredModel.exchange (nullptr);
}

void GuitarRigNAMProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate.store (sampleRate);
    preparedBlockSize.store (samplesPerBlock);
    monoScratch.setSize (1, samplesPerBlock);

    // prepareToPlay não é concorrente com processBlock, então é seguro tocar
    // no modelo ativo aqui. Reset() faz prewarm — caro, mas permitido fora do
    // caminho real-time.
    if (activeModel != nullptr)
        activeModel->Reset (sampleRate, samplesPerBlock);

    // Um modelo já publicado mas ainda não consumido foi preparado com o SR
    // antigo; o loader não toca mais nele depois de publicar, então podemos
    // prepará-lo de novo aqui.
    if (auto* p = pendingModel.load())
        p->Reset (sampleRate, samplesPerBlock);
}

void GuitarRigNAMProcessor::releaseResources()
{
    // Fora do caminho real-time: bom momento para coletar um modelo aposentado.
    delete retiredModel.exchange (nullptr);
}

bool GuitarRigNAMProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::mono()) return true;
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo()) return true;
    if (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo()) return true;

    return false;
}

// REGRA INEGOCIÁVEL: dentro de processBlock é PROIBIDO alocar memória, usar
// locks, fazer I/O, logar ou chamar rede. A troca de modelo abaixo usa apenas
// atomics; delete acontece nas outras threads.
void GuitarRigNAMProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Consumir modelo recém-carregado, aposentando o anterior. Só há um
    // pendente por vez (o loader garante que retiredModel está vazio antes
    // de publicar), então o store abaixo nunca sobrescreve um aposentado.
    if (auto* p = pendingModel.exchange (nullptr))
    {
        retiredModel.store (activeModel.release());
        activeModel.reset (p);
        modelIsActive.store (true);
    }

    const float inGain = juce::Decibels::decibelsToGain (pInputGain->load());
    const float outGain = juce::Decibels::decibelsToGain (pOutputGain->load());
    const bool ampOn = pAmpOn->load() > 0.5f;

    // Cadeia mono: somar as entradas no canal 0 — a guitarra pode estar em
    // qualquer entrada da interface (ex.: instrumento na entrada 2). Para
    // fonte única a soma é transparente.
    for (int ch = 1; ch < numIn; ++ch)
        buffer.addFrom (0, 0, buffer, ch, 0, n);

    buffer.applyGain (0, 0, n, inGain);
    inputPeak.store (buffer.getMagnitude (0, 0, n));

    if (activeModel != nullptr && ampOn)
    {
        float* io = buffer.getWritePointer (0);
        float* scratch = monoScratch.getWritePointer (0);
        const int maxChunk = monoScratch.getNumSamples();

        // O host pode, raramente, mandar blocos maiores que o preparado;
        // processar em pedaços mantém o contrato do Reset(maxBufferSize).
        for (int pos = 0; pos < n; pos += maxChunk)
        {
            const int len = juce::jmin (maxChunk, n - pos);
            float* in = io + pos;
            activeModel->process (&in, &scratch, len);
            juce::FloatVectorOperations::copy (io + pos, scratch, len);
        }
    }

    buffer.applyGain (0, 0, n, outGain);

    if (numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    outputPeak.store (buffer.getMagnitude (0, 0, n));

    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
}

//==============================================================================
void GuitarRigNAMProcessor::loadModelAsync (const juce::File& file)
{
    loading.store (true);

    loaderPool.addJob ([this, file]
    {
        std::unique_ptr<nam::DSP> model;
        juce::String error;

        try
        {
            const auto path = std::filesystem::u8path (file.getFullPathName().toRawUTF8());
            model = nam::get_dsp (path);
        }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8 (e.what());
        }
        catch (...)
        {
            error = "Falha desconhecida ao carregar o modelo";
        }

        if (model != nullptr && (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1))
        {
            error = "Somente captures mono (1 in / 1 out) sao suportados";
            model = nullptr;
        }

        if (model == nullptr)
        {
            const juce::ScopedLock sl (modelInfoLock);
            loadError = error.isNotEmpty() ? error : "Arquivo .nam invalido";
            loading.store (false);
            return;
        }

        // Prepara (incl. prewarm) ANTES de publicar — a thread de áudio recebe
        // o modelo pronto para uso.
        model->Reset (hostSampleRate.load(), preparedBlockSize.load());

        {
            const juce::ScopedLock sl (modelInfoLock);
            modelName = file.getFileNameWithoutExtension();
            modelPath = file.getFullPathName();
            modelExpectedSampleRate = model->GetExpectedSampleRate();
            loadError.clear();
        }

        // Coletar um aposentado antigo garante a invariante de "no máximo um
        // aposentado por vez" antes de publicar o novo modelo.
        delete retiredModel.exchange (nullptr);
        delete pendingModel.exchange (model.release()); // descarta pendente não consumido

        loading.store (false);
    });
}

juce::String GuitarRigNAMProcessor::getModelName() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return modelName;
}

juce::String GuitarRigNAMProcessor::getModelPath() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return modelPath;
}

juce::String GuitarRigNAMProcessor::getLoadError() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return loadError;
}

double GuitarRigNAMProcessor::getModelExpectedSampleRate() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return modelExpectedSampleRate;
}

//==============================================================================
void GuitarRigNAMProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kStateModelPath, getModelPath(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GuitarRigNAMProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid())
            return;

        apvts.replaceState (state);

        const juce::File modelFile (state.getProperty (kStateModelPath, "").toString());
        if (modelFile.existsAsFile())
            loadModelAsync (modelFile);
    }
}

juce::AudioProcessorEditor* GuitarRigNAMProcessor::createEditor()
{
    return new GuitarRigNAMEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarRigNAMProcessor();
}
