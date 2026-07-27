#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"

class GuitarRigNAMProcessor;

//==============================================================================
// SONG / SCENES screen (vNext, per docs/design/pedalforge-vnext-complete.html):
// setlist of presets on the left, one scene card per drum section in the
// middle (each holding a full guitar-rig snapshot), an inspector on the right
// (capture / apply / clear / auto-switch) and a song transport at the bottom.
// The mockup's automation lanes and MIDI actions have no engine backing yet
// and are intentionally left out.
class SongOverlay : public juce::Component,
                    private juce::Timer
{
public:
    explicit SongOverlay (GuitarRigNAMProcessor&);
    ~SongOverlay() override;

    void open();
    std::function<void()> onSaveSong;   // wired to the editor's preset save
    std::function<void()> onAddSong;    // wired to the editor's "new preset" flow
                                        // (the button hides while unset)

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildSetlist();
    void rebuildScenes();     // scene cards + inspector state
    void refreshInspector();
    void ensureSceneVisible();   // scrolls the strip so the selected card shows
    juce::String sectionName (int sec) const;   // dominant role or "Section A"

    GuitarRigNAMProcessor& processor;

    // one card per drum section (custom-drawn; the strip content handles the
    // click - bounds are relative to that content, not to the overlay)
    struct SceneCard
    {
        juce::Rectangle<int> bounds;
        int section = 0;
    };
    std::vector<SceneCard> sceneCards;
    int selSection = 0;

    // setlist item (preset file); bounds are relative to the setlist content
    struct SetlistItem
    {
        juce::Rectangle<int> bounds;
        juce::File file;
        juce::String name;
        int scenes = 0;
        int bpm = 0;        // scanned from the preset XML (drumBpm attribute)
        bool current = false;
    };
    std::vector<SetlistItem> setlist;

    // Scrollable contents: a long setlist scrolls vertically and a wide scene
    // strip scrolls horizontally, so neither can spill over the inspector or
    // off-screen. Both are laid out by SongOverlay::resized().
    struct SetlistContent : public juce::Component
    {
        explicit SetlistContent (SongOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        SongOverlay& owner;
    };

    struct SceneStripContent : public juce::Component
    {
        explicit SceneStripContent (SongOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        SongOverlay& owner;
    };

    juce::Viewport setlistVp;
    SetlistContent setlistContent { *this };
    juce::Viewport sceneVp;
    SceneStripContent sceneContent { *this };

    juce::TextEditor nameEditor;        // editable scene name (inspector)
    juce::TextButton addSongBtn { juce::CharPointer_UTF8 ("\xef\xbc\x8b ADD SONG") };
    juce::TextButton closeBtn { juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")) };
    juce::TextButton saveSongBtn { "SAVE SONG" };
    juce::TextButton captureBtn { "CAPTURE CURRENT RIG" };
    juce::TextButton applyBtn { "APPLY NOW" };
    juce::TextButton clearBtn { "CLEAR SNAPSHOT" };
    juce::TextButton autoChip { "AUTO-SWITCH" };
    juce::TextButton addSceneBtn { juce::CharPointer_UTF8 ("\xef\xbc\x8b ADD SCENE") };
    juce::TextButton playBtn { juce::CharPointer_UTF8 ("\xe2\x96\xb6 PLAY SONG") };
    juce::TextButton stopBtn { juce::CharPointer_UTF8 ("\xe2\x96\xa0 STOP") };
    juce::TextButton countChip { "COUNT-IN" };

    // cached per-section summaries (parsing the scene XML is not free)
    juce::String sceneSummaryCache[8];
    void refreshSummaries();

    // live transport readouts (repainted by the timer)
    juce::Rectangle<int> transportInfoArea;
    int lastUiBar = -2, lastUiStep = -2;
    bool lastPlaying = false;

    // "SAVED" tag state - isPresetDirty() hashes the whole state, so poll slowly
    bool presetDirtyCached = false;
    int dirtyPollTick = 0;
    void refreshDirtyFlag();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongOverlay)
};
