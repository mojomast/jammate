#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Info info;
    Status status = Status::add;
    int progress = 0;
    juce::Image image;
    juce::TextButton addButton;

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

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab { explore, library };

    void timerCallback() override;
    /// Sincroniza os status dos cartões com o que está carregado no rig.
    void updateRigStatuses();
    /// Fluxo do Adicionar: lista os modelos do tone; um só -> baixa direto,
    /// vários -> menu de escolha (como no site do TONE3000).
    void startAddFlow (ToneCardComponent&);
    void startDownload (ToneCardComponent&, const Tone3000Client::Model&);
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
    juce::TextButton exploreTab { "Explorar" }, libraryTab { "Minha biblioteca" };
    juce::TextEditor searchBox;
    juce::TextButton connectButton { "Conectar TONE3000" };
    juce::TextButton userChip;

    // filtros
    juce::OwnedArray<juce::TextButton> gearChips;
    juce::ComboBox sortCombo;

    // banner de erro
    juce::TextButton retryButton { "Tentar novamente" };
    juce::TextButton dismissButton { "X" };

    // grid
    juce::Viewport viewport;
    juce::Component gridContent;
    juce::OwnedArray<ToneCardComponent> cards;
    juce::TextButton loadMoreButton { "Carregar mais" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoreOverlay)
};
