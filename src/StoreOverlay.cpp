#include "StoreOverlay.h"

#include "PluginCatalog.h"
#include "PluginProcessor.h"
#include "BinaryData.h"

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

// TYPE pill labels (mockup store-tools row 1: ALL / AMP / AMP + CAB / ...).
juce::String gearPillLabel (const juce::String& gear)
{
    if (gear == "amp") return "AMP";
    if (gear == "amp-cab") return "AMP + CAB";
    if (gear == "pedal") return "PEDAL";
    if (gear == "ir") return "IR";
    return "ALL";
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

    // open the tone's page on tone3000.com
    linkButton.getProperties().set ("chip", true);
    linkButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x97")));  // arrow up-right
    linkButton.setTooltip ("Open on tone3000.com");
    linkButton.setMouseClickGrabsKeyboardFocus (false);
    linkButton.onClick = [this]
    {
        auto u = info.toneUrl;
        if (u.isEmpty() && info.toneId != 0)
            u = "https://www.tone3000.com/tones/" + juce::String (info.toneId);
        if (u.startsWith ("/"))
            u = "https://www.tone3000.com" + u;
        if (u.isNotEmpty())
            juce::URL (u).launchInDefaultBrowser();
    };
    addChildComponent (linkButton);
    linkButton.setVisible (info.toneId != 0);

    // vNext: temporary A/B preview into AMP 1 (no commitment)
    previewButton.getProperties().set ("chip", true);
    previewButton.setTooltip ("A/B preview: hear this tone in AMP 1 without changing your rig");
    previewButton.setMouseClickGrabsKeyboardFocus (false);
    previewButton.onClick = [this] { if (onPreview) onPreview (*this); };
    addChildComponent (previewButton);
    previewButton.setVisible (info.toneId != 0);

    // ST1: "load options" dropdown next to the primary action (the same menu
    // opens on right-click anywhere on the card)
    menuButton.getProperties().set ("chip", true);
    menuButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")));
    menuButton.setTooltip ("Load options: replace a specific amp/IR or add as parallel rig");
    menuButton.setMouseClickGrabsKeyboardFocus (false);
    menuButton.onClick = [this] { if (onShowMenu) onShowMenu (*this); };
    addAndMakeVisible (menuButton);

    setStatus (Status::add);
}

void ToneCardComponent::setPrimaryLabel (const juce::String& label)
{
    if (primaryLabel == label)
        return;
    primaryLabel = label;
    if (status == Status::add)
        addButton.setButtonText (primaryLabel);
}

void ToneCardComponent::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onShowMenu)
            onShowMenu (*this);
        return;
    }
    // body click (child buttons consume their own clicks) -> details view
    if (e.mouseWasClicked() && onOpenDetails)
        onOpenDetails (*this);
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
            addButton.setButtonText (primaryLabel);
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

void ToneCardComponent::setAvatar (juce::Image newAvatar)
{
    avatar = std::move (newAvatar);
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
    // mockup .tone-actions: secondary "PREVIEW" + contextual primary + "v" menu
    auto row = getLocalBounds().reduced (12).removeFromBottom (34);
    menuButton.setBounds (row.removeFromRight (26));
    row.removeFromRight (6);
    if (previewButton.isVisible())
    {
        previewButton.setBounds (row.removeFromLeft ((row.getWidth() - 6) / 2));
        row.removeFromLeft (6);
    }
    addButton.setBounds (row);
    favButton.setBounds (8, 8, 30, 26);  // top-left (NAM/A2 badges sit on the right)
    linkButton.setBounds (42, 8, 30, 26); // next to the star
}

void ToneCardComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setGradientFill ({ juce::Colour (0xff25272b), 0.0f, b.getY(),
                         juce::Colour (0xff1c1e21), 0.0f, b.getBottom(), false });
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (juce::Colour (0xff303338));
    g.drawRoundedRectangle (b.reduced (0.5f), 2.0f, 1.0f);

    // ---- header: tone image, or hatched as placeholder (130 px)
    auto header = getLocalBounds().withHeight (130);
    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), 130.0f, 2.0f);
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

        // type chip (top left) - mockup .tone-badges .tag: solid dark backdrop
        // (rgba(7,12,14,.74)) so the text stays readable over any tone photo
        const auto typeText = gearDisplay (info.gear);
        g.setFont (ui::monoFont (9.0f, true));
        const int tw = 14 + 6 * typeText.length();
        auto typeChip = juce::Rectangle<float> (9.0f, 9.0f, (float) tw, 17.0f);
        g.setColour (juce::Colour (0xff070c0e).withAlpha (0.74f));
        g.fillRoundedRectangle (typeChip, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (typeChip, 5.0f, 1.0f);
        g.setColour (juce::Colour (0xffc8cace));
        g.drawText (typeText, typeChip, juce::Justification::centred);

        // badges (top right): NAM architecture (A1/A2) or IR
        {
            float badgeX = b.getWidth() - 9.0f;
            auto drawBadge = [&] (const juce::String& text, bool strong)
            {
                const float bw = 14.0f + 6.5f * (float) text.length();
                badgeX -= bw;
                auto badge = juce::Rectangle<float> (badgeX, 9.0f, bw, 17.0f);
                g.setColour (juce::Colour (0xff070c0e).withAlpha (0.74f));
                g.fillRoundedRectangle (badge, 5.0f);
                g.setColour (strong ? ui::accent : ui::border());
                g.drawRoundedRectangle (badge, 5.0f, 1.0f);
                g.setColour (strong ? ui::accent : ui::textDim);
                g.setFont (ui::monoFont (9.0f, true));
                g.drawText (text, badge, juce::Justification::centred);
                badgeX -= 5.0f;
            };
            if (info.formatBadge == "IR")
                drawBadge ("IR", true);
            else if (info.formatBadge.isNotEmpty())   // NAM: show A1 or A2
                drawBadge (info.a2 ? "A2" : "A1", info.a2);
        }

        // availability tag (bottom left) - vNext copy: "Available offline"
        if (info.offline && status != Status::downloading)
        {
            auto tag = juce::Rectangle<float> (9.0f, 130.0f - 9.0f - 17.0f, 108.0f, 17.0f);
            g.setColour (ui::green.withAlpha (0.14f));
            g.fillRoundedRectangle (tag, 5.0f);
            g.setColour (ui::green.withAlpha (0.4f));
            g.drawRoundedRectangle (tag, 5.0f, 1.0f);
            g.setColour (juce::Colour (0xff5fe0a0));
            g.fillEllipse (tag.getX() + 7.0f, tag.getCentreY() - 2.5f, 5.0f, 5.0f);
            g.setFont (ui::monoFont (8.5f, true));
            g.drawText ("Available offline", tag.withTrimmedLeft (14.0f), juce::Justification::centred);
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

    // creator: username + small round avatar (TONE3000 attribution)
    int creatorX = pad;
    if (info.creator.isNotEmpty() && info.toneId != 0)
    {
        const float av = 16.0f;
        auto avR = juce::Rectangle<float> ((float) pad, 175.0f, av, av);
        if (avatar.isValid())
        {
            juce::Path circ;
            circ.addEllipse (avR);
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (circ);
            g.drawImage (avatar, avR, juce::RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (ui::accent.withAlpha (0.20f));
            g.fillEllipse (avR);
            g.setColour (ui::accent.withAlpha (0.85f));
            g.setFont (ui::uiFont (9.0f, true));
            g.drawText (info.creator.substring (0, 1).toUpperCase(), avR, juce::Justification::centred);
        }
        creatorX = pad + (int) av + 6;
    }
    g.setFont (ui::uiFont (11.0f));
    g.setColour (juce::Colour (0xff8a8d93));
    g.drawText (info.creator.isNotEmpty() ? "by " + info.creator : juce::String ("local file"),
                creatorX, 176, getWidth() - creatorX - pad, 14, juce::Justification::centredLeft);

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
        // matches the (hidden) primary button - preview/menu keep their spots
        auto btn = addButton.getBounds().toFloat();
        g.setColour (ui::green.withAlpha (0.12f));
        g.fillRoundedRectangle (btn, 2.0f);
        g.setColour (ui::green.withAlpha (0.5f));
        g.drawRoundedRectangle (btn, 2.0f, 1.0f);
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
        g.fillRoundedRectangle (b, 2.0f);
        g.setColour (juce::Colour (0xff303338));
        g.drawRoundedRectangle (b.reduced (0.5f), 2.0f, 1.0f);

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
    // Official TONE3000 branding (design requirement): full wordmark for the
    // list-view header, compact T3K mark for tight spots.
    brandLogo = juce::ImageFileFormat::loadFrom (BinaryData::tone3000logo_png,
                                                 (size_t) BinaryData::tone3000logo_pngSize);
    brandMark = juce::ImageFileFormat::loadFrom (BinaryData::t3kmark_png,
                                                 (size_t) BinaryData::t3kmark_pngSize);

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

    // ---- TONE3000 access setup (own key per user) ------------------------
    keyEditor.setTextToShowWhenEmpty ("t3k_pub_...", ui::textMuted);
    keyEditor.setFont (ui::monoFont (12.0f));
    keyEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff17181a));
    keyEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2f3237));
    keyEditor.setColour (juce::TextEditor::focusedOutlineColourId, ui::accent.withAlpha (0.6f));
    keyEditor.setColour (juce::TextEditor::textColourId, ui::text);
    keyEditor.onReturnKey = [this] { saveKeyButton.triggerClick(); };
    addChildComponent (keyEditor);

    getKeyButton.getProperties().set ("chip", true);
    getKeyButton.setTooltip ("Opens tone3000.com > Settings > API Keys in your browser");
    getKeyButton.onClick = [] { juce::URL (Tone3000Client::apiKeysUrl()).launchInDefaultBrowser(); };
    addChildComponent (getKeyButton);

    copyRedirectButton.getProperties().set ("chip", true);
    copyRedirectButton.setTooltip ("Register this exact URI alongside the key");
    copyRedirectButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (Tone3000Client::redirectUri());
        keyNotice = "Redirect URI copied - paste it into the key's Redirect URIs field";
        repaint();
    };
    addChildComponent (copyRedirectButton);

    saveKeyButton.getProperties().set ("accent", true);
    saveKeyButton.onClick = [this]
    {
        const auto key = keyEditor.getText().trim();
        if (! Tone3000Client::looksLikePublishableKey (key))
        {
            keyNotice = "That does not look like a publishable key (it starts with t3k_pub_)";
            repaint();
            return;
        }
        client.setPublishableKey (key);
        keySetupVisible = false;
        keyNotice.clear();
        keyEditor.setText ({}, false);
        updateHeaderState();
        updateKeySetupState();
        repaint();
    };
    addChildComponent (saveKeyButton);

    changeKeyButton.getProperties().set ("ghost", true);
    changeKeyButton.setTooltip ("Use a different TONE3000 key on this machine");
    changeKeyButton.onClick = [this]
    {
        keySetupVisible = true;
        keyNotice.clear();
        keyEditor.setText (client.getPublishableKey(), false);
        updateKeySetupState();
        repaint();
    };
    addChildComponent (changeKeyButton);

    // Free tier's browse path: TONE3000's own picker, scoped by the gear pills.
    browseButton.getProperties().set ("accent", true);
    browseButton.setTooltip ("Pick a tone on tone3000.com and it comes back here");
    browseButton.setMouseClickGrabsKeyboardFocus (false);
    browseButton.onClick = [this]
    {
        if (! client.isConnected())
        {
            setSplashVisible (true);
            return;
        }
        bannerError.clear();
        browseButton.setButtonText ("WAITING FOR YOUR PICK...");
        browseButton.setEnabled (false);
        repaint();

        juce::Component::SafePointer<StoreOverlay> safe (this);
        auto onPicked = [safe] (Tone3000Client::Tone tone, juce::String error)
        {
            if (safe == nullptr)
                return;
            auto* self = safe.getComponent();
            self->browseButton.setButtonText (juce::String (juce::CharPointer_UTF8 (
                "BROWSE ON TONE3000 \xe2\x86\x97")));
            self->browseButton.setEnabled (true);
            if (error.isNotEmpty())
            {
                // "No tone was chosen" just means the picker was closed
                self->bannerError = error == "No tone was chosen" ? juce::String() : error;
                self->resized();
                self->repaint();
                return;
            }
            self->openDetailsFor (tone);
        };

        // Their picker, inside our window. Without WebView2 it still works, in
        // the system browser, exactly as it did before.
        // The architecture filter is handed over to the panel, which lets the
        // user flip it there: TONE3000 only ever shows one architecture, and
        // the tones outside it read as "Not supported" rather than "filtered".
        const auto arch = a2Only ? Tone3000Client::Architecture::a2
                                 : Tone3000Client::Architecture::a1AndCustom;
        if (ToneWebView::isSupported())
        {
            ensureWebView();
            webView->openToneFlow (Tone3000Client::selectToneParams (gearFilter, 0),
                                   "Browse TONE3000", arch, std::move (onPicked));
        }
        else
        {
            client.selectTone (gearFilter, a2Only ? 2 : 0, std::move (onPicked));
        }
    };
    addChildComponent (browseButton);

    connectButton.getProperties().set ("accent", true);
    // First the TONE3000 notice (their design guidance), then the OAuth flow.
    connectButton.onClick = [this] { setSplashVisible (true); };
    addChildComponent (connectButton);

    splashContinue.getProperties().set ("accent", true);
    splashContinue.setMouseClickGrabsKeyboardFocus (false);
    splashContinue.onClick = [this] { setSplashVisible (false); doConnect(); };
    addChildComponent (splashContinue);
    splashCancel.getProperties().set ("chip", true);
    splashCancel.setMouseClickGrabsKeyboardFocus (false);
    splashCancel.onClick = [this] { setSplashVisible (false); };
    addChildComponent (splashCancel);

    userChip.setTooltip ("Click to disconnect");
    userChip.onClick = [this]
    {
        client.disconnect();
        updateHeaderState();
        repaint();
    };
    addChildComponent (userChip);

    // TYPE pills (mockup row 1): always visible, map straight to gearFilter.
    // No "full-rig": it is not one of TONE3000's gear types (amp, amp-cab,
    // pedal, outboard, cab, space, experimental) and the API never returns it -
    // a tone titled "... - Full Rig" is published as amp-cab. The pill always
    // came back empty, so AMP+CAB covers that content instead.
    for (const auto* gear : { "", "amp", "amp-cab", "pedal", "ir" })
    {
        auto* chip = gearChips.add (new juce::TextButton (gearPillLabel (gear)));
        chip->getProperties().set ("chip", true);
        chip->getProperties().set ("gearValue", juce::String (gear));
        chip->getProperties().set ("chipActive", juce::String (gear) == gearFilter);
        const juce::String value (gear);
        chip->onClick = [this, value]
        {
            gearFilter = value;
            for (int i = 0; i < gearChips.size(); ++i)
                gearChips[i]->getProperties().set ("chipActive",
                    gearChips[i]->getProperties() ["gearValue"].toString() == value);
            repaint();
            doSearch (1);
        };
        addAndMakeVisible (chip);
    }

    // NO tag chips (metal/clean/vintage/...). They only ever worked by being
    // appended to the /tones/search query, and that endpoint is full API access
    // - gone on the free tier. Left in place they lit up and did nothing, which
    // made the grid look broken: picking "metal" + AMP still showed the same
    // handful of tones and read as "the store only has 11 amps".
    // Tag search lives in TONE3000's own picker now (BROWSE ON TONE3000).
    // A2 and favourites stay: those two filter client-side and really work.

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

    // "MORE FILTERS" reveals the extra chips (tags / A2 / favorites) on a
    // third tools row (mockup keeps the TYPE pills fixed on row 1)
    filtersChip.getProperties().set ("chip", true);
    filtersChip.setMouseClickGrabsKeyboardFocus (false);
    filtersChip.setTooltip ("More filters: tags, A2 only, favorites");
    filtersChip.onClick = [this]
    {
        filtersOpen = ! filtersOpen;
        filtersChip.getProperties().set ("chipActive", filtersOpen);
        filtersChip.repaint();
        setTab (tab);   // re-applies chip visibility
        resized();      // the extra row moves the banner/grid down
    };
    addAndMakeVisible (filtersChip);

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

    // TONE3000 collections (Explore / Favorites / Created / Downloaded)
    sourceCombo.addItem ("Explore", 1);
    sourceCombo.addItem (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 Favorites")), 2);
    sourceCombo.addItem ("Created", 3);
    sourceCombo.addItem ("Downloaded", 4);
    sourceCombo.setSelectedId (1, juce::dontSendNotification);
    sourceCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff14171b));
    sourceCombo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff23272c));
    sourceCombo.setColour (juce::ComboBox::textColourId, ui::text);
    sourceCombo.setColour (juce::ComboBox::arrowColourId, ui::textMuted);
    sourceCombo.onChange = [this]
    {
        switch (sourceCombo.getSelectedId())
        {
            case 2:  sourceMode = "favorited";  break;
            case 3:  sourceMode = "created";    break;
            case 4:  sourceMode = "downloaded"; break;
            default: sourceMode = "search";     break;
        }
        // sort/search only make sense on the open Explore search
        sortCombo.setEnabled (sourceMode == "search");
        doSearch (1);
    };
    addAndMakeVisible (sourceCombo);

    // vNext: A/B preview bar actions
    keepBtn.getProperties().set ("chip", true);
    keepBtn.setMouseClickGrabsKeyboardFocus (false);
    keepBtn.onClick = [this] { endPreview (false); };
    addChildComponent (keepBtn);
    applyPrevBtn.getProperties().set ("accent", true);
    applyPrevBtn.setMouseClickGrabsKeyboardFocus (false);
    applyPrevBtn.onClick = [this] { endPreview (true); };
    addChildComponent (applyPrevBtn);

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
    {
        startTimerHz (2);
    }
    else
    {
        stopTimer();
        pendingLane = -1;   // the next open() aims at nothing until told to
        // Closing the store abandons any sign-in/picker running inside it -
        // otherwise the client stays reserved and the next attempt is refused
        // with "already in progress".
        if (webView != nullptr)
            webView->cancel();
    }
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
        // contextual primary label tracks the rig (AMP 1 fills up / frees)
        card->setPrimaryLabel (primaryLabelFor (card->getInfo()));

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
        // Sub-overlays close first (browser, splash, details), then the store.
        // The browser panel usually handles Esc itself (it holds focus), but it
        // is checked here too for when focus is elsewhere.
        if (webView != nullptr && webView->isVisible())
            webView->cancel();
        else if (splashVisible)
            setSplashVisible (false);
        else if (detailsView != nullptr && detailsView->isVisible())
            detailsView->setVisible (false);
        else
            setVisible (false);
        return true;
    }
    return false;
}

void StoreOverlay::open()
{
    // Opening the store on its own aims at nothing in particular; openForRig()
    // sets the target after this returns.
    pendingLane = -1;
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

void StoreOverlay::openForRig (int lane)
{
    open();
    setTab (Tab::explore);
    pendingLane = lane;
    updateRigStatuses();   // card labels now say the rig they will land in
    repaint();
}

void StoreOverlay::openOnBrowser()
{
    open();
    setTab (Tab::explore);
    // Same path a click takes, so the flag exercises the real thing.
    if (browseButton.isEnabled())
        browseButton.triggerClick();
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

    // search/filters only make sense on the TONE3000 tabs; the extra filters
    // (tags/A2/favorites) stay collapsed behind "Filters" (clean UI)
    // the setup form owns the screen until there is a key to work with
    const bool toneTabs = tab != Tab::plugins && ! showKeySetup();
    // FREE TIER: no paginated search and no server-side collections/sort.
    searchBox.setVisible (false);
    sourceCombo.setVisible (false);
    sortCombo.setVisible (false);
    browseButton.setVisible (toneTabs && tab == Tab::explore);
    for (auto* chip : gearChips)
        chip->setVisible (toneTabs);
    filtersChip.setVisible (toneTabs);
    updateKeySetupState();
    for (auto* chip : tagChips)
        chip->setVisible (toneTabs && filtersOpen);
    a2Chip.setVisible (toneTabs && filtersOpen);
    favChip.setVisible (toneTabs && filtersOpen);
    // sortCombo / sourceCombo stay hidden: both drive /tones/search and the
    // server-side collections, which are full API access (see doSearch).

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
            countText.clear();
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

bool StoreOverlay::showKeySetup() const
{
    // no key yet -> the setup form IS the explore tab; with a key it only
    // appears when the user asks to change it
    return tab == Tab::explore && (! client.hasPublishableKey() || keySetupVisible);
}

void StoreOverlay::updateKeySetupState()
{
    const bool setup = showKeySetup();
    keyEditor.setVisible (setup);
    getKeyButton.setVisible (setup);
    copyRedirectButton.setVisible (setup);
    saveKeyButton.setVisible (setup);
    // the "change key" affordance lives next to the account chip
    changeKeyButton.setVisible (tab == Tab::explore && client.hasPublishableKey() && ! setup);
    resized();
}

void StoreOverlay::updateHeaderState()
{
    const bool connected = client.isConnected();
    // Reachable as soon as a key exists - it used to be the only path and was
    // hidden behind a JSON file the user had to find and edit by hand.
    connectButton.setVisible (! connected && client.hasPublishableKey() && ! showKeySetup());
    updateKeySetupState();
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

    // TYPE pill. The bounded lists (trending/latest) do not filter by format,
    // and latest is not scoped by gear at all, so the pill is enforced here.
    if (gearFilter == "ir" && tone.format != "ir")
        return;
    if (gearFilter.isNotEmpty() && gearFilter != "ir" && tone.gear != gearFilter)
        return;

    ToneCardComponent::Info info;
    info.toneId = tone.id;
    info.title = tone.title;
    info.creator = tone.creator;
    info.creatorAvatar = tone.creatorAvatar;
    info.gear = tone.gear;
    info.formatBadge = tone.format == "ir" ? "IR" : "NAM";
    info.imageUrl = tone.imageUrl;
    info.toneUrl = tone.url;
    info.a2 = tone.hasA2;
    info.downloads = formatCount (tone.downloads);
    info.favorites = formatCount (tone.favorites);

    // ST1: the primary button loads straight into the rig the store is aimed
    // at (AMP 1 / IR 1 when it is aimed at nothing); the details view opens
    // from a click on the card body.
    // targetLane(), NOT a literal 0: with a hard-coded 0 the button said
    // "REPLACE IR 2" and then replaced cab 1.
    auto* card = cards.add (new ToneCardComponent (info,
        [this] (ToneCardComponent& c) { loadCardIntoLane (c, targetLane()); }));
    card->setPrimaryLabel (primaryLabelFor (info));
    card->setFavorite (favIds.contains (juce::String (tone.id)));
    card->onToggleFavorite = [this] (ToneCardComponent& c) { toggleFavorite (c); };
    card->onPreview = [this] (ToneCardComponent& c) { startPreview (c); };
    card->onOpenDetails = [this] (ToneCardComponent& c) { openDetails (c); };
    card->onShowMenu = [this] (ToneCardComponent& c) { showCardMenu (c); };
    gridContent.addAndMakeVisible (card);

    if (info.imageUrl.isNotEmpty())
        client.fetchImage (info.imageUrl,
            [safe = juce::Component::SafePointer<ToneCardComponent> (card)] (juce::Image img)
            {
                if (safe != nullptr)
                    safe->setImage (std::move (img));
            });

    if (info.creatorAvatar.isNotEmpty())
        client.fetchImage (info.creatorAvatar,
            [safe = juce::Component::SafePointer<ToneCardComponent> (card)] (juce::Image img)
            {
                if (safe != nullptr)
                    safe->setAvatar (std::move (img));
            });
}

void StoreOverlay::openDetailsFor (const Tone3000Client::Tone& tone)
{
    detailsTargetLane = pendingLane;
    ToneCardComponent::Info info;
    info.toneId = tone.id;
    info.title = tone.title;
    info.creator = tone.creator;
    info.creatorAvatar = tone.creatorAvatar;
    info.gear = tone.gear;
    info.formatBadge = tone.format == "ir" ? "IR" : "NAM";
    info.imageUrl = tone.imageUrl;
    info.toneUrl = tone.url;
    info.a2 = tone.hasA2;
    info.downloads = formatCount (tone.downloads);
    info.favorites = formatCount (tone.favorites);

    detailsInfo = info;
    ensureDetailsView();
    detailsView->setBounds (getLocalBounds());
    detailsView->open (detailsInfo);
}

void StoreOverlay::openDetails (ToneCardComponent& card)
{
    // Card click: the variation lands in the rig the store was opened for, or
    // in a free lane when it was opened on its own.
    detailsTargetLane = pendingLane;
    detailsInfo = card.getInfo();
    ensureDetailsView();
    detailsView->setBounds (getLocalBounds());
    detailsView->open (detailsInfo);
}

//==============================================================================
// ST1: contextual card actions (mockup .tone-actions).
juce::String StoreOverlay::primaryLabelFor (const ToneCardComponent::Info& info) const
{
    const int lane = targetLane();
    const juce::String n (lane + 1);

    if (info.formatBadge == "IR")
        return "REPLACE IR " + n;
    return processor.hasModelLoaded (lane) ? "REPLACE AMP " + n : "LOAD IN AMP " + n;
}

void StoreOverlay::loadCardIntoLane (ToneCardComponent& card, int lane)
{
    const auto& info = card.getInfo();

    if (info.toneId == 0)
    {
        // local file (My library): load directly, no API round-trip
        if (info.formatBadge == "IR")
            processor.loadIrAsync (lane, info.localFile);
        else
            processor.setModelPair (lane, info.localFile, {});
        updateRigStatuses();
        return;
    }

    startAddFlow (card, lane);
}

void StoreOverlay::showCardMenu (ToneCardComponent& card)
{
    const auto info = card.getInfo();

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    // item ids: 1..3 = amp lane, 10 = parallel rig, 101..103 = IR slot
    if (info.formatBadge == "IR")
    {
        const int cabs = juce::jlimit (1, GuitarRigNAMProcessor::maxCabSlots,
                                       processor.getCabCount());
        for (int s = 0; s < cabs; ++s)
            menu.addItem (101 + s,
                          juce::String (processor.hasIrLoaded (s) ? "Replace IR "
                                                                  : "Load in IR ")
                              + juce::String (s + 1));
    }
    else
    {
        // Every ACTIVE lane is listed, loaded or not - same rule the IR branch
        // above already used. Listing only loaded lanes made a second AMP+CAB
        // unreachable until something had been dropped into it by other means.
        const int rigs = juce::jlimit (1, GuitarRigNAMProcessor::maxRigs,
                                       processor.getRigCount());
        for (int l = 0; l < rigs; ++l)
            menu.addItem (1 + l,
                          juce::String (processor.hasModelLoaded (l) ? "Replace AMP "
                                                                     : "Load in AMP ")
                              + juce::String (l + 1));
        // firstFreeModelLane() only ever returns a lane inside getRigCount(),
        // so it is already covered by the entries above - no extra item.
    }

    juce::Component::SafePointer<ToneCardComponent> safe (&card);
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&card),
        [this, safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            int lane = -1;
            if (result >= 101)      lane = result - 101;                       // IR slot
            else if (result == 10)  lane = processor.firstFreeModelLane();     // parallel rig
            else                    lane = result - 1;                         // amp lane

            if (lane < 0)
                return;   // the free lane vanished meanwhile
            loadCardIntoLane (*safe, lane);
        });
}

//==============================================================================
// Inline variation picker (call-out anchored to the amp card): a search box +
// a scrollable list of the tone's captures, so even very long lists stay
// manageable without opening the full store.
class VariationPickerContent : public juce::Component
{
public:
    VariationPickerContent (std::vector<Tone3000Client::Model> models,
                            juce::String currentName, int currentModelId,
                            std::function<void (Tone3000Client::Model, ModelRowComponent*)> onPick)
        : all (std::move (models)), current (std::move (currentName)),
          currentId (currentModelId), pick (std::move (onPick))
    {
        search.setTextToShowWhenEmpty (juce::String (juce::CharPointer_UTF8 (
                                           "Search variations\xe2\x80\xa6")),
                                       juce::Colour (ui::textFaint));
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff14171b));
        search.setColour (juce::TextEditor::outlineColourId, ui::border());
        search.setColour (juce::TextEditor::textColourId, ui::text);
        search.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (search);

        vp.setViewedComponent (&content, false);
        vp.setScrollBarsShown (true, false);
        addAndMakeVisible (vp);

        setSize (420, juce::jmin (440, 60 + (int) all.size() * 44 + 10));
        rebuild();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10);
        search.setBounds (b.removeFromTop (30));
        b.removeFromTop (8);
        vp.setBounds (b);
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ui::cardBottom);
        g.setColour (ui::border());
        g.drawRect (getLocalBounds(), 1);
    }

private:
    void layoutRows()
    {
        const int rowH = 38, gap = 6, w = juce::jmax (10, vp.getWidth() - 12);
        content.setSize (w, juce::jmax (1, (int) rows.size() * (rowH + gap)));
        int y = 0;
        for (auto* r : rows) { r->setBounds (0, y, w, rowH); y += rowH + gap; }
    }

    void rebuild()
    {
        const auto q = search.getText().trim().toLowerCase();
        rows.clear();
        content.removeAllChildren();
        for (const auto& m : all)
        {
            const juce::String name = m.name.isNotEmpty() ? m.name : ("Model " + juce::String (m.id));
            if (q.isNotEmpty() && ! name.toLowerCase().contains (q))
                continue;
            auto* row = rows.add (new ModelRowComponent (m, false));
            // prefer the exact model id saved in the loaded capture's .meta;
            // fall back to name containment ("Title - Variant [A2]" vs "Variant").
            const bool isCurrent = (currentId != 0 && m.id == currentId)
                                   || (currentId == 0 && current.isNotEmpty()
                                       && (current == name || current.contains (name)));
            if (isCurrent)
                row->setInRig();
            auto* rp = row;
            auto mm = m;
            row->onDownloadClicked = [this, mm, rp]
            {
                // clear the previous "In rig"/progress marks so only the row
                // being switched to shows active (avoids a stale In-rig row).
                for (auto* other : rows)
                    if (other != rp)
                        other->reset();
                if (pick) pick (mm, rp);
            };
            content.addAndMakeVisible (row);
        }
        layoutRows();
        repaint();
    }

    std::vector<Tone3000Client::Model> all;
    juce::String current;
    int currentId = 0;
    std::function<void (Tone3000Client::Model, ModelRowComponent*)> pick;
    juce::TextEditor search;
    juce::Viewport vp;
    juce::Component content;
    juce::OwnedArray<ModelRowComponent> rows;
    // the call-out is its own desktop window; it needs its own tooltip window
    // for the per-row full-name tips to show.
    juce::TooltipWindow tooltip { this };
};

void StoreOverlay::loadVariationIntoLane (const Tone3000Client::Model& model, int lane,
                                          const ToneCardComponent::Info& toneInfo,
                                          ModelRowComponent* row)
{
    const juce::String kind = toneInfo.formatBadge == "IR" ? "ir" : "nam";

    juce::String baseName = toneInfo.title;
    if (model.name.isNotEmpty() && model.name != baseName)
        baseName += " - " + model.name;
    if (model.arch == "2")
        baseName += " [A2]";

    const int toneId = toneInfo.toneId;
    const juce::String imageUrl = toneInfo.imageUrl;
    juce::Component::SafePointer<ModelRowComponent> rsafe (row);

    auto finish = [this, model, baseName, kind, toneId, imageUrl, lane] (juce::File file)
    {
        client.saveImageSidecar (imageUrl, file);
        if (kind == "ir")
        {
            writeModelMeta (file, model, toneId);
            // lane >= 0 = swap this cab's IR in place; -1 = first free slot.
            const int slot = lane >= 0 ? lane : juce::jmax (0, processor.firstFreeIrSlot());
            processor.loadIrAsync (slot, file);
        }
        else
        {
            finalizeNamModel (file, toneId, model, baseName, lane);
        }
        updateRigStatuses();
    };

    if (const auto local = Tone3000Client::localFileForModel (model, kind, baseName);
        local.existsAsFile())
    {
        if (rsafe != nullptr) rsafe->setInRig();
        finish (local);
        return;
    }

    if (rsafe != nullptr) rsafe->setDownloading (0);
    client.downloadModel (model, kind, baseName,
        [rsafe] (int pct) { if (rsafe != nullptr) rsafe->setDownloading (pct); },
        [this, rsafe, finish] (juce::File file, juce::String error)
        {
            if (error.isNotEmpty())
            {
                bannerError = error;
                resized();
                repaint();
                return;
            }
            if (rsafe != nullptr) rsafe->setInRig();
            finish (file);
        });
}

void StoreOverlay::showVariationPicker (int lane, int toneId, juce::Component* anchor)
{
    // Build the tone info the download needs (title/image/format) from getTone,
    // fetch the model list, then pop the picker anchored to the amp/cab card.
    auto present = [this, lane, anchor]
        (ToneCardComponent::Info toneInfo, std::vector<Tone3000Client::Model> models)
    {
        // which capture is loaded now depends on the format: NAM -> the amp
        // lane's model; IR -> the cab slot's IR file (used to mark "In rig").
        const bool ir = toneInfo.formatBadge == "IR";
        const juce::String loadedPath = ir ? processor.getIrPath (lane)
                                           : processor.getModelPathNormal (lane);
        const juce::String currentName = juce::File (loadedPath).getFileNameWithoutExtension();
        // exact match: the model id persisted in the loaded capture's .meta
        int currentModelId = 0;
        if (loadedPath.isNotEmpty())
        {
            const juce::File meta (loadedPath + ".meta");
            if (meta.existsAsFile())
                currentModelId = (int) juce::JSON::parse (meta.loadFileAsString())
                                            .getProperty ("model_id", 0);
        }

        auto content = std::make_unique<VariationPickerContent> (
            std::move (models), currentName, currentModelId,
            [this, lane, toneInfo] (Tone3000Client::Model m, ModelRowComponent* r)
            {
                loadVariationIntoLane (m, lane, toneInfo, r);
            });

        juce::Rectangle<int> area = anchor != nullptr
            ? anchor->getScreenBounds()
            : juce::Rectangle<int> (getScreenBounds().getCentre(), getScreenBounds().getCentre());
        juce::CallOutBox::launchAsynchronously (std::move (content), area, nullptr);
    };

    // getTone for title/image/format, then the models (cache when possible).
    juce::Component::SafePointer<StoreOverlay> safe (this);
    client.getTone (toneId, [safe, toneId, present] (Tone3000Client::Tone t, juce::String)
    {
        if (safe == nullptr) return;
        ToneCardComponent::Info info;
        info.toneId    = toneId;
        info.title     = t.title;
        info.imageUrl  = t.imageUrl;
        info.formatBadge = (t.format == "ir") ? "IR" : "NAM";

        auto deliver = [present, info] (std::vector<Tone3000Client::Model> ms) { present (info, ms); };
        if (const auto it = safe->modelsCache.find (toneId); it != safe->modelsCache.end())
        {
            deliver (it->second);
            return;
        }
        safe->client.listModels (toneId, [safe, toneId, deliver] (std::vector<Tone3000Client::Model> ms, juce::String)
        {
            if (safe != nullptr) safe->modelsCache[toneId] = ms;
            deliver (ms);
        });
    });
}

//==============================================================================
// vNext: temporary A/B preview - loads the tone's best capture into AMP 1
// while remembering the current pair; KEEP CURRENT restores it, APPLY commits.
void StoreOverlay::startPreview (ToneCardComponent& card)
{
    if (card.getInfo().formatBadge == "IR")
    {
        bannerError = "A/B preview works with amp captures (IRs load instantly anyway)";
        resized();
        repaint();
        return;
    }

    const int toneId = card.getInfo().toneId;
    auto proceed = [this, safe = juce::Component::SafePointer<ToneCardComponent> (&card)]
        (const std::vector<Tone3000Client::Model>& models)
    {
        if (safe == nullptr || models.empty())
            return;
        // best capture: A2 standard > any standard > first
        const Tone3000Client::Model* best = nullptr;
        for (const auto& m : models)
            if (m.arch == "2" && m.size == "standard") { best = &m; break; }
        if (best == nullptr)
            for (const auto& m : models)
                if (m.size == "standard") { best = &m; break; }
        if (best == nullptr)
            best = &models.front();

        const auto model = *best;
        juce::String baseName = safe->getInfo().title;
        if (model.name.isNotEmpty() && model.name != baseName)
            baseName += " - " + model.name;
        if (model.arch == "2")
            baseName += " [A2]";

        if (const auto local = Tone3000Client::localFileForModel (model, "nam", baseName);
            local.existsAsFile())
        {
            previewBaseName = baseName;
            previewLoad (*safe, model, local);
            return;
        }

        safe->setProgress (0);
        safe->setStatus (ToneCardComponent::Status::downloading);
        client.downloadModel (model, "nam", baseName,
            [safe] (int pct) { if (safe != nullptr) safe->setProgress (pct); },
            [this, safe, model, baseName] (juce::File file, juce::String error)
            {
                if (safe == nullptr)
                    return;
                safe->setStatus (ToneCardComponent::Status::add);
                if (error.isNotEmpty())
                {
                    bannerError = error;
                    resized();
                    repaint();
                    return;
                }
                previewBaseName = baseName;
                previewLoad (*safe, model, file);
            });
    };

    if (const auto it = modelsCache.find (toneId); it != modelsCache.end())
    {
        proceed (it->second);
        return;
    }
    client.listModels (toneId,
        [this, toneId, proceed] (std::vector<Tone3000Client::Model> models, juce::String)
        {
            modelsCache[toneId] = models;
            proceed (models);
        });
}

void StoreOverlay::previewLoad (ToneCardComponent& card, const Tone3000Client::Model& model,
                                const juce::File& file)
{
    if (! previewing)
    {
        prevStdPath = processor.getModelPathNormal (0);
        prevEcoPath = processor.getModelPathEco (0);
        previewing = true;
    }
    previewTitle = card.getInfo().title;
    previewImageUrl = card.getInfo().imageUrl;
    previewToneId = card.getInfo().toneId;
    previewModel = model;
    previewFile = file;
    processor.setModelPair (0, file, {});
    resized();
    repaint();
}

void StoreOverlay::endPreview (bool apply)
{
    if (! previewing)
        return;
    previewing = false;

    if (apply)
    {
        // commit: meta + tone photo sidecar + eco pair via the normal path
        finalizeNamModel (previewFile, previewToneId, previewModel, previewBaseName, 0);
        client.saveImageSidecar (previewImageUrl, previewFile);
        updateRigStatuses();
    }
    else
    {
        if (prevStdPath.isNotEmpty())
            processor.setModelPair (0, juce::File (prevStdPath), juce::File (prevEcoPath));
        else
            processor.unloadModelLane (0);
    }
    resized();
    repaint();
}

void StoreOverlay::doConnect()
{
    connectButton.setEnabled (false);
    connectButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Waiting for login\xe2\x80\xa6")));
    auto* self = this; // MSVC: 'this' in a nested lambda init-capture resolves wrong
    auto onDone = [safe = juce::Component::SafePointer<StoreOverlay> (self)] (bool ok, juce::String error)
    {
        if (safe == nullptr)
            return;
        safe->connectButton.setEnabled (true);
        safe->connectButton.setButtonText ("Connect TONE3000");
        // Closing the sign-in panel is a decision, not a failure - no banner.
        safe->bannerError = (ok || error == "Sign-in cancelled")
                                ? juce::String()
                                : "Failed to connect: " + error;
        safe->updateHeaderState();
        if (ok)
            safe->doSearch (1);
        else
            safe->resized();
        safe->repaint();
    };

    if (ToneWebView::isSupported())
    {
        ensureWebView();
        webView->openConnect (std::move (onDone));
    }
    else
    {
        client.connect (std::move (onDone));
    }
}

void StoreOverlay::ensureWebView()
{
    if (webView != nullptr)
        return;

    webView = std::make_unique<ToneWebView> (client);
    webView->brandLogo = brandLogo;
    addChildComponent (*webView);
    webView->setBounds (getLocalBounds());
}

void StoreOverlay::setSplashVisible (bool v)
{
    splashVisible = v;
    splashContinue.setVisible (v);
    splashCancel.setVisible (v);
    // The notice is drawn in paint(), and JUCE draws children AFTER the parent -
    // so the card grid sat on top of it. Hide the grid (and the header controls
    // it dims) while the notice owns the screen; only its own buttons stay.
    viewport.setVisible (! v);
    if (v)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &browseButton, &changeKeyButton, &filtersChip, &connectButton, &userChip })
            c->setVisible (false);
        for (auto* chip : gearChips)
            chip->setVisible (false);
        splashContinue.toFront (false);
        splashCancel.toFront (false);
    }
    else
    {
        updateHeaderState();   // restores whatever should be showing
        setTab (tab);
    }
    resized();
    repaint();
}

void StoreOverlay::ensureDetailsView()
{
    if (detailsView != nullptr)
        return;

    detailsView = std::make_unique<ToneDetailsView> (client);
    detailsView->brandLogo = brandLogo;
    addAndMakeVisible (*detailsView);
    detailsView->onClose = [this] { if (detailsView != nullptr) detailsView->setVisible (false); };
    detailsView->onDownload = [this] (const Tone3000Client::Model& m,
                                      const ToneCardComponent::Info& filled, ModelRowComponent* row)
    {
        // adopt the tone info the details view resolved (title/image/format),
        // but keep the target lane chosen when the view was opened.
        detailsInfo = filled;
        downloadFromDetails (m, row);
    };
}

void StoreOverlay::downloadFromDetails (const Tone3000Client::Model& model, ModelRowComponent* row)
{
    // The details view sets detailsInfo (from getTone) and detailsTargetLane
    // (-1 = add to a free lane; >=0 = swap that lane). Same core as the picker.
    loadVariationIntoLane (model, detailsTargetLane, detailsInfo, row);
}

void StoreOverlay::startAddFlow (ToneCardComponent& card, int forceLane)
{
    // No explicit lane from a card menu? Then honour the rig the store was
    // opened for (the amp/cab card's LOAD/CHANGE).
    if (forceLane < 0)
        forceLane = pendingLane;

    const int toneId = card.getInfo().toneId;

    // Variations already cached: no API call.
    if (const auto it = modelsCache.find (toneId); it != modelsCache.end())
    {
        showModelChoices (card, it->second, forceLane);
        return;
    }

    // Feedback + guard against double-click while listing the models.
    card.setProgress (0);
    card.setStatus (ToneCardComponent::Status::downloading);

    client.listModels (toneId,
        [this, toneId, forceLane,
         safe = juce::Component::SafePointer<ToneCardComponent> (&card)]
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
            showModelChoices (*safe, models, forceLane);
        });
}

void StoreOverlay::showModelChoices (ToneCardComponent& card,
                                     const std::vector<Tone3000Client::Model>& models,
                                     int forceLane)
{
    if (models.size() == 1)
    {
        startDownload (card, models.front(), forceLane);
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
        [this, safe, models, forceLane] (int result)
        {
            if (safe == nullptr)
                return;
            if (result <= 0 || result > (int) models.size())
            {
                safe->setStatus (ToneCardComponent::Status::add); // canceled
                return;
            }
            startDownload (*safe, models[(size_t) (result - 1)], forceLane);
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

void StoreOverlay::writeModelMeta (const juce::File& file, const Tone3000Client::Model& m,
                                   int toneId)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("arch", m.arch);
    obj->setProperty ("size", m.size);
    obj->setProperty ("name", m.name);
    obj->setProperty ("tone_id", toneId);   // lets the amp card list variations
    obj->setProperty ("model_id", m.id);
    juce::File (file.getFullPathName() + ".meta")
        .replaceWithText (juce::JSON::toString (juce::var (obj), true));
}

void StoreOverlay::finalizeNamModel (const juce::File& mainFile, int toneId,
                                     const Tone3000Client::Model& chosen,
                                     const juce::String& baseName,
                                     int forceLane)
{
    writeModelMeta (mainFile, chosen, toneId);

    // target lane: caller-forced (variation swap) or first free (all busy -> 1st)
    const int lane = forceLane >= 0 ? forceLane
                                    : juce::jmax (0, processor.firstFreeModelLane());

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
        [proc = &processor, lane, mainFile, partnerCopy, toneId] (juce::File ecoFile, juce::String error)
        {
            if (error.isNotEmpty() || ! ecoFile.existsAsFile())
                return; // no eco pair - chip stays disabled
            writeModelMeta (ecoFile, partnerCopy, toneId);
            // the user may have changed the lane meanwhile - only
            // complete the pair if the main one is still on it
            if (proc->getModelPathNormal (lane) == mainFile.getFullPathName())
                proc->setModelPair (lane, mainFile, ecoFile);
        });
}

void StoreOverlay::startDownload (ToneCardComponent& card, const Tone3000Client::Model& model,
                                  int forceLane)
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
            writeModelMeta (local, model, card.getInfo().toneId);
            const int slot = forceLane >= 0 ? forceLane
                                            : juce::jmax (0, processor.firstFreeIrSlot());
            processor.loadIrAsync (slot, local);
        }
        else
        {
            finalizeNamModel (local, card.getInfo().toneId, model, baseName, forceLane);
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
        [this, model, baseName, forceLane,
         safe = juce::Component::SafePointer<ToneCardComponent> (&card)]
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
                writeModelMeta (file, model, safe->getInfo().toneId);
                const int slot = forceLane >= 0 ? forceLane
                                                : juce::jmax (0, processor.firstFreeIrSlot());
                processor.loadIrAsync (slot, file);
            }
            else
            {
                finalizeNamModel (file, safe->getInfo().toneId, model, baseName, forceLane);
            }
        });
}

void StoreOverlay::doSearch (int)
{
    // FREE TIER: there is no paginated search here. The grid is TRENDING for
    // the selected gear followed by LATEST - the two bounded list endpoints
    // that tier allows. Browsing the whole catalogue happens in TONE3000's own
    // picker (BROWSE ON TONE3000 -> selectTone), not inside this window.
    if (! client.isConnected() || searching)
        return;

    searching = true;
    currentPage = totalPages = 1;
    cards.clear();

    auto finish = [] (StoreOverlay* self)
    {
        self->searching = false;
        const int n = self->cards.size();
        self->countText = juce::String (n) + (n == 1 ? " TONE" : " TONES");
        self->layoutCards();
        self->resized();
        self->repaint();
    };

    juce::Component::SafePointer<StoreOverlay> safe (this);

    // The TYPE pills are not the trending endpoint's vocabulary. It takes a
    // GEAR (amp, amp-cab, cab, pedal...), while "IR" is a FORMAT - impulse
    // responses are published under gear=cab. Without this mapping the IR pill
    // asked for a gear that does not exist and the cabinet slots had nothing
    // to load.
    const juce::String trendGear = gearFilter == "ir"        ? juce::String ("cab")
                                 : gearFilter.isNotEmpty()   ? gearFilter
                                                             : juce::String ("amp-cab");

    // /tones/trending is scoped to ONE gear and returns 10, so asking for a
    // single gear made "ALL" show ~19 tones - which reads as "the store is
    // nearly empty". The endpoint is bounded PER GEAR, so ALL now fans out over
    // every gear type TONE3000 publishes: still only bounded list endpoints,
    // ~8 requests against a 100/min limit.
    auto gears = std::make_shared<juce::StringArray>();
    if (gearFilter == "ir")           gears->add ("cab");
    else if (gearFilter.isNotEmpty()) gears->add (trendGear);
    else                              *gears = { "amp-cab", "amp", "pedal", "cab",
                                                 "outboard", "space", "experimental" };

    auto next = std::make_shared<int> (0);
    auto step = std::make_shared<std::function<void()>>();

    *step = [safe, gears, next, step, finish]
    {
        if (safe == nullptr)
            return;
        auto* self = safe.getComponent();   // MSVC: never capture 'this' in a nested lambda
        if (self->tab != Tab::explore)
        {
            self->searching = false;
            return;
        }

        if (*next >= gears->size())   // trending done; latest closes the grid
        {
            self->client.listLatest ([safe, finish] (Tone3000Client::SearchResult latest)
            {
                if (safe == nullptr)
                    return;
                auto* s = safe.getComponent();
                if (latest.error.isNotEmpty() && s->cards.isEmpty())
                    s->bannerError = latest.error;
                else if (latest.error.isEmpty())
                    s->appendUnique (latest.tones);
                finish (s);
            });
            return;
        }

        const auto gear = (*gears)[(*next)++];
        self->client.listTrending (gear, [safe, step] (Tone3000Client::SearchResult r)
        {
            if (safe == nullptr)
                return;
            auto* s = safe.getComponent();
            if (r.error.isNotEmpty() && s->cards.isEmpty())
                s->bannerError = r.error;      // one gear failing must not empty the grid
            else if (r.error.isEmpty())
                s->appendUnique (r.tones);
            (*step)();
        });
    };

    (*step)();
}

void StoreOverlay::appendUnique (const std::vector<Tone3000Client::Tone>& tones)
{
    // trending (per gear) and latest overlap, and so do gears on multi-format tones
    for (const auto& tone : tones)
    {
        bool already = false;
        for (auto* c : cards)
            if (c->getInfo().toneId == tone.id)
                already = true;
        if (! already)
            addCardFor (tone, true);
    }
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

        // ST1: local cards get the same contextual primary (the aimed rig, or
        // AMP 1 / IR 1) and the lane menu; statuses refresh via the rig-status
        // timer, so the card is no longer destroyed inside its own onClick.
        auto* card = cards.add (new ToneCardComponent (info,
            [this] (ToneCardComponent& c) { loadCardIntoLane (c, targetLane()); }));
        card->setPrimaryLabel (primaryLabelFor (info));
        card->onShowMenu = [this] (ToneCardComponent& c) { showCardMenu (c); };

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

    countText = juce::String (cards.size())
                + (cards.size() == 1 ? " LOCAL TONE" : " LOCAL TONES");

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

    if (detailsView != nullptr)
        detailsView->setBounds (getLocalBounds());

    if (webView != nullptr)
        webView->setBounds (getLocalBounds());

    if (splashVisible)
    {
        auto card = juce::Rectangle<int> (0, 0, 520, 300).withCentre ({ W / 2, getHeight() / 2 });
        splashContinue.setBounds (card.getCentreX() - 8 - 200, card.getBottom() - 40 - 36, 200, 36);
        splashCancel.setBounds (card.getCentreX() + 8, card.getBottom() - 40 - 36, 120, 36);
    }

    closeButton.setBounds (W - 22 - 34, 15, 34, 34);

    // Keep the navigation clear of the full TONE3000 wordmark.  The editor is
    // commonly shown with Windows display scaling, so the previous x=208
    // position put the logo directly over the Explore label.
    exploreTab.setBounds (278, 20, 76, 30);
    libraryTab.setBounds (366, 20, 130, 30);
    pluginsTab.setBounds (504, 20, 74, 30);

    const int chipRightEdge = W - 22 - 34 - 14;
    userChip.setBounds (chipRightEdge - 130, 15, 130, 34);
    connectButton.setBounds (chipRightEdge - 170, 15, 170, 34);
    // the browse button takes the slot the search box used to own
    const int anchorX = connectButton.isVisible() ? connectButton.getX() : userChip.getX();
    browseButton.setBounds (anchorX - 12 - 230, 15, 230, 34);
    searchBox.setBounds (anchorX - 12 - 300, 15, 300, 34);   // hidden on the free tier
    // "Change key" goes on the row the VIEW/SORT selects used to occupy - in
    // the header it collided with the Plugins tab.
    changeKeyButton.setBounds (22, 108, 118, 30);

    // TONE3000 access setup form, centred in the explore empty-state area
    {
        const int fw = 470, fx = (W - fw) / 2;
        getKeyButton.setBounds (fx, 318, 236, 30);
        copyRedirectButton.setBounds (fx + 246, 318, 224, 30);
        keyEditor.setBounds (fx, 366, fw - 128, 34);
        saveKeyButton.setBounds (fx + fw - 118, 366, 118, 34);
    }

    // ---- tools row 1 (mockup): TYPE pills always visible + MORE FILTERS +
    // tones count tag (painted, right-aligned)
    int cx = 64;
    for (auto* chip : gearChips)
    {
        const int w = 28 + 7 * chip->getButtonText().length();
        chip->setBounds (cx, 72, w, 28);
        cx += w + 9;
    }
    cx += 6;
    filtersChip.setBounds (cx, 72, 112, 28);

    // ---- tools row 2 (mockup): VIEW (collections) + SORT selects
    int rx = 22;
    if (sourceCombo.isVisible())
    {
        rx += 42;   // "VIEW" label (painted)
        sourceCombo.setBounds (rx, 108, 150, 32);
        rx = sourceCombo.getRight() + 18;
    }
    if (sortCombo.isVisible())
    {
        rx += 42;   // "SORT" label (painted)
        sortCombo.setBounds (rx, 108, 160, 32);
    }

    // ---- collapsed extra filters (MORE FILTERS): a third row while open
    if (filtersOpen)
    {
        int fx = 64;   // after the "TAGS" label (painted)
        for (auto* chip : tagChips)
        {
            const int w = 22 + 6 * chip->getButtonText().length();
            chip->setBounds (fx, 150, w, 28);
            fx += w + 7;
        }
        fx += 8;
        a2Chip.setBounds (fx, 150, 62, 28);
        favChip.setBounds (fx + 68, 150, 58, 28);
    }

    // error banner (below the tools rows). Row 2 only carries content on
    // Explore (the "Change key" chip + what the grid is showing); on the other
    // tabs it was an empty 40 px strip, so the area stops after row 1 there.
    const bool extraRow = tab != Tab::plugins && filtersOpen;
    const int toolsBottom = showKeySetup() ? 64
                          : extraRow       ? 186
                          : tab == Tab::explore ? 146 : 106;
    const int bannerY = toolsBottom + 4;
    const bool banner = bannerError.isNotEmpty();
    retryButton.setVisible (banner);
    dismissButton.setVisible (banner);
    if (banner)
    {
        retryButton.setBounds (W - 22 - 30 - 8 - 130, bannerY + 5, 130, 26);
        dismissButton.setBounds (W - 22 - 30, bannerY + 5, 26, 26);
    }

    const int gridTop = banner ? bannerY + 44 : bannerY + 4;
    // A/B preview bar reserves the bottom strip while active
    const int previewBarH = previewing ? 54 : 0;
    viewport.setBounds (22, gridTop, W - 44 + 10, getHeight() - gridTop - 16 - previewBarH);
    keepBtn.setVisible (previewing);
    applyPrevBtn.setVisible (previewing);
    if (previewing)
    {
        applyPrevBtn.setBounds (W - 22 - 150, getHeight() - 44, 150, 32);
        keepBtn.setBounds (W - 22 - 150 - 8 - 130, getHeight() - 44, 130, 32);
    }
    layoutCards();
}

void StoreOverlay::paint (juce::Graphics& g)
{
    const int W = getWidth();

    // Fully opaque: at 0.985 the rig behind stayed faintly visible, which read
    // as a rendering fault whenever the grid was empty (disconnected state).
    g.fillAll (juce::Colour (0xff0b0c0e));

    // ---- header
    g.setFont (ui::uiFont (19.0f, true));
    g.setColour (ui::textBright);
    g.drawText ("Tone Store", 22, 18, 120, 30, juce::Justification::centredLeft);

    // Full TONE3000 logo (official wordmark) — list-view branding requirement.
    if (brandLogo.isValid())
    {
        const float logoH = 18.0f;
        const float logoW = logoH * brandLogo.getWidth() / (float) brandLogo.getHeight();
        g.drawImage (brandLogo, juce::Rectangle<float> (134.0f, 24.0f, logoW, logoH),
                     juce::RectanglePlacement::centred);
    }
    else
    {
        auto badge = juce::Rectangle<float> (134.0f, 25.0f, 66.0f, 17.0f);
        g.setColour (ui::accent.withAlpha (0.4f));
        g.drawRoundedRectangle (badge, 4.0f, 1.0f);
        g.setFont (ui::monoFont (9.0f, true));
        g.setColour (ui::accent);
        g.drawText ("TONE3000", badge, juce::Justification::centred);
    }

    g.setColour (juce::Colour (0xff24262a));
    g.fillRect (0, 64, W, 1);

    // ---- tools rows (only on the TONE3000 tabs): row 1 = TYPE pills + count
    // tag; row 2 = VIEW/SORT selects; optional third row = extra filters
    // The key-setup screen hides every tool, so the rows must go too - they were
    // leaving an empty band with an orphan "TYPE" label on the very first screen
    // a new user sees.
    const bool showTools = tab != Tab::plugins && ! showKeySetup();
    const bool extraRow = showTools && filtersOpen;
    const int toolsBottom = showKeySetup() ? 64                             // sync with resized()
                          : extraRow       ? 186
                          : tab == Tab::explore ? 146 : 106;
    if (showTools)
    {
        g.setFont (ui::monoFont (9.0f));
        g.setColour (ui::textMuted);
        g.drawText ("TYPE", 22, 72, 40, 28, juce::Justification::centredLeft);
        if (sourceCombo.isVisible())
            g.drawText ("VIEW", 22, 108, 40, 32, juce::Justification::centredLeft);
        if (sortCombo.isVisible())
            g.drawText ("SORT", sortCombo.getX() - 42, 108, 40, 32,
                        juce::Justification::centredLeft);

        // Row 2 lost the VIEW/SORT selects on the free tier. Rather than leave a
        // blank strip, it now says what the grid actually is - the two bounded
        // lists - so nobody hunts for a search box that is not coming back.
        if (tab == Tab::explore && ! showKeySetup() && client.isConnected())
        {
            const auto scope = gearFilter == "ir"      ? juce::String ("CABS / IR")
                             : gearFilter.isEmpty()    ? juce::String ("AMP+CAB")
                                                       : gearFilter.toUpperCase();
            g.setColour (ui::textFaint);
            // Say the size out loud. Users read a short grid as "the store only
            // has 11 amps" and assume it is broken - it is a curated 20, and
            // the whole catalogue is one button away.
            g.drawText ("SHOWING TRENDING " + scope
                            + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                            + "LATEST"
                            + juce::String (juce::CharPointer_UTF8 (
                                  "  \xe2\x80\x94  this is a short curated list, not the catalogue "
                                  "\xc2\xb7 search all 8000+ tones with BROWSE ON TONE3000")),
                        changeKeyButton.getRight() + 16, 108, W - changeKeyButton.getRight() - 200, 30,
                        juce::Justification::centredLeft);
        }
        if (filtersOpen)
            g.drawText ("SHOW", 22, 150, 40, 28, juce::Justification::centredLeft);

        // tones count tag, right end of row 1 (mockup "1.248 TONES")
        if (countText.isNotEmpty())
        {
            g.setFont (ui::monoFont (9.0f, true));
            const int tw = 20 + 6 * countText.length();
            auto tag = juce::Rectangle<float> ((float) (W - 22 - tw), 75.0f,
                                               (float) tw, 22.0f);
            g.setColour (juce::Colour (0xff070c0e).withAlpha (0.74f));
            g.fillRoundedRectangle (tag, 5.0f);
            g.setColour (ui::border());
            g.drawRoundedRectangle (tag, 5.0f, 1.0f);
            g.setColour (ui::textDim);
            g.drawText (countText, tag, juce::Justification::centred);
        }
    }
    else if (tab == Tab::plugins)   // NOT just "!showTools": the key-setup screen
    {                               // also turns tools off and must stay blank
        g.setFont (ui::monoFont (9.5f));
        g.setColour (ui::textFaint);
        g.drawText (juce::CharPointer_UTF8 (
                        "Embedded catalog \xc2\xb7 installs without admin in "
                        "%LOCALAPPDATA%\\Programs\\Common\\VST3 \xc2\xb7 "
                        "registry in Documents\\PedalForge NAM\\plugins.json"),
                    22, 74, W - 44, 28, juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (0xff1e2023));
    g.fillRect (0, toolsBottom, W, 1);

    // ---- error banner
    if (bannerError.isNotEmpty())
    {
        auto banner = juce::Rectangle<float> (22.0f, (float) toolsBottom + 4.0f,
                                              (float) W - 44.0f, 36.0f);
        g.setColour (ui::red.withAlpha (0.1f));
        g.fillRoundedRectangle (banner, 2.0f);
        g.setColour (ui::red.withAlpha (0.4f));
        g.drawRoundedRectangle (banner, 2.0f, 1.0f);
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
        if (showKeySetup())
        {
            g.setFont (ui::uiFont (16.0f, true));
            g.setColour (juce::Colour (0xffc8cace));
            g.drawText ("Connect PedalForge to your own TONE3000 account", 0, 210, W, 24,
                        juce::Justification::centred);
            g.setFont (ui::uiFont (12.5f));
            g.setColour (juce::Colour (0xff84878d));
            g.drawText ("Each person uses their own key, so your downloads and rate limit are yours.",
                        0, 240, W, 18, juce::Justification::centred);
            // the numbered steps are a LIST: centring each line made it read as
            // loose prose. Left-aligned under a shared left edge.
            const auto steps =
                juce::String ("1.  Open Settings > API Keys on tone3000.com and create a key\n")
                + "2.  Register this redirect URI on that same key:  " + Tone3000Client::redirectUri() + "\n"
                + "3.  Paste the key below - it is stored only on this machine";
            g.drawFittedText (steps, (W - 470) / 2, 264, 470, 46,
                              juce::Justification::topLeft, 3);

            if (keyNotice.isNotEmpty())
            {
                g.setFont (ui::uiFont (11.5f));
                g.setColour (Tone3000Client::looksLikePublishableKey (keyEditor.getText())
                                 ? ui::accent : juce::Colour (0xffe8b4ac));
                g.drawText (keyNotice, 0, 408, W, 18, juce::Justification::centred);
            }

            g.setFont (ui::monoFont (10.5f));
            g.setColour (juce::Colour (0xff6c7076));
            g.drawText ("stored in " + client.configFile().getFullPathName(),
                        0, 434, W, 16, juce::Justification::centred);
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
    else if (tab == Tab::library && cards.isEmpty())
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

    // ---- vNext: A/B preview bar (temporary capture in AMP 1)
    if (previewing)
    {
        const int barY = getHeight() - 54;
        g.setColour (juce::Colour (0xff14191d));
        g.fillRect (0, barY, W, 54);
        g.setColour (ui::accent.withAlpha (0.5f));
        g.fillRect (0, barY, W, 1);
        g.setColour (ui::accentBright);
        g.setFont (ui::uiFont (12.5f, true));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 A/B preview \xc2\xb7 "))
                        + previewTitle,
                    22, barY + 8, W - 44 - 300, 20, juce::Justification::centredLeft);
        g.setColour (ui::textFaint);
        g.setFont (ui::monoFont (8.5f));
        g.drawText ("temporary - APPLY commits, KEEP CURRENT restores your rig",
                    22, barY + 30, W - 44 - 300, 14, juce::Justification::centredLeft);
    }

    // ---- API notice shown before the first TONE3000 sign-in
    if (splashVisible)
    {
        g.fillAll (juce::Colour (0xff0b0c0e).withAlpha (0.92f));

        auto card = juce::Rectangle<int> (0, 0, 520, 300).withCentre ({ W / 2, getHeight() / 2 });
        g.setColour (ui::cardBottom);
        g.fillRect (card.toFloat());
        g.setColour (ui::border());
        g.drawRect (card.toFloat(), 1.0f);

        // full TONE3000 wordmark (logo usage: full mark before compact)
        if (brandLogo.isValid())
        {
            const float lh = 30.0f;
            const float lw = lh * brandLogo.getWidth() / (float) brandLogo.getHeight();
            g.drawImage (brandLogo,
                         juce::Rectangle<float> (card.getCentreX() - lw / 2.0f,
                                                 (float) card.getY() + 40.0f, lw, lh),
                         juce::RectanglePlacement::centred);
        }

        // NO partnership is claimed: there is no agreement with TONE3000 and
        // saying otherwise would be false. This states what is actually true -
        // the public API used under its free, non-commercial tier.
        g.setColour (ui::textBright);
        g.setFont (ui::uiFont (17.0f, true));
        g.drawText ("Built on the TONE3000 API",
                    card.getX(), card.getY() + 92, card.getWidth(), 24,
                    juce::Justification::centred);

        g.setColour (ui::textDim);
        g.setFont (ui::uiFont (12.5f));
        g.drawFittedText (
            "Sign in with your own TONE3000 account to browse and load community "
            "amp captures and IRs inside PedalForge NAM. A browser window opens "
            "for a one-time secure login - your credentials never touch this app.",
            card.getX() + 40, card.getY() + 124, card.getWidth() - 80, 64,
            juce::Justification::topLeft, 3);

        g.setColour (ui::textFaint);
        g.setFont (ui::monoFont (9.5f));
        g.drawFittedText (
            "Uses the public TONE3000 API under its free, non-commercial tier. "
            "PedalForge NAM is not affiliated with or endorsed by TONE3000.",
            card.getX() + 40, card.getY() + 192, card.getWidth() - 80, 28,
            juce::Justification::topLeft, 2);
    }
}

//==============================================================================
// ModelRowComponent - one variation in the Tone Details model selector.
ModelRowComponent::ModelRowComponent (const Tone3000Client::Model& m, bool offline)
    : model (m)
{
    dlButton.getProperties().set ("outlineAccent", true);
    dlButton.setButtonText (offline ? "Re-add" : "Add");
    dlButton.setMouseClickGrabsKeyboardFocus (false);
    dlButton.onClick = [this] { if (onDownloadClicked != nullptr && progress < 0) onDownloadClicked(); };
    addAndMakeVisible (dlButton);

    // full name on hover (row text is truncated for long variation names)
    juce::String full = m.name.isNotEmpty() ? m.name : ("Model " + juce::String (m.id));
    if (m.arch == "2")      full += "  (A2)";
    else if (m.arch == "1") full += "  (A1)";
    if (m.size.isNotEmpty() && m.size != "standard")
        full += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + m.size;
    setTooltip (full);
}

void ModelRowComponent::setDownloading (int pct)
{
    progress = juce::jlimit (0, 100, pct);
    dlButton.setButtonText (juce::String (progress) + "%");
    dlButton.setEnabled (false);
    repaint();
}

void ModelRowComponent::setInRig()
{
    progress = 101;
    dlButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 In rig")));
    dlButton.setEnabled (false);
    repaint();
}

void ModelRowComponent::reset()
{
    progress = -1;
    dlButton.setButtonText ("Add");
    dlButton.setEnabled (true);
    repaint();
}

void ModelRowComponent::resized()
{
    dlButton.setBounds (getLocalBounds().reduced (7).removeFromRight (96));
}

void ModelRowComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (ui::glass());
    g.fillRect (b);
    g.setColour (ui::border());
    g.drawRect (b, 1.0f);

    // right side is the Add button (96) + a gap; the tag sits just left of it,
    // and the name gets everything that's left (was a fixed -250 that crushed
    // the name to nothing in the narrow inline picker).
    juce::String tag;
    if (model.arch == "2")      tag = "A2";
    else if (model.arch == "1") tag = "A1";
    if (model.size.isNotEmpty() && model.size != "standard")
        tag += (tag.isEmpty() ? juce::String() : juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))) + model.size;

    const int btnW = 96 + 10;                       // Add button + gap
    const int tagW = tag.isNotEmpty() ? 66 : 0;
    const int nameW = juce::jmax (24, getWidth() - 12 - btnW - tagW);

    g.setColour (ui::text);
    g.setFont (ui::uiFont (12.5f, true));
    const juce::String name = model.name.isNotEmpty() ? model.name
                                                      : ("Model " + juce::String (model.id));
    g.drawText (name, 12, 0, nameW, getHeight(), juce::Justification::centredLeft);

    if (tag.isNotEmpty())
    {
        g.setColour (model.arch == "2" ? ui::accentBright : ui::textDim);
        g.setFont (ui::monoFont (10.0f, true));
        g.drawText (tag, 12 + nameW, 0, tagW, getHeight(), juce::Justification::centredLeft);
    }
}

//==============================================================================
// ToneDetailsView - tone image, title, gear, format, creator + avatar,
// scrollable variation selector and the creator description.
ToneDetailsView::ToneDetailsView (Tone3000Client& c) : client (c)
{
    closeButton.getProperties().set ("chip", true);
    closeButton.setMouseClickGrabsKeyboardFocus (false);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    webButton.getProperties().set ("chip", true);
    webButton.setMouseClickGrabsKeyboardFocus (false);
    webButton.onClick = [this]
    {
        auto u = info.toneUrl;
        if (u.isEmpty() && info.toneId != 0)
            u = "https://www.tone3000.com/tones/" + juce::String (info.toneId);
        if (u.startsWith ("/")) u = "https://www.tone3000.com" + u;
        if (u.isNotEmpty()) juce::URL (u).launchInDefaultBrowser();
    };
    addAndMakeVisible (webButton);

    modelsVp.setViewedComponent (&modelsContent, false);
    modelsVp.setScrollBarsShown (true, false);
    addAndMakeVisible (modelsVp);
}

void ToneDetailsView::open (const ToneCardComponent::Info& i)
{
    info = i;
    description = {};
    toneImage = juce::Image();
    avatarImage = juce::Image();
    models.clear();
    modelRows.clear();
    modelsContent.removeAllChildren();
    setVisible (true);
    toFront (true);
    resized();
    repaint();

    juce::Component::SafePointer<ToneDetailsView> safe (this);

    if (info.imageUrl.isNotEmpty())
        client.fetchImage (info.imageUrl,
            [safe] (juce::Image img) { if (safe) { safe->toneImage = img; safe->repaint(); } });

    if (info.toneId != 0)
    {
        client.getTone (info.toneId, [safe] (Tone3000Client::Tone t, juce::String err)
        {
            if (safe == nullptr) return;
            if (err.isEmpty())
            {
                safe->description = t.description;
                // fill any fields the caller did not know (amp-card entry: only
                // the tone id is known, so title/image/creator come from here).
                if (safe->info.title.isEmpty())    safe->info.title = t.title;
                if (safe->info.creator.isEmpty())  safe->info.creator = t.creator;
                if (t.gear.isNotEmpty())           safe->info.gear = t.gear;
                if (t.format.isNotEmpty())         safe->info.formatBadge = (t.format == "ir" ? "IR" : "NAM");
                if (safe->info.toneUrl.isEmpty())  safe->info.toneUrl = t.url;
                if (safe->info.imageUrl.isEmpty() && t.imageUrl.isNotEmpty())
                {
                    safe->info.imageUrl = t.imageUrl;
                    safe->client.fetchImage (t.imageUrl,
                        [safe] (juce::Image img) { if (safe) { safe->toneImage = img; safe->repaint(); } });
                }
                if (t.creatorAvatar.isNotEmpty())
                    safe->client.fetchImage (t.creatorAvatar,
                        [safe] (juce::Image a) { if (safe) { safe->avatarImage = a; safe->repaint(); } });
            }
            safe->repaint();
        });

        client.listModels (info.toneId, [safe] (std::vector<Tone3000Client::Model> ms, juce::String)
        {
            if (safe == nullptr) return;
            safe->models = std::move (ms);
            safe->rebuildModels();
        });
    }
}

void ToneDetailsView::rebuildModels()
{
    modelRows.clear();
    modelsContent.removeAllChildren();
    for (const auto& m : models)
    {
        auto* row = modelRows.add (new ModelRowComponent (m, false));
        auto* rp = row;
        auto mm = m;
        row->onDownloadClicked = [this, mm, rp] { if (onDownload) onDownload (mm, info, rp); };
        modelsContent.addAndMakeVisible (row);
    }
    resized();
    repaint();
}

void ToneDetailsView::resized()
{
    auto b = getLocalBounds().reduced (40);
    const int rowH = 40, gap = 6;
    const int listX = b.getX() + 430, listW = b.getWidth() - 430 - 24;
    modelsVp.setBounds (listX, b.getY() + 150, listW, b.getHeight() - 150 - 24);
    modelsContent.setSize (juce::jmax (10, modelsVp.getWidth() - 12),
                           juce::jmax (1, (int) modelRows.size() * (rowH + gap)));
    int y = 0;
    for (auto* r : modelRows) { r->setBounds (0, y, modelsContent.getWidth(), rowH); y += rowH + gap; }

    closeButton.setBounds (getWidth() - 40 - 34, 30, 34, 30);
    webButton.setBounds (getWidth() - 40 - 34 - 8 - 190, 32, 190, 26);
}

void ToneDetailsView::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg.withAlpha (0.97f));
    auto b = getLocalBounds().reduced (40);

    g.setColour (ui::cardBottom);
    g.fillRect (b.toFloat());
    g.setColour (ui::border());
    g.drawRect (b.toFloat(), 1.0f);

    // left column: tone image
    auto img = juce::Rectangle<int> (b.getX() + 24, b.getY() + 24, 382, 216);
    g.setColour (ui::meterBg);
    g.fillRect (img);
    if (toneImage.isValid())
        g.drawImage (toneImage, img.toFloat(),
                     juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
    else
    {
        g.setColour (ui::textFaint);
        g.setFont (ui::uiFont (12.0f));
        g.drawText ("no image", img, juce::Justification::centred);
    }

    // right column: title / gear / format / creator
    const int tx = b.getX() + 430;
    int ty = b.getY() + 24;
    g.setColour (ui::textBright);
    g.setFont (ui::uiFont (21.0f, true));
    g.drawText (info.title, tx, ty, b.getWidth() - 430 - 24, 28, juce::Justification::centredLeft);
    ty += 34;

    g.setColour (ui::textDim);
    g.setFont (ui::monoFont (11.0f));
    juce::String meta = info.gear;
    if (info.formatBadge.isNotEmpty())
        meta += (meta.isEmpty() ? juce::String() : juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   "))) + info.formatBadge;
    g.drawText (meta.toUpperCase(), tx, ty, b.getWidth() - 430 - 24, 16, juce::Justification::centredLeft);
    ty += 26;

    // creator (avatar + username)
    if (avatarImage.isValid())
    {
        juce::Path circ;
        circ.addEllipse ((float) tx, (float) ty, 26.0f, 26.0f);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (circ);
        g.drawImage (avatarImage, juce::Rectangle<float> ((float) tx, (float) ty, 26.0f, 26.0f),
                     juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.setColour (ui::accent.withAlpha (0.22f));
        g.fillEllipse ((float) tx, (float) ty, 26.0f, 26.0f);
    }
    g.setColour (ui::textDim);
    g.setFont (ui::uiFont (12.5f, true));
    g.drawText ("by " + (info.creator.isNotEmpty() ? info.creator : juce::String ("unknown")),
                tx + 34, ty, 320, 26, juce::Justification::centredLeft);

    // TONE3000 attribution (official wordmark), bottom-right of the panel
    if (brandLogo.isValid())
    {
        const float logoH = 16.0f;
        const float logoW = logoH * brandLogo.getWidth() / (float) brandLogo.getHeight();
        g.setColour (ui::textFaint);
        g.setFont (ui::uiFont (9.5f));
        g.drawText ("FROM", b.getRight() - 24 - (int) logoW - 46, b.getBottom() - 24 - 16,
                    42, 16, juce::Justification::centredRight);
        g.drawImage (brandLogo,
                     juce::Rectangle<float> (b.getRight() - 24 - logoW, b.getBottom() - 24 - logoH,
                                             logoW, logoH),
                     juce::RectanglePlacement::centred);
    }

    // "VARIATIONS" header for the scrollable model list
    g.setColour (ui::textFaint);
    g.setFont (ui::uiFont (9.5f, true));
    juce::String vhead = "VARIATIONS";
    if (! models.empty()) vhead += "  (" + juce::String ((int) models.size()) + ")";
    g.drawText (vhead, tx, b.getY() + 132, 260, 14, juce::Justification::centredLeft);

    // description (below the image, left column)
    if (description.isNotEmpty())
    {
        g.setColour (ui::textFaint);
        g.setFont (ui::uiFont (9.5f, true));
        g.drawText ("DESCRIPTION", b.getX() + 24, b.getY() + 252, 200, 14, juce::Justification::centredLeft);
        g.setColour (ui::textDim);
        g.setFont (ui::uiFont (12.5f));
        g.drawFittedText (description, b.getX() + 24, b.getY() + 270, 382,
                          b.getHeight() - 294, juce::Justification::topLeft, 14);
    }
}
