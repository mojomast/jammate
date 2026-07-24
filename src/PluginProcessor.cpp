#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <NAM/dsp.h>
#include <NAM/get_dsp.h>

// AudioDSPTools' LanczosResampler.h uses iplug::PI but doesn't include iPlug2
// (it's inherited from the official plugin, which is iPlug2). Outside that
// context, the constant must be provided before the include.
namespace iplug
{
inline constexpr double PI = 3.14159265358979323846;
}

// The header uses DEFAULT_BLOCK_SIZE as a default argument to Reset() without
// defining it; we always pass the value explicitly, but the symbol needs to
// exist to compile.
#ifndef DEFAULT_BLOCK_SIZE
  #define DEFAULT_BLOCK_SIZE 512
#endif

#include <Dependencies/AudioDSPTools/dsp/ResamplingContainer/ResamplingContainer.h>

#include <filesystem>

namespace
{
// Migration for the "GuitarRig NAM" -> "PedalForge NAM" rename: runs at module
// load (before the standalone reads settings) and moves the old user data
// folder and settings file, if the new ones don't exist yet.
bool migrateOldAppData()
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto oldDocs = docs.getChildFile ("GuitarRig NAM");
    const auto newDocs = docs.getChildFile ("PedalForge NAM");
    if (oldDocs.isDirectory() && ! newDocs.exists())
        oldDocs.moveFileTo (newDocs);

    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto oldSettings = appData.getChildFile ("GuitarRig NAM")
                                 .getChildFile ("GuitarRig NAM.settings");
    const auto newSettingsDir = appData.getChildFile ("PedalForge NAM");
    const auto newSettings = newSettingsDir.getChildFile ("PedalForge NAM.settings");
    if (oldSettings.existsAsFile() && ! newSettings.existsAsFile())
    {
        newSettingsDir.createDirectory();
        oldSettings.copyFileTo (newSettings);
    }
    return true;
}
const bool dataMigrated = migrateOldAppData();

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

GuitarRigNAMProcessor::LoadedModel* GuitarRigNAMProcessor::unloadSentinel()
{
    static LoadedModel s;   // model == nullptr; never owned, never deleted
    return &s;
}

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

void GuitarRigNAMProcessor::Biquad::setBandPass (double sr, double freq, double q)
{
    // RBJ bandpass (ganho de pico constante = Q)
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const double c = std::cos (w), s = std::sin (w);
    const double alpha = s / (2.0 * q);

    const double a0 = 1 + alpha;
    b0 = (float) ((q * alpha) / a0);
    b1 = 0.0f;
    b2 = (float) ((-q * alpha) / a0);
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

void GuitarRigNAMProcessor::updateToneStackIfNeeded (int lane)
{
    const float bass = pAmpBass[lane]->load();
    const float mid = pAmpMid[lane]->load();
    const float treble = pAmpTreble[lane]->load();
    const float pres = pAmpPresence[lane]->load();

    if (bass == tsCachedBass[lane] && mid == tsCachedMid[lane]
        && treble == tsCachedTreble[lane] && pres == tsCachedPresence[lane])
        return;

    tsCachedBass[lane] = bass;
    tsCachedMid[lane] = mid;
    tsCachedTreble[lane] = treble;
    tsCachedPresence[lane] = pres;

    const double sr = hostSampleRate.load();
    // 5 = neutral; range of +/-12 dB (+/-9 dB on presence).
    tsBass[lane].setLowShelf (sr, 150.0, (bass - 5.0) * 2.4);
    tsMid[lane].setPeak (sr, 500.0, (mid - 5.0) * 2.4, 0.7);
    tsTreble[lane].setHighShelf (sr, 1800.0, (treble - 5.0) * 2.4);
    tsPresence[lane].setHighShelf (sr, 4500.0, (pres - 5.0) * 1.8);
}

juce::AudioProcessorValueTreeState::ParameterLayout GuitarRigNAMProcessor::createParameterLayout()
{
    using FloatParam = juce::AudioParameterFloat;
    using BoolParam = juce::AudioParameterBool;
    auto dB = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto ms = juce::AudioParameterFloatAttributes().withLabel ("ms");
    auto pct = juce::AudioParameterFloatAttributes().withLabel ("%");

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

    // Amp panel PER LANE (up to 3 rigs in parallel): GAIN pushes the signal
    // into the capture; tone stack + presence post-model; MASTER at the lane
    // output. Lane 1 keeps the legacy ids.
    auto zeroToTen = juce::NormalisableRange<float> (0.0f, 10.0f, 0.1f);
    for (int r = 0; r < 3; ++r)
    {
        const auto prefix = r == 0 ? juce::String ("amp") : "amp" + juce::String (r + 1);
        const auto label = r == 0 ? juce::String ("Amp ") : "Amp " + juce::String (r + 1) + " ";
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { prefix + "Gain", 1 }, label + "Gain",
            juce::NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f, dB));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { prefix + "Bass", 1 }, label + "Bass", zeroToTen, 5.0f));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { prefix + "Mid", 1 }, label + "Mid", zeroToTen, 5.0f));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { prefix + "Treble", 1 }, label + "Treble", zeroToTen, 5.0f));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { prefix + "Presence", 1 }, label + "Presence", zeroToTen, 5.0f));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { prefix + "Master", 1 }, label + "Master",
            juce::NormalisableRange<float> (-20.0f, 10.0f, 0.1f), 0.0f, dB));
    }

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

    // pedal compressor
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

    // per-effect model variations (selected on the card) - inspirations
    // and study sources in docs/EFEITOS.md; new options always go at the
    // END of the list (preserves indices saved in old presets)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "odType", 1 }, "OD Type",
        juce::StringArray { "Screamer", "Blues", "Distortion", "Fuzz",
                            "Boost", "Heavy Fuzz", "Valve", "Metal" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "compType", 1 }, "Comp Type",
        juce::StringArray { "Dyna", "Optical", "Studio", "Squeezer" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayType", 1 }, "Delay Type",
        juce::StringArray { "Digital", "Analog", "Tape", "Ping-Pong", "Ducking" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayDiv", 1 }, "Delay Division",
        juce::StringArray { "1/4", "1/8", "1/8.", "1/16" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "revType", 1 }, "Reverb Type",
        juce::StringArray { "Hall", "Room", "Plate", "Spring", "Shimmer" }, 0));

    // pitch/octaver (Pitch card)
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "pitchOn", 1 }, "Pitch On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "pitchType", 1 }, "Pitch Type",
        juce::StringArray { juce::String::fromUTF8 ("Octave \xe2\x86\x93"),
                            juce::String::fromUTF8 ("Octave \xe2\x86\x91"),
                            "Fifth", "Detune", "Fourth" }, 0));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "pitchMix", 1 }, "Pitch Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "pitchLevel", 1 }, "Pitch Level",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dB));

    // looper (LED = playback monitoring; loop level in the mix)
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "looperOn", 1 }, "Looper On", true));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "looperLevel", 1 }, "Looper Level",
        juce::NormalisableRange<float> (-20.0f, 6.0f, 0.1f), 0.0f, dB));

    // ---- P4 cards (each effect with its own card and controls) ----
    auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz");

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "wahOn", 1 }, "Wah On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "wahMode", 1 }, "Wah Mode",
        juce::StringArray { "Auto", "Manual", "LFO" }, 0));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "wahFreq", 1 }, "Wah Freq",
        juce::NormalisableRange<float> (200.0f, 1600.0f, 1.0f, 0.5f), 500.0f, hz));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "wahRange", 1 }, "Wah Range",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f, pct));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "wahRes", 1 }, "Wah Res", zeroToTen, 6.0f));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "sgOn", 1 }, "Slow Gear On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "sgSens", 1 }, "Slow Gear Sens", zeroToTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "sgRise", 1 }, "Slow Gear Rise",
        juce::NormalisableRange<float> (50.0f, 2000.0f, 1.0f, 0.5f), 400.0f, ms));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "octOn", 1 }, "Octaver On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "octSub", 1 }, "Octaver Sub",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, pct));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "octDirect", 1 }, "Octaver Direct",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "octTone", 1 }, "Octaver Tone",
        juce::NormalisableRange<float> (200.0f, 2000.0f, 1.0f, 0.5f), 700.0f, hz));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "rmOn", 1 }, "Ring Mod On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "rmFreq", 1 }, "Ring Mod Freq",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.4f), 220.0f, hz));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "rmMix", 1 }, "Ring Mod Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "bcOn", 1 }, "Bitcrush On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "bcBits", 1 }, "Bitcrush Bits",
        juce::NormalisableRange<float> (4.0f, 16.0f, 1.0f), 12.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "bcRate", 1 }, "Bitcrush Rate",
        juce::NormalisableRange<float> (1000.0f, 48000.0f, 10.0f, 0.4f), 48000.0f, hz));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "bcMix", 1 }, "Bitcrush Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "harmOn", 1 }, "Harmonizer On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "harmKey", 1 }, "Harmonizer Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F",
                            "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "harmScale", 1 }, "Harmonizer Scale",
        juce::StringArray { "Major", "Minor" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "harmInterval", 1 }, "Harmonizer Interval",
        juce::StringArray { "3rd", "5th", "6th", "Octave" }, 0));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "harmMix", 1 }, "Harmonizer Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "harmLevel", 1 }, "Harmonizer Level",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dB));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "excOn", 1 }, "Exciter On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "excFreq", 1 }, "Exciter Freq",
        juce::NormalisableRange<float> (2000.0f, 8000.0f, 10.0f, 0.6f), 3500.0f, hz));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "excAmt", 1 }, "Exciter Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f, pct));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "dsOn", 1 }, "De-esser On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "dsFreq", 1 }, "De-esser Freq",
        juce::NormalisableRange<float> (2000.0f, 9000.0f, 10.0f, 0.6f), 5000.0f, hz));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "dsSens", 1 }, "De-esser Sens", zeroToTen, 5.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "dsAmt", 1 }, "De-esser Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, pct));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "tapeOn", 1 }, "Tape On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "tapeDrive", 1 }, "Tape Drive", zeroToTen, 4.0f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "tapeBump", 1 }, "Tape Bump",
        juce::NormalisableRange<float> (0.0f, 6.0f, 0.1f), 2.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "tapeRoll", 1 }, "Tape Rolloff",
        juce::NormalisableRange<float> (3000.0f, 16000.0f, 10.0f, 0.5f), 9000.0f, hz));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "cnsOn", 1 }, "Console On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "cnsAmt", 1 }, "Console Glue", zeroToTen, 4.0f));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "anOn", 1 }, "Analyzer On", true));

    // external VST3 plugin slots (slot 1 keeps the legacy ids)
    for (int s = 0; s < 8; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        const auto label = s == 0 ? juce::String ("Ext Plugin ")
                                  : "Ext Plugin " + juce::String (s + 1) + " ";
        layout.add (std::make_unique<BoolParam> (
            juce::ParameterID { prefix + "On", 1 }, label + "On", true));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { prefix + "Mix", 1 }, label + "Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));
    }

    // output limiter (brickwall)
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "limOn", 1 }, "Limiter On", false));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "limCeiling", 1 }, "Limiter Ceiling",
        juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f, dB));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { "limRelease", 1 }, "Limiter Release",
        juce::NormalisableRange<float> (10.0f, 500.0f, 1.0f, 0.5f), 100.0f, ms));

    // modulations (Mod card)
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { "modOn", 1 }, "Mod On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "modType", 1 }, "Mod Type",
        juce::StringArray { "Chorus", "Phaser", "Flanger", "Tremolo H.",
                            "Vibrato", "Rotary" }, 0));
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

    // pre-EQ (before the NAM)
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

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { kParamCabAir, 1 }, "Cab Air", zeroTen, 0.0f));

    // parallel rigs (1..3 AMP+CAB pairs) - "cabCount" keeps the legacy id
    // but now counts RIGS; Mixer blend + low/high cut/phase PER lane
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "cabCount", 1 }, "Rig Count", 1, maxRigs, 1));
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
    // internal drum kit samples (before audio starts)
    drumEngine.loadEmbeddedSamples();

    pInputGain = apvts.getRawParameterValue (kParamInputGain);
    pOutputGain = apvts.getRawParameterValue (kParamOutputGain);
    pAmpOn = apvts.getRawParameterValue (kParamAmpOn);
    pAmpEco = apvts.getRawParameterValue ("ampEco");
    pAutoEco = apvts.getRawParameterValue ("autoEco");
    for (int r = 0; r < maxRigs; ++r)
    {
        const auto prefix = r == 0 ? juce::String ("amp") : "amp" + juce::String (r + 1);
        pAmpGain[r] = apvts.getRawParameterValue (prefix + "Gain");
        pAmpBass[r] = apvts.getRawParameterValue (prefix + "Bass");
        pAmpMid[r] = apvts.getRawParameterValue (prefix + "Mid");
        pAmpTreble[r] = apvts.getRawParameterValue (prefix + "Treble");
        pAmpPresence[r] = apvts.getRawParameterValue (prefix + "Presence");
        pAmpMaster[r] = apvts.getRawParameterValue (prefix + "Master");
    }
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
    pPitchOn = apvts.getRawParameterValue ("pitchOn");
    pPitchType = apvts.getRawParameterValue ("pitchType");
    pPitchMix = apvts.getRawParameterValue ("pitchMix");
    pPitchLevel = apvts.getRawParameterValue ("pitchLevel");
    pLooperOn = apvts.getRawParameterValue ("looperOn");
    pLooperLevel = apvts.getRawParameterValue ("looperLevel");
    pLimOn = apvts.getRawParameterValue ("limOn");
    pLimCeiling = apvts.getRawParameterValue ("limCeiling");
    pLimRelease = apvts.getRawParameterValue ("limRelease");
    for (int s = 0; s < maxExtSlots; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        pExtOn[s] = apvts.getRawParameterValue (prefix + "On");
        pExtMix[s] = apvts.getRawParameterValue (prefix + "Mix");
    }

    pWahOn = apvts.getRawParameterValue ("wahOn");
    pWahMode = apvts.getRawParameterValue ("wahMode");
    pWahFreq = apvts.getRawParameterValue ("wahFreq");
    pWahRange = apvts.getRawParameterValue ("wahRange");
    pWahRes = apvts.getRawParameterValue ("wahRes");
    pSgOn = apvts.getRawParameterValue ("sgOn");
    pSgSens = apvts.getRawParameterValue ("sgSens");
    pSgRise = apvts.getRawParameterValue ("sgRise");
    pOctOn = apvts.getRawParameterValue ("octOn");
    pOctSub = apvts.getRawParameterValue ("octSub");
    pOctDirect = apvts.getRawParameterValue ("octDirect");
    pOctTone = apvts.getRawParameterValue ("octTone");
    pRmOn = apvts.getRawParameterValue ("rmOn");
    pRmFreq = apvts.getRawParameterValue ("rmFreq");
    pRmMix = apvts.getRawParameterValue ("rmMix");
    pBcOn = apvts.getRawParameterValue ("bcOn");
    pBcBits = apvts.getRawParameterValue ("bcBits");
    pBcRate = apvts.getRawParameterValue ("bcRate");
    pBcMix = apvts.getRawParameterValue ("bcMix");
    pHarmOn = apvts.getRawParameterValue ("harmOn");
    pHarmKey = apvts.getRawParameterValue ("harmKey");
    pHarmScale = apvts.getRawParameterValue ("harmScale");
    pHarmInterval = apvts.getRawParameterValue ("harmInterval");
    pHarmMix = apvts.getRawParameterValue ("harmMix");
    pHarmLevel = apvts.getRawParameterValue ("harmLevel");
    pExcOn = apvts.getRawParameterValue ("excOn");
    pExcFreq = apvts.getRawParameterValue ("excFreq");
    pExcAmt = apvts.getRawParameterValue ("excAmt");
    pDsOn = apvts.getRawParameterValue ("dsOn");
    pDsFreq = apvts.getRawParameterValue ("dsFreq");
    pDsSens = apvts.getRawParameterValue ("dsSens");
    pDsAmt = apvts.getRawParameterValue ("dsAmt");
    pTapeOn = apvts.getRawParameterValue ("tapeOn");
    pTapeDrive = apvts.getRawParameterValue ("tapeDrive");
    pTapeBump = apvts.getRawParameterValue ("tapeBump");
    pTapeRoll = apvts.getRawParameterValue ("tapeRoll");
    pCnsOn = apvts.getRawParameterValue ("cnsOn");
    pCnsAmt = apvts.getRawParameterValue ("cnsAmt");
    pAnOn = apvts.getRawParameterValue ("anOn");

    extFormatManager.addFormat (new juce::VST3PluginFormat());
    recThread.startThread();
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
    for (int r = 0; r < maxRigs; ++r)
    {
        if (auto* q = pendingModels[r].exchange (nullptr); q != unloadSentinel())
            delete q;
        if (auto* q = retiredModels[r].exchange (nullptr); q != unloadSentinel())
            delete q;
    }
    for (int s = 0; s < maxExtSlots; ++s)
    {
        delete extPending[s].exchange (nullptr);
        delete extRetired[s].exchange (nullptr);
        extActive[s].reset();
    }
    delete drumPending.exchange (nullptr);
    delete drumRetired.exchange (nullptr);
    drumActive.reset();

    recActive.store (nullptr);
    recActiveGtr.store (nullptr);
    recActiveDrm.store (nullptr);
    recWriter.reset();
    recWriterGtr.reset();
    recWriterDrm.reset();
    recThread.stopThread (2000);
}

void GuitarRigNAMProcessor::prepareLoadedModel (LoadedModel& lm, double hostRate, int blockSize) const
{
    const bool needsResample = lm.modelSampleRate > 0.0
                               && std::abs (lm.modelSampleRate - hostRate) > 1.0;

    if (needsResample)
    {
        // The NAM runs at the capture SR; the container does host <-> capture.
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

    // Pre-built so processBlock never creates a std::function.
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

    // Forces filter recompute at the new sample rate and clears state.
    for (int r = 0; r < maxRigs; ++r)
    {
        tsCachedBass[r] = -1.0f;
        tsBass[r].reset();
        tsMid[r].reset();
        tsTreble[r].reset();
        tsPresence[r].reset();
    }
    odCachedTone = -1.0f;
    eqCachedLow = -99.0f;
    airCached = -1.0f;
    revCachedDecay = -1.0f;
    for (auto* f : { &odHp, &odToneLp, &eqLowF, &eqMidF, &eqHighF, &airF })
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

    pitchShift.prepare (sampleRate);
    revShimmer.prepare (sampleRate);
    delayDuckEnv = 0.0f;

    // cards P4
    wahBp.reset();
    wahEnv = 0.0f;
    wahLfoPhase = 0.0;
    wahRecalcCount = 0;
    sgEnv = sgEnvPrev = 0.0f;
    sgGain = 1.0f;
    octFlip = false;
    octPrev = octEnv = 0.0f;
    octToneCached = -1.0f;
    octLp.reset();
    rmPhase = 0.0;
    bcHold = 0.0f;
    bcCount = 0.0f;
    harmShift.prepare (sampleRate);
    std::fill (std::begin (harmDecim), std::end (harmDecim), 0.0f);
    harmDecimPos = 0;
    harmAccum = 0.0f;
    harmAccumCount = 0;
    harmDetectCounter = 0;
    harmRatioCur = 1.0;
    excCachedFreq = -1.0f;
    excHp.reset();
    dsEnv = 0.0f;
    dsCachedFreq = -1.0f;
    dsBp.reset();
    tapeCachedBump = -99.0f;
    tapeCachedRoll = -1.0f;
    tapeBumpF.reset();
    tapeRollF.reset();
    tapeHpF.setHighPass (sampleRate, 30.0, 0.707);
    tapeHpF.reset();

    // looper: pre-allocated buffer; an old loop loses meaning at another SR
    loopBuf.setSize (1, (int) (sampleRate * looperMaxSeconds) + 1);
    loopBuf.clear();
    looperState.store (0);
    looperLen.store (0);
    looperPos.store (0);
    looperCmd.store (0);

    outLimiter.prepare (spec);
    limCachedThresh = 99.0f;
    limCachedRelease = -1.0f;
    limGrDb.store (0.0f);

    // external VST3 slots: stereo buffer + (re)prepare live instances
    extBuf.setSize (2, samplesPerBlock);
    extMidi.ensureSize (64);

    // drums: engine + bus + (re)prepare the live drum VST
    drumEngine.prepare (sampleRate, samplesPerBlock);
    drumBuf.setSize (2, samplesPerBlock);
    recDrumScratch.setSize (2, samplesPerBlock);
    drumMidi.ensureSize (256);
    for (auto* inst : { drumActive.get(), drumPending.load() })
        if (inst != nullptr)
        {
            inst->setPlayConfigDetails (2, 2, sampleRate, samplesPerBlock);
            inst->prepareToPlay (sampleRate, samplesPerBlock);
        }
    for (int s = 0; s < maxExtSlots; ++s)
        for (auto* inst : { extActive[s].get(), extPending[s].load() })
            if (inst != nullptr)
            {
                inst->setPlayConfigDetails (2, 2, sampleRate, samplesPerBlock);
                inst->prepareToPlay (sampleRate, samplesPerBlock);
            }

    wetScratchR.setSize (1, samplesPerBlock);
    stereoExtra.setSize (1, samplesPerBlock);

    preDelayLine.prepare (spec);
    preDelayLine.setMaximumDelayInSamples ((int) (sampleRate * 0.15) + 1);

    reverb.setSampleRate (sampleRate);
    reverbParams.dryLevel = 0.0f;   // manual mix (with predelay on the wet path)
    reverbParams.wetLevel = 1.0f;
    reverbParams.damping = 0.45f;
    reverbParams.width = 1.0f;
    reverb.setParameters (reverbParams);

    wetScratch.setSize (1, samplesPerBlock);

    // prepareToPlay is not concurrent with processBlock; it can allocate/touch
    // the active and pending models (the loader doesn't touch the pending one after publishing).
    int maxLatency = 0;
    for (int r = 0; r < maxRigs; ++r)
    {
        if (activeModels[r] != nullptr)
        {
            prepareLoadedModel (*activeModels[r], sampleRate, samplesPerBlock);
            maxLatency = juce::jmax (maxLatency, activeModels[r]->latencySamples);
        }
        if (auto* p = pendingModels[r].load())
            if (p != unloadSentinel() && p->model != nullptr)
                prepareLoadedModel (*p, sampleRate, samplesPerBlock);
    }
    setLatencySamples (maxLatency);
}

void GuitarRigNAMProcessor::releaseResources()
{
    for (int r = 0; r < maxRigs; ++r)
        if (auto* q = retiredModels[r].exchange (nullptr); q != unloadSentinel())
            delete q;
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

// NON-NEGOTIABLE RULE: inside processBlock it is FORBIDDEN to allocate memory,
// use locks, do I/O, log or call the network. Model/IR swaps use atomics or
// JUCE's internal RT-safe mechanisms (Convolution).
void GuitarRigNAMProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto ticksStart = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int r = 0; r < maxRigs; ++r)
    {
        if (auto* p = pendingModels[r].exchange (nullptr))
        {
            retiredModels[r].store (activeModels[r].release());
            if (p == unloadSentinel() || p->model == nullptr)
            {
                // "unload lane" sentinel: a static instance - NOTHING is
                // deleted here (RT rule: no allocation/free on this thread)
                modelIsActive[r].store (false);
                resamplingActive[r].store (false);
            }
            else
            {
                activeModels[r].reset (p);
                modelIsActive[r].store (true);
                resamplingActive[r].store (activeModels[r]->resampler != nullptr);
            }
        }
    }

    const float inGain = juce::Decibels::decibelsToGain (pInputGain->load());
    const float outGain = juce::Decibels::decibelsToGain (pOutputGain->load());

    // Mono chain: sum the inputs into channel 0 (the guitar can be on
    // any interface input); for a single source the sum is transparent.
    for (int ch = 1; ch < numIn; ++ch)
        buffer.addFrom (0, 0, buffer, ch, 0, n);

    buffer.applyGain (0, 0, n, inGain);
    inputPeak.store (buffer.getMagnitude (0, 0, n));

    float* io = buffer.getWritePointer (0);

    // ---- tuner tap (raw signal post-gain, pre-gate)
    {
        int w = tunerWritePos.load();
        for (int i = 0; i < n; ++i)
        {
            tunerRing[w] = io[i];
            w = (w + 1) & (tunerRingSize - 1);
        }
        tunerWritePos.store (w);
    }

    // ---- chain in dynamic order (user-reorderable)
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
                case ChainFx::pitch:    processPitchFx (io, n); break;
                case ChainFx::looper:   processLooperFx (io, n); break;
                case ChainFx::limiter:  processLimiterFx (io, n); break;
                case ChainFx::extPlugin:  processExtFx (0, io, n); break;
                case ChainFx::extPlugin2: processExtFx (1, io, n); break;
                case ChainFx::extPlugin3: processExtFx (2, io, n); break;
                case ChainFx::extPlugin4: processExtFx (3, io, n); break;
                case ChainFx::extPlugin5: processExtFx (4, io, n); break;
                case ChainFx::extPlugin6: processExtFx (5, io, n); break;
                case ChainFx::extPlugin7: processExtFx (6, io, n); break;
                case ChainFx::extPlugin8: processExtFx (7, io, n); break;
                case ChainFx::wah:      processWahFx (io, n); break;
                case ChainFx::harm:     processHarmFx (io, n); break;
                case ChainFx::octaver:  processOctaverFx (io, n); break;
                case ChainFx::ringmod:  processRingModFx (io, n); break;
                case ChainFx::bitcrush: processBitcrushFx (io, n); break;
                case ChainFx::slowgear: processSlowGearFx (io, n); break;
                case ChainFx::exciter:  processExciterFx (io, n); break;
                case ChainFx::deesser:  processDeesserFx (io, n); break;
                case ChainFx::tape:     processTapeFx (io, n); break;
                case ChainFx::console:  processConsoleFx (io, n); break;
                case ChainFx::analyzer: processAnalyzerFx (io, n); break;
            }
        }
    }

    buffer.applyGain (0, 0, n, outGain);

    if (numOut >= 2)
    {
        buffer.copyFrom (1, 0, buffer, 0, 0, n);
        // stereo content (ping-pong/reverb width) goes only into the R channel
        if (n <= stereoExtra.getNumSamples())
            buffer.addFrom (1, 0, stereoExtra, 0, 0, n, outGain);
    }

    // scenes (vNext F6): short fade-in after a scene lands, masking parameter
    // and model jumps. Guitar only - the drums keep playing steadily.
    if (int fadeLeft = sceneFadeLeft.load(); fadeLeft > 0)
    {
        const int total = juce::jmax (1, sceneFadeTotal.load());
        const float g0 = (float) (total - fadeLeft) / (float) total;
        const float g1 = (float) juce::jmin (total, total - fadeLeft + n) / (float) total;
        for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
            buffer.applyGainRamp (ch, 0, n, g0, g1);
        sceneFadeLeft.store (juce::jmax (0, fadeLeft - n));
    }

    // guitar stem: the finished guitar bus, BEFORE the drum sum
    if (auto* w = recActiveGtr.load())
    {
        const float* chans[2] = { buffer.getReadPointer (0),
                                  numOut > 1 ? buffer.getReadPointer (1)
                                             : buffer.getReadPointer (0) };
        w->write (chans, n);
    }
    if (recActiveDrm.load() != nullptr && n <= recDrumScratch.getNumSamples())
        recDrumScratch.clear();   // processDrums fills it when drums sound

    // drums: own bus summed AFTER the guitar chain
    processDrums (buffer, numOut, n);

    // drum stem: what processDrums added to the mix in this block
    if (auto* w = recActiveDrm.load(); w != nullptr && n <= recDrumScratch.getNumSamples())
    {
        const float* chans[2] = { recDrumScratch.getReadPointer (0),
                                  recDrumScratch.getReadPointer (1) };
        w->write (chans, n);
    }

    outputPeak.store (buffer.getMagnitude (0, 0, n));

    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    // quick recorder: writes the output (the ThreadedWriter does the disk on
    // another thread; write() here just copies into its FIFO)
    if (auto* w = recActive.load())
    {
        const float* chans[2] = { buffer.getReadPointer (0),
                                  numOut > 1 ? buffer.getReadPointer (1)
                                             : buffer.getReadPointer (0) };
        w->write (chans, n);
    }

    // tuner mute: silences the output (detection uses the pre-chain tap)
    if (tunerMute.load())
        for (int ch = 0; ch < numOut; ++ch)
            buffer.clear (ch, 0, n);

    // ---- CPU meter (fraction of the block time, smoothed)
    {
        const double elapsed = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - ticksStart);
        const double blockDur = n / juce::jmax (1.0, hostSampleRate.load());
        const float load = (float) juce::jlimit (0.0, 1.0, elapsed / blockDur);
        cpuLoad.store (cpuLoad.load() * 0.9f + load * 0.1f);
    }
}

//==============================================================================
// Chain modules (called in dynamic order; same RT rules as processBlock -
// no allocation/locks/IO here)

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
    const float closeLin = juce::Decibels::decibelsToGain (threshDb - 6.0f); // hysteresis
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
                --holdCounter;   // holds open during the hold
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
    // Own gate (follower + 6 dB hysteresis + hold); behavior
    // studied from the references/ToobAmp gate - see docs/EFEITOS.md.
    if (pGateOn->load() <= 0.5f)
        return;

    smartGate.process (io, n, pGateThresh->load(), pGateHold->load(), pGateRelease->load());
}

void GuitarRigNAMProcessor::processCompFx (float* io, int n)
{
    // Pedal compressor: Sustain controls threshold+ratio+makeup together;
    // Blend does parallel compression (mixes with the dry signal).
    // Engine: juce::dsp::Compressor; per-type curves studied from
    // references/lsp-plugins and references/rkrlv2 - see docs/EFEITOS.md.
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
            case 0: // Dyna: aggressive, MXR Dyna Comp style
                pedalComp.setThreshold (-10.0f - sustain * 4.0f);
                pedalComp.setRatio (2.0f + sustain * 0.8f);
                pedalComp.setAttack (attack);
                pedalComp.setRelease (180.0f);
                break;
            case 1: // Optical: slow and musical (LA-2A style)
                pedalComp.setThreshold (-8.0f - sustain * 3.5f);
                pedalComp.setRatio (1.5f + sustain * 0.45f);
                pedalComp.setAttack (juce::jmax (10.0f, attack));
                pedalComp.setRelease (400.0f);
                break;
            case 2: // Studio: transparent VCA (rack dbx/SSL style)
                pedalComp.setThreshold (-6.0f - sustain * 3.0f);
                pedalComp.setRatio (3.0f);
                pedalComp.setAttack (attack);
                pedalComp.setRelease (250.0f);
                break;
            case 3: // Squeezer (Orange Squeezer/Armstrong style): fast vintage squish
                pedalComp.setThreshold (-14.0f - sustain * 4.5f);
                pedalComp.setRatio (5.0f + sustain * 0.5f);
                pedalComp.setAttack (juce::jmin (5.0f, attack));
                pedalComp.setRelease (120.0f);
                break;
        }
    }

    // keeps the dry signal for the blend
    float* dry = monoScratch.getWritePointer (0);
    juce::FloatVectorOperations::copy (dry, io, n);

    juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    pedalComp.process (ctx);

    const float makeupPerSustain = type == 1 ? 2.0f : type == 2 ? 1.6f : type == 3 ? 2.6f : 2.2f;
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
    // pre-amp voicing: changes what ENTERS the capture (changes saturation)
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
    // HP -> clip (per variation) -> tone LP -> post-filter -> level
    // Topology and voicings studied from references/BYOD, references/guitarix
    // and references/GxPlugins.lv2 (own implementation) - see docs/EFEITOS.md.
    if (pOdOn->load() <= 0.5f)
        return;

    const int type = (int) pOdType->load();
    if (type != odCachedType)
    {
        odCachedType = type;
        const double sr = hostSampleRate.load();
        // input voicing and post-filter per variation
        switch (type)
        {
            default:
            case 0: // Screamer (Ibanez Tube Screamer style): tightens lows, mid hump
                odHp.setHighPass (sr, 300.0, 0.707);
                odPost.setPeak (sr, 700.0, 2.5, 0.9);
                odPostActive = true;
                break;
            case 1: // Blues (Marshall Blues Breaker style): near flat, soft clip
                odHp.setHighPass (sr, 100.0, 0.707);
                odPostActive = false;
                break;
            case 2: // Distortion (ProCo RAT/DS-1 style): slight mid scoop
                odHp.setHighPass (sr, 120.0, 0.707);
                odPost.setPeak (sr, 800.0, -2.0, 0.9);
                odPostActive = true;
                break;
            case 3: // Fuzz (Fuzz Face style): full lows, asymmetric clip
                odHp.setHighPass (sr, 80.0, 0.707);
                odPostActive = false;
                break;
            case 4: // Boost (linear clean boost, EP Booster style): flat, almost no clip
                odHp.setHighPass (sr, 40.0, 0.707);
                odPostActive = false;
                break;
            case 5: // Heavy Fuzz (Big Muff style): full lows + mid scoop
                odHp.setHighPass (sr, 60.0, 0.707);
                odPost.setPeak (sr, 1000.0, -3.5, 0.8);
                odPostActive = true;
                break;
            case 6: // Valve (Airwindows Tube style, MIT): tube saturation, even harmonics
                odHp.setHighPass (sr, 50.0, 0.707);
                odPost.setPeak (sr, 1200.0, 1.5, 0.8);
                odPostActive = true;
                break;
            case 7: // Metal (Guitarix/Metal Zone style): high gain + deep scoop
                odHp.setHighPass (sr, 90.0, 0.707);
                odPost.setPeak (sr, 650.0, -6.0, 0.7);
                odPostActive = true;
                break;
        }
        odHp.reset();
        odPost.reset();
    }

    updateOdIfNeeded();
    const float driveGain = juce::Decibels::decibelsToGain (pOdDrive->load() * 4.0f);
    const float levelGain = juce::Decibels::decibelsToGain ((pOdLevel->load() - 5.0f) * 3.0f - 6.0f);
    const float fuzzBiasOut = std::tanh (0.2f); // removes the DC from the asymmetric clip
    const float fuzzBiasIn = std::tanh (0.1f);  // same, for the Valve

    for (int i = 0; i < n; ++i)
    {
        float v = odHp.process (io[i]) * driveGain;

        switch (type)
        {
            default:
            case 0: v = std::tanh (v); break;                                    // soft
            case 1: v = v / (1.0f + std::abs (v)); break;                        // softer
            case 2: v = juce::jlimit (-0.9f, 0.9f, std::tanh (v * 1.6f) * 1.1f); break; // hard
            case 3: v = std::tanh (v * 1.5f + 0.2f) - fuzzBiasOut; break;        // asymmetric
            case 4: v = std::tanh (v * 0.35f) * 2.86f; break;                    // ~linear, saturates only at the extreme
            case 5: v = juce::jlimit (-0.85f, 0.85f, std::tanh (v * 3.0f) * 1.2f); break; // massive sustain
            case 6: v = std::tanh (v * 1.1f + 0.1f) - fuzzBiasIn; break;         // slight asymmetry = even harmonics
            case 7: v = juce::jlimit (-0.75f, 0.75f, std::tanh (v * 4.0f) * 1.3f); break; // hard clip
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
    // Trails: even when off, the pending repeats keep sounding -
    // only the INPUT is cut. Minimal cost, modern pedal behavior.
    // Line: juce::dsp::DelayLine; Analog (BBD, Memory Man style) and
    // Tape (wobble, Echoplex style) voicings studied from references/
    // airwindows and references/guitarix - see docs/EFEITOS.md.
    const bool on = pDelayOn->load() > 0.5f;

    const double sr = hostSampleRate.load();
    const int type = (int) pDelayType->load();
    if (type != delayCachedType)
    {
        delayCachedType = type;
        if (type == 1) // Analog: dark, compressed repeats
        {
            delayFbLp.setLowPass (sr, 3000.0, 0.707);
            delayFbHp.setHighPass (sr, 150.0, 0.707);
        }
        else if (type == 2) // Tape: a bit more open + wobble
        {
            delayFbLp.setLowPass (sr, 4500.0, 0.707);
        }
        delayFbLp.reset();
        delayFbHp.reset();
    }

    delaySmoothedSamples.setTargetValue ((float) (pDelayTime->load() / 1000.0 * sr));
    const float fb = pDelayFb->load() / 100.0f;
    const float mix = pDelayMix->load() / 100.0f;
    const double lfoInc = juce::MathConstants<double>::twoPi * 0.9 / sr; // tape wobble
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
            // Ping-Pong: repeats alternate L/R (the R goes to stereoExtra)
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

        float wetGain = mix;
        if (type == 4)
        {
            // Ducking (TC 2290 style): repeats dip while you
            // play and return in the pauses - fast follower/slow release
            const float rect = std::abs (input);
            delayDuckEnv += (rect > delayDuckEnv ? 0.008f : 0.0004f) * (rect - delayDuckEnv);
            wetGain *= 1.0f - juce::jlimit (0.0f, 0.85f, delayDuckEnv * 6.0f);
        }
        io[i] += wet * wetGain;
    }
}

void GuitarRigNAMProcessor::processReverbFx (float* io, int n)
{
    // manual mix, with predelay on the wet path; trails when off; real
    // stereo via stereoExtra (R-L difference)
    // Engine: juce::Reverb (Freeverb/Schroeder); Hall/Room/Plate voicings
    // studied from references/dragonfly-reverb and the Spring tone (bandpass
    // on the wet) from references/GxPlugins.lv2 - see docs/EFEITOS.md.
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
            case 0: // Hall: large and smooth
                reverbParams.roomSize = 0.2f + decay / 10.0f * 0.75f;
                reverbParams.damping = 0.45f;
                reverbParams.width = 1.0f;
                break;
            case 1: // Room: short and damped
                reverbParams.roomSize = 0.1f + decay / 10.0f * 0.5f;
                reverbParams.damping = 0.6f;
                reverbParams.width = 0.7f;
                break;
            case 2: // Plate: dense and bright
                reverbParams.roomSize = 0.3f + decay / 10.0f * 0.65f;
                reverbParams.damping = 0.12f;
                reverbParams.width = 1.0f;
                break;
            case 3: // Spring: short, springy mids (bandpass on the wet)
                reverbParams.roomSize = 0.15f + decay / 10.0f * 0.35f;
                reverbParams.damping = 0.2f;
                reverbParams.width = 0.6f;
                break;
            case 4: // Shimmer: large and bright, input with an octave up
                reverbParams.roomSize = 0.5f + decay / 10.0f * 0.48f;
                reverbParams.damping = 0.1f;
                reverbParams.width = 1.0f;
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
        preDelayLine.pushSample (0, on ? io[i] : 0.0f); // trails: cuts only the input
        float v = d;
        if (type == 3) // spring: bandpass gives the "spring" tone
            v = revSpringLp.process (revSpringHp.process (v));
        wet[i] = v;
    }

    // Shimmer: the reverb input gets a voice an octave up (60%) -
    // the reverb tail turns "choral" (same technique as Valhalla/Dragonfly)
    if (type == 4)
        revShimmer.process (wet, n, 2.0, 0.6f, 1.0f);

    juce::FloatVectorOperations::copy (wetR, wet, n);
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
    // Chorus/Flanger: juce::dsp::Chorus (flanger = short delay + feedback);
    // Phaser: juce::dsp::Phaser; own harmonic tremolo (anti-phase
    // bands, Fender brownface style) - study in references/ToobAmp and
    // references/GxPlugins.lv2; see docs/EFEITOS.md.
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
            case 2: // Flanger = chorus with short delay + feedback
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
            case 4: // Vibrato = chorus 100% wet (only the pitch wavers)
                chorusFx.setCentreDelay (5.0f);
                chorusFx.setFeedback (0.0f);
                chorusFx.setRate (rate);
                chorusFx.setDepth (depth);
                chorusFx.setMix (1.0f);
                break;
            case 5: // Rotary (Leslie style): slight doppler + per-band AM
                chorusFx.setCentreDelay (8.0f);
                chorusFx.setFeedback (0.05f);
                chorusFx.setRate (rate * 0.8f);
                chorusFx.setDepth (depth * 0.5f);
                chorusFx.setMix (1.0f);
                break;
            default: break; // tremolo doesn't use juce::dsp
        }
    }

    if (type == 0 || type == 2 || type == 4 || type == 5)
    {
        juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        chorusFx.process (ctx);

        if (type == 5)
        {
            // horn (treble) spins ~2.7x faster than the drum (bass)
            const double sr = hostSampleRate.load();
            const double incLo = juce::MathConstants<double>::twoPi * rate / sr;
            const double incHi = incLo * 2.7;
            for (int i = 0; i < n; ++i)
            {
                tremPhase += incLo;
                tremPhase2 += incHi;
                if (tremPhase > juce::MathConstants<double>::twoPi)
                    tremPhase -= juce::MathConstants<double>::twoPi;
                if (tremPhase2 > juce::MathConstants<double>::twoPi)
                    tremPhase2 -= juce::MathConstants<double>::twoPi;
                const float amLo = 1.0f + (float) std::sin (tremPhase) * depth * 0.35f;
                const float amHi = 1.0f + (float) std::sin (tremPhase2) * depth * 0.5f;
                const float lo = tremLp.process (io[i]) * amLo;
                const float hi = tremHp.process (io[i]) * amHi;
                const float wet = lo + hi;
                io[i] = io[i] * (1.0f - mix) + wet * mix;
            }
        }
    }
    else if (type == 1)
    {
        juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        phaserFx.process (ctx);
    }
    else // Harmonic tremolo: bass and treble tremble in opposite phases
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

void GuitarRigNAMProcessor::PitchShifter::process (float* io, int n, double ratio,
                                                   float mix, float outGain) noexcept
{
    const double inc = 1.0 - ratio;
    const float dry = 1.0f - mix;

    for (int i = 0; i < n; ++i)
    {
        buf[w] = io[i];

        ph += inc;
        while (ph >= win) ph -= win;
        while (ph < 0.0)  ph += win;

        double d2 = ph + win * 0.5;
        if (d2 >= win) d2 -= win;

        // sine/cosine crossfade = constant power between the 2 heads;
        // -3 dB compensates the coherent sum (else tonal material reaches +1.41x)
        const float g1 = std::sin ((float) (juce::MathConstants<double>::pi * ph / win));
        const float g2 = std::sin ((float) (juce::MathConstants<double>::pi * d2 / win));
        const float s = (readInterp (ph) * g1 + readInterp (d2) * g2) * 0.7071f;

        w = (w + 1) & (bufSize - 1);
        io[i] = io[i] * dry + s * mix * outGain;
    }
}

void GuitarRigNAMProcessor::processPitchFx (float* io, int n)
{
    // 2-head granular shifter (classic delay-line pitch shifting technique,
    // DAFX/Zolzer); musical-use reference in references/rkrlv2
    // (rakarrack harmonizer) - see docs/EFEITOS.md.
    if (pPitchOn->load() <= 0.5f)
        return;

    // ratios per type: octave down/up, perfect fifth and fourth, detune (~12 cents)
    const int type = (int) pPitchType->load();
    const double ratio = type == 0 ? 0.5
                       : type == 1 ? 2.0
                       : type == 2 ? 1.5
                       : type == 4 ? 4.0 / 3.0
                                   : 1.007;
    const float mix = pPitchMix->load() / 100.0f;
    const float level = juce::Decibels::decibelsToGain (pPitchLevel->load());
    pitchShift.process (io, n, ratio, mix, level);
}

void GuitarRigNAMProcessor::processLooperFx (float* io, int n)
{
    const int maxLen = loopBuf.getNumSamples();
    if (maxLen <= 0)
        return;

    float* loop = loopBuf.getWritePointer (0);
    int st = looperState.load();
    int len = looperLen.load();
    int pos = looperPos.load();

    // editor commands (applied at the block boundary)
    switch (looperCmd.exchange (0))
    {
        case 1: // REC: record -> close and play -> overdub -> play
            if (st == (int) LooperState::empty)        { st = (int) LooperState::recording; len = 0; pos = 0; }
            else if (st == (int) LooperState::recording) { st = (int) LooperState::playing; len = pos; pos = 0; }
            else if (st == (int) LooperState::playing)   st = (int) LooperState::overdub;
            else if (st == (int) LooperState::overdub)   st = (int) LooperState::playing;
            else if (st == (int) LooperState::stopped)   { st = (int) LooperState::overdub; }
            break;
        case 2: // PLAY/STOP
            if (st == (int) LooperState::playing || st == (int) LooperState::overdub)
                { st = (int) LooperState::stopped; pos = 0; }
            else if (st == (int) LooperState::stopped && len > 0)
                st = (int) LooperState::playing;
            else if (st == (int) LooperState::recording)
                { st = (int) LooperState::playing; len = pos; pos = 0; }
            break;
        case 3: // CLEAR
            st = (int) LooperState::empty; len = 0; pos = 0;
            break;
    }

    const float lvl = pLooperOn->load() > 0.5f
                          ? juce::Decibels::decibelsToGain (pLooperLevel->load())
                          : 0.0f;

    if (st == (int) LooperState::recording)
    {
        for (int i = 0; i < n && pos < maxLen; ++i)
            loop[pos++] = io[i];
        if (pos >= maxLen) // hit the max: closes the loop by itself
        {
            st = (int) LooperState::playing;
            len = maxLen;
            pos = 0;
        }
    }
    else if ((st == (int) LooperState::playing || st == (int) LooperState::overdub) && len > 0)
    {
        const bool dub = st == (int) LooperState::overdub;
        for (int i = 0; i < n; ++i)
        {
            const float played = loop[pos];
            if (dub)
                loop[pos] = played + io[i];
            io[i] += played * lvl;
            if (++pos >= len)
                pos = 0;
        }
    }

    looperState.store (st);
    looperLen.store (len);
    looperPos.store (pos);
}

void GuitarRigNAMProcessor::processLimiterFx (float* io, int n)
{
    // Engine: juce::dsp::Limiter (brickwall); output limiter role
    // studied from references/lsp-plugins - see docs/EFEITOS.md.
    if (pLimOn->load() <= 0.5f)
    {
        limGrDb.store (0.0f);
        return;
    }

    const float thresh = pLimCeiling->load();
    const float release = pLimRelease->load();
    if (thresh != limCachedThresh || release != limCachedRelease)
    {
        limCachedThresh = thresh;
        limCachedRelease = release;
        outLimiter.setThreshold (thresh);
        outLimiter.setRelease (release);
    }

    const float preepk = juce::FloatVectorOperations::findMaximum (io, n);

    juce::dsp::AudioBlock<float> block (&io, 1, (size_t) n);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    outLimiter.process (ctx);

    // gain reduction estimate for the card (peak before/after)
    const float postpk = juce::FloatVectorOperations::findMaximum (io, n);
    const float gr = preepk > 1.0e-4f && postpk > 1.0e-4f && preepk > postpk
                         ? juce::Decibels::gainToDecibels (preepk / postpk)
                         : 0.0f;
    limGrDb.store (limGrDb.load() * 0.7f + gr * 0.3f);
}

//==============================================================================
// P4 cards - one effect per card, own controls (refs in docs/EFEITOS.md)

void GuitarRigNAMProcessor::processWahFx (float* io, int n)
{
    // Wah (study: Guitarix GxWahwah): resonant bandpass swept by
    // envelope (Auto), knob (Manual) or LFO
    if (pWahOn->load() <= 0.5f)
        return;

    const int mode = (int) pWahMode->load();
    const float baseFreq = pWahFreq->load();
    const float range = pWahRange->load() / 100.0f;
    const double q = 1.5 + pWahRes->load() * 0.65; // 1.5..8
    const double sr = hostSampleRate.load();
    const double lfoInc = juce::MathConstants<double>::twoPi * 2.0 / sr;

    for (int i = 0; i < n; ++i)
    {
        const float rect = std::abs (io[i]);
        wahEnv += (rect > wahEnv ? 0.006f : 0.0006f) * (rect - wahEnv);
        wahLfoPhase += lfoInc;
        if (wahLfoPhase > juce::MathConstants<double>::twoPi)
            wahLfoPhase -= juce::MathConstants<double>::twoPi;

        // recomputing the biquad per sample is costly - every 16 is smooth enough
        if (--wahRecalcCount <= 0)
        {
            wahRecalcCount = 16;
            double mod = 0.0;
            if (mode == 0)      mod = juce::jlimit (0.0f, 1.0f, wahEnv * 8.0f) * range;
            else if (mode == 2) mod = (std::sin (wahLfoPhase) * 0.5 + 0.5) * range;
            const double f = juce::jlimit (150.0, 2400.0, baseFreq * (1.0 + mod * 2.5));
            wahBp.setBandPass (sr, f, q);
        }
        io[i] = wahBp.process (io[i]) * 1.6f;
    }
}

void GuitarRigNAMProcessor::processSlowGearFx (float* io, int n)
{
    // Slow Gear (BOSS SG-1 style; study: Guitarix GxSlowGear): detects the
    // pick attack and raises the volume slowly - "violin" swell
    if (pSgOn->load() <= 0.5f)
    {
        sgGain = 1.0f;
        return;
    }

    const float thresh = juce::Decibels::decibelsToGain (-58.0f + pSgSens->load() * 3.6f);
    const float step = 1.0f / juce::jmax (1.0f, (float) (pSgRise->load() / 1000.0
                                                         * hostSampleRate.load()));
    for (int i = 0; i < n; ++i)
    {
        const float rect = std::abs (io[i]);
        sgEnv += (rect > sgEnv ? 0.01f : 0.0005f) * (rect - sgEnv);

        // new attack: envelope crossed the threshold rising -> reset and rise
        if (sgEnv > thresh && sgEnvPrev <= thresh)
            sgGain = 0.0f;
        sgEnvPrev = sgEnv;

        sgGain = juce::jmin (1.0f, sgGain + step);
        io[i] *= sgGain * sgGain; // quadratic curve sounds more natural
    }
}

void GuitarRigNAMProcessor::processOctaverFx (float* io, int n)
{
    // Analog octaver (BOSS OC-2 style; study: GxPlugins GxOctaver):
    // flip-flop at zero crossings generates the sub-octave, modulated by the
    // signal envelope and filtered
    if (pOctOn->load() <= 0.5f)
        return;

    const float tone = pOctTone->load();
    if (tone != octToneCached)
    {
        octToneCached = tone;
        octLp.setLowPass (hostSampleRate.load(), tone, 0.707);
    }

    const float sub = pOctSub->load() / 100.0f;
    const float direct = pOctDirect->load() / 100.0f;

    for (int i = 0; i < n; ++i)
    {
        const float x = io[i];
        const float rect = std::abs (x);
        octEnv += (rect > octEnv ? 0.008f : 0.0008f) * (rect - octEnv);

        if (octPrev <= 0.0f && x > 0.0f) // positive crossing: toggle
            octFlip = ! octFlip;
        octPrev = x;

        const float square = (octFlip ? 1.0f : -1.0f) * octEnv * 1.4f;
        io[i] = x * direct + octLp.process (square) * sub;
    }
}

void GuitarRigNAMProcessor::processRingModFx (float* io, int n)
{
    // Ring modulator (study: airwindows) - sine carrier
    if (pRmOn->load() <= 0.5f)
        return;

    const double inc = juce::MathConstants<double>::twoPi * pRmFreq->load()
                       / hostSampleRate.load();
    const float mix = pRmMix->load() / 100.0f;
    for (int i = 0; i < n; ++i)
    {
        rmPhase += inc;
        if (rmPhase > juce::MathConstants<double>::twoPi)
            rmPhase -= juce::MathConstants<double>::twoPi;
        io[i] = io[i] * (1.0f - mix) + io[i] * (float) std::sin (rmPhase) * mix;
    }
}

void GuitarRigNAMProcessor::processBitcrushFx (float* io, int n)
{
    // Bitcrusher (study: airwindows): sample&hold + bit quantization
    if (pBcOn->load() <= 0.5f)
        return;

    const float factor = (float) (hostSampleRate.load() / juce::jmax (1.0f, pBcRate->load()));
    const float q = std::pow (2.0f, pBcBits->load() - 1.0f);
    const float mix = pBcMix->load() / 100.0f;

    for (int i = 0; i < n; ++i)
    {
        bcCount += 1.0f;
        if (bcCount >= factor)
        {
            bcCount -= factor;
            bcHold = std::round (io[i] * q) / q;
        }
        io[i] = io[i] * (1.0f - mix) + bcHold * mix;
    }
}

void GuitarRigNAMProcessor::processHarmFx (float* io, int n)
{
    // Diatonic harmonizer (study: rkrlv2/rakarrack): autocorrelation over
    // decimated signal detects the note; the chosen interval is applied INSIDE
    // the scale (3rd becomes major or minor per degree) via granular shifter
    if (pHarmOn->load() <= 0.5f)
        return;

    const double sr = hostSampleRate.load();

    for (int i = 0; i < n; ++i)
    {
        // decimation by 8 with averaging (cheap anti-alias) -> ring of 512
        harmAccum += io[i];
        if (++harmAccumCount >= 8)
        {
            harmDecim[harmDecimPos] = harmAccum / 8.0f;
            harmDecimPos = (harmDecimPos + 1) & (harmDecimSize - 1);
            harmAccum = 0.0f;
            harmAccumCount = 0;
        }
    }

    // detection every ~21 ms (1024 samples at 48 kHz)
    harmDetectCounter += n;
    if (harmDetectCounter >= 1024)
    {
        harmDetectCounter = 0;
        const double decSr = sr / 8.0;

        // normalized autocorrelation over the guitar range lags
        const int minLag = (int) (decSr / 900.0);  // ~900 Hz
        const int maxLag = (int) (decSr / 70.0);   // ~70 Hz
        float bestCorr = 0.0f;
        int bestLag = 0;
        float energy = 1.0e-9f;
        for (int j = 0; j < harmDecimSize; ++j)
            energy += harmDecim[j] * harmDecim[j];

        for (int lag = minLag; lag <= juce::jmin (maxLag, harmDecimSize / 2); ++lag)
        {
            float corr = 0.0f;
            for (int j = 0; j < harmDecimSize - lag; ++j)
                corr += harmDecim[j] * harmDecim[j + lag];
            corr /= energy;
            if (corr > bestCorr)
            {
                bestCorr = corr;
                bestLag = lag;
            }
        }

        if (bestCorr > 0.35f && bestLag > 0)
        {
            const double freq = decSr / bestLag;
            const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 (freq / 440.0));

            // degree in the chosen scale (note out of scale: uses the step below)
            static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
            static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
            const int* scale = (int) pHarmScale->load() == 0 ? majorScale : minorScale;
            const int key = (int) pHarmKey->load();
            const int chroma = ((midi - key) % 12 + 12) % 12;
            int degree = 0;
            for (int d = 6; d >= 0; --d)
                if (scale[d] <= chroma) { degree = d; break; }

            const int stepsPerInterval[4] = { 2, 4, 5, 7 }; // 3rd, 5th, 6th, octave
            const int steps = stepsPerInterval[juce::jlimit (0, 3,
                                                             (int) pHarmInterval->load())];
            const int targetDegree = degree + steps;
            const int semis = scale[targetDegree % 7] + 12 * (targetDegree / 7)
                              - scale[degree];
            harmRatioCur = std::pow (2.0, semis / 12.0);
        }
        // no reliable pitch: keeps the previous ratio (doesn't "jump")
    }

    const float mix = pHarmMix->load() / 100.0f;
    const float level = juce::Decibels::decibelsToGain (pHarmLevel->load());
    harmShift.process (io, n, harmRatioCur, mix, level);
}

void GuitarRigNAMProcessor::processExciterFx (float* io, int n)
{
    // Exciter (study: airwindows Energy): harmonics of the saturated treble
    // summed back into the signal
    if (pExcOn->load() <= 0.5f)
        return;

    const float freq = pExcFreq->load();
    if (freq != excCachedFreq)
    {
        excCachedFreq = freq;
        excHp.setHighPass (hostSampleRate.load(), freq, 0.707);
    }

    const float amt = pExcAmt->load() / 100.0f * 0.6f;
    for (int i = 0; i < n; ++i)
    {
        const float hi = excHp.process (io[i]);
        io[i] += std::tanh (hi * 3.0f) * amt;
    }
}

void GuitarRigNAMProcessor::processDeesserFx (float* io, int n)
{
    // De-esser/resonance tamer (study: lsp-plugins): the harsh band is
    // subtracted dynamically when it exceeds the threshold
    if (pDsOn->load() <= 0.5f)
        return;

    const float freq = pDsFreq->load();
    if (freq != dsCachedFreq)
    {
        dsCachedFreq = freq;
        dsBp.setBandPass (hostSampleRate.load(), freq, 2.0);
    }

    const float sens = 2.0f + pDsSens->load() * 3.0f;
    const float amt = pDsAmt->load() / 100.0f;
    for (int i = 0; i < n; ++i)
    {
        const float band = dsBp.process (io[i]);
        const float rect = std::abs (band);
        dsEnv += (rect > dsEnv ? 0.01f : 0.001f) * (rect - dsEnv);
        const float excess = juce::jlimit (0.0f, 1.0f, dsEnv * sens - 0.1f);
        io[i] -= band * excess * amt;
    }
}

void GuitarRigNAMProcessor::processTapeFx (float* io, int n)
{
    // Tape (adapted from the airwindows ToTape/IronOxide idea, MIT):
    // HP sub -> light asymmetric saturation -> head bump -> treble rolloff
    if (pTapeOn->load() <= 0.5f)
        return;

    const float bump = pTapeBump->load();
    const float roll = pTapeRoll->load();
    if (bump != tapeCachedBump || roll != tapeCachedRoll)
    {
        tapeCachedBump = bump;
        tapeCachedRoll = roll;
        const double sr = hostSampleRate.load();
        tapeBumpF.setLowShelf (sr, 90.0, bump);
        tapeRollF.setLowPass (sr, roll, 0.707);
    }

    const float d = 1.0f + pTapeDrive->load() * 0.6f;
    const float bias = std::tanh (0.03f * d);
    const float makeup = 1.0f / (0.4f + 0.6f * std::tanh (d * 0.5f));
    for (int i = 0; i < n; ++i)
    {
        float v = tapeHpF.process (io[i]);
        v = std::tanh (v * d + 0.03f * d) - bias;
        v = tapeBumpF.process (v);
        v = tapeRollF.process (v);
        io[i] = v * makeup;
    }
}

void GuitarRigNAMProcessor::processConsoleFx (float* io, int n)
{
    // Console glue (adapted from the airwindows Console idea, MIT): subtle
    // sine waveshape - "glues" the signal like an analog buss
    if (pCnsOn->load() <= 0.5f)
        return;

    const float a = 0.35f + pCnsAmt->load() * 0.12f; // 0.35..1.55
    const float inv = 1.0f / a;
    for (int i = 0; i < n; ++i)
        io[i] = std::sin (juce::jlimit (-1.5f, 1.5f, io[i] * a)) * inv;
}

void GuitarRigNAMProcessor::processAnalyzerFx (float* io, int n)
{
    // passthrough + tap for the spectrum (the editor reads and draws)
    if (pAnOn->load() <= 0.5f)
        return;

    int w = anWritePos.load();
    for (int i = 0; i < n; ++i)
    {
        anRing[w] = io[i];
        w = (w + 1) & (analyzerRingSize - 1);
    }
    anWritePos.store (w);
}

void GuitarRigNAMProcessor::readAnalyzerBlock (float* dest, int numSamples) const
{
    const int writePos = anWritePos.load();
    int start = (writePos - numSamples) & (analyzerRingSize - 1);
    for (int i = 0; i < numSamples; ++i)
    {
        dest[i] = anRing[start];
        start = (start + 1) & (analyzerRingSize - 1);
    }
}

juce::File GuitarRigNAMProcessor::startRecording()
{
    // message thread
    if (isRecording())
        return {};

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PedalForge NAM")
                   .getChildFile ("Recordings");
    dir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
    auto file = dir.getChildFile ("Take " + stamp + ".wav");

    juce::WavAudioFormat wav;
    auto makeWriter = [&] (const juce::File& f)
        -> std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>
    {
        if (auto stream = f.createOutputStream())
            if (auto* writer = wav.createWriterFor (stream.get(), hostSampleRate.load(), 2, 24, {}, 0))
            {
                stream.release(); // the writer owns the stream now
                return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
                    writer, recThread, 1 << 17);
            }
        return nullptr;
    };

    recWriter = makeWriter (file);
    if (recWriter == nullptr)
        return {};

    // stems: separate guitar and drum tracks of the same take (same length,
    // aligned - drop them into a DAW and they line up with the mix)
    recWriterGtr = makeWriter (dir.getChildFile ("Take " + stamp + " (guitar).wav"));
    recWriterDrm = makeWriter (dir.getChildFile ("Take " + stamp + " (drums).wav"));

    recActiveGtr.store (recWriterGtr.get());
    recActiveDrm.store (recWriterDrm.get());
    recActive.store (recWriter.get());
    return file;
}

void GuitarRigNAMProcessor::stopRecording()
{
    // message thread: removes from audio first; deletes with slack (the audio
    // may be mid-write with the old pointer)
    recActive.store (nullptr);
    recActiveGtr.store (nullptr);
    recActiveDrm.store (nullptr);
    if (auto* old = recWriter.release())
        juce::Timer::callAfterDelay (400, [old] { delete old; });
    if (auto* old = recWriterGtr.release())
        juce::Timer::callAfterDelay (400, [old] { delete old; });
    if (auto* old = recWriterDrm.release())
        juce::Timer::callAfterDelay (400, [old] { delete old; });
}

//==============================================================================
// vNext F6 - Song/Scenes: full guitar-rig snapshot per drum section

juce::ValueTree GuitarRigNAMProcessor::captureRigScene()
{
    auto t = captureState (true);

    // guitar rig only: strip the drum domain and UI prefs so applying a scene
    // never touches the playing drums nor flips view preferences
    for (int b = 0; b < drum::maxBars; ++b)
    {
        const auto sfx = juce::String (b + 1);
        t.removeProperty ("drumBar" + sfx, nullptr);
        t.removeProperty ("drumBarName" + sfx, nullptr);
        t.removeProperty ("drumMeter" + sfx, nullptr);
        t.removeProperty ("drumBarRole" + sfx, nullptr);
    }
    t.removeProperty ("drumPattern", nullptr);
    for (int i = 0; i < drum::maxSections * 2; ++i)
        t.removeProperty ("drumSecPattern" + juce::String (i + 1), nullptr);
    for (auto* key : { "drumNumSections", "drumBpm", "drumSwing", "drumLevel",
                       "drumHumVel", "drumHumTime", "drumHumRR", "drumClick",
                       "drumCountIn", "drumHostSync", "drumUseVst", "drumVstPath",
                       "drumVstState", "chainCompact", "tunerOn", "uiTheme",
                       "drumRibMin", "scenesOn" })
        t.removeProperty (key, nullptr);
    t.removeProperty (kStatePresetName, nullptr);
    for (int i = 0; i < drum::maxSections; ++i)          // scenes don't nest
        t.removeProperty ("sceneRig" + juce::String (i + 1), nullptr);
    return t;
}

void GuitarRigNAMProcessor::saveSceneForSection (int sec)
{
    if (sec < 0 || sec >= drum::maxSections)
        return;
    sceneXml[sec] = captureRigScene().toXmlString (
        juce::XmlElement::TextFormat().singleLine());
}

void GuitarRigNAMProcessor::clearSceneForSection (int sec)
{
    if (sec >= 0 && sec < drum::maxSections)
        sceneXml[sec].clear();
}

void GuitarRigNAMProcessor::applySceneForSection (int sec)
{
    // message thread
    if (! hasScene (sec))
        return;
    auto tree = juce::ValueTree::fromXml (sceneXml[sec]);
    if (! tree.isValid())
        return;

    // live UI prefs survive the replaceState (the scene doesn't carry them)
    for (auto* key : { "chainCompact", "tunerOn", "uiTheme", "drumRibMin" })
        if (apvts.state.hasProperty (key))
            tree.setProperty (key, apvts.state.getProperty (key), nullptr);

    applyingSceneNow = true;
    applyState (tree);
    applyingSceneNow = false;

    // anti-click: ~30 ms fade-in on the guitar bus after the jump
    const int fade = juce::jmax (64, (int) (hostSampleRate.load() * 0.03));
    sceneFadeTotal.store (fade);
    sceneFadeLeft.store (fade);
}

juce::String GuitarRigNAMProcessor::sceneSummary (int sec) const
{
    if (! hasScene (sec))
        return {};
    auto t = juce::ValueTree::fromXml (sceneXml[sec]);
    if (! t.isValid())
        return {};

    const juce::File model (t.getProperty (kStateModelPath, "").toString());
    const auto amp = model.getFullPathName().isNotEmpty()
                         ? model.getFileNameWithoutExtension()
                         : juce::String ("No capture");
    const auto order = juce::StringArray::fromTokens (
        t.getProperty ("chainOrder", "").toString(), ",", "");
    int fx = 0;
    for (const auto& id : order)
        if (id != "amp")
            ++fx;
    return amp + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (fx) + " fx";
}

void GuitarRigNAMProcessor::shiftScenesOnSectionRemove (int sec)
{
    if (sec < 0 || sec >= drum::maxSections)
        return;
    for (int i = sec; i < drum::maxSections - 1; ++i)
        sceneXml[i] = sceneXml[i + 1];
    sceneXml[drum::maxSections - 1].clear();
}

void GuitarRigNAMProcessor::handleAsyncUpdate()
{
    const int sec = scenePendingSection.exchange (-1);
    if (sec >= 0)
        applySceneForSection (sec);
}

void GuitarRigNAMProcessor::toggleAB()
{
    // message thread: saves the current state in the active slot and toggles
    abSlots[abCurrent] = captureState();
    abCurrent = 1 - abCurrent;
    if (abSlots[abCurrent].isValid())
        applyState (abSlots[abCurrent]);
    else
        abSlots[abCurrent] = abSlots[1 - abCurrent].createCopy(); // first time: B starts from A
}

void GuitarRigNAMProcessor::processExtFx (int slot, float* io, int n)
{
    // RT-safe swap of the hosted instance (same protocol as the NAM models)
    if (auto* p = extPending[slot].exchange (nullptr))
    {
        extRetired[slot].store (extActive[slot].release());
        extActive[slot].reset (p);
        extUiInstance[slot].store (extActive[slot].get());
        extLoaded[slot].store (true);
    }
    if (extUnloadRequest[slot].exchange (false))
    {
        extUiInstance[slot].store (nullptr);
        extRetired[slot].store (extActive[slot].release());
        extLoaded[slot].store (false);
    }

    if (extActive[slot] == nullptr || pExtOn[slot]->load() <= 0.5f)
        return;
    if (n > extBuf.getNumSamples())
        return;

    // mono -> stereo for the guest; L goes back into the chain and the R-L
    // difference goes into stereoExtra (same convention as delay/reverb)
    extBuf.copyFrom (0, 0, io, n);
    extBuf.copyFrom (1, 0, io, n);
    juce::AudioBuffer<float> view (extBuf.getArrayOfWritePointers(), 2, n);
    extMidi.clear();
    extActive[slot]->processBlock (view, extMidi);

    const float mix = pExtMix[slot]->load() / 100.0f;
    const float* l = extBuf.getReadPointer (0);
    const float* r = extBuf.getReadPointer (1);
    float* extra = stereoExtra.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        io[i] = io[i] * (1.0f - mix) + l[i] * mix;
        extra[i] += (r[i] - l[i]) * mix;
    }
}

void GuitarRigNAMProcessor::processDrums (juce::AudioBuffer<float>& buffer, int numOut, int n)
{
    // vNext: DAW sync - follow the host BPM when available and enabled.
    // getPosition() returns a value struct; no allocation on the RT path.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                hostBpm.store ((float) *b);
    if (drumHostSync.load())
    {
        const float hb = hostBpm.load();
        if (hb >= 30.0f && hb <= 300.0f)
            drumEngine.bpm.store (hb);
    }

    // RT-safe swap of the drum VST instance
    if (auto* p = drumPending.exchange (nullptr))
    {
        drumRetired.store (drumActive.release());
        drumActive.reset (p);
        drumUiInstance.store (drumActive.get());
        drumLoaded.store (true);
    }
    if (drumUnloadRequest.exchange (false))
    {
        drumUiInstance.store (nullptr);
        drumRetired.store (drumActive.release());
        drumLoaded.store (false);
    }

    // scenes (vNext F6): entering a new section (bar start) requests the
    // section's rig snapshot on the message thread. uiBar advances exactly at
    // the bar boundary; stopping (-1) re-arms so a restart re-applies.
    // Runs before the audibility early-return so the stop re-arm always fires.
    if (scenesOn.load())
    {
        const int bar = drumEngine.uiBar.load();
        const int sec = bar >= 0 ? bar / drum::barsPerSection : -1;
        if (sec != sceneLastSection)
        {
            sceneLastSection = sec;
            if (sec >= 0)
            {
                scenePendingSection.store (sec);
                triggerAsyncUpdate();
            }
        }
    }

    auto* vst = (drumEngine.useVst.load() && drumActive != nullptr) ? drumActive.get() : nullptr;
    if (n > drumBuf.getNumSamples() || (! drumEngine.isAudible() && vst == nullptr))
        return;

    juce::AudioBuffer<float> view (drumBuf.getArrayOfWritePointers(), 2, n);
    drumEngine.process (view, n, vst, drumMidi);

    const float lv = drumEngine.level.load();
    buffer.addFrom (0, 0, drumBuf, 0, 0, n, lv);
    if (numOut >= 2)
        buffer.addFrom (1, 0, drumBuf, 1, 0, n, lv);

    // drum stem (recording): the same signal that entered the mix
    if (recActiveDrm.load() != nullptr && n <= recDrumScratch.getNumSamples())
        for (int ch = 0; ch < 2; ++ch)
        {
            recDrumScratch.copyFrom (ch, 0, drumBuf, ch, 0, n);
            recDrumScratch.applyGain (ch, 0, n, lv);
        }
}

void GuitarRigNAMProcessor::loadDrumPluginAsync (const juce::File& file,
                                                 const juce::MemoryBlock* stateToRestore)
{
    // message thread (same recipe as loadExternalPluginAsync)
    if (! file.exists())
        return;

    collectExternalRetired();

    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : extFormatManager.getFormats())
        format->findAllTypesForFile (types, file.getFullPathName());

    if (types.isEmpty())
    {
        const juce::ScopedLock sl (modelInfoLock);
        loadError = juce::String ("No valid VST3 plugin found in ")
                    + file.getFileName();
        return;
    }

    // prefers an INSTRUMENT type if the .vst3 has more than one
    int pick = 0;
    for (int i = 0; i < types.size(); ++i)
        if (types[i]->isInstrument) { pick = i; break; }

    juce::MemoryBlock state = stateToRestore != nullptr ? *stateToRestore : juce::MemoryBlock();
    loading.store (true);

    extFormatManager.createPluginInstanceAsync (
        *types[pick], hostSampleRate.load(), preparedBlockSize.load(),
        [this, state, path = file.getFullPathName()]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            loading.store (false);
            if (instance == nullptr)
            {
                const juce::ScopedLock sl (modelInfoLock);
                loadError = error.isNotEmpty() ? error : "Failed to instantiate the plugin";
                return;
            }

            const double sr = hostSampleRate.load();
            const int block = preparedBlockSize.load();
            instance->setPlayConfigDetails (0, 2, sr, block);
            if (instance->getTotalNumOutputChannels() < 2)
                instance->setPlayConfigDetails (2, 2, sr, block);
            instance->prepareToPlay (sr, block);
            if (state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());

            {
                const juce::ScopedLock sl (modelInfoLock);
                drumVstName = instance->getName();
                drumVstPath = path;
                loadError.clear();
            }

            if (onDrumPluginWillChange)
                onDrumPluginWillChange();

            collectExternalRetired();
            delete drumPending.exchange (instance.release());
            drumEngine.useVst.store (true);
        });
}

void GuitarRigNAMProcessor::clearDrumPlugin()
{
    // message thread
    if (onDrumPluginWillChange)
        onDrumPluginWillChange();

    {
        const juce::ScopedLock sl (modelInfoLock);
        drumVstName.clear();
        drumVstPath.clear();
    }

    collectExternalRetired();
    delete drumPending.exchange (nullptr);
    drumUnloadRequest.store (true);
    drumLoaded.store (false);
    drumEngine.useVst.store (false);
}

juce::String GuitarRigNAMProcessor::getDrumPluginName() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return drumVstName;
}

juce::String GuitarRigNAMProcessor::getDrumPluginPath() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return drumVstPath;
}

void GuitarRigNAMProcessor::loadExternalPluginAsync (int slot, const juce::File& file,
                                                     const juce::MemoryBlock* stateToRestore)
{
    // message thread. Discovering the types inside the .vst3 loads the module -
    // fast enough for an explicit user click.
    if (slot < 0 || slot >= maxExtSlots || ! file.exists())
        return;

    collectExternalRetired();

    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : extFormatManager.getFormats())
        format->findAllTypesForFile (types, file.getFullPathName());

    if (types.isEmpty())
    {
        const juce::ScopedLock sl (modelInfoLock);
        loadError = juce::String ("No valid VST3 plugin found in ")
                    + file.getFileName();
        return;
    }

    juce::MemoryBlock state = stateToRestore != nullptr ? *stateToRestore : juce::MemoryBlock();
    loading.store (true);

    extFormatManager.createPluginInstanceAsync (
        *types[0], hostSampleRate.load(), preparedBlockSize.load(),
        [this, slot, state, path = file.getFullPathName()]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            loading.store (false);
            if (instance == nullptr)
            {
                const juce::ScopedLock sl (modelInfoLock);
                loadError = error.isNotEmpty() ? error : "Failed to instantiate the plugin";
                return;
            }

            const double sr = hostSampleRate.load();
            const int block = preparedBlockSize.load();
            instance->setPlayConfigDetails (2, 2, sr, block);
            instance->prepareToPlay (sr, block);
            if (state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());

            {
                const juce::ScopedLock sl (modelInfoLock);
                extName[slot] = instance->getName();
                extPath[slot] = path;
                loadError.clear();
            }

            if (onExternalPluginWillChange)
                onExternalPluginWillChange (slot); // closes the old instance's panel

            collectExternalRetired();
            delete extPending[slot].exchange (instance.release());
        });
}

void GuitarRigNAMProcessor::clearExternalPlugin (int slot)
{
    // message thread
    if (slot < 0 || slot >= maxExtSlots)
        return;

    if (onExternalPluginWillChange)
        onExternalPluginWillChange (slot);

    {
        const juce::ScopedLock sl (modelInfoLock);
        extName[slot].clear();
        extPath[slot].clear();
    }

    collectExternalRetired();
    delete extPending[slot].exchange (nullptr); // pending never reached the audio thread
    extUnloadRequest[slot].store (true);
    extLoaded[slot].store (false); // UI doesn't wait for the next audio block
}

juce::String GuitarRigNAMProcessor::getExternalPluginName (int slot) const
{
    if (slot < 0 || slot >= maxExtSlots)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return extName[slot];
}

juce::String GuitarRigNAMProcessor::getExternalPluginPath (int slot) const
{
    if (slot < 0 || slot >= maxExtSlots)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return extPath[slot];
}

double GuitarRigNAMProcessor::getLooperSeconds() const noexcept
{
    return looperLen.load() / juce::jmax (1.0, hostSampleRate.load());
}

double GuitarRigNAMProcessor::getLooperPosSeconds() const noexcept
{
    return looperPos.load() / juce::jmax (1.0, hostSampleRate.load());
}

juce::File GuitarRigNAMProcessor::exportLoopToWav() const
{
    const int len = looperLen.load();
    if (len <= 0 || loopBuf.getNumSamples() < len)
        return {};

    // copy first: the audio may be playing/overdubbing the buffer
    juce::AudioBuffer<float> copy (1, len);
    copy.copyFrom (0, 0, loopBuf, 0, 0, len);

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PedalForge NAM")
                   .getChildFile ("Loops");
    dir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
    auto file = dir.getChildFile ("Loop " + stamp + ".wav");

    juce::WavAudioFormat wav;
    if (auto stream = file.createOutputStream())
    {
        if (std::unique_ptr<juce::AudioFormatWriter> writer {
                wav.createWriterFor (stream.get(), hostSampleRate.load(), 1, 24, {}, 0) })
        {
            stream.release(); // the writer owns the stream now
            writer->writeFromAudioSampleBuffer (copy, 0, len);
            return file;
        }
    }
    return {};
}

void GuitarRigNAMProcessor::processAmpAndCabs (juce::AudioBuffer<float>& buffer, float* io, int n)
{
    // ---- up to 3 AMP+CAB lanes in parallel, always as a pair (capture + IR),
    //      summed in the Mixer: per lane, GAIN -> NAM model (resampler if
    //      needed) -> tone stack -> MASTER -> IR -> LC/HC/phase -> blend.
    const bool ampOn = pAmpOn->load() > 0.5f;
    const bool cabOn = pCabOn->load() > 0.5f;
    const int count = juce::jlimit (1, (int) maxRigs, (int) pCabCount->load());

    auto processAmpLane = [this] (int r, float* lane, int n)
    {
        auto& lm = *activeModels[r];
        juce::FloatVectorOperations::multiply (
            lane, juce::Decibels::decibelsToGain (pAmpGain[r]->load()), n);

        float* scratch = monoScratch.getWritePointer (0);
        const int maxChunk = monoScratch.getNumSamples();

        // Blocks larger than prepared (rare) are processed in chunks,
        // keeping the Reset max-size contract - on both paths.
        for (int pos = 0; pos < n; pos += maxChunk)
        {
            const int len = juce::jmin (maxChunk, n - pos);
            float* in = lane + pos;

            if (lm.resampler != nullptr)
                lm.resampler->ProcessBlock (&in, &scratch, len, lm.func);
            else
                lm.model->process (&in, &scratch, len);

            juce::FloatVectorOperations::copy (lane + pos, scratch, len);
        }

        updateToneStackIfNeeded (r);
        for (int i = 0; i < n; ++i)
            lane[i] = tsPresence[r].process (tsTreble[r].process (
                tsMid[r].process (tsBass[r].process (lane[i]))));

        juce::FloatVectorOperations::multiply (
            lane, juce::Decibels::decibelsToGain (pAmpMaster[r]->load()), n);
    };

    // Very rare fallback (block larger than prepared): processes only lane 1
    // in-place, no mix - keeps audio without touching too-small buffers.
    if (n > cabDryBuf.getNumSamples())
    {
        if (ampOn && activeModels[0] != nullptr)
            processAmpLane (0, io, n);
        return;
    }

    juce::FloatVectorOperations::copy (cabDryBuf.getWritePointer (0), io, n);
    cabAccBuf.clear (0, 0, n);

    for (int r = 0; r < count; ++r)
    {
        float* lane = cabSlotBuf.getWritePointer (0);
        juce::FloatVectorOperations::copy (lane, cabDryBuf.getReadPointer (0), n);

        if (ampOn && activeModels[r] != nullptr)
            processAmpLane (r, lane, n);

        if (cabOn)
        {
            if (irLoadedFlags[r].load() && convolutions[r].getCurrentIRSize() > 0)
            {
                juce::dsp::AudioBlock<float> block (&lane, 1, (size_t) n);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                convolutions[r].process (ctx);
            }

            updateCabSlotFilters (r);
            const bool lcOn = pCabLowCut[r]->load() > 22.0f;
            const bool hcOn = pCabHighCut[r]->load() < 19000.0f;
            if (lcOn || hcOn)
                for (int i = 0; i < n; ++i)
                {
                    float v = lane[i];
                    if (lcOn) v = cabLc[r].process (v);
                    if (hcOn) v = cabHc[r].process (v);
                    lane[i] = v;
                }
        }

        // Mixer: per-lane blend (+ phase inversion)
        const float g = (pCabBlend[r]->load() / 100.0f)
                        * (pCabPhase[r]->load() > 0.5f ? -1.0f : 1.0f);
        if (g != 0.0f)
            juce::FloatVectorOperations::addWithMultiply (
                cabAccBuf.getWritePointer (0), lane, g, n);
    }

    juce::FloatVectorOperations::copy (io, cabAccBuf.getReadPointer (0), n);
    buffer.applyGain (0, 0, n, juce::Decibels::decibelsToGain (pCabLevel->load()));

    // AIR: post-mix treble shelf (global)
    if (cabOn && pCabAir->load() > 0.05f)
    {
        updateAirIfNeeded();
        for (int i = 0; i < n; ++i)
            io[i] = airF.process (io[i]);
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
        case ChainFx::pitch:    return "pitch";
        case ChainFx::looper:   return "looper";
        case ChainFx::limiter:  return "limiter";
        case ChainFx::extPlugin: return "ext";
        case ChainFx::wah:      return "wah";
        case ChainFx::harm:     return "harm";
        case ChainFx::octaver:  return "octaver";
        case ChainFx::ringmod:  return "ringmod";
        case ChainFx::bitcrush: return "bitcrush";
        case ChainFx::slowgear: return "slowgear";
        case ChainFx::exciter:  return "exciter";
        case ChainFx::deesser:  return "deesser";
        case ChainFx::tape:     return "tape";
        case ChainFx::console:  return "console";
        case ChainFx::analyzer: return "analyzer";
        case ChainFx::extPlugin2: return "ext2";
        case ChainFx::extPlugin3: return "ext3";
        case ChainFx::extPlugin4: return "ext4";
        case ChainFx::extPlugin5: return "ext5";
        case ChainFx::extPlugin6: return "ext6";
        case ChainFx::extPlugin7: return "ext7";
        case ChainFx::extPlugin8: return "ext8";
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
    // lean initial pedalboard - the rest stays in the "+ effect" drawer
    const ChainFx def[] = { ChainFx::gate, ChainFx::comp, ChainFx::od, ChainFx::preEq,
                            ChainFx::ampBlock, ChainFx::eq, ChainFx::mod,
                            ChainFx::delay, ChainFx::reverb };
    for (int i = 0; i < (int) std::size (def); ++i)
        chainOrder[i].store ((int) def[i]);
    chainLen.store ((int) std::size (def));
}

int GuitarRigNAMProcessor::canonicalRank (int fx)
{
    // full "musically obvious" order - used to insert effects from the
    // drawer in the right position and to ensure the amp anchor
    static const ChainFx canon[] = { ChainFx::gate, ChainFx::comp, ChainFx::slowgear,
                                     ChainFx::wah, ChainFx::octaver, ChainFx::ringmod,
                                     ChainFx::od, ChainFx::pitch, ChainFx::harm,
                                     ChainFx::preEq, ChainFx::ampBlock, ChainFx::bitcrush,
                                     ChainFx::eq, ChainFx::exciter, ChainFx::deesser,
                                     ChainFx::mod, ChainFx::tape, ChainFx::delay,
                                     ChainFx::reverb, ChainFx::extPlugin, ChainFx::extPlugin2,
                                     ChainFx::extPlugin3, ChainFx::extPlugin4,
                                     ChainFx::extPlugin5, ChainFx::extPlugin6,
                                     ChainFx::extPlugin7, ChainFx::extPlugin8,
                                     ChainFx::console,
                                     ChainFx::analyzer, ChainFx::limiter, ChainFx::looper };
    for (int i = 0; i < (int) std::size (canon); ++i)
        if ((int) canon[i] == fx)
            return i;
    return (int) std::size (canon);
}

int GuitarRigNAMProcessor::canonicalRank (const juce::String& id)
{
    return canonicalRank (fxFromString (id));
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
    // dev: GUITARRIG_DEBUGLOG=<file> logs every chain change
    {
        static const auto logPath =
            juce::SystemStats::getEnvironmentVariable ("GUITARRIG_DEBUGLOG", "");
        if (logPath.isNotEmpty())
            juce::File (logPath).appendText (
                juce::Time::getCurrentTime().formatted ("%H:%M:%S")
                + " setChainOrder: " + ids.joinIntoString (",") + "\n");
    }

    // The chain is PARTIAL: only the effects "on the pedalboard" - the rest
    // stays in the drawer (not processed, but keeps its parameter settings).
    // Normalizes: known ids, each at most once; "amp" always present
    // (anchor), inserted at the canonical position if missing.
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

    if (! used[(int) ChainFx::ampBlock])
    {
        int pos = order.size();
        for (int i = 0; i < order.size(); ++i)
            if (canonicalRank (order[i]) > canonicalRank ((int) ChainFx::ampBlock))
            {
                pos = i;
                break;
            }
        order.insert (pos, (int) ChainFx::ampBlock);
    }

    const int len = juce::jmin (order.size(), (int) chainMaxSlots);
    for (int i = 0; i < len; ++i)
        chainOrder[i].store (order[i]);
    chainLen.store (len);
}

//==============================================================================
void GuitarRigNAMProcessor::loadModelAsync (int lane, const juce::File& file)
{
    if (lane < 0 || lane >= maxRigs)
        return;

    // guard: IR (.wav/.aiff/.flac) never enters the amp - prevents a
    // wrong routing from loading an impulse as a capture
    if (! file.hasFileExtension ("nam"))
    {
        const juce::ScopedLock sl (modelInfoLock);
        loadError = juce::String ("Only .nam files can be loaded into the amp (")
                    + file.getFileName() + ")";
        return;
    }

    loading.store (true);

    loaderPool.addJob ([this, lane, file]
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
            error = "Unknown failure loading the model";
        }

        if (lm->model != nullptr
            && (lm->model->NumInputChannels() != 1 || lm->model->NumOutputChannels() != 1))
        {
            error = "Only mono captures (1 in / 1 out) are supported";
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
            loadError = error.isNotEmpty() ? error : "Invalid .nam file";
            loading.store (false);
            return;
        }

        // Architecture (A1/A2 badge): the TONE3000 .meta sidecar takes priority;
        // without it, we read the "architecture" field from the .nam itself.
        juce::String archLabel;
        {
            const auto meta = juce::JSON::parse (
                juce::File (file.getFullPathName() + ".meta").loadFileAsString());
            const auto metaArch = meta.getProperty ("arch", "").toString();
            if (metaArch == "2")
                archLabel = "A2";
            else if (metaArch == "1")
                archLabel = "A1";
            else
            {
                // A2 = SlimmableContainer format in the .nam file itself
                const auto namJson = juce::JSON::parse (file.loadFileAsString());
                const auto arch = namJson.getProperty ("architecture", "").toString();
                if (arch.isNotEmpty())
                    archLabel = arch.containsIgnoreCase ("slimmable") ? "A2" : "A1";
            }
        }

        {
            const juce::ScopedLock sl (modelInfoLock);
            modelNames[lane] = file.getFileNameWithoutExtension();
            modelPaths[lane] = file.getFullPathName();
            modelExpectedSampleRates[lane] = lm->modelSampleRate;
            modelArchLabels[lane] = archLabel;
            loadError.clear();
        }

        const int latency = lm->latencySamples;

        if (auto* q = retiredModels[lane].exchange (nullptr); q != unloadSentinel())
            delete q;
        if (auto* q = pendingModels[lane].exchange (lm.release()); q != unloadSentinel())
            delete q;

        juce::MessageManager::callAsync ([this, latency]
        {
            setLatencySamples (juce::jmax (getLatencySamples(), latency));
        });

        loading.store (false);
    });
}

void GuitarRigNAMProcessor::setModelPair (int lane, const juce::File& normal, const juce::File& eco)
{
    if (lane < 0 || lane >= maxRigs)
        return;

    {
        const juce::ScopedLock sl (modelInfoLock);
        modelPathsStd[lane] = normal.getFullPathName();
        modelPathsEco[lane] = eco.existsAsFile() ? eco.getFullPathName() : juce::String();
    }

    const bool wantEco = pAmpEco->load() > 0.5f && eco.existsAsFile();
    loadModelAsync (lane, wantEco ? eco : normal);
}

void GuitarRigNAMProcessor::unloadModelLane (int lane)
{
    if (lane < 0 || lane >= maxRigs)
        return;

    {
        const juce::ScopedLock sl (modelInfoLock);
        modelNames[lane].clear();
        modelPaths[lane].clear();
        modelPathsStd[lane].clear();
        modelPathsEco[lane].clear();
        modelArchLabels[lane].clear();
        modelExpectedSampleRates[lane] = -1.0;
    }

    // the audio thread drops the lane on the next block; nothing to free there
    if (auto* q = pendingModels[lane].exchange (unloadSentinel()); q != unloadSentinel())
        delete q;
}

int GuitarRigNAMProcessor::firstFreeModelLane() const
{
    const int count = getRigCount();
    const juce::ScopedLock sl (modelInfoLock);
    for (int r = 0; r < count; ++r)
        if (modelPaths[r].isEmpty())
            return r;
    return -1;
}

int GuitarRigNAMProcessor::getRigCount() const
{
    return juce::jlimit (1, (int) maxRigs, (int) pCabCount->load());
}

bool GuitarRigNAMProcessor::isModelFileLoaded (const juce::String& fullPath) const
{
    const int count = getRigCount();
    const juce::ScopedLock sl (modelInfoLock);
    for (int r = 0; r < count; ++r)
        if (modelPaths[r] == fullPath)
            return true;
    return false;
}

juce::String GuitarRigNAMProcessor::getModelPathNormal (int lane) const
{
    if (lane < 0 || lane >= maxRigs)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return modelPathsStd[lane];
}

juce::String GuitarRigNAMProcessor::getModelPathEco (int lane) const
{
    if (lane < 0 || lane >= maxRigs)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return modelPathsEco[lane];
}

juce::String GuitarRigNAMProcessor::getModelArchLabel (int lane) const
{
    if (lane < 0 || lane >= maxRigs)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return modelArchLabels[lane];
}

juce::String GuitarRigNAMProcessor::getModelName (int lane) const
{
    if (lane < 0 || lane >= maxRigs)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return modelNames[lane];
}

juce::String GuitarRigNAMProcessor::getModelPath (int lane) const
{
    if (lane < 0 || lane >= maxRigs)
        return {};
    const juce::ScopedLock sl (modelInfoLock);
    return modelPaths[lane];
}

juce::String GuitarRigNAMProcessor::getLoadError() const
{
    const juce::ScopedLock sl (modelInfoLock);
    return loadError;
}

double GuitarRigNAMProcessor::getModelExpectedSampleRate (int lane) const
{
    if (lane < 0 || lane >= maxRigs)
        return -1.0;
    const juce::ScopedLock sl (modelInfoLock);
    return modelExpectedSampleRates[lane];
}

//==============================================================================
void GuitarRigNAMProcessor::loadIrAsync (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= maxCabSlots || ! file.existsAsFile())
        return;

    // JUCE PITFALL: Convolution::Normalise::yes normalizes the IR to a
    // total energy of 0.125 (= -18 dB!) - the cab came out WAY too quiet. We do
    // the UNIT-ENERGY normalization (0 dB of energy) ourselves and
    // load with Normalise::no; the Convolution still resamples and swaps
    // RT-safe internally.
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return;

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                   (juce::int64) (reader->sampleRate * 4));
    juce::AudioBuffer<float> ir (1, len);
    reader->read (&ir, 0, len, 0, true, false);

    double sum = 0.0;
    const float* d = ir.getReadPointer (0);
    for (int i = 0; i < len; ++i)
        sum += (double) d[i] * d[i];
    if (sum > 1.0e-12)
        ir.applyGain ((float) (1.0 / std::sqrt (sum)));

    convolutions[slot].loadImpulseResponse (std::move (ir), reader->sampleRate,
                                            juce::dsp::Convolution::Stereo::no,
                                            juce::dsp::Convolution::Trim::yes,
                                            juce::dsp::Convolution::Normalise::no);
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
juce::ValueTree GuitarRigNAMProcessor::captureState (bool includeExtPluginState)
{
    auto state = apvts.copyState();
    for (int r = 0; r < maxRigs; ++r)
    {
        // lane 1 keeps the legacy keys (no number)
        const auto suffix = r == 0 ? juce::String() : juce::String (r + 1);
        state.setProperty (kStateModelPath + suffix, getModelPath (r), nullptr);
        state.setProperty ("modelPathStd" + suffix, getModelPathNormal (r), nullptr);
        state.setProperty ("modelPathEco" + suffix, getModelPathEco (r), nullptr);
    }
    for (int s = 0; s < maxCabSlots; ++s)
        state.setProperty ("irPath" + juce::String (s + 1), getIrPath (s), nullptr);
    state.setProperty ("chainOrder", getChainOrder().joinIntoString (","), nullptr);
    state.setProperty (kStatePresetName, getCurrentPresetName(), nullptr);

    // external VST3 slots: path + plugin internal state (base64);
    // slot 1 keeps the legacy keys (no number)
    for (int s = 0; s < maxExtSlots; ++s)
    {
        const auto suffix = s == 0 ? juce::String() : juce::String (s + 1);
        state.setProperty ("extPluginPath" + suffix, getExternalPluginPath (s), nullptr);
        if (includeExtPluginState)
            if (auto* inst = extUiInstance[s].load(); inst != nullptr && extLoaded[s].load())
            {
                juce::MemoryBlock blob;
                inst->getStateInformation (blob);
                if (blob.getSize() > 0)
                    state.setProperty ("extPluginState" + suffix,
                                       blob.toBase64Encoding(), nullptr);
            }
    }

    // drums: bar timeline (v4) + transport + sound source
    {
        // apvts.replaceState (in applyState) carries the previous state's drum
        // bar properties into this copy - strip them so a shrunk arrangement
        // can't leave orphan bars that later desync the section count.
        for (int b = 0; b < drum::maxBars; ++b)
        {
            const auto sfx = juce::String (b + 1);
            state.removeProperty ("drumBar" + sfx, nullptr);
            state.removeProperty ("drumBarName" + sfx, nullptr);
            state.removeProperty ("drumMeter" + sfx, nullptr);
            state.removeProperty ("drumBarRole" + sfx, nullptr);
        }
        // also drop legacy v1/v2 keys so they can't shadow the v4 timeline on
        // the next load (they were carried over by apvts.replaceState)
        state.removeProperty ("drumPattern", nullptr);
        for (int i = 0; i < drum::maxSections * 2; ++i)
            state.removeProperty ("drumSecPattern" + juce::String (i + 1), nullptr);

        const int nSec = juce::jlimit (1, drum::maxSections, drumEngine.numSections.load());
        state.setProperty ("drumNumSections", nSec, nullptr);
        for (int b = 0; b < nSec * drum::barsPerSection; ++b)
        {
            const auto sfx = juce::String (b + 1);
            // meter (only saved if not 4/4, to avoid bloating the preset)
            if (drumEngine.barNum[b].load() > 0 && ! (drumEngine.meterNum (b) == 4
                                                      && drumEngine.meterDen (b) == 4))
                state.setProperty ("drumMeter" + sfx,
                                   juce::String (drumEngine.meterNum (b)) + "/"
                                       + juce::String (drumEngine.meterDen (b)), nullptr);
            if (drumEngine.barRole[b] > 0)   // 0 = auto (not saved)
                state.setProperty ("drumBarRole" + sfx, drumEngine.barRole[b], nullptr);
            if (drumEngine.barUsed[b].load())
            {
                state.setProperty ("drumBar" + sfx, drumEngine.barToString (b), nullptr);
                if (drumEngine.barNames[b].isNotEmpty())
                    state.setProperty ("drumBarName" + sfx, drumEngine.barNames[b], nullptr);
            }
        }
    }
    state.setProperty ("drumBpm", drumEngine.bpm.load(), nullptr);
    state.setProperty ("drumSwing", drumEngine.swingPct.load(), nullptr);
    state.setProperty ("drumLevel", drumEngine.level.load(), nullptr);
    state.setProperty ("drumHumVel", drumEngine.humanVel.load(), nullptr);
    state.setProperty ("drumHumTime", drumEngine.humanTime.load(), nullptr);
    state.setProperty ("drumHumRR", drumEngine.humanRR.load(), nullptr);
    state.setProperty ("drumClick", drumEngine.clickOn.load(), nullptr);
    state.setProperty ("drumCountIn", drumEngine.countInOn.load(), nullptr);
    state.setProperty ("drumHostSync", drumHostSync.load(), nullptr);
    state.setProperty ("drumUseVst", drumEngine.useVst.load(), nullptr);
    state.setProperty ("drumVstPath", getDrumPluginPath(), nullptr);
    if (includeExtPluginState)
        if (auto* inst = drumUiInstance.load(); inst != nullptr && drumLoaded.load())
        {
            juce::MemoryBlock blob;
            inst->getStateInformation (blob);
            if (blob.getSize() > 0)
                state.setProperty ("drumVstState", blob.toBase64Encoding(), nullptr);
        }

    // scenes (vNext F6): strip stale keys (replaceState carryover) and write
    // the live snapshots - the member array is the single source of truth
    for (int i = 0; i < drum::maxSections; ++i)
    {
        const auto key = "sceneRig" + juce::String (i + 1);
        state.removeProperty (key, nullptr);
        if (sceneXml[i].isNotEmpty())
            state.setProperty (key, sceneXml[i], nullptr);
    }
    state.setProperty ("scenesOn", scenesOn.load(), nullptr);

    return state;
}

void GuitarRigNAMProcessor::applyState (juce::ValueTree state)
{
    if (! state.isValid())
        return;

    apvts.replaceState (state);

    // per lane: ECO pair (new format has modelPathStd/Eco; legacy only
    // modelPath). Lane 1 uses the legacy keys without a number.
    for (int r = 0; r < maxRigs; ++r)
    {
        const auto suffix = r == 0 ? juce::String() : juce::String (r + 1);
        const juce::File modelFile (state.getProperty (kStateModelPath + suffix, "").toString());
        const juce::File stdFile (state.getProperty ("modelPathStd" + suffix,
                                                     modelFile.getFullPathName()).toString());
        const juce::File ecoFile (state.getProperty ("modelPathEco" + suffix, "").toString());
        {
            const juce::ScopedLock sl (modelInfoLock);
            modelPathsStd[r] = stdFile.existsAsFile() ? stdFile.getFullPathName() : juce::String();
            modelPathsEco[r] = ecoFile.existsAsFile() ? ecoFile.getFullPathName() : juce::String();
        }
        if (modelFile.existsAsFile())
        {
            // scenes/A-B switch the state often: same model already in the
            // lane -> skip the reload (it would glitch and waste a worker pass)
            if (modelFile.getFullPathName() != getModelPath (r))
                loadModelAsync (r, modelFile);
        }
        else if (state.hasProperty ("modelPathStd" + suffix) && getModelPath (r).isNotEmpty())
        {
            // new-format state with an explicitly empty lane: honor it
            unloadModelLane (r);
        }
    }

    for (int s = 0; s < maxCabSlots; ++s)
    {
        // "irPath" without a number = old format (single slot) -> slot 1
        const auto key = s == 0 && ! state.hasProperty ("irPath1")
                             ? juce::String (kStateIrPath)
                             : "irPath" + juce::String (s + 1);
        const juce::File irFile (state.getProperty (key, "").toString());
        if (irFile.existsAsFile() && irFile.getFullPathName() != getIrPath (s))
            loadIrAsync (s, irFile);
    }

    setChainOrder (juce::StringArray::fromTokens (
        state.getProperty ("chainOrder", "gate,od,amp,eq,delay,reverb").toString(), ",", ""));

    // external VST3 slots: reload (with the saved state) or clear;
    // slot 1 uses the legacy keys without a number
    for (int s = 0; s < maxExtSlots; ++s)
    {
        const auto suffix = s == 0 ? juce::String() : juce::String (s + 1);
        const juce::File extFile (state.getProperty ("extPluginPath" + suffix, "").toString());
        if (extFile.exists())
        {
            juce::MemoryBlock blob;
            const auto b64 = state.getProperty ("extPluginState" + suffix, "").toString();
            if (b64.isNotEmpty())
                blob.fromBase64Encoding (b64);

            if (extFile.getFullPathName() == getExternalPluginPath (s) && hasExternalPlugin (s))
            {
                // same plugin already loaded: just re-applies the state
                if (auto* inst = extUiInstance[s].load(); inst != nullptr && blob.getSize() > 0)
                    inst->setStateInformation (blob.getData(), (int) blob.getSize());
            }
            else
            {
                loadExternalPluginAsync (s, extFile, blob.getSize() > 0 ? &blob : nullptr);
            }
        }
        else if (hasExternalPlugin (s))
        {
            clearExternalPlugin (s);
        }
    }

    // drums (old presets don't have the keys - keeps what's there)
    if (state.hasProperty ("drumNumSections") || state.hasProperty ("drumPattern"))
    {
        for (int b = 0; b < drum::maxBars; ++b)
        {
            drumEngine.clearBar (b);
            drumEngine.barNames[b].clear();
            drumEngine.barRole[b] = 0;
        }

        if (state.hasProperty ("drumSecPattern1") && ! state.hasProperty ("drumBar1"))
        {
            // v2 format (2-bar sections, 288 digits): each old section
            // becomes 2 consecutive bars in the timeline. Only when there is NO
            // v4 timeline present - a state can carry BOTH (stale drumSecPattern
            // left behind by apvts.replaceState) and the current v4 must win.
            const int oldSec = juce::jlimit (1, drum::maxSections,
                                             (int) state.getProperty ("drumNumSections", 1));
            for (int i = 0; i < oldSec; ++i)
            {
                const auto str = state.getProperty ("drumSecPattern" + juce::String (i + 1),
                                                    "").toString();
                if (str.length() >= 288 && 2 * i + 1 < drum::maxBars)
                {
                    // de-interleaves: v2 stored [voice][32 steps]
                    juce::String bar1, bar2;
                    bar1.preallocateBytes (150);
                    bar2.preallocateBytes (150);
                    for (int v = 0; v < drum::numVoices; ++v)
                    {
                        bar1 << str.substring (v * 32, v * 32 + 16);
                        bar2 << str.substring (v * 32 + 16, v * 32 + 32);
                    }
                    drumEngine.barFromString (bar1, 2 * i);
                    drumEngine.barFromString (bar2, 2 * i + 1);
                }
            }
            drumEngine.numSections.store (juce::jlimit (1, drum::maxSections,
                (2 * oldSec + drum::barsPerSection - 1) / drum::barsPerSection));
        }
        else if (state.hasProperty ("drumBar1") || state.hasProperty ("drumNumSections"))
        {
            // v4 format (bar timeline). Honor the highest bar actually present,
            // not just drumNumSections: an older save could desync the two and
            // drop whole sections (drumNumSections=1 while drumBar5..8 exist).
            const int savedSec = juce::jlimit (1, drum::maxSections,
                                               (int) state.getProperty ("drumNumSections", 1));
            int highestBar = 0;
            for (int b = 0; b < drum::maxBars; ++b)
                if (state.hasProperty ("drumBar" + juce::String (b + 1)))
                    highestBar = b + 1;
            const int fromBars = (highestBar + drum::barsPerSection - 1) / drum::barsPerSection;
            const int nSec = juce::jlimit (1, drum::maxSections, juce::jmax (savedSec, fromBars));
            drumEngine.numSections.store (nSec);
            for (int b = 0; b < nSec * drum::barsPerSection; ++b)
            {
                const auto sfx = juce::String (b + 1);
                // meter BEFORE the pattern (barFromString uses barSteps)
                const auto mt = state.getProperty ("drumMeter" + sfx, "4/4").toString();
                drumEngine.setMeter (b, mt.upToFirstOccurrenceOf ("/", false, false).getIntValue(),
                                     mt.fromFirstOccurrenceOf ("/", false, false).getIntValue());
                drumEngine.barFromString (state.getProperty ("drumBar" + sfx, "").toString(), b);
                drumEngine.barNames[b] =
                    state.getProperty ("drumBarName" + sfx, "").toString();
                drumEngine.barRole[b] = (int) state.getProperty ("drumBarRole" + sfx, 0);
            }
        }
        else
        {
            // v1 format: one 2-bar pattern -> bars 1 and 2
            const auto str = state.getProperty ("drumPattern", "").toString();
            if (str.length() >= 288)
            {
                juce::String bar1, bar2;
                for (int v = 0; v < drum::numVoices; ++v)
                {
                    bar1 << str.substring (v * 32, v * 32 + 16);
                    bar2 << str.substring (v * 32 + 16, v * 32 + 32);
                }
                drumEngine.barFromString (bar1, 0);
                drumEngine.barFromString (bar2, 1);
            }
            drumEngine.numSections.store (1);
        }
        drumEngine.bpm.store ((float) (double) state.getProperty ("drumBpm", 104.0));
        drumEngine.swingPct.store ((float) (double) state.getProperty ("drumSwing", 0.0));
        drumEngine.level.store ((float) (double) state.getProperty ("drumLevel", 0.8));
        drumEngine.humanVel.store ((float) (double) state.getProperty ("drumHumVel", 0.3));
        drumEngine.humanTime.store ((float) (double) state.getProperty ("drumHumTime", 0.2));
        drumEngine.humanRR.store ((float) (double) state.getProperty ("drumHumRR", 0.4));
        drumEngine.clickOn.store ((bool) state.getProperty ("drumClick", false));
        drumEngine.countInOn.store ((bool) state.getProperty ("drumCountIn", false));
        drumHostSync.store ((bool) state.getProperty ("drumHostSync", true));

        const juce::File drumFile (state.getProperty ("drumVstPath", "").toString());
        const bool wantVst = (bool) state.getProperty ("drumUseVst", false);
        if (drumFile.exists())
        {
            juce::MemoryBlock blob;
            const auto b64 = state.getProperty ("drumVstState", "").toString();
            if (b64.isNotEmpty())
                blob.fromBase64Encoding (b64);
            if (drumFile.getFullPathName() == getDrumPluginPath() && hasDrumPlugin())
            {
                if (auto* inst = drumUiInstance.load(); inst != nullptr && blob.getSize() > 0)
                    inst->setStateInformation (blob.getData(), (int) blob.getSize());
                drumEngine.useVst.store (wantVst);
            }
            else
            {
                loadDrumPluginAsync (drumFile, blob.getSize() > 0 ? &blob : nullptr);
                drumEngine.useVst.store (wantVst);
            }
        }
        else
            drumEngine.useVst.store (false);
    }

    // scenes (vNext F6): restore the per-section snapshots - except when THIS
    // apply is a scene landing (the scene tree deliberately carries none)
    if (! applyingSceneNow)
    {
        scenesOn.store ((bool) state.getProperty ("scenesOn", false));
        for (int i = 0; i < drum::maxSections; ++i)
            sceneXml[i] = state.getProperty ("sceneRig" + juce::String (i + 1), "").toString();
    }

    if (state.hasProperty (kStatePresetName))
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
                   .getChildFile ("PedalForge NAM")
                   .getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

// Factory presets: parameters only (no capture/IR - they keep whatever is
// loaded). Created once, when the folder is empty.
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
    // without the external plugin blob: runs at 2 Hz and getStateInformation of
    // a guest can be costly (its internal changes don't dirty the preset)
    auto state = captureState (false);
    state.removeProperty ("tunerOn", nullptr); // UI preference, doesn't dirty the preset
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
        // the baseline consolidates when the async model/IR load finishes
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
