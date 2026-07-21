#include "PluginEditor.h"

GuitarRigNAMEditor::GuitarRigNAMEditor (GuitarRigNAMProcessor& p)
    : AudioProcessorEditor (p)
{
    titleLabel.setText ("GuitarRig NAM — Fase 0 (passthrough)",
                        juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::FontOptions (20.0f));
    addAndMakeVisible (titleLabel);

    setSize (500, 300);
}

void GuitarRigNAMEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void GuitarRigNAMEditor::resized()
{
    titleLabel.setBounds (getLocalBounds());
}
