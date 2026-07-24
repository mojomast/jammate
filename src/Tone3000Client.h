#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <functional>
#include <vector>

// Cliente da API do TONE3000 (https://www.tone3000.com — docs em /api).
//
// - Toda a API exige Bearer token obtido via OAuth 2.0 + PKCE; o client_id é a
//   chave publishable (t3k_pub_...) que o usuário cria em Settings -> API Keys
//   e configura em Documentos/PedalForge NAM/tone3000.json, junto com o
//   redirect http://localhost:53682/callback registrado na mesma tela.
// - Rede roda num ThreadPool próprio; os callbacks são entregues na message
//   thread via MessageManager::callAsync.
class Tone3000Client
{
public:
    Tone3000Client();
    ~Tone3000Client();

    static constexpr int kCallbackPort = 53682;
    static juce::String redirectUri();

    struct Tone
    {
        int id = 0;
        juce::String title, creator, gear, format;
        juce::String creatorAvatar;    // avatar do criador (user.avatar_url)
        juce::String description;      // descrição do criador (detalhe do tone)
        juce::String imageUrl; // primeira imagem do tone ("" se não houver)
        juce::String url;      // página do tone em tone3000.com (API field "url")
        bool hasA2 = false;    // a2_models_count > 0
        int a1Count = 0, a2Count = 0;  // p/ a view de detalhes
        juce::int64 downloads = 0, favorites = 0;
    };

    /// Detalhe de um tone (GET /tones/{id}) — traz description, avatar, gear...
    void getTone (int toneId, std::function<void (Tone, juce::String error)> done);

    struct SearchResult
    {
        std::vector<Tone> tones;
        int page = 1, totalPages = 1;
        juce::String error; // vazio = sucesso
    };

    // ---- configuração / sessão (message thread) ----
    bool hasPublishableKey() const { return publishableKey.isNotEmpty(); }
    bool isConnected() const { return refreshToken.isNotEmpty(); }
    juce::String getUsername() const { return username; }
    juce::File configFile() const;
    void reloadConfig();
    void disconnect();

    /// Fluxo OAuth completo: abre o navegador, escuta o callback em
    /// localhost:53682, troca o code por tokens e busca o perfil.
    void connect (std::function<void (bool ok, juce::String error)> done);

    // ---- API (message thread -> callback na message thread) ----
    /// architecture: 0 = todas, 2 = só tones com modelos A2.
    void searchTones (const juce::String& query, const juce::String& gear,
                      const juce::String& sort, int page, int architecture,
                      std::function<void (SearchResult)> done);

    /// Lista os tones do usuário logado numa das coleções do TONE3000:
    /// kind = "favorited" | "created" | "downloaded" (GET /tones/{kind}).
    void listUserTones (const juce::String& kind, int page,
                        std::function<void (SearchResult)> done);

    struct Model
    {
        int id = 0;
        juce::String name, url, size, arch; // arch: "1" | "2" | "custom"
    };

    /// Lista os modelos/variações de um tone (mics, canais, tamanhos...),
    /// ordenados por preferência (A2 e standard primeiro).
    void listModels (int toneId,
                     std::function<void (std::vector<Model>, juce::String error)> done);

    /// Baixa um modelo específico para o diretório certo
    /// (kind "ir" -> IRs/, senão Captures/). baseName é o nome legível do
    /// arquivo (ex.: "Mesa Dual Rectifier - TB 57"); vazio usa o nome técnico
    /// do modelo. progress recebe 0..100.
    void downloadModel (const Model& model, const juce::String& kind,
                        const juce::String& baseName,
                        std::function<void (int)> progress,
                        std::function<void (juce::File, juce::String error)> done);

    /// Onde o modelo ficaria/fica salvo localmente — permite pular o
    /// download quando o arquivo já existe.
    static juce::File localFileForModel (const Model&, const juce::String& kind,
                                         const juce::String& baseName = {});

    /// Busca a imagem de um tone (com cache em disco). done só é chamado se a
    /// imagem carregar; formatos que o JUCE não decodifica (ex.: webp) são
    /// silenciosamente ignorados.
    void fetchImage (const juce::String& url, std::function<void (juce::Image)> done);

    /// Grava a foto do tone ao lado de um arquivo baixado
    /// (<arquivo>.img) — os cartões do rig usam esse sidecar.
    void saveImageSidecar (const juce::String& imageUrl, const juce::File& besideFile);

    static juce::File dataDir();
    static juce::File capturesDir();
    static juce::File irsDir();

private:
    juce::String apiGet (const juce::String& path, int& statusCode);
    bool ensureAccessToken (juce::String& error);   // pool thread
    bool refreshAccessToken (juce::String& error);  // pool thread
    void saveConfig();

    juce::String publishableKey, refreshToken, username;
    juce::String accessToken;               // só pool thread
    juce::int64 accessTokenExpiry = 0;      // só pool thread
    juce::CriticalSection configLock;

    // 1 thread: serializa a rede autenticada e evita corrida no accessToken.
    juce::ThreadPool pool { 1 };
    // Imagens são públicas (sem token) e podem baixar em paralelo.
    juce::ThreadPool imagePool { 2 };
    std::atomic<bool> connecting { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tone3000Client)
};
