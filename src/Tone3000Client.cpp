#include "Tone3000Client.h"

#include <juce_cryptography/juce_cryptography.h>

#include <set>

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
    imagePool.removeAllJobs (true, 5000);
}

juce::String Tone3000Client::redirectUri()
{
    return "http://localhost:" + juce::String (kCallbackPort) + "/callback";
}

juce::File Tone3000Client::dataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("PedalForge NAM");
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
        // Create a template for the user to fill in.
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

bool Tone3000Client::looksLikePublishableKey (const juce::String& key)
{
    return key.trim().startsWith ("t3k_pub_") && key.trim().length() > 12;
}

juce::String Tone3000Client::apiKeysUrl()
{
    return kApiBase + "/settings/api-keys";
}

void Tone3000Client::setPublishableKey (const juce::String& key)
{
    const auto trimmed = key.trim();
    bool changed = false;
    {
        const juce::ScopedLock sl (configLock);
        changed = trimmed != publishableKey;
        publishableKey = trimmed;
        if (changed)
        {
            // the refresh token was issued to the OLD client_id - keeping it
            // would only produce confusing 401s on the next call
            refreshToken.clear();
            username.clear();
            accessToken.clear();
            accessTokenExpiry = 0;
        }
    }
    saveConfig();
}

//==============================================================================
// OAuth

void Tone3000Client::connect (std::function<void (bool, juce::String)> done)
{
    if (connecting.exchange (true))
    {
        done (false, "A TONE3000 authorisation is already in progress");
        return;
    }

    pool.addJob ([this, done]
    {
        // "&format=nam" keeps the sign-in page scoped the way it always was
        const auto r = runAuthFlow ("&format=nam");
        connecting.store (false);
        juce::MessageManager::callAsync ([done, r] { done (r.ok, r.error); });
    });
}

// The whole PKCE round trip. Shared by the plain connect and by the prompt
// flows (select_tone / load_tone), which differ only in the authorize params
// and in the tone_id that comes back on the callback.
Tone3000Client::AuthOutcome Tone3000Client::runAuthFlow (const juce::String& promptParams)
{
    AuthOutcome out;
    juce::String key;
    {
        const juce::ScopedLock sl (configLock);
        key = publishableKey;
    }
    if (key.isEmpty())
    {
        out.error = "No TONE3000 key set";
        return out;
    }

    {
        // PKCE
        const auto codeVerifier = randomBase64url (32);
        const auto state = randomBase64url (16);
        const juce::SHA256 hash (codeVerifier.toRawUTF8(),
                                 (size_t) codeVerifier.getNumBytesAsUTF8());
        const auto challenge = base64url (hash.getRawData().getData(),
                                          hash.getRawData().getSize());

        // Listener BEFORE opening the browser (avoids a race).
        juce::StreamingSocket server;
        if (! server.createListener (kCallbackPort, "127.0.0.1"))
        {
            out.error = "Port " + juce::String (kCallbackPort) + " is busy";
            return out;
        }

        const auto authorizeUrl =
            kApiBase + "/api/v1/oauth/authorize"
            + "?client_id=" + urlEncode (key)
            + "&redirect_uri=" + urlEncode (redirectUri())
            + "&response_type=code"
            + "&code_challenge=" + urlEncode (challenge)
            + "&code_challenge_method=S256"
            + "&state=" + urlEncode (state)
            + promptParams;

        juce::MessageManager::callAsync ([authorizeUrl]
        {
            juce::URL (authorizeUrl).launchInDefaultBrowser();
        });

        // Waits for the redirect (up to 3 minutes). Browsers open empty
        // speculative connections and request /favicon.ico before the real
        // redirect - those get a 404 and are IGNORED; we only leave the loop
        // when a request with the OAuth callback parameters arrives.
        juce::String request;
        const auto deadline = juce::Time::currentTimeMillis() + 180000;

        while (juce::Time::currentTimeMillis() < deadline)
        {
            if (server.waitUntilReady (true, 1000) != 1)
                continue;

            std::unique_ptr<juce::StreamingSocket> conn (server.waitForNextConnection());
            if (conn == nullptr)
                continue;

            juce::String thisRequest;
            if (conn->waitUntilReady (true, 3000) == 1)
            {
                char buf[8192] = {};
                const int numRead = conn->read (buf, sizeof (buf) - 1, false);
                thisRequest = juce::String::fromUTF8 (buf, juce::jmax (0, numRead));
            }

            const auto line = thisRequest.upToFirstOccurrenceOf ("\r\n", false, false);
            const bool isCallback = line.startsWith ("GET ")
                                    && (line.contains ("code=") || line.contains ("error=")
                                        || line.contains ("state=") || line.contains ("tone_id="));

            if (! isCallback)
            {
                const juce::String notFound =
                    "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                conn->write (notFound.toRawUTF8(), (int) notFound.getNumBytesAsUTF8());
                conn->close();
                continue;
            }

            const juce::String reply =
                "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
                "<html><body style=\"background:#141517;color:#e5e6e8;font-family:sans-serif;"
                "display:flex;align-items:center;justify-content:center;height:100vh\">"
                "<h2>Authorized &mdash; go back to PedalForge NAM.</h2></body></html>";
            conn->write (reply.toRawUTF8(), (int) reply.getNumBytesAsUTF8());
            conn->close();
            request = thisRequest;
            break;
        }

        server.close();

        if (request.isEmpty())
        {
            out.error = "Timed out waiting for login";
            return out;
        }

        // Extracts the query params from the first line: GET /callback?... HTTP/1.1
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
            else if (k == "tone_id") out.toneId = v.getIntValue();   // prompt flows
        }

        if (oauthError.isNotEmpty()) { out.error = oauthError; return out; }
        if (returnedState != state) { out.error = "state mismatch (CSRF?)"; return out; }
        if (code.isEmpty()) { out.error = "callback without code"; return out; }

        // Exchanges the code for tokens.
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
            out.error = "Token exchange failed (HTTP "
                        + juce::String (stream.getStatusCode()) + ")";
            return out;
        }

        const auto json = juce::JSON::parse (stream.readEntireStreamAsString());
        const auto newAccess = json.getProperty ("access_token", "").toString();
        const auto newRefresh = json.getProperty ("refresh_token", "").toString();
        const double expiresIn = (double) json.getProperty ("expires_in", 3600.0);

        if (newAccess.isEmpty() || newRefresh.isEmpty())
        {
            out.error = "Invalid token response";
            return out;
        }

        {
            const juce::ScopedLock sl (configLock);
            refreshToken = newRefresh;
        }
        accessToken = newAccess;
        accessTokenExpiry = juce::Time::currentTimeMillis() + (juce::int64) (expiresIn * 1000.0) - 60000;

        // Profile (name for the UI chip).
        int status = 0;
        const auto userJson = apiGet ("/api/v1/user", status);
        if (status == 200)
        {
            const auto user = juce::JSON::parse (userJson);
            const juce::ScopedLock sl (configLock);
            username = user.getProperty ("username", "").toString();
        }

        saveConfig();
        out.ok = true;
        return out;
    }
}

//==============================================================================
// Authenticated calls (pool thread)

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
        error = "Not connected";
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
        error = "Session expired - reconnect your account";
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

// Parse one tone JSON object (shared by search and getTone).
static Tone3000Client::Tone parseToneJson (const juce::var& t)
{
    Tone3000Client::Tone tone;
    tone.id = (int) t.getProperty ("id", 0);
    tone.title = t.getProperty ("title", "").toString();
    const auto user = t.getProperty ("user", juce::var());
    tone.creator = user.getProperty ("username", "").toString();
    tone.creatorAvatar = user.getProperty ("avatar_url",
                             user.getProperty ("avatar",
                                 user.getProperty ("image_url", ""))).toString();
    tone.gear = t.getProperty ("gear", "").toString();
    tone.format = t.getProperty ("format", "").toString();
    tone.description = t.getProperty ("description", "").toString();
    tone.url = t.getProperty ("url", "").toString();
    tone.downloads = (juce::int64) t.getProperty ("downloads_count", 0);
    tone.favorites = (juce::int64) t.getProperty ("favorites_count", 0);
    tone.a1Count = (int) t.getProperty ("a1_models_count", 0);
    tone.a2Count = (int) t.getProperty ("a2_models_count", 0);
    tone.hasA2 = tone.a2Count > 0;
    if (auto* images = t.getProperty ("images", juce::var()).getArray())
        if (! images->isEmpty())
            tone.imageUrl = images->getFirst().toString();
    return tone;
}



void Tone3000Client::getTone (int toneId, std::function<void (Tone, juce::String error)> done)
{
    pool.addJob ([this, toneId, done]
    {
        auto deliver = [done] (Tone t, juce::String e)
        {
            juce::MessageManager::callAsync ([done, t = std::move (t), e = std::move (e)] { done (t, e); });
        };

        juce::String error;
        if (! ensureAccessToken (error)) { deliver ({}, error); return; }

        const juce::String path = "/api/v1/tones/" + juce::String (toneId);
        int status = 0;
        juce::String body;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            body = apiGet (path, status);
            if (status != 401)
                break;
            juce::String err;
            if (! refreshAccessToken (err)) { deliver ({}, "Session expired - reconnect your account"); return; }
        }
        if (status != 200) { deliver ({}, "Could not load tone (HTTP " + juce::String (status) + ")"); return; }

        const auto json = juce::JSON::parse (body);
        const auto obj = json.hasProperty ("data") ? json.getProperty ("data", juce::var()) : json;
        deliver (parseToneJson (obj), {});
    });
}

//==============================================================================
// Prompt flows + bounded lists - the only browse paths the free tier allows.

void Tone3000Client::runToneFlow (const juce::String& promptParams,
                                  std::function<void (Tone, juce::String)> done)
{
    if (connecting.exchange (true))
    {
        done ({}, "A TONE3000 authorisation is already in progress");
        return;
    }

    pool.addJob ([this, promptParams, done]
    {
        auto deliver = [done] (Tone t, juce::String e)
        {
            juce::MessageManager::callAsync ([done, t, e] { done (t, e); });
        };

        const auto auth = runAuthFlow (promptParams);
        connecting.store (false);

        if (! auth.ok)
        {
            deliver ({}, auth.error);
            return;
        }
        if (auth.toneId <= 0)
        {
            // the user closed the picker without choosing - not an error worth
            // shouting about, but the caller has to know nothing came back
            deliver ({}, "No tone was chosen");
            return;
        }

        // documented continuation: GET /api/v1/tones/{id}
        int status = 0;
        const auto body = apiGet ("/api/v1/tones/" + juce::String (auth.toneId), status);
        if (status != 200)
        {
            deliver ({}, "Could not read the chosen tone (HTTP "
                             + juce::String (status) + ")");
            return;
        }
        deliver (parseToneJson (juce::JSON::parse (body)), {});
    });
}

void Tone3000Client::selectTone (const juce::String& gears, int architecture,
                                 std::function<void (Tone, juce::String)> done)
{
    juce::String params = "&prompt=select_tone";
    if (gears.isNotEmpty())
        params += "&gears=" + urlEncode (gears);
    if (architecture > 0)
        params += "&architecture=" + juce::String (architecture);
    runToneFlow (params, std::move (done));
}

void Tone3000Client::loadTone (int toneId, std::function<void (Tone, juce::String)> done)
{
    runToneFlow ("&prompt=load_tone&tone_id=" + juce::String (toneId), std::move (done));
}

void Tone3000Client::fetchBoundedList (const juce::String& path,
                                       std::function<void (SearchResult)> done)
{
    pool.addJob ([this, path, done]
    {
        SearchResult result;

        auto deliver = [done] (SearchResult r)
        {
            juce::MessageManager::callAsync ([done, r = std::move (r)] { done (r); });
        };

        if (! ensureAccessToken (result.error))
        {
            deliver (std::move (result));
            return;
        }

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
                result.error = "Session expired - reconnect your account";
                deliver (std::move (result));
                return;
            }
        }

        if (status != 200)
        {
            result.error = "No connection to TONE3000 (HTTP " + juce::String (status) + ")";
            deliver (std::move (result));
            return;
        }

        // unpaginated: either a bare array or the usual {data:[...]} envelope
        const auto json = juce::JSON::parse (body);
        const juce::Array<juce::var>* arr = json.getArray();
        if (arr == nullptr)
            arr = json.getProperty ("data", juce::var()).getArray();

        if (arr != nullptr)
            for (const auto& t : *arr)
            {
                const Tone tone = parseToneJson (t);
                if (tone.format == "nam" || tone.format == "ir")   // what the rig can load
                    result.tones.push_back (tone);
            }

        deliver (std::move (result));
    });
}

void Tone3000Client::listTrending (const juce::String& gear,
                                   std::function<void (SearchResult)> done)
{
    // the endpoint requires a gear type; "amp" is the sensible default here
    fetchBoundedList ("/api/v1/tones/trending?gear="
                          + urlEncode (gear.isNotEmpty() ? gear : juce::String ("amp")),
                      std::move (done));
}

void Tone3000Client::listLatest (std::function<void (SearchResult)> done)
{
    fetchBoundedList ("/api/v1/tones/latest", std::move (done));
}

//==============================================================================
void Tone3000Client::fetchImage (const juce::String& url, std::function<void (juce::Image)> done)
{
    if (url.isEmpty())
        return;

    imagePool.addJob ([url, done]
    {
        auto cacheDir = dataDir().getChildFile (".cache").getChildFile ("images");
        cacheDir.createDirectory();
        auto cacheFile = cacheDir.getChildFile (
            juce::String::toHexString (url.hashCode64()) + ".img");

        juce::Image img;
        if (cacheFile.existsAsFile())
            img = juce::ImageFileFormat::loadFrom (cacheFile);

        if (img.isNull())
        {
            juce::URL u (url);
            juce::WebInputStream stream (u, false);
            stream.connect (nullptr);
            if (stream.getStatusCode() != 200)
                return;

            juce::MemoryBlock data;
            stream.readIntoMemoryBlock (data);
            img = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
            if (img.isValid())
                cacheFile.replaceWithData (data.getData(), data.getSize());
        }

        if (img.isNull())
            return; // unsupported format (e.g. webp) - the card keeps the placeholder

        // Downscale to ~2x the card width: cheap memory and blit.
        if (img.getWidth() > 560)
            img = img.rescaled (560, juce::jmax (1, juce::roundToInt (
                                          560.0 * img.getHeight() / img.getWidth())));

        juce::MessageManager::callAsync ([done, img] { done (img); });
    });
}

void Tone3000Client::listModels (int toneId,
                                 std::function<void (std::vector<Model>, juce::String)> done)
{
    pool.addJob ([this, toneId, done]
    {
        auto deliver = [done] (std::vector<Model> models, juce::String error)
        {
            juce::MessageManager::callAsync (
                [done, models = std::move (models), error] { done (models, error); });
        };

        juce::String error;
        if (! ensureAccessToken (error))
        {
            deliver ({}, error);
            return;
        }

        // API PITFALL: /models without a parameter returns ONLY the A1 models
        // (for IRs, it returns the IRs). A2 requires a separate call with
        // &architecture=2 - without it, the store never sees (or downloads) the A2s.
        std::vector<Model> models;
        std::set<int> seenIds;

        auto fetchModels = [&] (const juce::String& extraQuery, bool required) -> bool
        {
            int totalPages = 1;
            for (int page = 1; page <= totalPages && page <= 4; ++page)
            {
                int status = 0;
                const auto body = apiGet ("/api/v1/models?tone_id=" + juce::String (toneId)
                                          + "&page_size=50&page=" + juce::String (page)
                                          + extraQuery, status);
                if (status != 200)
                    return ! required; // the extra call may fail without bringing everything down

                const auto json = juce::JSON::parse (body);
                totalPages = (int) json.getProperty ("total_pages", 1);

                if (auto* arr = json.getProperty ("data", juce::var()).getArray())
                {
                    for (const auto& m : *arr)
                    {
                        Model model;
                        model.id = (int) m.getProperty ("id", 0);
                        model.name = m.getProperty ("name", "").toString();
                        model.url = m.getProperty ("model_url", "").toString();
                        model.size = m.getProperty ("size", "").toString();
                        model.arch = m.getProperty ("architecture_version", "").toString();
                        if (model.url.isNotEmpty() && seenIds.insert (model.id).second)
                            models.push_back (std::move (model));
                    }
                }
            }
            return true;
        };

        if (! fetchModels ({}, true)) // A1 (and IRs)
        {
            deliver ({}, "Failed to list models");
            return;
        }
        fetchModels ("&architecture=2", false); // A2 (separate call)

        if (models.empty())
        {
            deliver ({}, "Tone has no available models");
            return;
        }

        // A2 first, standard first; original order as tiebreaker.
        std::stable_sort (models.begin(), models.end(),
                          [] (const Model& a, const Model& b)
                          {
                              auto score = [] (const Model& m)
                              { return (m.arch == "2" ? 10 : 0) + (m.size == "standard" ? 5 : 0); };
                              return score (a) > score (b);
                          });

        deliver (std::move (models), {});
    });
}

juce::File Tone3000Client::localFileForModel (const Model& model, const juce::String& kind,
                                              const juce::String& baseName)
{
    const auto storageName = juce::URL (model.url).getFileName();
    const auto ext = storageName.contains (".")
                         ? storageName.fromLastOccurrenceOf (".", true, false)
                         : (kind == "ir" ? juce::String (".wav") : juce::String (".nam"));
    const auto dir = kind == "ir" ? irsDir() : capturesDir();

    // Readable name (tone title + variation), preserving capitals and
    // spaces; fallback: the model's technical name.
    auto base = baseName.isNotEmpty()
                    ? juce::File::createLegalFileName (baseName).trim()
                    : sanitizeFilename (model.name.isNotEmpty() ? model.name : storageName);
    if (base.isEmpty())
        base = "tone";
    return dir.getChildFile (base + ext);
}

void Tone3000Client::saveImageSidecar (const juce::String& imageUrl, const juce::File& besideFile)
{
    if (imageUrl.isEmpty() || ! besideFile.existsAsFile())
        return;

    imagePool.addJob ([imageUrl, besideFile]
    {
        auto cacheDir = dataDir().getChildFile (".cache").getChildFile ("images");
        cacheDir.createDirectory();
        auto cacheFile = cacheDir.getChildFile (
            juce::String::toHexString (imageUrl.hashCode64()) + ".img");

        if (! cacheFile.existsAsFile())
        {
            juce::URL u (imageUrl);
            juce::WebInputStream stream (u, false);
            stream.connect (nullptr);
            if (stream.getStatusCode() != 200)
                return;
            juce::MemoryBlock data;
            stream.readIntoMemoryBlock (data);
            if (data.getSize() == 0)
                return;
            cacheFile.replaceWithData (data.getData(), data.getSize());
        }

        cacheFile.copyFileTo (juce::File (besideFile.getFullPathName() + ".img"));
    });
}

void Tone3000Client::downloadModel (const Model& model, const juce::String& kind,
                                    const juce::String& baseName,
                                    std::function<void (int)> progress,
                                    std::function<void (juce::File, juce::String)> done)
{
    pool.addJob ([this, model, kind, baseName, progress, done]
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

        const auto modelUrl = model.url;
        const auto modelName = model.name;
        const auto gear = kind; // "ir" -> IRs/, otherwise Captures/

        // Authenticated download with progress.
        juce::URL url (modelUrl);
        juce::WebInputStream stream (url, false);
        stream.withExtraHeaders ("Authorization: Bearer " + accessToken);
        stream.connect (nullptr);

        if (stream.getStatusCode() != 200)
        {
            fail ("Download failed (HTTP " + juce::String (stream.getStatusCode()) + ")");
            return;
        }

        auto target = localFileForModel (model, kind, baseName);

        target.deleteFile();
        juce::FileOutputStream out (target);
        if (! out.openedOk())
        {
            fail ("Could not create " + target.getFullPathName());
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
            fail ("Empty download");
            return;
        }

        juce::MessageManager::callAsync ([done, target] { done (target, {}); });
    });
}
