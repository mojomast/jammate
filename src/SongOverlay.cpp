#include "SongOverlay.h"

#include "PluginProcessor.h"

namespace
{
juce::String songRoleLabel (int r)
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

constexpr int kTopH = 60, kTransportH = 56, kSetlistW = 220, kInspectorW = 300;
constexpr int kCardH = 250;
} // namespace

//==============================================================================
SongOverlay::SongOverlay (GuitarRigNAMProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);

    closeBtn.getProperties().set ("ghost", true);
    closeBtn.setMouseClickGrabsKeyboardFocus (false);
    closeBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeBtn);

    saveSongBtn.getProperties().set ("chip", true);
    saveSongBtn.setTooltip ("Saves the song (preset) - scenes are stored inside it");
    saveSongBtn.setMouseClickGrabsKeyboardFocus (false);
    saveSongBtn.onClick = [this]
    {
        if (onSaveSong != nullptr)
            onSaveSong();
        rebuildSetlist();
        refreshDirtyFlag();
        repaint();
    };
    addAndMakeVisible (saveSongBtn);

    // editable scene name (mockup: the scene card's h4 / rigsnap title)
    nameEditor.setFont (ui::uiFont (13.0f, true));
    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff14181d));
    nameEditor.setColour (juce::TextEditor::outlineColourId, ui::border());
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, ui::accent);
    nameEditor.setColour (juce::TextEditor::textColourId, ui::text);
    nameEditor.setTextToShowWhenEmpty (juce::String (juce::CharPointer_UTF8 ("Name this scene\xe2\x80\xa6")),
                                       ui::textFaint);
    nameEditor.onTextChange = [this]
    {
        processor.setSceneName (selSection, nameEditor.getText().trim());
        repaint();   // timeline card mirrors the name live
    };
    nameEditor.onFocusLost = [this]
    { processor.setSceneName (selSection, nameEditor.getText().trim()); };
    nameEditor.onReturnKey = [this]
    {
        processor.setSceneName (selSection, nameEditor.getText().trim());
        grabKeyboardFocus();   // back to the overlay (esc etc.)
        repaint();
    };
    nameEditor.onEscapeKey = [this] { grabKeyboardFocus(); };
    addAndMakeVisible (nameEditor);

    captureBtn.getProperties().set ("chip", true);
    captureBtn.getProperties().set ("chipActive", true);
    captureBtn.setTooltip ("Snapshots the whole guitar rig into this scene");
    captureBtn.setMouseClickGrabsKeyboardFocus (false);
    captureBtn.onClick = [this]
    {
        processor.saveSceneForSection (selSection);
        refreshSummaries();
        refreshInspector();
        repaint();
        // a fresh unnamed scene: invite the player to name it
        if (processor.getSceneName (selSection).trim().isEmpty())
            nameEditor.grabKeyboardFocus();
    };
    addAndMakeVisible (captureBtn);

    applyBtn.getProperties().set ("chip", true);
    applyBtn.setTooltip ("Applies this scene's rig right now");
    applyBtn.setMouseClickGrabsKeyboardFocus (false);
    applyBtn.onClick = [this] { processor.applySceneForSection (selSection); };
    addAndMakeVisible (applyBtn);

    clearBtn.getProperties().set ("chip", true);
    clearBtn.setMouseClickGrabsKeyboardFocus (false);
    clearBtn.onClick = [this]
    {
        processor.clearSceneForSection (selSection);
        refreshSummaries();
        refreshInspector();
        repaint();
    };
    addAndMakeVisible (clearBtn);

    autoChip.getProperties().set ("chip", true);
    autoChip.setTooltip ("Entering a section during playback applies its rig at the bar start");
    autoChip.setMouseClickGrabsKeyboardFocus (false);
    autoChip.onClick = [this]
    {
        processor.scenesOn.store (! processor.scenesOn.load());
        refreshInspector();
        repaint();
    };
    addAndMakeVisible (autoChip);

    addSceneBtn.getProperties().set ("chip", true);
    addSceneBtn.setTooltip ("Adds a drum section (4 bars) = a new scene slot");
    addSceneBtn.setMouseClickGrabsKeyboardFocus (false);
    addSceneBtn.onClick = [this]
    {
        const int n = processor.drumEngine.numSections.load();
        if (n < drum::maxSections)
        {
            processor.drumEngine.numSections.store (n + 1);
            selSection = n;
            rebuildScenes();
            repaint();
        }
    };
    addAndMakeVisible (addSceneBtn);

    playBtn.getProperties().set ("chip", true);
    playBtn.getProperties().set ("chipActive", true);
    playBtn.setMouseClickGrabsKeyboardFocus (false);
    playBtn.onClick = [this] { processor.drumEngine.playing.store (true); };
    addAndMakeVisible (playBtn);

    stopBtn.getProperties().set ("chip", true);
    stopBtn.setMouseClickGrabsKeyboardFocus (false);
    stopBtn.onClick = [this] { processor.drumEngine.playing.store (false); };
    addAndMakeVisible (stopBtn);

    countChip.getProperties().set ("chip", true);
    countChip.setTooltip ("One count-in bar before the song starts");
    countChip.setMouseClickGrabsKeyboardFocus (false);
    countChip.onClick = [this]
    {
        processor.drumEngine.countInOn.store (! processor.drumEngine.countInOn.load());
        countChip.getProperties().set ("chipActive", processor.drumEngine.countInOn.load());
        countChip.repaint();
    };
    addAndMakeVisible (countChip);

    addSongBtn.getProperties().set ("chip", true);
    addSongBtn.setTooltip ("Creates a new song (preset)");
    addSongBtn.setMouseClickGrabsKeyboardFocus (false);
    addSongBtn.onClick = [this]
    {
        if (onAddSong != nullptr)
            onAddSong();
        refreshSummaries();
        rebuildSetlist();
        rebuildScenes();
        refreshDirtyFlag();
        repaint();
    };
    addChildComponent (addSongBtn);   // shown only while onAddSong is wired
}

SongOverlay::~SongOverlay() = default;

void SongOverlay::open()
{
    selSection = juce::jlimit (0, processor.drumEngine.numSections.load() - 1, selSection);
    refreshSummaries();
    refreshDirtyFlag();
    rebuildSetlist();
    rebuildScenes();
    refreshInspector();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    startTimerHz (10);
}

bool SongOverlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return false;
}

void SongOverlay::timerCallback()
{
    if (! isVisible())
    {
        stopTimer();
        return;
    }
    const int bar = processor.drumEngine.uiBar.load();
    const int step = processor.drumEngine.uiStep.load();
    const bool playing = processor.drumEngine.playing.load();
    if (bar != lastUiBar || step / 4 != lastUiStep / 4 || playing != lastPlaying)
    {
        lastUiBar = bar;
        lastUiStep = step;
        lastPlaying = playing;
        repaint();   // playing highlight + transport readout
    }

    // SAVED tag: hashing the whole state is not free, poll at ~1 Hz
    if (++dirtyPollTick >= 10)
    {
        dirtyPollTick = 0;
        const bool wasDirty = presetDirtyCached;
        refreshDirtyFlag();
        if (presetDirtyCached != wasDirty)
            repaint();
    }
}

void SongOverlay::refreshDirtyFlag()
{
    presetDirtyCached = processor.isPresetDirty();
}

void SongOverlay::refreshSummaries()
{
    for (int i = 0; i < drum::maxSections; ++i)
        sceneSummaryCache[i] = processor.sceneSummary (i);
}

void SongOverlay::rebuildSetlist()
{
    setlist.clear();
    const auto current = processor.getCurrentPresetName();
    auto files = processor.getPresetsDirectory().findChildFiles (juce::File::findFiles,
                                                                 false, "*.xml");
    files.sort();
    for (const auto& f : files)
    {
        SetlistItem it;
        it.file = f;
        it.name = f.getFileNameWithoutExtension();
        it.current = it.name == current;
        // cheap scene counter (attribute name scan - parsing every preset XML
        // would drag the open with big hosted-plugin state blobs)
        const auto txt = f.loadFileAsString();
        for (int i = 1; i <= drum::maxSections; ++i)
            if (txt.contains ("sceneRig" + juce::String (i) + "="))
                ++it.scenes;
        const int bp = txt.indexOf ("drumBpm=\"");
        if (bp >= 0)   // bounded substring - preset blobs can be large
            it.bpm = (int) txt.substring (bp + 9, bp + 24).getDoubleValue();
        setlist.push_back (std::move (it));
    }
    resized();
}

void SongOverlay::rebuildScenes()
{
    sceneCards.clear();
    const int nSec = juce::jlimit (1, drum::maxSections,
                                   processor.drumEngine.numSections.load());
    selSection = juce::jlimit (0, nSec - 1, selSection);
    for (int i = 0; i < nSec; ++i)
        sceneCards.push_back ({ {}, i });
    resized();
    refreshInspector();
}

void SongOverlay::refreshInspector()
{
    const bool has = processor.hasScene (selSection);
    applyBtn.setEnabled (has);
    clearBtn.setEnabled (has);
    nameEditor.setText (processor.getSceneName (selSection), false);   // no onTextChange echo
    autoChip.getProperties().set ("chipActive", processor.scenesOn.load());
    autoChip.repaint();
    countChip.getProperties().set ("chipActive", processor.drumEngine.countInOn.load());
    countChip.repaint();
}

juce::String SongOverlay::sectionName (int sec) const
{
    for (int b = sec * drum::barsPerSection;
         b < (sec + 1) * drum::barsPerSection && b < drum::maxBars; ++b)
        if (processor.drumEngine.barRole[b] > 0)
            return songRoleLabel (processor.drumEngine.barRole[b]);
    return "Section " + juce::String::charToString ((juce::juce_wchar) ('A' + sec));
}

juce::String SongOverlay::sectionMeter (int sec) const
{
    for (int b = sec * drum::barsPerSection;
         b < (sec + 1) * drum::barsPerSection && b < drum::maxBars; ++b)
        if (processor.drumEngine.barUsed[b].load())
            return juce::String (processor.drumEngine.meterNum (b)) + "/"
                   + juce::String (processor.drumEngine.meterDen (b));
    return "4/4";
}

void SongOverlay::resized()
{
    const int W = getWidth(), H = getHeight();
    closeBtn.setBounds (W - 46, 14, 32, 32);
    saveSongBtn.setBounds (W - 46 - 8 - 96, 16, 96, 28);

    // setlist items + ADD SONG footer button
    {
        int y = kTopH + 40;
        for (auto& it : setlist)
        {
            it.bounds = { 14, y, kSetlistW - 28, 50 };
            y += 56;
        }
        const bool fits = y + 30 <= H - kTransportH - 6;
        addSongBtn.setVisible (onAddSong != nullptr && fits);
        addSongBtn.setBounds (14, y + 2, kSetlistW - 28, 30);
    }

    // scene cards (equal split with a sensible minimum, like the mockup)
    {
        const int x0 = kSetlistW + 17, x1 = W - kInspectorW - 17;
        const int n = juce::jmax (1, (int) sceneCards.size());
        const int gap = 8;
        const int cw = juce::jmax (125, ((x1 - x0) - gap * (n - 1)) / n);
        int x = x0;
        for (auto& c : sceneCards)
        {
            c.bounds = { x, kTopH + 46, cw, kCardH };
            x += cw + gap;
        }
    }

    // inspector
    {
        const int ix = W - kInspectorW + 14, iw = kInspectorW - 28;
        int y = kTopH + 40;
        y += 14;                                     // "SCENE NAME" label (painted)
        nameEditor.setBounds (ix, y, iw, 28); y += 36;
        y += 118;                                    // info card (painted)
        captureBtn.setBounds (ix + 10, y, iw - 20, 30); y += 36;
        applyBtn.setBounds (ix + 10, y, (iw - 26) / 2, 28);
        clearBtn.setBounds (ix + 10 + (iw - 26) / 2 + 6, y, (iw - 26) / 2, 28); y += 40;
        y += 12;
        autoChip.setBounds (ix, y, iw, 30); y += 38;
        addSceneBtn.setBounds (ix, y, iw, 32);
    }

    // transport
    {
        const int ty = H - kTransportH + 13;
        playBtn.setBounds (20, ty, 110, 30);
        stopBtn.setBounds (136, ty, 74, 30);
        transportInfoArea = { 224, H - kTransportH, 320, kTransportH };
        countChip.setBounds (W - 20 - 96, ty, 96, 30);
    }
}

void SongOverlay::paint (juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();
    g.fillAll (ui::bg);

    // ---- top band
    g.setGradientFill ({ ui::barTop, 0.0f, 0.0f, ui::barBottom, 0.0f, (float) kTopH, false });
    g.fillRect (0, 0, W, kTopH);
    g.setColour (ui::border());
    g.fillRect (0, kTopH - 1, W, 1);

    g.setColour (ui::accent);
    g.fillEllipse (20.0f, kTopH / 2.0f - 4.0f, 8.0f, 8.0f);
    g.setColour (ui::text);
    g.setFont (ui::uiFont (15.0f, true));
    g.drawText ("SONG / SCENES", 38, 0, 150, kTopH, juce::Justification::centredLeft);
    const auto titleFont = ui::uiFont (17.0f, true);
    g.setFont (titleFont);
    const auto title = processor.getCurrentPresetName().isNotEmpty()
                           ? processor.getCurrentPresetName()
                           : juce::String ("Untitled song");
    g.drawText (title, 200, 0, W - 420, kTopH, juce::Justification::centredLeft);
    if (processor.getCurrentPresetName().isNotEmpty() && ! presetDirtyCached)
    {
        // mockup: green SAVED tag right after the song title
        const int tw = juce::jmin (W - 420,
                                   juce::GlyphArrangement::getStringWidthInt (titleFont, title));
        auto tag = juce::Rectangle<float> ((float) (200 + tw + 12),
                                           kTopH / 2.0f - 9.0f, 52.0f, 18.0f);
        g.setColour (ui::green.withAlpha (0.12f));
        g.fillRoundedRectangle (tag, 9.0f);
        g.setColour (ui::green.withAlpha (0.5f));
        g.drawRoundedRectangle (tag.reduced (0.5f), 9.0f, 1.0f);
        g.setColour (ui::green);
        g.setFont (ui::monoFont (8.0f, true));
        g.drawText ("SAVED", tag.toNearestInt(), juce::Justification::centred);
    }

    // ---- setlist column
    g.setColour (ui::chainBottom);
    g.fillRect (0, kTopH, kSetlistW, H - kTopH - kTransportH);
    g.setColour (ui::border());
    g.fillRect (kSetlistW - 1, kTopH, 1, H - kTopH - kTransportH);
    g.setColour (ui::textDim);
    g.setFont (ui::monoFont (10.0f, true));
    g.drawText ("SETLIST \xc2\xb7 PRESETS", 14, kTopH + 12, kSetlistW - 28, 14,
                juce::Justification::centredLeft);

    for (const auto& it : setlist)
    {
        if (it.bounds.getBottom() > H - kTransportH - 6)
            continue;   // simple clip; long lists scroll in a later pass
        auto b = it.bounds.toFloat();
        g.setColour (it.current ? ui::accent.withAlpha (0.10f) : ui::cardBottom);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (it.current ? ui::accent : ui::border());
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (ui::text);
        g.setFont (ui::uiFont (12.0f, true));
        g.drawText (it.name, it.bounds.getX() + 10, it.bounds.getY() + 8,
                    it.bounds.getWidth() - 20, 15, juce::Justification::centredLeft);
        g.setColour (ui::textDim);
        g.setFont (ui::monoFont (9.0f));
        auto meta = juce::String (it.scenes) + " scenes";
        if (it.bpm > 0)
            meta += juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (it.bpm) + " BPM";
        g.drawText (meta, it.bounds.getX() + 10, it.bounds.getY() + 27,
                    it.bounds.getWidth() - 20, 12, juce::Justification::centredLeft);
    }

    // ---- inspector column
    const int ix = W - kInspectorW;
    g.setColour (ui::chainBottom);
    g.fillRect (ix, kTopH, kInspectorW, H - kTopH - kTransportH);
    g.setColour (ui::border());
    g.fillRect (ix, kTopH, 1, H - kTopH - kTransportH);

    g.setColour (ui::textDim);
    g.setFont (ui::monoFont (10.0f, true));
    g.drawText ("SCENE 0" + juce::String (selSection + 1) + " \xc2\xb7 "
                    + sectionName (selSection).toUpperCase(),
                ix + 14, kTopH + 12, kInspectorW - 28, 14, juce::Justification::centredLeft);

    // scene name editor label (the TextEditor itself sits right below)
    g.setColour (ui::textDim);
    g.setFont (ui::monoFont (8.0f, true));
    g.drawText ("SCENE NAME", ix + 14, kTopH + 40, kInspectorW - 28, 11,
                juce::Justification::centredLeft);

    {
        auto card = juce::Rectangle<float> ((float) ix + 14, (float) kTopH + 90,
                                            (float) kInspectorW - 28, 110.0f);
        g.setColour (ui::cardBottom);
        g.fillRoundedRectangle (card, 7.0f);
        g.setColour (ui::border());
        g.drawRoundedRectangle (card.reduced (0.5f), 7.0f, 1.0f);

        const int cx = ix + 24;
        int y = kTopH + 100;
        auto label = [&] (const char* t)
        {
            g.setColour (ui::textDim);
            g.setFont (ui::monoFont (8.0f, true));
            g.drawText (t, cx, y, kInspectorW - 48, 11, juce::Justification::centredLeft);
            y += 12;
        };
        auto value = [&] (const juce::String& t)
        {
            g.setColour (ui::text);
            g.setFont (ui::uiFont (11.0f));
            g.drawText (t, cx, y, kInspectorW - 48, 14, juce::Justification::centredLeft);
            y += 20;
        };
        label ("RIG SNAPSHOT");
        value (sceneSummaryCache[selSection].isNotEmpty()
                   ? sceneSummaryCache[selSection]
                   : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 not captured yet")));
        label ("DRUM SECTION");
        value ("Section " + juce::String::charToString ((juce::juce_wchar) ('A' + selSection))
               + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 bars "))
               + juce::String (selSection * 4 + 1) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
               + juce::String (selSection * 4 + 4));
        label ("ON SCENE ENTER");
        value (juce::String (juce::CharPointer_UTF8 ("At bar start \xc2\xb7 30 ms fade")));
    }

    // ---- arrangement header
    g.setColour (ui::textDim);
    g.setFont (ui::monoFont (10.0f, true));
    g.drawText ("ARRANGEMENT \xc2\xb7 AUTO-SWITCH RIG SNAPSHOTS",
                kSetlistW + 17, kTopH + 14, 360, 14, juce::Justification::centredLeft);
    if (processor.scenesOn.load())
    {
        g.setColour (ui::accent);
        g.setFont (ui::monoFont (9.0f, true));
        g.drawText ("AUTO-SWITCH ON", ix - 17 - 120, kTopH + 14, 120, 14,
                    juce::Justification::centredRight);
    }

    // ---- scene cards
    const int playSec = processor.drumEngine.uiBar.load() >= 0
                            ? processor.drumEngine.uiBar.load() / drum::barsPerSection : -1;
    for (const auto& c : sceneCards)
    {
        auto b = c.bounds.toFloat();
        const bool on = c.section == selSection;
        g.setColour (ui::cardBottom);
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (on ? ui::accent : (c.section == playSec ? ui::glowOrange : ui::border()));
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, on ? 1.5f : 1.0f);
        if (on)
        {
            g.setColour (ui::accent);
            g.fillRoundedRectangle (b.getX() + 1, b.getY() + 1, b.getWidth() - 2, 3.0f, 1.5f);
        }

        const int x = c.bounds.getX() + 12;
        g.setColour (ui::textDim);
        g.setFont (ui::monoFont (8.0f, true));
        g.drawText ("0" + juce::String (c.section + 1) + " \xc2\xb7 BARS "
                        + juce::String (c.section * 4 + 1)
                        + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
                        + juce::String (c.section * 4 + 4),
                    x, c.bounds.getY() + 12, c.bounds.getWidth() - 24, 11,
                    juce::Justification::centredLeft);
        // h4 = scene name when set (mockup "VERSE"/"WIDE RHYTHM"); the section's
        // paper name (Verse/Chorus) then drops to a small subtitle underneath
        const auto sceneNm = processor.getSceneName (c.section).trim();
        const bool named = sceneNm.isNotEmpty();
        g.setColour (ui::text);
        g.setFont (ui::uiFont (14.0f, true));
        g.drawText (named ? sceneNm.toUpperCase() : sectionName (c.section).toUpperCase(),
                    x, c.bounds.getY() + 28,
                    c.bounds.getWidth() - 24, 18, juce::Justification::centredLeft);
        if (named)
        {
            g.setColour (ui::textDim);
            g.setFont (ui::uiFont (9.5f));
            g.drawText (sectionName (c.section), x, c.bounds.getY() + 47,
                        c.bounds.getWidth() - 24, 12, juce::Justification::centredLeft);
        }
        const int meterY = c.bounds.getY() + (named ? 62 : 52);
        g.setColour (ui::text);
        g.setFont (ui::monoFont (24.0f, true));
        g.drawText (sectionMeter (c.section), x, meterY,
                    c.bounds.getWidth() - 24, 30, juce::Justification::centredLeft);
        if (c.section == playSec)
        {
            g.setColour (ui::glowOrange);
            g.setFont (ui::monoFont (8.0f, true));
            g.drawText ("PLAYING", x, meterY + 36, c.bounds.getWidth() - 24, 11,
                        juce::Justification::centredLeft);
        }

        // rig snapshot box at the bottom of the card
        auto snap = juce::Rectangle<float> (b.getX() + 10, b.getBottom() - 62,
                                            b.getWidth() - 20, 52.0f);
        g.setColour (ui::chainBottom);
        g.fillRoundedRectangle (snap, 5.0f);
        g.setColour (ui::border());
        g.drawRoundedRectangle (snap.reduced (0.5f), 5.0f, 1.0f);
        const auto& sum = sceneSummaryCache[c.section];
        g.setFont (ui::uiFont (10.0f, true));
        g.setColour (sum.isNotEmpty() ? ui::text : ui::textFaint);
        // mockup rigsnap: <b> is the scene name when it has one ("RHYTHM")
        g.drawText (sum.isNotEmpty() ? (named ? sceneNm.toUpperCase()
                                              : juce::String ("RIG SNAPSHOT"))
                                     : juce::String ("NO SNAPSHOT"),
                    (int) snap.getX() + 8, (int) snap.getY() + 8,
                    (int) snap.getWidth() - 16, 12, juce::Justification::centredLeft);
        g.setFont (ui::monoFont (8.0f));
        g.setColour (sum.isNotEmpty() ? ui::accentBright : ui::textDim);
        g.drawText (sum.isNotEmpty() ? sum
                                     : juce::String ("select and CAPTURE CURRENT RIG"),
                    (int) snap.getX() + 8, (int) snap.getY() + 26,
                    (int) snap.getWidth() - 16, 12, juce::Justification::centredLeft);
    }

    // ---- transport
    g.setGradientFill ({ ui::barTop, 0.0f, (float) (H - kTransportH),
                         ui::barBottom, 0.0f, (float) H, false });
    g.fillRect (0, H - kTransportH, W, kTransportH);
    g.setColour (ui::border());
    g.fillRect (0, H - kTransportH, W, 1);

    {
        const int bar = processor.drumEngine.uiBar.load();
        const int step = processor.drumEngine.uiStep.load();
        g.setColour (ui::text);
        g.setFont (ui::monoFont (18.0f, true));
        g.drawText (juce::String ((int) processor.drumEngine.bpm.load()),
                    transportInfoArea.getX(), transportInfoArea.getY(),
                    54, transportInfoArea.getHeight(), juce::Justification::centred);
        g.setColour (ui::textDim);
        g.setFont (ui::monoFont (8.0f, true));
        g.drawText ("BPM", transportInfoArea.getX() + 56, transportInfoArea.getY(),
                    30, transportInfoArea.getHeight(), juce::Justification::centredLeft);
        g.setColour (bar >= 0 ? ui::accent : ui::textFaint);
        g.setFont (ui::monoFont (11.0f, true));
        g.drawText (bar >= 0 ? "BAR " + juce::String (bar + 1) + " \xc2\xb7 BEAT "
                                   + juce::String (step / 4 + 1)
                             : juce::String ("stopped"),
                    transportInfoArea.getX() + 96, transportInfoArea.getY(),
                    200, transportInfoArea.getHeight(), juce::Justification::centredLeft);
    }
}

void SongOverlay::mouseDown (const juce::MouseEvent& e)
{
    for (const auto& c : sceneCards)
        if (c.bounds.contains (e.getPosition()))
        {
            selSection = c.section;
            refreshInspector();
            repaint();
            return;
        }

    for (const auto& it : setlist)
        if (it.bounds.contains (e.getPosition()))
        {
            processor.loadPreset (it.file);
            selSection = 0;
            refreshSummaries();
            refreshDirtyFlag();
            rebuildSetlist();
            rebuildScenes();
            repaint();
            return;
        }
}
