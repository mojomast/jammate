#include "PluginEditor.h"

#include "PluginCatalog.h"

#include <BinaryData.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace ui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
        BinaryData::SpaceGroteskRegular_ttf, BinaryData::SpaceGroteskRegular_ttfSize);
    static juce::Typeface::Ptr boldTf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::SpaceGroteskBold_ttf, BinaryData::SpaceGroteskBold_ttfSize);
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
// ---------- afinador: detecção de pitch (NSDF/MPM simplificado) ----------
double detectPitchHz (const float* x, int n, double sr)
{
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
        energy += (double) x[i] * x[i];
    if (energy / n < 1.0e-5) // silêncio
        return -1.0;

    const int minLag = juce::jmax (2, (int) (sr / 500.0)); // até 500 Hz
    const int maxLag = juce::jmin (n / 2, (int) (sr / 55.0)); // desde 55 Hz
    if (maxLag <= minLag + 2)
        return -1.0;

    std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0.0, norm = 0.0;
        const int m = n - maxLag; // janela fixa para todos os lags
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

    // trava (detent) no valor default + interações refinadas
    if (auto* param = apvts.getParameter (paramId))
    {
        const auto& range = param->getNormalisableRange();
        slider.snapTarget = range.convertFrom0to1 (param->getDefaultValue());
        slider.snapRadius = (range.end - range.start) * 0.04;
        slider.setDoubleClickReturnValue (true, slider.snapTarget); // duplo-clique reseta
    }
    slider.setScrollWheelEnabled (true);                            // roda ajusta
    slider.setVelocityModeParameters (1.0, 1, 0.05, true,           // Ctrl = ajuste fino
                                      juce::ModifierKeys::ctrlModifier);
    slider.setMouseClickGrabsKeyboardFocus (false); // atalhos ficam com o RigContent
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
    // clique no valor -> digitar o número
    valueLabel.setEditable (true, false, true);
    valueLabel.setTooltip ("Clique para digitar o valor");
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
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();

    if (getToggleState())
    {
        g.setColour (ui::accent.withAlpha (0.35f));
        g.fillEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f);
        g.setColour (ui::accent);
        g.fillEllipse (c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff2c323a));
        g.fillEllipse (c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.5f);
    }
}

//==============================================================================
void LevelMeter::setLevel (float newLevelDb)
{
    solid = false;
    const float f = juce::jlimit (0.0f, 1.0f, (newLevelDb + 60.0f) / 60.0f);

    // peak-hold: segura o marcador ~1.5 s e depois deixa escorregar
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
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);

    if (fraction <= 0.003f)
        return;

    auto inner = b.reduced (1.0f);

    if (solid)
    {
        g.setColour (solidColour);
        g.fillRoundedRectangle (inner.withWidth (inner.getWidth() * fraction), 3.0f);
        return;
    }

    juce::ColourGradient grad (ui::green, inner.getX(), 0.0f, ui::red, inner.getRight(), 0.0f, false);
    grad.addColour (0.60, ui::green);
    grad.addColour (0.82, ui::yellow);

    g.saveState();
    g.reduceClipRegion (inner.withWidth (inner.getWidth() * fraction).toNearestInt());
    g.setGradientFill (grad);
    g.fillRoundedRectangle (inner, 3.0f);
    g.restoreState();

    // marcador de peak-hold
    if (peakFrac > 0.02f)
    {
        const float px = inner.getX() + inner.getWidth() * peakFrac;
        g.setColour (peakFrac >= 0.98f ? ui::red : juce::Colours::white.withAlpha (0.85f));
        g.fillRect (px - 1.0f, inner.getY(), 2.0f, inner.getHeight());
    }
}

//==============================================================================
void PillButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (ui::glass());
    g.fillRoundedRectangle (b, 9.0f);
    g.setColour (isHighlighted ? ui::borderHover() : juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (b, 9.0f, 1.0f);

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

        loadButtons[r].setButtonText ("CARREGAR CAPTURE NAM");
        loadButtons[r].setTooltip ("Escolher um arquivo .nam do disco");
        loadButtons[r].setMouseClickGrabsKeyboardFocus (false);
        loadButtons[r].onClick = [onLoadModel, r] { onLoadModel (r); };
        addChildComponent (loadButtons[r]);

        // blend da lane vive no card Mixer
        makeKnob (cabBlendKnob[r], ("cab" + n + "Blend").toRawUTF8(),
                  ("RIG " + n).toRawUTF8(), formatPct);
        makeKnob (cabLcKnob[r], ("cab" + n + "LowCut").toRawUTF8(), "LO CUT", formatHz);
        makeKnob (cabHcKnob[r], ("cab" + n + "HighCut").toRawUTF8(), "HI CUT", formatHz);

        cabPhaseChips[r].setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        cabPhaseChips[r].getProperties().set ("chip", true);
        cabPhaseChips[r].setClickingTogglesState (true);
        cabPhaseChips[r].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Inverte a fase deste cab (evita cancelamento em paralelo)")));
        cabPhaseChips[r].setMouseClickGrabsKeyboardFocus (false);
        cabPhaseAtt[r] = std::make_unique<Attachment> (apvts, "cab" + n + "Phase",
                                                       cabPhaseChips[r]);
        addChildComponent (cabPhaseChips[r]);

        cabIrButtons[r].setButtonText ("TROCAR");
        cabIrButtons[r].setTooltip ("Escolher o IR deste cab");
        cabIrButtons[r].setMouseClickGrabsKeyboardFocus (false);
        cabIrButtons[r].onClick = [onLoadIr, r] { onLoadIr (r); };
        addChildComponent (cabIrButtons[r]);
    }

    // seletores de variação nos cartões (menu no rodapé) — os tooltips citam
    // as fontes de estudo de cada família; detalhes em docs/EFEITOS.md
    setupTypeButton (odTypeButton, "odType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Escolher o modelo do drive \xc2\xb7 refs: BYOD, Guitarix, Airwindows (docs/EFEITOS.md)")));
    setupTypeButton (compTypeButton, "compType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Escolher o modelo do compressor \xc2\xb7 refs: LSP Plugins, rkrlv2 (docs/EFEITOS.md)")));
    setupTypeButton (delayTypeButton, "delayType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Escolher o modelo do delay \xc2\xb7 refs: Airwindows, Guitarix (docs/EFEITOS.md)")));
    setupTypeButton (revTypeButton, "revType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Escolher o modelo do reverb \xc2\xb7 refs: Dragonfly, GxPlugins (docs/EFEITOS.md)")));
    setupTypeButton (modTypeButton, "modType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Escolher o tipo de modula\xc3\xa7\xc3\xa3o \xc2\xb7 refs: ToobAmp, GxPlugins, Airwindows (docs/EFEITOS.md)")));
    setupTypeButton (delayDivButton, "delayDiv",
                     juce::String (juce::CharPointer_UTF8 (
                         "Subdivis\xc3\xa3o aplicada ao TAP (1/8. = colcheia pontuada)")));
    setupTypeButton (pitchTypeButton, "pitchType",
                     juce::String (juce::CharPointer_UTF8 (
                         "Escolher o intervalo do pitch \xc2\xb7 ref: rkrlv2/rakarrack (docs/EFEITOS.md)")));
    setupTypeButton (wahModeButton, "wahMode",
                     juce::String (juce::CharPointer_UTF8 (
                         "Auto = envelope \xc2\xb7 Manual = knob FREQ \xc2\xb7 LFO = vaiv\xc3\xa9m \xc2\xb7 ref: Guitarix")));
    setupTypeButton (harmKeyButton, "harmKey",
                     juce::String (juce::CharPointer_UTF8 ("Tom da m\xc3\xbasica")));
    setupTypeButton (harmScaleButton, "harmScale",
                     juce::String (juce::CharPointer_UTF8 ("Escala maior ou menor")));
    setupTypeButton (harmIntervalButton, "harmInterval",
                     juce::String (juce::CharPointer_UTF8 (
                         "Intervalo diat\xc3\xb4nico da segunda voz \xc2\xb7 ref: rkrlv2/rakarrack")));

    rigAddButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Adicionar um rig AMP+CAB em paralelo (at\xc3\xa9 3)")));
    rigRemoveButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Remover o \xc3\xbaltimo rig AMP+CAB")));
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
    // slots de plugin VST3 externo
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        makeKnob (extMixKnob[s], (prefix + "Mix").toRawUTF8(), "MIX", formatPct);

        extLoadButton[s].setButtonText ("CARREGAR VST3");
        extLoadButton[s].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Escolher um plugin .vst3 por categoria (Dragonfly, Airwindows, Zam\xe2\x80\xa6)")));
        extLoadButton[s].onClick = [onLoadExtPlugin, s] { onLoadExtPlugin (s); };
        extUiButton[s].setButtonText ("PAINEL");
        extUiButton[s].setTooltip ("Abrir a interface do plugin hospedado");
        extUiButton[s].onClick = [onOpenExtPluginUi, s] { onOpenExtPluginUi (s); };
        extRemoveButton[s].setButtonText ("REMOVER");
        extRemoveButton[s].setTooltip ("Esvaziar o slot");
        extRemoveButton[s].onClick = [this, s] { processor.clearExternalPlugin (s); };
        for (auto* b : { &extLoadButton[s], &extUiButton[s], &extRemoveButton[s] })
        {
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
    }

    // cards P4 — um efeito por card
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


    // botões do looper (textos dinâmicos em refreshDynamicText)
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
                           "Grava o loop; de novo fecha e toca; depois alterna overdub");
        setupLooperButton (looperPlayButton, 2, "Toca/para o loop gravado");
        setupLooperButton (looperClearButton, 3, "Apaga o loop");
        looperClearButton.setButtonText ("LIMPAR");
        setupLooperButton (looperExportButton, 0,
                           "Salva o loop em WAV (Documentos\\GuitarRig NAM\\Loops)");
        looperExportButton.setButtonText ("WAV");
        looperExportButton.onClick = [this]
        {
            const auto file = processor.exportLoopToWav();
            looperExportButton.setButtonText (file != juce::File() ? "SALVO" : "VAZIO");
            auto* self = this; // MSVC: 'this' em init-capture aninhada resolve errado
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
        led.setTooltip (juce::String (juce::CharPointer_UTF8 ("Liga/desliga o m\xc3\xb3""dulo")));
        led.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (led);
    };
    makeLed (gateLed, "gateOn", gateAtt);
    makeLed (odLed, "odOn", odAtt);
    makeLed (ampLed, "ampOn", ampAtt);
    ampLed.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Liga/desliga a se\xc3\xa7\xc3\xa3o do amp (espa\xc3\xa7o)")));
    makeLed (cabLed, "cabOn", cabAtt);
    makeLed (eqLed, "eqOn", eqAtt);
    makeLed (delayLed, "delayOn", delayAtt);
    makeLed (revLed, "revOn", revAtt);
    makeLed (compLed, "compOn", compAtt);
    makeLed (preEqLed, "preEqOn", preEqAtt);
    makeLed (pitchLed, "pitchOn", pitchAtt);
    makeLed (looperLed, "looperOn", looperAtt);
    looperLed.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Liga/desliga a escuta do loop (a grava\xc3\xa7\xc3\xa3o continua)")));
    makeLed (limLed, "limOn", limAtt);
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
    {
        const auto prefix = s == 0 ? juce::String ("ext") : "ext" + juce::String (s + 1);
        extAtt[s] = std::make_unique<Attachment> (apvts, prefix + "On", extLed[s]);
        extLed[s].setTooltip (juce::String (juce::CharPointer_UTF8 ("Liga/desliga o m\xc3\xb3""dulo")));
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
    modLed.setTooltip (juce::String (juce::CharPointer_UTF8 ("Liga/desliga o m\xc3\xb3""dulo")));
    modLed.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (modLed);

    // TAP tempo do delay
    tapButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Bata duas vezes no andamento da m\xc3\xbasica para definir o tempo do delay")));
    tapButton.setMouseClickGrabsKeyboardFocus (false);
    tapButton.onClick = [this] { applyTapTempo(); };
    addAndMakeVisible (tapButton);

    // presets do compressor: ajustam os 4 knobs de uma vez
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
            "Clean: compress\xc3\xa3o leve e transparente")));
        compPresetChips[1].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Country: squish r\xc3\xa1pido estilo Dyna Comp")));
        compPresetChips[2].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Lead: sustain m\xc3\xa1ximo para solos")));
    }

    // chip ECO: alterna para a versão leve do capture (quando existe)
    ecoChip.getProperties().set ("chip", true);
    ecoChip.setClickingTogglesState (true);
    ecoChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Usa a vers\xc3\xa3o leve do capture (menos CPU). Baixada junto quando o tone oferece.")));
    ecoChip.setMouseClickGrabsKeyboardFocus (false);
    ecoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "ampEco", ecoChip);
    addAndMakeVisible (ecoChip);

    // tooltips dos knobs
    auto tip = [] (std::unique_ptr<KnobComponent>& k, const char* utf8)
    { k->setKnobTooltip (juce::String (juce::CharPointer_UTF8 (utf8))); };
    tip (inputKnob, "Ganho de entrada (antes de tudo)");
    tip (outputKnob, "Volume final de sa\xc3\xad""da");
    tip (gateThreshKnob, "Abre neste n\xc3\xadvel; s\xc3\xb3 fecha 6 dB abaixo (preserva o sustain)");
    tip (gateHoldKnob, "Segura o gate aberto ap\xc3\xb3s o sinal cair");
    tip (gateReleaseKnob, "Tempo para o gate fechar");
    tip (compSustainKnob, "Mais sustain = mais compress\xc3\xa3o (threshold+ratio+makeup)");
    tip (compAttackKnob, "Ataque: alto deixa a palhetada passar antes de comprimir");
    tip (compBlendKnob, "Compress\xc3\xa3o paralela: mistura com o sinal seco");
    tip (compLevelKnob, "Volume de sa\xc3\xad""da do compressor");
    tip (preEqLowKnob, "Graves ANTES do amp (100 Hz) \xe2\x80\x94 muda a satura\xc3\xa7\xc3\xa3o");
    tip (preEqMidKnob, "M\xc3\xa9""dios ANTES do amp (500 Hz)");
    tip (preEqHighKnob, "Agudos ANTES do amp (2.2 kHz)");
    tip (modRateKnob, "Velocidade da modula\xc3\xa7\xc3\xa3o");
    tip (modDepthKnob, "Intensidade da modula\xc3\xa7\xc3\xa3o");
    tip (modMixKnob, "Mistura do efeito no sinal");
    tip (odDriveKnob, "Quantidade de satura\xc3\xa7\xc3\xa3o do pedal");
    tip (odToneKnob, "Brilho do overdrive");
    tip (odLevelKnob, "Volume do overdrive");
    tip (cabAirKnob, "Ar/brilho p\xc3\xb3s-mix dos rigs (shelf 8 kHz)");
    for (int r = 0; r < maxRigs; ++r)
    {
        tip (ampGainKnob[r], "Empurra o sinal para dentro do capture \xe2\x80\x94 age como o gain do amp real");
        tip (ampBassKnob[r], "Graves (150 Hz)");
        tip (ampMidKnob[r], "M\xc3\xa9""dios (500 Hz)");
        tip (ampTrebleKnob[r], "Agudos (1.8 kHz)");
        tip (ampPresKnob[r], "Presen\xc3\xa7""a (4.5 kHz)");
        tip (ampMasterKnob[r], "Volume da se\xc3\xa7\xc3\xa3o do amp");
        tip (cabBlendKnob[r], "Quanto deste rig entra na soma do Mixer");
        tip (cabLcKnob[r], "Corta graves deste cab (20 Hz = desligado)");
        tip (cabHcKnob[r], "Corta agudos deste cab (20 kHz = desligado)");
    }
    tip (eqLowKnob, "Graves p\xc3\xb3s-cab (120 Hz)");
    tip (eqMidKnob, "M\xc3\xa9""dios p\xc3\xb3s-cab (800 Hz)");
    tip (eqHighKnob, "Agudos p\xc3\xb3s-cab (4 kHz)");
    tip (delayTimeKnob, "Tempo entre repeti\xc3\xa7\xc3\xb5""es");
    tip (delayFbKnob, "Quantas repeti\xc3\xa7\xc3\xb5""es (realimenta\xc3\xa7\xc3\xa3o)");
    tip (delayMixKnob, "Mistura do delay no sinal");
    tip (revDecayKnob, "Tamanho/dura\xc3\xa7\xc3\xa3o do reverb");
    tip (revMixKnob, "Mistura do reverb no sinal");
    tip (revPreKnob, "Atraso antes do reverb come\xc3\xa7""ar");
    tip (pitchMixKnob, "Mistura da voz transposta com o sinal seco");
    tip (pitchLevelKnob, "Volume da voz transposta");
    tip (looperLevelKnob, "Volume do loop na mistura");
    tip (limCeilKnob, "Teto do limiter \xe2\x80\x94 nada passa deste n\xc3\xadvel");
    tip (limRelKnob, "Tempo de recupera\xc3\xa7\xc3\xa3o ap\xc3\xb3s limitar");
    for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
        tip (extMixKnob[s], "Mistura do plugin hospedado com o sinal seco");
    tip (wahFreqKnob, "Frequ\xc3\xaancia base do wah (posi\xc3\xa7\xc3\xa3o do pedal no modo Manual)");
    tip (wahRangeKnob, "Quanto o envelope/LFO varre a partir do FREQ");
    tip (wahResKnob, "Resson\xc3\xa2ncia do filtro (o \"quack\")");
    tip (sgSensKnob, "Sensibilidade \xc3\xa0 palhetada (quando o swell recome\xc3\xa7""a)");
    tip (sgRiseKnob, "Tempo do volume subir ap\xc3\xb3s cada nota");
    tip (octSubKnob, "Volume da sub-oitava sint\xc3\xa9tica");
    tip (octDirectKnob, "Volume do sinal direto");
    tip (octToneKnob, "Abafamento da sub-oitava");
    tip (rmFreqKnob, "Frequ\xc3\xaancia da portadora (grave = trem\xc3\xa9r; agudo = sinos)");
    tip (rmMixKnob, "Mistura do efeito");
    tip (bcBitsKnob, "Resolu\xc3\xa7\xc3\xa3o em bits (menos = mais sujo)");
    tip (bcRateKnob, "Sample rate reduzido (aliasing lo-fi)");
    tip (bcMixKnob, "Mistura do efeito");
    tip (harmMixKnob, "Mistura da segunda voz");
    tip (harmLevelKnob, "Volume da segunda voz");
    tip (excFreqKnob, "A partir de onde os harm\xc3\xb4nicos s\xc3\xa3o gerados");
    tip (excAmtKnob, "Quanto brilho \xc3\xa9 somado de volta");
    tip (dsFreqKnob, "Centro da banda \xc3\xa1spera a domar");
    tip (dsSensKnob, "Sensibilidade da detec\xc3\xa7\xc3\xa3o");
    tip (dsAmtKnob, "Profundidade m\xc3\xa1xima do corte din\xc3\xa2mico");
    tip (tapeDriveKnob, "Satura\xc3\xa7\xc3\xa3o da fita");
    tip (tapeBumpKnob, "Head bump: refor\xc3\xa7o de graves em 90 Hz");
    tip (tapeRollKnob, "Rolloff de agudos da fita");
    tip (cnsAmtKnob, "Intensidade da \"cola\" (waveshape seno sutil)");

    updateLayout();
}

// largura do bloco de rigs (lanes empilhadas, largura constante):
// bus 18 + amp 266 + 24 + cab 144 + bus 18 + 12 + mixer 170
int ChainView::rigBlockWidth() const
{
    return 18 + 266 + 24 + 144 + 18 + 12 + 170;
}

void ChainView::updateLayout()
{
    // métricas do design + conectores de 30 px; o bloco de rigs é dinâmico
    int x = 26 + 90 + 30; // margem + card IN + conector
    for (const auto& id : processor.getChainOrder())
        x += (id == "amp" ? rigBlockWidth() : effectCardWidth (id)) + 30;
    x += 74 + 30; // botão "+ EFEITO"
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
        return; // primeiro tap (ou fora da faixa útil): só arma o próximo

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

void ChainView::refreshDynamicText()
{
    for (int r = 0; r < maxRigs; ++r)
        loadButtons[r].setButtonText (processor.hasModelLoaded (r) ? "TROCAR CAPTURE NAM"
                                                                   : "CARREGAR CAPTURE NAM");
    ecoChip.setEnabled (processor.hasEcoVariant());
    refreshTypeButtons();

    // botões do looper acompanham o estado
    {
        using LS = GuitarRigNAMProcessor::LooperState;
        const auto st = processor.getLooperState();
        const auto rec = st == LS::empty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f REC"))
                         : st == LS::recording ? juce::String ("FECHAR")
                         : st == LS::overdub ? juce::String ("FIM DUB")
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
        const auto loadText = hasExt ? juce::String ("TROCAR VST3")
                                     : juce::String ("CARREGAR VST3");
        if (extLoadButton[s].getButtonText() != loadText)
            extLoadButton[s].setButtonText (loadText);
        extUiButton[s].setEnabled (hasExt);
        extRemoveButton[s].setEnabled (hasExt);
    }

    // cards desligados: esmaece knobs/botões (o LED fica aceso p/ religar)
    {
        static const char* dimIds[] = { "gate", "comp", "od", "preeq", "eq", "mod", "delay",
                                        "reverb", "pitch", "looper", "limiter", "ext", "wah",
                                        "harm", "octaver", "ringmod", "bitcrush", "slowgear",
                                        "exciter", "deesser", "tape", "console" };
        const auto chain = processor.getChainOrder();
        for (auto* id : dimIds)
        {
            if (! chain.contains (id))
                continue;
            auto* p = processor.apvts.getRawParameterValue (onParamIdForFx (id));
            const float alpha = p != nullptr && p->load() > 0.5f ? 1.0f : 0.4f;
            auto comps = componentsForFx (id);
            for (int i = 1; i < comps.size(); ++i) // 0 = LED, fica sempre visível
                comps[i]->setAlpha (alpha);
        }
    }

    // número de rigs ou ordem da cadeia mudou -> relayout. Com o botão do
    // mouse pressionado, ADIA: reflow sob um arrasto em andamento faz o
    // slider "pular" (a posição relativa muda sem o mouse se mover) e troca
    // o alvo sob o cursor. O timer re-tenta a cada tick até soltar.
    const auto orderNow = processor.getChainOrder().joinIntoString (",");
    if ((processor.getRigCount() != lastRigCount || orderNow != lastOrderSeen)
        && ! juce::Component::isMouseButtonDownAnywhere())
    {
        applyChainRelayout();
    }
    repaint();
}

//==============================================================================
// Gaveta de efeitos: mapeamentos por id (componentes, param On, nome)

juce::Array<juce::Component*> ChainView::componentsForFx (const juce::String& id)
{
    // convenção: o LED é sempre o primeiro (fica fora do esmaecimento)
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
    if (id == "ext")    return { &extLed[0], extMixKnob[0].get(), &extLoadButton[0], &extUiButton[0], &extRemoveButton[0] };
    if (id == "ext2")   return { &extLed[1], extMixKnob[1].get(), &extLoadButton[1], &extUiButton[1], &extRemoveButton[1] };
    if (id == "ext3")   return { &extLed[2], extMixKnob[2].get(), &extLoadButton[2], &extUiButton[2], &extRemoveButton[2] };
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

const char* ChainView::onParamIdForFx (const juce::String& id) const
{
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
    if (id == "ext") return "extOn";
    if (id == "ext2") return "ext2On";
    if (id == "ext3") return "ext3On";
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
    return nullptr;
}

juce::String ChainView::fxDisplayName (const juce::String& id)
{
    if (id == "gate") return "Noise Gate";
    if (id == "comp") return "Compressor";
    if (id == "od") return "Drive";
    if (id == "preeq") return juce::String (juce::CharPointer_UTF8 ("Pr\xc3\xa9-EQ"));
    if (id == "eq") return "EQ";
    if (id == "mod") return juce::String (juce::CharPointer_UTF8 ("Modula\xc3\xa7\xc3\xa3o"));
    if (id == "delay") return "Delay";
    if (id == "reverb") return "Reverb";
    if (id == "pitch") return "Pitch";
    if (id == "looper") return "Looper";
    if (id == "limiter") return "Limiter";
    if (id == "ext") return "Plugin VST3 1";
    if (id == "ext2") return "Plugin VST3 2";
    if (id == "ext3") return "Plugin VST3 3";
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
    if (id == "analyzer") return "Analisador";
    return id;
}

std::vector<std::pair<juce::Rectangle<int>, int>> ChainView::insertSpots() const
{
    // um "+" no meio de cada conector: inserir ANTES do card i = índice i
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
        { "Din\xc3\xa2mica",       { "gate", "comp", "slowgear", "limiter" } },
        { "Drive & Filtro",        { "wah", "od", "octaver", "ringmod", "bitcrush", "preeq" } },
        { "Pitch",                 { "pitch", "harm" } },
        { "Modula\xc3\xa7\xc3\xa3o & Cor", { "mod", "exciter", "deesser", "tape", "console" } },
        { "Amb\xc3\xaancia",       { "delay", "reverb" } },
        { "Extras",                { "ext", "ext2", "ext3", "looper", "analyzer" } },
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
                          "Todos os efeitos j\xc3\xa1 est\xc3\xa3o na cadeia")), false);

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
                // "+" do conector: entra exatamente onde foi clicado
                pos = juce::jlimit (0, order.size(), insertIndex);
            }
            else
            {
                // botão do fim: posição canônica (dá para arrastar depois)
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
    applyChainRelayout(); // layout atualiza na hora, não no próximo tick
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
// Drag-and-drop de reordenação

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    draggingId.clear();
    panning = false;

    // botão "+ EFEITO" (fim da cadeia)
    if (addFxB.contains (e.getPosition()))
    {
        showAddFxMenu (-1, addFxB);
        return;
    }

    // "+" dos conectores: adiciona efeito NAQUELA posição
    for (const auto& [rect, idx] : insertSpots())
        if (rect.contains (e.getPosition()))
        {
            showAddFxMenu (idx, rect);
            return;
        }

    // "✕" remove o efeito da cadeia (volta pra gaveta, ajustes preservados).
    // Síncrono + relayout imediato: cliques rápidos em sequência nunca caem
    // num layout defasado (knob/✕ errado deslizando sob o mouse).
    for (const auto& entry : orderedEntries())
        if (entry.id != "amp" && removeHotspot (entry.box).contains (e.getPosition()))
        {
            removeFxFromChain (entry.id);
            return;
        }

    // Cliques em knobs/botões vão para os filhos; aqui só chega o fundo dos
    // cartões. Amp+cabs são âncora e não podem ser arrastados.
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

    // fundo vazio (ou bloco do amp): arrastar faz pan da cadeia
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

    // índice de inserção: antes da primeira entrada cujo centro está à
    // direita do mouse
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
    // microinteração: realça o "+"/"✕" sob o mouse
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
// Drag-and-drop de arquivos: .nam -> amp, IR -> cab, .vst3 -> slot externo

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
        // fora de um amp: primeira lane livre (ou a 1ª)
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
        // slot sob o cursor; senão o primeiro slot vazio visível; senão o 1º
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
    // roda do mouse rola a cadeia (não há scroll vertical aqui)
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
        || id == "ext" || id == "ext2" || id == "ext3")
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
    if (id == "ext") return extB[0];
    if (id == "ext2") return extB[1];
    if (id == "ext3") return extB[2];
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

    // gaveta: zera as caixas e esconde os componentes de efeitos fora da
    // cadeia; os presentes reaparecem ao serem posicionados abaixo
    static const char* allFxIds[] = { "gate", "comp", "od", "preeq", "eq", "mod", "delay",
                                      "reverb", "pitch", "looper", "limiter", "ext", "ext2",
                                      "ext3", "wah", "harm", "octaver", "ringmod", "bitcrush",
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
    extB[0] = extB[1] = extB[2] = {};
    wahB = harmB = octB = rmB = bcB = sgB = excB = dsB = tapeB = cnsB = anB = {};

    // posiciona os cartões seguindo a ordem dinâmica da cadeia
    int x = 26;
    ioInB = { x, cardY (330), 90, 330 };
    x += 90 + 30;

    for (const auto& id : processor.getChainOrder())
    {
        if (id == "amp")
        {
            // lanes AMP+CAB EMPILHADAS (paralelo de verdade): uma linha por
            // rig, bus de divisão à esquerda e bus de soma entrando no Mixer
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
            else if (id == "ext") extB[0] = box;
            else if (id == "ext2") extB[1] = box;
            else if (id == "ext3") extB[2] = box;
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

    // ---- pedal genérico: knobs em wrap de 2 colunas (46 px)
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

    // harmonizer: 3 seletores (TOM/ESCALA/INTERVALO) + MIX/LEVEL
    {
        harmLed.setBounds (harmB.getRight() - 12 - 18, harmB.getY() + 10, 18, 18);
        const int bx = harmB.getX() + 12, bw = harmB.getWidth() - 24;
        harmKeyButton.setBounds (bx, harmB.getY() + 36, bw / 2 - 3, 24);
        harmScaleButton.setBounds (bx + bw / 2 + 3, harmB.getY() + 36, bw / 2 - 3, 24);
        harmIntervalButton.setBounds (bx, harmB.getY() + 66, bw, 24);
        harmMixKnob->setBounds (harmB.getCentreX() - 52, harmB.getY() + 130, 46, 46 + 26);
        harmLevelKnob->setBounds (harmB.getCentreX() + 6, harmB.getY() + 130, 46, 46 + 26);
    }

    // slots VST3: MIX + botões CARREGAR/PAINEL/REMOVER empilhados
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

    // looper: LOOP (nível) + grade de botões 2x2
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

    // TAP + subdivisão no cartão do delay (linha acima do seletor de modelo)
    tapButton.setBounds (delayB.getX() + 12, delayB.getBottom() - 96, 50, 24);
    delayDivButton.setBounds (delayB.getX() + 12 + 54, delayB.getBottom() - 96,
                              delayB.getWidth() - 24 - 54, 24);

    // chips de preset do compressor (linha sob o título)
    {
        const int cw = 36;
        int px = compB.getX() + (compB.getWidth() - (3 * cw + 2 * 4)) / 2;
        for (auto& chip : compPresetChips)
        {
            chip.setBounds (px, compB.getY() + 32, cw, 18);
            px += cw + 4;
        }
    }

    // seletores de variação (rodapé dos cartões, no lugar do texto)
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

    // pré-EQ: mesmos moldes do EQ
    {
        preEqLed.setBounds (preEqB.getRight() - 13 - 18, preEqB.getY() + 10, 18, 18);
        const int kw = 44, kh = kw + 26, gap = 13;
        const int gx = preEqB.getCentreX() - (3 * kw + 2 * gap) / 2;
        const int ky = preEqB.getY() + 130;
        preEqLowKnob->setBounds (gx, ky, kw, kh);
        preEqMidKnob->setBounds (gx + kw + gap, ky, kw, kh);
        preEqHighKnob->setBounds (gx + 2 * (kw + gap), ky, kw, kh);
    }

    // ---- rigs paralelos: par AMP+CAB por lane + Mixer
    {
        const int count = processor.getRigCount();

        for (int r = 0; r < GuitarRigNAMProcessor::maxRigs; ++r)
        {
            const bool active = r < count;
            const auto ampB = ampLaneB[r];
            const auto cabB = cabLaneB[r];
            const bool compact = ampB.getHeight() < 300; // 2-3 rigs empilhados

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

            // amp da lane
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
                // grade 3x2 clássica (foto opcional entre cabeçalho e knobs)
                const bool photo = ampImages[r].isValid();
                const int kw = 42, kh = kw + 26, gapX = 26, gapY = 4;
                const int gx = ampB.getX() + (266 - (3 * kw + 2 * gapX)) / 2;
                const int gy = ampB.getY() + (photo ? 152 : 118);
                for (int i = 0; i < 6; ++i)
                    grid[i]->setBounds (gx + (i % 3) * (kw + gapX), gy + (i / 3) * (kh + gapY), kw, kh);

                loadButtons[r].setBounds (ampB.getX() + 18, ampB.getBottom() - 15 - 32, 266 - 36, 32);
            }
            else
            {
                // linha única de 6 knobs menores
                const int kw = 32, kh = kw + 26, gapX = 6;
                const int gx = ampB.getX() + (266 - (6 * kw + 5 * gapX)) / 2;
                const int gy = ampB.getY() + 46 + (ampB.getHeight() - 46 - 32 - kh) / 2;
                for (int i = 0; i < 6; ++i)
                    grid[i]->setBounds (gx + i * (kw + gapX), gy, kw, kh);

                loadButtons[r].setBounds (ampB.getX() + 14, ampB.getBottom() - 28, 266 - 28, 22);
            }

            // cab da lane
            if (r == 0)
                cabLed.setBounds (cabB.getRight() - 10 - 18, cabB.getY() + 10, 18, 18);
            if (! compact)
            {
                cabPhaseChips[r].setBounds (cabB.getRight() - 12 - 26, cabB.getY() + 36, 26, 20);
                cabLcKnob[r]->setBounds (cabB.getX() + 18, cabB.getY() + 176, 40, 40 + 26);
                cabHcKnob[r]->setBounds (cabB.getX() + 78, cabB.getY() + 176, 40, 40 + 26);
                cabIrButtons[r].setBounds (cabB.getX() + 10, cabB.getBottom() - 12 - 24,
                                           cabB.getWidth() - 20, 24);
            }
            else
            {
                cabPhaseChips[r].setBounds (cabB.getRight() - 10 - 26, cabB.getY() + 32, 26, 20);
                const int kh2 = 36 + 26;
                const int ky = cabB.getY() + 34 + (cabB.getHeight() - 34 - 30 - kh2) / 2;
                cabLcKnob[r]->setBounds (cabB.getX() + 26, ky, 36, kh2);
                cabHcKnob[r]->setBounds (cabB.getX() + 82, ky, 36, kh2);
                cabIrButtons[r].setBounds (cabB.getX() + 10, cabB.getBottom() - 28,
                                           cabB.getWidth() - 20, 22);
            }
        }

        // Mixer: +/- de rigs, blend por lane e AIR global
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
    if (b.isEmpty()) // efeito na gaveta (fora da cadeia atual)
        return;

    auto bf = b.toFloat();
    g.setGradientFill ({ ui::cardTop, 0.0f, bf.getY(), ui::cardBottom, 0.0f, bf.getBottom(), false });
    g.fillRoundedRectangle (bf, 16.0f);
    g.setColour (ui::border());
    g.drawRoundedRectangle (bf.reduced (0.5f), 16.0f, 1.0f);

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
    clip.addRoundedRectangle (spot.toFloat(), 8.0f);
    g.reduceClipRegion (clip);
    const float scale = juce::jmax ((float) spot.getWidth() / img.getWidth(),
                                    (float) spot.getHeight() / img.getHeight());
    const float dw = img.getWidth() * scale, dh = img.getHeight() * scale;
    g.drawImage (img, juce::Rectangle<float> (spot.getX() + (spot.getWidth() - dw) / 2.0f,
                                              spot.getY() + (spot.getHeight() - dh) / 2.0f, dw, dh),
                 juce::RectanglePlacement::stretchToFit);
    g.restoreState();
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (spot.toFloat(), 8.0f, 1.0f);
}

void ChainView::paint (juce::Graphics& g)
{
    // fundo listrado sutil
    g.setColour (juce::Colours::white.withAlpha (0.018f));
    for (int gx = 0; gx < getWidth(); gx += 44)
        g.fillRect (gx, 0, 1, getHeight());

    g.setFont (ui::monoFont (9.0f));
    g.setColour (juce::Colour (0xff525b66));
    g.drawText ("SIGNAL FLOW", 24, 14, 200, 12, juce::Justification::centredLeft);

    // ---- conectores direcionais
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
    // conectores seguem a ordem dinâmica (amp -> cabs é interno ao bloco)
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
                    // topologia PARALELA: nó de divisão -> um ramo por lane
                    // (amp -> cab) -> bus de soma que entra no Mixer
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

                    // divisão do sinal seco
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

                        // saída da lane -> bus de soma
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
        g.fillRoundedRectangle (bf, 14.0f);
        g.setColour (ui::border());
        g.drawRoundedRectangle (bf.reduced (0.5f), 14.0f, 1.0f);

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

    // ---- pedais (os com variação têm seletor no rodapé em vez de texto)
    drawPedalFrame (g, gateB, "Noise Gate", juce::String (juce::CharPointer_UTF8 ("Histerese 6 dB \xc2\xb7 hold")));
    drawPedalFrame (g, odB, "Drive", " ");
    drawPedalFrame (g, delayB, "Delay", " ");
    drawPedalFrame (g, revB, "Reverb", " ");
    drawPedalFrame (g, compB, "Compressor", " ");
    drawPedalFrame (g, modB, juce::String (juce::CharPointer_UTF8 ("Modula\xc3\xa7\xc3\xa3o")), " ");
    drawPedalFrame (g, pitchB, "Pitch", " ");
    drawPedalFrame (g, wahB, "Wah", " ");
    drawPedalFrame (g, sgB, "Slow Gear",
                    juce::String (juce::CharPointer_UTF8 ("swell autom\xc3\xa1tico")));
    drawPedalFrame (g, octB, "Octaver",
                    juce::String (juce::CharPointer_UTF8 ("sub-oitava anal\xc3\xb3gica")));
    drawPedalFrame (g, rmB, "Ring Mod",
                    juce::String (juce::CharPointer_UTF8 ("portadora senoidal")));
    drawPedalFrame (g, bcB, "Bitcrusher",
                    juce::String (juce::CharPointer_UTF8 ("lo-fi \xc2\xb7 bits + rate")));
    drawPedalFrame (g, harmB, "Harmonizer", " ");
    drawPedalFrame (g, excB, "Exciter",
                    juce::String (juce::CharPointer_UTF8 ("brilho harm\xc3\xb4nico")));
    drawPedalFrame (g, dsB, "De-esser",
                    juce::String (juce::CharPointer_UTF8 ("doma a banda \xc3\xa1spera")));
    drawPedalFrame (g, tapeB, "Tape",
                    juce::String (juce::CharPointer_UTF8 ("satura\xc3\xa7\xc3\xa3o \xc2\xb7 bump \xc2\xb7 rolloff")));
    drawPedalFrame (g, cnsB, "Console",
                    juce::String (juce::CharPointer_UTF8 ("cola de buss anal\xc3\xb3gico")));

    // ---- looper (estado + tempo desenhados ao vivo)
    if (! looperB.isEmpty())
    {
        drawPedalFrame (g, looperB, "Looper", {});

        const auto st = processor.getLooperState();
        juce::String status;
        juce::Colour c = ui::textFaint;
        switch (st)
        {
            case GuitarRigNAMProcessor::LooperState::empty:
                status = juce::String (juce::CharPointer_UTF8 ("vazio \xc2\xb7 REC para gravar"));
                break;
            case GuitarRigNAMProcessor::LooperState::recording:
                status = "gravando " + juce::String (processor.getLooperPosSeconds(), 1) + " s";
                c = ui::red;
                break;
            case GuitarRigNAMProcessor::LooperState::playing:
                status = "tocando " + juce::String (processor.getLooperPosSeconds(), 1) + " / "
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                c = ui::accent;
                break;
            case GuitarRigNAMProcessor::LooperState::overdub:
                status = "overdub " + juce::String (processor.getLooperPosSeconds(), 1) + " / "
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                c = ui::glowOrange;
                break;
            case GuitarRigNAMProcessor::LooperState::stopped:
                status = juce::String (juce::CharPointer_UTF8 ("parado \xc2\xb7 "))
                         + juce::String (processor.getLooperSeconds(), 1) + " s";
                break;
        }
        g.setFont (ui::monoFont (9.0f));
        g.setColour (c);
        g.drawText (status, looperB.getX() + 12, looperB.getY() + 34, looperB.getWidth() - 24, 12,
                    juce::Justification::centredLeft);

        // barra de progresso do loop
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

    // ---- slots de plugin VST3 externo
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
                              : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 slot vazio \xe2\x80\x94")),
                          b.getX() + 12, b.getY() + 48, b.getWidth() - 24, 34,
                          juce::Justification::topLeft, 2);
    }

    // ---- analisador de espectro (FFT 2048 ao vivo)
    if (! anB.isEmpty())
    {
        drawPedalFrame (g, anB, "Analisador",
                        juce::String (juce::CharPointer_UTF8 ("espectro \xc2\xb7 40 Hz\xe2\x80\x93""16 kHz")));

        auto viz = juce::Rectangle<float> ((float) anB.getX() + 13.0f, (float) anB.getY() + 40.0f,
                                           (float) anB.getWidth() - 26.0f,
                                           (float) anB.getHeight() - 40.0f - 84.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 9.0f);

        if (processor.apvts.getRawParameterValue ("anOn")->load() > 0.5f)
        {
            // FFT do trecho mais recente + bandas log com decaimento suave
            constexpr int fftSize = 2048;
            static float sample[fftSize];
            processor.readAnalyzerBlock (sample, fftSize);
            for (int i = 0; i < fftSize; ++i)
                anFftBuf[(size_t) i] = sample[i]
                    * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1)));
            std::fill (anFftBuf.begin() + fftSize, anFftBuf.end(), 0.0f);
            anFft.performFrequencyOnlyForwardTransform (anFftBuf.data());

            const double sr = 48000.0; // exibição; a razão log é o que importa
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
                anBands[b] = norm > anBands[b] ? norm : anBands[b] * 0.85f; // decai suave
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

    // ---- limiter (com barrinha de gain reduction)
    if (! limB.isEmpty())
    {
        drawPedalFrame (g, limB, "Limiter", juce::String (juce::CharPointer_UTF8 ("brickwall \xc2\xb7 fim da cadeia")));

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

    // ---- pré-EQ (com barras vivas, como o EQ pós)
    if (! preEqB.isEmpty())
    {
        drawPedalFrame (g, preEqB, juce::String (juce::CharPointer_UTF8 ("Pr\xc3\xa9-EQ")),
                        juce::String (juce::CharPointer_UTF8 ("molda a satura\xc3\xa7\xc3\xa3o \xc2\xb7 pr\xc3\xa9-amp")));

        auto viz = juce::Rectangle<float> ((float) preEqB.getX() + 13.0f, (float) preEqB.getY() + 38.0f,
                                           (float) preEqB.getWidth() - 26.0f, 62.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (viz, 9.0f, 1.0f);

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

    // ---- cabs (um card por lane de rig)
    {
        const int count = processor.getRigCount();
        for (int s = 0; s < count; ++s)
        {
            const auto cabB = cabLaneB[s];
            const bool compact = cabB.getHeight() < 300;
            drawPedalFrame (g, cabB, count > 1 ? "Cab " + juce::String (s + 1)
                                               : juce::String ("Cab IR"), {});

            // badge V1/V2 do IR (quando o TONE3000 informa via .meta)
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

            // foto (só no card grande) ou nome do IR
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
                                      : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem IR \xe2\x80\x94")),
                                  cabB.getX() + (compact ? 46 : 12), cabB.getY() + (compact ? 32 : 60),
                                  cabB.getWidth() - (compact ? 84 : 24), compact ? 22 : 34,
                                  juce::Justification::topLeft, compact ? 2 : 3);
            }
        }
    }

    // ---- Mixer (soma dos rigs; +/- controla os pares AMP+CAB)
    {
        const int count = processor.getRigCount();
        drawPedalFrame (g, mixerB, "Mixer", {});

        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::accent);
        g.drawText (juce::CharPointer_UTF8 ("SOMA \xce\xa3 \xc2\xb7 " ),
                    mixerB.getX() + 12, mixerB.getY() + 30, 60, 11,
                    juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.drawText (juce::String (count) + (count > 1 ? " rigs" : " rig"),
                    mixerB.getX() + 52, mixerB.getY() + 30, 60, 11,
                    juce::Justification::centredLeft);

        // rótulo da coluna AIR
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("GLOBAL", mixerB.getRight() - 18 - 44 - 6, mixerB.getCentreY() - 34,
                    56, 12, juce::Justification::centred);
    }

    // ---- EQ (com barras vivas refletindo LOW/MID/HIGH)
    if (! eqB.isEmpty())
    {
        drawPedalFrame (g, eqB, "EQ", juce::String (juce::CharPointer_UTF8 ("3 bandas \xc2\xb7 p\xc3\xb3s-cab")));

        auto viz = juce::Rectangle<float> ((float) eqB.getX() + 13.0f, (float) eqB.getY() + 38.0f,
                                           (float) eqB.getWidth() - 26.0f, 62.0f);
        g.setColour (ui::meterBg);
        g.fillRoundedRectangle (viz, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (viz, 9.0f, 1.0f);

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

    // ---- amps (um head por lane de rig)
    for (int lane = 0; lane < processor.getRigCount(); ++lane)
    {
        const auto ampB = ampLaneB[lane];
        const bool compact = ampB.getHeight() < 300;
        auto bf = ampB.toFloat();
        g.setGradientFill ({ ui::ampTop, 0.0f, bf.getY(), ui::ampBottom, 0.0f, bf.getBottom(), false });
        g.fillRoundedRectangle (bf, 18.0f);
        g.setColour (ui::accent.withAlpha (0.28f));
        g.drawRoundedRectangle (bf.reduced (0.5f), 18.0f, 1.0f);

        // faixa accent no topo
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle (bf, 18.0f);
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
            // nome do capture na linha abaixo do título (sem subtítulo/info)
            g.setFont (ui::uiFont (13.0f, true));
            g.setColour (modelName.isNotEmpty() ? ui::textBright : ui::textMuted);
            g.drawText (modelName.isNotEmpty()
                            ? modelName
                            : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem capture \xe2\x80\x94")),
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
            // -40 reserva o canto direito para o badge V1/V2
            g.drawText (modelName.isNotEmpty() ? modelName
                                               : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem capture \xe2\x80\x94")),
                        ampB.getX() + 18, ampB.getY() + 54, ampB.getWidth() - 36 - 40, 22,
                        juce::Justification::centredLeft);
        }

        // badge V1/V2 da arquitetura do capture
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
                info = "carregue um capture da Tone Store";
            }
            g.setFont (ui::monoFont (9.0f));
            g.setColour (juce::Colour (0xff8a929c));
            g.drawText (info, ampB.getX() + 18, ampB.getY() + 80, ampB.getWidth() - 36, 12,
                        juce::Justification::centredLeft);

            if (ampImages[lane].isValid())
                drawPhoto (g, ampImages[lane],
                           { ampB.getX() + 18, ampB.getY() + 98, ampB.getWidth() - 36, 48 });
        }

        // barra de brilho (valvulado — laranja, como no design)
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

    // ---- cards desligados esmaecidos (os knobs recebem setAlpha à parte)
    for (const auto& entry : orderedEntries())
    {
        if (entry.id == "amp" || entry.box.isEmpty())
            continue;
        if (auto* p = processor.apvts.getRawParameterValue (onParamIdForFx (entry.id));
            p != nullptr && p->load() <= 0.5f)
        {
            g.setColour (ui::bg.withAlpha (0.55f));
            g.fillRoundedRectangle (entry.box.toFloat(), 16.0f);
        }
    }

    // ---- "✕" de remover (volta o efeito pra gaveta; realça no hover)
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

    // ---- "+" nos conectores (inserir efeito naquela posição; realça no hover)
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

    // alvo do drop de arquivo (capture/IR/vst3 arrastado do Explorer)
    if (! dropHighlight.isEmpty())
    {
        g.setColour (ui::accent.withAlpha (0.9f));
        g.drawRoundedRectangle (dropHighlight.toFloat().reduced (1.5f), 16.0f, 2.5f);
        g.setColour (ui::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (dropHighlight.toFloat(), 16.0f);
    }

    // ---- botão "+ EFEITO" (gaveta)
    {
        auto bf = addFxB.toFloat();
        g.setColour (ui::accent.withAlpha (addFxB == hoverHotspot ? 0.8f : 0.35f));
        const float dash[] = { 5.0f, 4.0f };
        juce::Path outline;
        outline.addRoundedRectangle (bf.reduced (1.0f), 14.0f);
        juce::PathStrokeType stroke (1.4f);
        juce::Path dashed;
        stroke.createDashedStroke (dashed, outline, dash, 2);
        g.fillPath (dashed);

        g.setColour (ui::accent.withAlpha (0.9f));
        g.setFont (ui::uiFont (26.0f, true));
        g.drawText ("+", addFxB.withHeight (40).withY (addFxB.getCentreY() - 34),
                    juce::Justification::centred);
        g.setFont (ui::monoFont (9.0f, true));
        g.drawText ("EFEITO", addFxB.withHeight (14).withY (addFxB.getCentreY() + 8),
                    juce::Justification::centred);
    }

    // ---- feedback do drag-and-drop (fantasma + indicador de inserção)
    if (draggingId.isNotEmpty())
    {
        const auto source = boxForFx (draggingId);

        // origem esmaecida
        g.setColour (ui::bg.withAlpha (0.55f));
        g.fillRoundedRectangle (source.toFloat(), 16.0f);

        // linha de inserção
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

        // fantasma do cartão seguindo o mouse
        auto ghost = source.toFloat().withX (dragMouseX - (float) dragGrabDx);
        g.setColour (ui::cardTop.withAlpha (0.85f));
        g.fillRoundedRectangle (ghost, 16.0f);
        g.setColour (ui::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (ghost, 16.0f, 1.5f);
        g.setFont (ui::uiFont (13.0f, true));
        g.setColour (ui::textBright);
        juce::String title = draggingId == "gate" ? juce::String ("Noise Gate")
                             : draggingId == "od" ? juce::String ("Overdrive")
                             : draggingId == "eq" ? juce::String ("EQ")
                             : draggingId == "delay" ? juce::String ("Delay")
                             : draggingId == "comp" ? juce::String ("Compressor")
                             : draggingId == "pitch" ? juce::String ("Pitch")
                             : draggingId == "looper" ? juce::String ("Looper")
                             : draggingId == "limiter" ? juce::String ("Limiter")
                             : draggingId == "ext" ? juce::String ("Plugin VST3 1")
                             : draggingId == "ext2" ? juce::String ("Plugin VST3 2")
                             : draggingId == "ext3" ? juce::String ("Plugin VST3 3")
                             : draggingId == "wah" ? juce::String ("Wah")
                             : draggingId == "harm" ? juce::String ("Harmonizer")
                             : draggingId == "octaver" ? juce::String ("Octaver")
                             : draggingId == "ringmod" ? juce::String ("Ring Mod")
                             : draggingId == "bitcrush" ? juce::String ("Bitcrusher")
                             : draggingId == "slowgear" ? juce::String ("Slow Gear")
                             : draggingId == "exciter" ? juce::String ("Exciter")
                             : draggingId == "deesser" ? juce::String ("De-esser")
                             : draggingId == "tape" ? juce::String ("Tape")
                             : draggingId == "console" ? juce::String ("Console")
                             : draggingId == "analyzer" ? juce::String ("Analisador")
                             : draggingId == "mod"
                                   ? juce::String (juce::CharPointer_UTF8 ("Modula\xc3\xa7\xc3\xa3o"))
                             : draggingId == "preeq"
                                   ? juce::String (juce::CharPointer_UTF8 ("Pr\xc3\xa9-EQ"))
                                   : juce::String ("Reverb");
        g.drawText (title, ghost.reduced (12.0f).removeFromTop (30.0f),
                    juce::Justification::centredLeft);
    }
}

//==============================================================================
RigContent::RigContent (GuitarRigNAMProcessor& p)
    : processor (p)
{
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

    // editor inline do nome do preset (aparece sobre o pill)
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

    // tooltips + atalhos
    setWantsKeyboardFocus (true);
    for (auto* b : std::initializer_list<juce::Button*> { &prevButton, &nextButton, &saveButton,
                                                          &presetPill, &audioButton, &storeButton,
                                                          &tunerToggle })
        b->setMouseClickGrabsKeyboardFocus (false);

    prevButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("Preset anterior (\xe2\x86\x90)")));
    nextButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("Pr\xc3\xb3ximo preset (\xe2\x86\x92)")));
    saveButton.setTooltip ("Salva o preset atual (sem nome: pede um)");
    presetPill.setTooltip ("Escolher preset / Salvar como novo");
    storeButton.setTooltip ("Buscar e baixar tones do TONE3000");
    audioButton.setTooltip ("Driver, dispositivo, sample rate e buffer (ASIO/WASAPI)");
    tunerToggle.setTooltip ("Liga/desliga o afinador (T)");

    // dev: GUITARRIG_TUNER=off inicia com o afinador desligado (teste de UI)
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

    // MUTE do afinador: silencia a saída enquanto o afinador estiver ligado
    muteChip.getProperties().set ("chip", true);
    muteChip.getProperties().set ("chipActive", false);
    muteChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Silencia a sa\xc3\xad""da enquanto o afinador est\xc3\xa1 ligado (afinar em sil\xc3\xaancio)")));
    muteChip.setMouseClickGrabsKeyboardFocus (false);
    muteChip.onClick = [this]
    {
        tunerMuteWanted = ! tunerMuteWanted;
        muteChip.getProperties().set ("chipActive", tunerMuteWanted);
        muteChip.repaint();
    };
    addAndMakeVisible (muteChip);

    // GRAVADOR rápido: WAV da saída em Documentos\GuitarRig NAM\Gravações
    recChip.getProperties().set ("chip", true);
    recChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Grava a sa\xc3\xad""da em WAV (Documentos\\GuitarRig NAM\\Grava\xc3\xa7\xc3\xb5""es)")));
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

    // A/B: compara dois ajustes completos
    abButton.getProperties().set ("chip", true);
    abButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "A/B: alterna entre dois ajustes completos do rig (o atual \xc3\xa9 salvo no slot ativo)")));
    abButton.setMouseClickGrabsKeyboardFocus (false);
    abButton.onClick = [this]
    {
        processor.toggleAB();
        abButton.setButtonText (processor.getABIndex() == 0 ? "A" : "B");
    };
    addAndMakeVisible (abButton);

    // PALCO: modo performance — só o essencial, gigante (tecla F)
    perfChip.getProperties().set ("chip", true);
    perfChip.getProperties().set ("chipActive", false);
    perfChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Modo palco (F): esconde a cadeia e mostra preset, afinador e "
        "medidores em tamanho grande. Esc volta.")));
    perfChip.setMouseClickGrabsKeyboardFocus (false);
    perfChip.onClick = [this] { setPerfMode (! perfMode); };
    addAndMakeVisible (perfChip);

    // AUTO-ECO: troca para o capture leve sozinho quando a CPU passa de 90%
    autoEcoChip.getProperties().set ("chip", true);
    autoEcoChip.setClickingTogglesState (true);
    autoEcoChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Com CPU acima de 90%%, troca automaticamente para a vers\xc3\xa3o leve "
        "do capture (quando dispon\xc3\xadvel)")));
    autoEcoChip.setMouseClickGrabsKeyboardFocus (false);
    autoEcoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "autoEco", autoEcoChip);
    addAndMakeVisible (autoEcoChip);

    chainView = std::make_unique<ChainView> (processor,
                                             [this] (int lane) { chooseModelFile (lane); },
                                             [this] (int slot) { chooseIrFile (slot); },
                                             [this] (int slot) { chooseExtPluginFile (slot); },
                                             [this] (int slot) { openExtPluginWindow (slot); });

    // fecha o painel do plugin hospedado antes de qualquer troca/descarte
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

    // Dev: GUITARRIG_EXT_PLUGIN=<caminho .vst3> carrega no slot ao iniciar.
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

    // Flag de dev: GUITARRIG_OPEN_STORE=explore|library abre o store ao iniciar.
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
    closeAllExtPluginWindows();
    setLookAndFeel (nullptr);
}

void RigContent::resized()
{
    const int W = getWidth();

    storeOverlay->setBounds (getLocalBounds());

    // ---- top bar (60 px)
    storeButton.setBounds (W - 18 - 108, 13, 108, 34);
    audioButton.setBounds (storeButton.getX() - 8 - 82, 13, 82, 34);
    const int metersRight = audioButton.getX() - 15 - 1 - 15;
    cpuMeter.setBounds (metersRight - 60, 34, 60, 7);
    inMeter.setBounds (metersRight - 60 - 14 - 78, 17, 78, 7);
    outMeter.setBounds (metersRight - 60 - 14 - 78, 32, 78, 7);

    {
        const int pillW = 190, navW = 32, saveW = 64, gap = 8;
        const int groupW = navW + gap + pillW + gap + navW + gap + saveW
                           + gap + 40 + 6 + 66; // + A/B + REC
        // deslocado para a esquerda para não colidir com os medidores
        int x = juce::jmin ((W - groupW) / 2, inMeter.getX() - 24 - groupW);
        x = juce::jmax (x, 200);
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

    // ---- cadeia (rolável) e barra inferior (chips)
    chainViewport.setBounds (0, 60, W, getHeight() - 60 - 60);
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

    // fundo geral (radial no topo)
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

        // logo com glow
        auto logo = juce::Rectangle<float> (18.0f, 15.0f, 30.0f, 30.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (logo.expanded (3.0f), 12.0f);
        g.setGradientFill ({ ui::accent, logo.getX(), logo.getY(),
                             ui::accentDark, logo.getRight(), logo.getBottom(), false });
        g.fillRoundedRectangle (logo, 9.0f);
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
        g.drawText ("GuitarRig", 56, 17, 90, 26, juce::Justification::centredLeft);

        auto badge = juce::Rectangle<float> (146.0f, 22.0f, 40.0f, 17.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        g.drawRoundedRectangle (badge, 5.0f, 1.0f);
        g.setFont (ui::monoFont (9.0f, true));
        g.setColour (ui::accent);
        g.drawText ("NAM", badge, juce::Justification::centred);

        // labels dos medidores
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
            g.beginTransparencyLayer (0.3f); // afinador desligado: tudo esmaecido

        // cordas
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

        // nota + cents
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

        // régua de cents
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

        // status compacto à direita
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
            if (pluginInstallMsg.isNotEmpty())
            {
                status = pluginInstallMsg; // download/instalação de plugin
                c = pluginInstallMsg.startsWith ("Erro") ? ui::red : ui::accent;
            }
            else if (err.isNotEmpty())
            {
                status = "Erro: " + err;
                c = ui::red;
            }
            else if (ecoNoticeTicks > 0)
            {
                status = juce::String (juce::CharPointer_UTF8 (
                    "ECO autom\xc3\xa1tico ativado (CPU alta)"));
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

    // ---- preset gigante (clique: esquerda = anterior, direita = próximo,
    //      centro = menu)
    const auto presetName = processor.getCurrentPresetName();
    const bool dirty = presetDirtyCached;
    g.setFont (ui::uiFont (48.0f, true));
    g.setColour (ui::textBright);
    g.drawFittedText ((dirty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) : juce::String())
                          + (presetName.isNotEmpty() ? presetName : juce::String ("(sem preset)")),
                      area.getX() + 120, area.getY() + 40, area.getWidth() - 240, 60,
                      juce::Justification::centred, 1);

    // setas de navegação nas laterais
    g.setFont (ui::uiFont (40.0f, true));
    g.setColour (ui::textFaint);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x97\x82"), area.getX() + 30, area.getY() + 40, 60, 60,
                juce::Justification::centred);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x96\xb8"), area.getRight() - 90, area.getY() + 40, 60, 60,
                juce::Justification::centred);

    // capture carregado + rigs
    {
        const auto model = processor.getModelName (0);
        juce::String info = model.isNotEmpty()
                                ? model
                                : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem capture \xe2\x80\x94"));
        if (processor.getRigCount() > 1)
            info += juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                    + juce::String (processor.getRigCount()) + " rigs";
        g.setFont (ui::monoFont (14.0f));
        g.setColour (ui::accent);
        g.drawText (info, area.getX(), area.getY() + 108, area.getWidth(), 20,
                    juce::Justification::centred);
    }

    // ---- afinador grande
    {
        const int cy = area.getCentreY() + 60;
        const bool hasNote = tunerNote.isNotEmpty();
        const bool inTune = hasNote && std::abs (tunerCents) <= 5.0;

        g.setFont (ui::monoFont (84.0f, true));
        g.setColour (! hasNote ? ui::textMuted : inTune ? ui::green : ui::textBright);
        g.drawText (hasNote ? tunerNote : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                    area.getX(), cy - 110, area.getWidth(), 100, juce::Justification::centred);

        // régua de cents: -50 .. +50, agulha na posição
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
            g.drawText ("toque uma corda para afinar", area.getX(), (int) bar.getBottom() + 14,
                        area.getWidth(), 18, juce::Justification::centred);
        }
    }

    // ---- dicas
    g.setFont (ui::monoFont (10.0f));
    g.setColour (ui::textFaint);
    g.drawText (juce::CharPointer_UTF8 ("\xe2\x86\x90/\xe2\x86\x92 presets \xc2\xb7 "
                                        "espa\xc3\xa7o liga/desliga o amp \xc2\xb7 "
                                        "T afinador \xc2\xb7 F/Esc volta a editar"),
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
        clipTicks = 60; // ~2 s de aviso
    else if (clipTicks > 0)
        --clipTicks;

    const float cpu = processor.cpuLoad.load();
    cpuMeter.setFraction (cpu, cpu > 0.8f ? ui::red : cpu > 0.5f ? ui::yellow : ui::accent);

    // fingerprint do preset é serialização de XML — checa a 2 Hz, não a 30 Hz
    if (tunerTick % 15 == 0)
    {
        processor.settlePresetBaseline();
        presetDirtyCached = processor.isPresetDirty();
    }

    const auto presetName = processor.getCurrentPresetName();
    const bool dirty = presetDirtyCached;
    presetPill.setButtonText (processor.isLoadingModel()
                                  ? juce::String (juce::CharPointer_UTF8 ("Carregando\xe2\x80\xa6"))
                                  : (presetName.isNotEmpty()
                                         ? (dirty ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) + presetName
                                                  : presetName)
                                         : juce::String ("(sem preset)")));
    presetPill.dotLit = processor.anyModelLoaded();

    if (saveFlashTicks > 0)
    {
        --saveFlashTicks;
        saveButton.setButtonText (saveFlashTicks > 0 ? "Salvo" : "SALVAR");
    }

    if (! focusGrabbed && isShowing())
    {
        focusGrabbed = true;
        grabKeyboardFocus();
    }

    chainView->refreshDynamicText();
    refreshSidecarImages();

    // instância VST3 aposentada é deletada aqui (message thread, fora do áudio)
    processor.collectExternalRetired();

    applyEcoSwitchIfNeeded();
    if (ecoNoticeTicks > 0)
        --ecoNoticeTicks;

    // mensagem de instalação de plugin: transiente só depois de concluída
    if (pluginMsgTicks > 0 && --pluginMsgTicks == 0)
        pluginInstallMsg.clear();

    if (++tunerTick % 3 == 0 && (isTunerOn() || perfMode))
        analyseTuner();

    // o chip pode ter mudado por load de estado/preset
    if ((bool) tunerToggle.getProperties()["chipActive"] != isTunerOn())
    {
        tunerToggle.getProperties().set ("chipActive", isTunerOn());
        tunerToggle.repaint();
    }

    // mute do afinador só vale com o afinador ativo (ou no palco)
    processor.setTunerMuted (tunerMuteWanted && (isTunerOn() || perfMode));

    // gravador: mostra o tempo decorrido no chip
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
            recText = "SALVO";
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
        repaint(); // afinador/medidores grandes ao vivo
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

    // auto-ECO: CPU acima de 90% por ~2 s liga o modo leve (nunca desliga
    // sozinho, para não ficar alternando o timbre)
    if (autoEco && ! eco && processor.hasEcoVariant()
        && processor.cpuLoad.load() >= 0.9f)
    {
        if (++cpuHighTicks >= 60)
        {
            cpuHighTicks = 0;
            if (auto* p = apvts.getParameter ("ampEco"))
                p->setValueNotifyingHost (1.0f);
            ecoNoticeTicks = 120; // aviso por ~4 s na barra inferior
        }
    }
    else
    {
        cpuHighTicks = 0;
    }

    // mantém o arquivo carregado coerente com o modo (chip, preset ou auto),
    // lane por lane
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
        if (bestDiff > 0.12) // > ~1.4 semitons de qualquer corda
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
    // Recarrega quando o caminho muda OU quando o sidecar aparece depois
    // (a gravação da foto é assíncrona ao download).
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

void RigContent::chooseModelFile (int lane)
{
    auto initialDir = juce::File (processor.getModelPath (lane)).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = Tone3000Client::capturesDir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Escolher capture NAM (.nam) para o AMP " + juce::String (lane + 1),
        initialDir, "*.nam");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, lane] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.setModelPair (lane, file, {}); // local: sem par eco
                              });
}

void RigContent::chooseExtPluginFile (int slot)
{
    // Menu por CATEGORIA: lista os .vst3 instalados (pasta do sistema + do
    // usuário — o instalador embutido usa a do usuário, sem admin), agrupados
    // pelo catálogo embutido (PluginCatalog) + instalar o que falta + disco.
    juce::Array<juce::File> found;
    for (const auto& dir : { plugcat::systemVst3Dir(), plugcat::userVst3Dir() })
        if (dir.isDirectory())
            for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories,
                                                     false, "*.vst3"))
                found.add (f);

    struct Category { const char* title; std::initializer_list<const char*> keys; };
    static const Category categories[] = {
        { "Reverb & Amb\xc3\xaancia",  { "dragonfly", "valhalla", "supermassive", "reverb" } },
        { "Cole\xc3\xa7\xc3\xa3o Airwindows", { "airwindows", "airwin" } },
        { "Est\xc3\xba""dio (LSP)",     { "lsp" } },
        { "Pedais & Din\xc3\xa2mica (Zam)", { "zam", "zamaudio" } },
        { "Amp sims",                   { "bias", "amplitube", "guitar rig", "th-u",
                                          "stormblade", "neural" } },
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
        menu.addSectionHeader ("Outros");
        for (int i = 0; i < found.size(); ++i)
            if (! used[i])
                menu.addItem (i + 1, found[i].getFileNameWithoutExtension(), true,
                              found[i].getFullPathName() == current);
    }

    // catálogo embutido: instalar direto pelo app o que ainda falta
    // (baixa do release oficial e instala no VST3 do usuário, sem admin)
    {
        bool header = false;
        const auto& cat = plugcat::entries();
        for (int i = 0; i < (int) cat.size(); ++i)
        {
            if (plugcat::isInstalled (cat[(size_t) i]))
                continue;
            if (! header)
            {
                menu.addSectionHeader (juce::String (juce::CharPointer_UTF8 (
                    "Instalar recomendados")));
                header = true;
            }
            const auto& e = cat[(size_t) i];
            if (juce::String (e.url).isNotEmpty())
                menu.addItem (9100 + i,
                              juce::String (juce::CharPointer_UTF8 ("\xe2\xac\x87 "))
                                  + e.name + " " + e.version + " (" + juce::String (e.sizeMB)
                                  + " MB, " + e.license + ")");
            else
                menu.addItem (9100 + i,
                              juce::String (e.name)
                                  + juce::String (juce::CharPointer_UTF8 (
                                      " \xe2\x80\x94 baixar no site\xe2\x80\xa6")));
        }
    }

    menu.addSeparator();
    menu.addItem (9000, juce::String (juce::CharPointer_UTF8 ("Procurar arquivo\xe2\x80\xa6")));

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

            // instalar do catálogo embutido
            if (result >= 9100 && result < 9100 + (int) plugcat::entries().size())
            {
                const auto& e = plugcat::entries()[(size_t) (result - 9100)];
                if (juce::String (e.url).isEmpty())
                {
                    juce::URL (e.homepage).launchInDefaultBrowser();
                    return;
                }
                self->pluginInstallMsg = juce::String (juce::CharPointer_UTF8 ("Baixando "))
                                         + e.name + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
                self->pluginMsgTicks = 0; // fica até terminar
                plugcat::installAsync (
                    e,
                    [safe, name = juce::String (e.name)] (int pct)
                    {
                        if (safe != nullptr)
                            safe->pluginInstallMsg =
                                juce::String (juce::CharPointer_UTF8 ("Baixando "))
                                + name + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6 "))
                                + juce::String (pct) + "%";
                    },
                    [safe] (bool ok, juce::String msg)
                    {
                        if (safe == nullptr)
                            return;
                        safe->pluginInstallMsg = (ok ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 "))
                                                     : juce::String ("Erro: "))
                                                 + msg;
                        safe->pluginMsgTicks = 150; // ~5 s e some
                    });
                return;
            }

            if (result != 9000)
                return;

            // procurar no disco
            auto initialDir = juce::File (self->processor.getExternalPluginPath (slot))
                                  .getParentDirectory();
            if (! initialDir.isDirectory())
                initialDir = juce::File ("C:\\Program Files\\Common Files\\VST3");
            if (! initialDir.isDirectory())
                initialDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

            self->fileChooser = std::make_unique<juce::FileChooser> (
                "Escolher plugin VST3 (.vst3)", initialDir, "*.vst3");
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

// Janela flutuante com o painel do plugin hospedado; fecha sozinha antes de
// qualquer troca de instância (onExternalPluginWillChange).
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

void RigContent::chooseIrFile (int slot)
{
    auto initialDir = juce::File (processor.getIrPath (slot)).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = Tone3000Client::irsDir();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Escolher impulse response para o CAB " + juce::String (slot + 1),
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
    saveFlashTicks = 27; // ~0.9 s de "Salvo"
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

    menu.addItem (1000, juce::String (juce::CharPointer_UTF8 ("Salvar como novo\xe2\x80\xa6")));
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
        return false; // o overlay tem seus próprios atalhos

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
    // modo palco: laterais navegam presets, centro abre o menu
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

    grabKeyboardFocus(); // clique em área vazia devolve o foco aos atalhos
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
