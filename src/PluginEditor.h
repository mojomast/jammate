#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class GuitarRigNAMEditor : public juce::AudioProcessorEditor
{
public:
    explicit GuitarRigNAMEditor (GuitarRigNAMProcessor&);
    ~GuitarRigNAMEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMEditor)
};
