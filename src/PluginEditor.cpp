#include "PluginEditor.h"

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
    if (std::abs (f - fraction) > 0.004f)
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
ChainView::ChainView (GuitarRigNAMProcessor& p, std::function<void()> onLoadModel,
                      std::function<void (int)> onLoadIr)
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
    makeKnob (gateReleaseKnob, "gateRelease", "RELEASE", formatMs);
    makeKnob (odDriveKnob, "odDrive", "DRIVE", formatTen);
    makeKnob (odToneKnob, "odTone", "TONE", formatTen);
    makeKnob (odLevelKnob, "odLevel", "LEVEL", formatTen);
    makeKnob (ampGainKnob, "ampGain", "GAIN", formatDb);
    makeKnob (ampBassKnob, "ampBass", "BASS", formatTen);
    makeKnob (ampMidKnob, "ampMid", "MID", formatTen);
    makeKnob (ampTrebleKnob, "ampTreble", "TREBLE", formatTen);
    makeKnob (ampPresKnob, "ampPresence", "PRES", formatTen);
    makeKnob (ampMasterKnob, "ampMaster", "MASTER", formatDb);
    makeKnob (cabAirKnob, "cabAir", "AIR", formatTen);

    auto formatHz = [] (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + "k" : juce::String ((int) v);
    };
    for (int s = 0; s < GuitarRigNAMProcessor::maxCabSlots; ++s)
    {
        const auto n = juce::String (s + 1);
        makeKnob (cabBlendKnob[s], ("cab" + n + "Blend").toRawUTF8(), "BLEND", formatPct);
        makeKnob (cabLcKnob[s], ("cab" + n + "LowCut").toRawUTF8(), "LO CUT", formatHz);
        makeKnob (cabHcKnob[s], ("cab" + n + "HighCut").toRawUTF8(), "HI CUT", formatHz);

        cabPhaseChips[s].setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        cabPhaseChips[s].getProperties().set ("chip", true);
        cabPhaseChips[s].setClickingTogglesState (true);
        cabPhaseChips[s].setTooltip (juce::String (juce::CharPointer_UTF8 (
            "Inverte a fase deste cab (evita cancelamento em paralelo)")));
        cabPhaseChips[s].setMouseClickGrabsKeyboardFocus (false);
        cabPhaseAtt[s] = std::make_unique<Attachment> (apvts, "cab" + n + "Phase",
                                                       cabPhaseChips[s]);
        addChildComponent (cabPhaseChips[s]);

        cabIrButtons[s].setButtonText ("TROCAR");
        cabIrButtons[s].setTooltip ("Escolher o IR deste cab");
        cabIrButtons[s].setMouseClickGrabsKeyboardFocus (false);
        cabIrButtons[s].onClick = [onLoadIr, s] { onLoadIr (s); };
        addChildComponent (cabIrButtons[s]);
    }

    cabAddButton.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Adicionar um cab em paralelo (at\xc3\xa9 3)")));
    cabRemoveButton.setTooltip (juce::String (juce::CharPointer_UTF8 ("Remover o \xc3\xbaltimo cab")));
    for (auto* b : { &cabAddButton, &cabRemoveButton })
        b->setMouseClickGrabsKeyboardFocus (false);
    auto changeCount = [this] (int delta)
    {
        if (auto* param = processor.apvts.getParameter ("cabCount"))
        {
            const int c = juce::jlimit (1, (int) GuitarRigNAMProcessor::maxCabSlots,
                                        processor.getCabCount() + delta);
            param->setValueNotifyingHost (param->getNormalisableRange()
                                              .convertTo0to1 ((float) c));
        }
    };
    cabAddButton.onClick = [changeCount] { changeCount (1); };
    cabRemoveButton.onClick = [changeCount] { changeCount (-1); };
    addAndMakeVisible (cabAddButton);
    addAndMakeVisible (cabRemoveButton);
    makeKnob (eqLowKnob, "eqLow", "LOW", formatDbInt);
    makeKnob (eqMidKnob, "eqMid", "MID", formatDbInt);
    makeKnob (eqHighKnob, "eqHigh", "HIGH", formatDbInt);
    makeKnob (delayTimeKnob, "delayTime", "TIME", formatMs);
    makeKnob (delayFbKnob, "delayFb", "FB", formatPct);
    makeKnob (delayMixKnob, "delayMix", "MIX", formatPct);
    makeKnob (revDecayKnob, "revDecay", "DECAY", formatTen);
    makeKnob (revMixKnob, "revMix", "MIX", formatPct);
    makeKnob (revPreKnob, "revPre", "PRE", formatMs);

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

    loadButton.onClick = std::move (onLoadModel);
    loadButton.setTooltip ("Escolher um arquivo .nam do disco");
    loadButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (loadButton);

    // tooltips dos knobs
    auto tip = [] (std::unique_ptr<KnobComponent>& k, const char* utf8)
    { k->setKnobTooltip (juce::String (juce::CharPointer_UTF8 (utf8))); };
    tip (inputKnob, "Ganho de entrada (antes de tudo)");
    tip (outputKnob, "Volume final de sa\xc3\xad""da");
    tip (gateThreshKnob, "Abaixo deste n\xc3\xadvel o gate fecha");
    tip (gateReleaseKnob, "Tempo para o gate fechar");
    tip (odDriveKnob, "Quantidade de satura\xc3\xa7\xc3\xa3o do pedal");
    tip (odToneKnob, "Brilho do overdrive");
    tip (odLevelKnob, "Volume do overdrive");
    tip (ampGainKnob, "Empurra o sinal para dentro do capture \xe2\x80\x94 age como o gain do amp real");
    tip (ampBassKnob, "Graves (150 Hz)");
    tip (ampMidKnob, "M\xc3\xa9""dios (500 Hz)");
    tip (ampTrebleKnob, "Agudos (1.8 kHz)");
    tip (ampPresKnob, "Presen\xc3\xa7""a (4.5 kHz)");
    tip (ampMasterKnob, "Volume da se\xc3\xa7\xc3\xa3o do amp");
    tip (cabAirKnob, "Ar/brilho p\xc3\xb3s-mix dos cabs (shelf 8 kHz)");
    for (int s = 0; s < GuitarRigNAMProcessor::maxCabSlots; ++s)
    {
        tip (cabBlendKnob[s], "Quanto deste cab entra na mistura");
        tip (cabLcKnob[s], "Corta graves deste cab (20 Hz = desligado)");
        tip (cabHcKnob[s], "Corta agudos deste cab (20 kHz = desligado)");
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

    updateLayout();
}

// largura do cartão de cabs: coluna do AIR (44) + 124 por slot ativo
int ChainView::cabCardWidth() const
{
    return 12 + 44 + processor.getCabCount() * 124 + 8;
}

void ChainView::updateLayout()
{
    // métricas do design + conectores de 30 px; o cartão de cabs é dinâmico
    const int total = 26 + 90 + 30 + 132 + 30 + 132 + 30 + 266 + 30 + cabCardWidth() + 30
                      + 176 + 30 + 132 + 30 + 132 + 30 + 90 + 26;
    setSize (total, chainHeight);
}

void ChainView::setAmpImage (juce::Image img)
{
    ampImage = std::move (img);
    resized();
    repaint();
}

void ChainView::setCabImage (juce::Image img)
{
    cabImage = std::move (img);
    resized();
    repaint();
}

void ChainView::refreshDynamicText()
{
    loadButton.setButtonText (processor.hasModelLoaded() ? "TROCAR CAPTURE NAM"
                                                         : "CARREGAR CAPTURE NAM");

    // número de cabs mudou -> relayout da cadeia inteira
    if (processor.getCabCount() != lastCabCount)
    {
        lastCabCount = processor.getCabCount();
        updateLayout();
    }
    repaint();
}

void ChainView::resized()
{
    const int H = chainHeight;
    auto cardY = [H] (int cardH) { return (H - cardH) / 2; };

    int x = 26;
    ioInB = { x, cardY (330), 90, 330 };
    x += 90 + 30;
    gateB = { x, cardY (330), 132, 330 };
    x += 132 + 30;
    odB = { x, cardY (330), 132, 330 };
    x += 132 + 30;
    ampB = { x, cardY (360), 266, 360 };
    x += 266 + 30;
    const int cabW = cabCardWidth();
    cabB = { x, cardY (330), cabW, 330 };
    x += cabW + 30;
    eqB = { x, cardY (330), 176, 330 };
    x += 176 + 30;
    delayB = { x, cardY (330), 132, 330 };
    x += 132 + 30;
    revB = { x, cardY (330), 132, 330 };
    x += 132 + 30;
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

    layoutPedal (gateB, gateLed, { gateThreshKnob.get(), gateReleaseKnob.get() });
    layoutPedal (odB, odLed, { odDriveKnob.get(), odToneKnob.get(), odLevelKnob.get() });
    layoutPedal (delayB, delayLed, { delayTimeKnob.get(), delayFbKnob.get(), delayMixKnob.get() });
    layoutPedal (revB, revLed, { revDecayKnob.get(), revMixKnob.get(), revPreKnob.get() });

    // ---- amp (foto opcional entre o cabeçalho e os knobs)
    {
        ampLed.setBounds (ampB.getRight() - 18 - 18, ampB.getY() + 19, 18, 18);
        const bool photo = ampImage.isValid();
        const int kw = 42, kh = kw + 26, gapX = 26, gapY = 4;
        const int gx = ampB.getX() + (266 - (3 * kw + 2 * gapX)) / 2;
        const int gy = ampB.getY() + (photo ? 152 : 118);
        KnobComponent* grid[6] = { ampGainKnob.get(), ampBassKnob.get(), ampMidKnob.get(),
                                   ampTrebleKnob.get(), ampPresKnob.get(), ampMasterKnob.get() };
        for (int i = 0; i < 6; ++i)
            grid[i]->setBounds (gx + (i % 3) * (kw + gapX), gy + (i / 3) * (kh + gapY), kw, kh);

        loadButton.setBounds (ampB.getX() + 18, ampB.getBottom() - 15 - 32, 266 - 36, 32);
    }

    // ---- cabs paralelos: coluna do AIR + uma faixa por slot ativo
    {
        const int count = processor.getCabCount();
        cabLed.setBounds (cabB.getRight() - 10 - 18, cabB.getY() + 10, 18, 18);
        cabRemoveButton.setBounds (cabB.getRight() - 10 - 18 - 6 - 22, cabB.getY() + 8, 22, 22);
        cabAddButton.setBounds (cabRemoveButton.getX() - 4 - 22, cabB.getY() + 8, 22, 22);
        cabAddButton.setEnabled (count < GuitarRigNAMProcessor::maxCabSlots);
        cabRemoveButton.setEnabled (count > 1);

        // coluna esquerda: AIR global
        cabAirKnob->setBounds (cabB.getX() + 10, cabB.getCentreY() - 20, 40, 40 + 26);

        for (int s = 0; s < GuitarRigNAMProcessor::maxCabSlots; ++s)
        {
            const bool active = s < count;
            const int sx = cabB.getX() + 12 + 44 + s * 124;

            cabBlendKnob[s]->setVisible (active);
            cabLcKnob[s]->setVisible (active);
            cabHcKnob[s]->setVisible (active);
            cabPhaseChips[s].setVisible (active);
            cabIrButtons[s].setVisible (active);
            if (! active)
                continue;

            cabPhaseChips[s].setBounds (sx + 124 - 34, cabB.getY() + 36, 26, 20);
            cabBlendKnob[s]->setBounds (sx + (124 - 44) / 2, cabB.getY() + 96, 44, 44 + 26);
            cabLcKnob[s]->setBounds (sx + 14, cabB.getY() + 176, 40, 40 + 26);
            cabHcKnob[s]->setBounds (sx + 68, cabB.getY() + 176, 40, 40 + 26);
            cabIrButtons[s].setBounds (sx + 8, cabB.getBottom() - 12 - 24, 124 - 16, 24);
        }
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
    connector (ioInB, gateB);
    connector (gateB, odB);
    connector (odB, ampB);
    connector (ampB, cabB);
    connector (cabB, eqB);
    connector (eqB, delayB);
    connector (delayB, revB);
    connector (revB, ioOutB);

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

    // ---- pedais
    drawPedalFrame (g, gateB, "Noise Gate", "Downward expander 10:1");
    drawPedalFrame (g, odB, "Overdrive", juce::String (juce::CharPointer_UTF8 ("Soft-clip \xc2\xb7 HP 120 Hz")));
    drawPedalFrame (g, delayB, "Delay", juce::String (juce::CharPointer_UTF8 ("Digital \xc2\xb7 mono")));
    drawPedalFrame (g, revB, "Reverb", juce::String (juce::CharPointer_UTF8 ("Hall \xc2\xb7 predelay")));

    // ---- cabs paralelos
    {
        const int count = processor.getCabCount();
        drawPedalFrame (g, cabB, count > 1 ? "Cabs (paralelo)" : "Cab IR", {});

        // rótulo da coluna AIR
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("GLOBAL", cabB.getX() + 6, cabB.getCentreY() - 38, 52, 12,
                    juce::Justification::centred);

        for (int s = 0; s < count; ++s)
        {
            const int sx = cabB.getX() + 12 + 44 + s * 124;

            // separador entre faixas
            if (s > 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.fillRect (sx - 2, cabB.getY() + 34, 1, cabB.getHeight() - 48);
            }

            g.setFont (ui::monoFont (8.5f, true));
            g.setColour (ui::accent.withAlpha (0.85f));
            g.drawText ("CAB " + juce::String (s + 1), sx + 4, cabB.getY() + 38, 60, 12,
                        juce::Justification::centredLeft);

            // foto (só com 1 cab, para não apertar) ou nome do IR
            const auto irName = processor.getIrName (s);
            if (count == 1 && cabImage.isValid())
            {
                drawPhoto (g, cabImage, { sx + 4, cabB.getY() + 54, 116, 40 });
            }
            else
            {
                g.setFont (ui::monoFont (8.5f));
                g.setColour (juce::Colour (0xffb4bbc4));
                g.drawFittedText (irName.isNotEmpty()
                                      ? irName
                                      : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem IR \xe2\x80\x94")),
                                  sx + 4, cabB.getY() + 54, 116, 34,
                                  juce::Justification::topLeft, 3);
            }
        }
    }

    // ---- EQ (com barras vivas refletindo LOW/MID/HIGH)
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

    // ---- amp
    {
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

        g.setFont (ui::uiFont (12.0f, true));
        g.setColour (ui::textBright);
        g.drawText ("AMP HEAD", ampB.getX() + 18, ampB.getY() + 19, 170, 14,
                    juce::Justification::centredLeft);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::accent);
        g.drawText (juce::CharPointer_UTF8 ("AMPLIFIER \xc2\xb7 NAM CAPTURE"),
                    ampB.getX() + 18, ampB.getY() + 35, 180, 11, juce::Justification::centredLeft);

        const auto modelName = processor.getModelName();
        g.setFont (ui::uiFont (18.0f, true));
        g.setColour (modelName.isNotEmpty() ? ui::textBright : ui::textMuted);
        g.drawText (modelName.isNotEmpty() ? modelName
                                           : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem capture \xe2\x80\x94")),
                    ampB.getX() + 18, ampB.getY() + 54, ampB.getWidth() - 36, 22,
                    juce::Justification::centredLeft);

        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        juce::String info;
        const double modelSr = processor.getModelExpectedSampleRate();
        if (modelName.isNotEmpty())
        {
            info = (modelSr > 0 ? juce::String (modelSr / 1000.0, 1) + " kHz" + dot : juce::String())
                   + "mono" + dot + "NAM";
            if (processor.isResampling())
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

        if (ampImage.isValid())
            drawPhoto (g, ampImage, { ampB.getX() + 18, ampB.getY() + 98, ampB.getWidth() - 36, 48 });

        // barra de brilho (valvulado — laranja, como no design)
        {
            auto glow = juce::Rectangle<float> (bf.getX() + 22.0f, bf.getBottom() - 66.0f,
                                                bf.getWidth() - 44.0f, 7.0f);
            const float alpha = processor.hasModelLoaded()
                                    && processor.apvts.getRawParameterValue ("ampOn")->load() > 0.5f
                                ? 0.85f : 0.15f;
            juce::ColourGradient grad (ui::glowOrange.withAlpha (0.0f), glow.getX(), 0.0f,
                                       ui::glowOrange.withAlpha (0.0f), glow.getRight(), 0.0f, false);
            grad.addColour (0.5, ui::glowOrange.withAlpha (alpha));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (glow, 5.0f);
        }
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

    chainView = std::make_unique<ChainView> (processor,
                                             [this] { chooseModelFile(); },
                                             [this] (int slot) { chooseIrFile (slot); });
    chainViewport.setViewedComponent (chainView.get(), false);
    chainViewport.setScrollBarsShown (false, true);
    chainViewport.setScrollBarThickness (9);
    addAndMakeVisible (chainViewport);

    storeOverlay = std::make_unique<StoreOverlay> (processor);
    addChildComponent (*storeOverlay);

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
        const int pillW = 200, navW = 32, saveW = 68, gap = 8;
        const int groupW = navW + gap + pillW + gap + navW + gap + saveW;
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
    }

    // ---- cadeia (rolável) e afinador
    chainViewport.setBounds (0, 60, W, getHeight() - 60 - 60);
    tunerToggle.setBounds (22, getHeight() - 60 + 16, 92, 28);
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
        g.drawText ("OUT", outMeter.getX() - 28, outMeter.getY() - 4, 24, 12, juce::Justification::centredRight);
        g.drawText ("CPU " + juce::String ((int) (processor.cpuLoad.load() * 100.0f)) + "%",
                    cpuMeter.getX(), cpuMeter.getY() - 14, 60, 12, juce::Justification::centredLeft);

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
            if (err.isNotEmpty())
            {
                status = "Erro: " + err;
                c = ui::red;
            }
            g.setFont (ui::monoFont (9.5f));
            g.setColour (c);
            g.drawText (status, W - 22 - 360, barY, 360, 60, juce::Justification::centredRight);
        }
    }
}

void RigContent::timerCallback()
{
    auto toDb = [] (float linear) { return juce::Decibels::gainToDecibels (linear, -80.0f); };

    inMeterDb = juce::jmax (toDb (processor.inputPeak.load()), inMeterDb - 2.2f);
    outMeterDb = juce::jmax (toDb (processor.outputPeak.load()), outMeterDb - 2.2f);
    inMeter.setLevel (inMeterDb);
    outMeter.setLevel (outMeterDb);

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
    presetPill.dotLit = processor.hasModelLoaded();

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

    if (++tunerTick % 3 == 0 && isTunerOn())
        analyseTuner();

    // o chip pode ter mudado por load de estado/preset
    if ((bool) tunerToggle.getProperties()["chipActive"] != isTunerOn())
    {
        tunerToggle.getProperties().set ("chipActive", isTunerOn());
        tunerToggle.repaint();
    }

    repaint (0, 0, getWidth(), 60);
    repaint (0, getHeight() - 60, getWidth(), 60);
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

    refresh (processor.getModelPath(), loadedModelPath, ampImageLoaded,
             [this] (juce::Image img) { chainView->setAmpImage (std::move (img)); });
    refresh (processor.getIrPath (0), loadedIrPath, cabImageLoaded,
             [this] (juce::Image img) { chainView->setCabImage (std::move (img)); });
}

void RigContent::chooseModelFile()
{
    auto initialDir = juce::File (processor.getModelPath()).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = Tone3000Client::capturesDir();

    fileChooser = std::make_unique<juce::FileChooser> ("Escolher capture NAM (.nam)",
                                                       initialDir, "*.nam");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.loadModelAsync (file);
                              });
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

void RigContent::mouseDown (const juce::MouseEvent&)
{
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
