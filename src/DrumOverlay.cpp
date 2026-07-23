#include "DrumOverlay.h"

#include "LookAndFeel.h"

//==============================================================================
// Layout fixo dentro do conteúdo 1100×700 do editor (v4: a pauta é a track).
namespace
{
constexpr int margin = 26;
constexpr int headerY = 12, headerH = 34;
constexpr int tabsY = 52, tabsH = 26;
constexpr int barHeadsY = 82, barHeadsH = 24;
constexpr int scoreY = 108, scoreH = 300;
constexpr int libY = 414, libChipsH = 25, libCardsH = 92; // biblioteca OU grade
constexpr int gridY = 418, gridH = 226;                   // grade no lugar da lib
constexpr int sourceY = 648, sourceH = 32;

// geometria da pauta: 4 compassos × 16 steps na largura útil (~1048)
// 4 compassos precisam caber em ~1038 px úteis: 64·stepW + 12·beatPad +
// 3·barPad + scoreLeft + folga ≤ largura, senão o 4º compasso corta no fim
constexpr int scoreLeft = 54;
constexpr float stepW = 13.2f, beatPad = 5.0f, barPad = 20.0f;
constexpr float staffSP = 7.0f;   // meia distância entre linhas
constexpr float staffTop = 104.0f;

constexpr float tsW = 20.0f;   // largura da fórmula de compasso na pauta
float staffY (float pos) { return staffTop + 8.0f * staffSP - pos * staffSP; }

// nº de steps + agrupamento das ligaduras de uma métrica (compostos de 3 em 3)
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

// posição na pauta / cabeça × / mão-ou-pé por voz — índices drum::Voice
constexpr float staffPos[drum::numVoices] = { 1, 5, 9, -1, 8, 10, 7, 6, 3 };
constexpr bool staffXHead[drum::numVoices] = { false, false, true, true, true, true,
                                               false, false, false };
constexpr bool staffIsHand[drum::numVoices] = { false, true, true, false, true, true,
                                                true, true, true };

// Desenha 1 compasso em PENTAGRAMA dentro de `area` — mesma linguagem da pauta
// central: pauta de 5 linhas, cabeças (× pratos / elipse tambores), hastes
// (↑ mãos, ↓ pés) e barras de ligação por tempo. Usado na miniatura dos cards.
void drawMiniBar (juce::Graphics& g, juce::Rectangle<float> area,
                  const juce::uint8 pat[drum::numVoices][drum::stepsPerBar])
{
    const juce::Colour ink (0xffc4cdd6), dim (0xff3a424b);
    const float sp = (area.getHeight() - 4.0f) / 16.0f;   // posições -4..12
    auto yOf = [&] (float pos) { return area.getBottom() - 2.0f - (pos + 4.0f) * sp; };
    const float x0 = area.getX() + 4.0f;
    const float sw = (area.getWidth() - 8.0f) / (float) drum::stepsPerBar;
    auto xOf = [&] (int s) { return x0 + (s + 0.5f) * sw; };
    const float hr = juce::jmax (1.7f, sp * 0.72f);       // raio da cabeça

    // 5 linhas da pauta (posições 0,2,4,6,8)
    g.setColour (dim);
    for (int i = 0; i <= 4; ++i)
        g.drawHorizontalLine ((int) yOf ((float) (i * 2)), area.getX(), area.getRight());
    // separadores de tempo
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    for (int beat = 1; beat < 4; ++beat)
        g.drawVerticalLine ((int) (x0 + beat * 4 * sw), yOf (9.0f), yOf (-2.0f));

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

    for (int beat = 0; beat < 4; ++beat)
        for (int limb = 0; limb < 2; ++limb)
        {
            const bool up = (limb == 0);
            struct Col { int s; float noteY; };
            Col cols[4];
            int nc = 0;
            for (int i = 0; i < 4; ++i)
            {
                const int s = beat * 4 + i;
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

// grade opcional
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
DrumOverlay::DrumOverlay (GuitarRigNAMProcessor& p)
    : processor (p), engine (p.drumEngine)
{
    setWantsKeyboardFocus (true);

    closeButton.onClick = [this] { setVisible (false); };
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

    for (auto* c : { &clickChip, &countChip, &followChip, &gridChip, &saveChip })
    {
        c->getProperties().set ("chip", true);
        c->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }
    clickChip.onClick = [this]
    {
        engine.clickOn.store (! engine.clickOn.load());
        clickChip.getProperties().set ("chipActive", engine.clickOn.load());
        clickChip.repaint();
    };
    countChip.setTooltip ("1 compasso de contagem antes de tocar");
    countChip.onClick = [this]
    {
        engine.countInOn.store (! engine.countInOn.load());
        countChip.getProperties().set ("chipActive", engine.countInOn.load());
        countChip.repaint();
    };
    followChip.getProperties().set ("chipActive", true);
    followChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "A pauta vira a p\xc3\xa1gina sozinha quando a m\xc3\xbasica entra na "
        "pr\xc3\xb3xima se\xc3\xa7\xc3\xa3o")));
    followChip.onClick = [this]
    {
        followOn = ! followOn;
        followChip.getProperties().set ("chipActive", followOn);
        followChip.repaint();
    };
    gridChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Grade de 16 steps do compasso selecionado (no lugar da biblioteca)")));
    gridChip.onClick = [this]
    {
        gridOn = ! gridOn;
        gridChip.getProperties().set ("chipActive", gridOn);
        gridChip.repaint();
        refreshAll();
    };

    // sub-filtro Tudo/Grooves/Viradas (cada gênero tem os dois)
    for (auto* c : { &kindTudo, &kindGroove, &kindVirada })
    {
        c->getProperties().set ("chip", true);
        c->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (*c);
    }
    kindTudo.onClick   = [this] { currentKind = 0; refreshKindChips(); rebuildCards(); };
    kindGroove.onClick = [this] { currentKind = 1; refreshKindChips(); rebuildCards(); };
    kindVirada.onClick = [this] { currentKind = 2; refreshKindChips(); rebuildCards(); };
    saveChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Guarda o compasso selecionado em Meus compassos "
        "(Documentos\\PedalForge NAM\\compassos)")));
    saveChip.onClick = [this]
    {
        currentGenre = "MEUS";
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
        "Remove a se\xc3\xa7\xc3\xa3o mostrada (compassos voltam uma casa)")));
    delSectionBtn.onClick = [this]
    {
        const int n = engine.numSections.load();
        if (n <= 1)
            return;
        // puxa os compassos das seções seguintes uma seção para trás
        for (int g = curSection * drum::barsPerSection;
             g < (n - 1) * drum::barsPerSection; ++g)
        {
            const int src = g + drum::barsPerSection;
            engine.setMeter (g, engine.meterNum (src), engine.meterDen (src));
            engine.barFromString (engine.barToString (src), g); // "" limpa; respeita métrica
            engine.barNames[g] = engine.barNames[src];
        }
        for (int g = (n - 1) * drum::barsPerSection; g < n * drum::barsPerSection; ++g)
        {
            engine.clearBar (g);
            engine.barNames[g].clear();
        }
        engine.numSections.store (n - 1);
        curSection = juce::jmin (curSection, n - 2);
        selBar = 0;
        refreshAll();
    };
    addAndMakeVisible (delSectionBtn);

    addAndMakeVisible (scoreView);

    cardsViewport.setViewedComponent (&cardsContent, false);
    cardsViewport.setScrollBarsShown (false, true);
    cardsViewport.setScrollBarThickness (7);
    addAndMakeVisible (cardsViewport);

    saveNameEditor.setFont (ui::monoFont (12.0f));
    saveNameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0c0e11));
    saveNameEditor.setColour (juce::TextEditor::outlineColourId, ui::border());
    saveNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, ui::accentDark);
    saveNameEditor.setColour (juce::TextEditor::textColourId, ui::text);
    saveNameEditor.setTextToShowWhenEmpty (
        juce::String (juce::CharPointer_UTF8 ("nome do compasso selecionado\xe2\x80\xa6")),
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
        "Alterna entre o kit interno e o VST3 de bateria carregado")));
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

    // primeira vez (timeline vazia): monta uma seção demo com o groove padrão
    bool empty = true;
    for (int b = 0; b < drum::maxBars && empty; ++b)
        empty = ! engine.barUsed[b].load();
    if (empty && drum::library().size() > 1)
    {
        juce::uint8 pat[drum::numVoices][drum::stepsPerBar];
        drum::parseSpec (drum::library()[0], pat);
        for (int b = 0; b < 3; ++b)
        {
            engine.setBarPattern (pat, b);
            engine.barNames[b] = juce::String (juce::CharPointer_UTF8 (drum::library()[0].name));
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
    setVisible (true);
    toFront (true);
}

void DrumOverlay::timerCallback()
{
    if (! isVisible())
        return;

    const int uiBar = engine.uiBar.load();
    if (uiBar != lastUiBar || (uiBar >= 0 && engine.uiStep.load() >= 0))
    {
        if (uiBar != lastUiBar)
        {
            lastUiBar = uiBar;
            // SEGUIR: vira a página quando a música entra em outra seção
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
        repaint (getWidth() - 220, headerY, 200, headerH); // posição na label
    }

    const bool playing = engine.playing.load();
    const auto want = playing ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 PARAR"))
                              : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 TOCAR"));
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
                                  ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 PARAR"))
                                  : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 TOCAR")));
    repaint();
}

//==============================================================================
void DrumOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfb101318));

    auto area = getLocalBounds().reduced (margin, 0);

    g.setColour (ui::green);
    g.fillEllipse ((float) area.getX(), headerY + 13.0f, 8.0f, 8.0f);
    g.setFont (ui::uiFont (15.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("BATERIA", area.getX() + 16, headerY, 100, headerH,
                juce::Justification::centredLeft);

    // BPM + rótulos do transporte
    g.setFont (ui::monoFont (16.0f, true));
    g.setColour (ui::textBright);
    g.drawText (juce::String ((int) engine.bpm.load()),
                area.getX() + 262, headerY, 46, headerH, juce::Justification::centred);
    g.setFont (ui::uiFont (9.0f, true));
    g.setColour (ui::textMuted);
    g.drawText ("BPM", area.getX() + 262, headerY - 6, 46, 10, juce::Justification::centred);
    g.drawText ("SWING " + juce::String ((int) engine.swingPct.load()) + "%",
                area.getX() + 362, headerY - 6, 110, 10, juce::Justification::centredLeft);

    // posição (compasso tocando)
    {
        const int uiBar = engine.uiBar.load();
        g.setFont (ui::monoFont (10.0f));
        g.setColour (ui::textFaint);
        const auto txt = uiBar >= 0
                             ? "compasso " + juce::String (uiBar + 1) + "/"
                                   + juce::String (engine.totalBars())
                             : juce::String (engine.totalBars()) + " compassos";
        g.drawText (txt, getWidth() - margin - 44 - 180, headerY, 170, headerH,
                    juce::Justification::centredRight);
    }

    g.setFont (ui::uiFont (9.0f, true));
    g.setColour (ui::textMuted);
    g.drawText ("VOLUME", getWidth() - margin - 130, sourceY - 10, 130, 10,
                juce::Justification::centredLeft);
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

    // abas de seção
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

    // cabeçalhos dos compassos + pauta (larguras seguem a métrica)
    scoreView.setBounds (margin, scoreY, W - 2 * margin, scoreH);
    computeBarLayout (W - 2 * margin);
    for (auto* h : barHeads)
    {
        const auto& L = barLay[h->barInSec];
        const int x0 = margin + (int) (L.notesX - curStepW * 0.5f - 5.0f);
        h->setBounds (x0, barHeadsY, (int) (L.width + curStepW + 10.0f), barHeadsH - 2);
    }

    // biblioteca (ou grade) na mesma área
    const int libW = W - 2 * margin;
    {
        const int chipsPerRow = (genreChips.size() + 1) / 2;
        for (int i = 0; i < genreChips.size(); ++i)
        {
            const int row = i / juce::jmax (1, chipsPerRow);
            const int col = i % juce::jmax (1, chipsPerRow);
            const int w = libW / juce::jmax (1, chipsPerRow);
            genreChips[i]->setBounds (margin + col * w, libY + row * (libChipsH + 4),
                                      w - 4, libChipsH);
        }
        const int subY = libY + 2 * (libChipsH + 4) + 2;
        kindTudo.setBounds (margin, subY, 58, 20);
        kindGroove.setBounds (margin + 62, subY, 82, 20);
        kindVirada.setBounds (margin + 148, subY, 82, 20);
        cardsViewport.setBounds (margin, subY + 26, libW, libCardsH + 10);
    }
    gridView.setBounds (margin, gridY, libW, gridH);

    // salvar (aba MEUS): dentro da fileira de cards (rebuildCards posiciona)

    sourceChip.setBounds (margin, sourceY, 150, sourceH);
    vstLoadButton.setBounds (margin + 158, sourceY, 140, sourceH);
    vstPanelButton.setBounds (margin + 306, sourceY, 76, sourceH);
    vstClearButton.setBounds (margin + 390, sourceY, 90, sourceH);
    saveChip.setBounds (margin + 492, sourceY + 2, 152, 28);
    levelSlider.setBounds (W - margin - 130, sourceY + 3, 130, 26);
}

//==============================================================================
// Layout da pauta com métricas variáveis (calcula larguras por compasso e,
// se estourar a largura útil, encolhe tudo proporcionalmente para caber).
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
    rebuildGenreChips();
    rebuildCards();

    const bool lib = ! gridOn;
    for (auto* c : genreChips)
        c->setVisible (lib);
    cardsViewport.setVisible (lib);
    gridView.setVisible (gridOn);
    refreshKindChips();
    if (! lib)
    {
        saveNameEditor.setVisible (false);
        saveConfirm.setVisible (false);
    }
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
            juce::String (juce::CharPointer_UTF8 ("SE\xc3\x87\xc3\x83O ")) + letter
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
                   + (h->empty ? juce::String ("vazio")
                               : (engine.barNames[g].isNotEmpty() ? engine.barNames[g]
                                                           : juce::String ("editado")));
        h->selected = b == selBar;
        h->meterText = juce::String (engine.meterNum (g)) + "/" + juce::String (engine.meterDen (g));
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

    const int xW = empty ? 0 : 18;   // largura do ✕
    g.setFont (ui::uiFont (11.0f, true));
    g.setColour (empty ? ui::textMuted : (selected ? ui::accent : ui::textDim));
    g.drawText (title, 9, 0, getWidth() - 44 - xW, getHeight(), juce::Justification::centredLeft);

    // fórmula de compasso (clicável) — abre o menu
    g.setFont (ui::monoFont (10.0f, true));
    g.setColour (meterText == "4/4" ? ui::textMuted : ui::accent);
    g.drawText (meterText, getWidth() - 40 - xW, 0, 34, getHeight(), juce::Justification::centredRight);

    if (! empty)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), getWidth() - 20, 0, 14, getHeight(),
                    juce::Justification::centred);
    }
}

void DrumOverlay::BarHead::mouseUp (const juce::MouseEvent& e)
{
    const int x = e.getPosition().x, w = getWidth();
    if (! empty && x > w - 24)          { if (onClear) onClear(); return; }
    if (x > w - 24 - 40 && x <= w - 24) { if (onMeter) onMeter(); return; }  // fórmula
    if (onSelect) onSelect();
}

//==============================================================================
// A pauta central (fórmula de compasso por compasso, larguras variáveis)
void DrumOverlay::ScoreView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0c0e11));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    g.setColour (ui::accentDark.withAlpha (0.55f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);

    owner.computeBarLayout (getWidth());
    auto& engine = owner.engine;
    const juce::Colour ink (0xffc9d2da), dim (0xff5a6570);
    const int sec0 = owner.curSection * drum::barsPerSection;
    auto sx = [&] (int b, int s) { return owner.stepXInBar (b, s); };

    // linhas da pauta + clave de percussão
    g.setColour (dim);
    for (int i = 0; i <= 4; ++i)
        g.drawHorizontalLine ((int) staffY ((float) (i * 2)), 16.0f, (float) getWidth() - 10.0f);
    g.setColour (ink);
    g.fillRect (22.0f, staffY (6), 3.6f, 4.0f * staffSP);
    g.fillRect (29.0f, staffY (6), 3.6f, 4.0f * staffSP);

    // fundo do compasso selecionado
    {
        const auto& L = owner.barLay[owner.selBar];
        const float x0 = L.notesX - owner.curStepW * 0.5f - 5.0f, x1 = L.notesX + L.width + 5.0f;
        g.setColour (ui::accent.withAlpha (0.05f));
        g.fillRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 8.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        const float dash[] = { 3.0f, 3.0f };
        juce::Path pth;
        pth.addRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 8.0f);
        juce::PathStrokeType (1.0f).createDashedStroke (pth, pth, dash, 2);
        g.fillPath (pth);
    }
    if (dragOverBar >= 0)
    {
        const auto& L = owner.barLay[dragOverBar];
        const float x0 = L.notesX - owner.curStepW * 0.5f - 5.0f, x1 = L.notesX + L.width + 5.0f;
        g.setColour (ui::glowOrange.withAlpha (0.10f));
        g.fillRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 8.0f);
        g.setColour (ui::glowOrange);
        g.drawRoundedRectangle (x0, staffY (13.0f), x1 - x0, staffY (-5.0f) - staffY (13.0f), 8.0f, 1.4f);
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

        // fórmula de compasso (só quando muda) — em ciano
        if (L.showTS)
        {
            g.setColour (ui::accent);
            g.setFont (ui::monoFont (15.0f, true));
            g.drawText (juce::String (L.num), (int) L.tsX - 9, (int) staffY (8), 18,
                        (int) (2 * staffSP), juce::Justification::centred);
            g.drawText (juce::String (L.den), (int) L.tsX - 9, (int) staffY (4), 18,
                        (int) (2 * staffSP), juce::Justification::centred);
        }

        // barra de compasso
        const float bx = L.notesX + L.width + owner.curBarPad * 0.5f - 2.0f;
        g.setColour (ink);
        g.drawLine (bx, staffY (8), bx, staffY (0), b == drum::barsPerSection - 1 ? 1.6f : 1.1f);

        // números dos tempos (1 por grupo)
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

        // notas + hastes + ligaduras, por grupo de tempo da métrica
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

    // legenda
    g.setFont (ui::monoFont (8.5f));
    g.setColour (ui::textMuted);
    g.drawText (juce::String (juce::CharPointer_UTF8 (
                    "arraste um groove \xc2\xb7 clique edita \xc2\xb7 f\xc3\xb3rmula no cabe\xc3\xa7"
                    "alho do compasso \xc2\xb7 \xc3\x97 pratos \xc2\xb7 > acento \xc2\xb7 ( ) ghost")),
                20, 4, getWidth() - 40, 12, juce::Justification::centredLeft);
}

int DrumOverlay::ScoreView::barAtX (int x) const { return owner.barAtXlocal (x); }

void DrumOverlay::ScoreView::mouseDown (const juce::MouseEvent& e)
{
    owner.computeBarLayout (getWidth());
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
        juce::uint8 zero[drum::numVoices][drum::stepsPerBar] = {};
        owner.engine.setBarPattern (zero, bar);
        owner.engine.barNames[bar] = "novo";
    }
    auto& cell = owner.engine.pattern[bar][voice][ss];
    cell.store ((juce::uint8) ((cell.load() + 1) % 4));
    owner.rebuildBarHeads();
    repaint();
    if (owner.gridOn)
        owner.gridView.repaint();
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
    owner.applyGrooveToBar (d.description.toString(),
                            owner.curSection * drum::barsPerSection + b);
}

//==============================================================================
// Menu da fórmula de compasso: 5 principais + Custom (numerador/denominador)
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
            juce::String (juce::CharPointer_UTF8 ("F\xc3\xb3rmula de compasso")),
            juce::String (juce::CharPointer_UTF8 (
                "Numerador (1 a 16) e denominador (2, 4, 8 ou 16)")),
            juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("num", juce::String (engine.meterNum (gb)), "Numerador");
        w->addTextEditor ("den", juce::String (engine.meterDen (gb)), "Denominador");
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (juce::String (juce::CharPointer_UTF8 ("Cancelar")), 0,
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

//==============================================================================
// Biblioteca
void DrumOverlay::refreshKindChips()
{
    const bool show = ! gridOn && currentGenre != "MEUS";
    juce::TextButton* chips[] = { &kindTudo, &kindGroove, &kindVirada };
    for (int i = 0; i < 3; ++i)
    {
        chips[i]->setVisible (show);
        chips[i]->getProperties().set ("chipActive", currentKind == i);
        chips[i]->repaint();
    }
}

void DrumOverlay::rebuildGenreChips()
{
    genreChips.clear();
    auto names = drum::genres();
    names.add (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 MEUS")));

    for (const auto& n : names)
    {
        auto* b = genreChips.add (new juce::TextButton (n));
        b->getProperties().set ("chip", true);
        const auto key = n.startsWith (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")))
                             ? juce::String ("MEUS") : n;
        b->getProperties().set ("chipActive", key == currentGenre);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->onClick = [this, key]
        {
            currentGenre = key;
            rebuildGenreChips();
            refreshKindChips();
            rebuildCards();
        };
        addAndMakeVisible (*b);
    }
    resized();
}

void DrumOverlay::rebuildCards()
{
    cards.clear();
    const bool mine = currentGenre == "MEUS";
    saveNameEditor.setVisible (false);
    saveConfirm.setVisible (false);

    int x = 0;
    auto place = [&] (GrooveCard* c)
    {
        cardsContent.addAndMakeVisible (c);
        c->setBounds (x, 0, 176, libCardsH - 6);
        x += 176 + 8;
    };

    if (mine && ! gridOn)
    {
        cardsContent.addAndMakeVisible (saveNameEditor);
        cardsContent.addAndMakeVisible (saveConfirm);
        saveNameEditor.setVisible (true);
        saveConfirm.setVisible (true);
        saveNameEditor.setBounds (0, 10, 200, 30);
        saveConfirm.setBounds (206, 10, 78, 30);
        x = 292;

        for (const auto& f : userGroovesDir().findChildFiles (juce::File::findFiles,
                                                              false, "*.json"))
        {
            auto* c = cards.add (new GrooveCard());
            const auto parsed = juce::JSON::parse (f.loadFileAsString());
            c->title = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
            const int bpmV = (int) parsed.getProperty ("bpm", 0);
            c->meta = bpmV > 0 ? juce::String (bpmV) + " bpm" : juce::String ("1 compasso");
            c->dragId = "u:" + f.getFullPathName();
            c->deletable = true;
            {
                const auto str = parsed.getProperty ("pattern", "").toString();
                int k = 0;
                for (int v = 0; v < drum::numVoices; ++v)
                    for (int s = 0; s < drum::stepsPerBar; ++s)
                    {
                        const juce::juce_wchar ch = k < str.length() ? str[k] : '0';
                        c->pat[v][s] = ch >= '0' && ch <= '3' ? (juce::uint8) (ch - '0') : 0;
                        ++k;
                    }
                c->hasPat = str.isNotEmpty();
            }
            c->onLoad = [this, id = c->dragId] { applyGrooveToBar (id, selectedBar()); };
            c->onDelete = [this, f]
            {
                f.deleteFile();
                rebuildCards();
            };
            place (c);
        }
    }
    else if (! gridOn)
    {
        const auto& lib = drum::library();
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            const auto& g = lib[(size_t) i];
            if (juce::String (juce::CharPointer_UTF8 (g.genre)) != currentGenre)
                continue;
            if (currentKind == 1 && g.fill)  continue;   // só grooves
            if (currentKind == 2 && ! g.fill) continue;  // só viradas

            auto* c = cards.add (new GrooveCard());
            c->title = juce::String (juce::CharPointer_UTF8 (g.name));
            c->fill = g.fill;
            const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
            c->meta = g.fill
                          ? "virada" + dot + "1 compasso"
                          : juce::String (g.bpm) + " bpm"
                                + (g.swing > 0 ? dot + "sw " + juce::String (g.swing) + "%"
                                               : juce::String());
            c->dragId = "f:" + juce::String (i);
            drum::parseSpec (g, c->pat);
            c->hasPat = true;
            c->onLoad = [this, id = c->dragId] { applyGrooveToBar (id, selectedBar()); };
            place (c);
        }
    }

    cardsContent.setSize (juce::jmax (x, 1), libCardsH - 6 + 10);
    cardsViewport.setViewPosition (0, 0);
}

void DrumOverlay::applyGrooveToBar (const juce::String& dragId, int globalBar)
{
    if (globalBar < 0 || globalBar >= engine.totalBars())
        return;

    juce::uint8 pat[drum::numVoices][drum::stepsPerBar] = {};
    juce::String name;

    if (dragId.startsWith ("f:"))
    {
        const int idx = dragId.substring (2).getIntValue();
        const auto& lib = drum::library();
        if (idx < 0 || idx >= (int) lib.size())
            return;
        const auto& g = lib[(size_t) idx];
        drum::parseSpec (g, pat);
        name = juce::String (juce::CharPointer_UTF8 (g.name));
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
    }
    else
        return;

    // aplica SÓ as notas — o BPM/swing da música não muda ao soltar um groove
    // (o valor no card é apenas uma sugestão; ajuste o tempo no transporte).
    // Grooves da biblioteca são 4/4 (16 steps), então o compasso vira 4/4.
    engine.setMeter (globalBar, 4, 4);
    engine.setBarPattern (pat, globalBar);
    engine.barNames[globalBar] = name;

    curSection = globalBar / drum::barsPerSection;
    selBar = globalBar % drum::barsPerSection;
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
void DrumOverlay::GrooveCard::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient grad (ui::cardTop, 0, 0, ui::cardBottom, 0, b.getHeight(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, 9.0f);
    g.setColour (dragging ? ui::glowOrange.withAlpha (0.6f) : ui::border());
    g.drawRoundedRectangle (b.reduced (0.5f), 9.0f, 1.0f);

    // viradas: faixa laranja na esquerda (distingue de groove num relance)
    if (fill)
    {
        g.setColour (ui::glowOrange);
        g.fillRoundedRectangle (1.5f, 6.0f, 2.5f, b.getHeight() - 12.0f, 1.2f);
    }

    g.setFont (ui::uiFont (11.5f, true));
    g.setColour (ui::textBright);
    g.drawText (title, 9, 3, getWidth() - 54, 15, juce::Justification::centredLeft);
    g.setFont (ui::monoFont (8.0f));
    g.setColour (ui::textMuted);
    g.drawText (meta, getWidth() - 50, 3, 44, 15, juce::Justification::centredRight);

    // miniatura em pentagrama do que vai ser colocado (antes de arrastar)
    if (hasPat)
        drawMiniBar (g, { 8.0f, 20.0f, getWidth() - 16.0f, getHeight() - 24.0f }, pat);

    if (deletable)
    {
        g.setColour (ui::textMuted);
        g.setFont (ui::monoFont (11.0f));
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), getWidth() - 17,
                    getHeight() - 16, 13, 13, juce::Justification::centred);
    }
}

void DrumOverlay::GrooveCard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        repaint();
        dnd->startDragging (dragId, this);
    }
}

void DrumOverlay::GrooveCard::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDragging = dragging;
    dragging = false;
    repaint();
    if (wasDragging || e.getDistanceFromDragStart() >= 6)
        return;

    if (deletable && e.getPosition().x > getWidth() - 24
        && e.getPosition().y > getHeight() - 20)
    {
        if (onDelete)
            onDelete();
        return;
    }
    if (onLoad)
        onLoad();
}

//==============================================================================
// Grade opcional (16 steps do compasso selecionado)
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
    g.drawText ("GRADE " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                    + " compasso " + juce::String (bar + 1)
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
                    juce::uint8 zero[drum::numVoices][drum::stepsPerBar] = {};
                    owner.engine.setBarPattern (zero, bar);
                    owner.engine.barNames[bar] = "novo";
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

    sourceChip.setButtonText (vstOn ? "FONTE: VST3" : "FONTE: INTERNA");
    sourceChip.getProperties().set ("chipActive", vstOn);
    sourceChip.repaint();

    vstLoadButton.setButtonText (hasVst
        ? processor.getDrumPluginName().substring (0, 16)
        : juce::String (juce::CharPointer_UTF8 ("CARREGAR VST3\xe2\x80\xa6")));
    vstPanelButton.setVisible (hasVst);
    vstClearButton.setVisible (hasVst);
}
