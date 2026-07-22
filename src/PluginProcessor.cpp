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
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "ampEco", 1 }, "Amp Eco", false));
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "autoEco", 1 }, "Auto Eco", true));

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
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "gateHold", 1 }, "Gate Hold",
        juce::NormalisableRange<float> (0.0f, 500.0f, 1.0f, 0.5f), 40.0f, ms));

    // compressor de pedal
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "compOn", 1 }, "Comp On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "compSustain", 1 }, "Comp Sustain",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f), 4.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "compAttack", 1 }, "Comp Attack",
        juce::NormalisableRange<float> (1.0f, 100.0f, 1.0f, 0.5f), 20.0f, ms));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "compBlend", 1 }, "Comp Blend",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "compLevel", 1 }, "Comp Level",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dB));

    // variações de modelo por efeito (selecionadas no cartão)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "odType", 1 }, "OD Type",
        juce::StringArray { "Screamer", "Blues", "Distortion", "Fuzz",
                            "Boost", "Heavy Fuzz" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "compType", 1 }, "Comp Type",
        juce::StringArray { "Dyna", "Optical", "Studio" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayType", 1 }, "Delay Type",
        juce::StringArray { "Digital", "Analog", "Tape", "Ping-Pong" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayDiv", 1 }, "Delay Division",
        juce::StringArray { "1/4", "1/8", "1/8.", "1/16" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "revType", 1 }, "Reverb Type",
        juce::StringArray { "Hall", "Room", "Plate", "Spring" }, 0));

    // modulações (cartão Mod)
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "modOn", 1 }, "Mod On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "modType", 1 }, "Mod Type",
        juce::StringArray { "Chorus", "Phaser", "Flanger", "Tremolo H." }, 0));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "modRate", 1 }, "Mod Rate",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.05f, 0.4f), 1.5f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "modDepth", 1 }, "Mod Depth",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "modMix", 1 }, "Mod Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    // pré-EQ (antes do NAM)
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "preEqOn", 1 }, "Pre EQ On", true));
    auto preEqRange = juce::NormalisableRange<float> (-12.0f, 12.0f, 0.5f);
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "preEqLow", 1 }, "Pre EQ Low", preEqRange, 0.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "preEqMid", 1 }, "Pre EQ Mid", preEqRange, 0.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "preEqHigh", 1 }, "Pre EQ High", preEqRange, 0.0f, dB));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { kParamCabOn, 1 }, "Cab On", true));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamCabLevel, 1 }, "Cab Level",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dB));

    auto zeroTen = juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f);
    auto pct = juce::AudioParameterFloatAttributes().withLabel ("%");

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamCabAir, 1 }, "Cab Air", zeroTen, 0.0f));

    // cabs paralelos (1..3), com blend/low cut/high cut/phase POR slot
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "cabCount", 1 }, "Cab Count", 1, maxCabSlots, 1));
    for (int s = 0; s < maxCabSlots; ++s)
    {
        const auto n = juce::String (s + 1);
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { "cab" + n + "Blend", 1 }, "Cab " + n + " Blend",
            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { "cab" + n + "LowCut", 1 }, "Cab " + n + " Low Cut",
            juce::NormalisableRange<float> (20.0f, 300.0f, 1.0f, 0.5f), 20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { "cab" + n + "HighCut", 1 }, "Cab " + n + " High Cut",
            juce::NormalisableRange<float> (2000.0f, 20000.0f, 10.0f, 0.5f), 20000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<BoolParam> (
            juce::ParameterID { "cab" + n + "Phase", 1 }, "Cab " + n + " Phase", false));
    }

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
    pAmpEco = apvts.getRawParameterValue ("ampEco");
    pAutoEco = apvts.getRawParameterValue ("autoEco");
    pAmpGain = apvts.getRawParameterValue (kParamAmpGain);
    pAmpBass = apvts.getRawParameterValue (kParamAmpBass);
    pAmpMid = apvts.getRawParameterValue (kParamAmpMid);
    pAmpTreble = apvts.getRawParameterValue (kParamAmpTreble);
    pAmpPresence = apvts.getRawParameterValue (kParamAmpPresence);
    pAmpMaster = apvts.getRawParameterValue (kParamAmpMaster);
    pGateOn = apvts.getRawParameterValue (kParamGateOn);
    pGateThresh = apvts.getRawParameterValue (kParamGateThresh);
    pGateRelease = apvts.getRawParameterValue (kParamGateRelease);
    pGateHold = apvts.getRawParameterValue ("gateHold");
    pCompOn = apvts.getRawParameterValue ("compOn");
    pCompSustain = apvts.getRawParameterValue ("compSustain");
    pCompAttack = apvts.getRawParameterValue ("compAttack");
    pCompBlend = apvts.getRawParameterValue ("compBlend");
    pCompLevel = apvts.getRawParameterValue ("compLevel");
    pOdType = apvts.getRawParameterValue ("odType");
    pCompType = apvts.getRawParameterValue ("compType");
    pDelayType = apvts.getRawParameterValue ("delayType");
    pRevType = apvts.getRawParameterValue ("revType");
    pModOn = apvts.getRawParameterValue ("modOn");
    pModType = apvts.getRawParameterValue ("modType");
    pModRate = apvts.getRawParameterValue ("modRate");
    pModDepth = apvts.getRawParameterValue ("modDepth");
    pModMix = apvts.getRawParameterValue ("modMix");
    pPreEqOn = apvts.getRawParameterValue ("preEqOn");
    pPreEqLow = apvts.getRawParameterValue ("preEqLow");
    pPreEqMid = apvts.getRawParameterValue ("preEqMid");
    pPreEqHigh = apvts.getRawParameterValue ("preEqHigh");
    pCabOn = apvts.getRawParameterValue (kParamCabOn);
    pCabLevel = apvts.getRawParameterValue (kParamCabLevel);
    pCabAir = apvts.getRawParameterValue (kParamCabAir);
    pCabCount = apvts.getRawParameterValue ("cabCount");
    for (int s = 0; s < maxCabSlots; ++s)
    {
        const auto n = juce::String (s + 1);
        pCabBlend[s] = apvts.getRawParameterValue ("cab" + n + "Blend");
        pCabLowCut[s] = apvts.getRawParameterValue ("cab" + n + "LowCut");
        pCabHighCut[s] = apvts.getRawParameterValue ("cab" + n + "HighCut");
        pCabPhase[s] = apvts.getRawParameterValue ("cab" + n + "Phase");
    }
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

    writeDefaultChain();
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
    smartGate.prepare (sampleRate);
    pedalComp.prepare (spec);
    compCachedSustain = -1.0f;
    odCachedType = delayCachedType = revCachedType = compCachedType = -1;
    preEqCachedLow = -99.0f;
    for (auto* f : { &preEqLowF, &preEqMidF, &preEqHighF })
        f->reset();
    for (auto& conv : convolutions)
        conv.prepare (spec);

    cabDryBuf.setSize (1, samplesPerBlock);
    cabAccBuf.setSize (1, samplesPerBlock);
    cabSlotBuf.setSize (1, samplesPerBlock);
    for (int s = 0; s < maxCabSlots; ++s)
    {
        cabLcCached[s] = -1.0f;
        cabLc[s].reset();
        cabHc[s].reset();
    }

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
    delayLineR.prepare (spec);
    delayLineR.setMaximumDelayInSamples ((int) (sampleRate * 1.2) + 1);
    delaySmoothedSamples.reset (sampleRate, 0.05);
    delaySmoothedSamples.setCurrentAndTargetValue ((float) (0.35 * sampleRate));

    chorusFx.prepare (spec);
    phaserFx.prepare (spec);
    modCachedType = -1;
    tremLp.setLowPass (sampleRate, 800.0, 0.707);
    tremHp.setHighPass (sampleRate, 800.0, 0.707);
    tremLp.reset();
    tremHp.reset();

    revSpringHp.setHighPass (sampleRate, 400.0, 0.707);
    revSpringLp.setLowPass (sampleRate, 5000.0, 0.707);
    revSpringHp.reset();
    revSpringLp.reset();

    wetScratchR.setSize (1, samplesPerBlock);
    stereoExtra.setSize (1, samplesPerBlock);

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

    // ---- cadeia na ordem dinâmica (reordenável pelo usuário)
    if (n <= stereoExtra.getNumSamples())
        stereoExtra.clear (0, 0, n);
    {
        const int len = juce::jlimit (0, (int) chainMaxSlots, chainLen.load());
        for (int i = 0; i < len; ++i)
        {
            switch ((ChainFx) chainOrder[i].load())
            {
                case ChainFx::gate:     processGateFx (io, n); break;
                case ChainFx::od:       processOdFx (io, n); break;
                case ChainFx::eq:       processEqFx (io, n); break;
                case ChainFx::delay:    processDelayFx (io, n); break;
                case ChainFx::reverb:   processReverbFx (io, n); break;
                case ChainFx::ampBlock: processAmpAndCabs (buffer, io, n); break;
                case ChainFx::comp:     processCompFx (io, n); break;
                case ChainFx::preEq:    processPreEqFx (io, n); break;
                case ChainFx::mod:      processModFx (io, n); break;
            }
        }
    }

    buffer.applyGain (0, 0, n, outGain);

    if (numOut >= 2)
    {
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
        // conteúdo estéreo (ping-pong/largura do reverb) entra só no canal R
        if (n <= stereoExtra.getNumSamples())
            buffer.addFrom (1, 0, stereoExtra, 0, 0, n, outGain);
    }

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
// Módulos da cadeia (chamados na ordem dinâmica; mesmas regras RT do
// processBlock — nada de alocação/locks/IO aqui)

void GuitarRigNAMProcessor::SmartGate::prepare (double sampleRate)
{
    sr = sampleRate;
    envAttackCoef = 1.0f - std::exp (-1.0f / (float) (0.0005 * sr));  // 0.5 ms
    envReleaseCoef = 1.0f - std::exp (-1.0f / (float) (0.03 * sr));   // 30 ms
    gainAttackCoef = 1.0f - std::exp (-1.0f / (float) (0.001 * sr));  // 1 ms
    reset();
}

void GuitarRigNAMProcessor::SmartGate::process (float* io, int n, float threshDb,
                                                float holdMs, float releaseMs)
{
    const float openLin = juce::Decibels::decibelsToGain (threshDb);
    const float closeLin = juce::Decibels::decibelsToGain (threshDb - 6.0f); // histerese
    const int holdSamples = (int) (holdMs / 1000.0f * (float) sr);
    const float gainReleaseCoef =
        1.0f - std::exp (-1.0f / juce::jmax (1.0f, (float) (releaseMs / 1000.0 * sr)));

    for (int i = 0; i < n; ++i)
    {
        const float rect = std::abs (io[i]);
        env += (rect > env ? envAttackCoef : envReleaseCoef) * (rect - env);

        if (env > openLin)
        {
            isOpen = true;
            holdCounter = holdSamples;
        }
        else if (isOpen && env < closeLin)
        {
            if (holdCounter > 0)
                --holdCounter;   // segura aberto durante o hold
            else
                isOpen = false;
        }

        const float target = isOpen ? 1.0f : 0.0f;
        gain += (target > gain ? gainAttackCoef : gainReleaseCoef) * (target - gain);
        io[i] *= gain;
    }
}

void GuitarRigNAMProcessor::processGateFx (float* io, int n)
{
    if (pGateOn->load() <= 0.5f)
        return;

    smartGate.process (io, n, pGateThresh->load(), pGateHold->load(), pGateRelease->load());
}

void GuitarRigNAMProcessor::processCompFx (float* io, int n)
{
    // Compressor de pedal: Sustain controla threshold+ratio+makeup juntos;
    // Blend faz compressão paralela (mistura com o sinal seco).
    if (pCompOn->load() <= 0.5f || n > monoScratch.getNumSamples())
        return;

    const float sustain = pCompSustain->load();
    const float attack = pCompAttack->load();
    const int type = (int) pCompType->load();
    if (sustain != compCachedSustain || attack != compCachedAttack || type != compCachedType)
    {
        compCachedSustain = sustain;
        compCachedAttack = attack;
        compCachedType = type;
        switch (type)
        {
            default:
            case 0: // Dyna: agressivo, estilo pedal clássico
                pedalComp.setThreshold (-10.0f - sustain * 4.0f);
                pedalComp.setRatio (2.0f + sustain * 0.8f);
                pedalComp.setAttack (attack);
                pedalComp.setRelease (180.0f);
                break;
            case 1: // Optical: lento e musical (estilo LA-2A)
                pedalComp.setThreshold (-8.0f - sustain * 3.5f);
                pedalComp.setRatio (1.5f + sustain * 0.45f);
                pedalComp.setAttack (juce::jmax (10.0f, attack));
                pedalComp.setRelease (400.0f);
                break;
            case 2: // Studio: VCA transparente
                pedalComp.setThreshold (-6.0f - sustain * 3.0f);
                pedalComp.setRatio (3.0f);
                pedalComp.setAttack (attack);
                pedalComp.setRelease (250.0f);
                break;
        }
    }

    // guarda o sinal seco para o blend
    float* dry = monoScratch.getWritePointer (0);
    juce::FloatVectorOperations::copy (dry, io, n);

    juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    pedalComp.process (ctx);

    const float makeupPerSustain = type == 1 ? 2.0f : type == 2 ? 1.6f : 2.2f;
    const float makeup = juce::Decibels::decibelsToGain (sustain * makeupPerSustain);
    const float blend = pCompBlend->load() / 100.0f;
    const float level = juce::Decibels::decibelsToGain (pCompLevel->load());
    for (int i = 0; i < n; ++i)
        io[i] = (dry[i] * (1.0f - blend) + io[i] * makeup * blend) * level;
}

void GuitarRigNAMProcessor::updatePreEqIfNeeded()
{
    const float lo = pPreEqLow->load(), mi = pPreEqMid->load(), hi = pPreEqHigh->load();
    if (lo == preEqCachedLow && mi == preEqCachedMid && hi == preEqCachedHigh)
        return;
    preEqCachedLow = lo;
    preEqCachedMid = mi;
    preEqCachedHigh = hi;
    const double sr = hostSampleRate.load();
    // vozeamento pré-amp: mexe no que ENTRA no capture (muda a saturação)
    preEqLowF.setLowShelf (sr, 100.0, lo);
    preEqMidF.setPeak (sr, 500.0, mi, 0.9);
    preEqHighF.setHighShelf (sr, 2200.0, hi);
}

void GuitarRigNAMProcessor::processPreEqFx (float* io, int n)
{
    if (pPreEqOn->load() <= 0.5f)
        return;

    updatePreEqIfNeeded();
    if (preEqCachedLow != 0.0f || preEqCachedMid != 0.0f || preEqCachedHigh != 0.0f)
        for (int i = 0; i < n; ++i)
            io[i] = preEqHighF.process (preEqMidF.process (preEqLowF.process (io[i])));
}

void GuitarRigNAMProcessor::processOdFx (float* io, int n)
{
    // HP -> clip (por variação) -> tone LP -> pós-filtro -> level
    if (pOdOn->load() <= 0.5f)
        return;

    const int type = (int) pOdType->load();
    if (type != odCachedType)
    {
        odCachedType = type;
        const double sr = hostSampleRate.load();
        // vozeamento de entrada e pós-filtro por variação
        switch (type)
        {
            default:
            case 0: // Screamer: aperta graves, corcova de médios
                odHp.setHighPass (sr, 300.0, 0.707);
                odPost.setPeak (sr, 700.0, 2.5, 0.9);
                odPostActive = true;
                break;
            case 1: // Blues: quase flat, clip suave
                odHp.setHighPass (sr, 100.0, 0.707);
                odPostActive = false;
                break;
            case 2: // Distortion: leve scoop de médios
                odHp.setHighPass (sr, 120.0, 0.707);
                odPost.setPeak (sr, 800.0, -2.0, 0.9);
                odPostActive = true;
                break;
            case 3: // Fuzz: grave cheio, clip assimétrico
                odHp.setHighPass (sr, 80.0, 0.707);
                odPostActive = false;
                break;
        }
        odHp.reset();
        odPost.reset();
    }

    updateOdIfNeeded();
    const float driveGain = juce::Decibels::decibelsToGain (pOdDrive->load() * 4.0f);
    const float levelGain = juce::Decibels::decibelsToGain ((pOdLevel->load() - 5.0f) * 3.0f - 6.0f);
    const float fuzzBiasOut = std::tanh (0.2f); // remove o DC do clip assimétrico

    for (int i = 0; i < n; ++i)
    {
        float v = odHp.process (io[i]) * driveGain;

        switch (type)
        {
            default:
            case 0: v = std::tanh (v); break;                                    // soft
            case 1: v = v / (1.0f + std::abs (v)); break;                        // mais suave
            case 2: v = juce::jlimit (-0.9f, 0.9f, std::tanh (v * 1.6f) * 1.1f); break; // duro
            case 3: v = std::tanh (v * 1.5f + 0.2f) - fuzzBiasOut; break;        // assimétrico
        }

        v = odToneLp.process (v);
        if (odPostActive)
            v = odPost.process (v);
        io[i] = v * levelGain;
    }
}

void GuitarRigNAMProcessor::processEqFx (float* io, int n)
{
    if (pEqOn->load() <= 0.5f)
        return;

    updateEqIfNeeded();
    if (eqCachedLow != 0.0f || eqCachedMid != 0.0f || eqCachedHigh != 0.0f)
        for (int i = 0; i < n; ++i)
            io[i] = eqHighF.process (eqMidF.process (eqLowF.process (io[i])));
}

void GuitarRigNAMProcessor::processDelayFx (float* io, int n)
{
    // Trails: mesmo desligado, as repetições pendentes continuam soando —
    // só a ENTRADA é cortada. Custo mínimo, comportamento de pedal moderno.
    const bool on = pDelayOn->load() > 0.5f;

    const double sr = hostSampleRate.load();
    const int type = (int) pDelayType->load();
    if (type != delayCachedType)
    {
        delayCachedType = type;
        if (type == 1) // Analog: repetições escuras e comprimidas
        {
            delayFbLp.setLowPass (sr, 3000.0, 0.707);
            delayFbHp.setHighPass (sr, 150.0, 0.707);
        }
        else if (type == 2) // Tape: um pouco mais aberto + wobble
        {
            delayFbLp.setLowPass (sr, 4500.0, 0.707);
        }
        delayFbLp.reset();
        delayFbHp.reset();
    }

    delaySmoothedSamples.setTargetValue ((float) (pDelayTime->load() / 1000.0 * sr));
    const float fb = pDelayFb->load() / 100.0f;
    const float mix = pDelayMix->load() / 100.0f;
    const double lfoInc = juce::MathConstants<double>::twoPi * 0.9 / sr; // wobble do tape
    float* extra = stereoExtra.getWritePointer (0);

    for (int i = 0; i < n; ++i)
    {
        float delaySamples = delaySmoothedSamples.getNextValue();
        if (type == 2)
        {
            delayLfoPhase += lfoInc;
            if (delayLfoPhase > juce::MathConstants<double>::twoPi)
                delayLfoPhase -= juce::MathConstants<double>::twoPi;
            delaySamples *= 1.0f + 0.0018f * (float) std::sin (delayLfoPhase);
        }
        delayLine.setDelay (delaySamples);
        const float input = on ? io[i] : 0.0f;

        if (type == 3)
        {
            // Ping-Pong: repetições alternam L/R (o R vai pro stereoExtra)
            delayLineR.setDelay (delaySamples);
            const float wetL = delayLine.popSample (0);
            const float wetR = delayLineR.popSample (0);
            delayLine.pushSample (0, input + wetR * fb);
            delayLineR.pushSample (0, wetL);
            io[i] += wetL * mix;
            extra[i] += (wetR - wetL) * mix;
            continue;
        }

        const float wet = delayLine.popSample (0);
        float fbSignal = wet;
        if (type == 1)
            fbSignal = std::tanh (delayFbLp.process (delayFbHp.process (wet)) * 1.05f);
        else if (type == 2)
            fbSignal = delayFbLp.process (wet);

        delayLine.pushSample (0, input + fbSignal * fb);
        io[i] += wet * mix;
    }
}

void GuitarRigNAMProcessor::processReverbFx (float* io, int n)
{
    // mix manual, com predelay no caminho wet; trails ao desligar; estéreo
    // real via stereoExtra (diferença R-L)
    const bool on = pRevOn->load() > 0.5f;
    if (n > wetScratch.getNumSamples() || n > wetScratchR.getNumSamples())
        return;

    const float decay = pRevDecay->load();
    const int type = (int) pRevType->load();
    if (decay != revCachedDecay || type != revCachedType)
    {
        revCachedDecay = decay;
        revCachedType = type;
        switch (type)
        {
            default:
            case 0: // Hall: grande e suave
                reverbParams.roomSize = 0.2f + decay / 10.0f * 0.75f;
                reverbParams.damping = 0.45f;
                reverbParams.width = 1.0f;
                break;
            case 1: // Room: curto e abafado
                reverbParams.roomSize = 0.1f + decay / 10.0f * 0.5f;
                reverbParams.damping = 0.6f;
                reverbParams.width = 0.7f;
                break;
            case 2: // Plate: denso e brilhante
                reverbParams.roomSize = 0.3f + decay / 10.0f * 0.65f;
                reverbParams.damping = 0.12f;
                reverbParams.width = 1.0f;
                break;
            case 3: // Spring: curto, médios "molejados" (bandpass no wet)
                reverbParams.roomSize = 0.15f + decay / 10.0f * 0.35f;
                reverbParams.damping = 0.2f;
                reverbParams.width = 0.6f;
                break;
        }
        reverb.setParameters (reverbParams);
    }

    const float mix = pRevMix->load() / 100.0f;
    const int preSamples = juce::jmin (
        preDelayLine.getMaximumDelayInSamples() - 1,
        (int) (pRevPre->load() / 1000.0 * hostSampleRate.load()));
    preDelayLine.setDelay ((float) preSamples);

    float* wet = wetScratch.getWritePointer (0);
    float* wetR = wetScratchR.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        const float d = preDelayLine.popSample (0);
        preDelayLine.pushSample (0, on ? io[i] : 0.0f); // trails: corta só a entrada
        float v = d;
        if (type == 3) // spring: bandpass dá o timbre "mola"
            v = revSpringLp.process (revSpringHp.process (v));
        wet[i] = v;
        wetR[i] = v;
    }
    reverb.processStereo (wet, wetR, n);

    float* extra = stereoExtra.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        io[i] += wet[i] * mix;
        extra[i] += (wetR[i] - wet[i]) * mix;
    }
}

void GuitarRigNAMProcessor::processModFx (float* io, int n)
{
    if (pModOn->load() <= 0.5f)
        return;

    const int type = (int) pModType->load();
    const float rate = pModRate->load();
    const float depth = pModDepth->load() / 100.0f;
    const float mix = pModMix->load() / 100.0f;

    if (type != modCachedType || rate != modCachedRate
        || depth != modCachedDepth || mix != modCachedMix)
    {
        modCachedType = type;
        modCachedRate = rate;
        modCachedDepth = depth;
        modCachedMix = mix;
        switch (type)
        {
            case 0: // Chorus
                chorusFx.setCentreDelay (7.0f);
                chorusFx.setFeedback (0.0f);
                chorusFx.setRate (rate);
                chorusFx.setDepth (depth);
                chorusFx.setMix (mix);
                break;
            case 2: // Flanger = chorus com delay curto + feedback
                chorusFx.setCentreDelay (1.8f);
                chorusFx.setFeedback (0.7f);
                chorusFx.setRate (rate);
                chorusFx.setDepth (depth);
                chorusFx.setMix (mix);
                break;
            case 1: // Phaser
                phaserFx.setRate (rate);
                phaserFx.setDepth (depth);
                phaserFx.setMix (mix);
                phaserFx.setCentreFrequency (900.0f);
                phaserFx.setFeedback (0.5f);
                break;
            default: break; // tremolo não usa juce::dsp
        }
    }

    if (type == 0 || type == 2)
    {
        juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        chorusFx.process (ctx);
    }
    else if (type == 1)
    {
        juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        phaserFx.process (ctx);
    }
    else // Tremolo harmônico: graves e agudos tremulam em fases opostas
    {
        const double inc = juce::MathConstants<double>::twoPi * rate / hostSampleRate.load();
        for (int i = 0; i < n; ++i)
        {
            tremPhase += inc;
            if (tremPhase > juce::MathConstants<double>::twoPi)
                tremPhase -= juce::MathConstants<double>::twoPi;
            const float lfo = (float) std::sin (tremPhase) * depth;
            const float lo = tremLp.process (io[i]) * (1.0f + lfo) * 0.5f;
            const float hi = tremHp.process (io[i]) * (1.0f - lfo) * 0.5f;
            const float wet = lo + hi;
            io[i] = io[i] * (1.0f - mix) + wet * mix * 2.0f;
        }
    }
}

void GuitarRigNAMProcessor::processAmpAndCabs (juce::AudioBuffer<float>& buffer, float* io, int n)
{
    // ---- amp NAM (com resampler quando o SR do capture difere do host):
    //      GAIN -> modelo -> tone stack (B/M/T/Pres) -> MASTER
    if (activeModel != nullptr && pAmpOn->load() > 0.5f)
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

    // ---- cabs em paralelo (até 3 IRs -> mixer de blend por slot)
    if (pCabOn->load() > 0.5f && n <= cabDryBuf.getNumSamples())
    {
        const int count = juce::jlimit (1, (int) maxCabSlots, (int) pCabCount->load());

        juce::FloatVectorOperations::copy (cabDryBuf.getWritePointer (0), io, n);
        cabAccBuf.clear (0, 0, n);

        for (int s = 0; s < count; ++s)
        {
            float* slot = cabSlotBuf.getWritePointer (0);
            juce::FloatVectorOperations::copy (slot, cabDryBuf.getReadPointer (0), n);

            if (irLoadedFlags[s].load() && convolutions[s].getCurrentIRSize() > 0)
            {
                juce::dsp::AudioBlock<float> block (&slot, 1, (size_t) n);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                convolutions[s].process (ctx);
            }

            updateCabSlotFilters (s);
            const bool lcOn = pCabLowCut[s]->load() > 22.0f;
            const bool hcOn = pCabHighCut[s]->load() < 19000.0f;
            if (lcOn || hcOn)
                for (int i = 0; i < n; ++i)
                {
                    float v = slot[i];
                    if (lcOn) v = cabLc[s].process (v);
                    if (hcOn) v = cabHc[s].process (v);
                    slot[i] = v;
                }

            // blend por slot (+ inversão de fase)
            const float g = (pCabBlend[s]->load() / 100.0f)
                            * (pCabPhase[s]->load() > 0.5f ? -1.0f : 1.0f);
            if (g != 0.0f)
                juce::FloatVectorOperations::addWithMultiply (
                    cabAccBuf.getWritePointer (0), slot, g, n);
        }

        juce::FloatVectorOperations::copy (io, cabAccBuf.getReadPointer (0), n);
        buffer.applyGain (0, 0, n, juce::Decibels::decibelsToGain (pCabLevel->load()));

        // AIR: shelf de agudos pós-mix
        if (pCabAir->load() > 0.05f)
        {
            updateAirIfNeeded();
            for (int i = 0; i < n; ++i)
                io[i] = airF.process (io[i]);
        }
    }
}

//==============================================================================
// Ordem da cadeia

juce::String GuitarRigNAMProcessor::fxToString (ChainFx fx)
{
    switch (fx)
    {
        case ChainFx::gate:     return "gate";
        case ChainFx::od:       return "od";
        case ChainFx::eq:       return "eq";
        case ChainFx::delay:    return "delay";
        case ChainFx::reverb:   return "reverb";
        case ChainFx::ampBlock: return "amp";
        case ChainFx::comp:     return "comp";
        case ChainFx::preEq:    return "preeq";
        case ChainFx::mod:      return "mod";
    }
    return "amp";
}

int GuitarRigNAMProcessor::fxFromString (const juce::String& id)
{
    for (int f = 0; f < numChainFx; ++f)
        if (fxToString ((ChainFx) f) == id)
            return f;
    return -1;
}

void GuitarRigNAMProcessor::writeDefaultChain()
{
    const ChainFx def[] = { ChainFx::gate, ChainFx::comp, ChainFx::od, ChainFx::preEq,
                            ChainFx::ampBlock, ChainFx::eq, ChainFx::mod,
                            ChainFx::delay, ChainFx::reverb };
    for (int i = 0; i < (int) std::size (def); ++i)
        chainOrder[i].store ((int) def[i]);
    chainLen.store ((int) std::size (def));
}

juce::StringArray GuitarRigNAMProcessor::getChainOrder() const
{
    juce::StringArray out;
    const int len = juce::jlimit (0, (int) chainMaxSlots, chainLen.load());
    for (int i = 0; i < len; ++i)
        out.add (fxToString ((ChainFx) chainOrder[i].load()));
    return out;
}

void GuitarRigNAMProcessor::setChainOrder (const juce::StringArray& ids)
{
    // Normaliza: ids conhecidos, cada um no máximo 1x; efeitos ausentes são
    // inseridos em posições sensatas (migração de presets antigos) e "amp"
    // garante presença (âncora).
    juce::Array<int> order;
    bool used[numChainFx] = {};

    for (const auto& id : ids)
    {
        const int f = fxFromString (id);
        if (f >= 0 && ! used[f] && order.size() < chainMaxSlots)
        {
            used[f] = true;
            order.add (f);
        }
    }

    auto insertAt = [&order] (int fx, int index)
    {
        order.insert (juce::jlimit (0, order.size(), index), fx);
    };

    if (! used[(int) ChainFx::ampBlock])
        insertAt ((int) ChainFx::ampBlock, order.size() / 2);
    // comp entra depois do gate (ou no início); preEq logo antes do amp
    if (! used[(int) ChainFx::comp])
        insertAt ((int) ChainFx::comp, order.indexOf ((int) ChainFx::gate) + 1);
    if (! used[(int) ChainFx::preEq])
        insertAt ((int) ChainFx::preEq, order.indexOf ((int) ChainFx::ampBlock));
    // mod entra antes do delay (ou depois do amp)
    if (! used[(int) ChainFx::mod])
    {
        const int delayIdx = order.indexOf ((int) ChainFx::delay);
        insertAt ((int) ChainFx::mod, delayIdx >= 0
                                          ? delayIdx
                                          : order.indexOf ((int) ChainFx::ampBlock) + 1);
    }
    used[(int) ChainFx::ampBlock] = used[(int) ChainFx::comp] = true;
    used[(int) ChainFx::preEq] = used[(int) ChainFx::mod] = true;

    for (int f = 0; f < numChainFx; ++f)
        if (! used[f] && order.size() < chainMaxSlots)
            order.add (f);

    const int len = juce::jmin (order.size(), (int) chainMaxSlots);
    for (int i = 0; i < len; ++i)
        chainOrder[i].store (order[i]);
    chainLen.store (len);
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

        // Arquitetura (badge V1/V2): sidecar .meta do TONE3000 tem prioridade;
        // sem ele, lemos o campo "architecture" do próprio .nam.
        juce::String archLabel;
        {
            const auto meta = juce::JSON::parse (
                juce::File (file.getFullPathName() + ".meta").loadFileAsString());
            const auto metaArch = meta.getProperty ("arch", "").toString();
            if (metaArch == "2")
                archLabel = "V2";
            else if (metaArch == "1")
                archLabel = "V1";
            else
            {
                const auto namJson = juce::JSON::parse (file.loadFileAsString());
                const auto arch = namJson.getProperty ("architecture", "").toString();
                if (arch.isNotEmpty())
                    archLabel = arch.containsIgnoreCase ("slimmable") ? "V2" : "V1";
            }
        }

        {
            const juce::ScopedLock sl (modelInfoLock);
            modelName = file.getFileNameWithoutExtension();
            modelPath = file.getFullPathName();
            modelExpectedSampleRate = lm->modelSampleRate;
            modelArchLabel = archLabel;
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

void GuitarRigNAMProcessor::setModelPair (const juce::File& normal, const juce::File& eco)
{
    {
        const juce::ScopedLock sl (modelInfoLock);
        modelPathStd = normal.getFullPathName();
        modelPathEco = eco.existsAsFile() ? eco.getFullPathName() : juce::String();
    }

    const bool wantEco = pAmpEco->load() > 0.5f && eco.existsAsFile();
    loadModelAsync (wantEco ? eco : normal);
}

juce::String GuitarRigNAMProcessor::getModelPathNormal() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return modelPathStd;
}

juce::String GuitarRigNAMProcessor::getModelPathEco() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return modelPathEco;
}

juce::String GuitarRigNAMProcessor::getModelArchLabel() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return modelArchLabel;
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
void GuitarRigNAMProcessor::loadIrAsync (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= maxCabSlots || ! file.existsAsFile())
        return;

    // O Convolution carrega em background e troca RT-safe internamente.
    convolutions[slot].loadImpulseResponse (file,
                                            juce::dsp::Convolution::Stereo::no,
                                            juce::dsp::Convolution::Trim::yes,
                                            0,
                                            juce::dsp::Convolution::Normalise::yes);
    {
        const juce::ScopedLock sl (modelInfoLock);
        irNames[slot] = file.getFileNameWithoutExtension();
        irPaths[slot] = file.getFullPathName();
    }
    irLoadedFlags[slot].store (true);
}

juce::String GuitarRigNAMProcessor::getIrName (int slot) const
{
    if (slot < 0 || slot >= maxCabSlots)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return irNames[slot];
}

juce::String GuitarRigNAMProcessor::getIrPath (int slot) const
{
    if (slot < 0 || slot >= maxCabSlots)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return irPaths[slot];
}

int GuitarRigNAMProcessor::getCabCount() const
{
    return juce::jlimit (1, (int) maxCabSlots, (int) pCabCount->load());
}

int GuitarRigNAMProcessor::firstFreeIrSlot() const
{
    const int count = getCabCount();
    for (int s = 0; s < count; ++s)
        if (! irLoadedFlags[s].load())
            return s;
    return -1;
}

bool GuitarRigNAMProcessor::isIrFileLoaded (const juce::String& fullPath) const
{
    const int count = getCabCount();
    const juce::ScopedLock sl (modelInfoLock);
    for (int s = 0; s < count; ++s)
        if (irPaths[s] == fullPath)
            return true;
    return false;
}

void GuitarRigNAMProcessor::updateCabSlotFilters (int slot)
{
    const float lc = pCabLowCut[slot]->load();
    const float hc = pCabHighCut[slot]->load();
    if (lc == cabLcCached[slot] && hc == cabHcCached[slot])
        return;
    cabLcCached[slot] = lc;
    cabHcCached[slot] = hc;
    const double sr = hostSampleRate.load();
    cabLc[slot].setHighPass (sr, lc, 0.707);
    cabHc[slot].setLowPass (sr, hc, 0.707);
}

//==============================================================================
juce::ValueTree GuitarRigNAMProcessor::captureState()
{
    auto state = apvts.copyState();
    state.setProperty (kStateModelPath, getModelPath(), nullptr);
    state.setProperty ("modelPathStd", getModelPathNormal(), nullptr);
    state.setProperty ("modelPathEco", getModelPathEco(), nullptr);
    for (int s = 0; s < maxCabSlots; ++s)
        state.setProperty ("irPath" + juce::String (s + 1), getIrPath (s), nullptr);
    state.setProperty ("chainOrder", getChainOrder().joinIntoString (","), nullptr);
    state.setProperty (kStatePresetName, getCurrentPresetName(), nullptr);
    return state;
}

void GuitarRigNAMProcessor::applyState (juce::ValueTree state)
{
    if (! state.isValid())
        return;

    apvts.replaceState (state);

    // par ECO: formato novo tem modelPathStd/Eco; legado só modelPath
    const juce::File modelFile (state.getProperty (kStateModelPath, "").toString());
    const juce::File stdFile (state.getProperty ("modelPathStd",
                                                 modelFile.getFullPathName()).toString());
    const juce::File ecoFile (state.getProperty ("modelPathEco", "").toString());
    {
        const juce::ScopedLock sl (modelInfoLock);
        modelPathStd = stdFile.existsAsFile() ? stdFile.getFullPathName() : juce::String();
        modelPathEco = ecoFile.existsAsFile() ? ecoFile.getFullPathName() : juce::String();
    }
    if (modelFile.existsAsFile())
        loadModelAsync (modelFile);

    for (int s = 0; s < maxCabSlots; ++s)
    {
        // "irPath" sem número = formato antigo (slot único) -> slot 1
        const auto key = s == 0 && ! state.hasProperty ("irPath1")
                             ? juce::String (kStateIrPath)
                             : "irPath" + juce::String (s + 1);
        const juce::File irFile (state.getProperty (key, "").toString());
        if (irFile.existsAsFile())
            loadIrAsync (s, irFile);
    }

    setChainOrder (juce::StringArray::fromTokens (
        state.getProperty ("chainOrder", "gate,od,amp,eq,delay,reverb").toString(), ",", ""));

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
