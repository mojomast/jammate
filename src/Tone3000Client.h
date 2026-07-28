#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <functional>
#include <vector>

// TONE3000 API client (https://www.tone3000.com, docs at /api).
//
// TIER: this app targets the FREE, NON-COMMERCIAL tier, which allows only the
// OAuth prompt flows (select_tone / load_tone) plus the bounded list endpoints
// (trending, latest). The paginated search and the user-collection endpoints
// are FULL API ACCESS and need a signed commercial agreement, so they are not
// used here - browsing happens in TONE3000's own picker via selectTone().
//
// - Every call needs a Bearer token obtained through OAuth 2.0 + PKCE. The
//   client_id is the publishable key (t3k_pub_...) the user creates at
//   tone3000.com -> Settings -> API Keys, registering the redirect
//   http://localhost:53682/callback. It is entered in the app (Tone Store ->
//   TONE3000 access) and persisted in Documents/PedalForge NAM/tone3000.json.
// - Networking runs on its own ThreadPool; callbacks are delivered on the
//   message thread via MessageManager::callAsync.
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

    /// The user's own publishable key. Each person uses their own, so the rate
    /// limit is theirs and no credential ships in the repository.
    juce::String getPublishableKey() const { return publishableKey; }
    /// Stores and persists the key; disconnects first if the key actually
    /// changed, because the existing refresh token belongs to the old client.
    void setPublishableKey (const juce::String& key);
    /// Basic shape check for the UI ("t3k_pub_" prefix), not a validation.
    static bool looksLikePublishableKey (const juce::String& key);
    /// Page where the key and the redirect URI are registered.
    static juce::String apiKeysUrl();

    /// Fluxo OAuth completo: abre o navegador, escuta o callback em
    /// localhost:53682, troca o code por tokens e busca o perfil.
    void connect (std::function<void (bool ok, juce::String error)> done);

    // ---- prompt flows (the browse path allowed on the free tier) ----------
    /// "select_tone": opens TONE3000's own picker in the browser; the user
    /// chooses a tone there and it comes back here. gears/architecture scope
    /// what the picker offers ("" / 0 = unscoped). Also authenticates, so it
    /// doubles as the connect step for a user who is not signed in yet.
    void selectTone (const juce::String& gears, int architecture,
                     std::function<void (Tone, juce::String error)> done);

    /// "load_tone": re-authorises a tone whose id we already stored (in a
    /// preset) and confirms the user still has access to it.
    void loadTone (int toneId, std::function<void (Tone, juce::String error)> done);

    /// Runs a prompt flow through the SYSTEM browser and the localhost
    /// listener. selectTone/loadTone go through here; the embedded panel also
    /// calls it directly when the user asks to finish in their own browser.
    void runPromptFlowInSystemBrowser (const juce::String& promptParams,
                                       std::function<void (Tone, juce::String error)> done);

    // ---- driving the flows from an embedded browser ----------------------
    // The panel (ToneWebView) opens the authorize URL itself and catches the
    // redirect before it is ever requested, so the localhost:53682 listener is
    // not involved. The redirect URI string stays the same because it is what
    // the user registered with their publishable key.

    /// One authorisation in flight: what to keep between opening the page and
    /// the redirect coming back.
    struct AuthSession
    {
        juce::String codeVerifier, state, authorizeUrl;
    };

    /// Reserves the client for an embedded authorisation and fills `session`.
    /// Returns false (with `error` set) if one is already running or no key is
    /// configured. On success you MUST end it with one of the finish* calls or
    /// with cancelEmbeddedAuth().
    bool beginEmbeddedAuth (const juce::String& promptParams,
                            AuthSession& session, juce::String& error);
    /// Releases the reservation when the user closes the panel.
    void cancelEmbeddedAuth();
    /// Completes a plain sign-in from the captured redirect URL.
    void finishEmbeddedConnect (const AuthSession&, const juce::String& callbackUrl,
                                std::function<void (bool ok, juce::String error)> done);
    /// Completes a prompt flow: exchanges the code and reads the chosen tone.
    void finishEmbeddedTone (const AuthSession&, const juce::String& callbackUrl,
                             std::function<void (Tone, juce::String error)> done);

    /// True when a URL the browser is about to load is our OAuth redirect.
    static bool isRedirectUrl (const juce::String& url);
    /// Query string of a callback URL (no "?", no fragment).
    static juce::String queryFromCallbackUrl (const juce::String& url);

    /// Re-mints the PKCE challenge of an authorisation ALREADY reserved by
    /// beginEmbeddedAuth(), so the panel can reload the picker with different
    /// parameters without releasing (and re-taking) the reservation.
    bool renewEmbeddedAuth (const juce::String& promptParams,
                            AuthSession& session, juce::String& error) const;

    /// Authorize parameters for each prompt flow (also used to sign in: the
    /// picker authenticates on the way).
    static juce::String selectToneParams (const juce::String& gears, int architecture);
    static juce::String loadToneParams (int toneId);

    /// Architecture filter of the picker. THEIR API takes ONE value - 1 (A1),
    /// 2 (A2) or "custom" - and there is no way to ask for all of them. Worse,
    /// OMITTING it is not neutral: their documented legacy default is
    /// "A1 + Custom", which EXCLUDES A2, so every A2-only tone shows up as
    /// "Not supported" even though this app plays A2 fine. That is why the
    /// choice is surfaced in the browser panel instead of being hard-coded.
    enum class Architecture { a1AndCustom = 0, a2 = 2 };
    static juce::String architectureParam (Architecture);

    /// Format filter of the picker. Same one-value-only limitation as the
    /// architecture, and this app reads TWO of their five formats, so the
    /// picker cannot be scoped to "ours" - it is scoped to one at a time and
    /// the panel lets the user switch. Leaving it out is what showed AIDA-X /
    /// Proteus / Amped Roots tones that nothing here can open.
    enum class Format { nam, ir };
    static juce::String formatParam (Format);
    /// TONE3000 gear for a format: IRs are published under gear "cab" - "ir"
    /// is a FORMAT and is not a valid gear value.
    static juce::String gearsForPicker (const juce::String& gearFilter);
    /// Parameters of a plain sign-in (no picker). "&format=nam" keeps the
    /// sign-in page scoped the way it always was.
    static juce::String connectParams() { return "&format=nam"; }

    // ---- bounded list endpoints (allowed on the free tier) ---------------
    /// Top 10 trending tones for a gear type ("amp", "amp-cab", "pedal"...).
    void listTrending (const juce::String& gear, std::function<void (SearchResult)> done);
    /// The 10 most recent tones (unpaginated).
    void listLatest (std::function<void (SearchResult)> done);

    // ---- API (message thread -> callback na message thread) ----
    // searchTones() and listUserTones() were REMOVED: /tones/search and
    // /tones/{favorited|created|downloaded} are full API access, which needs a
    // signed commercial agreement. Nothing in this build can reach them.

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

    /// Result of one authorisation round trip. toneId is only set by the
    /// prompt flows, which hand a tone back on the callback.
    struct AuthOutcome
    {
        bool ok = false;
        juce::String error;
        int toneId = 0;
    };
    /// Builds one PKCE authorisation (verifier, state, URL). Any thread.
    bool prepareAuth (const juce::String& promptParams, AuthSession&,
                      juce::String& error) const;
    /// pool thread: validates the callback query and exchanges the code for
    /// tokens (then reads the profile and saves the config).
    AuthOutcome completeAuth (const juce::String& callbackQuery, const AuthSession&);
    /// pool thread: the whole PKCE dance through the SYSTEM browser (listener on
    /// 53682, browser, callback, completeAuth). promptParams is appended to the
    /// authorize URL - "&format=nam" for a plain connect, "&prompt=select_tone&..."
    /// for the flows.
    AuthOutcome runAuthFlow (const juce::String& promptParams);
    /// pool thread: reads the tone a prompt flow handed back and delivers it.
    void deliverToneFor (const AuthOutcome&,
                         const std::function<void (Tone, juce::String)>& deliver);
    /// Shared body of select_tone/load_tone: authorise, then read the tone the
    /// callback handed back (the continuation the API guide documents).
    void runToneFlow (const juce::String& promptParams,
                      std::function<void (Tone, juce::String error)> done);
    /// pool thread: GET an unpaginated list endpoint into a SearchResult.
    void fetchBoundedList (const juce::String& path, std::function<void (SearchResult)> done);

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
