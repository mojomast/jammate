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

    sortCombo.addItem ("Em alta", 1);
    sortCombo.addItem ("Mais recentes", 2);
    sortCombo.addItem ("Mais baixados", 3);
    sortCombo.setSelectedId (1, juce::dontSendNotification);
    sortCombo.setColour (juce::ComboBox::backgroundColourId, ui::panel);
    sortCombo.setColour (juce::ComboBox::outlineColourId, ui::panelBorder);
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

void StoreOverlay::open()
{
    client.reloadConfig();
    updateHeaderState();
    setVisible (true);
    toFront (true);

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
        [this] (ToneCardComponent& c)
        {
            c.setStatus (ToneCardComponent::Status::downloading);
            const auto& ci = c.getInfo();
            // Roteia por FORMATO (não por gear): existem tones com gear "cab"
            // cujo formato é IR, por exemplo.
            const juce::String kind = ci.formatBadge == "IR" ? "ir" : "nam";
            client.downloadTone (ci.toneId, kind,
                [safe = juce::Component::SafePointer<ToneCardComponent> (&c)] (int pct)
                {
                    if (safe != nullptr)
                        safe->setProgress (pct);
                },
                [this, safe = juce::Component::SafePointer<ToneCardComponent> (&c)] (juce::File file, juce::String error)
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
                    safe->setStatus (ToneCardComponent::Status::inRig);
                    if (safe->getInfo().formatBadge == "IR")
                        processor.loadIrAsync (file);
                    else
                        processor.loadModelAsync (file);
                });
        }));
    gridContent.addAndMakeVisible (card);

    if (info.imageUrl.isNotEmpty())
        client.fetchImage (info.imageUrl,
            [safe = juce::Component::SafePointer<ToneCardComponent> (card)] (juce::Image img)
            {
                if (safe != nullptr)
                    safe->setImage (std::move (img));
            });
}

void StoreOverlay::doSearch (int page)
{
    if (! client.isConnected() || searching)
        return;

    searching = true;
    currentPage = page;

    client.searchTones (searchBox.getText().trim(), gearFilter, sortValue, page,
        [safe = juce::Component::SafePointer<StoreOverlay> (this), page] (Tone3000Client::SearchResult result)
        {
            if (safe == nullptr)
                return;
            auto* self = safe.getComponent();
            self->searching = false;

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
    g.drawText ("ORDENAR", sortCombo.getX() - 70, 72, 62, 32, juce::Justification::centredRight);

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
