#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <vector>

// Cliente da API do TONE3000 (https://www.tone3000.com — docs em /api).
//
// - Toda a API exige Bearer token obtido via OAuth 2.0 + PKCE; o client_id é a
//   chave publishable (t3k_pub_...) que o usuário cria em Settings -> API Keys
//   e configura em Documentos/GuitarRig NAM/tone3000.json, junto com o
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
        juce::int64 downloads = 0, favorites = 0;
    };

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
    void searchTones (const juce::String& query, const juce::String& gear,
                      const juce::String& sort, int page,
                      std::function<void (SearchResult)> done);

    /// Baixa o melhor modelo de um tone para o diretório certo
    /// (Captures/ para nam, IRs/ para ir). progress recebe 0..100.
    void downloadTone (int toneId, const juce::String& gear,
                       std::function<void (int)> progress,
                       std::function<void (juce::File, juce::String error)> done);

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

    // 1 thread: serializa a rede e evita corrida no accessToken.
    juce::ThreadPool pool { 1 };
    std::atomic<bool> connecting { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tone3000Client)
};
