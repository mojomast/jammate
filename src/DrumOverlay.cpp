#include "DrumOverlay.h"

#include "LookAndFeel.h"

//==============================================================================
// Layout fixo dentro do conteúdo 1100×700 do editor.
namespace
{
constexpr int margin = 26;
constexpr int headerY = 12, headerH = 32;
constexpr int transportY = 46, transportH = 34;
constexpr int sectionsY = 86, sectionsH = 26;
constexpr int genresY = 118, genreRowH = 25;
constexpr int cardsY = 176, cardsH = 58;
constexpr int scoreY = 240, scoreH = 160;
constexpr int gridY = 406, gridH = 226;
constexpr int sourceY = 640, sourceH = 32;

// partitura: posição na pauta / cabeça × / mão-ou-pé por voz (compartilhado
// entre desenho e clique) — índices seguem drum::Voice
constexpr float staffPos[drum::numVoices] = { 1, 5, 9, -1, 8, 10, 7, 6, 3 };
constexpr bool staffXHead[drum::numVoices] = { false, false, true, true, true, true,
                                               false, false, false };
constexpr bool staffIsHand[drum::numVoices] = { false, true, true, false, true, true,
                                                true, true, true };

// grade: geometria compartilhada entre GridView e ScoreView (colunas alinhadas)
constexpr int gridLabelW = 96;
constexpr int cellW = 24, cellGap = 2, beatGap = 8, barGap = 18;
constexpr int rowH = 21, rowGap = 2;

int stepX (int s)
{
    const int bar = s / drum::stepsPerBar, sb = s % drum::stepsPerBar;
    return gridLabelW + bar * (drum::stepsPerBar * (cellW + cellGap) + 3 * beatGap + barGap)
           + sb * (cellW + cellGap) + (sb / 4) * beatGap;
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

    for (auto* c : { &clickChip, &countChip })
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

    // ---- song mode: MÚSICA + seções A..H + repetições
    songChip.getProperties().set ("chip", true);
    songChip.setMouseClickGrabsKeyboardFocus (false);
    songChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Toca as se\xc3\xa7\xc3\xb5""es em sequ\xc3\xaancia (com as repeti\xc3\xa7\xc3\xb5""es); "
        "desligado, repete s\xc3\xb3 a se\xc3\xa7\xc3\xa3o selecionada")));
    songChip.onClick = [this]
    {
        engine.songMode.store (! engine.songMode.load());
        songChip.getProperties().set ("chipActive", engine.songMode.load());
        songChip.repaint();
    };
    addAndMakeVisible (songChip);

    addSectionBtn.onClick = [this]
    {
        const int n = engine.numSections.load();
        if (n >= drum::maxSections)
            return;
        const int cur = engine.editSection.load();
        for (int v = 0; v < drum::numVoices; ++v)     // duplica a seção atual
            for (int s = 0; s < drum::numSteps; ++s)
                engine.pattern[n][v][s].store (engine.pattern[cur][v][s].load());
        engine.sectionNames[n] = juce::String::charToString ((juce::juce_wchar) ('A' + n));
        engine.sectionRepeats[n].store (1);
        sectionGroove[n] = sectionGroove[cur];
        engine.numSections.store (n + 1);
        engine.editSection.store (n);
        rebuildSectionChips();
        refreshPatternViews();
    };
    addAndMakeVisible (addSectionBtn);

    delSectionBtn.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Apaga a se\xc3\xa7\xc3\xa3o selecionada")));
    delSectionBtn.onClick = [this]
    {
        const int n = engine.numSections.load();
        if (n <= 1)
            return;
        const int cur = engine.editSection.load();
        for (int i = cur; i < n - 1; ++i)             // puxa as seguintes p/ trás
        {
            for (int v = 0; v < drum::numVoices; ++v)
                for (int s = 0; s < drum::numSteps; ++s)
                    engine.pattern[i][v][s].store (engine.pattern[i + 1][v][s].load());
            engine.sectionNames[i] = engine.sectionNames[i + 1];
            engine.sectionRepeats[i].store (engine.sectionRepeats[i + 1].load());
            sectionGroove[i] = sectionGroove[i + 1];
        }
        engine.numSections.store (n - 1);
        engine.editSection.store (juce::jmin (cur, n - 2));
        currentGrooveName = sectionGroove[engine.editSection.load()];
        rebuildSectionChips();
        refreshPatternViews();
    };
    addAndMakeVisible (delSectionBtn);

    repeatBtn.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Quantas vezes a se\xc3\xa7\xc3\xa3o repete no song mode")));
    repeatBtn.onClick = [this]
    {
        const int cur = engine.editSection.load();
        static const int cycle[] = { 1, 2, 3, 4, 6, 8 };
        const int now = juce::jmax (1, engine.sectionRepeats[cur].load());
        int next = cycle[0];
        for (int i = 0; i < 5; ++i)
            if (cycle[i] == now) { next = cycle[i + 1]; break; }
        engine.sectionRepeats[cur].store (next);
        rebuildSectionChips();
    };
    addAndMakeVisible (repeatBtn);

    saveChip.getProperties().set ("chip", true);
    saveChip.setMouseClickGrabsKeyboardFocus (false);
    saveChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Guarda a pattern atual em Meus compassos "
        "(Documentos\\PedalForge NAM\\compassos)")));
    saveChip.onClick = [this]
    {
        currentGenre = "MEUS";
        rebuildGenreChips();
        rebuildCards();
        saveNameEditor.grabKeyboardFocus();
    };
    addAndMakeVisible (saveChip);

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
        juce::String (juce::CharPointer_UTF8 ("nome do compasso\xe2\x80\xa6")), ui::textMuted);
    saveNameEditor.onReturnKey = [this] { saveUserGroove(); };
    addChildComponent (saveNameEditor);

    saveConfirm.getProperties().set ("outlineAccent", true);
    saveConfirm.onClick = [this] { saveUserGroove(); };
    addChildComponent (saveConfirm);

    addAndMakeVisible (scoreView);
    addAndMakeVisible (gridView);

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
             &vstLoadButton, &vstPanelButton, &vstClearButton })
        b->setMouseClickGrabsKeyboardFocus (false);

    // primeira vez (pattern vazia): carrega o groove padrão da biblioteca
    bool empty = true;
    for (int v = 0; v < drum::numVoices && empty; ++v)
        for (int s = 0; s < drum::numSteps && empty; ++s)
            empty = engine.pattern[0][v][s].load() == 0;
    if (empty && ! drum::library().empty())
        loadFactoryGroove (drum::library()[0]);

    rebuildSectionChips();
    rebuildGenreChips();
    rebuildCards();
    refreshSourceRow();
    syncTransportUi();
    startTimerHz (30);
}

DrumOverlay::~DrumOverlay() = default;

void DrumOverlay::open()
{
    syncTransportUi();
    refreshSourceRow();
    setVisible (true);
    toFront (true);
}

void DrumOverlay::timerCallback()
{
    if (! isVisible())
        return;
    const int s = engine.uiStep.load();
    if (s != lastUiStep)
    {
        lastUiStep = s;
        scoreView.repaint();
        gridView.repaint();
    }
    const bool playing = engine.playing.load();
    const auto want = playing ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 PARAR"))
                              : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 TOCAR"));
    if (playButton.getButtonText() != want)
        playButton.setButtonText (want);

    // seção tocando mudou (song mode): atualiza o destaque nos chips
    const int uiSec = engine.uiSection.load();
    if (uiSec != lastUiSection)
    {
        lastUiSection = uiSec;
        rebuildSectionChips();
    }

    // preset/A-B trocaram as seções por fora: ressincroniza a UI
    const int nSec = engine.numSections.load(), eSec = engine.editSection.load();
    if (nSec != lastNumSections || eSec != lastEditSection)
    {
        lastNumSections = nSec;
        lastEditSection = eSec;
        songChip.getProperties().set ("chipActive", engine.songMode.load());
        songChip.repaint();
        rebuildSectionChips();
        refreshPatternViews();
        syncTransportUi();
    }

    // o load do VST publica no próximo bloco de áudio — poll leve aqui
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

    // header
    g.setColour (ui::green);
    g.fillEllipse ((float) area.getX(), headerY + 14.0f, 8.0f, 8.0f);
    g.setFont (ui::uiFont (15.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("BATERIA", area.getX() + 16, headerY, 120, headerH, juce::Justification::centredLeft);
    g.setFont (ui::monoFont (10.0f));
    g.setColour (ui::textFaint);
    {
        const int cur = juce::jlimit (0, drum::maxSections - 1, engine.editSection.load());
        const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("se\xc3\xa7\xc3\xa3o "))
                        + engine.sectionNames[cur] + dot + "groove: "
                        + (currentGrooveName.isNotEmpty() ? currentGrooveName : juce::String ("-"))
                        + dot + "2 compassos" + dot + "4/4",
                    area.getX() + 130, headerY, 520, headerH, juce::Justification::centredLeft);
    }

    // transporte: BPM + rótulos
    g.setFont (ui::monoFont (16.0f, true));
    g.setColour (ui::textBright);
    g.drawText (juce::String ((int) engine.bpm.load()),
                area.getX() + 118, transportY, 46, transportH, juce::Justification::centred);
    g.setFont (ui::uiFont (9.5f, true));
    g.setColour (ui::textMuted);
    g.drawText ("BPM", area.getX() + 118, transportY - 11, 46, 12, juce::Justification::centred);
    g.drawText ("SWING " + juce::String ((int) engine.swingPct.load()) + "%",
                area.getX() + 218, transportY - 11, 110, 12, juce::Justification::centredLeft);
    g.drawText ("VOLUME", getWidth() - margin - 130, sourceY - 11, 130, 12,
                juce::Justification::centredLeft);

    if (currentGenre == "VIRADAS")
    {
        g.setFont (ui::monoFont (9.5f));
        g.setColour (ui::textFaint);
        g.drawText (juce::String (juce::CharPointer_UTF8 (
                        "a virada entra no 2\xc2\xba compasso da pattern atual")),
                    area.getX(), cardsY + cardsH, 400, 14, juce::Justification::centredLeft);
    }
}

void DrumOverlay::resized()
{
    const int W = getWidth();
    auto x0 = margin;

    closeButton.setBounds (W - margin - 34, headerY, 34, 30);

    playButton.setBounds (x0, transportY, 96, transportH);
    bpmDown.setBounds (x0 + 96 + 12, transportY + 4, 24, 26);
    bpmUp.setBounds (x0 + 96 + 12 + 24 + 48, transportY + 4, 24, 26);
    swingSlider.setBounds (x0 + 218, transportY + 4, 110, 26);
    clickChip.setBounds (x0 + 344, transportY + 3, 66, 28);
    countChip.setBounds (x0 + 416, transportY + 3, 100, 28);
    saveChip.setBounds (x0 + 528, transportY + 3, 152, 28);

    // fileira do song mode: MÚSICA · seções · ×N · ✕ · + SEÇÃO
    {
        int sx = x0;
        songChip.setBounds (sx, sectionsY, 76, sectionsH);
        sx += 84;
        for (auto* b : sectionChips)
        {
            b->setBounds (sx, sectionsY, 58, sectionsH);
            sx += 62;
        }
        repeatBtn.setBounds (sx, sectionsY, 44, sectionsH);
        sx += 48;
        delSectionBtn.setBounds (sx, sectionsY, 34, sectionsH);
        sx += 40;
        addSectionBtn.setBounds (sx, sectionsY, 84, sectionsH);
    }

    // duas linhas de chips de gênero
    const int chipsPerRow = (genreChips.size() + 1) / 2;
    const int availW = W - 2 * margin;
    for (int i = 0; i < genreChips.size(); ++i)
    {
        const int row = i / juce::jmax (1, chipsPerRow);
        const int col = i % juce::jmax (1, chipsPerRow);
        const int w = availW / juce::jmax (1, chipsPerRow);
        genreChips[i]->setBounds (margin + col * w, genresY + row * (genreRowH + 4),
                                  w - 4, genreRowH);
    }

    cardsViewport.setBounds (margin, cardsY, W - 2 * margin, cardsH);
    scoreView.setBounds (margin, scoreY, W - 2 * margin, scoreH);
    gridView.setBounds (margin, gridY, W - 2 * margin, gridH);

    sourceChip.setBounds (margin, sourceY, 150, sourceH);
    vstLoadButton.setBounds (margin + 158, sourceY, 140, sourceH);
    vstPanelButton.setBounds (margin + 306, sourceY, 76, sourceH);
    vstClearButton.setBounds (margin + 390, sourceY, 90, sourceH);
    levelSlider.setBounds (W - margin - 130, sourceY + 3, 130, 26);
}

//==============================================================================
// Song mode
void DrumOverlay::refreshPatternViews()
{
    currentGrooveName = sectionGroove[juce::jlimit (0, drum::maxSections - 1,
                                                    engine.editSection.load())];
    scoreView.repaint();
    gridView.repaint();
    repaint();
}

void DrumOverlay::rebuildSectionChips()
{
    sectionChips.clear();
    const int n = juce::jlimit (1, drum::maxSections, engine.numSections.load());
    const int cur = juce::jlimit (0, n - 1, engine.editSection.load());

    for (int i = 0; i < n; ++i)
    {
        const int reps = juce::jmax (1, engine.sectionRepeats[i].load());
        auto label = engine.sectionNames[i].isNotEmpty()
                         ? engine.sectionNames[i]
                         : juce::String::charToString ((juce::juce_wchar) ('A' + i));
        if (reps > 1)
            label << juce::String (juce::CharPointer_UTF8 (" \xc3\x97")) << reps;

        auto* b = sectionChips.add (new juce::TextButton (label));
        b->getProperties().set ("chip", true);
        b->getProperties().set ("chipActive", i == cur);
        if (i == engine.uiSection.load())   // tocando agora (song mode)
            b->setColour (juce::TextButton::textColourOffId, ui::glowOrange);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->onClick = [this, i]
        {
            engine.editSection.store (i);
            rebuildSectionChips();
            refreshPatternViews();
        };
        addAndMakeVisible (*b);
    }

    repeatBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97"))
                             + juce::String (juce::jmax (1, engine.sectionRepeats[cur].load())));
    delSectionBtn.setEnabled (n > 1);
    addSectionBtn.setEnabled (n < drum::maxSections);
    resized();
}

//==============================================================================
// Biblioteca
void DrumOverlay::rebuildGenreChips()
{
    genreChips.clear();
    auto names = drum::genres();
    names.add (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 MEUS")));
    names.add ("VIRADAS");

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
            rebuildCards();
            repaint();
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
    auto place = [&] (DrumOverlay::GrooveCard* c)
    {
        cardsContent.addAndMakeVisible (c);
        c->setBounds (x, 0, 158, cardsH - 10);
        x += 158 + 8;
    };

    if (mine)
    {
        // "salvar a pattern atual" entra como primeiro item da fileira
        cardsContent.addAndMakeVisible (saveNameEditor);
        cardsContent.addAndMakeVisible (saveConfirm);
        saveNameEditor.setVisible (true);
        saveConfirm.setVisible (true);
        saveNameEditor.setBounds (0, 10, 200, 30);
        saveConfirm.setBounds (206, 10, 78, 30);
        x = 292;

        auto dir = userGroovesDir();
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            auto* c = cards.add (new GrooveCard());
            const auto parsed = juce::JSON::parse (f.loadFileAsString());
            c->title = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
            const int bpmV = (int) parsed.getProperty ("bpm", 0);
            c->meta = bpmV > 0 ? juce::String (bpmV) + " bpm" : juce::String ("--");
            c->selected = c->title == currentGrooveName;
            c->deletable = true;
            c->onLoad = [this, f] { loadUserGroove (f); };
            c->onDelete = [this, f]
            {
                f.deleteFile();
                rebuildCards();
            };
            place (c);
        }
    }
    else
    {
        for (const auto& g : drum::library())
        {
            const auto genre = juce::String (juce::CharPointer_UTF8 (g.genre));
            const bool isFill = genre == "VIRADA";
            if ((currentGenre == "VIRADAS") != isFill)
                continue;
            if (! isFill && genre != currentGenre)
                continue;

            auto* c = cards.add (new GrooveCard());
            c->title = juce::String (juce::CharPointer_UTF8 (g.name));
            c->meta = isFill
                          ? (g.swing > 0 ? "swing " + juce::String (g.swing) + "%"
                                         : juce::String ("virada"))
                          : juce::String (g.bpm) + " bpm"
                                + (g.swing > 0 ? " \xc2\xb7 sw " + juce::String (g.swing) + "%"
                                               : juce::String());
            c->selected = c->title == currentGrooveName;
            const drum::Groove* gp = &g;
            c->onLoad = [this, gp, isFill]
            {
                if (isFill)
                    applyFill (*gp);
                else
                    loadFactoryGroove (*gp);
            };
            place (c);
        }
    }

    cardsContent.setSize (juce::jmax (x, 1), cardsH - 10);
    cardsViewport.setViewPosition (0, 0);
}

void DrumOverlay::loadFactoryGroove (const drum::Groove& g)
{
    juce::uint8 pat[drum::numVoices][drum::numSteps];
    drum::parseSpec (g, pat);
    engine.setPattern (pat);
    if (g.bpm > 0)
        engine.bpm.store ((float) g.bpm);
    engine.swingPct.store ((float) g.swing);
    currentGrooveName = juce::String (juce::CharPointer_UTF8 (g.name));
    sectionGroove[juce::jlimit (0, drum::maxSections - 1, engine.editSection.load())]
        = currentGrooveName;
    syncTransportUi();
    rebuildCards();
    scoreView.repaint();
    gridView.repaint();
}

void DrumOverlay::applyFill (const drum::Groove& g)
{
    juce::uint8 pat[drum::numVoices][drum::numSteps];
    for (int v = 0; v < drum::numVoices; ++v)
        for (int s = 0; s < drum::numSteps; ++s)
            pat[v][s] = engine.cell (v, s).load();
    drum::parseSpec (g, pat, true);
    engine.setPattern (pat);
    if (g.swing > 0)
        engine.swingPct.store ((float) g.swing);
    currentGrooveName = currentGrooveName.upToFirstOccurrenceOf (" + ", false, false)
                        + " + " + juce::String (juce::CharPointer_UTF8 (g.name));
    sectionGroove[juce::jlimit (0, drum::maxSections - 1, engine.editSection.load())]
        = currentGrooveName;
    syncTransportUi();
    scoreView.repaint();
    gridView.repaint();
}

juce::File DrumOverlay::userGroovesDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PedalForge NAM").getChildFile ("compassos");
    dir.createDirectory();
    return dir;
}

void DrumOverlay::loadUserGroove (const juce::File& f)
{
    const auto parsed = juce::JSON::parse (f.loadFileAsString());
    engine.patternFromString (parsed.getProperty ("pattern", "").toString());
    const double bpmV = (double) parsed.getProperty ("bpm", 0.0);
    if (bpmV > 0)
        engine.bpm.store ((float) bpmV);
    engine.swingPct.store ((float) (double) parsed.getProperty ("swing", 0.0));
    currentGrooveName = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    sectionGroove[juce::jlimit (0, drum::maxSections - 1, engine.editSection.load())]
        = currentGrooveName;
    syncTransportUi();
    rebuildCards();
    scoreView.repaint();
    gridView.repaint();
}

void DrumOverlay::saveUserGroove()
{
    const auto name = saveNameEditor.getText().trim();
    if (name.isEmpty())
    {
        saveNameEditor.grabKeyboardFocus();
        return;
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("bpm", (int) engine.bpm.load());
    obj->setProperty ("swing", (int) engine.swingPct.load());
    obj->setProperty ("pattern", engine.patternToString());
    userGroovesDir()
        .getChildFile (juce::File::createLegalFileName (name) + ".json")
        .replaceWithText (juce::JSON::toString (juce::var (obj), true));

    saveNameEditor.setText ("");
    currentGrooveName = name;
    rebuildCards();
}

//==============================================================================
void DrumOverlay::GrooveCard::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient grad (ui::cardTop, 0, 0, ui::cardBottom, 0, b.getHeight(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, 9.0f);
    g.setColour (selected ? ui::accentDark : ui::border());
    g.drawRoundedRectangle (b.reduced (0.5f), 9.0f, selected ? 1.4f : 1.0f);

    g.setFont (ui::uiFont (12.0f, true));
    g.setColour (selected ? ui::accent : ui::textBright);
    g.drawText (title, 10, 6, getWidth() - (deletable ? 34 : 18), 18,
                juce::Justification::centredLeft);
    g.setFont (ui::monoFont (9.0f));
    g.setColour (ui::textFaint);
    g.drawText (meta, 10, 26, getWidth() - 18, 14, juce::Justification::centredLeft);

    if (deletable)
    {
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textMuted);
        g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), getWidth() - 22, 4, 16, 16,
                    juce::Justification::centred);
    }
}

void DrumOverlay::GrooveCard::mouseUp (const juce::MouseEvent& e)
{
    if (deletable && e.getPosition().x > getWidth() - 26 && e.getPosition().y < 22)
    {
        if (onDelete)
            onDelete();
        return;
    }
    if (onLoad)
        onLoad();
}

//==============================================================================
// Grade de steps
juce::Rectangle<int> DrumOverlay::GridView::cellBounds (int row, int step) const
{
    return { stepX (step), 18 + row * (rowH + rowGap), cellW, rowH };
}

void DrumOverlay::GridView::paint (juce::Graphics& g)
{
    auto& engine = owner.engine;

    // contagem 1 & 2 & ...
    g.setFont (ui::monoFont (9.0f));
    for (int s = 0; s < drum::numSteps; ++s)
    {
        g.setColour (s % 4 == 0 ? ui::textDim : ui::textMuted);
        const juce::String lbl = s % 4 == 0 ? juce::String ((s / 4) % 4 + 1)
                                            : (s % 2 == 0 ? juce::String ("&")
                                                          : juce::String::fromUTF8 ("\xc2\xb7"));
        g.drawText (lbl, stepX (s), 0, cellW, 14, juce::Justification::centred);
    }

    const int playStep = owner.playheadStep();

    for (int row = 0; row < gridRows; ++row)
    {
        const int v = gridRowVoice[row];
        g.setFont (ui::uiFont (11.0f, true));
        g.setColour (ui::textDim);
        g.drawText (juce::String (juce::CharPointer_UTF8 (drum::voiceNames[v])),
                    0, 18 + row * (rowH + rowGap), gridLabelW - 10, rowH,
                    juce::Justification::centredRight);

        for (int s = 0; s < drum::numSteps; ++s)
        {
            auto r = cellBounds (row, s).toFloat();
            const int val = engine.cell (v, s).load();

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
    for (int row = 0; row < gridRows; ++row)
        for (int s = 0; s < drum::numSteps; ++s)
            if (cellBounds (row, s).contains (e.getPosition()))
            {
                auto& cell = owner.engine.cell (gridRowVoice[row], s);
                cell.store ((juce::uint8) ((cell.load() + 1) % 4));
                repaint();
                owner.scoreView.repaint();
                return;
            }
}

//==============================================================================
// Partitura — mesma geometria de colunas da grade (stepX), pentagrama
// percussivo com a convenção do mockup: hastes ↑ mãos / ↓ pés, × = pratos.
void DrumOverlay::ScoreView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0c0e11));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    g.setColour (ui::border());
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);

    const float SP = 7.0f;                    // meia distância entre linhas
    const float top = 46.0f;
    auto yOf = [&] (float pos) { return top + 8.0f * SP - pos * SP; };
    auto xOf = [] (int s) { return (float) stepX (s) + cellW * 0.5f; };

    const juce::Colour ink (0xffc9d2da), dim (0xff5a6570);

    // linhas da pauta + clave de percussão + compassos
    g.setColour (dim);
    for (int i = 0; i <= 4; ++i)
        g.drawHorizontalLine ((int) yOf ((float) (i * 2)), 36.0f, (float) getWidth() - 16.0f);
    g.setColour (ink);
    g.fillRect (44.0f, yOf (6), 4.0f, 4.0f * SP);
    g.fillRect (52.0f, yOf (6), 4.0f, 4.0f * SP);
    g.setFont (ui::monoFont (17.0f, true));
    g.drawText ("4", 62, (int) yOf (8), 16, (int) (2 * SP), juce::Justification::centred);
    g.drawText ("4", 62, (int) yOf (4), 16, (int) (2 * SP), juce::Justification::centred);

    const float barX = (xOf (drum::stepsPerBar) + xOf (drum::stepsPerBar - 1)) * 0.5f;
    g.drawLine (barX, yOf (8), barX, yOf (0), 1.3f);
    g.drawLine ((float) getWidth() - 18.0f, yOf (8), (float) getWidth() - 18.0f, yOf (0), 1.3f);

    // números dos tempos
    g.setFont (ui::monoFont (9.0f));
    g.setColour (dim);
    for (int s = 0; s < drum::numSteps; s += 4)
        g.drawText (juce::String ((s / 4) % 4 + 1), (int) xOf (s) - 8, getHeight() - 18,
                    16, 12, juce::Justification::centred);

    // playhead
    const int playStep = owner.playheadStep();
    if (playStep >= 0)
    {
        g.setColour (ui::accent.withAlpha (0.13f));
        g.fillRoundedRectangle (xOf (playStep) - cellW * 0.5f + 1, yOf (13.0f),
                                (float) cellW - 2, yOf (-5.0f) - yOf (13.0f), 4.0f);
    }

    auto drawHead = [&] (float x, float y, bool cross, bool ghost)
    {
        g.setColour (ink);
        if (cross)
        {
            const float r = 4.4f;
            g.drawLine (x - r, y - r, x + r, y + r, 1.8f);
            g.drawLine (x - r, y + r, x + r, y - r, 1.8f);
        }
        else
        {
            juce::Path p;
            p.addEllipse (x - 5.4f, y - 4.0f, 10.8f, 8.0f);
            p.applyTransform (juce::AffineTransform::rotation (-0.31f, x, y));
            g.fillPath (p);
        }
        if (ghost)
        {
            g.setFont (ui::monoFont (11.0f));
            g.setColour (dim);
            g.drawText ("(", (int) x - 15, (int) y - 7, 8, 14, juce::Justification::centred);
            g.drawText (")", (int) x + 7, (int) y - 7, 8, 14, juce::Justification::centred);
            g.setColour (ink);
        }
    };

    const float beamYH = yOf (12.0f), beamYF = yOf (-4.0f);

    for (int beat = 0; beat < drum::numSteps / 4; ++beat)
    {
        for (int limb = 0; limb < 2; ++limb) // 0 = mãos (↑), 1 = pés (↓)
        {
            const bool up = limb == 0;
            struct Col { int s; float noteY; bool accent; };
            Col cols[4];
            int numCols = 0;

            for (int i = 0; i < 4; ++i)
            {
                const int s = beat * 4 + i;
                float extremeY = up ? -1.0e9f : 1.0e9f;
                bool any = false, acc = false;

                for (int v = 0; v < drum::numVoices; ++v)
                {
                    if (staffIsHand[v] != up)
                        continue;
                    const int val = owner.engine.cell (v, s).load();
                    if (val == 0)
                        continue;
                    any = true;
                    if (val == 2)
                        acc = true;
                    const float y = yOf (staffPos[v]);
                    drawHead (xOf (s), y, staffXHead[v], val == 3);
                    extremeY = up ? juce::jmax (extremeY, y) : juce::jmin (extremeY, y);
                }
                if (any)
                    cols[numCols++] = { s, extremeY, acc };
            }
            if (numCols == 0)
                continue;

            const float beamY = up ? beamYH : beamYF;
            auto stemX = [&] (int s) { return up ? xOf (s) + 5.2f : xOf (s) - 5.2f; };

            g.setColour (ink);
            for (int c = 0; c < numCols; ++c)
            {
                g.drawLine (stemX (cols[c].s), cols[c].noteY + (up ? -2.0f : 2.0f),
                            stemX (cols[c].s), beamY, 1.5f);
                if (cols[c].accent)
                {
                    g.setColour (ui::glowOrange);
                    g.setFont (ui::uiFont (13.0f, true));
                    g.drawText (">", (int) xOf (cols[c].s) - 8,
                                (int) (up ? beamY - 20.0f : beamY + 4.0f), 16, 16,
                                juce::Justification::centred);
                    g.setColour (ink);
                }
            }

            if (numCols > 1)
            {
                const float y = up ? beamY : beamY - 3.2f;
                g.fillRect (stemX (cols[0].s), y,
                            stemX (cols[numCols - 1].s) - stemX (cols[0].s), 3.2f);
                for (int c = 0; c < numCols - 1; ++c)
                    if (cols[c + 1].s - cols[c].s == 1)
                        g.fillRect (stemX (cols[c].s), up ? beamY + 5.0f : beamY - 8.2f,
                                    stemX (cols[c + 1].s) - stemX (cols[c].s), 3.2f);
            }
            else
            {
                juce::Path flag;
                const float x = stemX (cols[0].s), dir = up ? 1.0f : -1.0f;
                flag.startNewSubPath (x, beamY);
                flag.quadraticTo (x + 8.0f, beamY + 5.0f * dir, x + 4.0f, beamY + 14.0f * dir);
                g.strokePath (flag, juce::PathStrokeType (1.6f));
            }
        }
    }

    // legenda
    g.setFont (ui::monoFont (8.5f));
    g.setColour (ui::textMuted);
    g.drawText (juce::String (juce::CharPointer_UTF8 (
                    "clique para editar \xc2\xb7 hastes \xe2\x86\x91 m\xc3\xa3os \xc2\xb7 "
                    "\xe2\x86\x93 p\xc3\xa9s \xc2\xb7 \xc3\x97 pratos \xc2\xb7 > acento \xc2\xb7 "
                    "( ) ghost")),
                40, 4, getWidth() - 60, 12, juce::Justification::centredLeft);
}

void DrumOverlay::ScoreView::mouseDown (const juce::MouseEvent& e)
{
    // mesma geometria do paint: x -> step, y -> voz mais próxima na pauta
    const float SP = 7.0f, top = 46.0f;
    auto yOf = [&] (float pos) { return top + 8.0f * SP - pos * SP; };

    int step = -1;
    for (int s = 0; s < drum::numSteps; ++s)
        if (std::abs ((float) e.x - ((float) stepX (s) + cellW * 0.5f)) <= cellW * 0.5f + 1.0f)
        {
            step = s;
            break;
        }
    if (step < 0)
        return;

    int voice = -1;
    float best = 8.0f; // tolerância de meia posição e pouco
    for (int v = 0; v < drum::numVoices; ++v)
    {
        const float d = std::abs ((float) e.y - yOf (staffPos[v]));
        if (d < best)
        {
            best = d;
            voice = v;
        }
    }
    if (voice < 0)
        return;

    auto& cell = owner.engine.cell (voice, step);
    cell.store ((juce::uint8) ((cell.load() + 1) % 4));
    repaint();
    owner.gridView.repaint();
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
