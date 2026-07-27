#pragma once

#include "PluginProcessor.h"
#include "DrumGenerator.h"

#include <juce_gui_basics/juce_gui_basics.h>

class KnobComponent;   // guitar ribbon (top of the drums) - defined in PluginEditor.h

//==============================================================================
// Thin drum strip shown at the TOP of the guitar screen (phase 20): play/
// pause + BPM + the 4 bars of the playing section in a mini-staff with the
// playhead moving, for the guitarist to follow along. Clicking (outside play)
// opens the Drums module. Reuses the meter-aware staff drawing.
class DrumRibbon : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    explicit DrumRibbon (DrumEngine&);
    std::function<void()> onOpen;    // click (outside play) -> opens the drums
    // clean UI: the ribbon collapses to a minimal strip (chevron toggle)
    std::function<void (bool)> onToggleMin;
    void setMinimal (bool);
    bool isMinimal() const { return minimal; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    DrumEngine& engine;
    juce::TextButton playBtn;
    juce::TextButton chevBtn;        // collapse/expand
    bool minimal = false;
    int sectionShown = 0;
};

//==============================================================================
// Drums module overlay - layout v4 ("the staff is the track"):
// the central area shows the section's 4 bars on a continuous staff;
// 1-bar grooves are DRAGGED from the library straight onto the bar on the
// staff; clicking the staff edits; sections as tabs; optional grid.
class DrumOverlay : public juce::Component,
                    public juce::DragAndDropContainer,
                    private juce::Timer
{
public:
    explicit DrumOverlay (GuitarRigNAMProcessor&);
    ~DrumOverlay() override;

    void open();
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onChooseVst;
    std::function<void()> onOpenVstPanel;
    void refreshSourceRow();
    void devOpenGenerator() { if (! genOn) genChip.triggerClick(); }  // dev flag
    void devOpenGrid() { if (! gridOn) gridChip.triggerClick(); }      // dev flag
    void devGenerateAll() { generateAll(); }                          // dev flag

public:
    std::function<void()> onOpenSongMap;   // SONG MAP button (wired by the shell)

private:
    void timerCallback() override;

    // ---- notation editor (vNext D1): toolbar tool + palette voice + selection
    enum class ScoreTool { select = 0, quarter, eighth, sixteenth, rest, accent, ghost };
    ScoreTool scoreTool = ScoreTool::select;
    int activeVoice = -1;    // palette voice used by the note tools; -1 = staff Y map
    int noteSelBar = -1, noteSelVoice = -1, noteSelStep = -1;  // selected note (global bar)
    bool hasNoteSelection() const;
    void selectNote (int globalBar, int voice, int step);
    void clearNoteSelection();
    void noteInspectorAction (int action);   // 0 duplicate · 1 delete · 2 nudge L · 3 nudge R
    void setScoreTool (ScoreTool);
    int stepSnapForTool() const;             // 4 (quarter) / 2 (eighth) / 1 otherwise

    // ---- the central staff (4 bars, drag & drop target, click edits)
    class ScoreView : public juce::Component,
                      public juce::DragAndDropTarget
    {
    public:
        explicit ScoreView (DrumOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;   // assemble: drags the bar

        bool isInterestedInDragSource (const SourceDetails&) override { return true; }
        void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        int barAtX (int x) const; // 0..3 within the section, -1 outside
        DrumOverlay& owner;
        int dragOverBar = -1, downBar = -1;
        friend class DrumOverlay;
    };

    // ---- per-bar options overlaid ON the staff, aligned with their bar
    // (role pill + number/name + save + clear; the time signature is engraved
    // on the staff itself and edited by clicking it there)
    class BarHead : public juce::Component
    {
    public:
        int barInSec = 0;
        juce::String title, meterText { "4/4" }, roleText { "Verse" };
        int roleId = 1;              // 1..5 resolved (for color)
        bool roleAuto = true;        // role coming from the arc (not explicit)
        bool selected = false, empty = true;
        std::function<void()> onSelect, onClear, onMeter, onRole, onSave;
        juce::Rectangle<int> roleRect, meterRect, clearRect, saveRect;  // click zones
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    };

    // ---- optional grid for the selected bar (16 steps)
    class GridView : public juce::Component
    {
    public:
        explicit GridView (DrumOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> cellBounds (int row, int step) const;
        int cellWidth() const;
        int stepX (int step) const;
        int rowH() const;        // adapts to the panel height (no clipped rows)
        DrumOverlay& owner;
    };

    // ---- left column of the notation body: the 9 drum voices (active voice)
    class VoicePalette : public juce::Component
    {
    public:
        explicit VoicePalette (DrumOverlay& o) : owner (o)
        { setRepaintsOnMouseActivity (true); }
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        int rowAt (int y) const;
        DrumOverlay& owner;
    };

    // ---- right column: data + actions of the note selected on the staff
    class NoteInspector : public juce::Component
    {
    public:
        explicit NoteInspector (DrumOverlay& o) : owner (o)
        { setRepaintsOnMouseActivity (true); }
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        juce::Rectangle<int> actionRects[4];
        DrumOverlay& owner;
    };

    // ---- KIT MIXER (vNext D2): 9 piece strips (meter + LEVEL knob) plus the
    // drum MIX meter. B4-UI: with the INTERNAL sampler the per-piece meters
    // show the REAL audio peak of each piece (engine.uiVoicePeak, dBFS, our own
    // ballistics). With a hosted drum VST3 there is no per-piece audio at all,
    // so they fall back to the MIDI trigger velocity (engine.uiVoiceFlash) and
    // are drawn as an amber segmented ladder + labelled as triggers, so nobody
    // reads them as a level. The MIX meter is real audio in BOTH modes.
    class KitMixerView : public juce::Component
    {
    public:
        explicit KitMixerView (DrumOverlay& o);
        void paint (juce::Graphics&) override;
        void resized() override;
        void syncKnobs();      // knob positions <- engine.voiceGain
        void tickMeters();     // timer tick: drain the engine + ballistics (repaints)
        void drainMeters();    // tab opened: drop the stale max-hold peaks
    private:
        static constexpr float floorDb = -60.0f;   // bottom of the meter scale
        static constexpr int bannerH = 15;         // "what these meters mean" row
        /// strip i: 0..numVoices-1 = pieces, numVoices = the MIX column
        juce::Rectangle<int> stripRect (int i) const;

        juce::Slider knobs[drum::numVoices];
        float flash[drum::numVoices] = {};    // VST mode: trigger velocity 0..1
        float db[drum::numVoices] = {};       // internal: audio peak, dBFS
        float holdDb[drum::numVoices] = {};   // peak-hold marker, dBFS
        int holdCnt[drum::numVoices] = {};
        float mixDb = floorDb, mixHoldDb = floorDb;
        int mixHoldCnt = 0;
        bool fromVst = false;                 // engine.uiMixFromVst, last tick
        DrumOverlay& owner;
    };

    // ---- grooves/fills list row (draggable) - middle column
    class LibRow : public juce::Component
    {
    public:
        juce::String name, dragId;
        bool fill = false, selected = false, deletable = false;
        std::function<void()> onSelect, onDelete;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        bool dragging = false;
    };

    // ---- preview pane (right column): large score + drag
    class PreviewPane : public juce::Component
    {
    public:
        explicit PreviewPane (DrumOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { dragging = false; }
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        DrumOverlay& owner;
        bool dragging = false;
    };

    void rebuildSectionTabs();
    void rebuildBarHeads();
    void refreshAll();

    // ---- section tab row (B2) -------------------------------------------
    // The row holds the section tabs plus a fixed-width cluster
    // (+ SECTION | remove | RIG | SONG MAP) and, painted flush right, the
    // SCENE tag. With 6+ sections the tabs alone are wider than the row, so
    // the tabs have to give way - never the cluster. This struct is the ONLY
    // source of truth for the row geometry so rebuildSectionTabs() (labels)
    // and resized() (positions) can never disagree about the same nSec.
    struct TabRowLayout
    {
        int pitch = 124;        // x advance between two tabs
        int tabW = 120;         // tab width (pitch minus the 4 px gap)
        int clusterX = 0;       // x of "+ SECTION"
        bool compact = false;   // short tab labels ("A · 1-4")
    };
    TabRowLayout tabRowLayout (int nSec) const;
    juce::String sectionTabText (int index, bool compact) const;

    // ---- meter-aware staff layout (time signature per bar) ----
    struct BarLayout
    {
        float notesX = 0, tsX = -1, width = 0;
        int steps = 16, num = 4, den = 4, nGroups = 4;
        int groups[8] = { 4, 4, 4, 4, 0, 0, 0, 0 };
        bool showTS = true;
    };
    BarLayout barLay[drum::barsPerSection];
    float curStepW = 13.2f, curBeatPad = 5.0f, curBarPad = 20.0f, scoreTotalW = 0;
    void computeBarLayout (int availW);
    float stepXInBar (int b, int s) const;
    int groupIndexInBar (int b, int s) const;
    int barAtXlocal (int x) const;
    void openMeterMenu (int barInSec, juce::Component* anchor);
    void openRoleMenu (int barInSec, juce::Component* anchor);
    int resolveRole (int globalBar) const;   // 1..5 (auto becomes an arc by position)

    /// selected global bar (curSection*4 + selBar)
    int selectedBar() const { return curSection * drum::barsPerSection + selBar; }
    /// applies a groove (by dragId "f:<index>" or "u:<file>") to a bar
    void applyGrooveToBar (const juce::String& dragId, int globalBar);
    void saveUserGroove (const juce::String& name);
    void promptSaveBar();   // name popup -> saveUserGroove (bar overlay + chip)
    static juce::File userGroovesDir();
    void syncTransportUi();

    // morph: drum strip (top of the guitar) <-> full screen
    float morphT = 1.0f, morphTarget = 1.0f;   // 0 = strip, 1 = full screen
    bool morphing = false;
    void applyMorph();
public:
    void closeAnimated();   // closes with the morph animation
    int ribbonSourceH = 54; // current DrumRibbon height (26 when collapsed)
private:

    GuitarRigNAMProcessor& processor;
    DrumEngine& engine;

    // ---- guitar ribbon at the TOP of the drums: a miniature of the real chain
    juce::OwnedArray<KnobComponent> gtrKnobs;   // real knobs (wired to the APVTS)
    struct GtrGroup
    {
        juce::String name;
        juce::String onParam;
        int first = 0, last = -1, x = 0, w = 0;
        int rigLane = -1;
    };
    std::vector<GtrGroup> gtrGroups;            // one per active effect, in chain order
    juce::TextButton gtrOpenBtn { juce::CharPointer_UTF8 ("open guitar \xe2\xa4\xa2") };
    void setupGuitarRibbon();
    void buildGuitarRibbon();                   // (re)build from getChainOrder()
    // B1: a Scene switch rewrites the guitar chain (rig count and/or effect
    // order) behind our back and the processor has no callback for it, so the
    // timer watches this baseline and rebuilds the ribbon when it drifts
    // (1 tick of latency). gtrRibDirty defers the rebuild while a mouse button
    // is down - the rebuild DELETES the knobs.
    void refreshGuitarRibbon();
    int gtrRibRigCount = -1;
    juce::String gtrRibOrder;
    bool gtrRibDirty = false;
public:
    std::function<void()> onClose;   // "open guitar" -> back to the chain
private:

    juce::TextButton closeButton { juce::CharPointer_UTF8 ("\xe2\x9c\x95") };
    juce::TextButton playButton;
    juce::TextButton bpmDown { "-" }, bpmUp { "+" };
    juce::TextButton syncChip { "SYNC" };   // vNext: follow the DAW BPM (VST3)
    juce::Slider swingSlider, levelSlider;
    juce::TextButton clickChip { "CLICK" }, countChip { "COUNT-IN" };  // legacy (menu now)
    juce::TextButton metroChip { juce::CharPointer_UTF8 ("METRO \xe2\x96\xbe") };
    juce::TextButton followChip { "FOLLOW" }, gridChip { "GRID" };
    juce::TextButton genChip { "GENERATE" };
    juce::TextButton editChip { "EDIT" };
    juce::TextButton saveChip { "SAVE BAR" };
    juce::TextButton copyChip { "COPY BAR" };   // vNext D3: copy bar N to...
    // clean UI: humanize sliders live in a small popover panel; the drum sound
    // source row collapses into a single kit chip with a menu
    juce::TextButton humChip { juce::CharPointer_UTF8 ("HUMANIZE \xe2\x96\xbe") };
    std::unique_ptr<juce::Component> humPanel;
    juce::TextButton kitChip;
    juce::TextButton helpChip { "?" };

    juce::OwnedArray<juce::TextButton> sectionTabs;
    juce::TextButton addSectionBtn { "+ SECTION" };
    // vNext F6 - Song/Scenes: rig snapshot of the SHOWN section (save/apply/
    // clear + the auto-switch toggle live in the chip's menu)
    juce::TextButton rigChip { juce::CharPointer_UTF8 ("RIG \xe2\x96\xbe") };
    juce::TextButton delSectionBtn { juce::CharPointer_UTF8 ("\xe2\x9c\x95 remove") };
    juce::TextButton songMapBtn { "SONG MAP" };   // vNext D3: opens the SongOverlay

    juce::OwnedArray<BarHead> barHeads;
    ScoreView scoreView { *this };

    // notation editor chrome: toolbar tools + flanking columns
    juce::TextButton toolBtn[7];              // select · quarter/eighth/sixteenth · rest · accent · ghost
    VoicePalette voicePalette { *this };
    NoteInspector noteInspector { *this };

    // bottom panel tabs: LIBRARY | KIT MIXER (GRID/GENERATOR stay in the footer)
    juce::TextButton libTabBtn { "LIBRARY" }, mixTabBtn { "KIT MIXER" };
    KitMixerView kitMixer { *this };
    bool mixerOn = false;

    // ---- column browser: Genre | Grooves/Fills | Preview
    juce::Viewport genreVp;
    juce::Component genreContent;
    juce::OwnedArray<juce::TextButton> genreRows;
    juce::TextButton tabGrooves { "GROOVES" }, tabViradas { "FILLS" };
    juce::Viewport listVp;
    juce::Component listContent;
    juce::OwnedArray<LibRow> libRows;
    PreviewPane previewPane { *this };
    juce::TextButton applyBtn;
    juce::TextButton auditionBtn;    // vNext D3: loop the groove (timeline paused)
    juce::TextButton favBtn;         // vNext D3: favorite star
    juce::Slider humVelSlider, humTimeSlider, humRRSlider;   // humanize
    // groove selected in the preview
    juce::String selName, selDragId, selGenre;
    int selBpm = 0;
    int selNum = 4, selDen = 4;   // time signature of the selected groove
    bool selFill = false, selValid = false;
    juce::uint8 selPat[drum::numVoices][drum::maxStepsPerBar] = {};
    void rebuildGenreCol();
    void rebuildList();
    void selectEntry (const juce::String& dragId, const juce::String& name, bool fill);
    void updatePreview();
    void updatePreviewActionVis();
    void updateHumChipText();     // "HUMANIZE n%" (mean of the 3 sliders)

    // ---- vNext D3: copy bar / audition / favorites / recents ---------------
    void openCopyBarMenu();
    void toggleAudition();
    void stopAudition();
    void updateAuditionPattern();  // selPat -> engine audition atomics
    struct RecentEntry { juce::String dragId, name; bool fill = false; };
    std::vector<RecentEntry> recents;   // session only, newest first (max 8)
    void pushRecent (const juce::String& dragId, const juce::String& name, bool fill);
    struct FavEntry { juce::String name, genre; };
    std::vector<FavEntry> favs;         // persisted in groove-favs.json
    bool isFavourite (const juce::String& name, const juce::String& genre) const;
    void toggleFavourite();
    void loadFavs();
    void saveFavs() const;
    static juce::File favsFile();

    GridView gridView { *this };

    // ---- Generator (phase 19): same area as the browser/grid -------------------
    juce::ComboBox genGenreBox, genStyleBox, genDrummerBox;
    juce::Slider genComplex, genDynamics, genHuman, genFill, genSwing;
    juce::TextButton genOneBtn, genAllBtn;
    void setupGenerator();
    void rebuildGenStyles();
    void rebuildGenDrummers();
    void fillBarWithGen (int globalBar, const juce::String& role, juce::uint32 seed);
    void generateOne();
    void generateAll();
    juce::uint32 genSeedCtr = 1;

    juce::TextButton sourceChip;
    juce::TextButton vstLoadButton { juce::CharPointer_UTF8 ("LOAD VST3\xe2\x80\xa6") };
    juce::TextButton vstPanelButton { "PANEL" };
    juce::TextButton vstClearButton { "REMOVE" };

    int curSection = 0;   // section shown
    int selBar = 0;       // selected bar within the section (0..3)
    bool followOn = true;
    bool gridOn = false;
    bool genOn = false;    // generator panel in the browser area
    bool editMode = true;  // true = edit notes (click); false = assemble (drag bar)
    juce::String currentGenre { "ROCK" };
    int currentKind = 1;   // 1 grooves, 2 fills (middle-column tabs)

    int lastUiBar = -2;
    bool lastHasVst = false;
    int lastNumSections = -1;

    static constexpr int gridRows = 9;
    static const int gridRowVoice[gridRows];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumOverlay)
};
