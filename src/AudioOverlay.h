#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>

class GuitarRigNAMProcessor;

//==============================================================================
// Audio & MIDI settings overlay (mockup: docs/design/pedalforge-vnext-complete
// .html, #audio screen). Device changes are STAGED: the selects edit a pending
// configuration; APPLY CHANGES pushes it to the AudioDeviceManager in one shot
// (type first, then setup) and CANCEL reverts the selects. While nothing is
// applied the running audio continues uninterrupted.
//
// Standalone: the editor passes the StandalonePluginHolder's deviceManager.
// VST3: pass nullptr - the selects are replaced by a "managed by the host"
// notice while the live meters and the health column keep working.
class AudioOverlay : public juce::Component,
                     private juce::Timer
{
public:
    AudioOverlay (GuitarRigNAMProcessor&, juce::AudioDeviceManager* adm);
    ~AudioOverlay() override;

    /// Shows the overlay (setVisible + toFront). Closes with the X or ESC.
    void open();
    /// Optional: notified when the overlay closes itself (X / ESC).
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    //==========================================================================
    // Staged device configuration (nothing touches the manager until Apply).
    struct PendingSetup
    {
        juce::String type, device;
        double sampleRate = 0.0;
        int bufferSize = 0;
        int inputChannel = -1;   // hardware input index; -1 = device default
        int outputPair = -1;     // stereo pair index (0 = outs 1+2); -1 = default

        bool operator== (const PendingSetup& o) const
        {
            return type == o.type && device == o.device
                && sampleRate == o.sampleRate && bufferSize == o.bufferSize
                && inputChannel == o.inputChannel && outputPair == o.outputPair;
        }
        bool operator!= (const PendingSetup& o) const { return ! (*this == o); }
    };

    void timerCallback() override;
    void close();

    PendingSetup snapshotFromManager() const;
    void captureCurrent();               // applied = pending = live snapshot
    bool isDirty() const                 { return pending != applied; }
    bool pendingMatchesOpenDevice() const;
    juce::AudioIODeviceType* findTypeObject (const juce::String& name) const;

    void rebuildAll();
    void rebuildDriverBox();
    void rebuildDeviceBox();
    void rebuildRateAndBufferBoxes();
    void rebuildChannelBoxes();
    void applyPending();
    void cancelPending();
    void updateActionButtons();
    void refreshHealth();

    void styleCombo (juce::ComboBox&);
    std::array<juce::ComboBox*, 6> comboGrid();
    static juce::String formatSampleRate (double);
    juce::String formatBuffer (int numSamples) const;
    juce::String formatDb (float db) const;

    GuitarRigNAMProcessor& processor;
    juce::AudioDeviceManager* deviceManager;   // nullptr = host-managed (VST3)
    const bool hostMode;

    PendingSetup pending, applied;
    juce::String bannerError;                  // last Apply error ("" = none)
    bool rebuilding = false;                   // guards the combo callbacks

    // header / actions
    juce::TextButton closeButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")) };
    juce::TextButton cancelBtn { "CANCEL" }, applyBtn { "APPLY CHANGES" };
    juce::TextButton panelBtn { "OPEN DRIVER CONTROL PANEL" };

    // Device configuration selects (labels are painted above each combo)
    juce::ComboBox driverBox, deviceBox, rateBox, bufferBox, inputChanBox, outputChanBox;
    juce::Array<double> rateValues;
    juce::Array<int> bufferValues;
    juce::StringArray inputChanNames;
    int numOutputPairs = 0;

    // live telemetry (10 Hz timer)
    float inMeterDb = -80.0f, outMeterDb = -80.0f;
    int signalHoldTicks = 0;         // > 0 while input signal was seen recently
    bool deviceOnline = false;
    int xrunCount = -1;              // -1 = device does not report (row omitted)
    double latencyOutMs = 0.0, latencyRoundMs = 0.0;
    juce::String liveTypeName;       // device type actually running

    // layout rects shared between resized() and paint()
    juce::Rectangle<int> headerRect, tagRect, settingsRect, channelRect,
                         healthRect, actionsRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioOverlay)
};
