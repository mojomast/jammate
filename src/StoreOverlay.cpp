#include "StoreOverlay.h"

#include "PluginCatalog.h"
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

// Label for the FILTER chips ("" = no filter -> "All").
juce::String gearChipLabel (const juce::String& gear)
{
    if (gear == "amp") return "Amp";
    if (gear == "amp-cab") return "Amp+Cab";
    if (gear == "pedal") return "Pedal";
    if (gear == "full-rig") return "Full Rig";
    if (gear == "ir") return "IR";
    return "All";
}

// Type label shown ON THE CARD (fallback: raw capitalized value, for
// gear values the API may add in the future).
juce::String gearDisplay (const juce::String& gear)
{
    if (gear.isEmpty())
        return "Gear";
    const auto known = gearChipLabel (gear);
    if (known != "All")
        return known;
    return gear.substring (0, 1).toUpperCase() + gear.substring (1);
}
} // namespace

//==============================================================================
ToneCardComponent::ToneCardComponent (Info cardInfo, std::function<void (ToneCardComponent&)> onAdd)
    : info (std::move (cardInfo))
{
    addButton.getProperties().set ("outlineAccent", true);
    addButton.setButtonText ("Add");
    addButton.onClick = [this, onAdd = std::move (onAdd)] { onAdd (*this); };
    addAndMakeVisible (addButton);

    // star favorite (only for TONE3000 tones, not local files)
    favButton.getProperties().set ("chip", true);
    favButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")));
    favButton.setTooltip ("Favorite");
    favButton.setMouseClickGrabsKeyboardFocus (false);
    favButton.onClick = [this]
    {
        if (onToggleFavorite)
            onToggleFavorite (*this);
    };
    favButton.setVisible (info.toneId != 0);
    addChildComponent (favButton);
    favButton.setVisible (info.toneId != 0);

    setStatus (Status::add);
}

void ToneCardComponent::setFavorite (bool fav)
{
    favorite = fav;
    favButton.setButtonText (juce::String (juce::CharPointer_UTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86")));
    favButton.getProperties().set ("chipActive", fav);
    favButton.repaint();
}

void ToneCardComponent::setStatus (Status s)
{
    status = s;
    switch (status)
    {
        case Status::add:
            addButton.setButtonText ("Add");
            addButton.setEnabled (true);
            break;
        case Status::downloading:
            addButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Downloading\xe2\x80\xa6 "))
                                     + juce::String (progress) + "%");
            addButton.setEnabled (false);
            break;
        case Status::inRig:
            addButton.setButtonText ("In rig");
            addButton.setEnabled (false);
            break;
    }
    // In the "In rig" state the green visual is painted in paint(); the button disappears.
    addButton.setVisible (status != Status::inRig);
    repaint();
}

void ToneCardComponent::setProgress (int pct)
{
    progress = juce::jlimit (0, 100, pct);
    if (status == Status::downloading)
        addButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Downloading\xe2\x80\xa6 "))
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
    favButton.setBounds (8, 8, 30, 26); // top-left (NAM/A2 badges sit on the right)
}

void ToneCardComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setGradientFill ({ juce::Colour (0xff25272b), 0.0f, b.getY(),
                         juce::Colour (0xff1c1e21), 0.0f, b.getBottom(), false });
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (juce::Colour (0xff303338));
    g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);

    // ---- header: tone image, or hatched as placeholder (130 px)
    auto header = getLocalBounds().withHeight (130);
    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 130.0f, 12.0f);
        g.reduceClipRegion (clip);

        if (image.isValid())
        {
            // fills the header keeping aspect ratio (centered crop)
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

        // type chip (top left)
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

        // badges (top right): format and, if there are A2 models, "A2"
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

        // offline ok (bottom left)
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

        // download overlay with progress ring
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

    // ---- body
    const int pad = 12;
    g.setFont (ui::uiFont (14.0f, true));
    g.setColour (juce::Colour (0xffe9eaec));
    g.drawFittedText (info.title, pad, 140, getWidth() - pad * 2, 34,
                      juce::Justification::topLeft, 2);

    g.setFont (ui::uiFont (11.0f));
    g.setColour (juce::Colour (0xff8a8d93));
    g.drawText (info.creator.isNotEmpty() ? "by " + info.creator : juce::String ("local file"),
                pad, 176, getWidth() - pad * 2, 14, juce::Justification::centredLeft);

    // metrics (downloads / favorites)
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

    // "In rig" button gets a green check over the disabled style
    if (status == Status::inRig)
    {
        auto btn = getLocalBounds().reduced (12).removeFromBottom (34).toFloat();
        g.setColour (ui::green.withAlpha (0.12f));
        g.fillRoundedRectangle (btn, 8.0f);
        g.setColour (ui::green.withAlpha (0.5f));
        g.drawRoundedRectangle (btn, 8.0f, 1.0f);
        g.setFont (ui::uiFont (12.5f, true));
        g.setColour (juce::Colour (0xff5fe0a0));
        g.drawText ("In rig", btn, juce::Justification::centred);
    }
}

//==============================================================================
// Plugin manager row (Plugins tab): status + INSTALL/UNINSTALL toggle
// with progress, fed by the embedded catalog.
class PluginCatalogRow : public juce::Component
{
public:
    PluginCatalogRow (const plugcat::Entry& e, GuitarRigNAMProcessor& proc)
        : entry (e), processor (proc)
    {
        actionButton.getProperties().set ("outlineAccent", true);
        actionButton.setMouseClickGrabsKeyboardFocus (false);
        actionButton.onClick = [this] { act(); };
        addAndMakeVisible (actionButton);
        refresh();
    }

    void refresh()
    {
        installed = plugcat::isInstalled (entry);

        if (busy)
        {
            actionButton.setButtonText (uninstalling
                                            ? juce::String (juce::CharPointer_UTF8 ("Removing\xe2\x80\xa6"))
                                            : juce::String (juce::CharPointer_UTF8 ("Downloading\xe2\x80\xa6 "))
                                                  + juce::String (pct) + "%");
            actionButton.setEnabled (false);
        }
        else if (installed)
        {
            const bool possible = plugcat::canUninstall (entry);
            actionButton.setButtonText ("UNINSTALL");
            actionButton.setEnabled (possible);
            actionButton.setTooltip (possible
                ? juce::String ("Removes the plugin's files")
                : juce::String ("Installed in the system folder - remove with the installer/admin"));
        }
        else
        {
            actionButton.setButtonText ("INSTALL");
            actionButton.setEnabled (true);
            actionButton.setTooltip (juce::String ("Downloads from the official release and installs without admin"));
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff222529));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (juce::Colour (0xff303338));
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

        // status dot
        g.setColour (installed ? ui::green : ui::textMuted);
        g.fillEllipse (18.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);

        g.setFont (ui::uiFont (14.0f, true));
        g.setColour (ui::textBright);
        g.drawText (juce::String (juce::CharPointer_UTF8 (entry.name)),
                    38, 10, getWidth() - 240, 20, juce::Justification::centredLeft);

        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        juce::String info = juce::String (juce::CharPointer_UTF8 (entry.category))
                            + dot + entry.license;
        if (juce::String (entry.version).isNotEmpty())
            info += dot + "v" + entry.version;
        if (entry.sizeMB > 0)
            info += dot + juce::String (entry.sizeMB) + " MB";
        if (installed)
        {
            const auto instVer = plugcat::installedVersion (entry);
            info += dot + (instVer.isNotEmpty()
                               ? "installed (v" + instVer + ")"
                               : juce::String ("installed"));
        }
        g.setFont (ui::monoFont (9.5f));
        g.setColour (ui::textFaint);
        g.drawText (info, 38, 32, getWidth() - 240, 16, juce::Justification::centredLeft);

        if (lastError.isNotEmpty())
        {
            g.setColour (ui::red);
            g.setFont (ui::monoFont (9.0f));
            g.drawText (lastError, 38, getHeight() - 16, getWidth() - 240, 12,
                        juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        actionButton.setBounds (getWidth() - 12 - 170, (getHeight() - 32) / 2, 170, 32);
    }

private:
    void act()
    {
        if (busy)
            return;
        lastError.clear();

        if (! installed)
        {
            busy = true;
            uninstalling = false;
            pct = 0;
            refresh();
            plugcat::installAsync (
                entry,
                [safe = juce::Component::SafePointer<PluginCatalogRow> (this)] (int p)
                {
                    if (safe != nullptr)
                    {
                        safe->pct = p;
                        safe->refresh();
                    }
                },
                [safe = juce::Component::SafePointer<PluginCatalogRow> (this)]
                (bool ok, juce::String msg)
                {
                    if (safe == nullptr)
                        return;
                    safe->busy = false;
                    if (! ok)
                        safe->lastError = msg;
                    safe->refresh();
                });
            return;
        }

        // UNINSTALL: releases the slots using this plugin, waits for the module
        // to unload and deletes the files
        auto bundles = plugcat::installedBundles (entry);
        if (bundles.isEmpty())
            bundles.add (entry.checkBundle);
        for (int s = 0; s < GuitarRigNAMProcessor::maxExtSlots; ++s)
        {
            const auto path = processor.getExternalPluginPath (s);
            for (const auto& bn : bundles)
                if (path.isNotEmpty() && path.containsIgnoreCase (bn))
                    processor.clearExternalPlugin (s);
        }

        busy = true;
        uninstalling = true;
        refresh();
        juce::Timer::callAfterDelay (800,
            [safe = juce::Component::SafePointer<PluginCatalogRow> (this)]
            {
                if (safe == nullptr)
                    return;
                safe->processor.collectExternalRetired(); // ensures unload
                juce::String err;
                if (! plugcat::uninstall (safe->entry, err))
                    safe->lastError = err;
                safe->busy = false;
                safe->uninstalling = false;
                safe->refresh();
            });
    }

    plugcat::Entry entry;
    GuitarRigNAMProcessor& processor;
    juce::TextButton actionButton;
    bool installed = false, busy = false, uninstalling = false;
    int pct = 0;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalogRow)
};

//==============================================================================
StoreOverlay::StoreOverlay (GuitarRigNAMProcessor& p)
    : processor (p)
{
    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);

    exploreTab.getProperties().set ("tab", true);
    libraryTab.getProperties().set ("tab", true);
    pluginsTab.getProperties().set ("tab", true);
    exploreTab.onClick = [this] { setTab (Tab::explore); };
    libraryTab.onClick = [this] { setTab (Tab::library); };
    pluginsTab.onClick = [this] { setTab (Tab::plugins); };
    addAndMakeVisible (exploreTab);
    addAndMakeVisible (libraryTab);
    addAndMakeVisible (pluginsTab);

    searchBox.setTextToShowWhenEmpty (juce::String (juce::CharPointer_UTF8 (
                                          "Search amps, pedals, creators\xe2\x80\xa6")),
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
        connectButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Waiting for login\xe2\x80\xa6")));
        auto* self = this; // MSVC: 'this' in a nested lambda init-capture resolves wrong
        client.connect ([safe = juce::Component::SafePointer<StoreOverlay> (self)] (bool ok, juce::String error)
        {
            if (safe == nullptr)
                return;
            safe->connectButton.setEnabled (true);
            safe->connectButton.setButtonText ("Connect TONE3000");
            safe->bannerError = ok ? juce::String() : "Failed to connect: " + error;
            safe->updateHeaderState();
            if (ok)
                safe->doSearch (1);
            else
                safe->resized();
            safe->repaint();
        });
    };
    addChildComponent (connectButton);

    userChip.setTooltip ("Click to disconnect");
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

    // tags (multi-select; enter as search terms) and A2 filter
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

    loadFavorites();
    favChip.getProperties().set ("chip", true);
    favChip.getProperties().set ("chipActive", false);
    favChip.setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Only tones marked with \xe2\x98\x85")));
    favChip.onClick = [this]
    {
        favOnly = ! favOnly;
        favChip.getProperties().set ("chipActive", favOnly);
        favChip.repaint();
        doSearch (1);
    };
    addAndMakeVisible (favChip);

    a2Chip.getProperties().set ("chip", true);
    a2Chip.getProperties().set ("chipActive", false);
    a2Chip.setTooltip (juce::String ("Only tones with A2-architecture models (more efficient)"));
    a2Chip.onClick = [this]
    {
        a2Only = ! a2Only;
        a2Chip.getProperties().set ("chipActive", a2Only);
        a2Chip.repaint();
        doSearch (1);
    };
    addAndMakeVisible (a2Chip);

    sortCombo.addItem ("Trending", 1);
    sortCombo.addItem ("Newest", 2);
    sortCombo.addItem ("Most downloaded", 3);
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
    // Card statuses only need to track the rig while the store is open.
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
    // captures and IRs: any active lane/slot counts as "in rig"
    for (auto* card : cards)
    {
        if (card->getStatus() == ToneCardComponent::Status::downloading)
            continue;

        const auto file = card->getInfo().localFile;
        if (file == juce::File())
            continue;

        const bool inRig = processor.isModelFileLoaded (file.getFullPathName())
                           || processor.isIrFileLoaded (file.getFullPathName());
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

void StoreOverlay::openOnPlugins()
{
    open();
    setTab (Tab::plugins);
}

void StoreOverlay::setTab (Tab newTab)
{
    tab = newTab;
    exploreTab.getProperties().set ("tabActive", tab == Tab::explore);
    libraryTab.getProperties().set ("tabActive", tab == Tab::library);
    pluginsTab.getProperties().set ("tabActive", tab == Tab::plugins);
    exploreTab.repaint();
    libraryTab.repaint();
    pluginsTab.repaint();

    // search/filters only make sense on the TONE3000 tabs
    const bool toneTabs = tab != Tab::plugins;
    searchBox.setVisible (toneTabs);
    for (auto* chip : gearChips)
        chip->setVisible (toneTabs);
    for (auto* chip : tagChips)
        chip->setVisible (toneTabs);
    a2Chip.setVisible (toneTabs);
    favChip.setVisible (toneTabs);
    sortCombo.setVisible (toneTabs);

    if (tab == Tab::plugins)
    {
        cards.clear();
        loadMoreButton.setVisible (false);
        refreshPluginsTab();
    }
    else
    {
        pluginRows.clear();
        if (tab == Tab::library)
            refreshLibrary();
        else if (client.isConnected())
            doSearch (1);
        else
        {
            cards.clear();
            layoutCards();
        }
    }
    repaint();
}

void StoreOverlay::refreshPluginsTab()
{
    pluginRows.clear();
    const int rowW = 4 * (252 + 16) - 16; // same usable width as the grid
    int y = 0;

    for (const auto& e : plugcat::entries())
    {
        auto* row = pluginRows.add (new PluginCatalogRow (e, processor));
        gridContent.addAndMakeVisible (row);
        row->setBounds (0, y, rowW, 62);
        y += 62 + 10;
    }

    gridContent.setSize (rowW, juce::jmax (y, 1));
    viewport.setViewPosition (0, 0);
}

void StoreOverlay::updateHeaderState()
{
    const bool connected = client.isConnected();
    connectButton.setVisible (! connected && client.hasPublishableKey());
    userChip.setVisible (connected);
    userChip.setButtonText (client.getUsername().isNotEmpty()
                                ? "@" + client.getUsername()
                                : juce::String ("connected"));
}

juce::String StoreOverlay::formatCount (juce::int64 n) const
{
    if (n >= 1000)
        return juce::String ((double) n / 1000.0, 1) + "k";
    return juce::String (n);
}

void StoreOverlay::loadFavorites()
{
    const auto file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                          .getChildFile ("PedalForge NAM")
                          .getChildFile ("favoritos.json");
    favIds.clear();
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (auto* arr = parsed.getProperty ("ids", {}).getArray())
        for (const auto& v : *arr)
            favIds.add (v.toString());
}

void StoreOverlay::saveFavorites() const
{
    auto* obj = new juce::DynamicObject();
    juce::Array<juce::var> arr;
    for (const auto& id : favIds)
        arr.add (id);
    obj->setProperty ("ids", arr);

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PedalForge NAM");
    dir.createDirectory();
    dir.getChildFile ("favoritos.json")
        .replaceWithText (juce::JSON::toString (juce::var (obj), true));
}

void StoreOverlay::toggleFavorite (ToneCardComponent& card)
{
    const auto id = juce::String (card.getInfo().toneId);
    if (favIds.contains (id))
        favIds.removeString (id);
    else
        favIds.add (id);
    saveFavorites();
    card.setFavorite (favIds.contains (id));
}

void StoreOverlay::addCardFor (const Tone3000Client::Tone& tone, bool)
{
    // "Only star" filter (client-side; the API doesn't know our favorites)
    if (favOnly && ! favIds.contains (juce::String (tone.id)))
        return;

    // "A2 only": the API filters with &architecture=2, but lets through tones with
    // no A2 model (IRs, for example) - client-side reinforcement
    if (a2Only && ! tone.hasA2)
        return;

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
    card->setFavorite (favIds.contains (juce::String (tone.id)));
    card->onToggleFavorite = [this] (ToneCardComponent& c) { toggleFavorite (c); };
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

    // Variations already cached: no API call.
    if (const auto it = modelsCache.find (toneId); it != modelsCache.end())
    {
        showModelChoices (card, it->second);
        return;
    }

    // Feedback + guard against double-click while listing the models.
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

    // Several models: choice menu anchored to the card.
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    for (int i = 0; i < (int) models.size(); ++i)
    {
        const auto& m = models[(size_t) i];
        juce::String label = m.name.isNotEmpty() ? m.name
                                                 : "Model " + juce::String (m.id);
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
                safe->setStatus (ToneCardComponent::Status::add); // canceled
                return;
            }
            startDownload (*safe, models[(size_t) (result - 1)]);
        });
}

int StoreOverlay::sizeRank (const juce::String& s)
{
    if (s == "standard") return 0;
    if (s == "lite") return 1;
    if (s == "feather") return 2;
    if (s == "nano") return 3;
    return 0;
}

void StoreOverlay::writeModelMeta (const juce::File& file, const Tone3000Client::Model& m)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("arch", m.arch);
    obj->setProperty ("size", m.size);
    obj->setProperty ("name", m.name);
    juce::File (file.getFullPathName() + ".meta")
        .replaceWithText (juce::JSON::toString (juce::var (obj), true));
}

void StoreOverlay::finalizeNamModel (const juce::File& mainFile, int toneId,
                                     const Tone3000Client::Model& chosen,
                                     const juce::String& baseName)
{
    writeModelMeta (mainFile, chosen);

    // target lane: first free lane; all busy -> replaces the 1st
    const int lane = juce::jmax (0, processor.firstFreeModelLane());

    // ECO pair: same variation (name) AND same architecture, closest lighter
    // size (the list now has A1+A2 with the same name - without the architecture
    // guard the pair could cross A2 with A1)
    const Tone3000Client::Model* partner = nullptr;
    if (const auto it = modelsCache.find (toneId); it != modelsCache.end())
    {
        const int myRank = sizeRank (chosen.size);
        int bestRank = 99;
        for (const auto& m : it->second)
            if (m.name == chosen.name && m.arch == chosen.arch
                && sizeRank (m.size) > myRank && sizeRank (m.size) < bestRank)
            {
                bestRank = sizeRank (m.size);
                partner = &m;
            }
    }

    if (partner == nullptr)
    {
        processor.setModelPair (lane, mainFile, {});
        return;
    }

    const auto ecoBase = baseName + " (eco)";
    const auto ecoLocal = Tone3000Client::localFileForModel (*partner, "nam", ecoBase);
    if (ecoLocal.existsAsFile())
    {
        processor.setModelPair (lane, mainFile, ecoLocal);
        return;
    }

    // downloads the pair silently; meanwhile the main one already plays
    // (captures the processor by pointer - it outlives the editor/overlay)
    processor.setModelPair (lane, mainFile, {});
    const auto partnerCopy = *partner;
    client.downloadModel (partnerCopy, "nam", ecoBase, [] (int) {},
        [proc = &processor, lane, mainFile, partnerCopy] (juce::File ecoFile, juce::String error)
        {
            if (error.isNotEmpty() || ! ecoFile.existsAsFile())
                return; // no eco pair - chip stays disabled
            writeModelMeta (ecoFile, partnerCopy);
            // the user may have changed the lane meanwhile - only
            // complete the pair if the main one is still on it
            if (proc->getModelPathNormal (lane) == mainFile.getFullPathName())
                proc->setModelPair (lane, mainFile, ecoFile);
        });
}

void StoreOverlay::startDownload (ToneCardComponent& card, const Tone3000Client::Model& model)
{
    // Routes by FORMAT (not by gear): there are tones with gear "cab"
    // whose format is IR, for example.
    const juce::String kind = card.getInfo().formatBadge == "IR" ? "ir" : "nam";

    // Readable name: "Tone title - Variation"
    juce::String baseName = card.getInfo().title;
    if (model.name.isNotEmpty() && model.name != baseName)
        baseName += " - " + model.name;
    // A1 and A2 usually have the SAME variation/name - the suffix prevents one
    // from overwriting the other's file
    if (model.arch == "2")
        baseName += " [A2]";

    // Already downloaded before: loads the local file, without using network/API.
    if (const auto local = Tone3000Client::localFileForModel (model, kind, baseName);
        local.existsAsFile())
    {
        card.setLocalFile (local);
        card.setStatus (ToneCardComponent::Status::inRig);
        // ensures the sidecars even without re-download
        client.saveImageSidecar (card.getInfo().imageUrl, local);
        if (kind == "ir")
        {
            writeModelMeta (local, model);
            processor.loadIrAsync (juce::jmax (0, processor.firstFreeIrSlot()), local);
        }
        else
        {
            finalizeNamModel (local, card.getInfo().toneId, model, baseName);
        }
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
        [this, model, baseName, safe = juce::Component::SafePointer<ToneCardComponent> (&card)]
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
            // Optimistic: the status timer corrects if the load fails or if
            // another item enters the rig later.
            safe->setLocalFile (file);
            safe->setStatus (ToneCardComponent::Status::inRig);
            // The tone photo becomes the file's sidecar (rig cards show it).
            client.saveImageSidecar (safe->getInfo().imageUrl, file);
            if (safe->getInfo().formatBadge == "IR")
            {
                writeModelMeta (file, model);
                processor.loadIrAsync (juce::jmax (0, processor.firstFreeIrSlot()), file);
            }
            else
            {
                finalizeNamModel (file, safe->getInfo().toneId, model, baseName);
            }
        });
}

void StoreOverlay::doSearch (int page)
{
    if (! client.isConnected() || searching)
        return;

    searching = true;
    currentPage = page;

    // active tags enter as extra search terms (TONE3000 indexes tags)
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

            // Result arrived after switching to the library - discard
            // (otherwise it overwrites the local cards).
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
                    processor.loadIrAsync (juce::jmax (0, processor.firstFreeIrSlot()), ci.localFile);
                else
                    processor.setModelPair (juce::jmax (0, processor.firstFreeModelLane()),
                                            ci.localFile, {});
                // Deferred: refreshLibrary() destroys the card that originated the
                // click; we can't delete it inside its own onClick.
                auto* self = this; // MSVC: 'this' in a nested lambda init-capture resolves wrong
                juce::MessageManager::callAsync (
                    [safe = juce::Component::SafePointer<StoreOverlay> (self)]
                    {
                        if (safe != nullptr)
                            safe->refreshLibrary();
                    });
            }));

        if (file.getFullPathName() == loadedPath)
            card->setStatus (ToneCardComponent::Status::inRig);

        // photo saved as a sidecar at download
        const juce::File sidecar (file.getFullPathName() + ".img");
        if (sidecar.existsAsFile())
            if (auto img = juce::ImageFileFormat::loadFrom (sidecar); img.isValid())
                card->setImage (std::move (img));

        gridContent.addAndMakeVisible (card);
    };

    for (const auto& f : Tone3000Client::capturesDir().findChildFiles (juce::File::findFiles, false, "*.nam"))
        addLocal (f, "amp", "NAM",
                  processor.isModelFileLoaded (f.getFullPathName()) ? f.getFullPathName()
                                                                    : juce::String());
    for (const auto& f : Tone3000Client::irsDir().findChildFiles (juce::File::findFiles, false,
                                                                  "*.wav;*.aif;*.aiff;*.flac"))
        addLocal (f, "ir", "IR", processor.isIrFileLoaded (f.getFullPathName()) ? f.getFullPathName() : juce::String());

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
    pluginsTab.setBounds (434, 20, 74, 30);

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
    cx += 48; // divider + TAGS label (painted in paint)
    for (auto* chip : tagChips)
    {
        const int w = 22 + 6 * chip->getButtonText().length();
        chip->setBounds (cx, 74, w, 28);
        cx += w + 7;
    }
    cx += 8;
    a2Chip.setBounds (cx, 74, 62, 28);
    favChip.setBounds (cx + 68, 74, 58, 28);
    sortCombo.setBounds (W - 22 - 150, 72, 150, 32);

    // error banner
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

    // ---- filter labels (only on the TONE3000 tabs)
    if (tab != Tab::plugins)
    {
        g.setFont (ui::monoFont (9.0f));
        g.setColour (ui::textMuted);
        g.drawText ("TYPE", 22, 74, 40, 28, juce::Justification::centredLeft);
        if (! tagChips.isEmpty())
        {
            const int divX = tagChips.getFirst()->getX() - 48;
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillRect (divX + 4, 78, 1, 20);
            g.setColour (ui::textMuted);
            g.drawText ("TAGS", divX + 12, 74, 36, 28, juce::Justification::centredLeft);
        }
    }
    else
    {
        g.setFont (ui::monoFont (9.5f));
        g.setColour (ui::textFaint);
        g.drawText (juce::CharPointer_UTF8 (
                        "Embedded catalog \xc2\xb7 installs without admin in "
                        "%LOCALAPPDATA%\\Programs\\Common\\VST3 \xc2\xb7 "
                        "registry in Documents\\PedalForge NAM\\plugins.json"),
                    22, 74, W - 44, 28, juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (0xff1e2023));
    g.fillRect (0, 110, W, 1);

    // ---- error banner
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

    // ---- empty states
    if (tab == Tab::explore)
    {
        if (! client.hasPublishableKey())
        {
            g.setFont (ui::uiFont (16.0f, true));
            g.setColour (juce::Colour (0xffc8cace));
            g.drawText ("Set up your TONE3000 API key", 0, 240, W, 24,
                        juce::Justification::centred);
            g.setFont (ui::uiFont (12.5f));
            g.setColour (juce::Colour (0xff84878d));
            const auto steps =
                juce::String ("1. Create an account at tone3000.com and generate a key in Settings > API Keys\n")
                + "2. Register the redirect: " + Tone3000Client::redirectUri() + "\n"
                + "3. Paste the key (t3k_pub_...) in " + client.configFile().getFullPathName() + "\n"
                + "4. Close and reopen the Tone Store";
            g.drawFittedText (steps, 120, 276, W - 240, 90, juce::Justification::centredTop, 5);
        }
        else if (! client.isConnected())
        {
            g.setFont (ui::uiFont (16.0f, true));
            g.setColour (juce::Colour (0xffc8cace));
            g.drawText ("Connect your TONE3000 account to explore the library", 0, 250, W, 24,
                        juce::Justification::centred);
            g.setFont (ui::uiFont (12.5f));
            g.setColour (juce::Colour (0xff84878d));
            g.drawText ("Login opens in your browser; come back here after authorizing.",
                        0, 278, W, 20, juce::Justification::centred);
        }
        else if (cards.isEmpty() && ! searching)
        {
            g.setFont (ui::uiFont (16.0f, true));
            g.setColour (juce::Colour (0xffc8cace));
            g.drawText ("No tones found", 0, 250, W, 24, juce::Justification::centred);
            g.setFont (ui::uiFont (12.5f));
            g.setColour (juce::Colour (0xff84878d));
            g.drawText ("Try other terms or remove the active filters.",
                        0, 278, W, 20, juce::Justification::centred);
        }
        else if (searching && cards.isEmpty())
        {
            g.setFont (ui::uiFont (14.0f, true));
            g.setColour (juce::Colour (0xff84878d));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("Searching\xe2\x80\xa6")),
                        0, 250, W, 24, juce::Justification::centred);
        }
    }
    else if (cards.isEmpty())
    {
        g.setFont (ui::uiFont (16.0f, true));
        g.setColour (juce::Colour (0xffc8cace));
        g.drawText ("Your library is empty", 0, 250, W, 24, juce::Justification::centred);
        g.setFont (ui::uiFont (12.5f));
        g.setColour (juce::Colour (0xff84878d));
        g.drawText ("Download tones in the Explore tab or copy .nam files to "
                    + Tone3000Client::capturesDir().getFullPathName(),
                    60, 278, W - 120, 20, juce::Justification::centred);
    }
}
