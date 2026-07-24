#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "StoreOverlay.h"

//==============================================================================
// Rotary slider with a "detent" at the default value (snaps when the drag
// passes near it, like a physical detent).
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
// Knob + label + value, per Knob.dc.html v2 (gauge accent).
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
// Bypass LED (lit = module active), clickable.
class LedButton : public juce::Button
{
public:
    LedButton() : juce::Button ("bypass") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool, bool) override;
};

//==============================================================================
// Horizontal IN/OUT/CPU meter in the top bar.
class LevelMeter : public juce::Component
{
public:
    /// level mode (dB) - fraction computed from -60..0, with peak-hold
    void setLevel (float newLevelDb);
    /// direct fraction mode (CPU)
    void setFraction (float f, juce::Colour c);
    void paint (juce::Graphics&) override;

private:
    float fraction = 0.0f;
    bool solid = false;
    juce::Colour solidColour;
    float peakFrac = 0.0f; // peak marker (holds ~1.5 s then decays)
    int peakHoldTicks = 0;
};

//==============================================================================
// Preset pill in the top bar (dot + centered text), clickable.
class PillButton : public juce::Button
{
public:
    PillButton() : juce::Button ("preset") {}
    void paintButton (juce::Graphics&, bool, bool) override;

    bool dotLit = false;
};

//==============================================================================
// The scrollable signal chain: Input -> Gate -> OD -> Amp -> Cab -> EQ -> Delay ->
// Reverb -> Output, with cards at the design v2 metrics.
class ChainView : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    ChainView (GuitarRigNAMProcessor&, std::function<void (int)> onLoadModel,
               std::function<void (int)> onLoadIr,
               std::function<void (int)> onLoadExtPlugin,
               std::function<void (int)> onOpenExtPluginUi);

    void paint (juce::Graphics&) override;
    void resized() override;

    // reordering drag-and-drop (rigs+mixer are a fixed anchor) + pan via the
    // background + mouse wheel scrolling the chain
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // file drag-and-drop: .nam on the amp, IR on the cab, .vst3 on the slot
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

    void setAmpImage (int lane, juce::Image);
    void setCabImage (int lane, juce::Image);
    void refreshDynamicText();
    juce::Component& getLoadButton (int lane) { return loadButtons[lane]; }

    // amp-card "variations" click -> host opens the Tone Store details for the
    // tone, targeting this lane so the pick replaces the current capture.
    std::function<void (int lane, int toneId)> onShowVariations;

    static constexpr int chainHeight = 580;

private:
    static constexpr int maxRigs = GuitarRigNAMProcessor::maxRigs;

    int rigBlockWidth() const;
    void updateLayout();

    // chain entries in visual order (effect or amp+cabs block)
    struct ChainEntry
    {
        juce::String id;             // "gate".."reverb" or "amp"
        juce::Rectangle<int> box;    // amp: union amp..cabs
    };
    std::vector<ChainEntry> orderedEntries() const;
    juce::Rectangle<int> boxForFx (const juce::String& id) const;
    int effectCardWidth (const juce::String& id) const;

    // drag state
    juce::String draggingId;
    int dragGrabDx = 0;
    float dragMouseX = -1.0f;
    int dropIndex = -1;
    juce::String lastOrderSeen;      // relayout when the order changes via preset

    // effects drawer: the chain shows only what is in use
    juce::Rectangle<int> addFxB;     // "+ EFFECT" button at the end of the chain
    /// insertIndex >= 0 inserts at the exact position; -1 = canonical position
    void showAddFxMenu (int insertIndex, juce::Rectangle<int> targetArea);
    void removeFxFromChain (const juce::String& id);
    /// Immediate relayout (outside the timer) after changing the chain - the layout
    /// never lags behind under the user's mouse.
    void applyChainRelayout();
    /// "+" on the connectors between cards: {hotspot, insertion index}
    std::vector<std::pair<juce::Rectangle<int>, int>> insertSpots() const;
    juce::Array<juce::Component*> componentsForFx (const juce::String& id);
    juce::String onParamIdForFx (const juce::String& id) const;
    static juce::String fxDisplayName (const juce::String& id);
    /// "ext"->0, "ext2".."ext8"->1..7; -1 for any other id.
    static int extSlotForId (const juce::String& id);
    static juce::Rectangle<int> removeHotspot (juce::Rectangle<int> cardBox)
    {
        return { cardBox.getRight() - 12 - 18 - 6 - 14, cardBox.getY() + 12, 14, 14 };
    }

    // pan the chain by dragging the background
    bool panning = false;
    juce::Point<int> panStartMouse, panStartView;

    // microinteractions: hover on the "+" and "x"; file drop target
    juce::Rectangle<int> hoverHotspot;   // "+"/"x" under the mouse
    juce::Rectangle<int> dropHighlight;  // card targeted by the dragged file
    /// file destination at (x,y): {target rect, "nam:lane"/"ir:slot"/"vst3"}
    std::pair<juce::Rectangle<int>, juce::String> dropTargetAt (const juce::String& file,
                                                                int x, int y) const;

    // spectrum analyzer (card)
    juce::dsp::FFT anFft { 11 }; // 2048
    std::array<float, 4096> anFftBuf {};
    static constexpr int anNumBands = 24;
    float anBands[anNumBands] = {};

public:

private:
    void drawPedalFrame (juce::Graphics&, juce::Rectangle<int>, const juce::String& title,
                         const juce::String& footer);
    void drawPhoto (juce::Graphics&, const juce::Image&, juce::Rectangle<int>);

    GuitarRigNAMProcessor& processor;

    juce::Rectangle<int> ioInB, gateB, odB, eqB, delayB, revB, ioOutB;
    juce::Rectangle<int> compB, preEqB, pitchB, looperB, limB;
    juce::Rectangle<int> extB[GuitarRigNAMProcessor::maxExtSlots];
    juce::Rectangle<int> wahB, harmB, octB, rmB, bcB, sgB, excB, dsB, tapeB, cnsB, anB;
    // parallel rigs: one amp+cab pair per lane + the Mixer card that sums everything
    juce::Rectangle<int> ampLaneB[maxRigs], cabLaneB[maxRigs], mixerB;
    juce::Image ampImages[maxRigs], cabImages[maxRigs];
    juce::Image t3kMark;   // TONE3000 mark shown on store-loaded signal blocks

    // knobs / LEDs / buttons
    std::unique_ptr<KnobComponent> inputKnob, outputKnob;
    LedButton gateLed, odLed, ampLed, cabLed, eqLed, delayLed, revLed, compLed, preEqLed,
        pitchLed, looperLed, limLed;
    LedButton extLed[GuitarRigNAMProcessor::maxExtSlots];
    LedButton wahLed, harmLed, octLed, rmLed, bcLed, sgLed, excLed, dsLed, tapeLed, cnsLed,
        anLed;
    std::unique_ptr<KnobComponent> gateThreshKnob, gateReleaseKnob, gateHoldKnob;
    std::unique_ptr<KnobComponent> compSustainKnob, compAttackKnob, compBlendKnob, compLevelKnob;
    std::unique_ptr<KnobComponent> preEqLowKnob, preEqMidKnob, preEqHighKnob;
    juce::TextButton compPresetChips[3]; // Clean / Country / Lead

    // variation selectors (model/brand) on the cards
    juce::TextButton odTypeButton, compTypeButton, delayTypeButton, revTypeButton,
        modTypeButton, delayDivButton, pitchTypeButton;
    juce::TextButton wahModeButton, harmKeyButton, harmScaleButton, harmIntervalButton;
    void setupTypeButton (juce::TextButton&, const char* paramId, const juce::String& tooltip);
    void refreshTypeButtons();

    // Mod card + delay tap tempo
    juce::Rectangle<int> modB;
    LedButton modLed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modAtt;
    std::unique_ptr<KnobComponent> modRateKnob, modDepthKnob, modMixKnob;
    juce::TextButton tapButton { "TAP" };
    juce::int64 lastTapMs = 0;
    void applyTapTempo();
    std::unique_ptr<KnobComponent> odDriveKnob, odToneKnob, odLevelKnob;
    // amp PER LANE (its own knobs per rig)
    std::unique_ptr<KnobComponent> ampGainKnob[maxRigs], ampBassKnob[maxRigs],
        ampMidKnob[maxRigs], ampTrebleKnob[maxRigs], ampPresKnob[maxRigs],
        ampMasterKnob[maxRigs];
    juce::TextButton loadButtons[maxRigs];
    // "variations" selector on the amp card: lists the other captures of the
    // same TONE3000 tone (only shown when the loaded model came from the store).
    juce::TextButton ampVarButtons[maxRigs];
    int toneIdForLane (int lane) const;   // reads tone_id from the model .meta
    // cab PER LANE (LC/HC/phase/CHANGE); blend lives in the Mixer card
    std::unique_ptr<KnobComponent> cabAirKnob;
    std::unique_ptr<KnobComponent> cabBlendKnob[maxRigs]; // in the Mixer
    std::unique_ptr<KnobComponent> cabLcKnob[maxRigs];
    std::unique_ptr<KnobComponent> cabHcKnob[maxRigs];
    juce::TextButton cabPhaseChips[maxRigs];
    juce::TextButton cabIrButtons[maxRigs];
    // Mixer: sum of the lanes; +/- adds/removes an entire AMP+CAB pair
    juce::TextButton rigAddButton { "+" }, rigRemoveButton { "-" };
    int lastRigCount = 0;
    std::unique_ptr<KnobComponent> eqLowKnob, eqMidKnob, eqHighKnob;
    std::unique_ptr<KnobComponent> delayTimeKnob, delayFbKnob, delayMixKnob;
    std::unique_ptr<KnobComponent> revDecayKnob, revMixKnob, revPreKnob;
    // P3: pitch, looper and limiter
    std::unique_ptr<KnobComponent> pitchMixKnob, pitchLevelKnob;
    std::unique_ptr<KnobComponent> looperLevelKnob;
    std::unique_ptr<KnobComponent> limCeilKnob, limRelKnob;
    juce::TextButton looperRecButton, looperPlayButton, looperClearButton, looperExportButton;
    // external VST3 plugin slots (up to 3 in the chain)
    std::unique_ptr<KnobComponent> extMixKnob[GuitarRigNAMProcessor::maxExtSlots];
    juce::TextButton extLoadButton[GuitarRigNAMProcessor::maxExtSlots],
        extUiButton[GuitarRigNAMProcessor::maxExtSlots],
        extRemoveButton[GuitarRigNAMProcessor::maxExtSlots];
    // P4 cards (one effect per card)
    std::unique_ptr<KnobComponent> wahFreqKnob, wahRangeKnob, wahResKnob;
    std::unique_ptr<KnobComponent> sgSensKnob, sgRiseKnob;
    std::unique_ptr<KnobComponent> octSubKnob, octDirectKnob, octToneKnob;
    std::unique_ptr<KnobComponent> rmFreqKnob, rmMixKnob;
    std::unique_ptr<KnobComponent> bcBitsKnob, bcRateKnob, bcMixKnob;
    std::unique_ptr<KnobComponent> harmMixKnob, harmLevelKnob;
    std::unique_ptr<KnobComponent> excFreqKnob, excAmtKnob;
    std::unique_ptr<KnobComponent> dsFreqKnob, dsSensKnob, dsAmtKnob;
    std::unique_ptr<KnobComponent> tapeDriveKnob, tapeBumpKnob, tapeRollKnob;
    std::unique_ptr<KnobComponent> cnsAmtKnob;
    juce::TextButton ecoChip { "ECO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ecoAtt;

    // cache of the IRs' V1/V2 badge (read from the .meta sidecar)
    juce::String cabArchCache[GuitarRigNAMProcessor::maxCabSlots];
    juce::String cabArchPathSeen[GuitarRigNAMProcessor::maxCabSlots];
    juce::String archBadgeForIr (int slot);

    using Attachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<Attachment> gateAtt, odAtt, ampAtt, cabAtt, eqAtt, delayAtt, revAtt,
        compAtt, preEqAtt, pitchAtt, looperAtt, limAtt;
    std::unique_ptr<Attachment> extAtt[GuitarRigNAMProcessor::maxExtSlots];
    std::unique_ptr<Attachment> wahAtt, harmAtt, octAtt, rmAtt, bcAtt, sgAtt, excAtt,
        dsAtt, tapeAtt, cnsAtt, anAtt;
    std::unique_ptr<Attachment> cabPhaseAtt[GuitarRigNAMProcessor::maxCabSlots];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainView)
};

//==============================================================================
// Fixed logical canvas 1100x700 scaled by the editor.
class DrumOverlay;
class DrumRibbon;

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
    void chooseModelFile (int lane);
    void chooseModelSource (int lane);   // menu: TONE3000 store or local disk
    void chooseIrFile (int slot);
    void chooseExtPluginFile (int slot);
    void openExtPluginWindow (int slot);
    void closeExtPluginWindow (int slot);
    void closeAllExtPluginWindows();
    void chooseDrumVstFile();
    void openDrumVstWindow();
    void closeDrumVstWindow();
    void saveCurrentPreset();
    void beginPresetNameEdit();
    void showPresetMenu();
    void toggleTuner();

    GuitarRigNAMProcessor& processor;
    RigLookAndFeel lookAndFeel;

    // top bar
    LevelMeter inMeter, outMeter, cpuMeter;
    juce::TextButton audioButton { "Audio" };
    juce::TextButton storeButton { "Tone Store" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::TextButton saveButton { "SAVE" };
    PillButton presetPill;
    juce::TextEditor presetNameEditor;   // inline name editing (no dialog)
    int saveFlashTicks = 0;              // "Saved" feedback on the button
    bool focusGrabbed = false;
    bool presetDirtyCached = false;
    juce::TooltipWindow tooltipWindow { this, 600 };

    // chain
    juce::Viewport chainViewport;
    std::unique_ptr<ChainView> chainView;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<StoreOverlay> storeOverlay;

    // Drums module (overlay + drum VST panel window)
    std::unique_ptr<DrumOverlay> drumOverlay;
    std::unique_ptr<DrumRibbon> drumRibbon;   // top ribbon (follow along with the drums)
    juce::TextButton drumButton { "Drums" };
    std::unique_ptr<juce::DocumentWindow> drumVstWindow;

    // floating windows with the hosted VST3 plugins' panels
    std::unique_ptr<juce::DocumentWindow> extWindow[GuitarRigNAMProcessor::maxExtSlots];

    // tuner
    juce::TextButton tunerToggle { "TUNER" };
    bool isTunerOn() const;

    // performance mode (stage): hides the chain, shows the essentials large
    bool perfMode = false;
    juce::TextButton perfChip { "STAGE" };
    void setPerfMode (bool shouldBeOn);
    void paintPerformanceView (juce::Graphics&);

    // tuner mute (silences the output while tuning)
    juce::TextButton muteChip { "MUTE" };
    bool tunerMuteWanted = false;

    // quick recorder (output WAV) + rig A/B
    juce::TextButton recChip { juce::CharPointer_UTF8 ("\xe2\x97\x8f REC") };
    juce::int64 recStartMs = 0;
    int recSavedTicks = 0;
    juce::TextButton abButton { "A" };

    // auto-ECO (switches to the light capture when CPU spikes)
    juce::TextButton autoEcoChip { "AUTO-ECO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoEcoAtt;
    int cpuHighTicks = 0;
    int ecoNoticeTicks = 0;
    void applyEcoSwitchIfNeeded();
    double tunerFreq = -1.0;
    double tunerCents = 0.0;
    juce::String tunerNote;
    int tunerStringIndex = -1;
    int tunerTick = 0;

    // image sidecars (per rig lane)
    juce::String loadedModelPaths[GuitarRigNAMProcessor::maxRigs];
    juce::String loadedIrPaths[GuitarRigNAMProcessor::maxRigs];
    bool ampImagesLoaded[GuitarRigNAMProcessor::maxRigs] = {};
    bool cabImagesLoaded[GuitarRigNAMProcessor::maxRigs] = {};

    float inMeterDb = -80.0f, outMeterDb = -80.0f;
    int clipTicks = 0; // "CLIP" lit on the OUT meter after a peak >= 0 dBFS

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
