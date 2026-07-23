#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Overlay do módulo Bateria — layout v4 ("a pauta é a track"):
// a área central mostra os 4 compassos da seção em pentagrama corrido;
// grooves de 1 compasso são ARRASTADOS da biblioteca direto para cima do
// compasso na pauta; clique na pauta edita; seções em abas; grade opcional.
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

private:
    void timerCallback() override;

    // ---- a pauta central (4 compassos, alvo de drag & drop, clique edita)
    class ScoreView : public juce::Component,
                      public juce::DragAndDropTarget
    {
    public:
        explicit ScoreView (DrumOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        bool isInterestedInDragSource (const SourceDetails&) override { return true; }
        void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        int barAtX (int x) const; // 0..3 dentro da seção, -1 fora
        DrumOverlay& owner;
        int dragOverBar = -1;
        friend class DrumOverlay;
    };

    // ---- cabeçalho de cada compasso (nº + groove + ✕), acima da pauta
    class BarHead : public juce::Component
    {
    public:
        int barInSec = 0;
        juce::String title;
        bool selected = false, empty = true;
        std::function<void()> onSelect, onClear;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    };

    // ---- grade opcional do compasso selecionado (16 steps)
    class GridView : public juce::Component
    {
    public:
        explicit GridView (DrumOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> cellBounds (int row, int step) const;
        DrumOverlay& owner;
    };

    // ---- card de groove (fonte do arrasto), com miniatura da partitura
    class GrooveCard : public juce::Component
    {
    public:
        juce::String title, meta, dragId;
        bool deletable = false, fill = false;
        juce::uint8 pat[drum::numVoices][drum::stepsPerBar] = {};
        bool hasPat = false;
        std::function<void()> onLoad, onDelete;
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        bool dragging = false;
    };

    void rebuildSectionTabs();
    void rebuildBarHeads();
    void rebuildGenreChips();
    void rebuildCards();
    void refreshAll();

    /// compasso global selecionado (curSection*4 + selBar)
    int selectedBar() const { return curSection * drum::barsPerSection + selBar; }
    /// aplica um groove (por dragId "f:<índice>" ou "u:<arquivo>") num compasso
    void applyGrooveToBar (const juce::String& dragId, int globalBar);
    void saveUserGroove();
    static juce::File userGroovesDir();
    void syncTransportUi();

    GuitarRigNAMProcessor& processor;
    DrumEngine& engine;

    juce::TextButton closeButton { juce::CharPointer_UTF8 ("\xe2\x9c\x95") };
    juce::TextButton playButton;
    juce::TextButton bpmDown { "-" }, bpmUp { "+" };
    juce::Slider swingSlider, levelSlider;
    juce::TextButton clickChip { "CLICK" }, countChip { "CONTAGEM" };
    juce::TextButton followChip { "SEGUIR" }, gridChip { "GRADE" };
    juce::TextButton saveChip { "SALVAR COMPASSO" };

    juce::OwnedArray<juce::TextButton> sectionTabs;
    juce::TextButton addSectionBtn { juce::CharPointer_UTF8 ("+ SE\xc3\x87\xc3\x83O") };
    juce::TextButton delSectionBtn { juce::CharPointer_UTF8 ("\xe2\x9c\x95 remover") };

    juce::OwnedArray<BarHead> barHeads;
    ScoreView scoreView { *this };

    juce::OwnedArray<juce::TextButton> genreChips;
    // sub-filtro: cada gênero mostra seus grooves E viradas
    juce::TextButton kindTudo { "TUDO" }, kindGroove { "GROOVES" }, kindVirada { "VIRADAS" };
    void refreshKindChips();
    juce::Viewport cardsViewport;
    juce::Component cardsContent;
    juce::OwnedArray<GrooveCard> cards;
    juce::TextEditor saveNameEditor;
    juce::TextButton saveConfirm { "SALVAR" };

    GridView gridView { *this };

    juce::TextButton sourceChip;
    juce::TextButton vstLoadButton { juce::CharPointer_UTF8 ("CARREGAR VST3\xe2\x80\xa6") };
    juce::TextButton vstPanelButton { "PAINEL" };
    juce::TextButton vstClearButton { "REMOVER" };

    int curSection = 0;   // seção mostrada
    int selBar = 0;       // compasso selecionado dentro da seção (0..3)
    bool followOn = true;
    bool gridOn = false;
    juce::String currentGenre { "ROCK" };
    int currentKind = 0;   // 0 tudo · 1 grooves · 2 viradas

    int lastUiBar = -2;
    bool lastHasVst = false;
    int lastNumSections = -1;

    static constexpr int gridRows = 9;
    static const int gridRowVoice[gridRows];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumOverlay)
};
