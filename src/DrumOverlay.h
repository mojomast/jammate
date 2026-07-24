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

private:
    void timerCallback() override;

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

    juce::OwnedArray<BarHead> barHeads;
    ScoreView scoreView { *this };

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
    juce::Slider humVelSlider, humTimeSlider, humRRSlider;   // humanize
    // groove selected in the preview
    juce::String selName, selDragId;
    int selBpm = 0;
    int selNum = 4, selDen = 4;   // time signature of the selected groove
    bool selFill = false, selValid = false;
    juce::uint8 selPat[drum::numVoices][drum::maxStepsPerBar] = {};
    void rebuildGenreCol();
    void rebuildList();
    void selectEntry (const juce::String& dragId, const juce::String& name, bool fill);
    void updatePreview();

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
