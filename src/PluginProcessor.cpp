#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>

// O LanczosResampler.h do AudioDSPTools usa iplug::PI mas não inclui o iPlug2
// (é herdado do plugin oficial, que é iPlug2). Fora desse contexto, a
// constante precisa ser fornecida antes do include.
namespace iplug
{
inline constexpr double PI = 3.14159265358979323846;
}

// O header usa DEFAULT_BLOCK_SIZE como argumento default de Reset() sem
// defini-lo; sempre passamos o valor explicitamente, mas o símbolo precisa
// existir para compilar.
#ifndef DEFAULT_BLOCK_SIZE
  #define DEFAULT_BLOCK_SIZE 512
#endif

#include <Dependencies/AudioDSPTools/dsp/ResamplingContainer/ResamplingContainer.h>

#include <filesystem>

namespace
{
constexpr auto* kParamInputGain = "inputGain";
constexpr auto* kParamOutputGain = "outputGain";
constexpr auto* kParamAmpOn = "ampOn";
constexpr auto* kParamAmpGain = "ampGain";
constexpr auto* kParamAmpBass = "ampBass";
constexpr auto* kParamAmpMid = "ampMid";
constexpr auto* kParamAmpTreble = "ampTreble";
constexpr auto* kParamAmpPresence = "ampPresence";
constexpr auto* kParamAmpMaster = "ampMaster";
constexpr auto* kParamOdOn = "odOn";
constexpr auto* kParamOdDrive = "odDrive";
constexpr auto* kParamOdTone = "odTone";
constexpr auto* kParamOdLevel = "odLevel";
constexpr auto* kParamCabAir = "cabAir";
constexpr auto* kParamEqOn = "eqOn";
constexpr auto* kParamEqLow = "eqLow";
constexpr auto* kParamEqMid = "eqMid";
constexpr auto* kParamEqHigh = "eqHigh";
constexpr auto* kParamDelayOn = "delayOn";
constexpr auto* kParamDelayTime = "delayTime";
constexpr auto* kParamDelayFb = "delayFb";
constexpr auto* kParamDelayMix = "delayMix";
constexpr auto* kParamRevOn = "revOn";
constexpr auto* kParamRevDecay = "revDecay";
constexpr auto* kParamRevMix = "revMix";
constexpr auto* kParamRevPre = "revPre";
constexpr auto* kParamGateOn = "gateOn";
constexpr auto* kParamGateThresh = "gateThresh";
constexpr auto* kParamGateRelease = "gateRelease";
constexpr auto* kParamCabOn = "cabOn";
constexpr auto* kParamCabLevel = "cabLevel";
constexpr auto* kStateModelPath = "modelPath";
constexpr auto* kStateIrPath = "irPath";
constexpr auto* kStatePresetName = "presetName";
} // namespace

GuitarRigNAMProcessor::LoadedModel::~LoadedModel() = default;

//==============================================================================
// Biquads RBJ (Audio EQ Cookbook), S=1 nos shelves.

void GuitarRigNAMProcessor::Biquad::setLowShelf (double sr, double freq, double dbGain)
{
    const double A = std::pow (10.0, dbGain / 40.0);
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / 2.0 * std::sqrt (2.0);
    const double s2a = 2.0 * std::sqrt (A) * alpha;

    const double a0 = (A + 1) + (A - 1) * c + s2a;
    b0 = (float) (A * ((A + 1) - (A - 1) * c + s2a) / a0);
    b1 = (float) (2 * A * ((A - 1) - (A + 1) * c) / a0);
    b2 = (float) (A * ((A + 1) - (A - 1) * c - s2a) / a0);
    a1 = (float) (-2 * ((A - 1) + (A + 1) * c) / a0);
    a2 = (float) (((A + 1) + (A - 1) * c - s2a) / a0);
}

void GuitarRigNAMProcessor::Biquad::setHighShelf (double sr, double freq, double dbGain)
{
    const double A = std::pow (10.0, dbGain / 40.0);
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / 2.0 * std::sqrt (2.0);
    const double s2a = 2.0 * std::sqrt (A) * alpha;

    const double a0 = (A + 1) - (A - 1) * c + s2a;
    b0 = (float) (A * ((A + 1) + (A - 1) * c + s2a) / a0);
    b1 = (float) (-2 * A * ((A - 1) + (A + 1) * c) / a0);
    b2 = (float) (A * ((A + 1) + (A - 1) * c - s2a) / a0);
    a1 = (float) (2 * ((A - 1) - (A + 1) * c) / a0);
    a2 = (float) (((A + 1) - (A - 1) * c - s2a) / a0);
}

void GuitarRigNAMProcessor::Biquad::setLowPass (double sr, double freq, double q)
{
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / (2.0 * q);

    const double a0 = 1 + alpha;
    b0 = (float) (((1 - c) / 2) / a0);
    b1 = (float) ((1 - c) / a0);
    b2 = (float) (((1 - c) / 2) / a0);
    a1 = (float) (-2 * c / a0);
    a2 = (float) ((1 - alpha) / a0);
}

void GuitarRigNAMProcessor::Biquad::setHighPass (double sr, double freq, double q)
{
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / (2.0 * q);

    const double a0 = 1 + alpha;
    b0 = (float) (((1 + c) / 2) / a0);
    b1 = (float) (-(1 + c) / a0);
    b2 = (float) (((1 + c) / 2) / a0);
    a1 = (float) (-2 * c / a0);
    a2 = (float) ((1 - alpha) / a0);
}

void GuitarRigNAMProcessor::Biquad::setPeak (double sr, double freq, double dbGain, double q)
{
    const double A = std::pow (10.0, dbGain / 40.0);
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / (2.0 * q);

    const double a0 = 1 + alpha / A;
    b0 = (float) ((1 + alpha * A) / a0);
    b1 = (float) (-2 * c / a0);
    b2 = (float) ((1 - alpha * A) / a0);
    a1 = (float) (-2 * c / a0);
    a2 = (float) ((1 - alpha / A) / a0);
}

void GuitarRigNAMProcessor::updateOdIfNeeded()
{
    const float tone = pOdTone->load();
    if (tone == odCachedTone)
        return;
    odCachedTone = tone;
    // tone 0..10 -> LP de 1 kHz a 8 kHz (exponencial)
    odToneLp.setLowPass (hostSampleRate.load(), 1000.0 * std::pow (8.0, tone / 10.0), 0.707);
}

void GuitarRigNAMProcessor::updateEqIfNeeded()
{
    const float lo = pEqLow->load(), mi = pEqMid->load(), hi = pEqHigh->load();
    if (lo == eqCachedLow && mi == eqCachedMid && hi == eqCachedHigh)
        return;
    eqCachedLow = lo;
    eqCachedMid = mi;
    eqCachedHigh = hi;
    const double sr = hostSampleRate.load();
    eqLowF.setLowShelf (sr, 120.0, lo);
    eqMidF.setPeak (sr, 800.0, mi, 0.8);
    eqHighF.setHighShelf (sr, 4000.0, hi);
}

void GuitarRigNAMProcessor::updateAirIfNeeded()
{
    const float air = pCabAir->load();
    if (air == airCached)
        return;
    airCached = air;
    // 0..10 -> shelf de 0 a +9 dB em 8 kHz
    airF.setHighShelf (hostSampleRate.load(), 8000.0, air * 0.9);
}

void GuitarRigNAMProcessor::readTunerBlock (float* dest, int numSamples) const
{
    const int writePos = tunerWritePos.load();
    int start = (writePos - numSamples) & (tunerRingSize - 1);
    for (int i = 0; i < numSamples; ++i)
    {
        dest[i] = tunerRing[start];
        start = (start + 1) & (tunerRingSize - 1);
    }
}

void GuitarRigNAMProcessor::updateToneStackIfNeeded()
{
    const float bass = pAmpBass->load();
    const float mid = pAmpMid->load();
    const float treble = pAmpTreble->load();
    const float pres = pAmpPresence->load();

    if (bass == tsCachedBass && mid == tsCachedMid
        && treble == tsCachedTreble && pres == tsCachedPresence)
        return;

    tsCachedBass = bass;
    tsCachedMid = mid;
    tsCachedTreble = treble;
    tsCachedPresence = pres;

    const double sr = hostSampleRate.load();
    // 5 = neutro; curso de ±12 dB (±9 dB no presence).
    tsBass.setLowShelf (sr, 150.0, (bass - 5.0) * 2.4);
    tsMid.setPeak (sr, 500.0, (mid - 5.0) * 2.4, 0.7);
    tsTreble.setHighShelf (sr, 1800.0, (treble - 5.0) * 2.4);
    tsPresence.setHighShelf (sr, 4500.0, (pres - 5.0) * 1.8);
}

juce::AudioProcessorValueTreeState::ParameterLayout GuitarRigNAMProcessor::createParameterLayout()
{
    using FloatParam = juce::AudioParameterFloat;
    using BoolParam = juce::AudioParameterBool;
    auto dB = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto ms = juce::AudioParameterFloatAttributes().withLabel ("ms");

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamInputGain, 1 }, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamOutputGain, 1 }, "Output Level",
        juce::NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f, dB));
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamAmpOn, 1 }, "Amp On", true));

    // Painel do amp: GAIN empurra o sinal para dentro do capture (como o
    // gain do amp real); tone stack + presence pós-modelo; MASTER na saída
    // da seção do amp.
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamAmpGain, 1 }, "Amp Gain",
        juce::NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f, dB));
    auto zeroToTen = juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f);
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamAmpBass, 1 }, "Bass", zeroToTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamAmpMid, 1 }, "Mid", zeroToTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamAmpTreble, 1 }, "Treble", zeroToTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamAmpPresence, 1 }, "Presence", zeroToTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamAmpMaster, 1 }, "Amp Master",
        juce::NormalisableRange<float> (-20.0f, 10.0f, 0.1f), 0.0f, dB));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamGateOn, 1 }, "Gate On", true));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamGateThresh, 1 }, "Gate Threshold",
        juce::NormalisableRange<float> (-90.0f, -20.0f, 1.0f), -70.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamGateRelease, 1 }, "Gate Release",
        juce::NormalisableRange<float> (10.0f, 500.0f, 1.0f, 0.5f), 100.0f, ms));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamCabOn, 1 }, "Cab On", true));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamCabLevel, 1 }, "Cab Level",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dB));

    auto zeroTen = juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f);
    auto pct = juce::AudioParameterFloatAttributes().withLabel ("%");

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamCabAir, 1 }, "Cab Air", zeroTen, 0.0f));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamOdOn, 1 }, "OD On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamOdDrive, 1 }, "OD Drive", zeroTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamOdTone, 1 }, "OD Tone", zeroTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamOdLevel, 1 }, "OD Level", zeroTen, 5.0f));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamEqOn, 1 }, "EQ On", true));
    auto eqRange = juce::NormalisableRange<float> (-12.0f, 12.0f, 0.5f);
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamEqLow, 1 }, "EQ Low", eqRange, 0.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamEqMid, 1 }, "EQ Mid", eqRange, 0.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamEqHigh, 1 }, "EQ High", eqRange, 0.0f, dB));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamDelayOn, 1 }, "Delay On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamDelayTime, 1 }, "Delay Time",
        juce::NormalisableRange<float> (60.0f, 1000.0f, 1.0f, 0.5f), 350.0f, ms));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamDelayFb, 1 }, "Delay Feedback",
        juce::NormalisableRange<float> (0.0f, 90.0f, 1.0f), 35.0f, pct));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamDelayMix, 1 }, "Delay Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f, pct));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamRevOn, 1 }, "Reverb On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamRevDecay, 1 }, "Reverb Decay", zeroTen, 4.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamRevMix, 1 }, "Reverb Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f, pct));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamRevPre, 1 }, "Reverb Predelay",
        juce::NormalisableRange<float> (0.0f, 120.0f, 1.0f), 20.0f, ms));

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
    pAmpGain = apvts.getRawParameterValue (kParamAmpGain);
    pAmpBass = apvts.getRawParameterValue (kParamAmpBass);
    pAmpMid = apvts.getRawParameterValue (kParamAmpMid);
    pAmpTreble = apvts.getRawParameterValue (kParamAmpTreble);
    pAmpPresence = apvts.getRawParameterValue (kParamAmpPresence);
    pAmpMaster = apvts.getRawParameterValue (kParamAmpMaster);
    pGateOn = apvts.getRawParameterValue (kParamGateOn);
    pGateThresh = apvts.getRawParameterValue (kParamGateThresh);
    pGateRelease = apvts.getRawParameterValue (kParamGateRelease);
    pCabOn = apvts.getRawParameterValue (kParamCabOn);
    pCabLevel = apvts.getRawParameterValue (kParamCabLevel);
    pCabAir = apvts.getRawParameterValue (kParamCabAir);
    pOdOn = apvts.getRawParameterValue (kParamOdOn);
    pOdDrive = apvts.getRawParameterValue (kParamOdDrive);
    pOdTone = apvts.getRawParameterValue (kParamOdTone);
    pOdLevel = apvts.getRawParameterValue (kParamOdLevel);
    pEqOn = apvts.getRawParameterValue (kParamEqOn);
    pEqLow = apvts.getRawParameterValue (kParamEqLow);
    pEqMid = apvts.getRawParameterValue (kParamEqMid);
    pEqHigh = apvts.getRawParameterValue (kParamEqHigh);
    pDelayOn = apvts.getRawParameterValue (kParamDelayOn);
    pDelayTime = apvts.getRawParameterValue (kParamDelayTime);
    pDelayFb = apvts.getRawParameterValue (kParamDelayFb);
    pDelayMix = apvts.getRawParameterValue (kParamDelayMix);
    pRevOn = apvts.getRawParameterValue (kParamRevOn);
    pRevDecay = apvts.getRawParameterValue (kParamRevDecay);
    pRevMix = apvts.getRawParameterValue (kParamRevMix);
    pRevPre = apvts.getRawParameterValue (kParamRevPre);

    noiseGate.setRatio (10.0f);
    noiseGate.setAttack (5.0f);
}

GuitarRigNAMProcessor::~GuitarRigNAMProcessor()
{
    loaderPool.removeAllJobs (true, 5000);
    delete pendingModel.exchange (nullptr);
    delete retiredModel.exchange (nullptr);
}

void GuitarRigNAMProcessor::prepareLoadedModel (LoadedModel& lm, double hostRate, int blockSize) const
{
    const bool needsResample = lm.modelSampleRate > 0.0
                               && std::abs (lm.modelSampleRate - hostRate) > 1.0;

    if (needsResample)
    {
        // O NAM roda no SR do capture; o container faz host <-> capture.
        const int innerBlock = (int) std::ceil ((double) blockSize
                                                * lm.modelSampleRate / hostRate) + 8;
        lm.model->Reset (lm.modelSampleRate, innerBlock);

        lm.resampler = std::make_unique<Resampler> (lm.modelSampleRate);
        lm.resampler->Reset (hostRate, blockSize);
        lm.latencySamples = lm.resampler->GetLatency();
    }
    else
    {
        lm.model->Reset (hostRate, blockSize);
        lm.resampler.reset();
        lm.latencySamples = 0;
    }

    // Pré-construída para que o processBlock nunca crie std::function.
    auto* rawModel = lm.model.get();
    lm.func = [rawModel] (float** in, float** out, int n) { rawModel->process (in, out, n); };
}

void GuitarRigNAMProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate.store (sampleRate);
    preparedBlockSize.store (samplesPerBlock);
    monoScratch.setSize (1, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    noiseGate.prepare (spec);
    convolution.prepare (spec);

    // Força o recálculo dos filtros no novo sample rate e zera os estados.
    tsCachedBass = -1.0f;
    odCachedTone = -1.0f;
    eqCachedLow = -99.0f;
    airCached = -1.0f;
    revCachedDecay = -1.0f;
    for (auto* f : { &tsBass, &tsMid, &tsTreble, &tsPresence, &odHp, &odToneLp,
                     &eqLowF, &eqMidF, &eqHighF, &airF })
        f->reset();
    odHp.setHighPass (sampleRate, 120.0, 0.707);

    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples ((int) (sampleRate * 1.2) + 1);
    delaySmoothedSamples.reset (sampleRate, 0.05);
    delaySmoothedSamples.setCurrentAndTargetValue ((float) (0.35 * sampleRate));

    preDelayLine.prepare (spec);
    preDelayLine.setMaximumDelayInSamples ((int) (sampleRate * 0.15) + 1);

    reverb.setSampleRate (sampleRate);
    reverbParams.dryLevel = 0.0f;   // mix manual (com predelay no caminho wet)
    reverbParams.wetLevel = 1.0f;
    reverbParams.damping = 0.45f;
    reverbParams.width = 1.0f;
    reverb.setParameters (reverbParams);

    wetScratch.setSize (1, samplesPerBlock);

    // prepareToPlay não é concorrente com processBlock; pode alocar/tocar nos
    // modelos ativo e pendente (o loader não toca no pendente após publicar).
    if (activeModel != nullptr)
        prepareLoadedModel (*activeModel, sampleRate, samplesPerBlock);

    if (auto* p = pendingModel.load())
        prepareLoadedModel (*p, sampleRate, samplesPerBlock);

    if (activeModel != nullptr)
        setLatencySamples (activeModel->latencySamples);
}

void GuitarRigNAMProcessor::releaseResources()
{
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
// locks, fazer I/O, logar ou chamar rede. Trocas de modelo/IR usam atomics ou
// os mecanismos RT-safe internos do JUCE (Convolution).
void GuitarRigNAMProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto ticksStart = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (auto* p = pendingModel.exchange (nullptr))
    {
        retiredModel.store (activeModel.release());
        activeModel.reset (p);
        modelIsActive.store (true);
        resamplingActive.store (activeModel->resampler != nullptr);
    }

    const float inGain = juce::Decibels::decibelsToGain (pInputGain->load());
    const float outGain = juce::Decibels::decibelsToGain (pOutputGain->load());
    const float cabGain = juce::Decibels::decibelsToGain (pCabLevel->load());
    const bool ampOn = pAmpOn->load() > 0.5f;
    const bool gateOn = pGateOn->load() > 0.5f;
    const bool cabOn = pCabOn->load() > 0.5f;

    // Cadeia mono: somar as entradas no canal 0 (a guitarra pode estar em
    // qualquer entrada da interface); para fonte única a soma é transparente.
    for (int ch = 1; ch < numIn; ++ch)
        buffer.addFrom (0, 0, buffer, ch, 0, n);

    buffer.applyGain (0, 0, n, inGain);
    inputPeak.store (buffer.getMagnitude (0, 0, n));

    float* io = buffer.getWritePointer (0);

    // ---- tap do afinador (sinal cru pós-ganho, pré-gate)
    {
        int w = tunerWritePos.load();
        for (int i = 0; i < n; ++i)
        {
            tunerRing[w] = io[i];
            w = (w + 1) & (tunerRingSize - 1);
        }
        tunerWritePos.store (w);
    }

    // ---- noise gate (antes do amp, como num pedalboard)
    if (gateOn)
    {
        noiseGate.setThreshold (pGateThresh->load());
        noiseGate.setRelease (pGateRelease->load());

        juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        noiseGate.process (ctx);
    }

    // ---- overdrive (pré-amp): HP 120 Hz -> drive/tanh -> tone LP -> level
    if (pOdOn->load() > 0.5f)
    {
        updateOdIfNeeded();
        const float driveGain = juce::Decibels::decibelsToGain (pOdDrive->load() * 4.0f);
        const float levelGain = juce::Decibels::decibelsToGain ((pOdLevel->load() - 5.0f) * 3.0f - 6.0f);
        for (int i = 0; i < n; ++i)
        {
            float v = odHp.process (io[i]);
            v = std::tanh (v * driveGain);
            v = odToneLp.process (v);
            io[i] = v * levelGain;
        }
    }

    // ---- amp NAM (com resampler quando o SR do capture difere do host):
    //      GAIN -> modelo -> tone stack (B/M/T/Pres) -> MASTER
    if (activeModel != nullptr && ampOn)
    {
        buffer.applyGain (0, 0, n, juce::Decibels::decibelsToGain (pAmpGain->load()));

        float* scratch = monoScratch.getWritePointer (0);
        const int maxChunk = monoScratch.getNumSamples();

        // Blocos maiores que o preparado (raros) são processados em pedaços,
        // mantendo o contrato de tamanho máximo do Reset — nos dois caminhos.
        for (int pos = 0; pos < n; pos += maxChunk)
        {
            const int len = juce::jmin (maxChunk, n - pos);
            float* in = io + pos;

            if (activeModel->resampler != nullptr)
                activeModel->resampler->ProcessBlock (&in, &scratch, len, activeModel->func);
            else
                activeModel->model->process (&in, &scratch, len);

            juce::FloatVectorOperations::copy (io + pos, scratch, len);
        }

        updateToneStackIfNeeded();
        for (int i = 0; i < n; ++i)
            io[i] = tsPresence.process (tsTreble.process (tsMid.process (tsBass.process (io[i]))));

        buffer.applyGain (0, 0, n, juce::Decibels::decibelsToGain (pAmpMaster->load()));
    }

    // ---- cab IR (convolução; troca de IR é RT-safe dentro do Convolution)
    if (cabOn)
    {
        if (irIsLoaded.load() && convolution.getCurrentIRSize() > 0)
        {
            juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            convolution.process (ctx);
            buffer.applyGain (0, 0, n, cabGain);
        }

        // AIR: shelf de agudos pós-IR
        if (pCabAir->load() > 0.05f)
        {
            updateAirIfNeeded();
            for (int i = 0; i < n; ++i)
                io[i] = airF.process (io[i]);
        }
    }

    // ---- EQ pós-cab (LOW/MID/HIGH)
    if (pEqOn->load() > 0.5f)
    {
        updateEqIfNeeded();
        if (eqCachedLow != 0.0f || eqCachedMid != 0.0f || eqCachedHigh != 0.0f)
            for (int i = 0; i < n; ++i)
                io[i] = eqHighF.process (eqMidF.process (eqLowF.process (io[i])));
    }

    // ---- delay
    if (pDelayOn->load() > 0.5f)
    {
        const double sr = hostSampleRate.load();
        delaySmoothedSamples.setTargetValue ((float) (pDelayTime->load() / 1000.0 * sr));
        const float fb = pDelayFb->load() / 100.0f;
        const float mix = pDelayMix->load() / 100.0f;

        for (int i = 0; i < n; ++i)
        {
            delayLine.setDelay (delaySmoothedSamples.getNextValue());
            const float wet = delayLine.popSample (0);
            delayLine.pushSample (0, io[i] + wet * fb);
            io[i] += wet * mix;
        }
    }

    // ---- reverb (mix manual, com predelay no caminho wet)
    if (pRevOn->load() > 0.5f && n <= wetScratch.getNumSamples())
    {
        const float decay = pRevDecay->load();
        if (decay != revCachedDecay)
        {
            revCachedDecay = decay;
            reverbParams.roomSize = 0.2f + decay / 10.0f * 0.75f;
            reverb.setParameters (reverbParams);
        }

        const float mix = pRevMix->load() / 100.0f;
        const int preSamples = juce::jmin (
            preDelayLine.getMaximumDelayInSamples() - 1,
            (int) (pRevPre->load() / 1000.0 * hostSampleRate.load()));
        preDelayLine.setDelay ((float) preSamples);

        float* wet = wetScratch.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float d = preDelayLine.popSample (0);
            preDelayLine.pushSample (0, io[i]);
            wet[i] = d;
        }
        reverb.processMono (wet, n);
        for (int i = 0; i < n; ++i)
            io[i] += wet[i] * mix;
    }

    buffer.applyGain (0, 0, n, outGain);

    if (numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    outputPeak.store (buffer.getMagnitude (0, 0, n));

    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    // ---- medidor de CPU (fração do tempo de bloco, suavizado)
    {
        const double elapsed = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - ticksStart);
        const double blockDur = n / juce::jmax (1.0, hostSampleRate.load());
        const float load = (float) juce::jlimit (0.0, 1.0, elapsed / blockDur);
        cpuLoad.store (cpuLoad.load() * 0.9f + load * 0.1f);
    }
}

//==============================================================================
void GuitarRigNAMProcessor::loadModelAsync (const juce::File& file)
{
    loading.store (true);

    loaderPool.addJob ([this, file]
    {
        auto lm = std::make_unique<LoadedModel>();
        juce::String error;

        try
        {
            const auto path = std::filesystem::u8path (file.getFullPathName().toRawUTF8());
            lm->model = nam::get_dsp (path);
        }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8 (e.what());
        }
        catch (...)
        {
            error = "Falha desconhecida ao carregar o modelo";
        }

        if (lm->model != nullptr
            && (lm->model->NumInputChannels() != 1 || lm->model->NumOutputChannels() != 1))
        {
            error = "Somente captures mono (1 in / 1 out) sao suportados";
            lm->model = nullptr;
        }

        if (lm->model != nullptr)
        {
            lm->modelSampleRate = lm->model->GetExpectedSampleRate();
            try
            {
                prepareLoadedModel (*lm, hostSampleRate.load(), preparedBlockSize.load());
            }
            catch (const std::exception& e)
            {
                error = juce::String::fromUTF8 (e.what());
                lm->model = nullptr;
            }
        }

        if (lm->model == nullptr)
        {
            const juce::ScopedLock sl (modelInfoLock);
            loadError = error.isNotEmpty() ? error : "Arquivo .nam invalido";
            loading.store (false);
            return;
        }

        {
            const juce::ScopedLock sl (modelInfoLock);
            modelName = file.getFileNameWithoutExtension();
            modelPath = file.getFullPathName();
            modelExpectedSampleRate = lm->modelSampleRate;
            loadError.clear();
        }

        const int latency = lm->latencySamples;

        delete retiredModel.exchange (nullptr);
        delete pendingModel.exchange (lm.release());

        juce::MessageManager::callAsync ([this, latency]
        {
            setLatencySamples (latency);
        });

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
void GuitarRigNAMProcessor::loadIrAsync (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    // O Convolution carrega em background e troca RT-safe internamente.
    convolution.loadImpulseResponse (file,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     0,
                                     juce::dsp::Convolution::Normalise::yes);
    {
        const juce::ScopedLock sl (modelInfoLock);
        irName = file.getFileNameWithoutExtension();
        irPath = file.getFullPathName();
    }
    irIsLoaded.store (true);
}

juce::String GuitarRigNAMProcessor::getIrName() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return irName;
}

juce::String GuitarRigNAMProcessor::getIrPath() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return irPath;
}

//==============================================================================
juce::ValueTree GuitarRigNAMProcessor::captureState()
{
    auto state = apvts.copyState();
    state.setProperty (kStateModelPath, getModelPath(), nullptr);
    state.setProperty (kStateIrPath, getIrPath(), nullptr);
    state.setProperty (kStatePresetName, getCurrentPresetName(), nullptr);
    return state;
}

void GuitarRigNAMProcessor::applyState (juce::ValueTree state)
{
    if (! state.isValid())
        return;

    apvts.replaceState (state);

    const juce::File modelFile (state.getProperty (kStateModelPath, "").toString());
    if (modelFile.existsAsFile())
        loadModelAsync (modelFile);

    const juce::File irFile (state.getProperty (kStateIrPath, "").toString());
    if (irFile.existsAsFile())
        loadIrAsync (irFile);

    setCurrentPresetName (state.getProperty (kStatePresetName, "").toString());
}

void GuitarRigNAMProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GuitarRigNAMProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::File GuitarRigNAMProcessor::getPresetsDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("GuitarRig NAM")
                   .getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

// Presets de fábrica: só parâmetros (sem capture/IR — mantêm o que estiver
// carregado). Criados uma vez, quando a pasta está vazia.
void GuitarRigNAMProcessor::createFactoryPresetsIfNeeded() const
{
    auto dir = getPresetsDirectory();
    if (! dir.findChildFiles (juce::File::findFiles, false, "*.xml").isEmpty())
        return;

    auto make = [&] (const juce::String& name,
                     std::initializer_list<std::pair<const char*, float>> tweaks)
    {
        juce::ValueTree tree ("GuitarRigNAM");
        for (auto* p : getParameters())
        {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                float value = rp->convertFrom0to1 (rp->getDefaultValue());
                for (const auto& [id, v] : tweaks)
                    if (rp->paramID == id)
                        value = v;

                juce::ValueTree param ("PARAM");
                param.setProperty ("id", rp->paramID, nullptr);
                param.setProperty ("value", value, nullptr);
                tree.addChild (param, -1, nullptr);
            }
        }
        tree.setProperty ("presetName", name, nullptr);
        if (auto xml = tree.createXml())
            xml->writeTo (dir.getChildFile (name + ".xml"));
    };

    make ("Clean", { { "ampGain", -3.0f }, { "ampTreble", 6.0f }, { "gateThresh", -80.0f },
                     { "revOn", 1.0f }, { "revMix", 22.0f }, { "revDecay", 5.0f } });
    make ("Crunch", { { "odOn", 1.0f }, { "odDrive", 4.0f }, { "odLevel", 6.0f },
                      { "ampGain", 2.0f }, { "ampMid", 6.0f } });
    make ("Lead", { { "odOn", 1.0f }, { "odDrive", 7.0f }, { "ampGain", 3.0f },
                    { "delayOn", 1.0f }, { "delayTime", 380.0f }, { "delayMix", 22.0f },
                    { "revOn", 1.0f }, { "revMix", 15.0f } });
    make ("Metal", { { "gateThresh", -55.0f }, { "gateRelease", 60.0f }, { "ampGain", 4.0f },
                     { "ampBass", 6.5f }, { "ampMid", 3.5f }, { "ampTreble", 6.5f },
                     { "ampPresence", 6.0f } });
}

juce::Array<juce::File> GuitarRigNAMProcessor::getPresetFiles() const
{
    createFactoryPresetsIfNeeded();
    auto files = getPresetsDirectory().findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();
    return files;
}

juce::int64 GuitarRigNAMProcessor::stateFingerprint()
{
    auto state = captureState();
    state.removeProperty ("tunerOn", nullptr); // preferência de UI, não suja o preset
    return state.toXmlString().hashCode64();
}

bool GuitarRigNAMProcessor::isPresetDirty()
{
    if (getCurrentPresetName().isEmpty() || baselinePending.load())
        return false;
    return stateFingerprint() != savedFingerprint;
}

void GuitarRigNAMProcessor::settlePresetBaseline()
{
    if (baselinePending.load() && ! isLoadingModel())
    {
        savedFingerprint = stateFingerprint();
        baselinePending.store (false);
    }
}

void GuitarRigNAMProcessor::savePreset (const juce::File& file)
{
    setCurrentPresetName (file.getFileNameWithoutExtension());

    if (auto xml = captureState().createXml())
        xml->writeTo (file);

    savedFingerprint = stateFingerprint();
    baselinePending.store (false);
}

void GuitarRigNAMProcessor::loadPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        applyState (juce::ValueTree::fromXml (*xml));
        setCurrentPresetName (file.getFileNameWithoutExtension());
        // a baseline consolida quando o load assíncrono de modelo/IR terminar
        baselinePending.store (true);
    }
}

void GuitarRigNAMProcessor::loadAdjacentPreset (int delta)
{
    const auto files = getPresetFiles();
    if (files.isEmpty())
        return;

    const auto current = getCurrentPresetName();
    int index = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files.getReference (i).getFileNameWithoutExtension() == current)
        {
            index = i;
            break;
        }

    const int next = index < 0 ? 0 : (index + delta + files.size()) % files.size();
    loadPreset (files.getReference (next));
}

juce::String GuitarRigNAMProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return currentPresetName;
}

void GuitarRigNAMProcessor::setCurrentPresetName (const juce::String& name)
{
    const juce::ScopedLock sl (modelInfoLock);
    currentPresetName = name;
}

//==============================================================================
juce::AudioProcessorEditor* GuitarRigNAMProcessor::createEditor()
{
    return new GuitarRigNAMEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarRigNAMProcessor();
}
