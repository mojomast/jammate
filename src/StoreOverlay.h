#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "LookAndFeel.h"
#include "Tone3000Client.h"

class GuitarRigNAMProcessor;

//==============================================================================
// Cartão de tone conforme ToneCard.dc.html.
class ToneCardComponent : public juce::Component
{
public:
    enum class Status { add, downloading, inRig };

    struct Info
    {
        int toneId = 0;              // 0 = item local (sem download)
        juce::File localFile;        // preenchido para itens locais/baixados
        juce::String title, creator, gear;   // gear: amp/pedal/full-rig/ir/...
        juce::String formatBadge;    // "NAM" / "IR"
        juce::String imageUrl;       // imagem do tone ("" = placeholder)
        bool a2 = false;             // tem modelos A2 disponíveis
        juce::String downloads, favorites;   // formatados ("24.1k"); vazios p/ locais
        bool offline = false;        // já existe localmente
    };

    ToneCardComponent (Info info, std::function<void (ToneCardComponent&)> onAdd);

    void setStatus (Status s);
    void setProgress (int pct);
    void setImage (juce::Image);
    /// Associa o arquivo baixado ao cartão (habilita o badge offline e o
    /// acompanhamento de status pelo overlay).
    void setLocalFile (const juce::File&);
    Status getStatus() const { return status; }
    const Info& getInfo() const { return info; }

    // favorito (★): persistido pelo overlay em favoritos.json
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
// Overlay do Tone Store (TONE3000) conforme GuitarRig.dc.html.
class StoreOverlay : public juce::Component,
                     private juce::Timer
{
public:
    explicit StoreOverlay (GuitarRigNAMProcessor&);
    ~StoreOverlay() override;

    void visibilityChanged() override;

    void open();
    void openOnLibrary();   // usado pelo flag de dev GUITARRIG_OPEN_STORE
    void openOnPlugins();   // gerenciador de plugins VST3 (catálogo embutido)

    bool keyPressed (const juce::KeyPress&) override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab { explore, library, plugins };

    void timerCallback() override;
    /// Sincroniza os status dos cartões com o que está carregado no rig.
    void updateRigStatuses();
    /// Fluxo do Adicionar: lista os modelos do tone; um só -> baixa direto,
    /// vários -> menu de escolha (como no site do TONE3000).
    void startAddFlow (ToneCardComponent&);
    void showModelChoices (ToneCardComponent&, const std::vector<Tone3000Client::Model>&);
    void startDownload (ToneCardComponent&, const Tone3000Client::Model&);
    /// Pós-download de um capture: grava .meta, resolve o par ECO (variação
    /// mais leve com o mesmo nome) e entrega o par ao processor.
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
    juce::String gearFilter;          // "" = tudo
    juce::String sortValue = "trending";
    int currentPage = 1, totalPages = 1;
    bool searching = false;
    juce::String bannerError;         // "" = sem banner

    // header
    juce::TextButton closeButton { "X" };
    juce::TextButton exploreTab { "Explorar" }, libraryTab { "Minha biblioteca" },
        pluginsTab { "Plugins" };
    juce::TextEditor searchBox;
    juce::TextButton connectButton { "Conectar TONE3000" };
    juce::TextButton userChip;

    // filtros
    juce::OwnedArray<juce::TextButton> gearChips;
    juce::OwnedArray<juce::TextButton> tagChips;   // multi-toggle; entram na query
    juce::TextButton a2Chip { juce::String (juce::CharPointer_UTF8 ("S\xc3\xb3 A2")) };
    bool a2Only = false;
    // favoritos (★): ids persistidos em Documentos\GuitarRig NAM\favoritos.json
    juce::TextButton favChip { juce::String (juce::CharPointer_UTF8 ("S\xc3\xb3 \xe2\x98\x85")) };
    bool favOnly = false;
    juce::StringArray favIds;
    void loadFavorites();
    void saveFavorites() const;
    void toggleFavorite (ToneCardComponent&);
    juce::StringArray activeTags;
    juce::ComboBox sortCombo;

    // banner de erro
    juce::TextButton retryButton { "Tentar novamente" };
    juce::TextButton dismissButton { "X" };

    // grid
    juce::Viewport viewport;
    juce::Component gridContent;
    juce::OwnedArray<ToneCardComponent> cards;
    juce::TextButton loadMoreButton { "Carregar mais" };

    // aba Plugins: gerenciador do catálogo embutido (instalar/desinstalar)
    juce::OwnedArray<juce::Component> pluginRows;
    void refreshPluginsTab();

    // Cache de variações por tone (1 chamada de API por tone por sessão).
    std::map<int, std::vector<Tone3000Client::Model>> modelsCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoreOverlay)
};
