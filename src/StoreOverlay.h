#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "LookAndFeel.h"
#include "Tone3000Client.h"

class GuitarRigNAMProcessor;

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
        juce::String formatBadge;    // "NAM" / "IR"
        juce::String imageUrl;       // tone image ("" = placeholder)
        bool a2 = false;             // has A2 models available
        juce::String downloads, favorites;   // formatted ("24.1k"); empty for local items
        bool offline = false;        // already exists locally
    };

    ToneCardComponent (Info info, std::function<void (ToneCardComponent&)> onAdd);

    void setStatus (Status s);
    void setProgress (int pct);
    void setImage (juce::Image);
    /// Associates the downloaded file with the card (enables the offline badge
    /// and status tracking by the overlay).
    void setLocalFile (const juce::File&);
    Status getStatus() const { return status; }
    const Info& getInfo() const { return info; }

    // favorite (star): persisted by the overlay in favoritos.json
    void setFavorite (bool fav);
    bool isFavorite() const { return favorite; }
    std::function<void (ToneCardComponent&)> onToggleFavorite;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Info info;
    Status status = Status::add;
    int progress = 0;
    bool favorite = false;
    juce::Image image;
    juce::TextButton addButton;
    juce::TextButton favButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneCardComponent)
};

//==============================================================================
// Tone Store overlay (TONE3000) per GuitarRig.dc.html.
class StoreOverlay : public juce::Component,
                     private juce::Timer
{
public:
    explicit StoreOverlay (GuitarRigNAMProcessor&);
    ~StoreOverlay() override;

    void visibilityChanged() override;

    void open();
    void openOnLibrary();   // used by the GUITARRIG_OPEN_STORE dev flag
    void openOnPlugins();   // VST3 plugin manager (embedded catalog)

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
    void startAddFlow (ToneCardComponent&);
    void showModelChoices (ToneCardComponent&, const std::vector<Tone3000Client::Model>&);
    void startDownload (ToneCardComponent&, const Tone3000Client::Model&);
    /// After downloading a capture: writes .meta, resolves the ECO pair
    /// (lighter variation with the same name) and hands the pair to the processor.
    void finalizeNamModel (const juce::File& mainFile, int toneId,
                           const Tone3000Client::Model& chosen, const juce::String& baseName);
    static void writeModelMeta (const juce::File&, const Tone3000Client::Model&);
    static int sizeRank (const juce::String&);
    void setTab (Tab);
    void doSearch (int page);
    void refreshLibrary();
    void rebuildCards (bool append);
    void layoutCards();
    void updateHeaderState();
    void addCardFor (const Tone3000Client::Tone&, bool appendToGrid);
    juce::String formatCount (juce::int64) const;

    GuitarRigNAMProcessor& processor;
    Tone3000Client client;

    Tab tab = Tab::explore;
    juce::String gearFilter;          // "" = all
    juce::String sortValue = "trending";
    int currentPage = 1, totalPages = 1;
    bool searching = false;
    juce::String bannerError;         // "" = no banner

    // header
    juce::TextButton closeButton { "X" };
    juce::TextButton exploreTab { "Explore" }, libraryTab { "My library" },
        pluginsTab { "Plugins" };
    juce::TextEditor searchBox;
    juce::TextButton connectButton { "Connect TONE3000" };
    juce::TextButton userChip;

    // filters
    juce::OwnedArray<juce::TextButton> gearChips;
    juce::OwnedArray<juce::TextButton> tagChips;   // multi-toggle; enter the query
    juce::TextButton a2Chip { "A2 only" };
    bool a2Only = false;
    // favorites (star): ids persisted in Documents\PedalForge NAM\favoritos.json
    juce::TextButton favChip { juce::String (juce::CharPointer_UTF8 ("Only \xe2\x98\x85")) };
    bool favOnly = false;
    juce::StringArray favIds;
    void loadFavorites();
    void saveFavorites() const;
    void toggleFavorite (ToneCardComponent&);
    juce::StringArray activeTags;
    juce::ComboBox sortCombo;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoreOverlay)
};
