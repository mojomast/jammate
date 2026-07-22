#include "PluginEditor.h"

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

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
    addAndMakeVisible (slider);

    nameLabel.setText (labelText, juce::dontSendNotification);
    nameLabel.setFont (ui::monoFont (9.0f));
    nameLabel.setColour (juce::Label::textColourId, ui::textFaint);
    nameLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (nameLabel);

    valueLabel.setFont (ui::monoFont (11.0f, true));
    valueLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd3d5d8));
    valueLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (valueLabel);

    updateValueText();
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
        g.setColour (juce::Colour (0xff3a3d43));
        g.fillEllipse (c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.5f);
    }
}

//==============================================================================
void LevelMeter::setLevel (float newLevelDb)
{
    if (std::abs (newLevelDb - levelDb) > 0.1f)
    {
        levelDb = newLevelDb;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::meterBg);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (ui::bgBorder);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    const float frac = juce::jlimit (0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
    if (frac <= 0.001f)
        return;

    auto inner = b.reduced (1.0f);
    juce::ColourGradient grad (ui::green, inner.getX(), 0.0f, ui::red, inner.getRight(), 0.0f, false);
    grad.addColour (0.62, ui::green);
    grad.addColour (0.84, ui::yellow);

    g.saveState();
    g.reduceClipRegion (inner.withWidth (inner.getWidth() * frac).toNearestInt());
    g.setGradientFill (grad);
    g.fillRoundedRectangle (inner, 2.0f);
    g.restoreState();
}

//==============================================================================
void PillButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (ui::panel);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (isHighlighted ? juce::Colour (0xff4a4d54) : ui::panelBorder);
    g.drawRoundedRectangle (b, 8.0f, 1.0f);

    g.setColour (dotLit ? ui::accent : ui::textMuted);
    g.fillEllipse (b.getX() + 12.0f, b.getCentreY() - 3.0f, 6.0f, 6.0f);

    g.setFont (ui::uiFont (13.0f, true));
    g.setColour (ui::text);
    g.drawText (getButtonText(), getLocalBounds().reduced (26, 0), juce::Justification::centred);

    g.setFont (ui::uiFont (10.0f));
    g.setColour (ui::textMuted);
    g.drawText ("v", getLocalBounds().removeFromRight (20), juce::Justification::centredLeft);
}

//==============================================================================
RigContent::RigContent (GuitarRigNAMProcessor& p)
    : processor (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

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

    storeOverlay = std::make_unique<StoreOverlay> (processor);
    addChildComponent (*storeOverlay);

    // Flag de dev: GUITARRIG_OPEN_STORE=explore|library abre o store ao iniciar
    // (útil para testes automatizados de UI; sem efeito em uso normal).
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

    // presets
    prevButton.onClick = [this] { processor.loadAdjacentPreset (-1); };
    nextButton.onClick = [this] { processor.loadAdjacentPreset (1); };
    saveButton.onClick = [this] { savePresetDialog(); };
    presetPill.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (presetPill);

    // amp
    ampLedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "ampOn", ampLed);
    addAndMakeVisible (ampLed);
    loadButton.onClick = [this] { chooseModelFile(); };
    addAndMakeVisible (loadButton);

    // gate
    gateLedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "gateOn", gateLed);
    addAndMakeVisible (gateLed);

    // cab
    cabLedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "cabOn", cabLed);
    addAndMakeVisible (cabLed);
    irButton.onClick = [this] { chooseIrFile(); };
    addAndMakeVisible (irButton);

    // knobs
    auto formatDb = [] (float v) { return juce::String (v, 1) + " dB"; };
    auto formatDbInt = [] (float v) { return juce::String ((int) v) + " dB"; };
    auto formatMs = [] (float v) { return juce::String ((int) v) + " ms"; };

    inputKnob = std::make_unique<KnobComponent> (processor.apvts, "inputGain", "GAIN", formatDb);
    outputKnob = std::make_unique<KnobComponent> (processor.apvts, "outputGain", "LEVEL", formatDb);
    gateThreshKnob = std::make_unique<KnobComponent> (processor.apvts, "gateThresh", "THRESH", formatDbInt);
    gateReleaseKnob = std::make_unique<KnobComponent> (processor.apvts, "gateRelease", "RELEASE", formatMs);
    cabLevelKnob = std::make_unique<KnobComponent> (processor.apvts, "cabLevel", "LEVEL", formatDb);

    for (auto* k : { inputKnob.get(), outputKnob.get(), gateThreshKnob.get(),
                     gateReleaseKnob.get(), cabLevelKnob.get() })
        addAndMakeVisible (*k);

    setSize (designWidth, designHeight);
    startTimerHz (30);
}

RigContent::~RigContent()
{
    setLookAndFeel (nullptr);
}

void RigContent::resized()
{
    const auto full = getLocalBounds();
    const int W = full.getWidth();

    storeOverlay->setBounds (full);

    // ---- top bar (58 px)
    audioButton.setBounds (W - 18 - 110 - 8 - 76, 13, 76, 32);
    storeButton.setBounds (W - 18 - 110, 13, 110, 32);
    const int metersX = audioButton.getX() - 14 - 1 - 14 - 104;
    inMeter.setBounds (metersX + 30, 17, 74, 6);
    outMeter.setBounds (metersX + 30, 33, 74, 6);

    // grupo de preset centrado: ◂ pill ▸ SALVAR
    {
        const int pillW = 230, navW = 30, saveW = 70, gap = 6;
        const int groupW = navW + gap + pillW + gap + navW + 8 + saveW;
        int x = (W - groupW) / 2;
        prevButton.setBounds (x, 13, navW, 32);
        x += navW + gap;
        presetPill.setBounds (x, 13, pillW, 32);
        x += pillW + gap;
        nextButton.setBounds (x, 13, navW, 32);
        x += navW + 8;
        saveButton.setBounds (x, 13, saveW, 32);
    }

    // ---- cadeia de sinal
    const int ioW = 84, ioH = 326, slotW = 150, slotH = 326, ampW = 258, ampH = 356, connW = 34;
    const int rowW = ioW * 2 + slotW * 2 + ampW + connW * 4;
    const int x0 = (W - rowW) / 2;
    const int chainCentreY = 58 + (full.getHeight() - 58 - 40) / 2;

    int x = x0;
    inputCardBounds = { x, chainCentreY - ioH / 2, ioW, ioH };
    x += ioW + connW;
    gateCardBounds = { x, chainCentreY - slotH / 2, slotW, slotH };
    x += slotW + connW;
    ampCardBounds = { x, chainCentreY - ampH / 2, ampW, ampH };
    x += ampW + connW;
    cabCardBounds = { x, chainCentreY - slotH / 2, slotW, slotH };
    x += slotW + connW;
    outputCardBounds = { x, chainCentreY - ioH / 2, ioW, ioH };

    inputKnob->setBounds (inputCardBounds.getX() + (ioW - 48) / 2,
                          inputCardBounds.getCentreY() - 30, 48, 48 + 26);
    outputKnob->setBounds (outputCardBounds.getX() + (ioW - 48) / 2,
                           outputCardBounds.getCentreY() - 30, 48, 48 + 26);

    ampLed.setBounds (ampCardBounds.getRight() - 17 - 18, ampCardBounds.getY() + 15, 18, 18);
    loadButton.setBounds (ampCardBounds.getX() + 17, ampCardBounds.getBottom() - 15 - 30,
                          ampCardBounds.getWidth() - 34, 30);

    // gate: dois knobs lado a lado
    gateLed.setBounds (gateCardBounds.getRight() - 12 - 18, gateCardBounds.getY() + 10, 18, 18);
    {
        const int kw = 48, gap = 18;
        const int kx = gateCardBounds.getCentreX() - kw - gap / 2;
        const int ky = gateCardBounds.getCentreY() - 55;
        gateThreshKnob->setBounds (kx, ky, kw, kw + 26);
        gateReleaseKnob->setBounds (kx + kw + gap, ky, kw, kw + 26);
    }

    // cab: knob central + botão embaixo
    cabLed.setBounds (cabCardBounds.getRight() - 12 - 18, cabCardBounds.getY() + 10, 18, 18);
    cabLevelKnob->setBounds (cabCardBounds.getCentreX() - 24,
                             cabCardBounds.getCentreY() - 55, 48, 48 + 26);
    irButton.setBounds (cabCardBounds.getX() + 12, cabCardBounds.getBottom() - 12 - 28,
                        cabCardBounds.getWidth() - 24, 28);
}

void RigContent::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds();
    const int W = full.getWidth(), H = full.getHeight();

    g.fillAll (ui::bg);

    // ---- top bar
    {
        auto bar = juce::Rectangle<int> (0, 0, W, 58).toFloat();
        g.setGradientFill ({ ui::topBarTop, 0.0f, 0.0f, ui::topBarBottom, 0.0f, 58.0f, false });
        g.fillRect (bar);
        g.setColour (juce::Colour (0xff101215));
        g.fillRect (0, 57, W, 1);

        // logo
        auto logo = juce::Rectangle<float> (18.0f, 16.0f, 26.0f, 26.0f);
        g.setGradientFill ({ ui::accent, logo.getX(), logo.getY(),
                             juce::Colour (0xffc96a12), logo.getRight(), logo.getBottom(), false });
        g.fillRoundedRectangle (logo, 7.0f);
        {
            juce::Path diamond;
            diamond.addRectangle (-4.5f, -4.5f, 9.0f, 9.0f);
            diamond.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f)
                                        .translated (logo.getCentre()));
            g.setColour (juce::Colour (0xff161719));
            g.fillPath (diamond);
        }

        g.setFont (ui::uiFont (16.0f, true));
        g.setColour (ui::textBright);
        g.drawText ("GUITARRIG", 52, 16, 110, 26, juce::Justification::centredLeft);

        auto badge = juce::Rectangle<float> (148.0f, 21.0f, 38.0f, 16.0f);
        g.setColour (ui::accent.withAlpha (0.4f));
        g.drawRoundedRectangle (badge, 4.0f, 1.0f);
        g.setFont (ui::monoFont (9.0f, true));
        g.setColour (ui::accent);
        g.drawText ("NAM", badge, juce::Justification::centred);

        // labels IN/OUT dos medidores
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textFaint);
        g.drawText ("IN", inMeter.getX() - 26, inMeter.getY() - 4, 22, 12, juce::Justification::centredRight);
        g.drawText ("OUT", outMeter.getX() - 26, outMeter.getY() - 4, 22, 12, juce::Justification::centredRight);

        g.setColour (ui::panelBorder);
        g.fillRect (audioButton.getX() - 14, 16, 1, 26);
    }

    // ---- fundo da cadeia
    {
        auto chain = juce::Rectangle<int> (0, 58, W, H - 58 - 40);
        juce::ColourGradient grad (ui::chainTop, W * 0.5f, 58.0f - chain.getHeight() * 0.1f,
                                   ui::chainBottom, W * 0.5f, (float) chain.getBottom(), true);
        g.setGradientFill (grad);
        g.fillRect (chain);

        g.setFont (ui::monoFont (9.0f));
        g.setColour (juce::Colour (0xff5a5d63));
        g.drawText ("SIGNAL CHAIN", 22, 58 + 12, 200, 12, juce::Justification::centredLeft);
    }

    // ---- conectores
    auto drawConnector = [&g] (juce::Rectangle<int> left, juce::Rectangle<int> right)
    {
        const float y = (float) left.getCentreY();
        const float xa = (float) left.getRight() + 2.0f;
        const float xb = (float) right.getX() - 2.0f;
        g.setColour (juce::Colour (0xff43464c));
        g.fillEllipse (xa, y - 3.5f, 7.0f, 7.0f);
        g.fillEllipse (xb - 7.0f, y - 3.5f, 7.0f, 7.0f);
        g.setGradientFill ({ juce::Colour (0xff2b2d31), xa, 0.0f,
                             juce::Colour (0xff43464c), (xa + xb) / 2.0f, 0.0f, false });
        g.fillRoundedRectangle (xa + 7.0f, y - 1.5f, xb - xa - 14.0f, 3.0f, 1.5f);
    };
    drawConnector (inputCardBounds, gateCardBounds);
    drawConnector (gateCardBounds, ampCardBounds);
    drawConnector (ampCardBounds, cabCardBounds);
    drawConnector (cabCardBounds, outputCardBounds);

    // ---- cartões IO (Input / Output)
    auto drawIoCard = [&] (juce::Rectangle<int> bounds, const juce::String& name,
                           const juce::String& ioLabel)
    {
        auto b = bounds.toFloat();
        g.setGradientFill ({ juce::Colour (0xff26282c), 0.0f, b.getY(),
                             ui::panel, 0.0f, b.getBottom(), false });
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (ui::panelBorder);
        g.drawRoundedRectangle (b, 12.0f, 1.0f);

        g.setFont (ui::monoFont (9.0f));
        g.setColour (juce::Colour (0xff8a8d93));
        g.drawText (name, bounds.withTrimmedTop (16).withHeight (12), juce::Justification::centred);

        const float jackY = b.getY() + 62.0f;
        g.setColour (juce::Colours::black);
        g.fillEllipse (b.getCentreX() - 18.0f, jackY, 36.0f, 36.0f);
        g.setColour (juce::Colour (0xff3a3d43));
        g.drawEllipse (b.getCentreX() - 18.0f, jackY, 36.0f, 36.0f, 3.0f);

        g.setColour (ui::green);
        g.fillEllipse (b.getCentreX() - 22.0f, b.getBottom() - 26.0f, 6.0f, 6.0f);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (ui::textMuted);
        g.drawText (ioLabel, bounds.withTrimmedLeft (bounds.getWidth() / 2 - 8)
                                 .withY (bounds.getBottom() - 30).withHeight (14),
                    juce::Justification::centredLeft);
    };
    drawIoCard (inputCardBounds, "INPUT", "IN");
    drawIoCard (outputCardBounds, "OUTPUT", "OUT");

    // ---- cartões de pedal (gate / cab)
    auto drawPedalCard = [&] (juce::Rectangle<int> bounds, const juce::String& name,
                              const juce::String& modelLine)
    {
        auto b = bounds.toFloat();
        g.setGradientFill ({ ui::cardTop, 0.0f, b.getY(), ui::cardBottom, 0.0f, b.getBottom(), false });
        g.fillRoundedRectangle (b, 14.0f);
        g.setColour (ui::cardBorder);
        g.drawRoundedRectangle (b, 14.0f, 1.0f);

        g.setFont (ui::uiFont (11.5f, true));
        g.setColour (ui::text);
        g.drawText (name, bounds.getX() + 12, bounds.getY() + 12, bounds.getWidth() - 46, 14,
                    juce::Justification::centredLeft);

        // linha de modelo acima da base
        g.setFont (ui::monoFont (9.5f));
        g.setColour (juce::Colour (0xffb6b9be));
        g.drawFittedText (modelLine, bounds.getX() + 12, bounds.getBottom() - 78,
                          bounds.getWidth() - 24, 26, juce::Justification::centredLeft, 2);

        g.setColour (juce::Colour (0xff303338));
        g.fillRect (bounds.getX() + 12, bounds.getBottom() - 86, bounds.getWidth() - 24, 1);
    };

    drawPedalCard (gateCardBounds, "Noise Gate", "Downward expander 10:1");

    const auto irName = processor.getIrName();
    drawPedalCard (cabCardBounds, "Cab IR",
                   irName.isNotEmpty() ? irName : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem IR \xe2\x80\x94")));

    // ---- cartão do amp
    {
        auto b = ampCardBounds.toFloat();
        juce::ColourGradient grad (ui::ampTop, 0.0f, b.getY(), ui::ampBottom, 0.0f, b.getBottom(), false);
        grad.addColour (0.55, ui::ampMid);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 14.0f);
        g.setColour (ui::ampBorder);
        g.drawRoundedRectangle (b, 14.0f, 1.0f);

        g.setFont (ui::uiFont (12.0f, true));
        g.setColour (juce::Colour (0xfff2ede6));
        g.drawText ("AMP HEAD", ampCardBounds.getX() + 17, ampCardBounds.getY() + 15, 160, 14,
                    juce::Justification::centredLeft);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (juce::Colour (0xffc99a55));
        g.drawText (juce::CharPointer_UTF8 ("AMPLIFICADOR \xc2\xb7 NAM"),
                    ampCardBounds.getX() + 17, ampCardBounds.getY() + 31, 160, 11,
                    juce::Justification::centredLeft);

        const auto modelName = processor.getModelName();
        g.setFont (ui::uiFont (17.0f, true));
        g.setColour (modelName.isNotEmpty() ? ui::accentLight : juce::Colour (0xff8a7358));
        g.drawText (modelName.isNotEmpty() ? modelName
                                           : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 sem capture \xe2\x80\x94")),
                    ampCardBounds.getX() + 17, ampCardBounds.getY() + 52, ampCardBounds.getWidth() - 34, 22,
                    juce::Justification::centredLeft);

        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        juce::String info;
        const double modelSr = processor.getModelExpectedSampleRate();
        if (modelName.isNotEmpty())
        {
            info = (modelSr > 0 ? juce::String (modelSr / 1000.0, 1) + " kHz" + dot : juce::String())
                   + "mono" + dot + "NAM v0.5";
            if (processor.isResampling())
                info += dot + "resample";
        }
        else
        {
            info = "carregue um arquivo .nam";
        }
        g.setFont (ui::monoFont (9.0f));
        g.setColour (juce::Colour (0xffa98d63));
        g.drawText (info, ampCardBounds.getX() + 17, ampCardBounds.getY() + 76,
                    ampCardBounds.getWidth() - 34, 12, juce::Justification::centredLeft);

        // barra de glow
        {
            auto glow = juce::Rectangle<float> (b.getX() + 20.0f, b.getBottom() - 62.0f,
                                                b.getWidth() - 40.0f, 6.0f);
            const float alpha = processor.hasModelLoaded()
                                    && processor.apvts.getRawParameterValue ("ampOn")->load() > 0.5f
                                ? 0.85f : 0.18f;
            juce::ColourGradient grad2 (ui::accent.withAlpha (0.0f), glow.getX(), 0.0f,
                                        ui::accent.withAlpha (0.0f), glow.getRight(), 0.0f, false);
            grad2.addColour (0.5, ui::accent.withAlpha (alpha));
            g.setGradientFill (grad2);
            g.fillRoundedRectangle (glow, 4.0f);
        }
    }

    // ---- barra de status inferior
    {
        auto bar = juce::Rectangle<int> (0, H - 40, W, 40);
        g.setGradientFill ({ ui::topBarBottom, 0.0f, (float) bar.getY(),
                             juce::Colour (0xff1a1c1f), 0.0f, (float) bar.getBottom(), false });
        g.fillRect (bar);
        g.setColour (juce::Colour (0xff101215));
        g.fillRect (0, H - 40, W, 1);

        const double sr = processor.getSampleRate();
        const int bs = processor.getBlockSize();
        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        juce::String status;
        if (sr > 0)
        {
            status = juce::String (sr / 1000.0, 1) + " kHz" + dot + juce::String (bs) + " samples"
                     + dot + juce::String (bs / sr * 1000.0, 2) + " ms/bloco";
            if (const int lat = processor.getLatencySamples(); lat > 0)
                status += dot + "+" + juce::String (lat / sr * 1000.0, 2) + " ms resample";
        }

        g.setFont (ui::monoFont (9.5f));
        g.setColour (juce::Colour (0xff8a8d93));
        g.drawText (status, 22, H - 40, 460, 40, juce::Justification::centredLeft);

        juce::String warn;
        juce::Colour warnColour = ui::yellow;
        const auto err = processor.getLoadError();
        const double modelSr = processor.getModelExpectedSampleRate();
        if (err.isNotEmpty())
        {
            warn = "Erro: " + err;
            warnColour = ui::red;
        }
        else if (processor.isResampling() && modelSr > 0 && sr > 0)
        {
            warn = "resampleando " + juce::String (sr / 1000.0, 1)
                   + juce::String::fromUTF8 (" \xe2\x86\x92 ")
                   + juce::String (modelSr / 1000.0, 1) + " kHz";
            warnColour = ui::textFaint;
        }
        g.setFont (ui::monoFont (9.5f, true));
        g.setColour (warnColour);
        g.drawText (warn, W - 22 - 560, H - 40, 560, 40, juce::Justification::centredRight);
    }
}

void RigContent::timerCallback()
{
    auto toDb = [] (float linear) { return juce::Decibels::gainToDecibels (linear, -80.0f); };

    inMeterDb = juce::jmax (toDb (processor.inputPeak.load()), inMeterDb - 2.2f);
    outMeterDb = juce::jmax (toDb (processor.outputPeak.load()), outMeterDb - 2.2f);
    inMeter.setLevel (inMeterDb);
    outMeter.setLevel (outMeterDb);

    loadButton.setButtonText (processor.hasModelLoaded() ? "TROCAR CAPTURE NAM"
                                                         : "CARREGAR CAPTURE NAM");
    irButton.setButtonText (processor.hasIrLoaded() ? "TROCAR IR" : "CARREGAR IR");

    const auto presetName = processor.getCurrentPresetName();
    presetPill.setButtonText (processor.isLoadingModel()
                                  ? juce::String (juce::CharPointer_UTF8 ("Carregando\xe2\x80\xa6"))
                                  : (presetName.isNotEmpty() ? presetName
                                                             : juce::String ("(sem preset)")));
    presetPill.dotLit = processor.hasModelLoaded();

    repaint();
}

void RigContent::chooseModelFile()
{
    auto initialDir = juce::File (processor.getModelPath()).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

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

void RigContent::chooseIrFile()
{
    auto initialDir = juce::File (processor.getIrPath()).getParentDirectory();
    if (! initialDir.isDirectory())
        initialDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    fileChooser = std::make_unique<juce::FileChooser> ("Escolher impulse response (wav/aiff/flac)",
                                                       initialDir, "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                      processor.loadIrAsync (file);
                              });
}

void RigContent::savePresetDialog()
{
    const auto name = processor.getCurrentPresetName();
    auto initialFile = processor.getPresetsDirectory()
                           .getChildFile ((name.isNotEmpty() ? name : "Meu preset") + ".xml");

    fileChooser = std::make_unique<juce::FileChooser> ("Salvar preset", initialFile, "*.xml");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  processor.savePreset (file.withFileExtension ("xml"));
                              });
}

void RigContent::showPresetMenu()
{
    const auto files = processor.getPresetFiles();
    if (files.isEmpty())
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    const auto current = processor.getCurrentPresetName();

    for (int i = 0; i < files.size(); ++i)
    {
        const auto name = files.getReference (i).getFileNameWithoutExtension();
        menu.addItem (i + 1, name, true, name == current);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetPill),
                        [this, files] (int result)
                        {
                            if (result > 0 && result <= files.size())
                                processor.loadPreset (files.getReference (result - 1));
                        });
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
