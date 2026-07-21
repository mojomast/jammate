#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LookAndFeel.h"
#include "PluginProcessor.h"

//==============================================================================
// Knob + label + valor, conforme Knob.dc.html.
class KnobComponent : public juce::Component
{
public:
    KnobComponent (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                   const juce::String& labelText,
                   std::function<juce::String (float)> formatter);

    void resized() override;

private:
    void updateValueText();

    juce::Slider slider;
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
// Medidor horizontal IN/OUT do top bar.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float newLevelDb);
    void paint (juce::Graphics&) override;

private:
    float levelDb = -80.0f;
};

//==============================================================================
// Todo o conteúdo da UI num canvas lógico fixo de 1100×700 (o design é
// pixel-perfect nesse tamanho); o editor escala este componente via transform
// para caber em qualquer tela/tamanho de janela.
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

private:
    void timerCallback() override;
    void chooseModelFile();

    // geometria dos cartões da cadeia (usada em paint e resized)
    juce::Rectangle<int> inputCardBounds, ampCardBounds, outputCardBounds;
    juce::Rectangle<int> preSlotBounds, postSlotBounds;

    GuitarRigNAMProcessor& processor;
    RigLookAndFeel lookAndFeel;

    // top bar
    LevelMeter inMeter, outMeter;
    juce::TextButton audioButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x81udio")) };
    juce::TextButton storeButton { "Tone Store" };

    // amp
    LedButton ampLed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ampLedAttachment;
    juce::TextButton loadButton { "CARREGAR CAPTURE NAM" };

    // knobs
    std::unique_ptr<KnobComponent> inputKnob, outputKnob;

    std::unique_ptr<juce::FileChooser> fileChooser;

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
