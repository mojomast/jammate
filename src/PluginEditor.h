#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "StoreOverlay.h"

//==============================================================================
// Slider rotativo com "trava" no valor default (snap quando o arrasto passa
// perto dele, como um detent físico).
class SnapSlider : public juce::Slider
{
public:
    double snapTarget = 0.0, snapRadius = 0.0;

    double snapValue (double attemptedValue, DragMode dragMode) override
    {
        if (dragMode != notDragging && snapRadius > 0.0
            && std::abs (attemptedValue - snapTarget) < snapRadius)
            return snapTarget;
        return attemptedValue;
    }
};

//==============================================================================
// Knob + label + valor, conforme Knob.dc.html v2 (gauge accent).
class KnobComponent : public juce::Component
{
public:
    KnobComponent (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                   const juce::String& labelText,
                   std::function<juce::String (float)> formatter);

    void setKnobTooltip (const juce::String&);
    void resized() override;

private:
    void updateValueText();

    SnapSlider slider;
    juce::Label nameLabel, valueLabel;
    std::function<juce::String (float)> format;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
// LED de bypass (aceso = módulo ativo), clicável.
class LedButton : public juce::Button
{
public:
    LedButton() : juce::Button ("bypass") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool, bool) override;
};

//==============================================================================
// Medidor horizontal IN/OUT/CPU do top bar.
class LevelMeter : public juce::Component
{
public:
    /// modo nível (dB) — fração calculada de -60..0
    void setLevel (float newLevelDb);
    /// modo fração direta (CPU)
    void setFraction (float f, juce::Colour c);
    void paint (juce::Graphics&) override;

private:
    float fraction = 0.0f;
    bool solid = false;
    juce::Colour solidColour;
};

//==============================================================================
// Pill do preset no top bar (dot + texto centrado), clicável.
class PillButton : public juce::Button
{
public:
    PillButton() : juce::Button ("preset") {}
    void paintButton (juce::Graphics&, bool, bool) override;

    bool dotLit = false;
};

//==============================================================================
// A cadeia de sinal rolável: Input → Gate → OD → Amp → Cab → EQ → Delay →
// Reverb → Output, com cartões nas métricas do design v2.
class ChainView : public juce::Component
{
public:
    ChainView (GuitarRigNAMProcessor&, std::function<void()> onLoadModel,
               std::function<void()> onLoadIr);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setAmpImage (juce::Image);
    void setCabImage (juce::Image);
    void refreshDynamicText();

    static constexpr int chainHeight = 580;

private:
    void drawPedalFrame (juce::Graphics&, juce::Rectangle<int>, const juce::String& title,
                         const juce::String& footer);
    void drawPhoto (juce::Graphics&, const juce::Image&, juce::Rectangle<int>);

    GuitarRigNAMProcessor& processor;

    juce::Rectangle<int> ioInB, gateB, odB, ampB, cabB, eqB, delayB, revB, ioOutB;
    juce::Image ampImage, cabImage;

    // knobs / LEDs / botões
    std::unique_ptr<KnobComponent> inputKnob, outputKnob;
    LedButton gateLed, odLed, ampLed, cabLed, eqLed, delayLed, revLed;
    std::unique_ptr<KnobComponent> gateThreshKnob, gateReleaseKnob;
    std::unique_ptr<KnobComponent> odDriveKnob, odToneKnob, odLevelKnob;
    std::unique_ptr<KnobComponent> ampGainKnob, ampBassKnob, ampMidKnob,
        ampTrebleKnob, ampPresKnob, ampMasterKnob;
    std::unique_ptr<KnobComponent> cabLevelKnob, cabAirKnob;
    std::unique_ptr<KnobComponent> eqLowKnob, eqMidKnob, eqHighKnob;
    std::unique_ptr<KnobComponent> delayTimeKnob, delayFbKnob, delayMixKnob;
    std::unique_ptr<KnobComponent> revDecayKnob, revMixKnob, revPreKnob;
    juce::TextButton loadButton { "TROCAR CAPTURE NAM" };
    juce::TextButton irButton { "TROCAR IR" };

    using Attachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<Attachment> gateAtt, odAtt, ampAtt, cabAtt, eqAtt, delayAtt, revAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainView)
};

//==============================================================================
// Canvas lógico fixo 1100×700 escalado pelo editor.
class RigContent : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int designWidth = 1100;
    static constexpr int designHeight = 700;

    explicit RigContent (GuitarRigNAMProcessor&);
    ~RigContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void analyseTuner();
    void refreshSidecarImages();
    void chooseModelFile();
    void chooseIrFile();
    void saveCurrentPreset();
    void beginPresetNameEdit();
    void showPresetMenu();
    void toggleTuner();

    GuitarRigNAMProcessor& processor;
    RigLookAndFeel lookAndFeel;

    // top bar
    LevelMeter inMeter, outMeter, cpuMeter;
    juce::TextButton audioButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x81udio")) };
    juce::TextButton storeButton { "Tone Store" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::TextButton saveButton { "SALVAR" };
    PillButton presetPill;
    juce::TextEditor presetNameEditor;   // edição inline do nome (sem diálogo)
    int saveFlashTicks = 0;              // feedback "Salvo" no botão
    bool focusGrabbed = false;
    bool presetDirtyCached = false;
    juce::TooltipWindow tooltipWindow { this, 600 };

    // cadeia
    juce::Viewport chainViewport;
    std::unique_ptr<ChainView> chainView;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<StoreOverlay> storeOverlay;

    // afinador
    juce::TextButton tunerToggle { "AFINADOR" };
    bool isTunerOn() const;
    double tunerFreq = -1.0;
    double tunerCents = 0.0;
    juce::String tunerNote;
    int tunerStringIndex = -1;
    int tunerTick = 0;

    // sidecars de imagem
    juce::String loadedModelPath, loadedIrPath;
    bool ampImageLoaded = false, cabImageLoaded = false;

    float inMeterDb = -80.0f, outMeterDb = -80.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigContent)
};

//==============================================================================
class GuitarRigNAMEditor : public juce::AudioProcessorEditor
{
public:
    explicit GuitarRigNAMEditor (GuitarRigNAMProcessor&);

    void resized() override;

private:
    RigContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMEditor)
};
