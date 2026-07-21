#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <NAM/get_dsp.h>

GuitarRigNAMProcessor::GuitarRigNAMProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Smoke test da Fase 1: prova que o NAM Core compila e linka (a chamada é
    // externa, então não pode ser eliminada pelo compilador mesmo em Release).
    // Chamada fora da thread de áudio; o carregamento real de modelos é a Fase 2.
    [[maybe_unused]] const auto namSupport =
        nam::is_version_supported (nam::LATEST_FULLY_SUPPORTED_NAM_FILE_VERSION);
    jassert (namSupport == nam::Supported::YES);
}

void GuitarRigNAMProcessor::prepareToPlay (double, int)
{
    // Nada a preparar na Fase 0 (passthrough).
}

void GuitarRigNAMProcessor::releaseResources()
{
}

bool GuitarRigNAMProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    // Aceitos: mono->mono, stereo->stereo, mono->stereo.
    if (in == juce::AudioChannelSet::mono()   && out == juce::AudioChannelSet::mono())   return true;
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo()) return true;
    if (in == juce::AudioChannelSet::mono()   && out == juce::AudioChannelSet::stereo()) return true;

    return false;
}

// REGRA INEGOCIÁVEL (vale para todo o projeto, desta fase em diante):
// dentro de processBlock é PROIBIDO alocar memória, usar locks, fazer I/O,
// logar ou chamar rede. Este callback roda na thread de áudio em tempo real;
// qualquer operação de duração não determinística causa glitches/dropouts.
void GuitarRigNAMProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Passthrough: os canais de entrada já estão no buffer compartilhado.
    // Caso mono->stereo: duplicar o canal 0 no canal 1.
    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    // Limpar canais de saída excedentes que não receberam sinal.
    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* GuitarRigNAMProcessor::createEditor()
{
    return new GuitarRigNAMEditor (*this);
}

void GuitarRigNAMProcessor::getStateInformation (juce::MemoryBlock&)
{
    // Sem estado na Fase 0.
}

void GuitarRigNAMProcessor::setStateInformation (const void*, int)
{
    // Sem estado na Fase 0.
}

// Fábrica exigida pelos wrappers de plugin do JUCE.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarRigNAMProcessor();
}
