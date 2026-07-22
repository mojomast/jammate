#include "StoreOverlay.h"

#include "PluginProcessor.h"

namespace
{
juce::String gearLabel (const juce::String& gear)
{
    if (gear == "amp") return "AMP HEAD";
    if (gear == "amp-cab") return "AMP + CAB";
    if (gear == "pedal") return "STOMPBOX";
    if (gear == "full-rig") return "FULL RIG";
    if (gear == "ir") return "IMPULSE RESPONSE";
    if (gear == "outboard") return "OUTBOARD";
    return gear.isEmpty() ? juce::String ("GEAR") : gear.toUpperCase();
}

// Rótulo dos chips de FILTRO ("" = sem filtro -> "Tudo").
juce::String gearChipLabel (const juce::String& gear)
{
    if (gear == "amp") return "Amp";
    if (gear == "amp-cab") return "Amp+Cab";
    if (gear == "pedal") return "Pedal";
    if (gear == "full-rig") return "Full Rig";
    if (gear == "ir") return "IR";
    return "Tudo";
}

// Rótulo de tipo exibido NO CARTÃO (fallback: valor cru capitalizado, para
// valores de gear que a API adicionar no futuro).
juce::String gearDisplay (const juce::String& gear)
{
    if (gear.isEmpty())
        return "Gear";
    const auto known = gearChipLabel (gear);
    if (known != "Tudo")
        return known;
    return gear.substring (0, 1).toUpperCase() + gear.substring (1);
}
} // namespace

//==============================================================================
ToneCardComponent::ToneCardComponent (Info cardInfo, std::function<void (ToneCardComponent&)> onAdd)
    : info (std::move (cardInfo))
{
    addButton.getProperties().set ("outlineAccent", true);
    addButton.setButtonText ("Adicionar");
    addButton.onClick = [this, onAdd = std::move (onAdd)] { onAdd (*this); };
    addAndMakeVisible (addButton);
    setStatus (Status::add);
}

void ToneCardComponent::setStatus (Status s)
{
    status = s;
    switch (status)
    {
        case Status::add:
            addButton.setButtonText ("Adicionar");
            addButton.setEnabled (true);
            break;
        case Status::downloading:
            addButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Baixando\xe2\x80\xa6 "))
                                     + juce::String (progress) + "%");
            addButton.setEnabled (false);
            break;
        case Status::inRig:
            addButton.setButtonText ("No rig");
            addButton.setEnabled (false);
            break;
    }
    // No estado "No rig" o visual verde é pintado no paint(); o botão some.
    addButton.setVisible (status != Status::inRig);
    repaint();
}

void ToneCardComponent::setProgress (int pct)
{
    progress = juce::jlimit (0, 100, pct);
    if (status == Status::downloading)
        addButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Baixando\xe2\x80\xa6 "))
                                 + juce::String (progress) + "%");
    repaint();
}

void ToneCardComponent::setImage (juce::Image newImage)
{
    image = std::move (newImage);
    repaint();
}

void ToneCardComponent::setLocalFile (const juce::File& file)
{
    info.localFile = file;
    info.offline = true;
    repaint();
}

void ToneCardComponent::resized()
{
    addButton.setBounds (getLocalBounds().reduced (12).removeFromBottom (34));
}

void ToneCardComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setGradientFill ({ juce::Colour (0xff25272b), 0.0f, b.getY(),
                         juce::Colour (0xff1c1e21), 0.0f, b.getBottom(), false });
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (juce::Colour (0xff303338));
    g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);

    // ---- header: imagem do tone, ou hachurado como placeholder (130 px)
    auto header = getLocalBounds().withHeight (130);
    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 130.0f, 12.0f);
        g.reduceClipRegion (clip);

        if (image.isValid())
        {
            // preenche o header mantendo proporção (crop centralizado)
            const float scale = juce::jmax ((float) getWidth() / (float) image.getWidth(),
                                            130.0f / (float) image.getHeight());
            const float dw = image.getWidth() * scale, dh = image.getHeight() * scale;
            g.drawImage (image, juce::Rectangle<float> (((float) getWidth() - dw) / 2.0f,
                                                        (130.0f - dh) / 2.0f, dw, dh),
                         juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (juce::Colour (0xff2b2e33));
            g.fillRect (header);
            g.setColour (juce::Colour (0xff26282d));
            for (float x = -130.0f; x < (float) getWidth() + 130.0f; x += 18.0f)
            {
                juce::Path stripe;
                stripe.addQuadrilateral (x, 130.0f, x + 130.0f, 0.0f, x + 139.0f, 0.0f, x + 9.0f, 130.0f);
                g.fillPath (stripe);
            }
        }
        g.restoreState();

        if (image.isNull())
        {
            g.setFont (ui::monoFont (9.0f));
            g.setColour (juce::Colour (0xff5a5d63));
            g.drawText (gearLabel (info.gear), header, juce::Justification::centred);
        }

        // chip do tipo (topo esquerdo)
        const auto typeText = gearDisplay (info.gear);
        g.setFont (ui::monoFont (9.0f, true));
        const int tw = 14 + 6 * typeText.length();
        auto typeChip = juce::Rectangle<float> (9.0f, 9.0f, (float) tw, 17.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (typeChip, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (typeChip, 5.0f, 1.0f);
        g.setColour (juce::Colour (0xffc8cace));
        g.drawText (typeText, typeChip, juce::Justification::centred);

        // badges (topo direito): formato e, se houver modelos A2, "A2"
        {
            float badgeX = b.getWidth() - 9.0f;
            auto drawBadge = [&] (const juce::String& text)
            {
                const float bw = 14.0f + 6.5f * (float) text.length();
                badgeX -= bw;
                auto badge = juce::Rectangle<float> (badgeX, 9.0f, bw, 17.0f);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.fillRoundedRectangle (badge, 5.0f);
                g.setColour (ui::accent);
                g.drawRoundedRectangle (badge, 5.0f, 1.0f);
                g.setFont (ui::monoFont (9.0f, true));
                g.drawText (text, badge, juce::Justification::centred);
                badgeX -= 5.0f;
            };
            if (info.formatBadge.isNotEmpty())
                drawBadge (info.formatBadge);
            if (info.a2)
                drawBadge ("A2");
        }

        // offline ok (base esquerda)
        if (info.offline && status != Status::downloading)
        {
            auto tag = juce::Rectangle<float> (9.0f, 130.0f - 9.0f - 17.0f, 74.0f, 17.0f);
            g.setColour (ui::green.withAlpha (0.14f));
            g.fillRoundedRectangle (tag, 5.0f);
            g.setColour (ui::green.withAlpha (0.4f));
            g.drawRoundedRectangle (tag, 5.0f, 1.0f);
            g.setColour (juce::Colour (0xff5fe0a0));
            g.fillEllipse (tag.getX() + 7.0f, tag.getCentreY() - 2.5f, 5.0f, 5.0f);
            g.setFont (ui::monoFont (8.5f, true));
            g.drawText ("offline ok", tag.withTrimmedLeft (14.0f), juce::Justification::centred);
        }

        // overlay de download com anel de progresso
        if (status == Status::downloading)
        {
            g.setColour (juce::Colour (0xff0f1012).withAlpha (0.74f));
            g.fillRect (header);

            const auto centre = header.getCentre().toFloat();
            juce::Path ring;
            ring.addCentredArc (centre.x, centre.y, 26.0f, 26.0f, 0.0f,
                                0.0f, juce::MathConstants<float>::twoPi, true);
            g.setColour (juce::Colours::white.withAlpha (0.09f));
            g.strokePath (ring, juce::PathStrokeType (7.0f));

            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, 26.0f, 26.0f, 0.0f, 0.0f,
                               juce::MathConstants<float>::twoPi * (float) progress / 100.0f, true);
            g.setColour (ui::accent);
            g.strokePath (arc, juce::PathStrokeType (7.0f));

            g.setFont (ui::monoFont (11.0f, true));
            g.setColour (juce::Colour (0xffffcf8a));
            g.drawText (juce::String (progress) + "%", header, juce::Justification::centred);
        }
    }

    // ---- corpo
    const int pad = 12;
    g.setFont (ui::uiFont (14.0f, true));
    g.setColour (juce::Colour (0xffe9eaec));
    g.drawFittedText (info.title, pad, 140, getWidth() - pad * 2, 34,
                      juce::Justification::topLeft, 2);

    g.setFont (ui::uiFont (11.0f));
    g.setColour (juce::Colour (0xff8a8d93));
    g.drawText (info.creator.isNotEmpty() ? "por " + info.creator : juce::String ("arquivo local"),
                pad, 176, getWidth() - pad * 2, 14, juce::Justification::centredLeft);

    // métricas (downloads / favoritos)
    if (info.downloads.isNotEmpty())
    {
        const int my = getHeight() - 12 - 34 - 24;
        juce::Path tri;
        tri.addTriangle ((float) pad, (float) my + 4, (float) pad + 8, (float) my + 4,
                         (float) pad + 4, (float) my + 10);
        g.setColour (juce::Colour (0xff6f7278));
        g.fillPath (tri);
        g.setFont (ui::monoFont (11.0f));
        g.setColour (ui::textDim);
        g.drawText (info.downloads, pad + 13, my, 60, 14, juce::Justification::centredLeft);

        juce::Path diamond;
        diamond.addRectangle (-4.0f, -4.0f, 8.0f, 8.0f);
        diamond.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f)
                                    .translated ((float) pad + 86.0f, (float) my + 7.0f));
        g.setColour (ui::textDim);
        g.fillPath (diamond);
        g.drawText (info.favorites, pad + 96, my, 60, 14, juce::Justification::centredLeft);
    }

    // botão "No rig" ganha um check verde por cima do estilo desabilitado
    if (status == Status::inRig)
    {
        auto btn = getLocalBounds().reduced (12).removeFromBottom (34).toFloat();
        g.setColour (ui::green.withAlpha (0.12f));
        g.fillRoundedRectangle (btn, 8.0f);
        g.setColour (ui::green.withAlpha (0.5f));
        g.drawRoundedRectangle (btn, 8.0f, 1.0f);
        g.setFont (ui::uiFont (12.5f, true));
        g.setColour (juce::Colour (0xff5fe0a0));
        g.drawText ("No rig", btn, juce::Justification::centred);
    }
}

//==============================================================================
StoreOverlay::StoreOverlay (GuitarRigNAMProcessor& p)
    : processor (p)
{
    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);

    exploreTab.getProperties().set ("tab", true);
    libraryTab.getProperties().set ("tab", true);
    exploreTab.onClick = [this] { setTab (Tab::explore); };
    libraryTab.onClick = [this] { setTab (Tab::library); };
    addAndMakeVisible (exploreTab);
    addAndMakeVisible (libraryTab);

    searchBox.setTextToShowWhenEmpty (juce::String (juce::CharPointer_UTF8 (
                                          "Buscar amps, pedais, criadores\xe2\x80\xa6")),
                                      ui::textMuted);
    searchBox.setFont (ui::uiFont (13.0f));
    searchBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff17181a));
    searchBox.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2f3237));
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, ui::accent.withAlpha (0.6f));
    searchBox.setColour (juce::TextEditor::textColourId, ui::text);
    searchBox.onReturnKey = [this] { doSearch (1); };
    searchBox.onEscapeKey = [this] { setVisible (false); };
    addAndMakeVisible (searchBox);

    connectButton.getProperties().set ("accent", true);
    connectButton.onClick = [this]
    {
        connectButton.setEnabled (false);
        connectButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Aguardando login\xe2\x80\xa6")));
        auto* self = this; // MSVC: 'this' em init-capture de lambda aninhada resolve errado
        client.connect ([safe = juce::Component::SafePointer<StoreOverlay> (self)] (bool ok, juce::String error)
        {
            if (safe == nullptr)
                return;
            safe->connectButton.setEnabled (true);
            safe->connectButton.setButtonText ("Conectar TONE3000");
            safe->bannerError = ok ? juce::String() : "Falha ao conectar: " + error;
            safe->updateHeaderState();
            if (ok)
                safe->doSearch (1);
            else
                safe->resized();
            safe->repaint();
        });
    };
    addChildComponent (connectButton);

    userChip.setTooltip ("Clique para desconectar");
    userChip.onClick = [this]
    {
        client.disconnect();
        updateHeaderState();
        repaint();
    };
    addChildComponent (userChip);

    for (const auto* gear : { "", "amp", "amp-cab", "pedal", "full-rig", "ir" })
    {
        auto* chip = gearChips.add (new juce::TextButton (gearChipLabel (gear)));
        chip->getProperties().set ("chip", true);
        chip->getProperties().set ("chipActive", juce::String (gear) == gearFilter);
        const juce::String value (gear);
        chip->onClick = [this, value]
        {
            gearFilter = value;
            for (int i = 0; i < gearChips.size(); ++i)
                gearChips[i]->getProperties().set ("chipActive",
                    gearChips[i]->getButtonText() == gearChipLabel (value));
            repaint();
            doSearch (1);
        };
        addAndMakeVisible (chip);
    }

    // tags (multi-seleção; entram como termos da busca) e filtro A2
    for (const auto* tag : { "metal", "clean", "vintage", "blues", "ambient" })
    {
        auto* chip = tagChips.add (new juce::TextButton (tag));
        chip->getProperties().set ("chip", true);
        chip->getProperties().set ("chipActive", false);
        const juce::String value (tag);
        chip->onClick = [this, value, chip]
        {
            if (activeTags.contains (value))
                activeTags.removeString (value);
            else
                activeTags.add (value);
            chip->getProperties().set ("chipActive", activeTags.contains (value));
            chip->repaint();
            doSearch (1);
        };
        addAndMakeVisible (chip);
    }

    a2Chip.getProperties().set ("chip", true);
    a2Chip.getProperties().set ("chipActive", false);
    a2Chip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "S\xc3\xb3 tones com modelos da arquitetura A2 (mais eficientes)")));
    a2Chip.onClick = [this]
    {
        a2Only = ! a2Only;
        a2Chip.getProperties().set ("chipActive", a2Only);
        a2Chip.repaint();
        doSearch (1);
    };
    addAndMakeVisible (a2Chip);

    sortCombo.addItem ("Em alta", 1);
    sortCombo.addItem ("Mais recentes", 2);
    sortCombo.addItem ("Mais baixados", 3);
    sortCombo.setSelectedId (1, juce::dontSendNotification);
    sortCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff14171b));
    sortCombo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff23272c));
    sortCombo.setColour (juce::ComboBox::textColourId, ui::text);
    sortCombo.setColour (juce::ComboBox::arrowColourId, ui::textMuted);
    sortCombo.onChange = [this]
    {
        sortValue = sortCombo.getSelectedId() == 2 ? "newest"
                  : sortCombo.getSelectedId() == 3 ? "downloads-all-time"
                                                   : "trending";
        doSearch (1);
    };
    addAndMakeVisible (sortCombo);

    retryButton.onClick = [this] { bannerError.clear(); resized(); doSearch (currentPage); };
    dismissButton.onClick = [this] { bannerError.clear(); resized(); repaint(); };
    addChildComponent (retryButton);
    addChildComponent (dismissButton);

    viewport.setViewedComponent (&gridContent, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    loadMoreButton.onClick = [this] { doSearch (currentPage + 1); };
    gridContent.addChildComponent (loadMoreButton);

    setTab (Tab::explore);
}

StoreOverlay::~StoreOverlay() = default;

void StoreOverlay::visibilityChanged()
{
    // Status dos cartões só precisa acompanhar o rig com o store aberto.
    if (isVisible())
        startTimerHz (2);
    else
        stopTimer();
}

void StoreOverlay::timerCallback()
{
    updateRigStatuses();
}

void StoreOverlay::updateRigStatuses()
{
    const auto modelPath = processor.getModelPath();
    const auto irPath = processor.getIrPath();

    for (auto* card : cards)
    {
        if (card->getStatus() == ToneCardComponent::Status::downloading)
            continue;

        const auto file = card->getInfo().localFile;
        if (file == juce::File())
            continue;

        const bool inRig = file.getFullPathName() == modelPath
                           || file.getFullPathName() == irPath;
        const auto wanted = inRig ? ToneCardComponent::Status::inRig
                                  : ToneCardComponent::Status::add;
        if (card->getStatus() != wanted)
            card->setStatus (wanted);
    }
}

bool StoreOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return false;
}

void StoreOverlay::open()
{
    client.reloadConfig();
    updateHeaderState();
    setVisible (true);
    toFront (true);
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();

    if (tab == Tab::library)
        refreshLibrary();
    else if (client.isConnected() && cards.isEmpty())
        doSearch (1);

    repaint();
}

void StoreOverlay::openOnLibrary()
{
    open();
    setTab (Tab::library);
}

void StoreOverlay::setTab (Tab newTab)
{
    tab = newTab;
    exploreTab.getProperties().set ("tabActive", tab == Tab::explore);
    libraryTab.getProperties().set ("tabActive", tab == Tab::library);
    exploreTab.repaint();
    libraryTab.repaint();

    if (tab == Tab::library)
        refreshLibrary();
    else if (client.isConnected())
        doSearch (1);
    else
    {
        cards.clear();
        layoutCards();
    }
    repaint();
}

void StoreOverlay::updateHeaderState()
{
    const bool connected = client.isConnected();
    connectButton.setVisible (! connected && client.hasPublishableKey());
    userChip.setVisible (connected);
    userChip.setButtonText (client.getUsername().isNotEmpty()
                                ? "@" + client.getUsername()
                                : juce::String ("conectado"));
}

juce::String StoreOverlay::formatCount (juce::int64 n) const
{
    if (n >= 1000)
        return juce::String ((double) n / 1000.0, 1) + "k";
    return juce::String (n);
}

void StoreOverlay::addCardFor (const Tone3000Client::Tone& tone, bool)
{
    ToneCardComponent::Info info;
    info.toneId = tone.id;
    info.title = tone.title;
    info.creator = tone.creator;
    info.gear = tone.gear;
    info.formatBadge = tone.format == "ir" ? "IR" : "NAM";
    info.imageUrl = tone.imageUrl;
    info.a2 = tone.hasA2;
    info.downloads = formatCount (tone.downloads);
    info.favorites = formatCount (tone.favorites);

    auto* card = cards.add (new ToneCardComponent (info,
        [this] (ToneCardComponent& c) { startAddFlow (c); }));
    gridContent.addAndMakeVisible (card);

    if (info.imageUrl.isNotEmpty())
        client.fetchImage (info.imageUrl,
            [safe = juce::Component::SafePointer<ToneCardComponent> (card)] (juce::Image img)
            {
                if (safe != nullptr)
                    safe->setImage (std::move (img));
            });
}

void StoreOverlay::startAddFlow (ToneCardComponent& card)
{
    const int toneId = card.getInfo().toneId;

    // Variações já em cache: sem chamada de API.
    if (const auto it = modelsCache.find (toneId); it != modelsCache.end())
    {
        showModelChoices (card, it->second);
        return;
    }

    // Feedback + guarda contra duplo clique enquanto lista os modelos.
    card.setProgress (0);
    card.setStatus (ToneCardComponent::Status::downloading);

    client.listModels (toneId,
        [this, toneId, safe = juce::Component::SafePointer<ToneCardComponent> (&card)]
        (std::vector<Tone3000Client::Model> models, juce::String error)
        {
            if (safe == nullptr)
                return;

            if (error.isNotEmpty())
            {
                safe->setStatus (ToneCardComponent::Status::add);
                bannerError = error;
                resized();
                repaint();
                return;
            }

            modelsCache[toneId] = models;
            showModelChoices (*safe, models);
        });
}

void StoreOverlay::showModelChoices (ToneCardComponent& card,
                                     const std::vector<Tone3000Client::Model>& models)
{
    if (models.size() == 1)
    {
        startDownload (card, models.front());
        return;
    }

    juce::Component::SafePointer<ToneCardComponent> safe (&card);

    // Vários modelos: menu de escolha ancorado no cartão.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    for (int i = 0; i < (int) models.size(); ++i)
    {
        const auto& m = models[(size_t) i];
        juce::String label = m.name.isNotEmpty() ? m.name
                                                 : "Modelo " + juce::String (m.id);
        if (m.arch == "2") label += dot + "A2";
        else if (m.arch == "1") label += dot + "A1";
        if (m.size.isNotEmpty() && m.size != "standard")
            label += dot + m.size;
        menu.addItem (i + 1, label);
    }

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&card),
        [this, safe, models] (int result)
        {
            if (safe == nullptr)
                return;
            if (result <= 0 || result > (int) models.size())
            {
                safe->setStatus (ToneCardComponent::Status::add); // cancelado
                return;
            }
            startDownload (*safe, models[(size_t) (result - 1)]);
        });
}

void StoreOverlay::startDownload (ToneCardComponent& card, const Tone3000Client::Model& model)
{
    // Roteia por FORMATO (não por gear): existem tones com gear "cab"
    // cujo formato é IR, por exemplo.
    const juce::String kind = card.getInfo().formatBadge == "IR" ? "ir" : "nam";

    // Nome legível: "Título do tone - Variação"
    juce::String baseName = card.getInfo().title;
    if (model.name.isNotEmpty() && model.name != baseName)
        baseName += " - " + model.name;

    // Já baixado antes: carrega o arquivo local, sem gastar rede/API.
    if (const auto local = Tone3000Client::localFileForModel (model, kind, baseName);
        local.existsAsFile())
    {
        card.setLocalFile (local);
        card.setStatus (ToneCardComponent::Status::inRig);
        // garante o sidecar de imagem mesmo sem re-download
        client.saveImageSidecar (card.getInfo().imageUrl, local);
        if (kind == "ir")
            processor.loadIrAsync (local);
        else
            processor.loadModelAsync (local);
        return;
    }

    card.setProgress (0);
    card.setStatus (ToneCardComponent::Status::downloading);

    client.downloadModel (model, kind, baseName,
        [safe = juce::Component::SafePointer<ToneCardComponent> (&card)] (int pct)
        {
            if (safe != nullptr)
                safe->setProgress (pct);
        },
        [this, safe = juce::Component::SafePointer<ToneCardComponent> (&card)]
        (juce::File file, juce::String error)
        {
            if (safe == nullptr)
                return;
            if (error.isNotEmpty())
            {
                safe->setStatus (ToneCardComponent::Status::add);
                bannerError = error;
                resized();
                repaint();
                return;
            }
            // Otimista: o timer de status corrige se o load falhar ou se
            // outro item entrar no rig depois.
            safe->setLocalFile (file);
            safe->setStatus (ToneCardComponent::Status::inRig);
            // Foto do tone vira sidecar do arquivo (cartões do rig mostram).
            client.saveImageSidecar (safe->getInfo().imageUrl, file);
            if (safe->getInfo().formatBadge == "IR")
                processor.loadIrAsync (file);
            else
                processor.loadModelAsync (file);
        });
}

void StoreOverlay::doSearch (int page)
{
    if (! client.isConnected() || searching)
        return;

    searching = true;
    currentPage = page;

    // tags ativas entram como termos extras da busca (o TONE3000 indexa tags)
    juce::String query = searchBox.getText().trim();
    for (const auto& tag : activeTags)
        query += " " + tag;

    client.searchTones (query.trim(), gearFilter, sortValue, page, a2Only ? 2 : 0,
        [safe = juce::Component::SafePointer<StoreOverlay> (this), page] (Tone3000Client::SearchResult result)
        {
            if (safe == nullptr)
                return;
            auto* self = safe.getComponent();
            self->searching = false;

            // Resultado chegou depois de trocar para a biblioteca — descarta
            // (senão sobrescreve os cartões locais).
            if (self->tab != Tab::explore)
                return;

            if (result.error.isNotEmpty())
            {
                self->bannerError = result.error;
                self->resized();
                self->repaint();
                return;
            }

            self->bannerError.clear();
            self->totalPages = result.totalPages;
            if (page == 1)
                self->cards.clear();
            for (const auto& tone : result.tones)
                self->addCardFor (tone, true);
            self->layoutCards();
            self->resized();
            self->repaint();
        });
}

void StoreOverlay::refreshLibrary()
{
    cards.clear();

    auto addLocal = [this] (const juce::File& file, const juce::String& gear,
                            const juce::String& badge, const juce::String& loadedPath)
    {
        ToneCardComponent::Info info;
        info.localFile = file;
        info.title = file.getFileNameWithoutExtension();
        info.gear = gear;
        info.formatBadge = badge;
        info.offline = true;

        auto* card = cards.add (new ToneCardComponent (info,
            [this] (ToneCardComponent& c)
            {
                const auto& ci = c.getInfo();
                if (ci.gear == "ir")
                    processor.loadIrAsync (ci.localFile);
                else
                    processor.loadModelAsync (ci.localFile);
                // Deferido: refreshLibrary() destrói o cartão que originou o
                // clique; não podemos deletá-lo dentro do próprio onClick.
                auto* self = this; // MSVC: 'this' em init-capture de lambda aninhada resolve errado
                juce::MessageManager::callAsync (
                    [safe = juce::Component::SafePointer<StoreOverlay> (self)]
                    {
                        if (safe != nullptr)
                            safe->refreshLibrary();
                    });
            }));

        if (file.getFullPathName() == loadedPath)
            card->setStatus (ToneCardComponent::Status::inRig);

        // foto salva como sidecar no download
        const juce::File sidecar (file.getFullPathName() + ".img");
        if (sidecar.existsAsFile())
            if (auto img = juce::ImageFileFormat::loadFrom (sidecar); img.isValid())
                card->setImage (std::move (img));

        gridContent.addAndMakeVisible (card);
    };

    for (const auto& f : Tone3000Client::capturesDir().findChildFiles (juce::File::findFiles, false, "*.nam"))
        addLocal (f, "amp", "NAM", processor.getModelPath());
    for (const auto& f : Tone3000Client::irsDir().findChildFiles (juce::File::findFiles, false,
                                                                  "*.wav;*.aif;*.aiff;*.flac"))
        addLocal (f, "ir", "IR", processor.getIrPath());

    layoutCards();
    resized();
    repaint();
}

void StoreOverlay::layoutCards()
{
    const int cols = 4, gap = 16, cardW = 252, cardH = 300;
    int x = 0, y = 0;

    for (int i = 0; i < cards.size(); ++i)
    {
        const int col = i % cols, row = i / cols;
        x = col * (cardW + gap);
        y = row * (cardH + gap);
        cards[i]->setBounds (x, y, cardW, cardH);
    }

    int contentH = cards.isEmpty() ? 0 : ((cards.size() - 1) / cols + 1) * (cardH + gap);

    const bool showMore = tab == Tab::explore && currentPage < totalPages && ! cards.isEmpty();
    loadMoreButton.setVisible (showMore);
    if (showMore)
    {
        loadMoreButton.setBounds ((cols * (cardW + gap) - gap) / 2 - 90, contentH + 4, 180, 34);
        contentH += 50;
    }

    gridContent.setSize (cols * (cardW + gap) - gap, juce::jmax (contentH, 1));
}

void StoreOverlay::resized()
{
    const int W = getWidth();

    closeButton.setBounds (W - 22 - 34, 15, 34, 34);

    exploreTab.setBounds (208, 20, 76, 30);
    libraryTab.setBounds (296, 20, 130, 30);

    const int chipRightEdge = W - 22 - 34 - 14;
    userChip.setBounds (chipRightEdge - 130, 15, 130, 34);
    connectButton.setBounds (chipRightEdge - 170, 15, 170, 34);
    searchBox.setBounds ((connectButton.isVisible() ? connectButton.getX()
                                                    : userChip.getX()) - 12 - 300, 15, 300, 34);

    // filtros
    int cx = 66;
    for (auto* chip : gearChips)
    {
        const int w = 28 + 7 * chip->getButtonText().length();
        chip->setBounds (cx, 74, w, 28);
        cx += w + 9;
    }
    cx += 48; // divisor + rótulo TAGS (pintados no paint)
    for (auto* chip : tagChips)
    {
        const int w = 22 + 6 * chip->getButtonText().length();
        chip->setBounds (cx, 74, w, 28);
        cx += w + 7;
    }
    cx += 8;
    a2Chip.setBounds (cx, 74, 62, 28);
    sortCombo.setBounds (W - 22 - 150, 72, 150, 32);

    // banner de erro
    const int bannerY = 114;
    const bool banner = bannerError.isNotEmpty();
    retryButton.setVisible (banner);
    dismissButton.setVisible (banner);
    if (banner)
    {
        retryButton.setBounds (W - 22 - 30 - 8 - 130, bannerY + 5, 130, 26);
        dismissButton.setBounds (W - 22 - 30, bannerY + 5, 26, 26);
    }

    const int gridTop = banner ? bannerY + 44 : bannerY + 4;
    viewport.setBounds (22, gridTop, W - 44 + 10, getHeight() - gridTop - 16);
    layoutCards();
}

void StoreOverlay::paint (juce::Graphics& g)
{
    const int W = getWidth();

    g.fillAll (juce::Colour (0xff0b0c0e).withAlpha (0.985f));

    // ---- header
    g.setFont (ui::uiFont (19.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("Tone Store", 22, 18, 120, 30, juce::Justification::centredLeft);

    auto badge = juce::Rectangle<float> (134.0f, 25.0f, 66.0f, 17.0f);
    g.setColour (ui::accent.withAlpha (0.4f));
    g.drawRoundedRectangle (badge, 4.0f, 1.0f);
    g.setFont (ui::monoFont (9.0f, true));
    g.setColour (ui::accent);
    g.drawText ("TONE3000", badge, juce::Justification::centred);

    g.setColour (juce::Colour (0xff24262a));
    g.fillRect (0, 64, W, 1);

    // ---- rótulos dos filtros
    g.setFont (ui::monoFont (9.0f));
    g.setColour (ui::textMuted);
    g.drawText ("TIPO", 22, 74, 40, 28, juce::Justification::centredLeft);
    if (! tagChips.isEmpty())
    {
        const int divX = tagChips.getFirst()->getX() - 48;
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRect (divX + 4, 78, 1, 20);
        g.setColour (ui::textMuted);
        g.drawText ("TAGS", divX + 12, 74, 36, 28, juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (0xff1e2023));
    g.fillRect (0, 110, W, 1);

    // ---- banner de erro
    if (bannerError.isNotEmpty())
    {
        auto banner = juce::Rectangle<float> (22.0f, 114.0f, (float) W - 44.0f, 36.0f);
        g.setColour (ui::red.withAlpha (0.1f));
        g.fillRoundedRectangle (banner, 9.0f);
        g.setColour (ui::red.withAlpha (0.4f));
        g.drawRoundedRectangle (banner, 9.0f, 1.0f);
        g.setColour (ui::red);
        g.fillEllipse (banner.getX() + 14.0f, banner.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setFont (ui::uiFont (12.5f));
        g.setColour (juce::Colour (0xffe8b4ac));
        g.drawText (bannerError, (int) banner.getX() + 32, (int) banner.getY(),
                    retryButton.getX() - (int) banner.getX() - 44, 36,
                    juce::Justification::centredLeft);
    }

    // ---- estados vazios
    if (tab == Tab::explore)
    {
        if (! client.hasPublishableKey())
        {
            g.setFont (ui::uiFont (16.0f, true));
            g.setColour (juce::Colour (0xffc8cace));
            g.drawText ("Configure sua chave da API TONE3000", 0, 240, W, 24,
                        juce::Justification::centred);
            g.setFont (ui::uiFont (12.5f));
            g.setColour (juce::Colour (0xff84878d));
            const auto steps =
                juce::String ("1. Crie uma conta em tone3000.com e gere uma chave em Settings > API Keys\n")
                + "2. Registre o redirect: " + Tone3000Client::redirectUri() + "\n"
                + "3. Cole a chave (t3k_pub_...) em " + client.configFile().getFullPathName() + "\n"
                + "4. Feche e abra o Tone Store novamente";
            g.drawFittedText (steps, 120, 276, W - 240, 90, juce::Justification::centredTop, 5);
        }
        else if (! client.isConnected())
        {
            g.setFont (ui::uiFont (16.0f, true));
            g.setColour (juce::Colour (0xffc8cace));
            g.drawText ("Conecte sua conta TONE3000 para explorar a biblioteca", 0, 250, W, 24,
                        juce::Justification::centred);
            g.setFont (ui::uiFont (12.5f));
            g.setColour (juce::Colour (0xff84878d));
            g.drawText ("O login abre no seu navegador; volte aqui depois de autorizar.",
                        0, 278, W, 20, juce::Justification::centred);
        }
        else if (cards.isEmpty() && ! searching)
        {
            g.setFont (ui::uiFont (16.0f, true));
            g.setColour (juce::Colour (0xffc8cace));
            g.drawText ("Nenhum tone encontrado", 0, 250, W, 24, juce::Justification::centred);
            g.setFont (ui::uiFont (12.5f));
            g.setColour (juce::Colour (0xff84878d));
            g.drawText ("Tente outros termos ou remova os filtros ativos.",
                        0, 278, W, 20, juce::Justification::centred);
        }
        else if (searching && cards.isEmpty())
        {
            g.setFont (ui::uiFont (14.0f, true));
            g.setColour (juce::Colour (0xff84878d));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("Buscando\xe2\x80\xa6")),
                        0, 250, W, 24, juce::Justification::centred);
        }
    }
    else if (cards.isEmpty())
    {
        g.setFont (ui::uiFont (16.0f, true));
        g.setColour (juce::Colour (0xffc8cace));
        g.drawText ("Sua biblioteca esta vazia", 0, 250, W, 24, juce::Justification::centred);
        g.setFont (ui::uiFont (12.5f));
        g.setColour (juce::Colour (0xff84878d));
        g.drawText ("Baixe tones na aba Explorar ou copie arquivos .nam para "
                    + Tone3000Client::capturesDir().getFullPathName(),
                    60, 278, W - 120, 20, juce::Justification::centred);
    }
}
