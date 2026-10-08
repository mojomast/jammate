#include "JamOverlay.h"

namespace
{
const char* amountNames[] { "INTENSITY", "COMPLEXITY", "FILL AMOUNT", "FOLLOW TIGHTNESS" };
const char* lockNames[] { "ACQUIRING", "LOCKED", "HOLDOVER", "LOST" };

struct ActionDef
{
    const char* name;
    JamUiIntent::Kind kind;
    bool implemented;          // backed by a frozen JamLiveCommandType
    const char* tip;
};

// Order is presentation-only; names must stay stable for the preview wrapper.
const ActionDef actionDefs[] {
    { "TAP",           JamUiIntent::Kind::tap,         true,  "Tap tempo" },
    { "RESYNC",        JamUiIntent::Kind::resync,      true,  "Resync on the next beat" },
    { "RESYNC BAR",    JamUiIntent::Kind::resyncBar,   true,  "Resync at the next bar" },
    { "HALF",          JamUiIntent::Kind::half,        true,  "Half time" },
    { "DOUBLE",        JamUiIntent::Kind::doubleTempo, true,  "Double time" },
    { "FREEZE",        JamUiIntent::Kind::freeze,      true,  "Freeze the tempo" },
    { "RESUME",        JamUiIntent::Kind::resume,      true,  "Resume following" },
    { "STOP NEXT BAR", JamUiIntent::Kind::stopNextBar, true,  "Stop at the next bar" },
    { "RESET",         JamUiIntent::Kind::reset,       true,  "Reset the live session" },
    { "FILL",          JamUiIntent::Kind::fill,        false, "Fills are not implemented in this build" },
    { "BREAK",         JamUiIntent::Kind::breakBar,    false, "Breaks are not implemented in this build" },
};

void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
           float size = 13.0f, juce::Colour colour = ui::text, bool bold = false)
{
    g.setColour (colour);
    g.setFont (ui::uiFont (size, bold));
    g.drawFittedText (s, r, juce::Justification::centredLeft, 1);
}
void panel (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (ui::cardBottom); g.fillRect (r);
    g.setColour (ui::border()); g.drawRect (r);
}
juce::String bpmText (double v)
{
    return v > 0.001 ? juce::String (v, 1) : juce::String ("--");
}
juce::String pctText (float v, bool known)
{
    return known ? juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100.0f)) + "%"
                 : juce::String ("--");
}
}

JamOverlay::JamOverlay()
{
    setLookAndFeel (&look);
    setOpaque (true);
    setWantsKeyboardFocus (true);   // Tab traversal across the controls, Escape closes
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    for (auto* box : { &style, &mode })
    {
        content.addAndMakeVisible (*box);
        box->setColour (juce::ComboBox::backgroundColourId, ui::meterBg);
        box->setColour (juce::ComboBox::textColourId, ui::text);
    }
    style.setName ("Style");
    style.setTitle ("Jam style");
    style.setDescription ("Style of the accompaniment. Only Rock is implemented.");
    style.onChange = [this] { emit (JamUiIntent::Kind::style, style.getSelectedId()); };

    mode.setName ("Mode");
    mode.setTitle ("Tempo mode");
    mode.setDescription ("How much the clock follows the guitarist: Fixed, Follow or Loose.");
    mode.onChange = [this] { emit (JamUiIntent::Kind::mode, mode.getSelectedId()); };

    for (size_t i = 0; i < amounts.size(); ++i)
    {
        auto& s = amounts[i];
        content.addAndMakeVisible (s);
        s.setName (amountNames[i]);
        s.setTitle (amountNames[i]);
        s.setDescription ("Not implemented in this build.");
        s.setRange (0.0, 100.0, 1.0);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 24);
        s.setTextValueSuffix ("%");
        s.setColour (juce::Slider::textBoxTextColourId, ui::text);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.onValueChange = [this, i]
        {
            emit (static_cast<JamUiIntent::Kind> (
                static_cast<int> (JamUiIntent::Kind::intensity) + static_cast<int> (i)),
                amounts[i].getValue());
        };
    }

    content.addAndMakeVisible (start);
    start.setName ("StartStop");
    start.setTitle ("Start or stop listening");
    start.setDescription ("Starts or stops the live Jam session. Sound only begins after "
                          "the audio engine locks and echoes playback.");
    start.getProperties().set ("accent", true);
    start.onClick = [this] { emit (JamUiIntent::Kind::startStop); };

    for (size_t i = 0; i < actions.size(); ++i)
    {
        auto& b = actions[i];
        const auto& def = actionDefs[i];
        content.addAndMakeVisible (b);
        b.setButtonText (def.name);
        b.setName (def.name);
        b.setTitle (def.name);
        b.setDescription (def.tip);
        b.setTooltip (def.tip);
        const auto kind = def.kind;
        b.onClick = [this, kind] { emit (kind); };
    }

    content.addAndMakeVisible (diagnostics);
    diagnostics.setName ("Diagnostics");
    diagnostics.setTitle ("Diagnostics");
    diagnostics.onClick = [this]
    {
        showDiagnostics = ! showDiagnostics;
        diagnostics.setButtonText (showDiagnostics ? "HIDE DIAGNOSTICS" : "SHOW DIAGNOSTICS");
        resized();
    };

    configureControls();
    setViewState (state);
}

JamOverlay::~JamOverlay() { onIntent = {}; onClose = {}; setLookAndFeel (nullptr); }

void JamOverlay::emit (JamUiIntent::Kind kind, double value)
{
    if (onIntent) onIntent ({ kind, value });
}

void JamOverlay::setSimulatedPreview (bool shouldSimulate)
{
    if (simulatedPreview == shouldSimulate)
        return;
    simulatedPreview = shouldSimulate;
    state.simulated = shouldSimulate;
    configureControls();
    resized();
    content.repaint();
}

void JamOverlay::configureControls()
{
    const bool demo = simulatedPreview;

    style.clear (juce::dontSendNotification);
    if (demo) style.addItemList ({ "Indie Rock", "Pocket Funk", "Blues Shuffle", "Ambient Pulse" }, 1);
    else      style.addItem ("Rock", 1);
    style.setEnabled (demo);   // production surfaces Rock but style selection is not implemented

    mode.clear (juce::dontSendNotification);
    if (demo) mode.addItemList ({ "Fixed", "Count Me In", "Follow", "Loose Follow", "Free Jam" }, 1);
    else      mode.addItemList ({ "Fixed", "Follow", "Loose" }, 1);

    for (auto& s : amounts)
        s.setEnabled (demo);

    for (size_t i = 0; i < actions.size(); ++i)
        actions[i].setEnabled (demo || actionDefs[i].implemented);
}

void JamOverlay::open()
{
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void JamOverlay::close()
{
    setVisible (false);
    if (onClose) onClose();
}

void JamOverlay::setViewState (const JamViewState& s)
{
    state = s;
    style.setSelectedId (s.style, juce::dontSendNotification);
    mode.setSelectedId (s.mode, juce::dontSendNotification);
    for (size_t i = 0; i < amounts.size(); ++i)
        amounts[i].setValue (s.amounts[i], juce::dontSendNotification);

    const bool running = s.running;
    start.setButtonText (simulatedPreview ? (running ? "STOP DEMO" : "START DEMO")
                                          : (running ? "STOP" : "START LISTENING"));
    content.repaint();
}

void JamOverlay::paint (juce::Graphics& g) { g.fillAll (ui::bg); }

bool JamOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;   // let Tab and friends reach the focus traverser
}

void JamOverlay::resized()
{
    viewport.setBounds (getLocalBounds());
    const int sb = viewport.getScrollBarThickness();
    const int w = juce::jmax (560, getWidth() - sb);
    const int pad = 24, inner = w - pad * 2;

    int y = 20;
    headerArea = { pad, y, inner, 86 };
    y += 86 + 10;
    statusArea = { pad, y, inner, 176 };
    y += 176 + 10;
    telemetryArea = { pad, y, inner, 150 };
    y += 150 + 10;
    controlsArea = { pad, y, inner, 232 };
    {
        const int col = (inner - 32 - 44) / 2;
        style.setBounds (controlsArea.getX() + 16, controlsArea.getY() + 40, col, 34);
        mode.setBounds (controlsArea.getX() + 16 + col + 12, controlsArea.getY() + 40, col, 34);
        for (size_t i = 0; i < amounts.size(); ++i)
            amounts[i].setBounds (controlsArea.getX() + 16 + static_cast<int> (i % 2) * (col + 12),
                                  controlsArea.getY() + 116 + static_cast<int> (i / 2) * 52, col - 12, 32);
    }
    y += 232 + 10;

    // ---- action row: start button + wrapped action buttons
    const int startW = juce::jmin (214, inner / 3);
    const int startH = 46, gap = 8, rowH = 40;
    start.setBounds (pad + 16, y + 14, startW, startH);
    const int x0 = pad + 16 + startW + 14;
    const int availW = juce::jmax (120, pad + inner - 16 - x0);
    const int n = (int) actions.size();
    const int perRow = juce::jmax (2, availW / 120);
    const int rows = (n + perRow - 1) / perRow;
    const int btnW = juce::jmax (60, (availW - (perRow - 1) * gap) / perRow);
    for (int i = 0; i < n; ++i)
        actions[(size_t) i].setBounds (x0 + (i % perRow) * (btnW + gap),
                                       y + 14 + (i / perRow) * (rowH + gap), btnW, rowH);
    const int actH = 14 + rows * (rowH + gap) + 6;
    actionArea = { pad, y, inner, actH };
    y += actH + 10;

    diagnostics.setBounds (pad + 16, y + 12, 190, 30);
    y += 12 + 30 + 8;
    diagnosticsArea = { pad, y, inner, showDiagnostics ? 104 : 0 };
    if (showDiagnostics) y += 104 + 10;

    content.setSize (w, y + 10);
    content.repaint();
}

void JamOverlay::paintContent (juce::Graphics& g)
{
    g.fillAll (ui::bg);

    // ---- header
    auto header = headerArea.reduced (0, 0);
    text (g, "JAM / PERFORMANCE", header.removeFromTop (32), 25.0f, ui::text, true);
    const char* banner = simulatedPreview
        ? "SIMULATION  /  no live guitar input or audio output"
        : "LIVE PIPELINE  /  status below is the actual engine echo, not a preview";
    text (g, banner, header.removeFromTop (26), 14.0f, ui::yellow);
    text (g, "Rock groove, one prepared style. Tap and resync the clock to lock the pocket.",
          header, 12.0f, ui::textDim);

    // ---- status
    panel (g, statusArea);
    auto r = statusArea.reduced (16);

    juce::String statusText;
    juce::Colour statusColour = ui::text;
    if (simulatedPreview)
    {
        statusText = state.running ? "DEMO RUNNING" : "DEMO STOPPED";
        statusColour = state.running ? ui::accent : ui::yellow;
    }
    else if (! state.hasState)
    {
        statusText = state.failure != 0 ? "UNAVAILABLE" : "NOT CONNECTED";
        statusColour = ui::yellow;
    }
    else if (! state.prepared)
    {
        statusText = state.failure != 0 ? "UNAVAILABLE" : "NOT PREPARED";
        statusColour = ui::yellow;
    }
    else if (! state.requestedRunning)
    {
        statusText = "PREPARED - STOPPED";
        statusColour = ui::textDim;
    }
    else if (state.drumsPlaying)
    {
        statusText = "PLAYING (AUDIO ECHO)";
        statusColour = ui::accent;
    }
    else if (state.joinPending)
    {
        statusText = "ARMED - WAITING FOR THE CLOCK";
        statusColour = ui::yellow;
    }
    else
    {
        statusText = "ARMED - LISTENING";
        statusColour = ui::yellow;
    }
    text (g, statusText, r.removeFromTop (26), 16.0f, statusColour, true);
    text (g, "LOCK: " + juce::String (lockNames[static_cast<int> (state.lock)])
          + "    NEXT: " + state.nextIntent, r.removeFromTop (24), 13.0f, ui::text);

    if (state.availability.isNotEmpty())
        text (g, state.availability, r.removeFromTop (22), 11.5f, ui::yellow);

    auto line2 = r.removeFromTop (22);
    auto line2Left = line2.removeFromLeft (line2.getWidth() / 2);
    text (g, "MODE: " + mode.getText() + "     BACKEND: " + state.backendName
          + (state.failure != 0 ? "     FAILURE: " + state.failureName : juce::String()),
          line2Left, 11.0f, state.failure != 0 ? ui::red : ui::textDim);
    text (g, "BEAT " + juce::String (state.beat) + "/" + juce::String (state.beatsPerBar)
          + (state.tempoFrozen ? "   TEMPO FROZEN" : ""),
          line2, 11.0f, state.tempoFrozen ? ui::yellow : ui::textDim);

    if (state.commandFeedback.isNotEmpty())
        text (g, state.commandFeedback, r.removeFromTop (22), 12.0f, ui::red);

    // ---- telemetry
    panel (g, telemetryArea);
    auto t = telemetryArea.reduced (16);
    auto metrics = t.removeFromTop (70);
    const int cell = metrics.getWidth() / 3;
    const juce::String captions[] { "CANDIDATE BPM", "CLOCK BPM", "CONFIDENCE" };
    const juce::String values[] { bpmText (state.candidateBpm), bpmText (state.clockBpm),
                                  pctText (state.confidence, state.hasState || simulatedPreview
                                                                        || state.confidence > 0.001f) };
    for (int i = 0; i < 3; ++i)
    {
        auto c = metrics.removeFromLeft (cell);
        text (g, captions[i], c.removeFromTop (20), 11.0f, ui::textDim);
        text (g, values[i], c, 30.0f, i == 1 ? ui::accentBright : ui::text, true);
    }
    auto meter = t.removeFromTop (24);
    text (g, simulatedPreview ? "SIM INPUT" : "INPUT PEAK", meter.removeFromLeft (96), 10.0f, ui::textDim);
    g.setColour (ui::meterBg); g.fillRect (meter.reduced (0, 7));
    g.setColour (ui::accent); g.fillRect (meter.reduced (0, 7).withWidth (
        juce::roundToInt (meter.getWidth() * juce::jlimit (0.0f, 1.0f, state.inputLevel))));
    auto detail = t.removeFromTop (22);
    text (g, "DELIVERY: " + (state.receiptMeasured
              ? juce::String (state.receiptLagSamples) + " samples behind audio"
              : juce::String ("not measured"))
          + "     DROPS a/o/u/d " + juce::String ((int) state.analysisDrops) + "/"
          + juce::String ((int) state.observationDrops) + "/"
          + juce::String ((int) state.userCommandDrops) + "/"
          + juce::String ((int) state.drumCommandDrops)
          + "     DISC " + juce::String ((int) state.discontinuities),
          detail, 11.0f, ui::textDim);

    // ---- controls (child bounds are set in resized(); paint only labels)
    panel (g, controlsArea);
    const int col = (controlsArea.getWidth() - 32 - 44) / 2;
    text (g, "STYLE", style.getBounds().translated (0, -24).withHeight (20), 11.0f, ui::textDim);
    text (g, "JAM MODE", mode.getBounds().translated (0, -24).withHeight (20), 11.0f, ui::textDim);
    for (size_t i = 0; i < amounts.size(); ++i)
        text (g, amountNames[i], amounts[i].getBounds().translated (0, -22).withHeight (18),
              11.0f, ui::textDim);
    if (! simulatedPreview)
        text (g, "Intensity, complexity, fills and tightness are disabled: not implemented in this build.",
              { controlsArea.getX() + 16, controlsArea.getBottom() - 34, col * 2 + 12, 22 },
              11.0f, ui::yellow);

    panel (g, actionArea);

    if (showDiagnostics)
    {
        panel (g, diagnosticsArea);
        auto d = diagnosticsArea.reduced (12);
        text (g, simulatedPreview ? "DEVELOPMENT DIAGNOSTICS / SIMULATED"
                                  : "LIVE DIAGNOSTICS / FIXTURE OR HARDWARE DEPENDENT",
              d.removeFromTop (22), 11.0f, ui::yellow);
        text (g, state.diagnostics.isNotEmpty() ? state.diagnostics
                                                : juce::String ("No telemetry received yet."),
              d, 12.0f, ui::textDim);
    }
}
