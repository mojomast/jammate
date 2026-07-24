#include "PluginEditor.h"

#include "DrumOverlay.h"
#include "PluginCatalog.h"

#include <BinaryData.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace ui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    // Archivo (OFL) — heading/label typeface of the modernist redesign
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
        BinaryData::ArchivoRegular_ttf, BinaryData::ArchivoRegular_ttfSize);
    static juce::Typeface::Ptr boldTf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::ArchivoExtraBold_ttf, BinaryData::ArchivoExtraBold_ttfSize);
    return bold ? boldTf : regular;
}

juce::Typeface::Ptr monoTypeface (bool bold)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    static juce::Typeface::Ptr boldTf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoBold_ttf, BinaryData::JetBrainsMonoBold_ttfSize);
    return bold ? boldTf : regular;
}
} // namespace ui

namespace
{
// ---------- tuner: pitch detection (simplified NSDF/MPM) ----------
double detectPitchHz (const float* x, int n, double sr)
{
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
        energy += (double) x[i] * x[i];
    if (energy / n < 1.0e-5) // silence
        return -1.0;

    const int minLag = juce::jmax (2, (int) (sr / 500.0)); // up to 500 Hz
    const int maxLag = juce::jmin (n / 2, (int) (sr / 55.0)); // down to 55 Hz
    if (maxLag <= minLag + 2)
        return -1.0;

    std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0.0, norm = 0.0;
        const int m = n - maxLag; // fixed window for all lags
        for (int i = 0; i < m; ++i)
        {
            ac += (double) x[i] * x[i + lag];
            norm += (double) x[i] * x[i] + (double) x[i + lag] * x[i + lag];
        }
        nsdf[(size_t) lag] = norm > 0.0 ? 2.0 * ac / norm : 0.0;
    }

    double maxV = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        maxV = juce::jmax (maxV, nsdf[(size_t) lag]);
    if (maxV < 0.6)
        return -1.0;

    const double thr = 0.9 * maxV;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const double v = nsdf[(size_t) lag];
        if (v >= thr && v >= nsdf[(size_t) lag - 1] && v >= nsdf[(size_t) lag + 1])
        {
            const double denom = 2.0 * (2.0 * v - nsdf[(size_t) lag - 1] - nsdf[(size_t) lag + 1]);
            const double d = denom != 0.0 ? (nsdf[(size_t) lag + 1] - nsdf[(size_t) lag - 1]) / denom : 0.0;
            return sr / ((double) lag + d);
        }
    }
    return -1.0;
}

const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const double kStringFreqs[] = { 82.407, 110.0, 146.83, 196.0, 246.94, 329.63 };
const char* kStringNames[] = { "E", "A", "D", "G", "B", "e" };
} // namespace

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramId, const juce::String& labelText,
                              std::function<juce::String (float)> formatter)
    : format (std::move (formatter)),
      attachment (apvts, paramId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.onValueChange = [this] { updateValueText(); };

    // detent at the default value + refined interactions
    if (auto* param = apvts.getParameter (paramId))
    {
        const auto& range = param->getNormalisableRange();
        slider.snapTarget = range.convertFrom0to1 (param->getDefaultValue());
        slider.snapRadius = (range.end - range.start) * 0.04;
        slider.setDoubleClickReturnValue (true, slider.snapTarget); // double-click resets
    }
    slider.setScrollWheelEnabled (true);                            // wheel adjusts
    slider.setVelocityModeParameters (1.0, 1, 0.05, true,           // Ctrl = fine adjust
                                      juce::ModifierKeys::ctrlModifier);
    slider.setMouseClickGrabsKeyboardFocus (false); // shortcuts stay with RigContent
    addAndMakeVisible (slider);

    nameLabel.setText (labelText, juce::dontSendNotification);
    nameLabel.setFont (ui::monoFont (8.0f));
    nameLabel.setColour (juce::Label::textColourId, ui::textFaint);
    nameLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (nameLabel);

    valueLabel.setFont (ui::monoFont (10.5f, true));
    valueLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8ecf1));
    valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff14181d));
    valueLabel.setColour (juce::TextEditor::highlightColourId, ui::accent.withAlpha (0.4f));
    valueLabel.setJustificationType (juce::Justification::centred);
    // click the value -> type the number
    valueLabel.setEditable (true, false, true);
    valueLabel.setTooltip ("Click to type the value");
    valueLabel.onTextChange = [this]
    {
        const auto text = valueLabel.getText().retainCharacters ("0123456789.,-");
        if (text.isEmpty())
        {
            updateValueText();
            return;
        }
        slider.setValue (text.replaceCharacter (',', '.').getDoubleValue(),
                         juce::sendNotificationSync);
        updateValueText();
    };
    addAndMakeVisible (valueLabel);

    updateValueText();
}

void KnobComponent::setKnobTooltip (const juce::String& tip)
{
    slider.setTooltip (tip);
}

void KnobComponent::updateValueText()
{
    valueLabel.setText (format ((float) slider.getValue()), juce::dontSendNotification);
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    slider.setBounds (area.removeFromTop (getWidth()));
    nameLabel.setBounds (area.removeFromTop (12));
    valueLabel.setBounds (area.removeFromTop (14));
}

//==============================================================================
void LedButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    constexpr float d = 14.0f;   // bypass-led dot per tokens.json

    if (getToggleState())        // on: accent fill + glow
    {
        g.setColour (ui::accent.withAlpha (0.40f));
        g.fillEllipse (c.x - d * 0.78f, c.y - d * 0.78f, d * 1.56f, d * 1.56f);
        g.setColour (ui::accent);
        g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (c.x - d * 0.24f, c.y - d * 0.34f, d * 0.34f, d * 0.28f); // specular
    }
    else                         // off: 1.5px divider ring (hollow)
    {
        g.setColour (ui::text.withAlpha (0.24f));
        g.drawEllipse (c.x - d * 0.5f + 0.75f, c.y - d * 0.5f + 0.75f, d - 1.5f, d - 1.5f, 1.5f);
    }
}

//==============================================================================
void LevelMeter::setLevel (float newLevelDb)
{
    solid = false;
    const float f = juce::jlimit (0.0f, 1.0f, (newLevelDb + 60.0f) / 60.0f);

    // peak-hold: holds the marker ~1.5 s then lets it slide
    const float oldPeak = peakFrac;
    if (f >= peakFrac)
    {
        peakFrac = f;
        peakHoldTicks = 45;
    }
    else if (peakHoldTicks > 0)
    {
        --peakHoldTicks;
    }
    else
    {
        peakFrac = juce::jmax (f, peakFrac - 0.012f);
    }

    if (std::abs (f - fraction) > 0.004f || std::abs (peakFrac - oldPeak) > 0.003f)
    {
        fraction = f;
        repaint();
    }
}

void LevelMeter::setFraction (float f, juce::Colour c)
{
    solid = true;
    solidColour = c;
    f = juce::jlimit (0.0f, 1.0f, f);
    if (std::abs (f - fraction) > 0.004f)
    {
        fraction = f;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::meterBg);
    g.fillRect (b);
    g.setColour (ui::text.withAlpha (0.10f));
    g.drawRect (b, 1.0f);

    auto inner = b.reduced (1.5f);
    constexpr int N = 14;                 // segments per tokens.json
    constexpr int clipZone = N - 2;       // last 2 = warn (amber)
    const float gap = 1.4f;
    const float segW = (inner.getWidth() - gap * (float) (N - 1)) / (float) N;
    const float off = 0.08f;

    for (int i = 0; i < N; ++i)
    {
        auto seg = juce::Rectangle<float> (inner.getX() + (float) i * (segW + gap),
                                           inner.getY(), segW, inner.getHeight());
        const bool lit = fraction >= (float) i / (float) N + 0.001f;
        juce::Colour c;
        if (solid)
            c = lit ? solidColour : ui::text.withAlpha (off);
        else if (i >= clipZone)
            c = lit ? (peakFrac >= 0.98f ? ui::red : ui::glowOrange)
                    : ui::glowOrange.withAlpha (0.12f);
        else
            c = lit ? ui::accent : ui::text.withAlpha (off);
        g.setColour (c);
        g.fillRect (seg);
    }

    // peak-hold marker (bright segment)
    if (! solid && peakFrac > 0.02f)
    {
        const int pi = juce::jlimit (0, N - 1, (int) (peakFrac * (float) N));
        auto seg = juce::Rectangle<float> (inner.getX() + (float) pi * (segW + gap),
                                           inner.getY(), segW, inner.getHeight());
        g.setColour (peakFrac >= 0.98f ? ui::red : ui::textBright.withAlpha (0.9f));
        g.fillRect (seg);
    }
}

//==============================================================================
void PillButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (ui::glass());
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (isHighlighted ? ui::borderHover() : juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (b, 2.0f, 1.0f);

    g.setColour (dotLit ? ui::accent : ui::textMuted);
    g.fillEllipse (b.getX() + 13.0f, b.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setFont (ui::uiFont (13.0f, true));
    g.setColour (ui::text);
    g.drawText (getButtonText(), getLocalBounds().reduced (26, 0), juce::Justification::centred);

    g.setFont (ui::uiFont (9.0f));
    g.setColour (ui::textMuted);
    g.drawText ("v", getLocalBounds().removeFromRight (20), juce::Justification::centredLeft);
}

//==============================================================================
ChainView::ChainView (GuitarRigNAMProcessor& p, std::function<void (int)> onLoadModel,
                      std::function<void (int)> onLoadIr,
                      std::function<void (int)> onLoadExtPlugin,
                      std::function<void (int)> onOpenExtPluginUi)
    : processor (p)
{
    auto& apvts = processor.apvts;

    // TONE3000 mark for store-loaded signal blocks (design requirement 5)
    t3kMark = juce::ImageFileFormat::loadFrom (BinaryData::t3kmark_png,
                                               (size_t) BinaryData::t3kmark_pngSize);

    auto formatDb = [] (float v) { return juce::String (v, 1) + " dB"; };
    auto formatDbInt = [] (float v) { return juce::String ((int) v) + " dB"; };
    auto formatMs = [] (float v) { return juce::String ((int) v) + " ms"; };
    auto formatTen = [] (float v) { return juce::String (v, 1); };
    auto formatPct = [] (float v) { return juce::String ((int) v) + "%"; };

    auto makeKnob = [&] (std::unique_ptr<KnobComponent>& dest, const char* id,
                         const char* label, std::function<juce::String (float)> fmt)
    {
        dest = std::make_unique<KnobComponent> (apvts, id, label, std::move (fmt));
        addAndMakeVisible (*dest);
    };

    makeKnob (inputKnob, "inputGain", "GAIN", formatDb);
    makeKnob (outputKnob, "outputGain", "LEVEL", formatDb);
    makeKnob (gateThreshKnob, "gateThresh", "THRESH", formatDbInt);
    makeKnob (gateHoldKnob, "gateHold", "HOLD", formatMs);
    makeKnob (gateReleaseKnob, "gateRelease", "RELEASE", formatMs);
    makeKnob (compSustainKnob, "compSustain", "SUSTAIN", formatTen);
    makeKnob (compAttackKnob, "compAttack", "ATTACK", formatMs);
    makeKnob (compBlendKnob, "compBlend", "BLEND", formatPct);
    makeKnob (compLevelKnob, "compLevel", "LEVEL", formatDb);
    makeKnob (preEqLowKnob, "preEqLow", "LOW", formatDbInt);
    makeKnob (preEqMidKnob, "preEqMid", "MID", formatDbInt);
    makeKnob (preEqHighKnob, "preEqHigh", "HIGH", formatDbInt);
    auto formatHzMod = [] (float v) { return juce::String (v, 1) + " Hz"; };
    makeKnob (modRateKnob, "modRate", "RATE", formatHzMod);
    makeKnob (modDepthKnob, "modDepth", "DEPTH", formatPct);
    makeKnob (modMixKnob, "modMix", "MIX", formatPct);
    makeKnob (odDriveKnob, "odDrive", "DRIVE", formatTen);
    makeKnob (odToneKnob, "odTone", "TONE", formatTen);
    makeKnob (odLevelKnob, "odLevel", "LEVEL", formatTen);
    makeKnob (cabAirKnob, "cabAir", "AIR", formatTen);

    auto formatHz = [] (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + "k" : juce::String ((int) v);
    };
    for (int r = 0; r < maxRigs; ++r)
    {
        const auto n = juce::String (r + 1);
        const auto prefix = r == 0 ? juce::String ("amp") : "amp" + n;
        makeKnob (ampGainKnob[r], (prefix + "Gain").toRawUTF8(), "GAIN", formatDb);
        makeKnob (ampBassKnob[r], (prefix + "Bass").toRawUTF8(), "BASS", formatTen);
        makeKnob (ampMidKnob[r], (prefix + "Mid").toRawUTF8(), "MID", formatTen);
        makeKnob (ampTrebleKnob[r], (prefix + "Treble").toRawUTF8(), "TREBLE", formatTen);
        makeKnob (ampPresKnob[r], (prefix + "Presence").toRawUTF8(), "PRES", formatTen);
        makeKnob (ampMasterKnob[r], (prefix + "Master").toRawUTF8(), "MASTER", formatDb);

        loadButtons[r].setButtonText ("LOAD NAM CAPTURE");
        loadButtons[r].setTooltip ("Add a capture from the TONE3000 store or a local .nam file");
        loadButtons[r].setMouseClickGrabsKeyboardFocus (false);
        loadButtons[r].onClick = [onLoadModel, r] { onLoadModel (r); };
        addChildComponent (loadButtons[r]);

        // variation selector: swaps the loaded capture for another model of the
        // same TONE3000 tone (inline picker). Shown whenever a model is loaded;
        // enabled only for store captures (those carry a tone_id in the .meta).
        ampVarButtons[r].setButtonText (juce::String (juce::CharPointer_UTF8 ("VARIANTS \xe2\x96\xbe")));
        ampVarButtons[r].getProperties().set ("outlineAccent", true);
        ampVarButtons[r].setMouseClickGrabsKeyboardFocus (false);
        ampVarButtons[r].onClick = [this, r]
        {
            const int tid = toneIdForLane (r);
            if (tid > 0 && onShowVariations != nullptr)
                onShowVariations (r, tid, &ampVarButtons[r]);
        };
        addChildComponent (ampVarButtons[r]);

        // the lane's blend lives in the Mixer card
        makeKnob (cabBlendKnob[r], ("cab" + n + "Blend").toRawUTF8(),
                  ("RIG " + n).toRawUTF8(), formatPct);
        makeKnob (cabLcKnob[r], ("cab" + n + "LowCut").toRawUTF8(), "LO CUT", formatHz);
        makeKnob (cabHcKnob[r], ("cab" + n + "HighCut").toRawUTF8(), "HI CUT", formatHz);

        cabPhaseChips[r].setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        cabPhaseChips[r].getProperties().set ("chip", true);
        cabPhaseChips[r].setClickingTogglesState (true);
        cabPhaseChips[r].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Inverts this cab's phase (avoids cancellation in parallel)")));
        cabPhaseChips[r].setMouseClickGrabsKeyboardFocus (false);
        cabPhaseAtt[r] = std::make_unique<Attachment> (apvts, "cab" + n + "Phase",
                                                       cabPhaseChips[r]);
        addChildComponent (cabPhaseChips[r]);

        cabIrButtons[r].setButtonText ("CHANGE");
        cabIrButtons[r].setTooltip ("Add an IR from the TONE3000 store or a local file");
        cabIrButtons[r].setMouseClickGrabsKeyboardFocus (false);
        cabIrButtons[r].onClick = [onLoadIr, r] { onLoadIr (r); };
        addChildComponent (cabIrButtons[r]);

        // cab variation selector: other IRs of the same TONE3000 cab tone
        cabVarButtons[r].setButtonText (juce::String (juce::CharPointer_UTF8 ("VARIANTS \xe2\x96\xbe")));
        cabVarButtons[r].getProperties().set ("outlineAccent", true);
        cabVarButtons[r].setMouseClickGrabsKeyboardFocus (false);
        cabVarButtons[r].onClick = [this, r]
        {
            const int tid = toneIdForCab (r);
            if (tid > 0 && onShowVariations != nullptr)
                onShowVariations (r, tid, &cabVarButtons[r]);
        };
        addChildComponent (cabVarButtons[r]);
    }

    // variation selectors on the cards (menu in the footer) - the tooltips cite
    // the study sources of each family; details in docs/EFEITOS.md
    setupTypeButton (odTypeButton, "odType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the drive model \xc2\xb7 refs: BYOD, Guitarix, Airwindows (docs/EFEITOS.md)")));
    setupTypeButton (compTypeButton, "compType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the compressor model \xc2\xb7 refs: LSP Plugins, rkrlv2 (docs/EFEITOS.md)")));
    setupTypeButton (delayTypeButton, "delayType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the delay model \xc2\xb7 refs: Airwindows, Guitarix (docs/EFEITOS.md)")));
    setupTypeButton (revTypeButton, "revType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the reverb model \xc2\xb7 refs: Dragonfly, GxPlugins (docs/EFEITOS.md)")));
    setupTypeButton (modTypeButton, "modType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the modulation type \xc2\xb7 refs: ToobAmp, GxPlugins, Airwindows (docs/EFEITOS.md)")));
    setupTypeButton (delayDivButton, "delayDiv",
                     juce::String (juce::CharPointer_UTF8 (
                         "Subdivision applied to TAP (1/8. = dotted eighth)")));
    setupTypeButton (pitchTypeButton, "pitchType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Choose the pitch interval \xc2\xb7 ref: rkrlv2/rakarrack (docs/EFEITOS.md)")));
    setupTypeButton (wahModeButton, "wahMode",
                     juce::String (juce::CharPointer_UTF8 (
                         "Auto = envelope \xc2\xb7 Manual = FREQ knob \xc2\xb7 LFO = sweep \xc2\xb7 ref: Guitarix")));
    setupTypeButton (harmKeyButton, "harmKey",
                     juce::String (juce::CharPointer_UTF8 ("Song key")));
    setupTypeButton (harmScaleButton, "harmScale",
                     juce::String (juce::CharPointer_UTF8 ("Major or minor scale")));
    setupTypeButton (harmIntervalButton, "harmInterval",
                     juce::String (juce::CharPointer_UTF8 (
                         "Diatonic interval of the second voice \xc2\xb7 ref: rkrlv2/rakarrack")));

    rigAddButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Add an AMP+CAB rig in parallel (up to 3)")));
    rigRemoveButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Remove the last AMP+CAB rig")));
    for (auto* b : { &rigAddButton, &rigRemoveButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    auto changeCount = [this] (int delta)
    {
        if (auto* param = processor.apvts.getParameter ("cabCount"))
        {
            const int c = juce::jlimit (1, (int) GuitarRigNAMProcessor::maxRigs,
                                        processor.getRigCount() + delta);
            param->setValueNotifyingHost (param->getNormalisableRange()
                                              .convertTo0to1 ((float) c));
        }
    };
    rigAddButton.onClick = [changeCount] { changeCount (1); };
    rigRemoveButton.onClick = [changeCount] { changeCount (-1); };
    addAndMakeVisible (rigAddButton);
    addAndMakeVisible (rigRemoveButton);
    makeKnob (eqLowKnob, "eqLow", "LOW", formatDbInt);
    makeKnob (eqMidKnob, "eqMid", "MID", formatDbInt);
    makeKnob (eqHighKnob, "eqHigh", "HIGH", formatDbInt);
    makeKnob (delayTimeKnob, "delayTime", "TIME", formatMs);
    makeKnob (delayFbKnob, "delayFb", "FB", formatPct);
    makeKnob (delayMixKnob, "delayMix", "MIX", formatPct);
    makeKnob (revDecayKnob, "revDecay", "DECAY", formatTen);
    makeKnob (revMixKnob, "revMix", "MIX", formatPct);
    makeKnob (revPreKnob, "revPre", "PRE", formatMs);
    makeKnob (pitchMixKnob, "pitchMix", "MIX", formatPct);
    makeKnob (pitchLevelKnob, "pitchLevel", "LEVEL", formatDb);
    makeKnob (looperLevelKnob, "looperLevel", "LOOP", formatDb);
    makeKnob (limCeilKnob, "limCeiling", "CEIL", formatDb);
    makeKnob (limRelKnob, "limRelease", "REL", formatMs);
    // external VST3 plugin slots
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        makeKnob (extMixKnob[s], (prefix + "Mix").toRawUTF8(), "MIX", formatPct);

        extLoadButton[s].setButtonText ("LOAD VST3");
        extLoadButton[s].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Choose a .vst3 plugin by category (Dragonfly, Airwindows, Zam...)")));
        extLoadButton[s].onClick = [onLoadExtPlugin, s] { onLoadExtPlugin (s); };
        extUiButton[s].setButtonText ("PANEL");
        extUiButton[s].setTooltip ("Open the hosted plugin's interface");
        extUiButton[s].onClick = [onOpenExtPluginUi, s] { onOpenExtPluginUi (s); };
        extRemoveButton[s].setButtonText ("REMOVE");
        extRemoveButton[s].setTooltip ("Empty the slot");
        extRemoveButton[s].onClick = [this, s] { processor.clearExternalPlugin (s); };
        for (auto* b : { &extLoadButton[s], &extUiButton[s], &extRemoveButton[s] })
        {
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
    }

    // P4 cards - one effect per card
    makeKnob (wahFreqKnob, "wahFreq", "FREQ", formatHz);
    makeKnob (wahRangeKnob, "wahRange", "RANGE", formatPct);
    makeKnob (wahResKnob, "wahRes", "RES", formatTen);
    makeKnob (sgSensKnob, "sgSens", "SENS", formatTen);
    makeKnob (sgRiseKnob, "sgRise", "RISE", formatMs);
    makeKnob (octSubKnob, "octSub", "SUB", formatPct);
    makeKnob (octDirectKnob, "octDirect", "DIRECT", formatPct);
    makeKnob (octToneKnob, "octTone", "TONE", formatHz);
    makeKnob (rmFreqKnob, "rmFreq", "FREQ", formatHz);
    makeKnob (rmMixKnob, "rmMix", "MIX", formatPct);
    makeKnob (bcBitsKnob, "bcBits", "BITS", [] (float v) { return juce::String ((int) v); });
    makeKnob (bcRateKnob, "bcRate", "RATE", formatHz);
    makeKnob (bcMixKnob, "bcMix", "MIX", formatPct);
    makeKnob (harmMixKnob, "harmMix", "MIX", formatPct);
    makeKnob (harmLevelKnob, "harmLevel", "LEVEL", formatDb);
    makeKnob (excFreqKnob, "excFreq", "FREQ", formatHz);
    makeKnob (excAmtKnob, "excAmt", "AMT", formatPct);
    makeKnob (dsFreqKnob, "dsFreq", "FREQ", formatHz);
    makeKnob (dsSensKnob, "dsSens", "SENS", formatTen);
    makeKnob (dsAmtKnob, "dsAmt", "AMT", formatPct);
    makeKnob (tapeDriveKnob, "tapeDrive", "DRIVE", formatTen);
    makeKnob (tapeBumpKnob, "tapeBump", "BUMP", formatDb);
    makeKnob (tapeRollKnob, "tapeRoll", "ROLL", formatHz);
    makeKnob (cnsAmtKnob, "cnsAmt", "GLUE", formatTen);


    // looper buttons (dynamic text in refreshDynamicText)
    {
        auto setupLooperButton = [this] (juce::TextButton& b, int cmd, const char* tipUtf8)
        {
            b.setTooltip (juce::String (juce::CharPointer_UTF8 (tipUtf8)));
            b.setMouseClickGrabsKeyboardFocus (false);
            if (cmd > 0)
                b.onClick = [this, cmd] { processor.requestLooperCommand (cmd); };
            addAndMakeVisible (b);
        };
        setupLooperButton (looperRecButton, 1,
                           "Records the loop; again closes and plays; then toggles overdub");
        setupLooperButton (looperPlayButton, 2, "Plays/stops the recorded loop");
        setupLooperButton (looperClearButton, 3, "Erases the loop");
        looperClearButton.setButtonText ("CLEAR");
        setupLooperButton (looperExportButton, 0,
                           "Saves the loop as WAV (Documents\\PedalForge NAM\\Loops)");
        looperExportButton.setButtonText ("WAV");
        looperExportButton.onClick = [this]
        {
            const auto file = processor.exportLoopToWav();
            looperExportButton.setButtonText (file != juce::File() ? "SAVED" : "EMPTY");
            auto* self = this; // MSVC: 'this' in a nested init-capture resolves incorrectly
            juce::Timer::callAfterDelay (1200,
                [safe = juce::Component::SafePointer<ChainView> (self)]
                {
                    if (safe != nullptr)
                        safe->looperExportButton.setButtonText ("WAV");
                });
        };
    }

    auto makeLed = [&] (LedButton& led, const char* id, std::unique_ptr<Attachment>& att)
    {
        att = std::make_unique<Attachment> (apvts, id, led);
        led.setTooltip (juce::String (juce::CharPointer_UTF8 ("Enable/disable the module")));
        led.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (led);
    };
    makeLed (gateLed, "gateOn", gateAtt);
    makeLed (odLed, "odOn", odAtt);
    makeLed (ampLed, "ampOn", ampAtt);
    ampLed.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Enable/disable the amp section (space)")));
    makeLed (cabLed, "cabOn", cabAtt);
    makeLed (eqLed, "eqOn", eqAtt);
    makeLed (delayLed, "delayOn", delayAtt);
    makeLed (revLed, "revOn", revAtt);
    makeLed (compLed, "compOn", compAtt);
    makeLed (preEqLed, "preEqOn", preEqAtt);
    makeLed (pitchLed, "pitchOn", pitchAtt);
    makeLed (looperLed, "looperOn", looperAtt);
    looperLed.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Enable/disable loop monitoring (recording continues)")));
    makeLed (limLed, "limOn", limAtt);
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        extAtt[s] = std::make_unique<Attachment> (apvts, prefix + "On", extLed[s]);
        extLed[s].setTooltip (juce::String (juce::CharPointer_UTF8 ("Enable/disable the module")));
        extLed[s].setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (extLed[s]);
    }
    makeLed (wahLed, "wahOn", wahAtt);
    makeLed (harmLed, "harmOn", harmAtt);
    makeLed (octLed, "octOn", octAtt);
    makeLed (rmLed, "rmOn", rmAtt);
    makeLed (bcLed, "bcOn", bcAtt);
    makeLed (sgLed, "sgOn", sgAtt);
    makeLed (excLed, "excOn", excAtt);
    makeLed (dsLed, "dsOn", dsAtt);
    makeLed (tapeLed, "tapeOn", tapeAtt);
    makeLed (cnsLed, "cnsOn", cnsAtt);
    makeLed (anLed, "anOn", anAtt);
    modAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "modOn", modLed);
    modLed.setTooltip (juce::String (juce::CharPointer_UTF8 ("Enable/disable the module")));
    modLed.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (modLed);

    // delay TAP tempo
    tapButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Tap twice in time with the song to set the delay time")));
    tapButton.setMouseClickGrabsKeyboardFocus (false);
    tapButton.onClick = [this] { applyTapTempo(); };
    addAndMakeVisible (tapButton);

    // compressor presets: set the 4 knobs at once
    {
        struct CompPreset { const char* label; float sustain, attack, blend, level; };
        const CompPreset presets[3] = { { "CLN", 2.5f, 30.0f, 70.0f, 0.0f },
                                        { "CTY", 6.0f, 10.0f, 100.0f, 1.0f },
                                        { "LEAD", 8.0f, 25.0f, 100.0f, 2.0f } };
        for (int i = 0; i < 3; ++i)
        {
            auto& chip = compPresetChips[i];
            chip.setButtonText (presets[i].label);
            chip.getProperties().set ("chip", true);
            chip.setMouseClickGrabsKeyboardFocus (false);
            const CompPreset pr = presets[i];
            chip.onClick = [this, pr]
            {
                auto set = [this] (const char* id, float value)
                {
                    if (auto* param = processor.apvts.getParameter (id))
                        param->setValueNotifyingHost (
                            param->getNormalisableRange().convertTo0to1 (value));
                };
                set ("compSustain", pr.sustain);
                set ("compAttack", pr.attack);
                set ("compBlend", pr.blend);
                set ("compLevel", pr.level);
                set ("compOn", 1.0f);
            };
            addAndMakeVisible (chip);
        }
        compPresetChips[0].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Clean: light, transparent compression")));
        compPresetChips[1].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Country: fast Dyna Comp-style squish")));
        compPresetChips[2].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Lead: maximum sustain for solos")));
    }

    // ECO chip: switches to the light capture version (when it exists)
    ecoChip.getProperties().set ("chip", true);
    ecoChip.setClickingTogglesState (true);
    ecoChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Uses the light capture version (less CPU). Downloaded alongside when the tone offers it.")));
    ecoChip.setMouseClickGrabsKeyboardFocus (false);
    ecoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "ampEco", ecoChip);
    addAndMakeVisible (ecoChip);

    // knob tooltips
    auto tip = [] (std::unique_ptr<KnobComponent>& k, const char* utf8)
    { k->setKnobTooltip (juce::String (juce::CharPointer_UTF8 (utf8))); };
    tip (inputKnob, "Input gain (before everything)");
    tip (outputKnob, "Final output volume");
    tip (gateThreshKnob, "Opens at this level; only closes 6 dB below (preserves sustain)");
    tip (gateHoldKnob, "Holds the gate open after the signal drops");
    tip (gateReleaseKnob, "Time for the gate to close");
    tip (compSustainKnob, "More sustain = more compression (threshold+ratio+makeup)");
    tip (compAttackKnob, "Attack: high lets the pick attack through before compressing");
    tip (compBlendKnob, "Parallel compression: blend with the dry signal");
    tip (compLevelKnob, "Compressor output volume");
    tip (preEqLowKnob, "Bass BEFORE the amp (100 Hz) - changes the saturation");
    tip (preEqMidKnob, "Mids BEFORE the amp (500 Hz)");
    tip (preEqHighKnob, "Treble BEFORE the amp (2.2 kHz)");
    tip (modRateKnob, "Modulation speed");
    tip (modDepthKnob, "Modulation depth");
    tip (modMixKnob, "Effect blend into the signal");
    tip (odDriveKnob, "Amount of pedal saturation");
    tip (odToneKnob, "Overdrive brightness");
    tip (odLevelKnob, "Overdrive volume");
    tip (cabAirKnob, "Air/brightness after the rig mix (8 kHz shelf)");
    for (int r = 0; r < maxRigs; ++r)
    {
        tip (ampGainKnob[r], "Pushes the signal into the capture - acts like the real amp's gain");
        tip (ampBassKnob[r], "Bass (150 Hz)");
        tip (ampMidKnob[r], "Mids (500 Hz)");
        tip (ampTrebleKnob[r], "Treble (1.8 kHz)");
        tip (ampPresKnob[r], "Presence (4.5 kHz)");
        tip (ampMasterKnob[r], "Amp section volume");
        tip (cabBlendKnob[r], "How much of this rig enters the Mixer sum");
        tip (cabLcKnob[r], "Cuts this cab's bass (20 Hz = off)");
        tip (cabHcKnob[r], "Cuts this cab's treble (20 kHz = off)");
    }
    tip (eqLowKnob, "Bass after the cab (120 Hz)");
    tip (eqMidKnob, "Mids after the cab (800 Hz)");
    tip (eqHighKnob, "Treble after the cab (4 kHz)");
    tip (delayTimeKnob, "Time between repeats");
    tip (delayFbKnob, "How many repeats (feedback)");
    tip (delayMixKnob, "Delay blend into the signal");
    tip (revDecayKnob, "Reverb size/decay");
    tip (revMixKnob, "Reverb blend into the signal");
    tip (revPreKnob, "Delay before the reverb starts");
    tip (pitchMixKnob, "Blend of the pitched voice with the dry signal");
    tip (pitchLevelKnob, "Pitched voice volume");
    tip (looperLevelKnob, "Loop volume in the mix");
    tip (limCeilKnob, "Limiter ceiling - nothing passes this level");
    tip (limRelKnob, "Recovery time after limiting");
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
        tip (extMixKnob[s], "Blend of the hosted plugin with the dry signal");
    tip (wahFreqKnob, "Wah base frequency (pedal position in Manual mode)");
    tip (wahRangeKnob, "How far the envelope/LFO sweeps from FREQ");
    tip (wahResKnob, "Filter resonance (the \"quack\")");
    tip (sgSensKnob, "Sensitivity to picking (when the swell restarts)");
    tip (sgRiseKnob, "Time for the volume to rise after each note");
    tip (octSubKnob, "Volume of the synthetic sub-octave");
    tip (octDirectKnob, "Direct signal volume");
    tip (octToneKnob, "Sub-octave damping");
    tip (rmFreqKnob, "Carrier frequency (low = tremor; high = bells)");
    tip (rmMixKnob, "Effect blend");
    tip (bcBitsKnob, "Bit resolution (less = dirtier)");
    tip (bcRateKnob, "Reduced sample rate (lo-fi aliasing)");
    tip (bcMixKnob, "Effect blend");
    tip (harmMixKnob, "Blend of the second voice");
    tip (harmLevelKnob, "Second voice volume");
    tip (excFreqKnob, "Where the harmonics start being generated");
    tip (excAmtKnob, "How much brightness is added back");
    tip (dsFreqKnob, "Center of the harsh band to tame");
    tip (dsSensKnob, "Detection sensitivity");
    tip (dsAmtKnob, "Maximum depth of the dynamic cut");
    tip (tapeDriveKnob, "Tape saturation");
    tip (tapeBumpKnob, "Head bump: bass boost at 90 Hz");
    tip (tapeRollKnob, "Tape treble rolloff");
    tip (cnsAmtKnob, "Amount of the \"glue\" (subtle sine waveshaping)");

    updateLayout();
}

// width of the rig block (stacked lanes, constant width):
// bus 18 + amp 266 + 24 + cab 144 + bus 18 + 12 + mixer 170
int ChainView::rigBlockWidth() const
{
    return 18 + 266 + 24 + 144 + 18 + 12 + 170;
}

void ChainView::updateLayout()
{
    // design metrics + 30 px connectors; the rig block is dynamic
    int x = 26 + 90 + 30; // margin + IN card + connector
    for (const auto& id : processor.getChainOrder())
        x += (id == "amp" ? rigBlockWidth() : effectCardWidth (id)) + 30;
    x += 74 + 30; // "+ EFFECT" button
    setSize (x + 90 + 26, chainHeight);
}

void ChainView::setAmpImage (int lane, juce::Image img)
{
    if (lane < 0 || lane >= maxRigs)
        return;
    ampImages[lane] = std::move (img);
    resized();
    repaint();
}

void ChainView::setCabImage (int lane, juce::Image img)
{
    if (lane < 0 || lane >= maxRigs)
        return;
    cabImages[lane] = std::move (img);
    resized();
    repaint();
}

void ChainView::setupTypeButton (juce::TextButton& button, const char* paramId,
                                 const juce::String& tooltip)
{
    button.setTooltip (tooltip);
    button.setMouseClickGrabsKeyboardFocus (false);
    button.onClick = [this, &button, paramId]
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (
            processor.apvts.getParameter (paramId));
        if (param == nullptr)
            return;

        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        for (int i = 0; i < param->choices.size(); ++i)
            menu.addItem (i + 1, param->choices[i], true, i == param->getIndex());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                            [param] (int result)
                            {
                                if (result > 0)
                                    param->setValueNotifyingHost (
                                        param->convertTo0to1 ((float) (result - 1)));
                            });
    };
    addAndMakeVisible (button);
}

void ChainView::refreshTypeButtons()
{
    auto update = [this] (juce::TextButton& b, const char* id)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (id)))
        {
            const auto text = param->getCurrentChoiceName()
                              + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe"));
            if (b.getButtonText() != text)
                b.setButtonText (text);
        }
    };
    update (odTypeButton, "odType");
    update (compTypeButton, "compType");
    update (delayTypeButton, "delayType");
    update (revTypeButton, "revType");
    update (modTypeButton, "modType");
    update (delayDivButton, "delayDiv");
    update (pitchTypeButton, "pitchType");
    update (wahModeButton, "wahMode");
    update (harmKeyButton, "harmKey");
    update (harmScaleButton, "harmScale");
    update (harmIntervalButton, "harmInterval");
}

void ChainView::applyTapTempo()
{
    const auto now = juce::Time::currentTimeMillis();
    const auto interval = now - lastTapMs;
    lastTapMs = now;

    if (interval < 120 || interval > 2000)
        return; // first tap (or out of useful range): just arms the next one

    const float factors[] = { 1.0f, 0.5f, 0.75f, 0.25f }; // 1/4, 1/8, 1/8., 1/16
    const int div = juce::jlimit (0, 3, (int) processor.apvts.getRawParameterValue ("delayDiv")->load());
    const float timeMs = juce::jlimit (60.0f, 1000.0f, (float) interval * factors[div]);

    if (auto* param = processor.apvts.getParameter ("delayTime"))
        param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 (timeMs));
}

juce::String ChainView::archBadgeForIr (int slot)
{
    const auto path = processor.getIrPath (slot);
    if (path != cabArchPathSeen[slot])
    {
        cabArchPathSeen[slot] = path;
        cabArchCache[slot].clear();
        if (path.isNotEmpty())
        {
            const auto meta = juce::JSON::parse (juce::File (path + ".meta").loadFileAsString());
            const auto arch = meta.getProperty ("arch", "").toString();
            if (arch == "2") cabArchCache[slot] = "A2";
            else if (arch == "1") cabArchCache[slot] = "A1";
        }
    }
    return cabArchCache[slot];
}

int ChainView::toneIdForLane (int lane) const
{
    const auto path = processor.getModelPathNormal (lane);
    if (path.isEmpty())
        return 0;
    const juce::File meta (path + ".meta");
    if (! meta.existsAsFile())
        return 0;
    return (int) juce::JSON::parse (meta.loadFileAsString()).getProperty ("tone_id", 0);
}

int ChainView::toneIdForCab (int slot) const
{
    const auto path = processor.getIrPath (slot);
    if (path.isEmpty())
        return 0;
    const juce::File meta (path + ".meta");
    if (! meta.existsAsFile())
        return 0;
    return (int) juce::JSON::parse (meta.loadFileAsString()).getProperty ("tone_id", 0);
}

void ChainView::refreshDynamicText()
{
    bool varLayoutChanged = false;
    const int rigCount = processor.getRigCount();
    for (int r = 0; r < maxRigs; ++r)
    {
        const bool loaded = processor.hasModelLoaded (r);
        loadButtons[r].setButtonText (loaded ? "CHANGE NAM CAPTURE" : "LOAD NAM CAPTURE");
        // the variations selector shows whenever a model is loaded on an active
        // lane; it only works for store captures (a tone_id in the .meta), so
        // disable it for disk/old captures and explain via the tooltip.
        const bool showVar = loaded && r < rigCount;
        const bool hasVariations = loaded && toneIdForLane (r) > 0;
        ampVarButtons[r].setVisible (showVar);
        ampVarButtons[r].setEnabled (hasVariations);
        ampVarButtons[r].setTooltip (hasVariations
            ? "Switch to another capture of this TONE3000 tone"
            : "Add this capture from the TONE3000 store to switch between its variations");

        // the CHANGE row reserves space for the button only when it shows, so a
        // load/unload needs a relayout for the button to actually get bounds.
        if (showVar != lastVarLoaded[r])
        {
            lastVarLoaded[r] = showVar;
            varLayoutChanged = true;
        }

        // same for the cab: variations of the loaded IR/cab tone
        const bool cabLoaded = processor.getIrPath (r).isNotEmpty();
        const bool showCabVar = cabLoaded && r < rigCount;
        const bool cabHasVar = cabLoaded && toneIdForCab (r) > 0;
        cabVarButtons[r].setVisible (showCabVar);
        cabVarButtons[r].setEnabled (cabHasVar);
        cabVarButtons[r].setTooltip (cabHasVar
            ? "Switch to another IR of this TONE3000 cab tone"
            : "Add this IR from the TONE3000 store to switch between its variations");
        if (showCabVar != lastCabVarLoaded[r])
        {
            lastCabVarLoaded[r] = showCabVar;
            varLayoutChanged = true;
        }
    }
    if (varLayoutChanged)
        resized();

    ecoChip.setEnabled (processor.hasEcoVariant());
    refreshTypeButtons();

    // looper buttons track the state
    {
        using LS = GuitarRigNAMProcessor::LooperState;
        const auto st = processor.getLooperState();
        const auto rec = st == LS::empty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f REC"))
                         : st == LS::recording ? juce::String ("CLOSE")
                         : st == LS::overdub ? juce::String ("END DUB")
                                             : juce::String ("OVERDUB");
        if (looperRecButton.getButtonText() != rec)
            looperRecButton.setButtonText (rec);
        const auto play = st == LS::playing || st == LS::overdub
                              ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP"))
                              : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY"));
        if (looperPlayButton.getButtonText() != play)
            looperPlayButton.setButtonText (play);
        const bool hasLoop = st != LS::empty;
        looperPlayButton.setEnabled (hasLoop);
        looperClearButton.setEnabled (hasLoop);
        looperExportButton.setEnabled (hasLoop && st != LS::recording);
    }

    // slots VST3
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const bool hasExt = processor.hasExternalPlugin (s);
        const auto loadText = hasExt ? juce::String ("CHANGE VST3")
                                     : juce::String ("LOAD VST3");
        if (extLoadButton[s].getButtonText() != loadText)
            extLoadButton[s].setButtonText (loadText);
        extUiButton[s].setEnabled (hasExt);
        extRemoveButton[s].setEnabled (hasExt);
    }

    // disabled cards: dim knobs/buttons (the LED stays lit to re-enable)
    {
        static const char* dimIds[] = { "gate", "comp", "od", "preeq", "eq", "mod", "delay",
                                        "reverb", "pitch", "looper", "limiter", "ext", "ext2",
                                        "ext3", "ext4", "ext5", "ext6", "ext7", "ext8",
                                        "wah", "harm", "octaver", "ringmod", "bitcrush",
                                        "slowgear", "exciter", "deesser", "tape", "console",
                                        "analyzer" };
        const auto chain = processor.getChainOrder();
        for (auto* id : dimIds)
        {
            if (! chain.contains (id))
                continue;
            auto* p = processor.apvts.getRawParameterValue (onParamIdForFx (id));
            const float alpha = p != nullptr && p->load() > 0.5f ? 1.0f : 0.4f;
            auto comps = componentsForFx (id);
            for (int i = 1; i < comps.size(); ++i) // 0 = LED, always stays visible
                comps[i]->setAlpha (alpha);
        }
    }

    // rig count or chain order changed -> relayout. With the mouse button
    // pressed, DEFER: a reflow during an ongoing drag makes the slider
    // "jump" (the relative position changes without the mouse moving) and
    // swaps the target under the cursor. The timer retries each tick until release.
    const auto orderNow = processor.getChainOrder().joinIntoString (",");
    if ((processor.getRigCount() != lastRigCount || orderNow != lastOrderSeen)
        && ! juce::Component::isMouseButtonDownAnywhere())
    {
        applyChainRelayout();
    }
    repaint();
}

//==============================================================================
// Effects drawer: mappings by id (components, On param, name)

int ChainView::extSlotForId (const juce::String& id)
{
    if (id == "ext")
        return 0;
    if (id.startsWith ("ext"))
    {
        const auto rest = id.substring (3);
        if (rest.isNotEmpty() && rest.containsOnly ("0123456789")) // excludes "exciter"
        {
            const int n = rest.getIntValue();
            if (n >= 2 && n <= GuitarRigNAMProcessor::maxExtSlots)
                return n - 1;
        }
    }
    return -1;
}

juce::Array<juce::Component*> ChainView::componentsForFx (const juce::String& id)
{
    if (const int s = extSlotForId (id); s >= 0)
        return { &extLed[s], extMixKnob[s].get(), &extLoadButton[s], &extUiButton[s],
                 &extRemoveButton[s] };
    // convention: the LED is always first (stays out of the dimming)
    if (id == "gate")   return { &gateLed, gateThreshKnob.get(), gateHoldKnob.get(), gateReleaseKnob.get() };
    if (id == "comp")   return { &compLed, compSustainKnob.get(), compAttackKnob.get(), compBlendKnob.get(),
                                 compLevelKnob.get(), &compTypeButton, &compPresetChips[0],
                                 &compPresetChips[1], &compPresetChips[2] };
    if (id == "od")     return { &odLed, odDriveKnob.get(), odToneKnob.get(), odLevelKnob.get(), &odTypeButton };
    if (id == "preeq")  return { &preEqLed, preEqLowKnob.get(), preEqMidKnob.get(), preEqHighKnob.get() };
    if (id == "eq")     return { &eqLed, eqLowKnob.get(), eqMidKnob.get(), eqHighKnob.get() };
    if (id == "mod")    return { &modLed, modRateKnob.get(), modDepthKnob.get(), modMixKnob.get(), &modTypeButton };
    if (id == "delay")  return { &delayLed, delayTimeKnob.get(), delayFbKnob.get(), delayMixKnob.get(),
                                 &delayTypeButton, &delayDivButton, &tapButton };
    if (id == "reverb") return { &revLed, revDecayKnob.get(), revMixKnob.get(), revPreKnob.get(), &revTypeButton };
    if (id == "pitch")  return { &pitchLed, pitchMixKnob.get(), pitchLevelKnob.get(), &pitchTypeButton };
    if (id == "looper") return { &looperLed, looperLevelKnob.get(), &looperRecButton, &looperPlayButton,
                                 &looperClearButton, &looperExportButton };
    if (id == "limiter") return { &limLed, limCeilKnob.get(), limRelKnob.get() };
    if (id == "wah")    return { &wahLed, wahFreqKnob.get(), wahRangeKnob.get(), wahResKnob.get(), &wahModeButton };
    if (id == "harm")   return { &harmLed, harmMixKnob.get(), harmLevelKnob.get(), &harmKeyButton,
                                 &harmScaleButton, &harmIntervalButton };
    if (id == "octaver") return { &octLed, octSubKnob.get(), octDirectKnob.get(), octToneKnob.get() };
    if (id == "ringmod") return { &rmLed, rmFreqKnob.get(), rmMixKnob.get() };
    if (id == "bitcrush") return { &bcLed, bcBitsKnob.get(), bcRateKnob.get(), bcMixKnob.get() };
    if (id == "slowgear") return { &sgLed, sgSensKnob.get(), sgRiseKnob.get() };
    if (id == "exciter") return { &excLed, excFreqKnob.get(), excAmtKnob.get() };
    if (id == "deesser") return { &dsLed, dsFreqKnob.get(), dsSensKnob.get(), dsAmtKnob.get() };
    if (id == "tape")   return { &tapeLed, tapeDriveKnob.get(), tapeBumpKnob.get(), tapeRollKnob.get() };
    if (id == "console") return { &cnsLed, cnsAmtKnob.get() };
    if (id == "analyzer") return { &anLed };
    return {};
}

juce::String ChainView::onParamIdForFx (const juce::String& id) const
{
    if (const int s = extSlotForId (id); s >= 0)
        return (s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1)) + "On";
    if (id == "gate") return "gateOn";
    if (id == "comp") return "compOn";
    if (id == "od") return "odOn";
    if (id == "preeq") return "preEqOn";
    if (id == "eq") return "eqOn";
    if (id == "mod") return "modOn";
    if (id == "delay") return "delayOn";
    if (id == "reverb") return "revOn";
    if (id == "pitch") return "pitchOn";
    if (id == "looper") return "looperOn";
    if (id == "limiter") return "limOn";
    if (id == "wah") return "wahOn";
    if (id == "harm") return "harmOn";
    if (id == "octaver") return "octOn";
    if (id == "ringmod") return "rmOn";
    if (id == "bitcrush") return "bcOn";
    if (id == "slowgear") return "sgOn";
    if (id == "exciter") return "excOn";
    if (id == "deesser") return "dsOn";
    if (id == "tape") return "tapeOn";
    if (id == "console") return "cnsOn";
    if (id == "analyzer") return "anOn";
    return {};
}

juce::String ChainView::fxDisplayName (const juce::String& id)
{
    if (const int s = extSlotForId (id); s >= 0)
        return "Plugin VST3 " + juce::String (s + 1);
    if (id == "gate") return "Noise Gate";
    if (id == "comp") return "Compressor";
    if (id == "od") return "Drive";
    if (id == "preeq") return juce::String (juce::CharPointer_UTF8 ("Pre-EQ"));
    if (id == "eq") return "EQ";
    if (id == "mod") return juce::String (juce::CharPointer_UTF8 ("Modulation"));
    if (id == "delay") return "Delay";
    if (id == "reverb") return "Reverb";
    if (id == "pitch") return "Pitch";
    if (id == "looper") return "Looper";
    if (id == "limiter") return "Limiter";
    if (id == "wah") return "Wah";
    if (id == "harm") return "Harmonizer";
    if (id == "octaver") return "Octaver";
    if (id == "ringmod") return "Ring Mod";
    if (id == "bitcrush") return "Bitcrusher";
    if (id == "slowgear") return "Slow Gear";
    if (id == "exciter") return "Exciter";
    if (id == "deesser") return "De-esser";
    if (id == "tape") return "Tape";
    if (id == "console") return "Console";
    if (id == "analyzer") return "Analyzer";
    return id;
}

std::vector<std::pair<juce::Rectangle<int>, int>> ChainView::insertSpots() const
{
    // one "+" in the middle of each connector: insert BEFORE card i = index i
    std::vector<std::pair<juce::Rectangle<int>, int>> spots;
    juce::Rectangle<int> prev = ioInB;
    const auto entries = orderedEntries();
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& box = entries[(size_t) i].box;
        if (box.isEmpty())
            continue;
        const int midX = (prev.getRight() + box.getX()) / 2;
        spots.push_back ({ juce::Rectangle<int> (midX - 11, chainHeight / 2 - 11, 22, 22), i });
        prev = box;
    }
    return spots;
}

void ChainView::showAddFxMenu (int insertIndex, juce::Rectangle<int> targetArea)
{
    struct Category { const char* title; std::initializer_list<const char*> ids; };
    static const Category categories[] = {
        { "Dynamics",              { "gate", "comp", "slowgear", "limiter" } },
        { "Drive & Filter",        { "wah", "od", "octaver", "ringmod", "bitcrush", "preeq" } },
        { "Pitch",                 { "pitch", "harm" } },
        { "Modulation & Color",    { "mod", "exciter", "deesser", "tape", "console" } },
        { "Ambience",              { "delay", "reverb" } },
        { "Extras",                { "ext", "ext2", "ext3", "ext4", "ext5", "ext6",
                                     "ext7", "ext8", "looper", "analyzer" } },
    };

    const auto order = processor.getChainOrder();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    bool any = false;

    for (const auto& cat : categories)
    {
        bool catAny = false;
        for (auto* id : cat.ids)
            if (! order.contains (id))
                catAny = true;
        if (! catAny)
            continue;

        menu.addSectionHeader (juce::String (juce::CharPointer_UTF8 (cat.title)));
        for (auto* id : cat.ids)
            if (! order.contains (id))
                menu.addItem (GuitarRigNAMProcessor::fxFromString (id) + 1,
                              fxDisplayName (id));
        any = true;
    }

    if (! any)
        menu.addItem (99999, juce::String (juce::CharPointer_UTF8 (
                          "All effects are already in the chain")), false);

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetScreenArea (
            juce::Rectangle<int> (targetArea.getWidth(), 1)
                .withPosition (localPointToGlobal (targetArea.getPosition()))),
        [safe = juce::Component::SafePointer<ChainView> (this), insertIndex] (int result)
        {
            if (safe == nullptr || result <= 0 || result >= 99999)
                return;
            const auto id = GuitarRigNAMProcessor::fxToString (
                (GuitarRigNAMProcessor::ChainFx) (result - 1));

            auto order = safe->processor.getChainOrder();
            int pos;
            if (insertIndex >= 0)
            {
                // connector "+": lands exactly where it was clicked
                pos = juce::jlimit (0, order.size(), insertIndex);
            }
            else
            {
                // end button: canonical position (can be dragged afterward)
                const int rank = GuitarRigNAMProcessor::canonicalRank (id);
                pos = order.size();
                for (int i = 0; i < order.size(); ++i)
                    if (GuitarRigNAMProcessor::canonicalRank (order[i]) > rank)
                    {
                        pos = i;
                        break;
                    }
            }
            order.insert (pos, id);
            safe->processor.setChainOrder (order);
            safe->applyChainRelayout();
        });
}

void ChainView::removeFxFromChain (const juce::String& id)
{
    auto order = processor.getChainOrder();
    order.removeString (id);
    processor.setChainOrder (order);
    applyChainRelayout(); // layout updates immediately, not on the next tick
}

void ChainView::applyChainRelayout()
{
    lastRigCount = processor.getRigCount();
    lastOrderSeen = processor.getChainOrder().joinIntoString (",");
    updateLayout();
    resized();
    repaint();
}

//==============================================================================
// Reordering drag-and-drop

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    draggingId.clear();
    panning = false;

    // "+ EFFECT" button (end of the chain)
    if (addFxB.contains (e.getPosition()))
    {
        showAddFxMenu (-1, addFxB);
        return;
    }

    // connector "+": adds an effect AT THAT position
    for (const auto& [rect, idx] : insertSpots())
        if (rect.contains (e.getPosition()))
        {
            showAddFxMenu (idx, rect);
            return;
        }

    // "x" removes the effect from the chain (back to the drawer, settings preserved).
    // Synchronous + immediate relayout: fast successive clicks never land
    // on a stale layout (wrong knob/x sliding under the mouse).
    for (const auto& entry : orderedEntries())
        if (entry.id != "amp" && removeHotspot (entry.box).contains (e.getPosition()))
        {
            removeFxFromChain (entry.id);
            return;
        }

    // Clicks on knobs/buttons go to the children; only the card background
    // reaches here. Amp+cabs are an anchor and cannot be dragged.
    for (const auto& entry : orderedEntries())
        if (entry.id != "amp" && entry.box.contains (e.getPosition()))
        {
            draggingId = entry.id;
            dragGrabDx = e.x - entry.box.getX();
            dragMouseX = (float) e.x;
            dropIndex = -1;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;
        }

    // empty background (or amp block): dragging pans the chain
    if (draggingId.isEmpty())
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            panning = true;
            panStartMouse = e.getScreenPosition();
            panStartView = vp->getViewPosition();
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }
}

void ChainView::mouseDrag (const juce::MouseEvent& e)
{
    if (panning)
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            const int dx = e.getScreenPosition().x - panStartMouse.x;
            vp->setViewPosition (juce::jmax (0, panStartView.x - dx), panStartView.y);
        }
        return;
    }

    if (draggingId.isEmpty())
        return;

    dragMouseX = (float) e.x;

    // insertion index: before the first entry whose center is to the
    // right of the mouse
    const auto entries = orderedEntries();
    dropIndex = (int) entries.size();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (e.x < entries[(size_t) i].box.getCentreX())
        {
            dropIndex = i;
            break;
        }
    repaint();
}

void ChainView::mouseMove (const juce::MouseEvent& e)
{
    // microinteraction: highlights the "+"/"x" under the mouse
    juce::Rectangle<int> hot;
    for (const auto& [rect, idx] : insertSpots())
        if (rect.contains (e.getPosition()))
        {
            hot = rect;
            break;
        }
    if (hot.isEmpty())
        for (const auto& entry : orderedEntries())
            if (entry.id != "amp" && removeHotspot (entry.box).contains (e.getPosition()))
            {
                hot = removeHotspot (entry.box);
                break;
            }
    if (hot.isEmpty() && addFxB.contains (e.getPosition()))
        hot = addFxB;

    if (hot != hoverHotspot)
    {
        hoverHotspot = hot;
        setMouseCursor (hot.isEmpty() ? juce::MouseCursor::NormalCursor
                                      : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ChainView::mouseExit (const juce::MouseEvent&)
{
    if (! hoverHotspot.isEmpty())
    {
        hoverHotspot = {};
        repaint();
    }
}

//==============================================================================
// File drag-and-drop: .nam -> amp, IR -> cab, .vst3 -> external slot

static bool isNamFile (const juce::String& f) { return f.endsWithIgnoreCase (".nam"); }
static bool isIrFile (const juce::String& f)
{
    return f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
           || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac");
}
static bool isVst3File (const juce::String& f) { return f.endsWithIgnoreCase (".vst3"); }

bool ChainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isNamFile (f) || isIrFile (f) || isVst3File (f))
            return true;
    return false;
}

std::pair<juce::Rectangle<int>, juce::String> ChainView::dropTargetAt (const juce::String& file,
                                                                       int x, int y) const
{
    const auto pos = juce::Point<int> (x, y);
    const int count = processor.getRigCount();

    if (isNamFile (file))
    {
        for (int r = 0; r < count; ++r)
            if (ampLaneB[r].contains (pos))
                return { ampLaneB[r], "nam:" + juce::String (r) };
        // outside an amp: first free lane (or the 1st)
        const int lane = juce::jmax (0, processor.firstFreeModelLane());
        return { ampLaneB[juce::jlimit (0, count - 1, lane)], "nam:" + juce::String (lane) };
    }
    if (isIrFile (file))
    {
        for (int r = 0; r < count; ++r)
            if (cabLaneB[r].contains (pos))
                return { cabLaneB[r], "ir:" + juce::String (r) };
        const int slot = juce::jmax (0, processor.firstFreeIrSlot());
        return { cabLaneB[juce::jlimit (0, count - 1, slot)], "ir:" + juce::String (slot) };
    }
    if (isVst3File (file))
    {
        // slot under the cursor; else the first visible empty slot; else the 1st
        for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
            if (! extB[s].isEmpty() && extB[s].contains (pos))
                return { extB[s], "vst3:" + juce::String (s) };
        for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
            if (! extB[s].isEmpty() && ! processor.hasExternalPlugin (s))
                return { extB[s], "vst3:" + juce::String (s) };
        return { extB[0], "vst3:0" };
    }
    return { {}, {} };
}

void ChainView::fileDragMove (const juce::StringArray& files, int x, int y)
{
    juce::Rectangle<int> target;
    if (! files.isEmpty())
        target = dropTargetAt (files[0], x, y).first;
    if (target != dropHighlight)
    {
        dropHighlight = target;
        repaint();
    }
}

void ChainView::fileDragExit (const juce::StringArray&)
{
    if (! dropHighlight.isEmpty())
    {
        dropHighlight = {};
        repaint();
    }
}

void ChainView::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropHighlight = {};
    for (const auto& f : files)
    {
        const auto [rect, action] = dropTargetAt (f, x, y);
        if (action.startsWith ("nam:"))
            processor.setModelPair (action.fromFirstOccurrenceOf (":", false, false).getIntValue(),
                                    juce::File (f), {});
        else if (action.startsWith ("ir:"))
            processor.loadIrAsync (action.fromFirstOccurrenceOf (":", false, false).getIntValue(),
                                   juce::File (f));
        else if (action.startsWith ("vst3:"))
            processor.loadExternalPluginAsync (
                action.fromFirstOccurrenceOf (":", false, false).getIntValue(), juce::File (f));
    }
    repaint();
}

void ChainView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // mouse wheel scrolls the chain (there is no vertical scroll here)
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const int dx = juce::roundToInt ((wheel.deltaY + wheel.deltaX) * 480.0f);
        vp->setViewPosition (juce::jmax (0, vp->getViewPositionX() - dx),
                             vp->getViewPositionY());
    }
}

void ChainView::mouseUp (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    panning = false;

    if (draggingId.isNotEmpty() && dropIndex >= 0)
    {
        auto order = processor.getChainOrder();
        const int from = order.indexOf (draggingId);
        if (from >= 0)
        {
            int to = dropIndex;
            order.remove (from);
            if (to > from)
                --to;
            order.insert (juce::jlimit (0, order.size(), to), draggingId);
            processor.setChainOrder (order);
        }
    }

    draggingId.clear();
    dropIndex = -1;
    dragMouseX = -1.0f;
    repaint();
}

int ChainView::effectCardWidth (const juce::String& id) const
{
    if (id == "eq" || id == "preeq" || id == "looper" || id == "harm" || id == "analyzer"
        || extSlotForId (id) >= 0)
        return 176;
    return 132;
}

juce::Rectangle<int> ChainView::boxForFx (const juce::String& id) const
{
    if (id == "gate") return gateB;
    if (id == "od") return odB;
    if (id == "eq") return eqB;
    if (id == "delay") return delayB;
    if (id == "reverb") return revB;
    if (id == "comp") return compB;
    if (id == "preeq") return preEqB;
    if (id == "mod") return modB;
    if (id == "pitch") return pitchB;
    if (id == "looper") return looperB;
    if (id == "limiter") return limB;
    if (const int s = extSlotForId (id); s >= 0) return extB[s];
    if (id == "wah") return wahB;
    if (id == "harm") return harmB;
    if (id == "octaver") return octB;
    if (id == "ringmod") return rmB;
    if (id == "bitcrush") return bcB;
    if (id == "slowgear") return sgB;
    if (id == "exciter") return excB;
    if (id == "deesser") return dsB;
    if (id == "tape") return tapeB;
    if (id == "console") return cnsB;
    if (id == "analyzer") return anB;
    return ampLaneB[0].getUnion (mixerB); // "amp" = bloco rigs+mixer
}

std::vector<ChainView::ChainEntry> ChainView::orderedEntries() const
{
    std::vector<ChainEntry> out;
    for (const auto& id : processor.getChainOrder())
        out.push_back ({ id, boxForFx (id) });
    return out;
}

void ChainView::resized()
{
    const int H = chainHeight;
    auto cardY = [H] (int cardH) { return (H - cardH) / 2; };

    // drawer: clear the boxes and hide the effect components outside the
    // chain; the present ones reappear when positioned below
    static const char* allFxIds[] = { "gate", "comp", "od", "preeq", "eq", "mod", "delay",
                                      "reverb", "pitch", "looper", "limiter", "ext", "ext2",
                                      "ext3", "ext4", "ext5", "ext6", "ext7", "ext8",
                                      "wah", "harm", "octaver", "ringmod", "bitcrush",
                                      "slowgear", "exciter", "deesser", "tape", "console",
                                      "analyzer" };
    const auto chain = processor.getChainOrder();
    for (auto* id : allFxIds)
    {
        const bool present = chain.contains (id);
        for (auto* c : componentsForFx (id))
            c->setVisible (present);
    }
    gateB = odB = eqB = delayB = revB = compB = preEqB = pitchB = looperB = limB = {};
    for (auto& b : extB)
        b = {};
    wahB = harmB = octB = rmB = bcB = sgB = excB = dsB = tapeB = cnsB = anB = {};

    // position the cards following the chain's dynamic order
    int x = 26;
    ioInB = { x, cardY (330), 90, 330 };
    x += 90 + 30;

    for (const auto& id : processor.getChainOrder())
    {
        if (id == "amp")
        {
            // STACKED AMP+CAB lanes (true parallel): one row per
            // rig, split bus on the left and sum bus entering the Mixer
            const int count = processor.getRigCount();
            const int rowGap = 12, busW = 18;
            const int availH = H - 40;
            const int rowH = juce::jmin (360, (availH - (count - 1) * rowGap) / count);
            const int totalH = count * rowH + (count - 1) * rowGap;
            const int topY = (H - totalH) / 2;
            const int pairX = x + busW;

            for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
            {
                if (r >= count)
                {
                    ampLaneB[r] = cabLaneB[r] = {};
                    continue;
                }
                const int ry = topY + r * (rowH + rowGap);
                const int cabH = juce::jmin (rowH, 330);
                ampLaneB[r] = { pairX, ry, 266, rowH };
                cabLaneB[r] = { pairX + 266 + 24, ry + (rowH - cabH) / 2, 144, cabH };
            }
            mixerB = { pairX + 266 + 24 + 144 + busW + 12, cardY (330), 170, 330 };
            x = mixerB.getRight() + 30;
        }
        else
        {
            const int w = effectCardWidth (id);
            auto box = juce::Rectangle<int> { x, cardY (330), w, 330 };
            if (id == "gate") gateB = box;
            else if (id == "od") odB = box;
            else if (id == "eq") eqB = box;
            else if (id == "delay") delayB = box;
            else if (id == "reverb") revB = box;
            else if (id == "comp") compB = box;
            else if (id == "preeq") preEqB = box;
            else if (id == "mod") modB = box;
            else if (id == "pitch") pitchB = box;
            else if (id == "looper") looperB = box;
            else if (id == "limiter") limB = box;
            else if (const int es = extSlotForId (id); es >= 0) extB[es] = box;
            else if (id == "wah") wahB = box;
            else if (id == "harm") harmB = box;
            else if (id == "octaver") octB = box;
            else if (id == "ringmod") rmB = box;
            else if (id == "bitcrush") bcB = box;
            else if (id == "slowgear") sgB = box;
            else if (id == "exciter") excB = box;
            else if (id == "deesser") dsB = box;
            else if (id == "tape") tapeB = box;
            else if (id == "console") cnsB = box;
            else if (id == "analyzer") anB = box;
            x += w + 30;
        }
    }

    addFxB = { x, cardY (330), 74, 330 };
    x += 74 + 30;
    ioOutB = { x, cardY (330), 90, 330 };

    // ---- IO
    inputKnob->setBounds (ioInB.getX() + (90 - 50) / 2, ioInB.getCentreY() - 34, 50, 50 + 26);
    outputKnob->setBounds (ioOutB.getX() + (90 - 50) / 2, ioOutB.getCentreY() - 34, 50, 50 + 26);

    // ---- generic pedal: knobs wrapped in 2 columns (46 px)
    auto layoutPedal = [] (juce::Rectangle<int> b, LedButton& led,
                           std::initializer_list<KnobComponent*> knobs)
    {
        led.setBounds (b.getRight() - 12 - 18, b.getY() + 10, 18, 18);
        const int kw = 46, kh = kw + 26, gapX = 11, gapY = 12;
        const int n = (int) knobs.size();
        const int rows = (n + 1) / 2;
        const int blockH = rows * kh + (rows - 1) * gapY;
        int i = 0;
        for (auto* k : knobs)
        {
            const int row = i / 2;
            const int inRow = juce::jmin (2, n - row * 2);
            const int rowW = inRow * kw + (inRow - 1) * gapX;
            const int rx = b.getCentreX() - rowW / 2 + (i % 2) * (kw + gapX);
            const int ry = b.getY() + 52 + (b.getHeight() - 52 - 88 - blockH) / 2 + row * (kh + gapY);
            k->setBounds (rx, ry, kw, kh);
            ++i;
        }
    };

    layoutPedal (gateB, gateLed, { gateThreshKnob.get(), gateHoldKnob.get(),
                                   gateReleaseKnob.get() });
    layoutPedal (odB, odLed, { odDriveKnob.get(), odToneKnob.get(), odLevelKnob.get() });
    layoutPedal (delayB, delayLed, { delayTimeKnob.get(), delayFbKnob.get(), delayMixKnob.get() });
    layoutPedal (revB, revLed, { revDecayKnob.get(), revMixKnob.get(), revPreKnob.get() });
    layoutPedal (compB, compLed, { compSustainKnob.get(), compAttackKnob.get(),
                                   compBlendKnob.get(), compLevelKnob.get() });
    layoutPedal (modB, modLed, { modRateKnob.get(), modDepthKnob.get(), modMixKnob.get() });
    layoutPedal (pitchB, pitchLed, { pitchMixKnob.get(), pitchLevelKnob.get() });
    layoutPedal (limB, limLed, { limCeilKnob.get(), limRelKnob.get() });
    layoutPedal (wahB, wahLed, { wahFreqKnob.get(), wahRangeKnob.get(), wahResKnob.get() });
    layoutPedal (sgB, sgLed, { sgSensKnob.get(), sgRiseKnob.get() });
    layoutPedal (octB, octLed, { octSubKnob.get(), octDirectKnob.get(), octToneKnob.get() });
    layoutPedal (rmB, rmLed, { rmFreqKnob.get(), rmMixKnob.get() });
    layoutPedal (bcB, bcLed, { bcBitsKnob.get(), bcRateKnob.get(), bcMixKnob.get() });
    layoutPedal (excB, excLed, { excFreqKnob.get(), excAmtKnob.get() });
    layoutPedal (dsB, dsLed, { dsFreqKnob.get(), dsSensKnob.get(), dsAmtKnob.get() });
    layoutPedal (tapeB, tapeLed, { tapeDriveKnob.get(), tapeBumpKnob.get(), tapeRollKnob.get() });
    layoutPedal (cnsB, cnsLed, { cnsAmtKnob.get() });
    anLed.setBounds (anB.getRight() - 12 - 18, anB.getY() + 10, 18, 18);

    // harmonizer: 3 selectors (KEY/SCALE/INTERVAL) + MIX/LEVEL
    {
        harmLed.setBounds (harmB.getRight() - 12 - 18, harmB.getY() + 10, 18, 18);
        const int bx = harmB.getX() + 12, bw = harmB.getWidth() - 24;
        harmKeyButton.setBounds (bx, harmB.getY() + 36, bw / 2 - 3, 24);
        harmScaleButton.setBounds (bx + bw / 2 + 3, harmB.getY() + 36, bw / 2 - 3, 24);
        harmIntervalButton.setBounds (bx, harmB.getY() + 66, bw, 24);
        harmMixKnob->setBounds (harmB.getCentreX() - 52, harmB.getY() + 130, 46, 46 + 26);
        harmLevelKnob->setBounds (harmB.getCentreX() + 6, harmB.getY() + 130, 46, 46 + 26);
    }

    // VST3 slots: MIX + stacked LOAD/PANEL/REMOVE buttons
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto& b = extB[s];
        extLed[s].setBounds (b.getRight() - 12 - 18, b.getY() + 10, 18, 18);
        extMixKnob[s]->setBounds (b.getCentreX() - 23, b.getY() + 92, 46, 46 + 26);
        const int bx = b.getX() + 12, bw = b.getWidth() - 24;
        extLoadButton[s].setBounds (bx, b.getBottom() - 12 - 24 - 60, bw, 24);
        extUiButton[s].setBounds (bx, b.getBottom() - 12 - 24 - 30, bw, 24);
        extRemoveButton[s].setBounds (bx, b.getBottom() - 12 - 24, bw, 24);
    }

    // looper: LOOP (level) + 2x2 button grid
    {
        looperLed.setBounds (looperB.getRight() - 12 - 18, looperB.getY() + 10, 18, 18);
        looperLevelKnob->setBounds (looperB.getCentreX() - 23, looperB.getY() + 64, 46, 46 + 26);
        const int bw = (looperB.getWidth() - 24 - 8) / 2, bh = 30;
        const int bx = looperB.getX() + 12;
        const int by = looperB.getY() + 168;
        looperRecButton.setBounds (bx, by, bw, bh);
        looperPlayButton.setBounds (bx + bw + 8, by, bw, bh);
        looperClearButton.setBounds (bx, by + bh + 8, bw, bh);
        looperExportButton.setBounds (bx + bw + 8, by + bh + 8, bw, bh);
    }

    // TAP + subdivision on the delay card (row above the model selector)
    tapButton.setBounds (delayB.getX() + 12, delayB.getBottom() - 96, 50, 24);
    delayDivButton.setBounds (delayB.getX() + 12 + 54, delayB.getBottom() - 96,
                              delayB.getWidth() - 24 - 54, 24);

    // compressor preset chips (row under the title)
    {
        const int cw = 36;
        int px = compB.getX() + (compB.getWidth() - (3 * cw + 2 * 4)) / 2;
        for (auto& chip : compPresetChips)
        {
            chip.setBounds (px, compB.getY() + 32, cw, 18);
            px += cw + 4;
        }
    }

    // variation selectors (card footer, in place of the text)
    auto placeTypeButton = [] (juce::TextButton& b, juce::Rectangle<int> card)
    {
        b.setBounds (card.getX() + 12, card.getBottom() - 66, card.getWidth() - 24, 24);
    };
    placeTypeButton (odTypeButton, odB);
    placeTypeButton (compTypeButton, compB);
    placeTypeButton (delayTypeButton, delayB);
    placeTypeButton (revTypeButton, revB);
    placeTypeButton (modTypeButton, modB);
    placeTypeButton (pitchTypeButton, pitchB);
    placeTypeButton (wahModeButton, wahB);

    // pre-EQ: same layout as the EQ
    {
        preEqLed.setBounds (preEqB.getRight() - 13 - 18, preEqB.getY() + 10, 18, 18);
        const int kw = 44, kh = kw + 26, gap = 13;
        const int gx = preEqB.getCentreX() - (3 * kw + 2 * gap) / 2;
        const int ky = preEqB.getY() + 130;
        preEqLowKnob->setBounds (gx, ky, kw, kh);
        preEqMidKnob->setBounds (gx + kw + gap, ky, kw, kh);
        preEqHighKnob->setBounds (gx + 2 * (kw + gap), ky, kw, kh);
    }

    // ---- parallel rigs: AMP+CAB pair per lane + Mixer
    {
        const int count = processor.getRigCount();

        for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
        {
            const bool active = r < count;
            const auto ampB = ampLaneB[r];
            const auto cabB = cabLaneB[r];
            const bool compact = ampB.getHeight() < 300; // 2-3 rigs stacked

            for (auto* k : { ampGainKnob[r].get(), ampBassKnob[r].get(), ampMidKnob[r].get(),
                             ampTrebleKnob[r].get(), ampPresKnob[r].get(), ampMasterKnob[r].get() })
                k->setVisible (active);
            loadButtons[r].setVisible (active);
            cabLcKnob[r]->setVisible (active);
            cabHcKnob[r]->setVisible (active);
            cabPhaseChips[r].setVisible (active);
            cabIrButtons[r].setVisible (active);
            cabBlendKnob[r]->setVisible (active);
            if (! active)
                continue;

            // lane's amp
            if (r == 0)
            {
                ampLed.setBounds (ampB.getRight() - 18 - 18, ampB.getY() + (compact ? 10 : 19), 18, 18);
                ecoChip.setBounds (ampLed.getX() - 8 - 52, ampB.getY() + (compact ? 8 : 17), 52, 22);
            }

            KnobComponent* grid[6] = { ampGainKnob[r].get(), ampBassKnob[r].get(),
                                       ampMidKnob[r].get(), ampTrebleKnob[r].get(),
                                       ampPresKnob[r].get(), ampMasterKnob[r].get() };
            if (! compact)
            {
                // classic 3x2 grid (optional photo between header and knobs)
                const bool photo = ampImages[r].isValid();
                const int kw = 42, kh = kw + 26, gapX = 26, gapY = 4;
                const int gx = ampB.getX() + (266 - (3 * kw + 2 * gapX)) / 2;
                const int gy = ampB.getY() + (photo ? 152 : 118);
                for (int i = 0; i < 6; ++i)
                    grid[i]->setBounds (gx + (i % 3) * (kw + gapX), gy + (i / 3) * (kh + gapY), kw, kh);

                {
                    auto lb = juce::Rectangle<int> (ampB.getX() + 18, ampB.getBottom() - 15 - 32,
                                                    266 - 36, 32);
                    if (processor.hasModelLoaded (r))
                    {
                        ampVarButtons[r].setBounds (lb.removeFromRight (86));
                        lb.removeFromRight (6);
                    }
                    loadButtons[r].setBounds (lb);
                }
            }
            else
            {
                // single row of 6 smaller knobs
                const int kw = 32, kh = kw + 26, gapX = 6;
                const int gx = ampB.getX() + (266 - (6 * kw + 5 * gapX)) / 2;
                const int gy = ampB.getY() + 46 + (ampB.getHeight() - 46 - 32 - kh) / 2;
                for (int i = 0; i < 6; ++i)
                    grid[i]->setBounds (gx + i * (kw + gapX), gy, kw, kh);

                {
                    auto lb = juce::Rectangle<int> (ampB.getX() + 14, ampB.getBottom() - 28,
                                                    266 - 28, 22);
                    if (processor.hasModelLoaded (r))
                    {
                        ampVarButtons[r].setBounds (lb.removeFromRight (80));
                        lb.removeFromRight (5);
                    }
                    loadButtons[r].setBounds (lb);
                }
            }

            // lane's cab
            if (r == 0)
                cabLed.setBounds (cabB.getRight() - 10 - 18, cabB.getY() + 10, 18, 18);
            if (! compact)
            {
                cabPhaseChips[r].setBounds (cabB.getRight() - 12 - 26, cabB.getY() + 36, 26, 20);
                cabLcKnob[r]->setBounds (cabB.getX() + 18, cabB.getY() + 176, 40, 40 + 26);
                cabHcKnob[r]->setBounds (cabB.getX() + 78, cabB.getY() + 176, 40, 40 + 26);
                {
                    auto cb = juce::Rectangle<int> (cabB.getX() + 10, cabB.getBottom() - 12 - 24,
                                                    cabB.getWidth() - 20, 24);
                    if (processor.getIrPath (r).isNotEmpty())
                    {
                        cabVarButtons[r].setBounds (cb.removeFromRight (84));
                        cb.removeFromRight (6);
                    }
                    cabIrButtons[r].setBounds (cb);
                }
            }
            else
            {
                cabPhaseChips[r].setBounds (cabB.getRight() - 10 - 26, cabB.getY() + 32, 26, 20);
                const int kh2 = 36 + 26;
                const int ky = cabB.getY() + 34 + (cabB.getHeight() - 34 - 30 - kh2) / 2;
                cabLcKnob[r]->setBounds (cabB.getX() + 26, ky, 36, kh2);
                cabHcKnob[r]->setBounds (cabB.getX() + 82, ky, 36, kh2);
                {
                    auto cb = juce::Rectangle<int> (cabB.getX() + 10, cabB.getBottom() - 28,
                                                    cabB.getWidth() - 20, 22);
                    if (processor.getIrPath (r).isNotEmpty())
                    {
                        cabVarButtons[r].setBounds (cb.removeFromRight (78));
                        cb.removeFromRight (5);
                    }
                    cabIrButtons[r].setBounds (cb);
                }
            }
        }

        // Mixer: rig +/-, per-lane blend and global AIR
        rigRemoveButton.setBounds (mixerB.getRight() - 12 - 22, mixerB.getY() + 8, 22, 22);
        rigAddButton.setBounds (rigRemoveButton.getX() - 4 - 22, mixerB.getY() + 8, 22, 22);
        rigAddButton.setEnabled (count < GuitarRigNAMProcessor::maxRigs);
        rigRemoveButton.setEnabled (count > 1);

        for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
            if (r < count)
                cabBlendKnob[r]->setBounds (mixerB.getX() + 18, mixerB.getY() + 44 + r * 88,
                                            44, 44 + 26);
        cabAirKnob->setBounds (mixerB.getRight() - 18 - 44, mixerB.getCentreY() - 20,
                               44, 44 + 26);
    }

    // ---- EQ
    {
        eqLed.setBounds (eqB.getRight() - 13 - 18, eqB.getY() + 10, 18, 18);
        const int kw = 44, kh = kw + 26, gap = 13;
        const int gx = eqB.getCentreX() - (3 * kw + 2 * gap) / 2;
        const int ky = eqB.getY() + 130;
        eqLowKnob->setBounds (gx, ky, kw, kh);
        eqMidKnob->setBounds (gx + kw + gap, ky, kw, kh);
        eqHighKnob->setBounds (gx + 2 * (kw + gap), ky, kw, kh);
    }
}

void ChainView::drawPedalFrame (juce::Graphics& g, juce::Rectangle<int> b,
                                const juce::String& title, const juce::String& footer)
{
    if (b.isEmpty()) // effect in the drawer (outside the current chain)
        return;

    auto bf = b.toFloat();
    g.setGradientFill ({ ui::cardTop, 0.0f, bf.getY(), ui::cardBottom, 0.0f, bf.getBottom(), false });
    g.fillRoundedRectangle (bf, 2.0f);
    g.setColour (ui::border());
    g.drawRoundedRectangle (bf.reduced (0.5f), 2.0f, 1.0f);

    g.setFont (ui::uiFont (12.0f, true));
    g.setColour (ui::text);
    g.drawText (title, b.getX() + 12, b.getY() + 12, b.getWidth() - 46, 15,
                juce::Justification::centredLeft);

    if (footer.isNotEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (b.getX() + 12, b.getBottom() - 74, b.getWidth() - 24, 1);
        g.setFont (ui::monoFont (9.0f));
        g.setColour (juce::Colour (0xffb4bbc4));
        g.drawFittedText (footer, b.getX() + 12, b.getBottom() - 64, b.getWidth() - 24, 24,
                          juce::Justification::centredLeft, 2);
    }
}

void ChainView::drawPhoto (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> spot)
{
    if (! img.isValid())
        return;

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (spot.toFloat(), 2.0f);
    g.reduceClipRegion (clip);
    const float scale = juce::jmax ((float) spot.getWidth() / img.getWidth(),
                                    (float) spot.getHeight() / img.getHeight());
    const float dw = img.getWidth() * scale, dh = img.getHeight() * scale;
    g.drawImage (img, juce::Rectangle<float> (spot.getX() + (spot.getWidth() - dw) / 2.0f,
                                              spot.getY() + (spot.getHeight() - dh) / 2.0f, dw, dh),
                 juce::RectanglePlacement::stretchToFit);
    g.restoreState();
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (spot.toFloat(), 2.0f, 1.0f);
}

void ChainView::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);   // theme background behind the chain
    // subtle striped background
    g.setColour (ui::dividerBase().withAlpha (0.018f));
    for (int gx = 0; gx < getWidth(); gx += 44)
        g.fillRect (gx, 0, 1, getHeight());

    g.setFont (ui::monoFont (9.0f));
    g.setColour (juce::Colour (0xff525b66));
    g.drawText ("SIGNAL FLOW", 24, 14, 200, 12, juce::Justification::centredLeft);

    // ---- directional connectors
    auto connector = [&g] (juce::Rectangle<int> a, juce::Rectangle<int> b)
    {
        const float y = (float) a.getCentreY();
        const float xa = (float) a.getRight() + 3.0f;
        const float xb = (float) b.getX() - 3.0f;
        g.setColour (ui::accent);
        g.fillEllipse (xa, y - 3.5f, 7.0f, 7.0f);
        g.setGradientFill ({ ui::accent.withAlpha (0.7f), xa, 0.0f,
                             ui::accent.withAlpha (0.15f), xb, 0.0f, false });
        g.fillRoundedRectangle (xa + 7.0f, y - 1.0f, xb - xa - 12.0f, 2.0f, 1.0f);
        g.setColour (ui::accent.withAlpha (0.4f));
        g.fillEllipse (xb - 5.0f, y - 2.5f, 5.0f, 5.0f);
    };
    // connectors follow the dynamic order (amp -> cabs is internal to the block)
    {
        const auto entries = orderedEntries();
        juce::Rectangle<int> prev = ioInB;
        for (const auto& e : entries)
        {
            if (e.id == "amp")
            {
                const int count = processor.getRigCount();

                if (count == 1)
                {
                    connector (prev, ampLaneB[0]);
                    connector (ampLaneB[0], cabLaneB[0]);
                    connector (cabLaneB[0], mixerB);
                }
                else
                {
                    // PARALLEL topology: split node -> one branch per lane
                    // (amp -> cab) -> sum bus entering the Mixer
                    auto hLine = [&g] (float x1, float x2, float y)
                    {
                        g.setGradientFill ({ ui::accent.withAlpha (0.7f), x1, 0.0f,
                                             ui::accent.withAlpha (0.25f), x2, 0.0f, false });
                        g.fillRoundedRectangle (x1, y - 1.0f, x2 - x1, 2.0f, 1.0f);
                    };
                    auto vBar = [&g] (float x, float y1, float y2)
                    {
                        g.setColour (ui::accent.withAlpha (0.55f));
                        g.fillRoundedRectangle (x - 1.5f, y1 - 1.5f, 3.0f, y2 - y1 + 3.0f, 1.5f);
                    };

                    const float busInX = (float) ampLaneB[0].getX() - 9.0f;
                    const float busOutX = (float) cabLaneB[0].getRight() + 9.0f;
                    const float yPrev = (float) prev.getCentreY();
                    const float yMix = (float) mixerB.getCentreY();
                    const float yTop = (float) ampLaneB[0].getCentreY();
                    const float yBot = (float) ampLaneB[count - 1].getCentreY();

                    // dry signal split
                    g.setColour (ui::accent);
                    g.fillEllipse ((float) prev.getRight() + 3.0f, yPrev - 3.5f, 7.0f, 7.0f);
                    hLine ((float) prev.getRight() + 10.0f, busInX, yPrev);
                    vBar (busInX, juce::jmin (yTop, yPrev), juce::jmax (yBot, yPrev));

                    for (int r = 0; r < count; ++r)
                    {
                        const float ry = (float) ampLaneB[r].getCentreY();
                        hLine (busInX, (float) ampLaneB[r].getX() - 3.0f, ry);
                        g.setColour (ui::accent.withAlpha (0.4f));
                        g.fillEllipse ((float) ampLaneB[r].getX() - 8.0f, ry - 2.5f, 5.0f, 5.0f);

                        connector (ampLaneB[r], cabLaneB[r]);

                        // lane output -> sum bus
                        g.setColour (ui::accent);
                        g.fillEllipse ((float) cabLaneB[r].getRight() + 3.0f, ry - 3.5f, 7.0f, 7.0f);
                        hLine ((float) cabLaneB[r].getRight() + 10.0f, busOutX, ry);
                    }

                    vBar (busOutX, juce::jmin (yTop, yMix), juce::jmax (yBot, yMix));
                    hLine (busOutX, (float) mixerB.getX() - 3.0f, yMix);
                    g.setColour (ui::accent.withAlpha (0.4f));
                    g.fillEllipse ((float) mixerB.getX() - 8.0f, yMix - 2.5f, 5.0f, 5.0f);
                }
                prev = mixerB;
            }
            else
            {
                connector (prev, e.box);
                prev = e.box;
            }
        }
        connector (prev, addFxB);
        connector (addFxB, ioOutB);
    }

    // ---- IO
    auto drawIo = [&] (juce::Rectangle<int> b, const juce::String& name, const juce::String& lbl)
    {
        auto bf = b.toFloat();
        g.setGradientFill ({ juce::Colour (0xff1e232a), 0.0f, bf.getY(),
                             juce::Colour (0xff101318), 0.0f, bf.getBottom(), false });
        g.fillRoundedRectangle (bf, 2.0f);
        g.setColour (ui::border());
        g.drawRoundedRectangle (bf.reduced (0.5f), 2.0f, 1.0f);

        g.setFont (ui::monoFont (9.0f));
        g.setColour (juce::Colour (0xff8a929c));
        g.drawText (name, b.withTrimmedTop (16).withHeight (12), juce::Justification::centred);

        const float jackY = bf.getY() + 58.0f;
        g.setColour (juce::Colours::black);
        g.fillEllipse (bf.getCentreX() - 19.0f, jackY, 38.0f, 38.0f);
        g.setColour (juce::Colour (0xff363c45));
        g.drawEllipse (bf.getCentreX() - 19.0f, jackY, 38.0f, 38.0f, 3.0f);

        g.setColour (ui::green);
        g.fillEllipse (bf.getCentreX() - 22.0f, bf.getBottom() - 26.0f, 6.0f, 6.0f);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (juce::Colour (0xff6b747f));
        g.drawText (lbl, b.withTrimmedLeft (b.getWidth() / 2 - 9)
                            .withY (b.getBottom() - 30).withHeight (14),
                    juce::Justification::centredLeft);
    };
    drawIo (ioInB, "INPUT", "IN");
    drawIo (ioOutB, "OUTPUT", "OUT");

    // ---- pedals (those with variations have a selector in the footer instead of text)
    drawPedalFrame (g, gateB, "Noise Gate", juce::String (juce::CharPointer_UTF8 ("Hysteresis 6 dB \xc2\xb7 hold")));
    drawPedalFrame (g, odB, "Drive", " ");
    drawPedalFrame (g, delayB, "Delay", " ");
    drawPedalFrame (g, revB, "Reverb", " ");
    drawPedalFrame (g, compB, "Compressor", " ");
    drawPedalFrame (g, modB, "Modulation", " ");
    drawPedalFrame (g, pitchB, "Pitch", " ");
    drawPedalFrame (g, wahB, "Wah", " ");
    drawPedalFrame (g, sgB, "Slow Gear",
                    juce::String (juce::CharPointer_UTF8 ("automatic swell")));
    drawPedalFrame (g, octB, "Octaver",
                    juce::String (juce::CharPointer_UTF8 ("analog sub-octave")));
    drawPedalFrame (g, rmB, "Ring Mod",
                    juce::String (juce::CharPointer_UTF8 ("sine carrier")));
    drawPedalFrame (g, bcB, "Bitcrusher",
                    juce::String (juce::CharPointer_UTF8 ("lo-fi \xc2\xb7 bits + rate")));
    drawPedalFrame (g, harmB, "Harmonizer", " ");
    drawPedalFrame (g, excB, "Exciter",
                    juce::String (juce::CharPointer_UTF8 ("harmonic brightness")));
    drawPedalFrame (g, dsB, "De-esser",
                    juce::String (juce::CharPointer_UTF8 ("tames the harsh band")));
    drawPedalFrame (g, tapeB, "Tape",
                    juce::String (juce::CharPointer_UTF8 ("saturation \xc2\xb7 bump \xc2\xb7 rolloff")));
    drawPedalFrame (g, cnsB, "Console",
                    juce::String (juce::CharPointer_UTF8 ("analog buss glue")));

    // ---- looper (state + time drawn live)
    if (! looperB.isEmpty())
    {
        drawPedalFrame (g, looperB, "Looper", {});

        const auto st = processor.getLooperState();
        juce::String status;
        juce::Colour c = ui::textFaint;
        switch (st)
        {
            case GuitarRigNAMProcessor::LooperState::empty:
                status = juce::String (juce::CharPointer_UTF8 ("empty \xc2\xb7 REC to record"));
                break;
            case GuitarRigNAMProcessor::LooperState::recording:
                status = "recording " + juce::String (processor.getLooperPosSeconds(), 1) + " s";
                c = ui::red;
                break;
            case GuitarRigNAMProcessor::LooperState::playing:
                status = "playing " + juce::String (processor.getLooperPosSeconds(), 1) + " / "
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                c = ui::accent;
                break;
            case GuitarRigNAMProcessor::LooperState::overdub:
                status = "overdub " + juce::String (processor.getLooperPosSeconds(), 1) + " / "
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                c = ui::glowOrange;
                break;
            case GuitarRigNAMProcessor::LooperState::stopped:
                status = juce::String (juce::CharPointer_UTF8 ("stopped \xc2\xb7 "))
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                break;
        }
        g.setFont (ui::monoFont (9.0f));
        g.setColour (c);
        g.drawText (status, looperB.getX() + 12, looperB.getY() + 34, looperB.getWidth() - 24, 12,
                    juce::Justification::centredLeft);

        // loop progress bar
        if (st != GuitarRigNAMProcessor::LooperState::empty)
        {
            auto bar = juce::Rectangle<float> ((float) looperB.getX() + 12.0f,
                                               (float) looperB.getY() + 52.0f,
                                               (float) looperB.getWidth() - 24.0f, 4.0f);
            g.setColour (ui::meterBg);
            g.fillRoundedRectangle (bar, 2.0f);
            const double total = st == GuitarRigNAMProcessor::LooperState::recording
                                     ? (double) GuitarRigNAMProcessor::looperMaxSeconds
                                     : processor.getLooperSeconds();
            const double frac = total > 0 ? processor.getLooperPosSeconds() / total : 0.0;
            g.setColour (c);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, frac)), 2.0f);
        }
    }

    // ---- external VST3 plugin slots
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto& b = extB[s];
        if (b.isEmpty())
            continue;
        drawPedalFrame (g, b, "Plugin VST3 " + juce::String (s + 1), {});

        const auto extName = processor.getExternalPluginName (s);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::accent);
        g.drawText (juce::CharPointer_UTF8 ("HOSTING \xc2\xb7 VST3"),
                    b.getX() + 12, b.getY() + 32, 140, 11,
                    juce::Justification::centredLeft);
        g.setFont (ui::uiFont (13.0f, true));
        g.setColour (extName.isNotEmpty() ? ui::textBright : ui::textMuted);
        g.drawFittedText (extName.isNotEmpty()
                              ? extName
                              : juce::String ("- empty slot -"),
                          b.getX() + 12, b.getY() + 48, b.getWidth() - 24, 34,
                          juce::Justification::topLeft, 2);
    }

    // ---- spectrum analyzer (live FFT 2048)
    if (! anB.isEmpty())
    {
        drawPedalFrame (g, anB, "Analyzer",
                        juce::String (juce::CharPointer_UTF8 ("spectrum \xc2\xb7 40 Hz-16 kHz")));

        auto viz = juce::Rectangle<float> ((float) anB.getX() + 13.0f, (float) anB.getY() + 40.0f,
                                           (float) anB.getWidth() - 26.0f,
                                           (float) anB.getHeight() - 40.0f - 84.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 2.0f);

        if (processor.apvts.getRawParameterValue ("anOn")->load() > 0.5f)
        {
            // FFT of the most recent chunk + log bands with smooth decay
            constexpr int fftSize = 2048;
            static float sample[fftSize];
            processor.readAnalyzerBlock (sample, fftSize);
            for (int i = 0; i < fftSize; ++i)
                anFftBuf[(size_t) i] = sample[i]
                    * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1)));
            std::fill (anFftBuf.begin() + fftSize, anFftBuf.end(), 0.0f);
            anFft.performFrequencyOnlyForwardTransform (anFftBuf.data());

            const double sr = 48000.0; // display; the log ratio is what matters
            for (int b = 0; b < anNumBands; ++b)
            {
                const double f0 = 40.0 * std::pow (400.0, (double) b / anNumBands);
                const double f1 = 40.0 * std::pow (400.0, (double) (b + 1) / anNumBands);
                const int k0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 * fftSize / sr));
                const int k1 = juce::jlimit (k0 + 1, fftSize / 2, (int) (f1 * fftSize / sr) + 1);
                float mag = 0.0f;
                for (int k = k0; k < k1; ++k)
                    mag = juce::jmax (mag, anFftBuf[(size_t) k]);
                const float db = juce::Decibels::gainToDecibels (mag / (fftSize * 0.25f), -80.0f);
                const float norm = juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 70.0f);
                anBands[b] = norm > anBands[b] ? norm : anBands[b] * 0.85f; // smooth decay
            }

            const float bw = (viz.getWidth() - 12.0f) / anNumBands;
            for (int b = 0; b < anNumBands; ++b)
            {
                const float hgt = juce::jmax (2.0f, anBands[b] * (viz.getHeight() - 12.0f));
                const float bx = viz.getX() + 6.0f + b * bw;
                juce::ColourGradient grad (ui::accent, 0.0f, viz.getBottom() - 6.0f - hgt,
                                           ui::accent.withAlpha (0.15f), 0.0f,
                                           viz.getBottom() - 6.0f, false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (bx + 1.0f, viz.getBottom() - 6.0f - hgt,
                                        bw - 2.0f, hgt, 2.0f);
            }
        }
    }

    // ---- limiter (with a little gain reduction bar)
    if (! limB.isEmpty())
    {
        drawPedalFrame (g, limB, "Limiter", juce::String (juce::CharPointer_UTF8 ("brickwall \xc2\xb7 end of the chain")));

        const float gr = processor.getLimiterGrDb();
        auto bar = juce::Rectangle<float> ((float) limB.getX() + 13.0f, (float) limB.getY() + 40.0f,
                                           (float) limB.getWidth() - 26.0f, 6.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (bar, 3.0f);
        if (gr > 0.05f)
        {
            g.setColour (gr > 6.0f ? ui::red : ui::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth()
                                                   * juce::jlimit (0.0f, 1.0f, gr / 12.0f)), 3.0f);
        }
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("GR " + juce::String (gr, 1) + " dB",
                    limB.getX() + 13, limB.getY() + 50, limB.getWidth() - 26, 11,
                    juce::Justification::centredLeft);
    }

    // ---- pre-EQ (with live bars, like the post EQ)
    if (! preEqB.isEmpty())
    {
        drawPedalFrame (g, preEqB, juce::String (juce::CharPointer_UTF8 ("Pre-EQ")),
                        juce::String (juce::CharPointer_UTF8 ("shapes the saturation \xc2\xb7 pre-amp")));

        auto viz = juce::Rectangle<float> ((float) preEqB.getX() + 13.0f, (float) preEqB.getY() + 38.0f,
                                           (float) preEqB.getWidth() - 26.0f, 62.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (viz, 2.0f, 1.0f);

        const float lo = processor.apvts.getRawParameterValue ("preEqLow")->load();
        const float mi = processor.apvts.getRawParameterValue ("preEqMid")->load();
        const float hi = processor.apvts.getRawParameterValue ("preEqHigh")->load();
        const float gains[7] = { lo, lo, (lo + mi) / 2.0f, mi, (mi + hi) / 2.0f, hi, hi };
        const float bw = (viz.getWidth() - 2 * 11.0f - 6 * 5.0f) / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float hgt = juce::jlimit (0.12f, 0.95f, 0.5f + gains[i] / 30.0f)
                              * (viz.getHeight() - 18.0f);
            const float bx = viz.getX() + 11.0f + i * (bw + 5.0f);
            juce::ColourGradient grad (ui::glowOrange, 0.0f, viz.getBottom() - 9.0f - hgt,
                                       ui::glowOrange.withAlpha (0.15f), 0.0f, viz.getBottom() - 9.0f, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bx, viz.getBottom() - 9.0f - hgt, bw, hgt, 2.0f);
        }
    }

    // ---- cabs (one card per rig lane)
    {
        const int count = processor.getRigCount();
        for (int s = 0; s < count; ++s)
        {
            const auto cabB = cabLaneB[s];
            const bool compact = cabB.getHeight() < 300;
            drawPedalFrame (g, cabB, count > 1 ? "Cab " + juce::String (s + 1)
                                               : juce::String ("Cab IR"), {});

            // IR's V1/V2 badge (when TONE3000 reports it via .meta)
            if (const auto irArch = archBadgeForIr (s); irArch.isNotEmpty())
            {
                auto badge = juce::Rectangle<float> ((float) cabB.getX() + 12.0f,
                                                     (float) cabB.getY() + (compact ? 32.0f : 38.0f),
                                                     26.0f, 15.0f);
                g.setColour (ui::accent.withAlpha (irArch == "A2" ? 0.9f : 0.45f));
                g.drawRoundedRectangle (badge, 4.0f, 1.0f);
                g.setFont (ui::monoFont (8.0f, true));
                g.drawText (irArch, badge, juce::Justification::centred);
            }

            // photo (only on the large card) or IR name
            const auto irName = processor.getIrName (s);
            if (! compact && cabImages[s].isValid())
            {
                drawPhoto (g, cabImages[s], { cabB.getX() + 12, cabB.getY() + 60,
                                              cabB.getWidth() - 24, 46 });
            }
            else
            {
                g.setFont (ui::monoFont (8.5f));
                g.setColour (juce::Colour (0xffb4bbc4));
                g.drawFittedText (irName.isNotEmpty()
                                      ? irName
                                      : juce::String ("- no IR -"),
                                  cabB.getX() + (compact ? 46 : 12), cabB.getY() + (compact ? 32 : 60),
                                  cabB.getWidth() - (compact ? 84 : 24), compact ? 22 : 34,
                                  juce::Justification::topLeft, compact ? 2 : 3);
            }
        }
    }

    // ---- Mixer (sum of the rigs; +/- controls the AMP+CAB pairs)
    {
        const int count = processor.getRigCount();
        drawPedalFrame (g, mixerB, "Mixer", {});

        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::accent);
        g.drawText (juce::CharPointer_UTF8 ("SUM \xce\xa3 \xc2\xb7 " ),
                    mixerB.getX() + 12, mixerB.getY() + 30, 60, 11,
                    juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.drawText (juce::String (count) + (count > 1 ? " rigs" : " rig"),
                    mixerB.getX() + 52, mixerB.getY() + 30, 60, 11,
                    juce::Justification::centredLeft);

        // AIR column label
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("GLOBAL", mixerB.getRight() - 18 - 44 - 6, mixerB.getCentreY() - 34,
                    56, 12, juce::Justification::centred);
    }

    // ---- EQ (with live bars reflecting LOW/MID/HIGH)
    if (! eqB.isEmpty())
    {
        drawPedalFrame (g, eqB, "EQ", juce::String (juce::CharPointer_UTF8 ("3 bands \xc2\xb7 post-cab")));

        auto viz = juce::Rectangle<float> ((float) eqB.getX() + 13.0f, (float) eqB.getY() + 38.0f,
                                           (float) eqB.getWidth() - 26.0f, 62.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (viz, 2.0f, 1.0f);

        const float lo = processor.apvts.getRawParameterValue ("eqLow")->load();
        const float mi = processor.apvts.getRawParameterValue ("eqMid")->load();
        const float hi = processor.apvts.getRawParameterValue ("eqHigh")->load();
        const float gains[7] = { lo, lo, (lo + mi) / 2.0f, mi, (mi + hi) / 2.0f, hi, hi };
        const float bw = (viz.getWidth() - 2 * 11.0f - 6 * 5.0f) / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float h = juce::jlimit (0.12f, 0.95f, 0.5f + gains[i] / 30.0f)
                            * (viz.getHeight() - 18.0f);
            const float bx = viz.getX() + 11.0f + i * (bw + 5.0f);
            juce::ColourGradient grad (ui::accent, 0.0f, viz.getBottom() - 9.0f - h,
                                       ui::accent.withAlpha (0.15f), 0.0f, viz.getBottom() - 9.0f, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bx, viz.getBottom() - 9.0f - h, bw, h, 2.0f);
        }
    }

    // ---- amps (one head per rig lane)
    for (int lane = 0; lane < processor.getRigCount(); ++lane)
    {
        const auto ampB = ampLaneB[lane];
        const bool compact = ampB.getHeight() < 300;
        auto bf = ampB.toFloat();
        g.setGradientFill ({ ui::ampTop, 0.0f, bf.getY(), ui::ampBottom, 0.0f, bf.getBottom(), false });
        g.fillRoundedRectangle (bf, 2.0f);
        g.setColour (ui::accent.withAlpha (0.28f));
        g.drawRoundedRectangle (bf.reduced (0.5f), 2.0f, 1.0f);

        // accent stripe at the top
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (bf, 2.0f);
            g.reduceClipRegion (clip);
            juce::ColourGradient grad (ui::accent.withAlpha (0.0f), bf.getX(), 0.0f,
                                       ui::accent.withAlpha (0.0f), bf.getRight(), 0.0f, false);
            grad.addColour (0.5, ui::accent);
            g.setGradientFill (grad);
            g.fillRect (bf.getX(), bf.getY(), bf.getWidth(), 4.0f);
            g.restoreState();
        }

        const auto modelName = processor.getModelName (lane);

        g.setFont (ui::uiFont (12.0f, true));
        g.setColour (ui::textBright);
        g.drawText (processor.getRigCount() > 1 ? "AMP " + juce::String (lane + 1)
                                                : juce::String ("AMP HEAD"),
                    ampB.getX() + 18, ampB.getY() + (compact ? 10 : 19), 170, 14,
                    juce::Justification::centredLeft);

        if (compact)
        {
            // capture name on the line below the title (no subtitle/info)
            g.setFont (ui::uiFont (13.0f, true));
            g.setColour (modelName.isNotEmpty() ? ui::textBright : ui::textMuted);
            g.drawText (modelName.isNotEmpty()
                            ? modelName
                            : juce::String ("- no capture -"),
                        ampB.getX() + 18, ampB.getY() + 26, ampB.getWidth() - 36 - 34, 16,
                        juce::Justification::centredLeft);
        }
        else
        {
            g.setFont (ui::monoFont (8.0f));
            g.setColour (ui::accent);
            g.drawText (juce::CharPointer_UTF8 ("AMPLIFIER \xc2\xb7 NAM CAPTURE"),
                        ampB.getX() + 18, ampB.getY() + 35, 180, 11, juce::Justification::centredLeft);

            g.setFont (ui::uiFont (18.0f, true));
            g.setColour (modelName.isNotEmpty() ? ui::textBright : ui::textMuted);
            // -40 reserves the right corner for the V1/V2 badge
            g.drawText (modelName.isNotEmpty() ? modelName
                                               : juce::String ("- no capture -"),
                        ampB.getX() + 18, ampB.getY() + 54, ampB.getWidth() - 36 - 40, 22,
                        juce::Justification::centredLeft);
        }

        // V1/V2 badge of the capture's architecture
        const auto archLabel = processor.getModelArchLabel (lane);
        if (archLabel.isNotEmpty() && modelName.isNotEmpty())
        {
            auto badge = compact
                             ? juce::Rectangle<float> ((float) ampB.getRight() - 18.0f - 28.0f,
                                                       (float) ampB.getY() + 26.0f, 28.0f, 16.0f)
                             : juce::Rectangle<float> ((float) ampB.getRight() - 18.0f - 30.0f,
                                                       (float) ampB.getY() + 52.0f, 30.0f, 18.0f);
            g.setColour (ui::accent.withAlpha (archLabel == "A2" ? 0.9f : 0.45f));
            g.drawRoundedRectangle (badge, 5.0f, 1.0f);
            g.setFont (ui::monoFont (9.0f, true));
            g.drawText (archLabel, badge, juce::Justification::centred);

            // TONE3000 mark: this capture came from the store (design req 5)
            if (t3kMark.isValid() && toneIdForLane (lane) > 0)
            {
                const float mh = badge.getHeight();
                const float mw = mh * t3kMark.getWidth() / (float) t3kMark.getHeight();
                g.drawImage (t3kMark,
                             juce::Rectangle<float> (badge.getX() - 6.0f - mw, badge.getY(), mw, mh),
                             juce::RectanglePlacement::centred);
            }
        }

        if (! compact)
        {
            const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
            juce::String info;
            const double modelSr = processor.getModelExpectedSampleRate (lane);
            if (modelName.isNotEmpty())
            {
                info = (modelSr > 0 ? juce::String (modelSr / 1000.0, 1) + " kHz" + dot : juce::String())
                       + "mono" + dot + "NAM";
                if (processor.isResampling (lane))
                    info += dot + "resample";
            }
            else
            {
                info = "load a capture from the Tone Store";
            }
            g.setFont (ui::monoFont (9.0f));
            g.setColour (juce::Colour (0xff8a929c));
            g.drawText (info, ampB.getX() + 18, ampB.getY() + 80, ampB.getWidth() - 36, 12,
                        juce::Justification::centredLeft);

            if (ampImages[lane].isValid())
                drawPhoto (g, ampImages[lane],
                           { ampB.getX() + 18, ampB.getY() + 98, ampB.getWidth() - 36, 48 });
        }

        // glow bar (tube-style - orange, as in the design)
        {
            auto glow = compact
                            ? juce::Rectangle<float> (bf.getX() + 22.0f, bf.getBottom() - 36.0f,
                                                      bf.getWidth() - 44.0f, 5.0f)
                            : juce::Rectangle<float> (bf.getX() + 22.0f, bf.getBottom() - 66.0f,
                                                      bf.getWidth() - 44.0f, 7.0f);
            const float alpha = processor.hasModelLoaded (lane)
                                    && processor.apvts.getRawParameterValue ("ampOn")->load() > 0.5f
                                ? 0.85f : 0.15f;
            juce::ColourGradient grad (ui::glowOrange.withAlpha (0.0f), glow.getX(), 0.0f,
                                       ui::glowOrange.withAlpha (0.0f), glow.getRight(), 0.0f, false);
            grad.addColour (0.5, ui::glowOrange.withAlpha (alpha));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (glow, 5.0f);
        }
    }

    // ---- dimmed disabled cards (the knobs get setAlpha separately)
    for (const auto& entry : orderedEntries())
    {
        if (entry.id == "amp" || entry.box.isEmpty())
            continue;
        if (auto* p = processor.apvts.getRawParameterValue (onParamIdForFx (entry.id));
            p != nullptr && p->load() <= 0.5f)
        {
            g.setColour (ui::bg.withAlpha (0.55f));
            g.fillRoundedRectangle (entry.box.toFloat(), 2.0f);
        }
    }

    // ---- remove "x" (returns the effect to the drawer; highlights on hover)
    for (const auto& entry : orderedEntries())
    {
        if (entry.id == "amp" || entry.box.isEmpty())
            continue;
        const auto hi = removeHotspot (entry.box);
        const bool hov = hi == hoverHotspot;
        const auto h = hi.toFloat();
        g.setColour (hov ? ui::red.withAlpha (0.95f) : ui::textFaint.withAlpha (0.55f));
        g.drawLine (h.getX() + 4.0f, h.getY() + 4.0f, h.getRight() - 4.0f, h.getBottom() - 4.0f,
                    hov ? 1.8f : 1.4f);
        g.drawLine (h.getRight() - 4.0f, h.getY() + 4.0f, h.getX() + 4.0f, h.getBottom() - 4.0f,
                    hov ? 1.8f : 1.4f);
    }

    // ---- "+" on the connectors (insert an effect at that position; highlights on hover)
    for (const auto& [rect, idx] : insertSpots())
    {
        const bool hov = rect == hoverHotspot;
        auto rf = rect.toFloat().reduced (hov ? 0.0f : 2.0f);
        g.setColour (ui::cardTop);
        g.fillEllipse (rf);
        g.setColour (ui::accent.withAlpha (hov ? 0.95f : 0.4f));
        g.drawEllipse (rf, hov ? 1.6f : 1.2f);
        g.setColour (ui::accent.withAlpha (hov ? 1.0f : 0.85f));
        g.setFont (ui::uiFont (hov ? 16.0f : 14.0f, true));
        g.drawText ("+", rect, juce::Justification::centred);
    }

    // file drop target (capture/IR/vst3 dragged from Explorer)
    if (! dropHighlight.isEmpty())
    {
        g.setColour (ui::accent.withAlpha (0.9f));
        g.drawRoundedRectangle (dropHighlight.toFloat().reduced (1.5f), 2.0f, 2.5f);
        g.setColour (ui::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (dropHighlight.toFloat(), 2.0f);
    }

    // ---- "+ EFFECT" button (drawer)
    {
        auto bf = addFxB.toFloat();
        g.setColour (ui::accent.withAlpha (addFxB == hoverHotspot ? 0.8f : 0.35f));
        const float dash[] = { 5.0f, 4.0f };
        juce::Path outline;
        outline.addRoundedRectangle (bf.reduced (1.0f), 2.0f);
        juce::PathStrokeType stroke (1.4f);
        juce::Path dashed;
        stroke.createDashedStroke (dashed, outline, dash, 2);
        g.fillPath (dashed);

        g.setColour (ui::accent.withAlpha (0.9f));
        g.setFont (ui::uiFont (26.0f, true));
        g.drawText ("+", addFxB.withHeight (40).withY (addFxB.getCentreY() - 34),
                    juce::Justification::centred);
        g.setFont (ui::monoFont (9.0f, true));
        g.drawText ("EFFECT", addFxB.withHeight (14).withY (addFxB.getCentreY() + 8),
                    juce::Justification::centred);
    }

    // ---- drag-and-drop feedback (ghost + insertion indicator)
    if (draggingId.isNotEmpty())
    {
        const auto source = boxForFx (draggingId);

        // dimmed source
        g.setColour (ui::bg.withAlpha (0.55f));
        g.fillRoundedRectangle (source.toFloat(), 2.0f);

        // insertion line
        const auto entries = orderedEntries();
        if (dropIndex >= 0)
        {
            const float ix = dropIndex < (int) entries.size()
                                 ? (float) entries[(size_t) dropIndex].box.getX() - 16.0f
                                 : (float) entries.back().box.getRight() + 16.0f;
            g.setColour (ui::accent);
            g.fillRoundedRectangle (ix - 2.0f, (float) source.getY() - 8.0f, 4.0f,
                                    (float) source.getHeight() + 16.0f, 2.0f);
        }

        // card ghost following the mouse
        auto ghost = source.toFloat().withX (dragMouseX - (float) dragGrabDx);
        g.setColour (ui::cardTop.withAlpha (0.85f));
        g.fillRoundedRectangle (ghost, 2.0f);
        g.setColour (ui::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (ghost, 2.0f, 1.5f);
        g.setFont (ui::uiFont (13.0f, true));
        g.setColour (ui::textBright);
        g.drawText (fxDisplayName (draggingId), ghost.reduced (12.0f).removeFromTop (30.0f),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
RigContent::RigContent (GuitarRigNAMProcessor& p)
    : processor (p)
{
    // restore the persisted theme (dark default) — press 'L' to toggle
    if (processor.apvts.state.getProperty ("uiTheme", "dark").toString() == "light")
    {
        ui::applyTheme (true);
        lookAndFeel.applyColours();
    }
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (cpuMeter);

    audioButton.onClick = []
    {
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->showAudioSettingsDialog();
    };
    addChildComponent (audioButton);
    audioButton.setVisible (juce::JUCEApplicationBase::isStandaloneApp());

    storeButton.getProperties().set ("accent", true);
    storeButton.onClick = [this] { storeOverlay->open(); };
    addAndMakeVisible (storeButton);

    prevButton.onClick = [this] { processor.loadAdjacentPreset (-1); };
    nextButton.onClick = [this] { processor.loadAdjacentPreset (1); };
    saveButton.onClick = [this] { saveCurrentPreset(); };
    presetPill.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (presetPill);

    // inline preset name editor (appears over the pill)
    presetNameEditor.setFont (ui::uiFont (13.0f, true));
    presetNameEditor.setJustification (juce::Justification::centred);
    presetNameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff14181d));
    presetNameEditor.setColour (juce::TextEditor::outlineColourId, ui::accent);
    presetNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, ui::accent);
    presetNameEditor.setColour (juce::TextEditor::textColourId, ui::text);
    presetNameEditor.onReturnKey = [this]
    {
        const auto name = juce::File::createLegalFileName (presetNameEditor.getText().trim());
        presetNameEditor.setVisible (false);
        if (name.isNotEmpty())
        {
            processor.savePreset (processor.getPresetsDirectory().getChildFile (name + ".xml"));
            saveFlashTicks = 27;
        }
        grabKeyboardFocus();
    };
    presetNameEditor.onEscapeKey = [this]
    {
        presetNameEditor.setVisible (false);
        grabKeyboardFocus();
    };
    presetNameEditor.onFocusLost = [this] { presetNameEditor.setVisible (false); };
    addChildComponent (presetNameEditor);

    // tooltips + shortcuts
    setWantsKeyboardFocus (true);
    for (auto* b : std::initializer_list<juce::Button*> { &prevButton, &nextButton, &saveButton,
                                                          &presetPill, &audioButton, &storeButton,
                                                          &tunerToggle })
        b->setMouseClickGrabsKeyboardFocus (false);

    prevButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("Previous preset (\xe2\x86\x90)")));
    nextButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("Next preset (\xe2\x86\x92)")));
    saveButton.setTooltip ("Saves the current preset (no name: asks for one)");
    presetPill.setTooltip ("Choose preset / Save as new");
    storeButton.setTooltip ("Search and download tones from TONE3000");
    audioButton.setTooltip ("Driver, device, sample rate and buffer (ASIO/WASAPI)");
    tunerToggle.setTooltip ("Enable/disable the tuner (T)");

    // dev: GUITARRIG_TUNER=off starts with the tuner disabled (UI test)
    if (juce::SystemStats::getEnvironmentVariable ("GUITARRIG_TUNER", "") == "off")
        processor.apvts.state.setProperty ("tunerOn", false, nullptr);

    tunerToggle.getProperties().set ("chip", true);
    tunerToggle.getProperties().set ("chipActive", isTunerOn());
    tunerToggle.onClick = [this]
    {
        const bool newState = ! isTunerOn();
        processor.apvts.state.setProperty ("tunerOn", newState, nullptr);
        tunerToggle.getProperties().set ("chipActive", newState);
        tunerToggle.repaint();
        if (! newState)
        {
            tunerFreq = -1.0;
            tunerNote.clear();
            tunerStringIndex = -1;
        }
    };
    addAndMakeVisible (tunerToggle);

    // tuner MUTE: silences the output while the tuner is on
    muteChip.getProperties().set ("chip", true);
    muteChip.getProperties().set ("chipActive", false);
    muteChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Silences the output while the tuner is on (tune in silence)")));
    muteChip.setMouseClickGrabsKeyboardFocus (false);
    muteChip.onClick = [this]
    {
        tunerMuteWanted = ! tunerMuteWanted;
        muteChip.getProperties().set ("chipActive", tunerMuteWanted);
        muteChip.repaint();
    };
    addAndMakeVisible (muteChip);

    // quick RECORDER: output WAV in Documents\PedalForge NAM\Recordings
    recChip.getProperties().set ("chip", true);
    recChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Records the output as WAV (Documents\\PedalForge NAM\\Recordings)")));
    recChip.setMouseClickGrabsKeyboardFocus (false);
    recChip.onClick = [this]
    {
        if (processor.isRecording())
        {
            processor.stopRecording();
            recSavedTicks = 45;
        }
        else
        {
            if (processor.startRecording() != juce::File())
                recStartMs = juce::Time::currentTimeMillis();
        }
        recChip.getProperties().set ("chipActive", processor.isRecording());
        recChip.repaint();
    };
    addAndMakeVisible (recChip);

    // A/B: compares two complete settings
    abButton.getProperties().set ("chip", true);
    abButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "A/B: toggles between two complete rig settings (the current one is saved in the active slot)")));
    abButton.setMouseClickGrabsKeyboardFocus (false);
    abButton.onClick = [this]
    {
        processor.toggleAB();
        abButton.setButtonText (processor.getABIndex() == 0 ? "A" : "B");
    };
    addAndMakeVisible (abButton);

    // STAGE: performance mode - only the essentials, huge (F key)
    perfChip.getProperties().set ("chip", true);
    perfChip.getProperties().set ("chipActive", false);
    perfChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Stage mode (F): hides the chain and shows preset, tuner and "
        "meters at large size. Esc returns.")));
    perfChip.setMouseClickGrabsKeyboardFocus (false);
    perfChip.onClick = [this] { setPerfMode (! perfMode); };
    addAndMakeVisible (perfChip);

    // AUTO-ECO: switches to the light capture by itself when CPU goes over 90%
    autoEcoChip.getProperties().set ("chip", true);
    autoEcoChip.setClickingTogglesState (true);
    autoEcoChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "With CPU above 90%%, automatically switches to the light "
        "capture version (when available)")));
    autoEcoChip.setMouseClickGrabsKeyboardFocus (false);
    autoEcoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "autoEco", autoEcoChip);
    addAndMakeVisible (autoEcoChip);

    chainView = std::make_unique<ChainView> (processor,
                                             [this] (int lane) { chooseModelSource (lane); },
                                             [this] (int slot) { chooseIrSource (slot); },
                                             [this] (int slot) { chooseExtPluginFile (slot); },
                                             [this] (int slot) { openExtPluginWindow (slot); });

    // close the hosted plugin's panel before any change/disposal
    processor.onExternalPluginWillChange =
        [safe = juce::Component::SafePointer<RigContent> (this)] (int slot)
        {
            if (safe != nullptr)
                safe->closeExtPluginWindow (slot);
        };
    chainViewport.setViewedComponent (chainView.get(), false);
    chainViewport.setScrollBarsShown (false, true);
    chainViewport.setScrollBarThickness (9);
    addAndMakeVisible (chainViewport);

    storeOverlay = std::make_unique<StoreOverlay> (processor);
    addChildComponent (*storeOverlay);

    // amp-card "variations": open the Tone Store details for this tone, aimed at
    // the lane so the picked capture replaces the one playing there.
    chainView->onShowVariations =
        [safe = juce::Component::SafePointer<RigContent> (this)]
        (int lane, int toneId, juce::Component* anchor)
        {
            if (safe != nullptr)
                safe->storeOverlay->showVariationPicker (lane, toneId, anchor);
        };

    // Drums module: overlay + top bar button + drum VST window
    drumOverlay = std::make_unique<DrumOverlay> (processor);
    addChildComponent (*drumOverlay);
    drumOverlay->onChooseVst = [this] { chooseDrumVstFile(); };
    drumOverlay->onOpenVstPanel = [this] { openDrumVstWindow(); };
    drumButton.onClick = [this] { drumOverlay->open(); };
    drumButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Electronic drums: grooves by genre, score and grid")));
    drumButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (drumButton);

    // drum ribbon at the top (follow along without opening the module)
    drumRibbon = std::make_unique<DrumRibbon> (processor.drumEngine);
    drumRibbon->onOpen = [this] { drumOverlay->open(); };
    drumRibbon->setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Follow along with the drums - click to open the module")));
    addAndMakeVisible (*drumRibbon);

    processor.onDrumPluginWillChange =
        [safe = juce::Component::SafePointer<RigContent> (this)]
        {
            if (safe != nullptr)
            {
                safe->closeDrumVstWindow();
                if (safe->drumOverlay != nullptr)
                    safe->drumOverlay->refreshSourceRow();
            }
        };

    // Dev: GUITARRIG_EXT_PLUGIN=<.vst3 path> loads into the slot at startup.
    {
        const auto extFlag = juce::SystemStats::getEnvironmentVariable ("GUITARRIG_EXT_PLUGIN", "");
        if (extFlag.isNotEmpty())
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<RigContent> (this), extFlag]
                {
                    if (safe != nullptr)
                        safe->processor.loadExternalPluginAsync (0, juce::File (extFlag));
                });
    }

    // Dev flags: GUITARRIG_OPEN_DRUMS=1 opens the Drums module at startup;
    // =play also starts playback (transport test without synthetic clicks).
    {
        const auto flag = juce::SystemStats::getEnvironmentVariable ("GUITARRIG_OPEN_DRUMS", "");
        if (flag.isNotEmpty())
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<RigContent> (this), flag]
                {
                    if (safe == nullptr)
                        return;
                    safe->drumOverlay->open();
                    if (flag == "play")
                        safe->processor.drumEngine.playing.store (true);
                    if (flag == "meter")   // test: 4/4, 3/4, 6/8, 7/8
                    {
                        auto& e = safe->processor.drumEngine;
                        e.setMeter (2, 3, 4); e.setMeter (3, 6, 8);
                    }
                    if (flag == "gen" || flag == "genfill")  // generator test (3rd = 7/8)
                    {
                        safe->processor.drumEngine.setMeter (2, 7, 8);
                        safe->drumOverlay->devOpenGenerator();
                        if (flag == "genfill")
                            safe->drumOverlay->devGenerateAll();
                    }
                });
    }

    // Dev flag: GUITARRIG_OPEN_STORE=explore|library opens the store at startup.
    {
        const auto flag = juce::SystemStats::getEnvironmentVariable ("GUITARRIG_OPEN_STORE", "");
        if (flag == "library" || flag == "explore")
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<RigContent> (this), flag]
                {
                    if (safe != nullptr)
                        flag == "library" ? safe->storeOverlay->openOnLibrary()
                                          : safe->storeOverlay->open();
                });
    }

    setSize (designWidth, designHeight);
    startTimerHz (30);
}

RigContent::~RigContent()
{
    processor.onExternalPluginWillChange = nullptr;
    processor.onDrumPluginWillChange = nullptr;
    closeAllExtPluginWindows();
    closeDrumVstWindow();
    setLookAndFeel (nullptr);
}

void RigContent::resized()
{
    const int W = getWidth();

    storeOverlay->setBounds (getLocalBounds());
    drumOverlay->setBounds (getLocalBounds());

    // ---- top bar (60 px)
    storeButton.setBounds (W - 18 - 108, 13, 108, 34);
    audioButton.setBounds (storeButton.getX() - 8 - 82, 13, 82, 34);
    drumButton.setBounds (audioButton.getX() - 8 - 78, 13, 78, 34);
    const int metersRight = drumButton.getX() - 16;
    const int meterW = 58, cpuW = 48;
    cpuMeter.setBounds (metersRight - cpuW, 34, cpuW, 7);
    inMeter.setBounds (metersRight - cpuW - 14 - meterW, 17, meterW, 7);
    outMeter.setBounds (metersRight - cpuW - 14 - meterW, 32, meterW, 7);

    {
        const int pillW = 145, navW = 32, saveW = 64, gap = 8;
        const int groupW = navW + gap + pillW + gap + navW + gap + saveW
                           + gap + 40 + 6 + 66; // + A/B + REC
        // shifted left so it clears the meters (reserve ~40px for IN/OUT labels)
        int x = juce::jmin ((W - groupW) / 2, inMeter.getX() - 40 - groupW);
        x = juce::jmax (x, 186);
        prevButton.setBounds (x, 13, navW, 34);
        x += navW + gap;
        presetPill.setBounds (x, 13, pillW, 34);
        x += pillW + gap;
        nextButton.setBounds (x, 13, navW, 34);
        x += navW + gap;
        saveButton.setBounds (x, 13, saveW, 34);
        x += saveW + gap;
        abButton.setBounds (x, 16, 40, 28);
        x += 40 + 6;
        recChip.setBounds (x, 16, 66, 28);
    }

    // ---- drum ribbon (top) + chain (scrollable) + bottom bar (chips)
    if (drumRibbon != nullptr)
        drumRibbon->setBounds (18, 62, W - 36, 54);
    chainViewport.setBounds (0, 120, W, getHeight() - 120 - 60);
    const int by = getHeight() - 60 + 16;
    tunerToggle.setBounds (22, by, 76, 28);
    muteChip.setBounds (102, by, 48, 28);
    autoEcoChip.setBounds (154, by, 78, 28);
    perfChip.setBounds (236, by, 58, 28);
}

void RigContent::setPerfMode (bool shouldBeOn)
{
    perfMode = shouldBeOn;
    chainViewport.setVisible (! perfMode);
    perfChip.getProperties().set ("chipActive", perfMode);
    perfChip.repaint();
    repaint();
    grabKeyboardFocus();
}

bool RigContent::isTunerOn() const
{
    return (bool) processor.apvts.state.getProperty ("tunerOn", true);
}

void RigContent::paint (juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();

    // overall background (radial at the top)
    {
        juce::ColourGradient grad (ui::bgTop, W * 0.5f, -H * 0.1f, ui::bg, W * 0.5f, H * 0.7f, true);
        g.setGradientFill (grad);
        g.fillAll();
    }

    // ---- top bar
    {
        g.setGradientFill ({ ui::barTop, 0.0f, 0.0f, ui::barBottom, 0.0f, 60.0f, false });
        g.fillRect (0, 0, W, 60);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (0, 59, W, 1);

        // logo with glow
        auto logo = juce::Rectangle<float> (18.0f, 15.0f, 30.0f, 30.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (logo.expanded (3.0f), 2.0f);
        g.setGradientFill ({ ui::accent, logo.getX(), logo.getY(),
                             ui::accentDark, logo.getRight(), logo.getBottom(), false });
        g.fillRoundedRectangle (logo, 2.0f);
        {
            juce::Path diamond;
            diamond.addRoundedRectangle (-5.0f, -5.0f, 10.0f, 10.0f, 2.0f);
            diamond.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f)
                                        .translated (logo.getCentre()));
            g.setColour (ui::bg);
            g.fillPath (diamond);
        }

        g.setFont (ui::uiFont (16.0f, true));
        g.setColour (ui::textBright);
        g.drawText ("PedalForge", 56, 17, 110, 26, juce::Justification::centredLeft);

        auto badge = juce::Rectangle<float> (146.0f, 22.0f, 40.0f, 17.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        g.drawRoundedRectangle (badge, 5.0f, 1.0f);
        g.setFont (ui::monoFont (9.0f, true));
        g.setColour (ui::accent);
        g.drawText ("NAM", badge, juce::Justification::centred);

        // meter labels
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("IN", inMeter.getX() - 28, inMeter.getY() - 4, 24, 12, juce::Justification::centredRight);
        if (clipTicks > 0)
        {
            g.setColour (ui::red);
            g.setFont (ui::monoFont (8.0f, true));
            g.drawText ("CLIP", outMeter.getX() - 32, outMeter.getY() - 4, 28, 12,
                        juce::Justification::centredRight);
            g.setColour (ui::textFaint);
            g.setFont (ui::monoFont (8.0f));
        }
        else
        {
            g.drawText ("OUT", outMeter.getX() - 28, outMeter.getY() - 4, 24, 12,
                        juce::Justification::centredRight);
        }
        {
            const float cpu = processor.cpuLoad.load();
            const bool overload = cpu >= 0.9f;
            g.setColour (overload ? ui::red : ui::textFaint);
            g.setFont (ui::monoFont (8.0f, overload));
            g.drawText ((overload ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa0 CPU "))
                                  : juce::String ("CPU "))
                            + juce::String ((int) (cpu * 100.0f)) + "%",
                        cpuMeter.getX() - 14, cpuMeter.getY() - 14, 76, 12,
                        juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (audioButton.getX() - 15, 16, 1, 28);
    }

    // ---- tuner bar
    {
        const int barY = H - 60;
        g.setGradientFill ({ ui::barTop, 0.0f, (float) barY, ui::barBottom, 0.0f, (float) H, false });
        g.fillRect (0, barY, W, 60);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (0, barY, W, 1);

        const int cy = barY + 30;
        const bool tunerOn = isTunerOn();
        if (! tunerOn)
            g.beginTransparencyLayer (0.3f); // tuner off: everything dimmed

        // strings
        int sx = 300;
        for (int i = 0; i < 6; ++i)
        {
            auto chip = juce::Rectangle<float> ((float) sx, (float) cy - 12, 24.0f, 24.0f);
            const bool active = i == tunerStringIndex;
            g.setColour (active ? ui::accent.withAlpha (0.14f) : ui::glass());
            g.fillRoundedRectangle (chip, 7.0f);
            g.setColour (active ? ui::accent : juce::Colours::white.withAlpha (0.09f));
            g.drawRoundedRectangle (chip, 7.0f, 1.0f);
            g.setFont (ui::monoFont (10.0f, true));
            g.setColour (active ? ui::accent : juce::Colour (0xff99a1ab));
            g.drawText (kStringNames[i], chip, juce::Justification::centred);
            sx += 30;
        }

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (sx + 8, cy - 14, 1, 28);

        // note + cents
        const bool hasPitch = tunerFreq > 0.0;
        g.setFont (ui::uiFont (30.0f, true));
        g.setColour (hasPitch ? ui::accent : ui::textMuted);
        g.drawText (hasPitch ? tunerNote : juce::String ("-"), sx + 22, barY + 10, 64, 40,
                    juce::Justification::centred);
        if (hasPitch)
        {
            g.setFont (ui::monoFont (11.0f));
            g.setColour (std::abs (tunerCents) < 5.0 ? ui::green : ui::yellow);
            g.drawText ((tunerCents >= 0 ? "+" : "") + juce::String ((int) tunerCents)
                            + juce::String (juce::CharPointer_UTF8 ("\xc2\xa2")),
                        sx + 86, cy - 8, 40, 16, juce::Justification::centredLeft);
        }

        // cents ruler
        {
            auto meter = juce::Rectangle<float> ((float) sx + 136, (float) barY + 14, 240.0f, 32.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            for (float mx = meter.getX(); mx <= meter.getRight(); mx += 12.0f)
                g.fillRect (mx, meter.getY() + 6.0f, 1.0f, 20.0f);

            juce::ColourGradient grad (ui::red, meter.getX(), 0.0f, ui::red, meter.getRight(), 0.0f, false);
            grad.addColour (0.44, ui::green);
            grad.addColour (0.56, ui::green);
            g.setGradientFill (grad);
            g.setOpacity (0.4f);
            g.fillRect (meter.getX(), meter.getCentreY() - 1.5f, meter.getWidth(), 3.0f);
            g.setOpacity (1.0f);

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (meter.getCentreX() - 1.0f, meter.getY(), 2.0f, meter.getHeight());

            if (hasPitch)
            {
                const float nx = meter.getCentreX()
                                 + (float) juce::jlimit (-50.0, 50.0, tunerCents) / 50.0f
                                       * (meter.getWidth() / 2.0f - 6.0f);
                g.setColour (ui::accent.withAlpha (0.4f));
                g.fillRoundedRectangle (nx - 3.0f, meter.getY() - 2.0f, 6.0f, meter.getHeight() + 4.0f, 3.0f);
                g.setColour (ui::accent);
                g.fillRoundedRectangle (nx - 1.5f, meter.getY() - 2.0f, 3.0f, meter.getHeight() + 4.0f, 2.0f);
            }
        }

        if (! tunerOn)
            g.endTransparencyLayer();

        // compact status on the right
        {
            const double sr = processor.getSampleRate();
            const int bs = processor.getBlockSize();
            const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");

            juce::String status = "A = 440 Hz";
            if (sr > 0)
                status = juce::String (sr / 1000.0, 1) + " kHz" + dot + juce::String (bs) + " smp"
                         + dot + "A = 440 Hz";

            const auto err = processor.getLoadError();
            juce::Colour c = ui::textFaint;
            if (err.isNotEmpty())
            {
                status = "Error: " + err;
                c = ui::red;
            }
            else if (ecoNoticeTicks > 0)
            {
                status = juce::String (juce::CharPointer_UTF8 (
                    "Auto ECO enabled (high CPU)"));
                c = ui::accent;
            }
            g.setFont (ui::monoFont (9.5f));
            g.setColour (c);
            g.drawText (status, W - 22 - 360, barY, 360, 60, juce::Justification::centredRight);
        }
    }

    if (perfMode)
        paintPerformanceView (g);
}

void RigContent::paintPerformanceView (juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();
    const auto area = juce::Rectangle<int> (0, 60, W, H - 120);

    // ---- huge preset (click: left = previous, right = next,
    //      center = menu)
    const auto presetName = processor.getCurrentPresetName();
    const bool dirty = presetDirtyCached;
    g.setFont (ui::uiFont (48.0f, true));
    g.setColour (ui::textBright);
    g.drawFittedText ((dirty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) : juce::String())
                          + (presetName.isNotEmpty() ? presetName : juce::String ("(no preset)")),
                      area.getX() + 120, area.getY() + 40, area.getWidth() - 240, 60,
                      juce::Justification::centred, 1);

    // navigation arrows on the sides
    g.setFont (ui::uiFont (40.0f, true));
    g.setColour (ui::textFaint);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x97\x82"), area.getX() + 30, area.getY() + 40, 60, 60,
                juce::Justification::centred);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x96\xb8"), area.getRight() - 90, area.getY() + 40, 60, 60,
                juce::Justification::centred);

    // loaded capture + rigs
    {
        const auto model = processor.getModelName (0);
        juce::String info = model.isNotEmpty()
                                ? model
                                : juce::String ("- no capture -");
        if (processor.getRigCount() > 1)
            info += juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                    + juce::String (processor.getRigCount()) + " rigs";
        g.setFont (ui::monoFont (14.0f));
        g.setColour (ui::accent);
        g.drawText (info, area.getX(), area.getY() + 108, area.getWidth(), 20,
                    juce::Justification::centred);
    }

    // ---- large tuner
    {
        const int cy = area.getCentreY() + 60;
        const bool hasNote = tunerNote.isNotEmpty();
        const bool inTune = hasNote && std::abs (tunerCents) <= 5.0;

        g.setFont (ui::monoFont (84.0f, true));
        g.setColour (! hasNote ? ui::textMuted : inTune ? ui::green : ui::textBright);
        g.drawText (hasNote ? tunerNote : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                    area.getX(), cy - 110, area.getWidth(), 100, juce::Justification::centred);

        // cents ruler: -50 .. +50, needle at the position
        const int barW = juce::jmin (560, W - 200);
        auto bar = juce::Rectangle<float> ((float) (W - barW) / 2.0f, (float) cy + 10.0f,
                                           (float) barW, 12.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (bar, 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        for (int t = -40; t <= 40; t += 10)
        {
            const float tx = bar.getCentreX() + (float) t / 50.0f * bar.getWidth() / 2.0f;
            g.fillRect (tx - 0.5f, bar.getY() - 5.0f, 1.0f, bar.getHeight() + 10.0f);
        }
        g.setColour (ui::green.withAlpha (0.5f));
        g.fillRect (bar.getCentreX() - 1.0f, bar.getY() - 8.0f, 2.0f, bar.getHeight() + 16.0f);

        if (hasNote)
        {
            const float nx = bar.getCentreX()
                             + (float) juce::jlimit (-50.0, 50.0, tunerCents) / 50.0f
                                   * bar.getWidth() / 2.0f;
            g.setColour (inTune ? ui::green : ui::glowOrange);
            g.fillRoundedRectangle (nx - 3.0f, bar.getY() - 10.0f, 6.0f,
                                    bar.getHeight() + 20.0f, 3.0f);

            g.setFont (ui::monoFont (16.0f, true));
            g.drawText ((tunerCents >= 0 ? "+" : "") + juce::String (tunerCents, 1) + " cents",
                        area.getX(), (int) bar.getBottom() + 14, area.getWidth(), 20,
                        juce::Justification::centred);
        }
        else
        {
            g.setFont (ui::monoFont (12.0f));
            g.setColour (ui::textFaint);
            g.drawText ("play a string to tune", area.getX(), (int) bar.getBottom() + 14,
                        area.getWidth(), 18, juce::Justification::centred);
        }
    }

    // ---- hints
    g.setFont (ui::monoFont (10.0f));
    g.setColour (ui::textFaint);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x86\x90/\xe2\x86\x92 presets \xc2\xb7 "
                                        "space toggles the amp \xc2\xb7 "
                                        "T tuner \xc2\xb7 F/Esc back to editing"),
                area.getX(), area.getBottom() - 26, area.getWidth(), 16,
                juce::Justification::centred);
}

void RigContent::timerCallback()
{
    auto toDb = [] (float linear) { return juce::Decibels::gainToDecibels (linear, -80.0f); };

    inMeterDb = juce::jmax (toDb (processor.inputPeak.load()), inMeterDb - 2.2f);
    outMeterDb = juce::jmax (toDb (processor.outputPeak.load()), outMeterDb - 2.2f);
    inMeter.setLevel (inMeterDb);
    outMeter.setLevel (outMeterDb);

    if (processor.outputPeak.load() >= 0.999f)
        clipTicks = 60; // ~2 s warning
    else if (clipTicks > 0)
        --clipTicks;

    const float cpu = processor.cpuLoad.load();
    cpuMeter.setFraction (cpu, cpu > 0.8f ? ui::red : cpu > 0.5f ? ui::yellow : ui::accent);

    // the preset fingerprint is XML serialization - check at 2 Hz, not 30 Hz
    if (tunerTick % 15 == 0)
    {
        processor.settlePresetBaseline();
        presetDirtyCached = processor.isPresetDirty();
    }

    const auto presetName = processor.getCurrentPresetName();
    const bool dirty = presetDirtyCached;
    presetPill.setButtonText (processor.isLoadingModel()
                                  ? juce::String (juce::CharPointer_UTF8 ("Loading..."))
                                  : (presetName.isNotEmpty()
                                         ? (dirty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) + presetName
                                                  : presetName)
                                         : juce::String ("(no preset)")));
    presetPill.dotLit = processor.anyModelLoaded();

    if (saveFlashTicks > 0)
    {
        --saveFlashTicks;
        saveButton.setButtonText (saveFlashTicks > 0 ? "Saved" : "SAVE");
    }

    if (! focusGrabbed && isShowing())
    {
        focusGrabbed = true;
        grabKeyboardFocus();
    }

    chainView->refreshDynamicText();
    refreshSidecarImages();

    // retired VST3 instance is deleted here (message thread, outside audio)
    processor.collectExternalRetired();

    applyEcoSwitchIfNeeded();
    if (ecoNoticeTicks > 0)
        --ecoNoticeTicks;

    if (++tunerTick % 3 == 0 && (isTunerOn() || perfMode))
        analyseTuner();

    // the chip may have changed via state/preset load
    if ((bool) tunerToggle.getProperties()["chipActive"] != isTunerOn())
    {
        tunerToggle.getProperties().set ("chipActive", isTunerOn());
        tunerToggle.repaint();
    }

    // tuner mute only applies with the tuner active (or on stage)
    processor.setTunerMuted (tunerMuteWanted && (isTunerOn() || perfMode));

    // recorder: shows the elapsed time on the chip
    {
        juce::String recText;
        if (processor.isRecording())
        {
            const int secs = (int) ((juce::Time::currentTimeMillis() - recStartMs) / 1000);
            recText = juce::String::fromUTF8 ("\xe2\x96\xa0 ")
                      + juce::String (secs / 60) + ":"
                      + juce::String (secs % 60).paddedLeft ('0', 2);
        }
        else if (recSavedTicks > 0)
        {
            --recSavedTicks;
            recText = "SAVED";
        }
        else
        {
            recText = juce::String::fromUTF8 ("\xe2\x97\x8f REC");
        }
        if (recChip.getButtonText() != recText)
            recChip.setButtonText (recText);
        if ((bool) recChip.getProperties()["chipActive"] != processor.isRecording())
        {
            recChip.getProperties().set ("chipActive", processor.isRecording());
            recChip.repaint();
        }
    }

    if (perfMode)
        repaint(); // large tuner/meters live
    else
    {
        repaint (0, 0, getWidth(), 60);
        repaint (0, getHeight() - 60, getWidth(), 60);
    }
}

void RigContent::applyEcoSwitchIfNeeded()
{
    auto& apvts = processor.apvts;
    const bool eco = apvts.getRawParameterValue ("ampEco")->load() > 0.5f;
    const bool autoEco = apvts.getRawParameterValue ("autoEco")->load() > 0.5f;

    // auto-ECO: CPU above 90% for ~2 s turns on the light mode (never turns
    // off by itself, to avoid constantly switching the tone)
    if (autoEco && ! eco && processor.hasEcoVariant()
        && processor.cpuLoad.load() >= 0.9f)
    {
        if (++cpuHighTicks >= 60)
        {
            cpuHighTicks = 0;
            if (auto* p = apvts.getParameter ("ampEco"))
                p->setValueNotifyingHost (1.0f);
            ecoNoticeTicks = 120; // notice for ~4 s in the bottom bar
        }
    }
    else
    {
        cpuHighTicks = 0;
    }

    // keeps the loaded file consistent with the mode (chip, preset or auto),
    // lane by lane
    const bool ecoNow = apvts.getRawParameterValue ("ampEco")->load() > 0.5f;
    for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
    {
        const auto ecoPath = processor.getModelPathEco (r);
        const auto target = ecoNow && ecoPath.isNotEmpty()
                                ? ecoPath
                                : processor.getModelPathNormal (r);
        if (target.isNotEmpty() && ! processor.isLoadingModel()
            && target != processor.getModelPath (r))
            processor.loadModelAsync (r, juce::File (target));
    }
}

void RigContent::analyseTuner()
{
    const double sr = processor.getSampleRate();
    if (sr <= 0)
        return;

    constexpr int N = 2048;
    float buf[N];
    processor.readTunerBlock (buf, N);

    const double freq = detectPitchHz (buf, N, sr);
    tunerFreq = freq;

    if (freq > 0.0)
    {
        const double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
        const int nearest = juce::roundToInt (midi);
        tunerCents = (midi - nearest) * 100.0;
        tunerNote = kNoteNames[((nearest % 12) + 12) % 12];

        tunerStringIndex = -1;
        double bestDiff = 1.0e9;
        for (int i = 0; i < 6; ++i)
        {
            const double diff = std::abs (std::log2 (freq / kStringFreqs[i]));
            if (diff < bestDiff)
            {
                bestDiff = diff;
                tunerStringIndex = i;
            }
        }
        if (bestDiff > 0.12) // > ~1.4 semitones from any string
            tunerStringIndex = -1;
    }
    else
    {
        tunerNote.clear();
        tunerStringIndex = -1;
    }
}

void RigContent::refreshSidecarImages()
{
    // Reloads when the path changes OR when the sidecar appears later
    // (the photo write is asynchronous to the download).
    auto refresh = [] (const juce::String& path, juce::String& cachedPath, bool& hadImage,
                       auto&& apply)
    {
        const juce::File sidecar (path + ".img");
        const bool exists = path.isNotEmpty() && sidecar.existsAsFile();

        if (path == cachedPath && hadImage == exists)
            return;

        cachedPath = path;
        hadImage = exists;
        juce::Image img;
        if (exists)
            img = juce::ImageFileFormat::loadFrom (sidecar);
        apply (std::move (img));
    };

    for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
    {
        refresh (processor.getModelPath (r), loadedModelPaths[r], ampImagesLoaded[r],
                 [this, r] (juce::Image img) { chainView->setAmpImage (r, std::move (img)); });
        refresh (processor.getIrPath (r), loadedIrPaths[r], cabImagesLoaded[r],
                 [this, r] (juce::Image img) { chainView->setCabImage (r, std::move (img)); });
    }
}

void RigContent::chooseModelSource (int lane)
{
    // Entry point from the signal chain (TONE3000 design requirement): the amp
    // card's LOAD/CHANGE offers the Tone Store first, then a local .nam file.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader (processor.hasModelLoaded (lane) ? "Change capture" : "Load capture");
    menu.addItem (1, "Browse TONE3000 Tone Store\xe2\x80\xa6");
    menu.addItem (2, "Load .nam file from disk\xe2\x80\xa6");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chainView->getLoadButton (lane)),
        [safe = juce::Component::SafePointer<RigContent> (this), lane] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1)      safe->storeOverlay->open();
            else if (r == 2) safe->chooseModelFile (lane);
        });
}

void RigContent::chooseModelFile (int lane)
{
    auto initialDir = juce::File (processor.getModelPath (lane)).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = Tone3000Client::capturesDir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose NAM capture (.nam) for AMP " + juce::String (lane + 1),
        initialDir, "*.nam");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, lane] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.setModelPair (lane, file, {}); // local: no eco pair
                              });
}

void RigContent::chooseExtPluginFile (int slot)
{
    // Menu by CATEGORY: lists the installed .vst3s (system + user folder
    // - the built-in installer uses the user's, no admin), grouped
    // by the built-in catalog (PluginCatalog) + install what's missing + disk.
    juce::Array<juce::File> found;
    for (const auto& dir : { plugcat::systemVst3Dir(), plugcat::userVst3Dir() })
        if (dir.isDirectory())
            for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories,
                                                     false, "*.vst3"))
                found.add (f);

    struct Category { const char* title; std::initializer_list<const char*> keys; };
    static const Category categories[] = {
        { "Reverb & Ambience", { "dragonfly", "reverb", "surge" } },
        { "Airwindows Collection", { "airwindows", "airwin" } },
        { "Pedals & Dynamics (Zam)", { "zam", "zamaudio" } },
        { "Drives & Pedals",            { "fire", "wolf-shaper", "peakeater" } },
        { "Neural / captures",          { "aida", "proteus", "neural" } },
        { "Amp sims",                   { "bias", "amplitube", "guitar rig", "th-u",
                                          "stormblade" } },
    };

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const auto current = processor.getExternalPluginPath (slot);
    juce::Array<bool> used;
    used.insertMultiple (0, false, found.size());

    auto matches = [] (const juce::String& lowerName,
                       std::initializer_list<const char*> keys)
    {
        for (auto* k : keys)
            if (lowerName.contains (k))
                return true;
        return false;
    };

    for (const auto& cat : categories)
    {
        bool any = false;
        for (int i = 0; i < found.size(); ++i)
            if (! used[i] && matches (found[i].getFileName().toLowerCase(), cat.keys))
                any = true;
        if (! any)
            continue;

        menu.addSectionHeader (juce::String (juce::CharPointer_UTF8 (cat.title)));
        for (int i = 0; i < found.size(); ++i)
            if (! used[i] && matches (found[i].getFileName().toLowerCase(), cat.keys))
            {
                menu.addItem (i + 1, found[i].getFileNameWithoutExtension(), true,
                              found[i].getFullPathName() == current);
                used.set (i, true);
            }
    }

    bool anyOther = false;
    for (int i = 0; i < found.size(); ++i)
        if (! used[i])
            anyOther = true;
    if (anyOther)
    {
        menu.addSectionHeader ("Other");
        for (int i = 0; i < found.size(); ++i)
            if (! used[i])
                menu.addItem (i + 1, found[i].getFileNameWithoutExtension(), true,
                              found[i].getFullPathName() == current);
    }

    menu.addSeparator();
    menu.addItem (9100, juce::String (juce::CharPointer_UTF8 (
                      "Manage plugins (install/uninstall)...")));
    menu.addItem (9000, juce::String (juce::CharPointer_UTF8 ("Browse file...")));

    menu.showMenuAsync (juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<RigContent> (this), found, slot] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto* self = safe.getComponent();

            if (result >= 1 && result <= found.size())
            {
                self->processor.loadExternalPluginAsync (slot, found[result - 1]);
                return;
            }

            // plugin manager (store's Plugins tab)
            if (result == 9100)
            {
                self->storeOverlay->openOnPlugins();
                return;
            }

            if (result != 9000)
                return;

            // browse on disk
            auto initialDir = juce::File (self->processor.getExternalPluginPath (slot))
                                  .getParentDirectory();
            if (! initialDir.isDirectory())
                initialDir = juce::File ("C:\\Program Files\\Common Files\\VST3");
            if (! initialDir.isDirectory())
                initialDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

            self->fileChooser = std::make_unique<juce::FileChooser> (
                "Choose VST3 plugin (.vst3)", initialDir, "*.vst3");
            self->fileChooser->launchAsync (
                juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::canSelectDirectories,
                [safe, slot] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (safe != nullptr && file.exists())
                        safe->processor.loadExternalPluginAsync (slot, file);
                });
        });
}

// Floating window with the hosted plugin's panel; closes by itself before
// any instance change (onExternalPluginWillChange).
class ExtPluginWindow : public juce::DocumentWindow
{
public:
    ExtPluginWindow (juce::AudioPluginInstance& inst, std::function<void()> onCloseIn)
        : juce::DocumentWindow (inst.getName(), juce::Colour (0xff14181d),
                                juce::DocumentWindow::closeButton),
          onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* ed = inst.createEditorIfNeeded();
        if (ed != nullptr)
            setContentOwned (ed, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (inst), true);
        setResizable (ed == nullptr || ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
    }

private:
    std::function<void()> onClose;
};

void RigContent::openExtPluginWindow (int slot)
{
    auto* inst = processor.getExternalInstance (slot);
    if (inst == nullptr || ! processor.hasExternalPlugin (slot))
        return;

    if (extWindow[slot] != nullptr)
    {
        extWindow[slot]->toFront (true);
        return;
    }

    extWindow[slot] = std::make_unique<ExtPluginWindow> (
        *inst, [safe = juce::Component::SafePointer<RigContent> (this), slot]
        {
            if (safe != nullptr)
                safe->closeExtPluginWindow (slot);
        });
}

void RigContent::closeExtPluginWindow (int slot)
{
    if (slot >= 0 && slot < GuitarRigNAMProcessor::maxExtSlots)
        extWindow[slot].reset();
}

void RigContent::closeAllExtPluginWindows()
{
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
        extWindow[s].reset();
}

void RigContent::chooseDrumVstFile()
{
    auto initialDir = juce::File (processor.getDrumPluginPath()).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = plugcat::userVst3Dir().isDirectory() ? plugcat::userVst3Dir()
                                                          : plugcat::systemVst3Dir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose the drum VST3", initialDir, "*.vst3");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.exists())
                                      processor.loadDrumPluginAsync (file);
                              });
}

void RigContent::openDrumVstWindow()
{
    auto* inst = processor.getDrumInstance();
    if (inst == nullptr || ! processor.hasDrumPlugin())
        return;

    if (drumVstWindow != nullptr)
    {
        drumVstWindow->toFront (true);
        return;
    }

    drumVstWindow = std::make_unique<ExtPluginWindow> (
        *inst, [safe = juce::Component::SafePointer<RigContent> (this)]
        {
            if (safe != nullptr)
                safe->closeDrumVstWindow();
        });
}

void RigContent::closeDrumVstWindow()
{
    drumVstWindow.reset();
}

void RigContent::chooseIrSource (int slot)
{
    // Cab entry point (TONE3000 design requirement): the CAB card's CHANGE
    // offers the Tone Store first (IRs live there too), then a local file.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader (processor.getIrPath (slot).isNotEmpty() ? "Change IR" : "Load IR");
    menu.addItem (1, "Browse TONE3000 Tone Store\xe2\x80\xa6");
    menu.addItem (2, "Load IR file from disk\xe2\x80\xa6");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chainView->getIrButton (slot)),
        [safe = juce::Component::SafePointer<RigContent> (this), slot] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1)      safe->storeOverlay->open();
            else if (r == 2) safe->chooseIrFile (slot);
        });
}

void RigContent::chooseIrFile (int slot)
{
    auto initialDir = juce::File (processor.getIrPath (slot)).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = Tone3000Client::irsDir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose impulse response for CAB " + juce::String (slot + 1),
        initialDir, "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, slot] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.loadIrAsync (slot, file);
                              });
}

void RigContent::saveCurrentPreset()
{
    const auto name = processor.getCurrentPresetName();
    if (name.isEmpty())
    {
        beginPresetNameEdit();
        return;
    }
    processor.savePreset (processor.getPresetsDirectory().getChildFile (name + ".xml"));
    saveFlashTicks = 27; // ~0.9 s of "Saved"
}

void RigContent::beginPresetNameEdit()
{
    presetNameEditor.setBounds (presetPill.getBounds());
    presetNameEditor.setText (processor.getCurrentPresetName(), juce::dontSendNotification);
    presetNameEditor.setVisible (true);
    presetNameEditor.toFront (true);
    presetNameEditor.grabKeyboardFocus();
    presetNameEditor.selectAll();
}

void RigContent::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const auto current = processor.getCurrentPresetName();
    const auto files = processor.getPresetFiles();

    menu.addItem (1000, juce::String (juce::CharPointer_UTF8 ("Save as new...")));
    if (! files.isEmpty())
        menu.addSeparator();

    for (int i = 0; i < files.size(); ++i)
    {
        const auto name = files.getReference (i).getFileNameWithoutExtension();
        menu.addItem (i + 1, name, true, name == current);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetPill),
                        [this, files] (int result)
                        {
                            if (result == 1000)
                                beginPresetNameEdit();
                            else if (result > 0 && result <= files.size())
                                processor.loadPreset (files.getReference (result - 1));
                        });
}

void RigContent::toggleTuner()
{
    tunerToggle.onClick();
}

bool RigContent::keyPressed (const juce::KeyPress& key)
{
    if (storeOverlay->isVisible())
        return false; // the overlay has its own shortcuts

    if (key == juce::KeyPress::spaceKey)
    {
        if (auto* p = processor.apvts.getParameter ("ampOn"))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        return true;
    }
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        toggleTuner();
        return true;
    }
    if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')
    {
        setPerfMode (! perfMode);
        return true;
    }
    if (key.getTextCharacter() == 'l' || key.getTextCharacter() == 'L')   // light/dark theme
    {
        ui::applyTheme (! ui::lightTheme);
        lookAndFeel.applyColours();
        processor.apvts.state.setProperty ("uiTheme", ui::lightTheme ? "light" : "dark", nullptr);
        if (auto* top = getTopLevelComponent())
            top->repaint();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && perfMode)
    {
        setPerfMode (false);
        return true;
    }
    if (key == juce::KeyPress::leftKey)
    {
        processor.loadAdjacentPreset (-1);
        return true;
    }
    if (key == juce::KeyPress::rightKey)
    {
        processor.loadAdjacentPreset (1);
        return true;
    }
    return false;
}

void RigContent::mouseDown (const juce::MouseEvent& e)
{
    // stage mode: sides navigate presets, center opens the menu
    if (perfMode && e.y > 60 && e.y < getHeight() - 60)
    {
        if (e.x < getWidth() / 4)
            processor.loadAdjacentPreset (-1);
        else if (e.x > getWidth() * 3 / 4)
            processor.loadAdjacentPreset (1);
        else
            showPresetMenu();
        return;
    }

    grabKeyboardFocus(); // click in empty area returns focus to the shortcuts
}

//==============================================================================
GuitarRigNAMEditor::GuitarRigNAMEditor (GuitarRigNAMProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    content.setBounds (0, 0, RigContent::designWidth, RigContent::designHeight);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) RigContent::designWidth
                                           / RigContent::designHeight);
    setResizeLimits (RigContent::designWidth / 2, RigContent::designHeight / 2,
                     RigContent::designWidth * 2, RigContent::designHeight * 2);

    double scale = 1.0;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        scale = juce::jmin (1.0,
                            (area.getWidth() - 60) / (double) RigContent::designWidth,
                            (area.getHeight() - 110) / (double) RigContent::designHeight);
    }
    setSize (juce::roundToInt (RigContent::designWidth * scale),
             juce::roundToInt (RigContent::designHeight * scale));
}

void GuitarRigNAMEditor::resized()
{
    const float scale = (float) getWidth() / (float) RigContent::designWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
}
