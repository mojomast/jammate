#include "DrumOverlay.h"

#include "LookAndFeel.h"
#include "PluginEditor.h"   // KnobComponent (guitar ribbon)

//==============================================================================
// Fixed layout inside the editor's 1100x700 content (v4: the staff is the track).
// The top (0..gtrRibH) is the guitar ribbon; the module starts at headerY.
namespace
{
constexpr int gtrRibH = 66;   // guitar strip at the top
constexpr int margin = 26;
constexpr int headerY = 12 + gtrRibH, headerH = 34;
constexpr int tabsY = 52 + gtrRibH, tabsH = 26;
constexpr int barHeadsY = 80 + gtrRibH, barHeadsH = 30;
constexpr int scoreY = 114 + gtrRibH, scoreH = 294 - gtrRibH;
constexpr int libY = 414;                                 // top of the browser/grid
constexpr int gridY = 418, gridH = 226;                   // grid in place of the lib
constexpr int sourceY = 648, sourceH = 32;
// column browser: Genre | Grooves/Fills | Preview
constexpr int colGap = 8, genreColW = 150, listColW = 208, colRowH = 26;

// bar role (1..5) - label (UI) and key (generator)
juce::String roleLabel (int r)
{
    switch (r)
    {
        case 2: return "Chorus";
        case 3: return "Bridge";
        case 4: return "Breakdown";
        case 5: return "Fill";
        default: return "Verse";
    }
}
const char* roleKey (int r)
{
    switch (r) { case 2: return "chorus"; case 3: return "bridge";
                 case 4: return "breakdown"; case 5: return "fill"; default: return "verse"; }
}

// staff geometry: 4 bars x 16 steps across the usable width (~1048)
// 4 bars must fit in ~1038 usable px: 64*stepW + 12*beatPad +
// 3*barPad + scoreLeft + slack <= width, otherwise the 4th bar clips at the end
constexpr int scoreLeft = 54;
constexpr float stepW = 13.2f, beatPad = 5.0f, barPad = 20.0f;
constexpr float staffSP = 7.0f;   // half the distance between lines
constexpr float staffTop = 104.0f;

constexpr float tsW = 20.0f;   // width of the time signature on the staff
float staffY (float pos) { return staffTop + 8.0f * staffSP - pos * staffSP; }

// number of steps + beam grouping of a meter (compound meters in threes)
void meterGroups (int num, int den, int& steps, int groups[8], int& nGroups)
{
    steps = drum::stepsForMeter (num, den);
    nGroups = 0;
    if (den == 8 && num % 3 == 0)
        for (int i = 0; i < num / 3 && nGroups < 8; ++i) groups[nGroups++] = 6;
    else if (den == 8 && num == 7) { int g[] = { 4,4,6 }; for (int x : g) groups[nGroups++] = x; }
    else if (den == 8 && num == 5) { int g[] = { 4,6 };   for (int x : g) groups[nGroups++] = x; }
    else if (den == 4)
        for (int i = 0; i < num && nGroups < 8; ++i) groups[nGroups++] = 4;
    else if (den == 2)
        for (int i = 0; i < num && nGroups < 8; ++i) groups[nGroups++] = 8;
    else
    {
        int rem = steps;
        while (rem >= 4 && nGroups < 8) { groups[nGroups++] = 4; rem -= 4; }
        if (rem > 0 && nGroups < 8) groups[nGroups++] = rem;
    }
    if (nGroups == 0) { groups[0] = steps; nGroups = 1; }
}

// staff position / x-head / hand-or-foot per voice - drum::Voice indices
constexpr float staffPos[drum::numVoices] = { 1, 5, 9, -1, 8, 10, 7, 6, 3 };
constexpr bool staffXHead[drum::numVoices] = { false, false, true, true, true, true,
                                               false, false, false };
constexpr bool staffIsHand[drum::numVoices] = { false, true, true, false, true, true,
                                                true, true, true };

// Draws 1 bar as a STAFF inside `area` - same language as the central staff:
// 5-line staff, note heads (x cymbals / ellipse drums), stems (up hands, down
// feet) and beams per beat. Used in the card thumbnails.
void drawMiniBar (juce::Graphics& g, juce::Rectangle<float> area,
                  const juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar],
                  int num = 4, int den = 4)
{
    int steps = 16, groups[8], nGroups = 1;
    meterGroups (num, den, steps, groups, nGroups);
    // starting offset (in steps) of each group
    int gStart[8] = {}; for (int i = 1; i < nGroups; ++i) gStart[i] = gStart[i - 1] + groups[i - 1];

    const juce::Colour ink (0xffc4cdd6), dim (0xff3a424b);
    const float sp = (area.getHeight() - 4.0f) / 16.0f;   // positions -4..12
    auto yOf = [&] (float pos) { return area.getBottom() - 2.0f - (pos + 4.0f) * sp; };
    const float x0 = area.getX() + 4.0f;
    const float sw = (area.getWidth() - 8.0f) / (float) steps;
    auto xOf = [&] (int s) { return x0 + (s + 0.5f) * sw; };
    const float hr = juce::jmax (1.7f, sp * 0.72f);       // head radius

    // 5 staff lines (positions 0,2,4,6,8)
    g.setColour (dim);
    for (int i = 0; i <= 4; ++i)
        g.drawHorizontalLine ((int) yOf ((float) (i * 2)), area.getX(), area.getRight());
    // beat separators (meter group boundaries)
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    for (int gi = 1; gi < nGroups; ++gi)
        g.drawVerticalLine ((int) (x0 + gStart[gi] * sw), yOf (9.0f), yOf (-2.0f));

    auto drawHead = [&] (float x, float y, bool cross, int val)
    {
        g.setColour (val == 2 ? ui::glowOrange : val == 3 ? dim.brighter (0.45f) : ink);
        if (cross)
        {
            g.drawLine (x - hr, y - hr, x + hr, y + hr, 1.0f);
            g.drawLine (x - hr, y + hr, x + hr, y - hr, 1.0f);
        }
        else
            g.fillEllipse (x - hr * 1.05f, y - hr * 0.8f, hr * 2.1f, hr * 1.6f);
    };

    const float beamYH = yOf (12.0f), beamYF = yOf (-4.0f);

    for (int beat = 0; beat < nGroups; ++beat)
        for (int limb = 0; limb < 2; ++limb)
        {
            const bool up = (limb == 0);
            const int gLen = groups[beat];
            struct Col { int s; float noteY; };
            Col cols[8];
            int nc = 0;
            for (int i = 0; i < gLen; ++i)
            {
                const int s = gStart[beat] + i;
                float ext = up ? -1.0e9f : 1.0e9f;
                bool any = false;
                for (int v = 0; v < drum::numVoices; ++v)
                {
                    if (staffIsHand[v] != up)
                        continue;
                    const int val = pat[v][s];
                    if (val == 0)
                        continue;
                    any = true;
                    const float y = yOf (staffPos[v]);
                    drawHead (xOf (s), y, staffXHead[v], val);
                    ext = up ? juce::jmax (ext, y) : juce::jmin (ext, y);
                }
                if (any)
                    cols[nc++] = { s, ext };
            }
            if (nc == 0)
                continue;

            const float beamY = up ? beamYH : beamYF;
            auto stemX = [&] (int s) { return up ? xOf (s) + hr * 0.85f : xOf (s) - hr * 0.85f; };
            g.setColour (ink);
            for (int c = 0; c < nc; ++c)
                g.drawLine (stemX (cols[c].s), cols[c].noteY + (up ? -1.5f : 1.5f),
                            stemX (cols[c].s), beamY, 1.0f);

            if (nc > 1)
            {
                const float y = up ? beamY : beamY - 2.0f;
                g.fillRect (stemX (cols[0].s), y, stemX (cols[nc - 1].s) - stemX (cols[0].s), 2.0f);
                for (int c = 0; c < nc - 1; ++c)
                    if (cols[c + 1].s - cols[c].s == 1)
                        g.fillRect (stemX (cols[c].s), up ? beamY + 3.0f : beamY - 5.0f,
                                    stemX (cols[c + 1].s) - stemX (cols[c].s), 2.0f);
            }
            else
            {
                juce::Path flag;
                const float x = stemX (cols[0].s), dir = up ? 1.0f : -1.0f;
                flag.startNewSubPath (x, beamY);
                flag.quadraticTo (x + 4.0f, beamY + 3.0f * dir, x + 2.0f, beamY + 8.0f * dir);
                g.strokePath (flag, juce::PathStrokeType (1.0f));
            }
        }
}

// optional grid
constexpr int gridLabelW = 96;
constexpr int gCellW = 48, gCellGap = 3, gBeatGap = 10;
constexpr int gRowH = 21, gRowGap = 2;
int gridStepX (int s)
{
    return gridLabelW + s * (gCellW + gCellGap) + (s / 4) * gBeatGap;
}
} // namespace

const int DrumOverlay::gridRowVoice[DrumOverlay::gridRows] = {
    drum::crash, drum::hat, drum::ride, drum::tom1, drum::tom2,
    drum::snare, drum::floorTom, drum::kick, drum::hatPedal
};

//==============================================================================
// DrumRibbon - drum strip at the top of the guitar screen
DrumRibbon::DrumRibbon (DrumEngine& e) : engine (e)
{
    playBtn.setMouseClickGrabsKeyboardFocus (false);
    playBtn.getProperties().set ("accent", true);
    playBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));
    playBtn.onClick = [this] { engine.playing.store (! engine.playing.load()); repaint(); };
    addAndMakeVisible (playBtn);
    startTimerHz (15);
}

void DrumRibbon::resized()
{
    playBtn.setBounds (8, (getHeight() - 34) / 2, 40, 34);
}

void DrumRibbon::timerCallback()
{
    const bool playing = engine.playing.load();
    sectionShown = playing ? juce::jlimit (0, drum::maxSections - 1,
                                           engine.uiBar.load() / drum::barsPerSection)
                           : 0;
    playBtn.setButtonText (playing ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9d\x9a\xe2\x9d\x9a"))
                                   : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")));
    repaint();
}

void DrumRibbon::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (ui::cardBottom);
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (ui::accentDark.withAlpha (0.45f));
    g.drawRoundedRectangle (b, 2.0f, 1.0f);

    const bool playing = engine.playing.load();
    const int base = sectionShown * drum::barsPerSection;

    g.setColour (ui::textBright);
    g.setFont (ui::uiFont (12.0f, true));
    g.drawText ("DRUMS", 58, 7, 130, 14, juce::Justification::centredLeft);
    g.setColour (ui::textFaint);
    g.setFont (ui::uiFont (9.0f));
    g.drawText (playing ? juce::String (juce::CharPointer_UTF8 ("playing \xc2\xb7 follow along"))
                        : juce::String ("stopped"),
                58, 22, 150, 12, juce::Justification::centredLeft);

    g.setColour (ui::textBright);
    g.setFont (ui::monoFont (15.0f, true));
    g.drawText (juce::String ((int) engine.bpm.load()), 196, 6, 46, 18, juce::Justification::centred);
    g.setColour (ui::textFaint);
    g.setFont (ui::uiFont (8.0f, true));
    g.drawText ("BPM", 196, 25, 46, 10, juce::Justification::centred);

    const float staffX = 258.0f, staffR = (float) getWidth() - 108.0f;
    const float bw = (staffR - staffX) / (float) drum::barsPerSection;
    for (int i = 0; i < drum::barsPerSection; ++i)
    {
        const int bar = base + i;
        juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar];
        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::maxStepsPerBar; ++s)
                pat[v][s] = engine.pattern[bar][v][s].load();
        drawMiniBar (g, { staffX + i * bw, 5.0f, bw - 3.0f, (float) getHeight() - 10.0f },
                     pat, engine.meterNum (bar), engine.meterDen (bar));
    }

    const int ub = engine.uiBar.load();
    if (playing && ub >= base && ub < base + drum::barsPerSection)
    {
        const int steps = engine.barSteps (ub);
        const int st = juce::jlimit (0, steps - 1, engine.uiStep.load());
        const float fx = staffX + (ub - base) * bw + (st + 0.5f) / (float) steps * (bw - 3.0f);
        g.setColour (ui::accent);
        g.fillRect (fx, 5.0f, 2.0f, (float) getHeight() - 10.0f);
    }

    g.setColour (ui::textFaint);
    g.setFont (ui::uiFont (10.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("open drums \xe2\xa4\xa2")),
                getWidth() - 104, 0, 98, getHeight(), juce::Justification::centredRight);
}

void DrumRibbon::mouseUp (const juce::MouseEvent&)
{
    if (onOpen) onOpen();
}

//==============================================================================
DrumOverlay::DrumOverlay (GuitarRigNAMProcessor& p)
    : processor (p), engine (p.drumEngine)
{
    setWantsKeyboardFocus (true);

    closeButton.onClick = [this] { closeAnimated(); };
    addAndMakeVisible (closeButton);

    playButton.getProperties().set ("accent", true);
    playButton.onClick = [this]
    {
        engine.playing.store (! engine.playing.load());
        syncTransportUi();
    };
    addAndMakeVisible (playButton);

    bpmDown.onClick = [this]
    {
        engine.bpm.store (juce::jlimit (40.0f, 260.0f, engine.bpm.load() - 2.0f));
        repaint();
    };
    bpmUp.onClick = [this]
    {
        engine.bpm.store (juce::jlimit (40.0f, 260.0f, engine.bpm.load() + 2.0f));
        repaint();
    };
    addAndMakeVisible (bpmDown);
    addAndMakeVisible (bpmUp);

    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    swingSlider.setRange (0.0, 60.0, 1.0);
    swingSlider.onValueChange = [this]
    {
        engine.swingPct.store ((float) swingSlider.getValue());
        repaint();
    };
    addAndMakeVisible (swingSlider);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    levelSlider.setRange (0.0, 1.5, 0.01);
    levelSlider.onValueChange = [this]
    { engine.level.store ((float) levelSlider.getValue()); };
    addAndMakeVisible (levelSlider);

    for (auto* c : { &clickChip, &countChip, &followChip, &gridChip, &genChip, &editChip, &saveChip })
    {
        c->getProperties().set ("chip", true);
        c->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }
    editChip.getProperties().set ("chipActive", editMode);
    editChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "EDIT: clicking the staff edits the notes. "
        "ASSEMBLE (off): drag the whole bar to reposition/copy it")));
    editChip.onClick = [this]
    {
        editMode = ! editMode;
        editChip.getProperties().set ("chipActive", editMode);
        editChip.repaint();
        scoreView.setMouseCursor (editMode ? juce::MouseCursor::NormalCursor
                                           : juce::MouseCursor::DraggingHandCursor);
        scoreView.repaint();
    };
    clickChip.onClick = [this]
    {
        engine.clickOn.store (! engine.clickOn.load());
        clickChip.getProperties().set ("chipActive", engine.clickOn.load());
        clickChip.repaint();
    };
    countChip.setTooltip ("1 count-in bar before playing");
    countChip.onClick = [this]
    {
        engine.countInOn.store (! engine.countInOn.load());
        countChip.getProperties().set ("chipActive", engine.countInOn.load());
        countChip.repaint();
    };
    followChip.getProperties().set ("chipActive", true);
    followChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "The staff turns the page by itself when the music enters the "
        "next section")));
    followChip.onClick = [this]
    {
        followOn = ! followOn;
        followChip.getProperties().set ("chipActive", followOn);
        followChip.repaint();
    };
    gridChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "16-step grid of the selected bar (in place of the library)")));
    gridChip.onClick = [this]
    {
        gridOn = ! gridOn;
        if (gridOn) { genOn = false; genChip.getProperties().set ("chipActive", false); genChip.repaint(); }
        gridChip.getProperties().set ("chipActive", gridOn);
        gridChip.repaint();
        refreshAll();
    };
    genChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Groove generator: genre/style/drummer + parameters; "
        "fills 1 or all 4 bars (in place of the library)")));
    genChip.onClick = [this]
    {
        genOn = ! genOn;
        if (genOn) { gridOn = false; gridChip.getProperties().set ("chipActive", false); gridChip.repaint(); }
        genChip.getProperties().set ("chipActive", genOn);
        genChip.repaint();
        refreshAll();
    };
    setupGenerator();
    setupGuitarRibbon();

    // column browser: GROOVES / FILLS tabs (middle column)
    for (auto* c : { &tabGrooves, &tabViradas })
    {
        c->getProperties().set ("chip", true);
        c->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*c);
    }
    tabGrooves.onClick = [this] { currentKind = 1; rebuildList(); };
    tabViradas.onClick = [this] { currentKind = 2; rebuildList(); };
    saveChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Saves the selected bar to My bars "
        "(Documents\\PedalForge NAM\\compassos)")));
    saveChip.onClick = [this]
    {
        currentGenre = "MINE";
        if (gridOn)
        {
            gridOn = false;
            gridChip.getProperties().set ("chipActive", false);
        }
        refreshAll();
        saveNameEditor.grabKeyboardFocus();
    };

    addSectionBtn.onClick = [this]
    {
        const int n = engine.numSections.load();
        if (n >= drum::maxSections)
            return;
        engine.numSections.store (n + 1);
        curSection = n;
        selBar = 0;
        refreshAll();
    };
    addAndMakeVisible (addSectionBtn);

    delSectionBtn.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Removes the shown section (bars shift back one slot)")));
    delSectionBtn.onClick = [this]
    {
        const int n = engine.numSections.load();
        if (n <= 1)
            return;
        // pulls the bars of the following sections back by one section
        for (int g = curSection * drum::barsPerSection;
             g < (n - 1) * drum::barsPerSection; ++g)
        {
            const int src = g + drum::barsPerSection;
            engine.setMeter (g, engine.meterNum (src), engine.meterDen (src));
            engine.barFromString (engine.barToString (src), g); // "" clears; honors the meter
            engine.barNames[g] = engine.barNames[src];
            engine.barRole[g] = engine.barRole[src];
        }
        for (int g = (n - 1) * drum::barsPerSection; g < n * drum::barsPerSection; ++g)
        {
            engine.clearBar (g);
            engine.barNames[g].clear();
            engine.barRole[g] = 0;
        }
        engine.numSections.store (n - 1);
        curSection = juce::jmin (curSection, n - 2);
        selBar = 0;
        refreshAll();
    };
    addAndMakeVisible (delSectionBtn);

    addAndMakeVisible (scoreView);

    // browser columns
    genreVp.setViewedComponent (&genreContent, false);
    genreVp.setScrollBarsShown (true, false);
    genreVp.setScrollBarThickness (7);
    addChildComponent (genreVp);
    listVp.setViewedComponent (&listContent, false);
    listVp.setScrollBarsShown (true, false);
    listVp.setScrollBarThickness (7);
    addChildComponent (listVp);
    addChildComponent (previewPane);

    applyBtn.getProperties().set ("outlineAccent", true);
    applyBtn.setMouseClickGrabsKeyboardFocus (false);
    applyBtn.onClick = [this]
    {
        if (selValid)
            applyGrooveToBar (selDragId, selectedBar());
    };
    addChildComponent (applyBtn);

    // humanize (internal kit): velocity, micro-timing, round-robin
    struct HS { juce::Slider* s; std::atomic<float>* p; };
    for (auto hs : { HS { &humVelSlider, &engine.humanVel },
                     HS { &humTimeSlider, &engine.humanTime },
                     HS { &humRRSlider, &engine.humanRR } })
    {
        hs.s->setSliderStyle (juce::Slider::LinearHorizontal);
        hs.s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        hs.s->setRange (0.0, 1.0, 0.01);
        hs.s->setValue (hs.p->load(), juce::dontSendNotification);
        hs.s->setColour (juce::Slider::trackColourId, ui::glowOrange.withAlpha (0.7f));
        auto* p = hs.p;
        auto* sl = hs.s;
        hs.s->onValueChange = [p, sl] { p->store ((float) sl->getValue()); };
        hs.s->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*hs.s);
    }

    saveNameEditor.setFont (ui::monoFont (12.0f));
    saveNameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0c0e11));
    saveNameEditor.setColour (juce::TextEditor::outlineColourId, ui::border());
    saveNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, ui::accentDark);
    saveNameEditor.setColour (juce::TextEditor::textColourId, ui::text);
    saveNameEditor.setTextToShowWhenEmpty (
        juce::String (juce::CharPointer_UTF8 ("name for the selected bar\xe2\x80\xa6")),
        ui::textMuted);
    saveNameEditor.onReturnKey = [this] { saveUserGroove(); };
    addChildComponent (saveNameEditor);

    saveConfirm.getProperties().set ("outlineAccent", true);
    saveConfirm.onClick = [this] { saveUserGroove(); };
    addChildComponent (saveConfirm);

    addChildComponent (gridView);

    sourceChip.getProperties().set ("chip", true);
    sourceChip.setMouseClickGrabsKeyboardFocus (false);
    sourceChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Toggles between the internal kit and the loaded drum VST3")));
    sourceChip.onClick = [this]
    {
        if (processor.hasDrumPlugin())
            engine.useVst.store (! engine.useVst.load());
        refreshSourceRow();
    };
    addAndMakeVisible (sourceChip);

    vstLoadButton.onClick = [this] { if (onChooseVst) onChooseVst(); };
    vstPanelButton.onClick = [this] { if (onOpenVstPanel) onOpenVstPanel(); };
    vstClearButton.onClick = [this]
    {
        processor.clearDrumPlugin();
        refreshSourceRow();
    };
    addAndMakeVisible (vstLoadButton);
    addChildComponent (vstPanelButton);
    addChildComponent (vstClearButton);

    for (auto* b : std::initializer_list<juce::Button*> {
             &closeButton, &playButton, &bpmDown, &bpmUp, &saveConfirm,
             &addSectionBtn, &delSectionBtn,
             &vstLoadButton, &vstPanelButton, &vstClearButton })
        b->setMouseClickGrabsKeyboardFocus (false);

    // first time (empty timeline): builds a demo section with the default groove
    bool empty = true;
    for (int b = 0; b < drum::maxBars && empty; ++b)
        empty = ! engine.barUsed[b].load();
    if (empty && drum::library().size() > 1)
    {
        juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar];
        const auto& g0 = drum::library()[0];
        drum::parseSpec (g0, pat);
        for (int b = 0; b < 3; ++b)
        {
            engine.setMeter (b, g0.num, g0.den);
            engine.setBarPattern (pat, b);
            engine.barNames[b] = juce::String (juce::CharPointer_UTF8 (g0.name));
        }
    }

    refreshAll();
    refreshSourceRow();
    syncTransportUi();
    startTimerHz (30);
}

DrumOverlay::~DrumOverlay() = default;

void DrumOverlay::open()
{
    syncTransportUi();
    refreshSourceRow();
    refreshAll();
    morphT = 0.0f; morphTarget = 1.0f; morphing = true;  // grows from the top strip
    applyMorph();
    setVisible (true);
    toFront (true);
}

void DrumOverlay::closeAnimated()
{
    morphTarget = 0.0f; morphing = true;   // shrinks back to the strip
}

void DrumOverlay::applyMorph()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    if (W < 1.0f || H < 1.0f) return;
    // rectangle of the drum strip on the guitar screen (== DrumRibbon)
    const float rx = 18.0f, ry = 62.0f, rw = W - 36.0f, rh = 54.0f;
    auto L = [] (float a, float b, float t) { return a + (b - a) * t; };
    const float sx = L (rw / W, 1.0f, morphT), sy = L (rh / H, 1.0f, morphT);
    const float tx = L (rx, 0.0f, morphT),     ty = L (ry, 0.0f, morphT);
    setTransform (juce::AffineTransform::scale (sx, sy).translated (tx, ty));
    setAlpha (L (0.25f, 1.0f, morphT));
}

void DrumOverlay::timerCallback()
{
    if (! isVisible())
        return;

    if (morphing)
    {
        morphT += (morphTarget - morphT) * 0.30f;
        if (std::abs (morphT - morphTarget) < 0.012f)
        {
            morphT = morphTarget;
            morphing = false;
            if (morphTarget < 0.5f)   // finished closing
            {
                setTransform ({});
                setAlpha (1.0f);
                setVisible (false);
                return;
            }
            setTransform ({});
            setAlpha (1.0f);
        }
        else
            applyMorph();
    }

    const int uiBar = engine.uiBar.load();
    if (uiBar != lastUiBar || (uiBar >= 0 && engine.uiStep.load() >= 0))
    {
        if (uiBar != lastUiBar)
        {
            lastUiBar = uiBar;
            // FOLLOW: turns the page when the music enters another section
            if (followOn && uiBar >= 0)
            {
                const int sec = uiBar / drum::barsPerSection;
                if (sec != curSection)
                {
                    curSection = sec;
                    selBar = uiBar % drum::barsPerSection;
                    refreshAll();
                }
            }
            rebuildSectionTabs();
        }
        scoreView.repaint();
        if (gridOn)
            gridView.repaint();
        repaint (getWidth() - 220, headerY, 200, headerH); // position on the label
    }

    const bool playing = engine.playing.load();
    const auto want = playing ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP"))
                              : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY"));
    if (playButton.getButtonText() != want)
        playButton.setButtonText (want);

    const int nSec = engine.numSections.load();
    if (nSec != lastNumSections)
    {
        lastNumSections = nSec;
        curSection = juce::jmin (curSection, nSec - 1);
        refreshAll();
        syncTransportUi();
    }

    const bool hasVst = processor.hasDrumPlugin();
    if (hasVst != lastHasVst)
    {
        lastHasVst = hasVst;
        refreshSourceRow();
    }
}

void DrumOverlay::syncTransportUi()
{
    swingSlider.setValue (engine.swingPct.load(), juce::dontSendNotification);
    levelSlider.setValue (engine.level.load(), juce::dontSendNotification);
    clickChip.getProperties().set ("chipActive", engine.clickOn.load());
    countChip.getProperties().set ("chipActive", engine.countInOn.load());
    playButton.setButtonText (engine.playing.load()
                                  ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP"))
                                  : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY")));
    repaint();
}

//==============================================================================
void DrumOverlay::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);

    // ---- guitar ribbon (top): background + labels ----
    {
        juce::Rectangle<float> rib (0.0f, 0.0f, (float) getWidth(), (float) gtrRibH);
        g.setColour (ui::cardBottom);
        g.fillRect (rib);
        g.setColour (ui::accentDark.withAlpha (0.35f));
        g.drawLine (0.0f, (float) gtrRibH, (float) getWidth(), (float) gtrRibH, 1.0f);

        g.setColour (ui::textBright);
        g.setFont (ui::uiFont (12.0f, true));
        g.drawText ("GUITAR", margin, 12, 90, 14, juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.setFont (ui::uiFont (8.5f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("amp + active pedals")),
                    margin, 28, 90, 12, juce::Justification::centredLeft);

        // group names above the knobs (amp: knobs 0..5; OD: 6..8)
        if (gtrKnobs.size() >= 9)
        {
            g.setFont (ui::monoFont (8.0f, true));
            auto grp = [&] (const juce::String& t, int a, int b, juce::Colour c)
            {
                const int x0g = gtrKnobs[a]->getX();
                const int x1g = gtrKnobs[b]->getRight();
                g.setColour (c);
                g.drawText (t, x0g, 1, x1g - x0g, 9, juce::Justification::centred);
            };
            grp (juce::String (juce::CharPointer_UTF8 ("AMP \xc2\xb7 EVH 5150")), 0, 5, juce::Colour (0xffe0b072));
            grp ("OVERDRIVE", 6, 8, ui::accent);
        }
    }

    auto area = getLocalBounds().reduced (margin, 0);

    g.setColour (ui::green);
    g.fillEllipse ((float) area.getX(), headerY + 13.0f, 8.0f, 8.0f);
    g.setFont (ui::uiFont (15.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("DRUMS", area.getX() + 16, headerY, 100, headerH,
                juce::Justification::centredLeft);

    // BPM + transport labels
    g.setFont (ui::monoFont (16.0f, true));
    g.setColour (ui::textBright);
    g.drawText (juce::String ((int) engine.bpm.load()),
                area.getX() + 262, headerY, 46, headerH, juce::Justification::centred);
    g.setFont (ui::uiFont (9.0f, true));
    g.setColour (ui::textMuted);
    g.drawText ("BPM", area.getX() + 262, headerY - 6, 46, 10, juce::Justification::centred);
    g.drawText ("SWING " + juce::String ((int) engine.swingPct.load()) + "%",
                area.getX() + 362, headerY - 6, 110, 10, juce::Justification::centredLeft);

    // position (playing bar)
    {
        const int uiBar = engine.uiBar.load();
        g.setFont (ui::monoFont (10.0f));
        g.setColour (ui::textFaint);
        const auto txt = uiBar >= 0
                             ? "bar " + juce::String (uiBar + 1) + "/"
                                   + juce::String (engine.totalBars())
                             : juce::String (engine.totalBars()) + " bars";
        g.drawText (txt, getWidth() - margin - 44 - 180, headerY, 170, headerH,
                    juce::Justification::centredRight);
    }

    g.setFont (ui::uiFont (9.0f, true));
    g.setColour (ui::textMuted);
    g.drawText ("VOLUME", getWidth() - margin - 130, sourceY - 10, 130, 10,
                juce::Justification::centredLeft);

    // humanize labels
    if (humVelSlider.isVisible())
    {
        g.setFont (ui::monoFont (7.5f));
        g.setColour (ui::textMuted);
        auto lbl = [&] (juce::Slider& s, const char* t)
        {
            auto b = s.getBounds();
            g.drawText (t, b.getX(), b.getY() - 10, b.getWidth(), 9, juce::Justification::centred);
        };
        lbl (humVelSlider, "VELOCITY");
        lbl (humTimeSlider, "TIMING");
        lbl (humRRSlider, "R-ROBIN");
        g.setColour (ui::textFaint);
        g.drawText ("HUMANIZE", humVelSlider.getX() - 76, humVelSlider.getY() - 2, 72, 12,
                    juce::Justification::centredRight);
    }

    // generator labels
    if (genOn)
    {
        auto title = [&] (const juce::String& t, int x)
        {
            g.setFont (ui::uiFont (9.0f, true));
            g.setColour (ui::textFaint);
            g.drawText (t, x, libY + 4, 220, 12, juce::Justification::centredLeft);
        };
        title ("SOURCE", genGenreBox.getX());
        title ("PARAMETERS", genComplex.getX() - 96);
        title ("GENERATE", genOneBtn.getX());

        auto fld = [&] (const juce::String& t, juce::Component& c)
        {
            g.setFont (ui::monoFont (7.5f));
            g.setColour (ui::textMuted);
            g.drawText (t, c.getX(), c.getY() - 11, c.getWidth(), 9, juce::Justification::centredLeft);
        };
        fld ("GENRE", genGenreBox);
        fld ("STYLE", genStyleBox);
        fld (juce::String (juce::CharPointer_UTF8 ("DRUMMER \xc2\xb7 optional")), genDrummerBox);

        g.setFont (ui::monoFont (8.5f));
        g.setColour (ui::textDim);
        const juce::String pn[] = { "Complexity", "Dynamics", "Humanize", "Fills", "Swing" };
        juce::Slider* ps[] = { &genComplex, &genDynamics, &genHuman, &genFill, &genSwing };
        for (int i = 0; i < 5; ++i)
            g.drawText (pn[i], ps[i]->getX() - 96, ps[i]->getY(), 92, ps[i]->getHeight(),
                        juce::Justification::centredLeft);

        g.setFont (ui::monoFont (8.5f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 (
            "Reads each bar's time signature \xc2\xb7 writes on the staff above"),
            genOneBtn.getX(), genAllBtn.getBottom() + 6, genOneBtn.getWidth(), 14,
            juce::Justification::centredLeft);
    }
}

void DrumOverlay::resized()
{
    const int W = getWidth();
    const int x0 = margin;

    closeButton.setBounds (W - margin - 34, headerY + 1, 34, 30);

    playButton.setBounds (x0 + 96, headerY, 96, headerH);
    bpmDown.setBounds (x0 + 204, headerY + 4, 24, 26);
    bpmUp.setBounds (x0 + 204 + 24 + 50, headerY + 4, 24, 26);
    swingSlider.setBounds (x0 + 362, headerY + 5, 108, 24);
    clickChip.setBounds (x0 + 484, headerY + 3, 64, 28);
    countChip.setBounds (x0 + 552, headerY + 3, 96, 28);
    followChip.setBounds (x0 + 652, headerY + 3, 70, 28);
    gridChip.setBounds (x0 + 726, headerY + 3, 62, 28);
    genChip.setBounds (x0 + 792, headerY + 3, 60, 28);
    editChip.setBounds (x0 + 856, headerY + 3, 72, 28);

    // ---- guitar ribbon (top): amp + OD knobs wired to the APVTS
    {
        const int ky = 11, kw = 28, kh = 54;
        int gx = margin + 96;
        for (int i = 0; i < gtrKnobs.size(); ++i)
        {
            gtrKnobs[i]->setBounds (gx, ky, kw, kh);
            gx += kw + 4;
            if (i == 5) gx += 22;   // separates AMP | OVERDRIVE
        }
        gtrOpenBtn.setBounds (W - margin - 116, (gtrRibH - 26) / 2, 116, 26);
    }

    // section tabs
    {
        int sx = x0;
        for (auto* t : sectionTabs)
        {
            t->setBounds (sx, tabsY, 120, tabsH);
            sx += 124;
        }
        addSectionBtn.setBounds (sx, tabsY, 84, tabsH);
        sx += 90;
        delSectionBtn.setBounds (sx, tabsY, 86, tabsH);
    }

    // bar headers + staff (widths follow the meter)
    scoreView.setBounds (margin, scoreY, W - 2 * margin, scoreH);
    computeBarLayout (W - 2 * margin);
    for (auto* h : barHeads)
    {
        const auto& L = barLay[h->barInSec];
        const int x0 = margin + (int) (L.notesX - curStepW * 0.5f - 5.0f);
        h->setBounds (x0, barHeadsY, (int) (L.width + curStepW + 10.0f), barHeadsH - 2);
    }

    // library (column browser) OR grid, in the same area
    const int libW = W - 2 * margin;
    const int libBottom = sourceY - 8;
    const int libH = libBottom - libY;
    {
        genreVp.setBounds (margin, libY, genreColW, libH);
        const int listX = margin + genreColW + colGap;
        const int tabW = (listColW - 4) / 2;
        tabGrooves.setBounds (listX, libY, tabW, 24);
        tabViradas.setBounds (listX + tabW + 4, libY, tabW, 24);
        listVp.setBounds (listX, libY + 28, listColW, libH - 28);
        const int prevX = listX + listColW + colGap;
        const int prevW = (W - margin) - prevX;
        previewPane.setBounds (prevX, libY, prevW, libH - 38);
        applyBtn.setBounds (prevX, libBottom - 30, 190, 30);
        // MINE tab: save field to the right of "apply"
        saveNameEditor.setBounds (prevX + 200, libBottom - 30, prevW - 200 - 72, 30);
        saveConfirm.setBounds (W - margin - 66, libBottom - 30, 66, 30);
        // humanize (non-MINE): 3 sliders on the right
        const int hw = 62, hg = 8;
        const int hx = W - margin - (hw * 3 + hg * 2);
        humVelSlider.setBounds (hx, libBottom - 26, hw, 22);
        humTimeSlider.setBounds (hx + hw + hg, libBottom - 26, hw, 22);
        humRRSlider.setBounds (hx + 2 * (hw + hg), libBottom - 26, hw, 22);
    }
    gridView.setBounds (margin, gridY, libW, gridH);

    // generator (same area): SOURCE | PARAMETERS | GENERATE
    {
        const int fx = margin;                     // SOURCE column
        const int mx = margin + 350;               // PARAMETERS column
        const int gx = margin + 712;               // GENERATE column
        const int gw = (W - margin) - gx;
        genGenreBox.setBounds   (fx, libY + 30, 150, 30);
        genStyleBox.setBounds   (fx + 158, libY + 30, 172, 30);
        genDrummerBox.setBounds (fx, libY + 82, 330, 30);

        juce::Slider* ps[] = { &genComplex, &genDynamics, &genHuman, &genFill, &genSwing };
        for (int i = 0; i < 5; ++i)
            ps[i]->setBounds (mx + 96, libY + 24 + i * 34, 262, 22);

        genOneBtn.setBounds (gx, libY + 40, gw, 46);
        genAllBtn.setBounds (gx, libY + 96, gw, 58);
    }

    sourceChip.setBounds (margin, sourceY, 150, sourceH);
    vstLoadButton.setBounds (margin + 158, sourceY, 140, sourceH);
    vstPanelButton.setBounds (margin + 306, sourceY, 76, sourceH);
    vstClearButton.setBounds (margin + 390, sourceY, 90, sourceH);
    saveChip.setBounds (margin + 492, sourceY + 2, 152, 28);
    levelSlider.setBounds (W - margin - 130, sourceY + 3, 130, 26);
}

//==============================================================================
// Staff layout with variable meters (computes per-bar widths and, if it
// overflows the usable width, shrinks everything proportionally to fit).
void DrumOverlay::computeBarLayout (int availW)
{
    const int sec0 = curSection * drum::barsPerSection;
    auto build = [&] (float sw, float bp, float bpad) -> float
    {
        float x = (float) scoreLeft;
        int prevN = -1, prevD = -1;
        for (int b = 0; b < drum::barsPerSection; ++b)
        {
            auto& L = barLay[b];
            const int gb = sec0 + b;
            L.num = engine.meterNum (gb);
            L.den = engine.meterDen (gb);
            meterGroups (L.num, L.den, L.steps, L.groups, L.nGroups);
            L.showTS = (b == 0) || L.num != prevN || L.den != prevD;
            if (L.showTS) { L.tsX = x + tsW * 0.5f - 3.0f; x += tsW; }
            else L.tsX = -1.0f;
            L.notesX = x;
            L.width = L.steps * sw + (L.nGroups - 1) * bp;
            x += L.width + bpad;
            prevN = L.num; prevD = L.den;
        }
        return x - bpad + 12.0f;
    };

    float total = build (stepW, beatPad, barPad);
    const float avail = (float) juce::jmax (200, availW);
    if (total > avail)
    {
        const float k = avail / total;
        curStepW = stepW * k; curBeatPad = beatPad * k; curBarPad = barPad * k;
        total = build (curStepW, curBeatPad, curBarPad);
    }
    else { curStepW = stepW; curBeatPad = beatPad; curBarPad = barPad; }
    scoreTotalW = total;
}

int DrumOverlay::groupIndexInBar (int b, int s) const
{
    const auto& L = barLay[b];
    int acc = 0;
    for (int i = 0; i < L.nGroups; ++i) { acc += L.groups[i]; if (s < acc) return i; }
    return juce::jmax (0, L.nGroups - 1);
}

float DrumOverlay::stepXInBar (int b, int s) const
{
    return barLay[b].notesX + s * curStepW + groupIndexInBar (b, s) * curBeatPad
           + curStepW * 0.5f;
}

int DrumOverlay::barAtXlocal (int x) const
{
    for (int b = 0; b < drum::barsPerSection; ++b)
        if ((float) x >= barLay[b].notesX - curStepW * 0.5f - 5.0f
            && (float) x <= barLay[b].notesX + barLay[b].width + 5.0f)
            return b;
    return -1;
}

//==============================================================================
void DrumOverlay::refreshAll()
{
    rebuildSectionTabs();
    rebuildBarHeads();
    rebuildGenreCol();
    rebuildList();

    const bool lib = ! gridOn && ! genOn;      // library, grid OR generator
    const bool mine = currentGenre == "MINE";
    genreVp.setVisible (lib);
    listVp.setVisible (lib);
    previewPane.setVisible (lib);
    tabGrooves.setVisible (lib && ! mine);
    tabViradas.setVisible (lib && ! mine);
    applyBtn.setVisible (lib && selValid);
    saveNameEditor.setVisible (lib && mine);
    saveConfirm.setVisible (lib && mine);
    const bool humShow = lib && ! mine;
    humVelSlider.setVisible (humShow);
    humTimeSlider.setVisible (humShow);
    humRRSlider.setVisible (humShow);
    gridView.setVisible (gridOn);

    juce::Component* genComps[] = { &genGenreBox, &genStyleBox, &genDrummerBox,
                                    &genComplex, &genDynamics, &genHuman, &genFill, &genSwing,
                                    &genOneBtn, &genAllBtn };
    for (auto* c : genComps)
        c->setVisible (genOn);

    gridView.repaint();
    scoreView.repaint();
    repaint();
}

void DrumOverlay::rebuildSectionTabs()
{
    sectionTabs.clear();
    const int nSec = juce::jlimit (1, drum::maxSections, engine.numSections.load());
    curSection = juce::jlimit (0, nSec - 1, curSection);
    const int playSec = engine.uiBar.load() >= 0
                            ? engine.uiBar.load() / drum::barsPerSection : -1;

    for (int i = 0; i < nSec; ++i)
    {
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
        auto* t = sectionTabs.add (new juce::TextButton (
            juce::String ("SECTION ") + letter
            + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
            + juce::String (i * 4 + 1) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
            + juce::String (i * 4 + 4)));
        t->getProperties().set ("chip", true);
        t->getProperties().set ("chipActive", i == curSection);
        if (i == playSec && i != curSection)
            t->setColour (juce::TextButton::textColourOffId, ui::glowOrange);
        t->setMouseClickGrabsKeyboardFocus (false);
        t->onClick = [this, i]
        {
            curSection = i;
            selBar = 0;
            refreshAll();
        };
        addAndMakeVisible (*t);
    }
    addSectionBtn.setEnabled (nSec < drum::maxSections);
    delSectionBtn.setEnabled (nSec > 1);
    resized();
}

void DrumOverlay::rebuildBarHeads()
{
    barHeads.clear();
    for (int b = 0; b < drum::barsPerSection; ++b)
    {
        const int g = curSection * drum::barsPerSection + b;
        auto* h = barHeads.add (new BarHead());
        h->barInSec = b;
        h->empty = ! engine.barUsed[g].load();
        h->title = juce::String (g + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                   + (h->empty ? juce::String ("empty")
                               : (engine.barNames[g].isNotEmpty() ? engine.barNames[g]
                                                           : juce::String ("edited")));
        h->selected = b == selBar;
        h->meterText = juce::String (engine.meterNum (g)) + "/" + juce::String (engine.meterDen (g));
        h->roleId = resolveRole (g);
        h->roleAuto = engine.barRole[g] == 0;
        h->roleText = roleLabel (h->roleId);
        h->onSelect = [this, b]
        {
            selBar = b;
            refreshAll();
        };
        h->onClear = [this, g]
        {
            engine.clearBar (g);
            engine.barNames[g].clear();
            refreshAll();
        };
        h->onMeter = [this, b, h] { selBar = b; openMeterMenu (b, h); };
        h->onRole  = [this, b, h] { selBar = b; openRoleMenu (b, h); };
        addAndMakeVisible (*h);
    }
    resized();
}

void DrumOverlay::BarHead::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    if (selected)
    {
        g.setColour (ui::accent.withAlpha (0.08f));
        g.fillRoundedRectangle (b, 7.0f);
    }
    g.setColour (selected ? ui::accentDark : ui::border());
    g.drawRoundedRectangle (b, 7.0f, 1.0f);

    const int H = getHeight(), W = getWidth();
    const int py = (H - 18) / 2;
    const juce::juce_wchar caret = juce::CharPointer_UTF8 ("\xe2\x96\xbe")[0];

    auto pill = [&] (juce::Rectangle<int> r, const juce::String& txt, juce::Colour c,
                     bool strong, bool mono)
    {
        g.setColour (c.withAlpha (0.11f)); g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (c.withAlpha (strong ? 0.6f : 0.32f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (strong ? c : c.withAlpha (0.85f));
        g.setFont (mono ? ui::monoFont (9.5f, true) : ui::uiFont (9.5f, true));
        g.drawText (txt, r.getX() + 7, r.getY(), r.getWidth() - 22, 18, juce::Justification::centredLeft);
        g.setFont (ui::monoFont (7.0f));
        g.drawText (juce::String::charToString (caret), r.getRight() - 13, r.getY(), 10, 18,
                    juce::Justification::centred);
    };

    // ROLE pill (left) - color by role; dimmer when "auto"
    const juce::Colour rc = roleId == 3 ? ui::textDim : (roleId >= 4 ? ui::glowOrange : ui::accent);
    const int rtw = juce::GlyphArrangement::getStringWidthInt (ui::uiFont (9.5f, true), roleText);
    roleRect = { 6, py, rtw + 24, 18 };
    pill (roleRect, roleText, rc, ! roleAuto, false);

    // close/clear (right)
    const bool showX = ! empty;
    clearRect = showX ? juce::Rectangle<int> (W - 22, py, 16, 18) : juce::Rectangle<int>();
    if (showX)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), clearRect, juce::Justification::centred);
    }

    // TIME SIGNATURE pill (before the close icon)
    const bool odd = meterText != "4/4";
    const juce::Colour mc = odd ? ui::accent : ui::textFaint;
    const int mtw = juce::GlyphArrangement::getStringWidthInt (ui::monoFont (9.5f, true), meterText);
    const int mpw = mtw + 24;
    const int mrx = (showX ? clearRect.getX() : W - 6) - 6 - mpw;
    meterRect = { mrx, py, mpw, 18 };
    pill (meterRect, meterText, mc, odd, true);

    // title (number, groove) in the middle, if it fits
    const int tx = roleRect.getRight() + 8;
    const int tw = meterRect.getX() - 6 - tx;
    if (tw > 24)
    {
        g.setFont (ui::uiFont (10.5f, false));
        g.setColour (empty ? ui::textMuted : (selected ? ui::accent : ui::textDim));
        g.drawText (title, tx, 0, tw, H, juce::Justification::centredLeft, true);
    }
}

void DrumOverlay::BarHead::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (! empty && clearRect.contains (p)) { if (onClear)  onClear();  return; }
    if (meterRect.contains (p))            { if (onMeter)  onMeter();  return; }
    if (roleRect.contains (p))             { if (onRole)   onRole();   return; }
    if (onSelect) onSelect();
}

//==============================================================================
// The central staff (time signature per bar, variable widths)
void DrumOverlay::ScoreView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0c0e11));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 2.0f);
    g.setColour (ui::accentDark.withAlpha (0.55f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 2.0f, 1.0f);

    owner.computeBarLayout (getWidth());
    auto& engine = owner.engine;
    const juce::Colour ink (0xffc9d2da), dim (0xff5a6570);
    const int sec0 = owner.curSection * drum::barsPerSection;
    auto sx = [&] (int b, int s) { return owner.stepXInBar (b, s); };

    // staff lines + percussion clef
    g.setColour (dim);
    for (int i = 0; i <= 4; ++i)
        g.drawHorizontalLine ((int) staffY ((float) (i * 2)), 16.0f, (float) getWidth() - 10.0f);
    g.setColour (ink);
    g.fillRect (22.0f, staffY (6), 3.6f, 4.0f * staffSP);
    g.fillRect (29.0f, staffY (6), 3.6f, 4.0f * staffSP);

    // background of the selected bar
    {
        const auto& L = owner.barLay[owner.selBar];
        const float x0 = L.notesX - owner.curStepW * 0.5f - 5.0f, x1 = L.notesX + L.width + 5.0f;
        g.setColour (ui::accent.withAlpha (0.05f));
        g.fillRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 2.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        const float dash[] = { 3.0f, 3.0f };
        juce::Path pth;
        pth.addRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 2.0f);
        juce::PathStrokeType (1.0f).createDashedStroke (pth, pth, dash, 2);
        g.fillPath (pth);
    }
    if (dragOverBar >= 0)
    {
        const auto& L = owner.barLay[dragOverBar];
        const float x0 = L.notesX - owner.curStepW * 0.5f - 5.0f, x1 = L.notesX + L.width + 5.0f;
        g.setColour (ui::glowOrange.withAlpha (0.10f));
        g.fillRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 2.0f);
        g.setColour (ui::glowOrange);
        g.drawRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 2.0f, 1.4f);
    }

    // playhead
    const int uiBar = engine.uiBar.load();
    if (uiBar >= 0 && uiBar / drum::barsPerSection == owner.curSection)
    {
        const int b = uiBar % drum::barsPerSection, s = juce::jmax (0, engine.uiStep.load());
        g.setColour (ui::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (sx (b, s) - owner.curStepW * 0.5f + 1, staffY (13.0f),
                                owner.curStepW - 2, staffY (-5.0f) - staffY (13.0f), 3.0f);
    }

    auto drawHead = [&] (float x, float y, bool cross, bool ghost)
    {
        g.setColour (ink);
        if (cross)
        {
            const float r = 3.9f;
            g.drawLine (x - r, y - r, x + r, y + r, 1.7f);
            g.drawLine (x - r, y + r, x + r, y - r, 1.7f);
        }
        else
        {
            juce::Path p;
            p.addEllipse (x - 4.8f, y - 3.6f, 9.6f, 7.2f);
            p.applyTransform (juce::AffineTransform::rotation (-0.31f, x, y));
            g.fillPath (p);
        }
        if (ghost)
        {
            g.setFont (ui::monoFont (10.0f));
            g.setColour (dim);
            g.drawText ("(", (int) x - 13, (int) y - 6, 7, 12, juce::Justification::centred);
            g.drawText (")", (int) x + 6, (int) y - 6, 7, 12, juce::Justification::centred);
            g.setColour (ink);
        }
    };

    const float beamYH = staffY (12.0f), beamYF = staffY (-4.0f);

    for (int b = 0; b < drum::barsPerSection; ++b)
    {
        const auto& L = owner.barLay[b];
        const int bar = sec0 + b;

        // time signature (only when it changes) - in cyan
        if (L.showTS)
        {
            g.setColour (ui::accent);
            g.setFont (ui::monoFont (15.0f, true));
            g.drawText (juce::String (L.num), (int) L.tsX - 9, (int) staffY (8), 18,
                        (int) (2 * staffSP), juce::Justification::centred);
            g.drawText (juce::String (L.den), (int) L.tsX - 9, (int) staffY (4), 18,
                        (int) (2 * staffSP), juce::Justification::centred);
        }

        // bar line
        const float bx = L.notesX + L.width + owner.curBarPad * 0.5f - 2.0f;
        g.setColour (ink);
        g.drawLine (bx, staffY (8), bx, staffY (0), b == drum::barsPerSection - 1 ? 1.6f : 1.1f);

        // beat numbers (1 per group)
        g.setFont (ui::monoFont (9.0f));
        g.setColour (dim);
        for (int gi = 0, gs = 0; gi < L.nGroups; gs += L.groups[gi], ++gi)
            g.drawText (juce::String (gi + 1), (int) sx (b, gs) - 8,
                        (int) staffY (-5.0f) + 12, 16, 12, juce::Justification::centred);

        if (! engine.barUsed[bar].load())
        {
            g.setFont (ui::monoFont (10.0f));
            g.setColour (dim);
            g.drawText (juce::CharPointer_UTF8 ("\xc2\xb7 \xc2\xb7 \xc2\xb7"),
                        (int) L.notesX, (int) staffY (5.0f) - 8, (int) L.width, 16,
                        juce::Justification::centred);
            continue;
        }

        // notes + stems + beams, per meter beat group
        for (int gi = 0, gStart = 0; gi < L.nGroups; gStart += L.groups[gi], ++gi)
        {
            const int gLen = L.groups[gi];
            for (int limb = 0; limb < 2; ++limb)
            {
                const bool up = limb == 0;
                struct Col { int s; float noteY; bool accent; };
                Col cols[8];
                int numCols = 0;

                for (int k = 0; k < gLen; ++k)
                {
                    const int s = gStart + k;
                    float extremeY = up ? -1.0e9f : 1.0e9f;
                    bool any = false, acc = false;
                    for (int v = 0; v < drum::numVoices; ++v)
                    {
                        if (staffIsHand[v] != up)
                            continue;
                        const int val = engine.pattern[bar][v][s].load();
                        if (val == 0)
                            continue;
                        any = true;
                        if (val == 2)
                            acc = true;
                        const float y = staffY (staffPos[v]);
                        drawHead (sx (b, s), y, staffXHead[v], val == 3);
                        extremeY = up ? juce::jmax (extremeY, y) : juce::jmin (extremeY, y);
                    }
                    if (any && numCols < 8)
                        cols[numCols++] = { s, extremeY, acc };
                }
                if (numCols == 0)
                    continue;

                const float beamY = up ? beamYH : beamYF;
                auto stemX = [&] (int s) { return up ? sx (b, s) + 4.6f : sx (b, s) - 4.6f; };

                g.setColour (ink);
                for (int c = 0; c < numCols; ++c)
                {
                    g.drawLine (stemX (cols[c].s), cols[c].noteY + (up ? -2.0f : 2.0f),
                                stemX (cols[c].s), beamY, 1.4f);
                    if (cols[c].accent)
                    {
                        g.setColour (ui::glowOrange);
                        g.setFont (ui::uiFont (12.0f, true));
                        g.drawText (">", (int) sx (b, cols[c].s) - 8,
                                    (int) (up ? beamY - 18.0f : beamY + 3.0f), 16, 15,
                                    juce::Justification::centred);
                        g.setColour (ink);
                    }
                }

                if (numCols > 1)
                {
                    const float y = up ? beamY : beamY - 3.0f;
                    g.fillRect (stemX (cols[0].s), y,
                                stemX (cols[numCols - 1].s) - stemX (cols[0].s), 3.0f);
                    for (int c = 0; c < numCols - 1; ++c)
                        if (cols[c + 1].s - cols[c].s == 1)
                            g.fillRect (stemX (cols[c].s), up ? beamY + 4.8f : beamY - 7.8f,
                                        stemX (cols[c + 1].s) - stemX (cols[c].s), 3.0f);
                }
                else
                {
                    juce::Path flag;
                    const float x = stemX (cols[0].s), dir = up ? 1.0f : -1.0f;
                    flag.startNewSubPath (x, beamY);
                    flag.quadraticTo (x + 7.0f, beamY + 4.5f * dir, x + 3.5f, beamY + 13.0f * dir);
                    g.strokePath (flag, juce::PathStrokeType (1.5f));
                }
            }
        }
    }

    // legend
    g.setFont (ui::monoFont (8.5f));
    g.setColour (ui::textMuted);
    g.drawText (juce::String (juce::CharPointer_UTF8 (
                    "drag a groove \xc2\xb7 click edits \xc2\xb7 time signature in the bar "
                    "header \xc2\xb7 \xc3\x97 cymbals \xc2\xb7 > accent \xc2\xb7 ( ) ghost")),
                20, 4, getWidth() - 40, 12, juce::Justification::centredLeft);
}

int DrumOverlay::ScoreView::barAtX (int x) const { return owner.barAtXlocal (x); }

void DrumOverlay::ScoreView::mouseDown (const juce::MouseEvent& e)
{
    owner.computeBarLayout (getWidth());
    downBar = owner.barAtXlocal (e.x);

    // ASSEMBLE: only selects; the bar drag starts in mouseDrag
    if (! owner.editMode)
    {
        if (downBar >= 0) { owner.selBar = downBar; owner.refreshAll(); }
        return;
    }

    // EDIT: find step/voice and edit the note
    int bb = -1, ss = -1;
    for (int b = 0; b < drum::barsPerSection && bb < 0; ++b)
        for (int s = 0; s < owner.barLay[b].steps; ++s)
            if (std::abs ((float) e.x - owner.stepXInBar (b, s)) <= owner.curStepW * 0.5f + 0.5f)
            {
                bb = b; ss = s; break;
            }
    if (bb < 0)
        return;

    int voice = -1;
    float best = 8.0f;
    for (int v = 0; v < drum::numVoices; ++v)
    {
        const float d = std::abs ((float) e.y - staffY (staffPos[v]));
        if (d < best) { best = d; voice = v; }
    }
    if (voice < 0)
        return;

    const int bar = owner.curSection * drum::barsPerSection + bb;
    owner.selBar = bb;
    if (! owner.engine.barUsed[bar].load())
    {
        juce::uint8 zero[drum::numVoices][drum::maxStepsPerBar] = {};
        owner.engine.setBarPattern (zero, bar);
        owner.engine.barNames[bar] = "new";
    }
    auto& cell = owner.engine.pattern[bar][voice][ss];
    cell.store ((juce::uint8) ((cell.load() + 1) % 4));
    owner.rebuildBarHeads();
    repaint();
    if (owner.gridOn)
        owner.gridView.repaint();
}

void DrumOverlay::ScoreView::mouseDrag (const juce::MouseEvent& e)
{
    if (owner.editMode || downBar < 0 || e.getDistanceFromDragStart() < 6)
        return;
    const int srcG = owner.curSection * drum::barsPerSection + downBar;
    if (! owner.engine.barUsed[srcG].load())
        return;   // empty bar: nothing to drag
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        dnd->startDragging ("bar:" + juce::String (srcG), this);
}

void DrumOverlay::ScoreView::itemDragMove (const SourceDetails& d)
{
    const int b = owner.barAtXlocal (d.localPosition.getX());
    if (b != dragOverBar) { dragOverBar = b; repaint(); }
}

void DrumOverlay::ScoreView::itemDragExit (const SourceDetails&)
{
    dragOverBar = -1;
    repaint();
}

void DrumOverlay::ScoreView::itemDropped (const SourceDetails& d)
{
    const int b = owner.barAtXlocal (d.localPosition.getX());
    dragOverBar = -1;
    repaint();
    if (b < 0)
        return;
    const int dstG = owner.curSection * drum::barsPerSection + b;
    const auto desc = d.description.toString();

    // dragging a whole bar (assemble): copies pattern + meter
    if (desc.startsWith ("bar:"))
    {
        const int srcG = desc.substring (4).getIntValue();
        if (srcG != dstG)
        {
            owner.engine.setMeter (dstG, owner.engine.meterNum (srcG),
                                   owner.engine.meterDen (srcG));
            owner.engine.barFromString (owner.engine.barToString (srcG), dstG);
            owner.engine.barNames[dstG] = owner.engine.barNames[srcG];
            owner.selBar = b;
            owner.refreshAll();
        }
        return;
    }
    owner.applyGrooveToBar (desc, dstG);
}

//==============================================================================
// Time signature menu: 5 common ones + Custom (numerator/denominator)
void DrumOverlay::openMeterMenu (int barInSec, juce::Component* anchor)
{
    const int gb = curSection * drum::barsPerSection + barInSec;
    auto apply = [safe = juce::Component::SafePointer<DrumOverlay> (this)] (int g, int num, int den)
    {
        if (safe == nullptr)
            return;
        safe->engine.setMeter (g, num, den);
        safe->refreshAll();
    };

    juce::PopupMenu m;
    static const int MAIN[5][2] = { { 4, 4 }, { 3, 4 }, { 2, 4 }, { 6, 8 }, { 12, 8 } };
    for (auto& mm : MAIN)
    {
        const bool on = engine.meterNum (gb) == mm[0] && engine.meterDen (gb) == mm[1];
        m.addItem (juce::String (mm[0]) + "/" + juce::String (mm[1]), true, on,
                   [apply, gb, mm] { apply (gb, mm[0], mm[1]); });
    }
    m.addSeparator();
    m.addItem (juce::String (juce::CharPointer_UTF8 ("Custom\xe2\x80\xa6")),
               [this, gb, apply]
    {
        auto* w = new juce::AlertWindow (
            juce::String ("Time signature"),
            juce::String ("Numerator (1 to 16) and denominator (2, 4, 8 or 16)"),
            juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("num", juce::String (engine.meterNum (gb)), "Numerator");
        w->addTextEditor ("den", juce::String (engine.meterDen (gb)), "Denominator");
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (juce::String ("Cancel"), 0,
                      juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [w, gb, apply] (int r)
            {
                if (r == 1)
                {
                    const int num = w->getTextEditorContents ("num").getIntValue();
                    int den = w->getTextEditorContents ("den").getIntValue();
                    if (den != 2 && den != 4 && den != 8 && den != 16)
                        den = 4;
                    apply (gb, juce::jlimit (1, 16, num), den);
                }
                delete w;
            }), false);
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}

int DrumOverlay::resolveRole (int globalBar) const
{
    const int r = engine.barRole[globalBar];
    if (r > 0) return r;
    static const int arc[drum::barsPerSection] = { 1, 2, 3, 5 }; // verse/chorus/bridge/fill
    return arc[globalBar % drum::barsPerSection];
}

void DrumOverlay::openRoleMenu (int barInSec, juce::Component* anchor)
{
    const int gb = curSection * drum::barsPerSection + barInSec;
    auto set = [safe = juce::Component::SafePointer<DrumOverlay> (this)] (int g, int role)
    {
        if (safe == nullptr) return;
        safe->engine.barRole[g] = role;
        safe->refreshAll();
    };

    juce::PopupMenu m;
    const int cur = engine.barRole[gb];
    m.addItem (juce::String ("Automatic (by arc)"), true,
               cur == 0, [set, gb] { set (gb, 0); });
    m.addSeparator();
    for (int r = 1; r <= 5; ++r)
        m.addItem (roleLabel (r), true, cur == r, [set, gb, r] { set (gb, r); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor));
}

//==============================================================================
// Column browser: Genre | Grooves/Fills | Preview
void DrumOverlay::rebuildGenreCol()
{
    genreRows.clear();
    auto names = drum::genres();
    names.add (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 MINE")));
    const auto star = juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85"));
    int y = 0;
    for (const auto& n : names)
    {
        const auto key = n.startsWith (star) ? juce::String ("MINE") : n;
        int cnt = 0;
        if (key == "MINE")
            cnt = userGroovesDir().findChildFiles (juce::File::findFiles, false, "*.json").size();
        else
            for (const auto& gg : drum::library())
                if (juce::String (juce::CharPointer_UTF8 (gg.genre)) == key) ++cnt;

        auto* b = genreRows.add (new juce::TextButton());
        b->setButtonText (n + "  (" + juce::String (cnt) + ")");
        b->setColour (juce::TextButton::buttonColourId,
                      key == currentGenre ? ui::accentDark.withAlpha (0.22f) : juce::Colour (0));
        b->setColour (juce::TextButton::buttonOnColourId, ui::accentDark.withAlpha (0.22f));
        b->setColour (juce::TextButton::textColourOffId,
                      key == currentGenre ? ui::accent : ui::textDim);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->onClick = [this, key] { currentGenre = key; selValid = false; refreshAll(); };
        genreContent.addAndMakeVisible (b);
        b->setBounds (2, y, genreColW - 14, colRowH);
        y += colRowH + 2;
    }
    genreContent.setSize (genreColW, juce::jmax (y, 1));
    genreVp.setViewPosition (0, 0);
}

void DrumOverlay::rebuildList()
{
    libRows.clear();
    tabGrooves.getProperties().set ("chipActive", currentKind == 1);
    tabViradas.getProperties().set ("chipActive", currentKind == 2);
    tabGrooves.repaint();
    tabViradas.repaint();

    const bool mine = currentGenre == "MINE";
    int y = 0;
    auto add = [&] (const juce::String& name, const juce::String& dragId, bool fill, bool del,
                    std::function<void()> onDel)
    {
        auto* r = libRows.add (new LibRow());
        r->name = name;
        r->dragId = dragId;
        r->fill = fill;
        r->deletable = del;
        r->selected = (selValid && dragId == selDragId);
        r->onSelect = [this, dragId, name, fill] { selectEntry (dragId, name, fill); };
        r->onDelete = std::move (onDel);
        listContent.addAndMakeVisible (r);
        r->setBounds (0, y, listColW - 10, colRowH);
        y += colRowH + 2;
    };

    if (mine)
    {
        for (const auto& f : userGroovesDir().findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            const auto parsed = juce::JSON::parse (f.loadFileAsString());
            const auto nm = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
            add (nm, "u:" + f.getFullPathName(), false, true,
                 [this, f] { f.deleteFile(); selValid = false; rebuildList(); updatePreview(); });
        }
    }
    else
    {
        const auto& lib = drum::library();
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            const auto& g = lib[(size_t) i];
            if (juce::String (juce::CharPointer_UTF8 (g.genre)) != currentGenre)
                continue;
            if (currentKind == 1 && g.fill)  continue;
            if (currentKind == 2 && ! g.fill) continue;
            add (juce::String (juce::CharPointer_UTF8 (g.name)), "f:" + juce::String (i),
                 g.fill, false, nullptr);
        }
    }
    listContent.setSize (listColW - 10, juce::jmax (y, 1));
    listVp.setViewPosition (0, 0);

    if (! selValid && ! libRows.isEmpty())        // selects the first by default
    {
        auto* r = libRows[0];
        selectEntry (r->dragId, r->name, r->fill);
    }
    else
        updatePreview();
}

void DrumOverlay::selectEntry (const juce::String& dragId, const juce::String& name, bool fill)
{
    selDragId = dragId;
    selName = name;
    selFill = fill;
    selValid = true;
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::maxStepsPerBar; ++s)
            selPat[v][s] = 0;
    selBpm = 0;
    selNum = 4; selDen = 4;

    if (dragId.startsWith ("f:"))
    {
        const int i = dragId.substring (2).getIntValue();
        const auto& lib = drum::library();
        if (i >= 0 && i < (int) lib.size())
        {
            drum::parseSpec (lib[(size_t) i], selPat);
            selBpm = lib[(size_t) i].bpm;
            selNum = lib[(size_t) i].num;
            selDen = lib[(size_t) i].den;
        }
    }
    else if (dragId.startsWith ("u:"))
    {
        const auto parsed = juce::JSON::parse (juce::File (dragId.substring (2)).loadFileAsString());
        selBpm = (int) parsed.getProperty ("bpm", 0);
        const auto str = parsed.getProperty ("pattern", "").toString();
        int k = 0;
        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::stepsPerBar; ++s)
            {
                const juce::juce_wchar ch = k < str.length() ? str[k] : '0';
                selPat[v][s] = ch >= '0' && ch <= '3' ? (juce::uint8) (ch - '0') : 0;
                ++k;
            }
    }
    for (auto* r : libRows)
    {
        const bool on = r->dragId == dragId;
        if (r->selected != on) { r->selected = on; r->repaint(); }
    }
    updatePreview();
}

void DrumOverlay::updatePreview()
{
    applyBtn.setButtonText (juce::String ("apply to bar ")
                            + juce::String (selectedBar() + 1));
    applyBtn.setVisible (! gridOn && selValid);
    previewPane.repaint();
}

void DrumOverlay::applyGrooveToBar (const juce::String& dragId, int globalBar)
{
    if (globalBar < 0 || globalBar >= engine.totalBars())
        return;

    juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar] = {};
    juce::String name;
    int gNum = 4, gDen = 4;   // groove meter (library grooves may be odd)

    if (dragId.startsWith ("f:"))
    {
        const int idx = dragId.substring (2).getIntValue();
        const auto& lib = drum::library();
        if (idx < 0 || idx >= (int) lib.size())
            return;
        const auto& g = lib[(size_t) idx];
        drum::parseSpec (g, pat);
        name = juce::String (juce::CharPointer_UTF8 (g.name));
        gNum = g.num; gDen = g.den;
    }
    else if (dragId.startsWith ("u:"))
    {
        const juce::File f (dragId.substring (2));
        const auto parsed = juce::JSON::parse (f.loadFileAsString());
        const auto str = parsed.getProperty ("pattern", "").toString();
        if (str.isEmpty())
            return;
        int i = 0;
        for (int v = 0; v < drum::numVoices; ++v)
            for (int s = 0; s < drum::stepsPerBar; ++s)
            {
                const juce::juce_wchar c = i < str.length() ? str[i] : '0';
                pat[v][s] = c >= '0' && c <= '3' ? (juce::uint8) (c - '0') : 0;
                ++i;
            }
        name = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
        // user bars are saved in 4/4 (16 steps)
    }
    else
        return;

    // applies ONLY the notes - the song's BPM/swing does not change when you
    // drop a groove (the value on the card is just a suggestion; adjust the
    // tempo in the transport). The bar's meter becomes the groove's (odd
    // prog/djent grooves bring 7/8, 5/4, etc.). setMeter BEFORE setBarPattern
    // so barSteps() already reflects the new meter.
    engine.setMeter (globalBar, gNum, gDen);
    engine.setBarPattern (pat, globalBar);
    engine.barNames[globalBar] = name;

    curSection = globalBar / drum::barsPerSection;
    selBar = globalBar % drum::barsPerSection;
    refreshAll();
}

//==============================================================================
// ---- Groove generator (phase 19) ------------------------------------------
void DrumOverlay::setupGuitarRibbon()
{
    auto& apvts = processor.apvts;
    struct K { const char* id; const char* lbl; };
    static const K knobs[] = {
        { "ampGain", "GAIN" }, { "ampBass", "BASS" }, { "ampMid", "MID" },
        { "ampTreble", "TREB" }, { "ampPresence", "PRES" }, { "ampMaster", "MASTER" },
        { "odDrive", "DRIVE" }, { "odTone", "TONE" }, { "odLevel", "LVL" }
    };
    for (auto& k : knobs)
    {
        auto* kn = new KnobComponent (apvts, k.id, k.lbl,
                                      [] (float v) { return juce::String (v, 1); });
        gtrKnobs.add (kn);
        addAndMakeVisible (*kn);
    }

    gtrOpenBtn.getProperties().set ("chip", true);
    gtrOpenBtn.setMouseClickGrabsKeyboardFocus (false);
    gtrOpenBtn.onClick = [this] { if (onClose) onClose(); closeAnimated(); };
    addAndMakeVisible (gtrOpenBtn);
}

void DrumOverlay::setupGenerator()
{
    for (auto* b : { &genGenreBox, &genStyleBox, &genDrummerBox })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*b);
    }
    for (const auto& g : drum::genGenres())
        genGenreBox.addItem (g, genGenreBox.getNumItems() + 1);
    genGenreBox.setSelectedItemIndex (0, juce::dontSendNotification);
    genGenreBox.onChange = [this] { rebuildGenStyles(); rebuildGenDrummers(); };

    // 5 parameters 0..1
    struct SP { juce::Slider* s; double def; } sps[] = {
        { &genComplex, 0.65 }, { &genDynamics, 0.60 }, { &genHuman, 0.35 },
        { &genFill, 0.20 },    { &genSwing, 0.00 } };
    for (auto& sp : sps)
    {
        sp.s->setSliderStyle (juce::Slider::LinearHorizontal);
        sp.s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
        sp.s->setRange (0.0, 1.0, 0.01);
        sp.s->setValue (sp.def, juce::dontSendNotification);
        sp.s->setColour (juce::Slider::trackColourId, ui::accent.withAlpha (0.7f));
        sp.s->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*sp.s);
    }
    genFill.setColour (juce::Slider::trackColourId, ui::glowOrange.withAlpha (0.7f));

    genOneBtn.setButtonText (juce::String ("GENERATE THIS BAR"));
    genAllBtn.setButtonText (juce::String ("FILL THE 4 BARS"));
    genOneBtn.getProperties().set ("chip", true);
    genAllBtn.getProperties().set ("accent", true);
    genOneBtn.setMouseClickGrabsKeyboardFocus (false);
    genAllBtn.setMouseClickGrabsKeyboardFocus (false);
    genOneBtn.onClick = [this] { generateOne(); };
    genAllBtn.onClick = [this] { generateAll(); };
    addChildComponent (genOneBtn);
    addChildComponent (genAllBtn);

    rebuildGenStyles();
    rebuildGenDrummers();
}

void DrumOverlay::rebuildGenStyles()
{
    genStyleBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& s : drum::genStyles (genGenreBox.getText()))
        genStyleBox.addItem (s, id++);
    genStyleBox.setSelectedItemIndex (0, juce::dontSendNotification);
}

void DrumOverlay::rebuildGenDrummers()
{
    genDrummerBox.clear (juce::dontSendNotification);
    genDrummerBox.addItem (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 none \xe2\x80\x94")), 1);
    const auto& drs = drum::genDrummers();
    const juce::String genre = genGenreBox.getText();
    for (int i = 0; i < (int) drs.size(); ++i)
    {
        const bool fits = drum::drummerFitsGenre (drs[(size_t) i].id, genre);
        auto label = juce::String (drs[(size_t) i].name);
        if (! fits) label += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7 (other genre)"));
        genDrummerBox.addItem (label, i + 2);
        genDrummerBox.setItemEnabled (i + 2, fits);
    }
    genDrummerBox.setSelectedId (1, juce::dontSendNotification);
}

void DrumOverlay::fillBarWithGen (int globalBar, const juce::String& role, juce::uint32 seed)
{
    if (globalBar < 0 || globalBar >= engine.totalBars())
        return;

    drum::GenParams gp;
    gp.genre = genGenreBox.getText();
    gp.style = genStyleBox.getText();
    const int di = genDrummerBox.getSelectedId();
    const auto& drs = drum::genDrummers();
    if (di >= 2 && di - 2 < (int) drs.size())
        gp.drummer = drs[(size_t) (di - 2)].id;
    gp.role       = role;
    gp.complexity = (float) genComplex.getValue();
    gp.dynamics   = (float) genDynamics.getValue();
    gp.fillFreq   = (float) genFill.getValue();
    gp.num = engine.meterNum (globalBar);   // HONORS the bar's time signature
    gp.den = engine.meterDen (globalBar);
    gp.seed = seed;

    juce::uint8 pat[drum::numVoices][drum::maxStepsPerBar];
    drum::generateBar (gp, pat);
    engine.setBarPattern (pat, globalBar);

    auto nm = gp.style.substring (0, 1).toUpperCase() + gp.style.substring (1);
    if (role == "fill") nm = "Fill " + nm;
    engine.barNames[globalBar] = nm;

    // the humanize/swing parameters go to playback (atomics)
    const float h = (float) genHuman.getValue();
    engine.humanVel.store (juce::jlimit (0.0f, 1.0f, 0.15f + h * 0.5f));
    engine.humanTime.store (juce::jlimit (0.0f, 1.0f, h * 0.45f));
    engine.humanRR.store  (juce::jlimit (0.0f, 1.0f, 0.2f + h * 0.6f));
    engine.swingPct.store ((float) genSwing.getValue() * 60.0f);
}

void DrumOverlay::generateOne()
{
    const int g = selectedBar();
    fillBarWithGen (g, roleKey (resolveRole (g)), genSeedCtr++);
    refreshAll();
}

void DrumOverlay::generateAll()
{
    const int base = curSection * drum::barsPerSection;
    for (int i = 0; i < drum::barsPerSection; ++i)
        fillBarWithGen (base + i, roleKey (resolveRole (base + i)), genSeedCtr++);
    refreshAll();
}

juce::File DrumOverlay::userGroovesDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PedalForge NAM").getChildFile ("compassos");
    dir.createDirectory();
    return dir;
}

void DrumOverlay::saveUserGroove()
{
    const auto name = saveNameEditor.getText().trim();
    const int g = selectedBar();
    if (name.isEmpty() || ! engine.barUsed[g].load())
    {
        saveNameEditor.grabKeyboardFocus();
        return;
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("bpm", (int) engine.bpm.load());
    obj->setProperty ("swing", (int) engine.swingPct.load());
    obj->setProperty ("pattern", engine.barToString (g));
    userGroovesDir()
        .getChildFile (juce::File::createLegalFileName (name) + ".json")
        .replaceWithText (juce::JSON::toString (juce::var (obj), true));

    saveNameEditor.setText ("");
    engine.barNames[g] = name;
    refreshAll();
}

//==============================================================================
void DrumOverlay::LibRow::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (selected)
    {
        g.setColour (ui::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (b, 6.0f);
    }
    else if (isMouseOver())
    {
        g.setColour (ui::glass());
        g.fillRoundedRectangle (b, 6.0f);
    }
    // marker: dot (groove) / orange dash (fill)
    if (fill)
    {
        g.setColour (ui::glowOrange);
        g.fillRoundedRectangle (3.0f, 5.0f, 2.5f, b.getHeight() - 10.0f, 1.2f);
    }
    else
    {
        g.setColour (ui::accentDark);
        g.fillEllipse (4.0f, b.getCentreY() - 2.0f, 4.0f, 4.0f);
    }
    g.setFont (ui::uiFont (12.0f, selected));
    g.setColour (selected ? ui::accent : ui::textDim);
    g.drawText (name, 14, 0, getWidth() - (deletable ? 30 : 18), getHeight(),
                juce::Justification::centredLeft);
    if (deletable)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), getWidth() - 16, 0, 12, getHeight(),
                    juce::Justification::centred);
    }
}

void DrumOverlay::LibRow::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    if (deletable && e.getPosition().x > getWidth() - 20)
    {
        if (onDelete) onDelete();
        return;
    }
    if (onSelect) onSelect();
}

void DrumOverlay::LibRow::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    if (deletable && e.getMouseDownPosition().x > getWidth() - 20)
        return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        dnd->startDragging (dragId, this);
    }
}

//==============================================================================
void DrumOverlay::PreviewPane::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0c0e11));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 2.0f);
    g.setColour (ui::border());
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 2.0f, 1.0f);

    if (! owner.selValid)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::String ("select a groove from the list"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setFont (ui::uiFont (14.0f, true));
    g.setColour (ui::textBright);
    g.drawText (owner.selName, 14, 8, getWidth() - 150, 20, juce::Justification::centredLeft);
    g.setFont (ui::monoFont (9.0f));
    g.setColour (owner.selFill ? ui::glowOrange : ui::accent);
    auto tag = owner.selFill ? juce::String ("FILL")
                             : "GROOVE" + (owner.selBpm > 0 ? juce::String (" \xc2\xb7 ")
                                                                  + juce::String (owner.selBpm) + " bpm"
                                                            : juce::String());
    if (owner.selNum != 4 || owner.selDen != 4)
        tag += juce::String (" \xc2\xb7 ") + juce::String (owner.selNum) + "/" + juce::String (owner.selDen);
    g.drawText (juce::String (juce::CharPointer_UTF8 (tag.toRawUTF8())), getWidth() - 150, 9, 140, 16,
                juce::Justification::centredRight);

    drawMiniBar (g, { 14.0f, 34.0f, (float) getWidth() - 28.0f, (float) getHeight() - 74.0f },
                 owner.selPat, owner.selNum, owner.selDen);

    g.setFont (ui::monoFont (9.0f));
    g.setColour (ui::textFaint);
    g.drawText (juce::String (juce::CharPointer_UTF8 (
                    "\xe2\xa0\xbf drag onto the staff, or use \"apply\"")),
                14, getHeight() - 26, getWidth() - 28, 14, juce::Justification::centredLeft);
}

void DrumOverlay::PreviewPane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! owner.selValid || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        dnd->startDragging (owner.selDragId, this);
    }
}

//==============================================================================
// Optional grid (16 steps of the selected bar)
juce::Rectangle<int> DrumOverlay::GridView::cellBounds (int row, int step) const
{
    return { gridStepX (step), 16 + row * (gRowH + gRowGap), gCellW, gRowH };
}

void DrumOverlay::GridView::paint (juce::Graphics& g)
{
    auto& engine = owner.engine;
    const int bar = owner.selectedBar();
    const int steps = engine.barSteps (bar);

    g.setFont (ui::monoFont (9.0f));
    g.setColour (ui::textFaint);
    g.drawText ("GRID " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                    + " bar " + juce::String (bar + 1)
                    + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                    + juce::String (engine.meterNum (bar)) + "/" + juce::String (engine.meterDen (bar)),
                0, 0, 260, 14, juce::Justification::centredLeft);
    for (int s = 0; s < steps; ++s)
    {
        g.setColour (s % 4 == 0 ? ui::textDim : ui::textMuted);
        const juce::String lbl = s % 4 == 0 ? juce::String (s / 4 + 1)
                                            : (s % 2 == 0 ? juce::String ("&")
                                                          : juce::String::fromUTF8 ("\xc2\xb7"));
        g.drawText (lbl, gridStepX (s), 0, gCellW, 14, juce::Justification::centred);
    }

    const int uiBar = engine.uiBar.load();
    const int playStep = uiBar == bar ? engine.uiStep.load() : -1;

    for (int row = 0; row < gridRows; ++row)
    {
        const int v = gridRowVoice[row];
        g.setFont (ui::uiFont (11.0f, true));
        g.setColour (ui::textDim);
        g.drawText (juce::String (juce::CharPointer_UTF8 (drum::voiceNames[v])),
                    0, 16 + row * (gRowH + gRowGap), gridLabelW - 10, gRowH,
                    juce::Justification::centredRight);

        for (int s = 0; s < steps; ++s)
        {
            auto r = cellBounds (row, s).toFloat();
            const int val = engine.barUsed[bar].load()
                                ? engine.pattern[bar][v][s].load() : 0;

            juce::Colour c = s % 4 == 0 ? juce::Colours::white.withAlpha (0.085f)
                                        : juce::Colours::white.withAlpha (0.05f);
            if (val == 1) c = ui::accentDark;
            else if (val == 2) c = ui::glowOrange;
            else if (val == 3) c = ui::accent.withAlpha (0.3f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 5.0f);

            if (s == playStep)
            {
                g.setColour (ui::accent);
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.4f);
            }
        }
    }
}

void DrumOverlay::GridView::mouseDown (const juce::MouseEvent& e)
{
    const int steps = owner.engine.barSteps (owner.selectedBar());
    for (int row = 0; row < gridRows; ++row)
        for (int s = 0; s < steps; ++s)
            if (cellBounds (row, s).contains (e.getPosition()))
            {
                const int bar = owner.selectedBar();
                if (! owner.engine.barUsed[bar].load())
                {
                    juce::uint8 zero[drum::numVoices][drum::maxStepsPerBar] = {};
                    owner.engine.setBarPattern (zero, bar);
                    owner.engine.barNames[bar] = "new";
                    owner.rebuildBarHeads();
                }
                auto& cell = owner.engine.pattern[bar][gridRowVoice[row]][s];
                cell.store ((juce::uint8) ((cell.load() + 1) % 4));
                repaint();
                owner.scoreView.repaint();
                return;
            }
}

//==============================================================================
void DrumOverlay::refreshSourceRow()
{
    const bool hasVst = processor.hasDrumPlugin();
    const bool vstOn = engine.useVst.load() && hasVst;

    sourceChip.setButtonText (vstOn ? "SOURCE: VST3" : "SOURCE: INTERNAL");
    sourceChip.getProperties().set ("chipActive", vstOn);
    sourceChip.repaint();

    vstLoadButton.setButtonText (hasVst
        ? processor.getDrumPluginName().substring (0, 16)
        : juce::String (juce::CharPointer_UTF8 ("LOAD VST3\xe2\x80\xa6")));
    vstPanelButton.setVisible (hasVst);
    vstClearButton.setVisible (hasVst);
}
