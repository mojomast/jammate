#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "LookAndFeel.h"
#include "Tone3000Client.h"
#include "ToneWebView.h"

class GuitarCompanionProcessor;

//==============================================================================
// Tone card per ToneCard.dc.html.
class ToneCardComponent : public juce::Component
{
public:
    enum class Status { add, downloading, inRig };

    struct Info
    {
        int toneId = 0;              // 0 = local item (no download)
        juce::File localFile;        // filled for local/downloaded items
        juce::String title, creator, gear;   // gear: amp/pedal/full-rig/ir/...
        juce::String creatorAvatar;  // creator avatar url ("" = initial dot)
        /// Raw TONE3000 format: nam / ir / aida-x / aa-snapshot / proteus.
        /// Kept apart from formatBadge because only this one says whether the
        /// file is something this app can open at all.
        juce::String format;
        juce::String formatBadge;    // "NAM" / "IR" / the format, when foreign
        juce::String imageUrl;       // tone image ("" = placeholder)
        juce::String toneUrl;        // tone page on tone3000.com ("" = none)
        bool a2 = false;             // has A2 models available
        juce::String downloads, favorites;   // formatted ("24.1k"); empty for local items
        bool offline = false;        // already exists locally
    };

    ToneCardComponent (Info info, std::function<void (ToneCardComponent&)> onAdd);

    void setStatus (Status s);
    void setProgress (int pct);
    void setImage (juce::Image);
    void setAvatar (juce::Image);
    /// Associates the downloaded file with the card (enables the offline badge
    /// and status tracking by the overlay).
    void setLocalFile (const juce::File&);
    Status getStatus() const { return status; }
    const Info& getInfo() const { return info; }

    // favorite (star): persisted by the overlay in favoritos.json
    void setFavorite (bool fav);
    bool isFavorite() const { return favorite; }
    std::function<void (ToneCardComponent&)> onToggleFavorite;

    // vNext: temporary A/B preview (loads into AMP 1 without committing)
    std::function<void (ToneCardComponent&)> onPreview;

    // ST1: contextual primary action - the overlay computes the label from the
    // rig state ("LOAD IN AMP 1" / "REPLACE AMP 1" / "REPLACE IR").
    void setPrimaryLabel (const juce::String&);
    // card body click (outside the buttons) opens the Tone Details view
    std::function<void (ToneCardComponent&)> onOpenDetails;
    // small dropdown next to the primary action (also via right-click):
    // replace a specific amp/IR slot or add as parallel rig
    std::function<void (ToneCardComponent&)> onShowMenu;

    void mouseUp (const juce::MouseEvent&) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Info info;
    Status status = Status::add;
    int progress = 0;
    bool favorite = false;
    juce::String primaryLabel { "Add" };
    juce::Image image, avatar;
    juce::TextButton addButton;
    juce::TextButton favButton;
    juce::TextButton linkButton;   // open the tone's page on tone3000.com
    juce::TextButton previewButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 PREVIEW")) };
    juce::TextButton menuButton;   // "load options" dropdown

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneCardComponent)
};

//==============================================================================
// One selectable model/variation row in the Tone Details view.
class ModelRowComponent : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    explicit ModelRowComponent (const Tone3000Client::Model&, bool offline);
    std::function<void()> onDownloadClicked;
    const Tone3000Client::Model& getModel() const { return model; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void setDownloading (int pct);
    void setInRig();
    void reset();   // back to the idle "Add" state
private:
    Tone3000Client::Model model;
    juce::TextButton dlButton;
    int progress = -1;   // -1 = idle, 0..100 downloading, 101 = in rig
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelRowComponent)
};

//==============================================================================
// Tone Details view (TONE3000 design requirement): tone image, title, gear
// type, format, creator (username + avatar), a scrollable model/variation
// selector (handles very long lists) and the creator's description.
class ToneDetailsView : public juce::Component
{
public:
    ToneDetailsView (Tone3000Client&);
    void open (const ToneCardComponent::Info&);
    juce::Image brandLogo;   // official TONE3000 wordmark (attribution)
    std::function<void()> onClose;
    // carries the (getTone-filled) tone info back so the store can build the
    // file name / route the download even when opened with only a tone id.
    std::function<void (const Tone3000Client::Model&, const ToneCardComponent::Info&,
                        ModelRowComponent*)> onDownload;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Tone3000Client& client;
    ToneCardComponent::Info info;
    juce::String description;
    juce::Image toneImage, avatarImage;
    std::vector<Tone3000Client::Model> models;

    juce::TextButton closeButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")) };
    juce::TextButton webButton { juce::String (juce::CharPointer_UTF8 ("Open on TONE3000 \xe2\x86\x97")) };
    juce::Viewport modelsVp;
    juce::Component modelsContent;
    juce::OwnedArray<ModelRowComponent> modelRows;

    void rebuildModels();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneDetailsView)
};

//==============================================================================
// Tone Store overlay (TONE3000) per GuitarRig.dc.html.
class StoreOverlay : public juce::Component,
                     private juce::Timer
{
public:
    explicit StoreOverlay (GuitarCompanionProcessor&);
    ~StoreOverlay() override;

    void visibilityChanged() override;

    void open();
    void openOnLibrary();   // used by the GUITAR_COMPANION_OPEN_STORE dev flag
    // ---- formats this app can actually open --------------------------------
    // TONE3000 also publishes aida-x, aa-snapshot and proteus, which belong to
    // other ecosystems. Their authorize endpoint takes ONE `format` value and
    // Guitar Companion uses two (nam + ir), so the picker cannot be scoped to just
    // ours - it would have to hide either the captures or the IRs. So instead
    // of filtering we say so: without this the file downloaded, was treated as
    // a NAM capture, and the amp went quiet with no explanation.
    static bool canLoadFormat (const juce::String& format);
    /// "AIDA-X", "Proteus"... for the message.
    static juce::String formatDisplayName (const juce::String& format);

    void openOnPlugins();   // VST3 plugin manager (embedded catalog)
    /// Opens the store aimed at ONE rig: whatever is loaded from it lands in
    /// that amp lane (or, for an IR tone, that rig's cab slot) instead of the
    /// first free one. This is what the amp card's "LOAD/CHANGE CAPTURE" and
    /// the cab's "CHANGE" use - without it, clicking CHANGE on AMP 2 loaded the
    /// capture into whichever lane happened to be empty. Cleared on close.
    /// `forIr` says the user came from a cab, so the TONE3000 picker starts on
    /// IRs instead of captures.
    void openForRig (int lane, bool forIr = false);
    /// Opens the store and goes straight into the TONE3000 picker - the
    /// GUITAR_COMPANION_OPEN_STORE=browse dev flag, for testing the embedded browser
    /// without clicking by coordinate.
    void openOnBrowser();

    bool keyPressed (const juce::KeyPress&) override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab { explore, library, plugins };

    void timerCallback() override;
    /// Syncs the card statuses with what is loaded in the rig.
    void updateRigStatuses();
    /// Add flow: lists the tone's models; only one -> download directly,
    /// several -> choice menu (like on the TONE3000 site).
    /// forceLane >= 0 targets that amp lane / IR slot; -1 = first free.
    void startAddFlow (ToneCardComponent&, int forceLane = -1);
    void showModelChoices (ToneCardComponent&, const std::vector<Tone3000Client::Model>&,
                           int forceLane = -1);
    void startDownload (ToneCardComponent&, const Tone3000Client::Model&, int forceLane = -1);
    /// ST1: contextual card actions - routes a card into a specific lane/slot
    /// (local files load directly; TONE3000 tones go through the add flow).
    void loadCardIntoLane (ToneCardComponent&, int lane);
    /// "Replace AMP 1/2/3" (loaded lanes) / "Add as parallel rig" (free lane) /
    /// "Replace IR 1/2/3" - shown by the card's "\xe2\x96\xbe" button or right-click.
    void showCardMenu (ToneCardComponent&);
    juce::String primaryLabelFor (const ToneCardComponent::Info&) const;

    /// Shows the "we can't open this one" notice. Returns true when the format
    /// is fine and the caller should carry on.
    bool checkFormatSupported (const ToneCardComponent::Info&);
    /// After downloading a capture: writes .meta, resolves the ECO pair
    /// (lighter variation with the same name) and hands the pair to the processor.
    /// forceLane >= 0 loads into that exact lane (variation swap); -1 = first free.
    void finalizeNamModel (const juce::File& mainFile, int toneId,
                           const Tone3000Client::Model& chosen, const juce::String& baseName,
                           int forceLane = -1);
    static void writeModelMeta (const juce::File&, const Tone3000Client::Model&, int toneId);
    static int sizeRank (const juce::String&);
    void setTab (Tab);
    void doSearch (int page);
    void refreshLibrary();
    void rebuildCards (bool append);
    void layoutCards();
    void updateHeaderState();
    void addCardFor (const Tone3000Client::Tone&, bool appendToGrid);
    /// Adds every tone not already on the grid (the bounded lists overlap).
    void appendUnique (const std::vector<Tone3000Client::Tone>&);
    juce::String formatCount (juce::int64) const;

    GuitarCompanionProcessor& processor;
    Tone3000Client client;

    Tab tab = Tab::explore;
    /// Rig the store is aiming at (-1 = none: load into the first free lane).
    /// Set by openForRig(), cleared by open() and when the store hides.
    int pendingLane = -1;
    /// The store was opened from a cab, so the picker should start on IRs.
    bool pendingIsIr = false;
    /// Lane a download should land in: the pending rig, or lane 0 by default
    /// (which is what the card actions always assumed).
    int targetLane() const { return pendingLane >= 0 ? pendingLane : 0; }
    juce::String gearFilter;          // "" = all
    juce::String sortValue = "trending";
    int currentPage = 1, totalPages = 1;
    bool searching = false;
    juce::String bannerError;         // "" = no banner
    /// False for notices that retrying cannot fix (an unsupported format stays
    /// unsupported), so the banner does not offer a button that does nothing.
    bool bannerRetryable = true;

    // header
    juce::TextButton closeButton { "X" };
    juce::TextButton exploreTab { "Explore" }, libraryTab { "My library" },
        pluginsTab { "Plugins" };
    juce::TextEditor searchBox;   // free tier: hidden - /tones/search is full API access
    /// Opens TONE3000's own picker (prompt=select_tone). This replaces the
    /// in-app search: browsing the whole catalogue happens on their site.
    juce::TextButton browseButton { juce::String (juce::CharPointer_UTF8 (
        "BROWSE ON TONE3000 \xe2\x86\x97")) };
    int trendingCount = 0;        // index in `cards` where LATEST starts
    juce::TextButton connectButton { "Connect TONE3000" };
    juce::TextButton userChip;

    // ---- TONE3000 access setup ------------------------------------------
    // Each person uses their OWN publishable key, so no credential ships in the
    // repository and the rate limit is theirs. Entered here instead of by
    // hand-editing tone3000.json, which was the only way before.
    juce::TextEditor keyEditor;
    juce::TextButton getKeyButton { juce::String (juce::CharPointer_UTF8 (
        "Open TONE3000 API keys \xe2\x86\x97")) };
    juce::TextButton copyRedirectButton { "Copy redirect URI" };
    juce::TextButton saveKeyButton { "Save key" };
    juce::TextButton changeKeyButton { "Change key" };  // shown once a key exists
    bool keySetupVisible = false;                       // forced open by "Change key"
    /// True while the setup form should be on screen (no key yet, or reopened).
    bool showKeySetup() const;
    void updateKeySetupState();
    juce::String keyNotice;   // inline feedback under the field ("" = none)

    // ---- embedded TONE3000 browser --------------------------------------
    // The free tier browses through TONE3000's own pages, and those pages now
    // open inside the store instead of in the system browser. Built lazily:
    // spinning up a WebView2 costs real memory and most sessions never sign in.
    // Falls back to the system browser when ToneWebView::isSupported() is false.
    std::unique_ptr<ToneWebView> webView;
    void ensureWebView();

    // TONE3000 notice shown before the first sign-in. NOT a partnership claim -
    // there is no agreement with them; it states the free-tier API use instead.
    juce::TextButton splashContinue { "Continue to TONE3000" }, splashCancel { "Not now" };
    bool splashVisible = false;
    void setSplashVisible (bool);
    void doConnect();   // the actual OAuth flow (after the splash)

    // vNext: temporary A/B preview into AMP 1 - the previous pair is restored
    // by KEEP CURRENT; APPLY commits (meta + sidecar + eco pair)
    bool previewing = false;
    juce::String prevStdPath, prevEcoPath;      // pair before the preview started
    juce::String previewTitle, previewBaseName, previewImageUrl;
    juce::File previewFile;
    int previewToneId = 0;
    Tone3000Client::Model previewModel;
    juce::TextButton keepBtn { "KEEP CURRENT" }, applyPrevBtn { "APPLY PREVIEW" };
    void startPreview (ToneCardComponent&);
    void previewLoad (ToneCardComponent&, const Tone3000Client::Model&, const juce::File&);
    void endPreview (bool apply);

    // filters (mockup store-tools row 1): the extra chips (tags/A2/favorites)
    // collapse behind the "MORE FILTERS" pill and expand on a third row
    juce::TextButton filtersChip { "MORE FILTERS" };
    bool filtersOpen = false;
    // right-aligned tones count tag ("48+ TONES" / "4 LOCAL TONES")
    juce::String countText;
    juce::OwnedArray<juce::TextButton> gearChips;
    juce::OwnedArray<juce::TextButton> tagChips;   // multi-toggle; enter the query
    juce::TextButton a2Chip { "A2 only" };
    bool a2Only = false;
    // favorites (star): ids persisted in Documents\Guitar Companion\favoritos.json
    juce::TextButton favChip { juce::String (juce::CharPointer_UTF8 ("Only \xe2\x98\x85")) };
    bool favOnly = false;
    juce::StringArray favIds;
    void loadFavorites();
    void saveFavorites() const;
    void toggleFavorite (ToneCardComponent&);
    juce::StringArray activeTags;
    juce::ComboBox sortCombo;
    // TONE3000 collections: Explore / Favorites / Created / Downloaded (design
    // req 4). "search" = the normal Explore search; else a /tones/{kind} list.
    juce::ComboBox sourceCombo;
    juce::String sourceMode = "search";

    // error banner
    juce::TextButton retryButton { "Try again" };
    juce::TextButton dismissButton { "X" };

    // grid
    juce::Viewport viewport;
    juce::Component gridContent;
    juce::OwnedArray<ToneCardComponent> cards;
    juce::TextButton loadMoreButton { "Load more" };

    // Plugins tab: embedded catalog manager (install/uninstall)
    juce::OwnedArray<juce::Component> pluginRows;
    void refreshPluginsTab();

    // Cache of variations per tone (1 API call per tone per session).
    std::map<int, std::vector<Tone3000Client::Model>> modelsCache;

    // Tone Details view (opened on card click; TONE3000 design requirement)
    std::unique_ptr<ToneDetailsView> detailsView;
    ToneCardComponent::Info detailsInfo;   // tone shown in the details view
    int detailsTargetLane = -1;            // >=0 = swap into that amp lane
    void openDetails (ToneCardComponent&);
    /// Same view, but for a tone that has no card - what the select_tone
    /// picker hands back.
    void openDetailsFor (const Tone3000Client::Tone&);
    void downloadFromDetails (const Tone3000Client::Model&, ModelRowComponent*);
    void ensureDetailsView();   // lazily builds detailsView + wires callbacks

public:
    /// Shows an inline variation picker (a call-out anchored to the amp card)
    /// listing the other captures of the tone loaded in that lane, with search
    /// for long lists. Picking one swaps the capture in that lane. Does NOT open
    /// the full store. Used by the amp card's "variations" control.
    void showVariationPicker (int lane, int toneId, juce::Component* anchor);
    /// Loads a specific variation into a lane (used by the picker + details).
    /// lane < 0 = first free lane. row (optional) shows download progress.
    void loadVariationIntoLane (const Tone3000Client::Model&, int lane,
                                const ToneCardComponent::Info& toneInfo,
                                ModelRowComponent* row = nullptr);
private:

    // full TONE3000 logo (list-view branding) + T3K mark (compact)
    juce::Image brandLogo, brandMark;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoreOverlay)
};
