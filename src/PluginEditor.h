#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "StoreOverlay.h"

//==============================================================================
// Slider rotativo com "trava" no valor default (snap quando o arrasto passa
// perto dele, como um detent físico).
class SnapSlider : public juce::Slider
{
public:
    double snapTarget = 0.0, snapRadius = 0.0;

    double snapValue (double attemptedValue, DragMode dragMode) override
    {
        if (dragMode != notDragging && snapRadius > 0.0
            && std::abs (attemptedValue - snapTarget) < snapRadius)
            return snapTarget;
        return attemptedValue;
    }
};

//==============================================================================
// Knob + label + valor, conforme Knob.dc.html v2 (gauge accent).
class KnobComponent : public juce::Component
{
public:
    KnobComponent (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                   const juce::String& labelText,
                   std::function<juce::String (float)> formatter);

    void setKnobTooltip (const juce::String&);
    void resized() override;

private:
    void updateValueText();

    SnapSlider slider;
    juce::Label nameLabel, valueLabel;
    std::function<juce::String (float)> format;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
// LED de bypass (aceso = módulo ativo), clicável.
class LedButton : public juce::Button
{
public:
    LedButton() : juce::Button ("bypass") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool, bool) override;
};

//==============================================================================
// Medidor horizontal IN/OUT/CPU do top bar.
class LevelMeter : public juce::Component
{
public:
    /// modo nível (dB) — fração calculada de -60..0, com peak-hold
    void setLevel (float newLevelDb);
    /// modo fração direta (CPU)
    void setFraction (float f, juce::Colour c);
    void paint (juce::Graphics&) override;

private:
    float fraction = 0.0f;
    bool solid = false;
    juce::Colour solidColour;
    float peakFrac = 0.0f; // marcador de pico (segura ~1.5 s e decai)
    int peakHoldTicks = 0;
};

//==============================================================================
// Pill do preset no top bar (dot + texto centrado), clicável.
class PillButton : public juce::Button
{
public:
    PillButton() : juce::Button ("preset") {}
    void paintButton (juce::Graphics&, bool, bool) override;

    bool dotLit = false;
};

//==============================================================================
// A cadeia de sinal rolável: Input → Gate → OD → Amp → Cab → EQ → Delay →
// Reverb → Output, com cartões nas métricas do design v2.
class ChainView : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    ChainView (GuitarRigNAMProcessor&, std::function<void (int)> onLoadModel,
               std::function<void (int)> onLoadIr,
               std::function<void (int)> onLoadExtPlugin,
               std::function<void (int)> onOpenExtPluginUi);

    void paint (juce::Graphics&) override;
    void resized() override;

    // drag-and-drop de reordenação (rigs+mixer são âncora fixa) + pan pelo
    // fundo + roda do mouse rolando a cadeia
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // drag-and-drop de arquivos: .nam no amp, IR no cab, .vst3 no slot
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

    void setAmpImage (int lane, juce::Image);
    void setCabImage (int lane, juce::Image);
    void refreshDynamicText();

    static constexpr int chainHeight = 580;

private:
    static constexpr int maxRigs = GuitarRigNAMProcessor::maxRigs;

    int rigBlockWidth() const;
    void updateLayout();

    // entradas da cadeia na ordem visual (efeito ou bloco amp+cabs)
    struct ChainEntry
    {
        juce::String id;             // "gate".."reverb" ou "amp"
        juce::Rectangle<int> box;    // amp: união amp..cabs
    };
    std::vector<ChainEntry> orderedEntries() const;
    juce::Rectangle<int> boxForFx (const juce::String& id) const;
    int effectCardWidth (const juce::String& id) const;

    // estado do arrasto
    juce::String draggingId;
    int dragGrabDx = 0;
    float dragMouseX = -1.0f;
    int dropIndex = -1;
    juce::String lastOrderSeen;      // relayout quando a ordem muda por preset

    // gaveta de efeitos: a cadeia mostra só o que está em uso
    juce::Rectangle<int> addFxB;     // botão "+ EFEITO" no fim da cadeia
    /// insertIndex >= 0 insere na posição exata; -1 = posição canônica
    void showAddFxMenu (int insertIndex, juce::Rectangle<int> targetArea);
    void removeFxFromChain (const juce::String& id);
    /// Relayout imediato (fora do timer) após mudar a cadeia — o layout
    /// nunca fica defasado sob o mouse do usuário.
    void applyChainRelayout();
    /// "+" nos conectores entre cards: {hotspot, índice de inserção}
    std::vector<std::pair<juce::Rectangle<int>, int>> insertSpots() const;
    juce::Array<juce::Component*> componentsForFx (const juce::String& id);
    juce::String onParamIdForFx (const juce::String& id) const;
    static juce::String fxDisplayName (const juce::String& id);
    /// "ext"->0, "ext2".."ext8"->1..7; -1 para qualquer outro id.
    static int extSlotForId (const juce::String& id);
    static juce::Rectangle<int> removeHotspot (juce::Rectangle<int> cardBox)
    {
        return { cardBox.getRight() - 12 - 18 - 6 - 14, cardBox.getY() + 12, 14, 14 };
    }

    // pan da cadeia arrastando o fundo
    bool panning = false;
    juce::Point<int> panStartMouse, panStartView;

    // microinterações: hover nos "+" e "✕"; alvo do drop de arquivo
    juce::Rectangle<int> hoverHotspot;   // "+"/"✕" sob o mouse
    juce::Rectangle<int> dropHighlight;  // card alvo do arquivo arrastado
    /// destino do arquivo em (x,y): {rect do alvo, "nam:lane"/"ir:slot"/"vst3"}
    std::pair<juce::Rectangle<int>, juce::String> dropTargetAt (const juce::String& file,
                                                                int x, int y) const;

    // analisador de espectro (card)
    juce::dsp::FFT anFft { 11 }; // 2048
    std::array<float, 4096> anFftBuf {};
    static constexpr int anNumBands = 24;
    float anBands[anNumBands] = {};

public:

private:
    void drawPedalFrame (juce::Graphics&, juce::Rectangle<int>, const juce::String& title,
                         const juce::String& footer);
    void drawPhoto (juce::Graphics&, const juce::Image&, juce::Rectangle<int>);

    GuitarRigNAMProcessor& processor;

    juce::Rectangle<int> ioInB, gateB, odB, eqB, delayB, revB, ioOutB;
    juce::Rectangle<int> compB, preEqB, pitchB, looperB, limB;
    juce::Rectangle<int> extB[GuitarRigNAMProcessor::maxExtSlots];
    juce::Rectangle<int> wahB, harmB, octB, rmB, bcB, sgB, excB, dsB, tapeB, cnsB, anB;
    // rigs paralelos: um par amp+cab por lane + o card Mixer que soma tudo
    juce::Rectangle<int> ampLaneB[maxRigs], cabLaneB[maxRigs], mixerB;
    juce::Image ampImages[maxRigs], cabImages[maxRigs];

    // knobs / LEDs / botões
    std::unique_ptr<KnobComponent> inputKnob, outputKnob;
    LedButton gateLed, odLed, ampLed, cabLed, eqLed, delayLed, revLed, compLed, preEqLed,
        pitchLed, looperLed, limLed;
    LedButton extLed[GuitarRigNAMProcessor::maxExtSlots];
    LedButton wahLed, harmLed, octLed, rmLed, bcLed, sgLed, excLed, dsLed, tapeLed, cnsLed,
        anLed;
    std::unique_ptr<KnobComponent> gateThreshKnob, gateReleaseKnob, gateHoldKnob;
    std::unique_ptr<KnobComponent> compSustainKnob, compAttackKnob, compBlendKnob, compLevelKnob;
    std::unique_ptr<KnobComponent> preEqLowKnob, preEqMidKnob, preEqHighKnob;
    juce::TextButton compPresetChips[3]; // Clean / Country / Lead

    // seletores de variação (modelo/marca) nos cartões
    juce::TextButton odTypeButton, compTypeButton, delayTypeButton, revTypeButton,
        modTypeButton, delayDivButton, pitchTypeButton;
    juce::TextButton wahModeButton, harmKeyButton, harmScaleButton, harmIntervalButton;
    void setupTypeButton (juce::TextButton&, const char* paramId, const juce::String& tooltip);
    void refreshTypeButtons();

    // cartão Mod + tap tempo do delay
    juce::Rectangle<int> modB;
    LedButton modLed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modAtt;
    std::unique_ptr<KnobComponent> modRateKnob, modDepthKnob, modMixKnob;
    juce::TextButton tapButton { "TAP" };
    juce::int64 lastTapMs = 0;
    void applyTapTempo();
    std::unique_ptr<KnobComponent> odDriveKnob, odToneKnob, odLevelKnob;
    // amp POR LANE (knobs próprios por rig)
    std::unique_ptr<KnobComponent> ampGainKnob[maxRigs], ampBassKnob[maxRigs],
        ampMidKnob[maxRigs], ampTrebleKnob[maxRigs], ampPresKnob[maxRigs],
        ampMasterKnob[maxRigs];
    juce::TextButton loadButtons[maxRigs];
    // cab POR LANE (LC/HC/fase/TROCAR); blend fica no card Mixer
    std::unique_ptr<KnobComponent> cabAirKnob;
    std::unique_ptr<KnobComponent> cabBlendKnob[maxRigs]; // no Mixer
    std::unique_ptr<KnobComponent> cabLcKnob[maxRigs];
    std::unique_ptr<KnobComponent> cabHcKnob[maxRigs];
    juce::TextButton cabPhaseChips[maxRigs];
    juce::TextButton cabIrButtons[maxRigs];
    // Mixer: soma das lanes; +/- adiciona/remove um par AMP+CAB inteiro
    juce::TextButton rigAddButton { "+" }, rigRemoveButton { "-" };
    int lastRigCount = 0;
    std::unique_ptr<KnobComponent> eqLowKnob, eqMidKnob, eqHighKnob;
    std::unique_ptr<KnobComponent> delayTimeKnob, delayFbKnob, delayMixKnob;
    std::unique_ptr<KnobComponent> revDecayKnob, revMixKnob, revPreKnob;
    // P3: pitch, looper e limiter
    std::unique_ptr<KnobComponent> pitchMixKnob, pitchLevelKnob;
    std::unique_ptr<KnobComponent> looperLevelKnob;
    std::unique_ptr<KnobComponent> limCeilKnob, limRelKnob;
    juce::TextButton looperRecButton, looperPlayButton, looperClearButton, looperExportButton;
    // slots de plugin VST3 externo (até 3 na cadeia)
    std::unique_ptr<KnobComponent> extMixKnob[GuitarRigNAMProcessor::maxExtSlots];
    juce::TextButton extLoadButton[GuitarRigNAMProcessor::maxExtSlots],
        extUiButton[GuitarRigNAMProcessor::maxExtSlots],
        extRemoveButton[GuitarRigNAMProcessor::maxExtSlots];
    // cards P4 (um efeito por card)
    std::unique_ptr<KnobComponent> wahFreqKnob, wahRangeKnob, wahResKnob;
    std::unique_ptr<KnobComponent> sgSensKnob, sgRiseKnob;
    std::unique_ptr<KnobComponent> octSubKnob, octDirectKnob, octToneKnob;
    std::unique_ptr<KnobComponent> rmFreqKnob, rmMixKnob;
    std::unique_ptr<KnobComponent> bcBitsKnob, bcRateKnob, bcMixKnob;
    std::unique_ptr<KnobComponent> harmMixKnob, harmLevelKnob;
    std::unique_ptr<KnobComponent> excFreqKnob, excAmtKnob;
    std::unique_ptr<KnobComponent> dsFreqKnob, dsSensKnob, dsAmtKnob;
    std::unique_ptr<KnobComponent> tapeDriveKnob, tapeBumpKnob, tapeRollKnob;
    std::unique_ptr<KnobComponent> cnsAmtKnob;
    juce::TextButton ecoChip { "ECO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ecoAtt;

    // cache do badge V1/V2 dos IRs (lido do sidecar .meta)
    juce::String cabArchCache[GuitarRigNAMProcessor::maxCabSlots];
    juce::String cabArchPathSeen[GuitarRigNAMProcessor::maxCabSlots];
    juce::String archBadgeForIr (int slot);

    using Attachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<Attachment> gateAtt, odAtt, ampAtt, cabAtt, eqAtt, delayAtt, revAtt,
        compAtt, preEqAtt, pitchAtt, looperAtt, limAtt;
    std::unique_ptr<Attachment> extAtt[GuitarRigNAMProcessor::maxExtSlots];
    std::unique_ptr<Attachment> wahAtt, harmAtt, octAtt, rmAtt, bcAtt, sgAtt, excAtt,
        dsAtt, tapeAtt, cnsAtt, anAtt;
    std::unique_ptr<Attachment> cabPhaseAtt[GuitarRigNAMProcessor::maxCabSlots];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainView)
};

//==============================================================================
// Canvas lógico fixo 1100×700 escalado pelo editor.
class DrumOverlay;

class RigContent : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int designWidth = 1100;
    static constexpr int designHeight = 700;

    explicit RigContent (GuitarRigNAMProcessor&);
    ~RigContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void analyseTuner();
    void refreshSidecarImages();
    void chooseModelFile (int lane);
    void chooseIrFile (int slot);
    void chooseExtPluginFile (int slot);
    void openExtPluginWindow (int slot);
    void closeExtPluginWindow (int slot);
    void closeAllExtPluginWindows();
    void chooseDrumVstFile();
    void openDrumVstWindow();
    void closeDrumVstWindow();
    void saveCurrentPreset();
    void beginPresetNameEdit();
    void showPresetMenu();
    void toggleTuner();

    GuitarRigNAMProcessor& processor;
    RigLookAndFeel lookAndFeel;

    // top bar
    LevelMeter inMeter, outMeter, cpuMeter;
    juce::TextButton audioButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x81udio")) };
    juce::TextButton storeButton { "Tone Store" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::TextButton saveButton { "SALVAR" };
    PillButton presetPill;
    juce::TextEditor presetNameEditor;   // edição inline do nome (sem diálogo)
    int saveFlashTicks = 0;              // feedback "Salvo" no botão
    bool focusGrabbed = false;
    bool presetDirtyCached = false;
    juce::TooltipWindow tooltipWindow { this, 600 };

    // cadeia
    juce::Viewport chainViewport;
    std::unique_ptr<ChainView> chainView;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<StoreOverlay> storeOverlay;

    // módulo Bateria (overlay + janela do painel do VST de bateria)
    std::unique_ptr<DrumOverlay> drumOverlay;
    juce::TextButton drumButton { "Bateria" };
    std::unique_ptr<juce::DocumentWindow> drumVstWindow;

    // janelas flutuantes com os painéis dos plugins VST3 hospedados
    std::unique_ptr<juce::DocumentWindow> extWindow[GuitarRigNAMProcessor::maxExtSlots];

    // afinador
    juce::TextButton tunerToggle { "AFINADOR" };
    bool isTunerOn() const;

    // modo performance (palco): esconde a cadeia, mostra o essencial grande
    bool perfMode = false;
    juce::TextButton perfChip { "PALCO" };
    void setPerfMode (bool shouldBeOn);
    void paintPerformanceView (juce::Graphics&);

    // mute do afinador (silencia a saída enquanto afina)
    juce::TextButton muteChip { "MUTE" };
    bool tunerMuteWanted = false;

    // gravador rápido (WAV da saída) + A/B de rigs
    juce::TextButton recChip { juce::CharPointer_UTF8 ("\xe2\x97\x8f REC") };
    juce::int64 recStartMs = 0;
    int recSavedTicks = 0;
    juce::TextButton abButton { "A" };

    // auto-ECO (troca para o capture leve quando a CPU estoura)
    juce::TextButton autoEcoChip { "AUTO-ECO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoEcoAtt;
    int cpuHighTicks = 0;
    int ecoNoticeTicks = 0;
    void applyEcoSwitchIfNeeded();
    double tunerFreq = -1.0;
    double tunerCents = 0.0;
    juce::String tunerNote;
    int tunerStringIndex = -1;
    int tunerTick = 0;

    // sidecars de imagem (por lane de rig)
    juce::String loadedModelPaths[GuitarRigNAMProcessor::maxRigs];
    juce::String loadedIrPaths[GuitarRigNAMProcessor::maxRigs];
    bool ampImagesLoaded[GuitarRigNAMProcessor::maxRigs] = {};
    bool cabImagesLoaded[GuitarRigNAMProcessor::maxRigs] = {};

    float inMeterDb = -80.0f, outMeterDb = -80.0f;
    int clipTicks = 0; // "CLIP" aceso no medidor OUT após pico >= 0 dBFS

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigContent)
};

//==============================================================================
class GuitarRigNAMEditor : public juce::AudioProcessorEditor
{
public:
    explicit GuitarRigNAMEditor (GuitarRigNAMProcessor&);

    void resized() override;

private:
    RigContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarRigNAMEditor)
};
