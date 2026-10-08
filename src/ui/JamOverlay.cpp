#include "JamOverlay.h"

namespace
{
const char* amountNames[] { "INTENSITY", "COMPLEXITY", "FILL AMOUNT", "FOLLOW TIGHTNESS" };
const char* actionNames[] { "TAP", "RESYNC", "HALF", "DOUBLE", "FILL", "BREAK", "STOP NEXT BAR" };
const JamUiIntent::Kind actionKinds[] { JamUiIntent::Kind::tap, JamUiIntent::Kind::resync,
    JamUiIntent::Kind::half, JamUiIntent::Kind::doubleTempo, JamUiIntent::Kind::fill,
    JamUiIntent::Kind::breakBar, JamUiIntent::Kind::stopNextBar };
void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
           float size = 13.0f, juce::Colour colour = ui::text)
{
    g.setColour (colour);
    g.setFont (ui::uiFont (size, true));
    g.drawFittedText (s, r, juce::Justification::centredLeft, 1);
}
void panel (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (ui::cardBottom); g.fillRect (r);
    g.setColour (ui::border()); g.drawRect (r);
}
}

JamOverlay::JamOverlay()
{
    setLookAndFeel (&look);
    setOpaque (true);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    style.addItemList ({ "Indie Rock", "Pocket Funk", "Blues Shuffle", "Ambient Pulse" }, 1);
    mode.addItemList ({ "Fixed", "Count Me In", "Follow", "Loose Follow", "Free Jam" }, 1);
    for (auto* box : { &style, &mode })
    {
        content.addAndMakeVisible (*box);
        box->setColour (juce::ComboBox::backgroundColourId, ui::meterBg);
        box->setColour (juce::ComboBox::textColourId, ui::text);
    }
    style.setName ("Style"); mode.setName ("Mode");
    style.onChange = [this] { emit (JamUiIntent::Kind::style, style.getSelectedId()); };
    mode.onChange = [this] { emit (JamUiIntent::Kind::mode, mode.getSelectedId()); };
    for (size_t i = 0; i < amounts.size(); ++i)
    {
        auto& s = amounts[i]; content.addAndMakeVisible (s);
        s.setName (amountNames[i]); s.setRange (0.0, 100.0, 1.0);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 24);
        s.setTextValueSuffix ("%");
        s.setColour (juce::Slider::textBoxTextColourId, ui::text);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.onValueChange = [this, i] { emit (static_cast<JamUiIntent::Kind> (
            static_cast<int> (JamUiIntent::Kind::intensity) + static_cast<int> (i)), amounts[i].getValue()); };
    }
    content.addAndMakeVisible (start); start.setName ("StartStop");
    start.getProperties().set ("accent", true);
    start.onClick = [this] { emit (JamUiIntent::Kind::startStop); };
    for (size_t i = 0; i < actions.size(); ++i)
    {
        auto& b = actions[i]; content.addAndMakeVisible (b);
        b.setButtonText (actionNames[i]); b.setName (actionNames[i]);
        b.onClick = [this, i] { emit (actionKinds[i]); };
        b.setTooltip ("Simulation action only; no audio engine connected.");
    }
    content.addAndMakeVisible (diagnostics); diagnostics.setName ("Diagnostics");
    diagnostics.onClick = [this]
    {
        showDiagnostics = ! showDiagnostics;
        diagnostics.setButtonText (showDiagnostics ? "HIDE DIAGNOSTICS" : "SHOW DIAGNOSTICS");
        resized();
    };
    setViewState (state);
}

JamOverlay::~JamOverlay() { setLookAndFeel (nullptr); }
void JamOverlay::emit (JamUiIntent::Kind kind, double value)
{
    if (onIntent) onIntent ({ kind, value });
}
void JamOverlay::setViewState (const JamViewState& s)
{
    state = s;
    style.setSelectedId (s.style, juce::dontSendNotification);
    mode.setSelectedId (s.mode, juce::dontSendNotification);
    for (size_t i = 0; i < amounts.size(); ++i)
        amounts[i].setValue (s.amounts[i], juce::dontSendNotification);
    start.setButtonText (s.running ? "STOP DEMO" : "START DEMO");
    content.repaint();
}
void JamOverlay::paint (juce::Graphics& g) { g.fillAll (ui::bg); }
void JamOverlay::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = juce::jmax (560, getWidth() - viewport.getScrollBarThickness());
    content.setSize (w, showDiagnostics ? 770 : 680);
    const int pad = 24, inner = w - pad * 2;
    statusArea = { pad, 116, inner, 170 };
    controlsArea = { pad, 302, inner, 234 };
    actionArea = { pad, 552, inner, 66 };
    diagnostics.setBounds (pad, 634, 190, 28);
    diagnosticsArea = { pad, 676, inner, 76 };
    const int col = (inner - 44) / 2;
    style.setBounds (pad + 16, 342, col, 34);
    mode.setBounds (pad + 16 + col + 12, 342, col, 34);
    for (size_t i = 0; i < amounts.size(); ++i)
        amounts[i].setBounds (pad + 16 + static_cast<int> (i % 2) * (col + 12),
                             409 + static_cast<int> (i / 2) * 65, col - 12, 32);
    const int buttonW = (inner - 12) / 8;
    start.setBounds (pad + 12, 564, buttonW * 2 - 20, 42);
    // Wide layouts use one performance row; compact layouts wrap the actions.
    if (w >= 1000)
    {
        const int aw = (inner - buttonW * 2 - 20) / 7;
        for (int i = 0; i < 7; ++i)
            actions[static_cast<size_t> (i)].setBounds (pad + buttonW * 2 + i * aw, 564, aw - 5, 42);
    }
    else
    {
        start.setBounds (pad + 12, 564, inner - 24, 42);
        for (int i = 0; i < 7; ++i)
        {
            const int count = i < 4 ? 4 : 3;
            const int aw = (inner - 24) / count;
            actions[static_cast<size_t> (i)].setBounds (pad + 12 + (i < 4 ? i : i - 4) * aw,
                                                      i < 4 ? 620 : 672, aw - 6, 44);
        }
        diagnostics.setBounds (pad, 734, 190, 28);
        diagnosticsArea.setY (776);
        actionArea.setHeight (176);
        content.setSize (w, showDiagnostics ? 870 : 778);
    }
    content.repaint();
}

void JamOverlay::paintContent (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    auto header = content.getLocalBounds().reduced (24).removeFromTop (80);
    text (g, "JAM / PERFORMANCE", header.removeFromTop (32), 25.0f);
    text (g, "SIMULATION  /  no live guitar input or audio output", header.removeFromTop (26), 14.0f, ui::yellow);
    text (g, "Find a pocket. Shape the groove. Stay in the moment.", header, 12.0f, ui::textDim);
    panel (g, statusArea); panel (g, controlsArea); panel (g, actionArea);
    auto r = statusArea.reduced (16);
    const char* locks[] { "ACQUIRING", "LOCKED", "HOLDOVER", "LOST" };
    text (g, juce::String (state.running ? "DEMO RUNNING  /  " : "DEMO STOPPED  /  ")
          + locks[static_cast<int> (state.lock)], r.removeFromTop (24), 13.0f,
          state.lock == JamViewState::Lock::locked ? ui::accent : ui::yellow);
    auto metrics = r.removeFromTop (65);
    const int cell = metrics.getWidth() / 3;
    const juce::String captions[] { "CANDIDATE BPM", "CLOCK BPM", "CONFIDENCE" };
    const juce::String values[] { juce::String (state.candidateBpm, 1), juce::String (state.clockBpm, 1),
                                 juce::String (juce::roundToInt (state.confidence * 100)) + "%" };
    for (int i = 0; i < 3; ++i)
    {
        auto c = metrics.removeFromLeft (cell);
        text (g, captions[i], c.removeFromTop (20), 11.0f, ui::textDim);
        text (g, values[i], c, 30.0f, i == 1 ? ui::accentBright : ui::text);
    }
    text (g, "BAR " + juce::String (state.bar) + "  /  BEAT " + juce::String (state.beat)
          + "     NEXT: " + state.nextIntent, r.removeFromTop (24), 13.0f);
    auto meter = r.removeFromTop (22);
    text (g, "SIM INPUT", meter.removeFromLeft (86), 10.0f, ui::textDim);
    g.setColour (ui::meterBg); g.fillRect (meter.reduced (0, 6));
    g.setColour (ui::accent); g.fillRect (meter.reduced (0, 6).withWidth (
        juce::roundToInt (meter.getWidth() * juce::jlimit (0.0f, 1.0f, state.inputLevel))));
    text (g, "STYLE", style.getBounds().translated (0, -24).withHeight (20), 11.0f, ui::textDim);
    text (g, "JAM MODE", mode.getBounds().translated (0, -24).withHeight (20), 11.0f, ui::textDim);
    for (size_t i = 0; i < amounts.size(); ++i)
        text (g, amountNames[i], amounts[i].getBounds().translated (0, -24).withHeight (20), 11.0f, ui::textDim);
    if (showDiagnostics)
    {
        panel (g, diagnosticsArea);
        text (g, "DEVELOPMENT DIAGNOSTICS / SIMULATED", diagnosticsArea.reduced (12).removeFromTop (22), 11.0f, ui::yellow);
        text (g, state.diagnostics, diagnosticsArea.reduced (12).withTrimmedTop (24), 12.0f, ui::textDim);
    }
}
