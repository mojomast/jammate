#pragma once

#include "PluginProcessor.h"
#include "DrumGenerator.h"

#include <juce_gui_basics/juce_gui_basics.h>

class KnobComponent;   // ribbon da guitarra (topo da bateria) — def. em PluginEditor.h

//==============================================================================
// Faixa fina da bateria mostrada no TOPO da tela da guitarra (fase 20): play/
// pausa + BPM + os 4 compassos da seção que toca em mini-pentagrama com o
// playhead andando, para o guitarrista acompanhar. Clicar (fora do play) abre
// o módulo Bateria. Reusa o desenho meter-aware da pauta.
class DrumRibbon : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    explicit DrumRibbon (DrumEngine&);
    std::function<void()> onOpen;    // clique (fora do play) -> abre a bateria
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    DrumEngine& engine;
    juce::TextButton playBtn;
    int sectionShown = 0;
};

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
    void devOpenGenerator() { if (! genOn) genChip.triggerClick(); }  // dev flag
    void devGenerateAll() { generateAll(); }                          // dev flag

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
        void mouseDrag (const juce::MouseEvent&) override;   // montar: arrasta o compasso

        bool isInterestedInDragSource (const SourceDetails&) override { return true; }
        void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        int barAtX (int x) const; // 0..3 dentro da seção, -1 fora
        DrumOverlay& owner;
        int dragOverBar = -1, downBar = -1;
        friend class DrumOverlay;
    };

    // ---- cabeçalho de cada compasso (nº + groove + ✕), acima da pauta
    class BarHead : public juce::Component
    {
    public:
        int barInSec = 0;
        juce::String title, meterText { "4/4" }, roleText { "Verso" };
        int roleId = 1;              // 1..5 resolvido (p/ cor)
        bool roleAuto = true;        // papel vindo do arco (não explícito)
        bool selected = false, empty = true;
        std::function<void()> onSelect, onClear, onMeter, onRole;
        juce::Rectangle<int> roleRect, meterRect, clearRect;  // zonas de clique
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

    // ---- linha da lista de grooves/viradas (arrastável) — coluna do meio
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

    // ---- painel de preview (coluna da direita): partitura grande + arrasto
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

    // ---- layout meter-aware da pauta (fórmula de compasso por compasso) ----
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
    int resolveRole (int globalBar) const;   // 1..5 (auto vira arco pela posição)

    /// compasso global selecionado (curSection*4 + selBar)
    int selectedBar() const { return curSection * drum::barsPerSection + selBar; }
    /// aplica um groove (por dragId "f:<índice>" ou "u:<arquivo>") num compasso
    void applyGrooveToBar (const juce::String& dragId, int globalBar);
    void saveUserGroove();
    static juce::File userGroovesDir();
    void syncTransportUi();

    // morph: faixa da bateria (topo da guitarra) <-> tela cheia
    float morphT = 1.0f, morphTarget = 1.0f;   // 0 = faixa, 1 = tela cheia
    bool morphing = false;
    void applyMorph();
public:
    void closeAnimated();   // fecha com a animação de morph
private:

    GuitarRigNAMProcessor& processor;
    DrumEngine& engine;

    // ---- ribbon da guitarra no TOPO da bateria (fase 20): amp + pedais ativos
    juce::OwnedArray<KnobComponent> gtrKnobs;   // knobs reais (ligados ao APVTS)
    juce::TextButton gtrOpenBtn { juce::CharPointer_UTF8 ("abrir guitarra \xe2\xa4\xa2") };
    void setupGuitarRibbon();
public:
    std::function<void()> onClose;   // "abrir guitarra" -> volta para a cadeia
private:

    juce::TextButton closeButton { juce::CharPointer_UTF8 ("\xe2\x9c\x95") };
    juce::TextButton playButton;
    juce::TextButton bpmDown { "-" }, bpmUp { "+" };
    juce::Slider swingSlider, levelSlider;
    juce::TextButton clickChip { "CLICK" }, countChip { "CONTAGEM" };
    juce::TextButton followChip { "SEGUIR" }, gridChip { "GRADE" };
    juce::TextButton genChip { "GERAR" };
    juce::TextButton editChip { juce::CharPointer_UTF8 ("EDI\xc3\x87\xc3\x83O") };
    juce::TextButton saveChip { "SALVAR COMPASSO" };

    juce::OwnedArray<juce::TextButton> sectionTabs;
    juce::TextButton addSectionBtn { juce::CharPointer_UTF8 ("+ SE\xc3\x87\xc3\x83O") };
    juce::TextButton delSectionBtn { juce::CharPointer_UTF8 ("\xe2\x9c\x95 remover") };

    juce::OwnedArray<BarHead> barHeads;
    ScoreView scoreView { *this };

    // ---- navegador em colunas: Gênero | Grooves/Viradas | Preview
    juce::Viewport genreVp;
    juce::Component genreContent;
    juce::OwnedArray<juce::TextButton> genreRows;
    juce::TextButton tabGrooves { "GROOVES" }, tabViradas { "VIRADAS" };
    juce::Viewport listVp;
    juce::Component listContent;
    juce::OwnedArray<LibRow> libRows;
    PreviewPane previewPane { *this };
    juce::TextButton applyBtn;
    juce::Slider humVelSlider, humTimeSlider, humRRSlider;   // humanização
    juce::TextEditor saveNameEditor;
    juce::TextButton saveConfirm { "SALVAR" };
    // groove selecionado no preview
    juce::String selName, selDragId;
    int selBpm = 0;
    int selNum = 4, selDen = 4;   // métrica do groove selecionado
    bool selFill = false, selValid = false;
    juce::uint8 selPat[drum::numVoices][drum::maxStepsPerBar] = {};
    void rebuildGenreCol();
    void rebuildList();
    void selectEntry (const juce::String& dragId, const juce::String& name, bool fill);
    void updatePreview();

    GridView gridView { *this };

    // ---- Gerador (fase 19): mesma zona do navegador/grade -------------------
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
    juce::TextButton vstLoadButton { juce::CharPointer_UTF8 ("CARREGAR VST3\xe2\x80\xa6") };
    juce::TextButton vstPanelButton { "PAINEL" };
    juce::TextButton vstClearButton { "REMOVER" };

    int curSection = 0;   // seção mostrada
    int selBar = 0;       // compasso selecionado dentro da seção (0..3)
    bool followOn = true;
    bool gridOn = false;
    bool genOn = false;    // painel do gerador na zona do navegador
    bool editMode = true;  // true = editar notas (clique); false = montar (arrasta compasso)
    juce::String currentGenre { "ROCK" };
    int currentKind = 1;   // 1 grooves · 2 viradas (abas da coluna do meio)

    int lastUiBar = -2;
    bool lastHasVst = false;
    int lastNumSections = -1;

    static constexpr int gridRows = 9;
    static const int gridRowVoice[gridRows];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumOverlay)
};
