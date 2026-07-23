#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Overlay do módulo Bateria (fase 18): transporte, biblioteca de grooves por
// gênero (+ viradas + meus compassos), grade de 32 steps editável e partitura
// em pentagrama percussivo desenhada da mesma pattern.
class DrumOverlay : public juce::Component,
                    private juce::Timer
{
public:
    explicit DrumOverlay (GuitarRigNAMProcessor&);
    ~DrumOverlay() override;

    void open();
    void paint (juce::Graphics&) override;
    void resized() override;

    /// RigContent pluga: abrir file chooser do VST de bateria / painel dele.
    std::function<void()> onChooseVst;
    std::function<void()> onOpenVstPanel;

    /// Atualiza o rótulo da fonte de som (chamar após load/clear do VST).
    void refreshSourceRow();

private:
    void timerCallback() override;

    // ---- partitura (desenhada da pattern; clique edita como na grade)
    class ScoreView : public juce::Component
    {
    public:
        explicit ScoreView (DrumOverlay& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        DrumOverlay& owner;
    };

    // ---- grade de steps (editável)
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

    // ---- card de groove/virada na biblioteca
    class GrooveCard : public juce::Component
    {
    public:
        juce::String title, meta;
        bool selected = false, deletable = false;
        std::function<void()> onLoad, onDelete;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    };

    void rebuildGenreChips();
    void rebuildCards();
    void rebuildSectionChips();
    void refreshPatternViews();  // repinta grade/partitura + rótulo do groove
    /// playhead só aparece quando a seção tocando é a seção em edição
    int playheadStep() const
    {
        return engine.uiSection.load() == engine.editSection.load()
                   ? engine.uiStep.load() : -1;
    }
    void loadFactoryGroove (const drum::Groove&);
    void applyFill (const drum::Groove&);
    void loadUserGroove (const juce::File&);
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
    juce::TextButton saveChip { "SALVAR COMPASSO" };

    // song mode: seções A..H com repetições
    juce::TextButton songChip { juce::String (juce::CharPointer_UTF8 ("M\xc3\x9aSICA")) };
    juce::OwnedArray<juce::TextButton> sectionChips;
    juce::TextButton addSectionBtn { juce::CharPointer_UTF8 ("+ SE\xc3\x87\xc3\x83O") };
    juce::TextButton delSectionBtn { juce::CharPointer_UTF8 ("\xe2\x9c\x95") };
    juce::TextButton repeatBtn { juce::CharPointer_UTF8 ("\xc3\x97""1") };
    juce::String sectionGroove[drum::maxSections]; // nome do groove por seção

    juce::OwnedArray<juce::TextButton> genreChips;
    juce::Viewport cardsViewport;
    juce::Component cardsContent;
    juce::OwnedArray<GrooveCard> cards;
    juce::TextEditor saveNameEditor;
    juce::TextButton saveConfirm { "SALVAR" };

    ScoreView scoreView { *this };
    GridView gridView { *this };

    // fonte de som
    juce::TextButton sourceChip;         // INTERNA <-> VST3
    juce::TextButton vstLoadButton { juce::CharPointer_UTF8 ("CARREGAR VST3\xe2\x80\xa6") };
    juce::TextButton vstPanelButton { "PAINEL" };
    juce::TextButton vstClearButton { "REMOVER" };

    juce::String currentGenre { "ROCK" };  // ou "MEUS" / "VIRADAS"
    juce::String currentGrooveName;
    int lastUiStep = -2;
    int lastUiSection = -2;
    int lastNumSections = -1, lastEditSection = -1;
    bool lastHasVst = false;

    static constexpr int gridRows = 9;
    static const int gridRowVoice[gridRows]; // ordem de exibição -> drum::Voice

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumOverlay)
};
