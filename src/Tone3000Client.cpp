#include "Tone3000Client.h"

#include <juce_cryptography/juce_cryptography.h>

namespace
{
const juce::String kApiBase = "https://www.tone3000.com";

juce::String base64url (const void* data, size_t size)
{
    auto b64 = juce::Base64::toBase64 (data, size);
    return b64.replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
}

juce::String randomBase64url (int numBytes)
{
    juce::MemoryBlock block ((size_t) numBytes);
    juce::Random::getSystemRandom().fillBitsRandomly (block.getData(), block.getSize());
    return base64url (block.getData(), block.getSize());
}

juce::String urlEncode (const juce::String& s)
{
    return juce::URL::addEscapeChars (s, true);
}

juce::String sanitizeFilename (const juce::String& name)
{
    auto out = name.toLowerCase().retainCharacters (
        "abcdefghijklmnopqrstuvwxyz0123456789 -_");
    return out.trim().replaceCharacter (' ', '-');
}
} // namespace

Tone3000Client::Tone3000Client()
{
    reloadConfig();
}

Tone3000Client::~Tone3000Client()
{
    pool.removeAllJobs (true, 10000);
}

juce::String Tone3000Client::redirectUri()
{
    return "http://localhost:" + juce::String (kCallbackPort) + "/callback";
}

juce::File Tone3000Client::dataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("GuitarRig NAM");
    dir.createDirectory();
    return dir;
}

juce::File Tone3000Client::capturesDir()
{
    auto dir = dataDir().getChildFile ("Captures");
    dir.createDirectory();
    return dir;
}

juce::File Tone3000Client::irsDir()
{
    auto dir = dataDir().getChildFile ("IRs");
    dir.createDirectory();
    return dir;
}

juce::File Tone3000Client::configFile() const
{
    return dataDir().getChildFile ("tone3000.json");
}

void Tone3000Client::reloadConfig()
{
    const juce::ScopedLock sl (configLock);

    const auto file = configFile();
    if (! file.existsAsFile())
    {
        // Cria um template para o usuário preencher.
        file.replaceWithText (
            "{\n"
            "  \"publishable_key\": \"\",\n"
            "  \"refresh_token\": \"\",\n"
            "  \"username\": \"\"\n"
            "}\n");
    }

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    publishableKey = parsed.getProperty ("publishable_key", "").toString().trim();
    refreshToken = parsed.getProperty ("refresh_token", "").toString().trim();
    username = parsed.getProperty ("username", "").toString().trim();
}

void Tone3000Client::saveConfig()
{
    const juce::ScopedLock sl (configLock);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("publishable_key", publishableKey);
    obj->setProperty ("refresh_token", refreshToken);
    obj->setProperty ("username", username);
    configFile().replaceWithText (juce::JSON::toString (juce::var (obj), true));
}

void Tone3000Client::disconnect()
{
    {
        const juce::ScopedLock sl (configLock);
        refreshToken.clear();
        username.clear();
        accessToken.clear();
        accessTokenExpiry = 0;
    }
    saveConfig();
}

//==============================================================================
// OAuth

void Tone3000Client::connect (std::function<void (bool, juce::String)> done)
{
    if (connecting.exchange (true))
        return;

    const auto key = publishableKey;

    pool.addJob ([this, key, done]
    {
        auto finish = [this, done] (bool ok, juce::String error)
        {
            connecting.store (false);
            juce::MessageManager::callAsync ([done, ok, error] { done (ok, error); });
        };

        // PKCE
        const auto codeVerifier = randomBase64url (32);
        const auto state = randomBase64url (16);
        const juce::SHA256 hash (codeVerifier.toRawUTF8(),
                                 (size_t) codeVerifier.getNumBytesAsUTF8());
        const auto challenge = base64url (hash.getRawData().getData(),
                                          hash.getRawData().getSize());

        // Listener ANTES de abrir o navegador (evita corrida).
        juce::StreamingSocket server;
        if (! server.createListener (kCallbackPort, "127.0.0.1"))
        {
            finish (false, "Porta " + juce::String (kCallbackPort) + " ocupada");
            return;
        }

        const auto authorizeUrl =
            kApiBase + "/api/v1/oauth/authorize"
            + "?client_id=" + urlEncode (key)
            + "&redirect_uri=" + urlEncode (redirectUri())
            + "&response_type=code"
            + "&code_challenge=" + urlEncode (challenge)
            + "&code_challenge_method=S256"
            + "&state=" + urlEncode (state)
            + "&format=nam";

        juce::MessageManager::callAsync ([authorizeUrl]
        {
            juce::URL (authorizeUrl).launchInDefaultBrowser();
        });

        // Espera o redirect (até 3 minutos).
        std::unique_ptr<juce::StreamingSocket> conn;
        for (int i = 0; i < 180 && conn == nullptr; ++i)
        {
            if (server.waitUntilReady (true, 1000) == 1)
                conn.reset (server.waitForNextConnection());
        }

        if (conn == nullptr)
        {
            finish (false, "Tempo esgotado aguardando o login");
            return;
        }

        char buf[8192] = {};
        const int numRead = conn->read (buf, sizeof (buf) - 1, false);
        const juce::String request = juce::String::fromUTF8 (buf, juce::jmax (0, numRead));

        const juce::String reply =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
            "<html><body style=\"background:#141517;color:#e5e6e8;font-family:sans-serif;"
            "display:flex;align-items:center;justify-content:center;height:100vh\">"
            "<h2>Autorizado &mdash; volte ao GuitarRig NAM.</h2></body></html>";
        conn->write (reply.toRawUTF8(), (int) reply.getNumBytesAsUTF8());
        conn->close();
        server.close();

        // Extrai os query params da primeira linha: GET /callback?... HTTP/1.1
        const auto firstLine = request.upToFirstOccurrenceOf ("\r\n", false, false);
        const auto query = firstLine.fromFirstOccurrenceOf ("?", false, false)
                               .upToFirstOccurrenceOf (" ", false, false);
        juce::String code, returnedState, oauthError;
        for (const auto& pair : juce::StringArray::fromTokens (query, "&", ""))
        {
            const auto k = pair.upToFirstOccurrenceOf ("=", false, false);
            const auto v = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));
            if (k == "code") code = v;
            else if (k == "state") returnedState = v;
            else if (k == "error") oauthError = v;
        }

        if (oauthError.isNotEmpty()) { finish (false, oauthError); return; }
        if (returnedState != state) { finish (false, "state divergente (CSRF?)"); return; }
        if (code.isEmpty()) { finish (false, "callback sem code"); return; }

        // Troca o code por tokens.
        const juce::String form =
            "grant_type=authorization_code&code=" + urlEncode (code)
            + "&code_verifier=" + urlEncode (codeVerifier)
            + "&redirect_uri=" + urlEncode (redirectUri())
            + "&client_id=" + urlEncode (key);

        juce::URL tokenUrl (kApiBase + "/api/v1/oauth/token");
        juce::WebInputStream stream (tokenUrl.withPOSTData (form), true);
        stream.withExtraHeaders ("Content-Type: application/x-www-form-urlencoded");
        stream.connect (nullptr);

        if (stream.getStatusCode() != 200)
        {
            finish (false, "Troca de tokens falhou (HTTP "
                            + juce::String (stream.getStatusCode()) + ")");
            return;
        }

        const auto json = juce::JSON::parse (stream.readEntireStreamAsString());
        const auto newAccess = json.getProperty ("access_token", "").toString();
        const auto newRefresh = json.getProperty ("refresh_token", "").toString();
        const double expiresIn = (double) json.getProperty ("expires_in", 3600.0);

        if (newAccess.isEmpty() || newRefresh.isEmpty())
        {
            finish (false, "Resposta de token invalida");
            return;
        }

        {
            const juce::ScopedLock sl (configLock);
            refreshToken = newRefresh;
        }
        accessToken = newAccess;
        accessTokenExpiry = juce::Time::currentTimeMillis() + (juce::int64) (expiresIn * 1000.0) - 60000;

        // Perfil (nome para o chip da UI).
        int status = 0;
        const auto userJson = apiGet ("/api/v1/user", status);
        if (status == 200)
        {
            const auto user = juce::JSON::parse (userJson);
            const juce::ScopedLock sl (configLock);
            username = user.getProperty ("username", "").toString();
        }

        saveConfig();
        finish (true, {});
    });
}

//==============================================================================
// Chamadas autenticadas (pool thread)

bool Tone3000Client::refreshAccessToken (juce::String& error)
{
    juce::String key, refresh;
    {
        const juce::ScopedLock sl (configLock);
        key = publishableKey;
        refresh = refreshToken;
    }

    if (refresh.isEmpty())
    {
        error = "Nao conectado";
        return false;
    }

    const juce::String form = "grant_type=refresh_token&refresh_token=" + urlEncode (refresh)
                              + "&client_id=" + urlEncode (key);

    juce::URL tokenUrl (kApiBase + "/api/v1/oauth/token");
    juce::WebInputStream stream (tokenUrl.withPOSTData (form), true);
    stream.withExtraHeaders ("Content-Type: application/x-www-form-urlencoded");
    stream.connect (nullptr);

    if (stream.getStatusCode() != 200)
    {
        error = "Sessao expirada - reconecte sua conta";
        return false;
    }

    const auto json = juce::JSON::parse (stream.readEntireStreamAsString());
    accessToken = json.getProperty ("access_token", "").toString();
    const auto newRefresh = json.getProperty ("refresh_token", "").toString();
    const double expiresIn = (double) json.getProperty ("expires_in", 3600.0);
    accessTokenExpiry = juce::Time::currentTimeMillis() + (juce::int64) (expiresIn * 1000.0) - 60000;

    if (newRefresh.isNotEmpty())
    {
        {
            const juce::ScopedLock sl (configLock);
            refreshToken = newRefresh;
        }
        saveConfig();
    }

    return accessToken.isNotEmpty();
}

bool Tone3000Client::ensureAccessToken (juce::String& error)
{
    if (accessToken.isNotEmpty() && juce::Time::currentTimeMillis() < accessTokenExpiry)
        return true;
    return refreshAccessToken (error);
}

juce::String Tone3000Client::apiGet (const juce::String& path, int& statusCode)
{
    juce::URL url (path.startsWith ("http") ? path : kApiBase + path);
    juce::WebInputStream stream (url, false);
    stream.withExtraHeaders ("Authorization: Bearer " + accessToken);
    stream.connect (nullptr);
    statusCode = stream.getStatusCode();
    return stream.readEntireStreamAsString();
}

void Tone3000Client::searchTones (const juce::String& query, const juce::String& gear,
                                  const juce::String& sort, int page,
                                  std::function<void (SearchResult)> done)
{
    pool.addJob ([this, query, gear, sort, page, done]
    {
        SearchResult result;
        result.page = page;

        auto deliver = [done] (SearchResult r)
        {
            juce::MessageManager::callAsync ([done, r = std::move (r)] { done (r); });
        };

        if (! ensureAccessToken (result.error))
        {
            deliver (std::move (result));
            return;
        }

        juce::String path = "/api/v1/tones/search?page=" + juce::String (page)
                            + "&page_size=24&sort=" + urlEncode (sort);
        if (query.isNotEmpty())
            path += "&query=" + urlEncode (query);
        if (gear.isNotEmpty())
            path += "&gears=" + urlEncode (gear);

        // Uma tentativa + um retry após refresh em caso de 401.
        int status = 0;
        juce::String body;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            body = apiGet (path, status);
            if (status != 401)
                break;
            juce::String err;
            if (! refreshAccessToken (err))
            {
                result.error = "Sessao expirada - reconecte sua conta";
                deliver (std::move (result));
                return;
            }
        }

        if (status != 200)
        {
            result.error = "Sem conexao com TONE3000 (HTTP " + juce::String (status) + ")";
            deliver (std::move (result));
            return;
        }

        const auto json = juce::JSON::parse (body);
        result.totalPages = (int) json.getProperty ("total_pages", 1);

        if (auto* arr = json.getProperty ("data", juce::var()).getArray())
        {
            for (const auto& t : *arr)
            {
                Tone tone;
                tone.id = (int) t.getProperty ("id", 0);
                tone.title = t.getProperty ("title", "").toString();
                tone.creator = t.getProperty ("user", juce::var())
                                   .getProperty ("username", "").toString();
                tone.gear = t.getProperty ("gear", "").toString();
                tone.format = t.getProperty ("format", "").toString();
                tone.downloads = (juce::int64) t.getProperty ("downloads_count", 0);
                tone.favorites = (juce::int64) t.getProperty ("favorites_count", 0);

                // Só formatos que o GuitarRig consegue usar hoje.
                if (tone.format == "nam" || tone.format == "ir")
                    result.tones.push_back (tone);
            }
        }

        deliver (std::move (result));
    });
}

void Tone3000Client::downloadTone (int toneId, const juce::String& gear,
                                   std::function<void (int)> progress,
                                   std::function<void (juce::File, juce::String)> done)
{
    pool.addJob ([this, toneId, gear, progress, done]
    {
        auto fail = [done] (juce::String error)
        {
            juce::MessageManager::callAsync ([done, error] { done ({}, error); });
        };

        juce::String error;
        if (! ensureAccessToken (error))
        {
            fail (error);
            return;
        }

        // 1) Modelos do tone — preferimos A2, depois standard.
        int status = 0;
        const auto body = apiGet ("/api/v1/models?tone_id=" + juce::String (toneId)
                                  + "&page_size=50", status);
        if (status != 200)
        {
            fail ("Falha ao listar modelos (HTTP " + juce::String (status) + ")");
            return;
        }

        const auto json = juce::JSON::parse (body);
        auto* arr = json.getProperty ("data", juce::var()).getArray();
        if (arr == nullptr || arr->isEmpty())
        {
            fail ("Tone sem modelos disponiveis");
            return;
        }

        auto score = [] (const juce::var& m)
        {
            int s = 0;
            if (m.getProperty ("architecture_version", "").toString() == "2") s += 10;
            if (m.getProperty ("size", "").toString() == "standard") s += 5;
            return s;
        };

        juce::var best = arr->getFirst();
        for (const auto& m : *arr)
            if (score (m) > score (best))
                best = m;

        const auto modelUrl = best.getProperty ("model_url", "").toString();
        const auto modelName = best.getProperty ("name", "").toString();
        if (modelUrl.isEmpty())
        {
            fail ("Modelo sem URL de download");
            return;
        }

        // 2) Download autenticado com progresso.
        juce::URL url (modelUrl);
        juce::WebInputStream stream (url, false);
        stream.withExtraHeaders ("Authorization: Bearer " + accessToken);
        stream.connect (nullptr);

        if (stream.getStatusCode() != 200)
        {
            fail ("Download falhou (HTTP " + juce::String (stream.getStatusCode()) + ")");
            return;
        }

        const auto storageName = url.getFileName();
        const auto ext = storageName.contains (".")
                             ? storageName.fromLastOccurrenceOf (".", true, false)
                             : (gear == "ir" ? juce::String (".wav") : juce::String (".nam"));

        const auto targetDir = gear == "ir" ? irsDir() : capturesDir();
        auto target = targetDir.getChildFile (
            sanitizeFilename (modelName.isNotEmpty() ? modelName : storageName) + ext);

        target.deleteFile();
        juce::FileOutputStream out (target);
        if (! out.openedOk())
        {
            fail ("Nao foi possivel criar " + target.getFullPathName());
            return;
        }

        const juce::int64 total = stream.getTotalLength();
        juce::int64 written = 0;
        int lastPct = -1;
        juce::HeapBlock<char> chunk (1 << 15);

        while (! stream.isExhausted())
        {
            const int numRead = stream.read (chunk.getData(), 1 << 15);
            if (numRead <= 0)
                break;
            out.write (chunk.getData(), (size_t) numRead);
            written += numRead;

            if (total > 0)
            {
                const int pct = (int) (written * 100 / total);
                if (pct != lastPct)
                {
                    lastPct = pct;
                    juce::MessageManager::callAsync ([progress, pct] { progress (pct); });
                }
            }
        }
        out.flush();

        if (written <= 0)
        {
            target.deleteFile();
            fail ("Download vazio");
            return;
        }

        juce::MessageManager::callAsync ([done, target] { done (target, {}); });
    });
}
