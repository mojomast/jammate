#include "ToneWebView.h"

#include <utility>

namespace
{
constexpr int kHeaderH = 56;

/// Host shown in the header, so the user always knows which site is on screen
/// (federated sign-ins do hop to another domain and back).
juce::String hostOf (const juce::String& url)
{
    return url.fromFirstOccurrenceOf ("//", false, false)
              .upToFirstOccurrenceOf ("/", false, false)
              .upToFirstOccurrenceOf ("?", false, false);
}
} // namespace

#if JUCE_WEB_BROWSER

//==============================================================================
// The view itself. Everything interesting happens in pageAboutToLoad().
class ToneWebView::Browser : public juce::WebBrowserComponent
{
public:
    Browser (ToneWebView& o, const Options& options)
        : juce::WebBrowserComponent (options), owner (o) {}

    bool pageAboutToLoad (const juce::String& url) override
    {
        if (Tone3000Client::isRedirectUrl (url))
        {
            // Cancel the navigation: the redirect only exists to carry the
            // query back to us, and there is nothing listening on that port.
            // The handling is deferred so we never re-enter WebView2 (hiding
            // the panel, loading another page) from inside its own event.
            defer ([url] (ToneWebView& o) { o.handleRedirect (url); });
            return false;
        }

        owner.setCurrentUrl (url);
        return true;
    }

    // Sign-in pages like to open pop-ups (target=_blank, federated providers).
    // A pop-up would be a window we do not own and cannot intercept, so it is
    // loaded in this same view instead.
    void newWindowAttemptingToLoad (const juce::String& url) override
    {
        if (Tone3000Client::isRedirectUrl (url))
            defer ([url] (ToneWebView& o) { o.handleRedirect (url); });
        else
            defer ([url] (ToneWebView& o) { o.browser->goToURL (url); });
    }

    void pageFinishedLoading (const juce::String&) override { owner.updateStatus ({}); }

    void windowCloseRequest() override
    {
        defer ([] (ToneWebView& o) { o.cancel(); });
    }

private:
    /// Runs `fn` on the message thread after this event has returned, and only
    /// if the panel is still alive.
    template <typename Fn>
    void defer (Fn&& fn)
    {
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<ToneWebView> (&owner), fn = std::forward<Fn> (fn)]
            {
                if (safe != nullptr)
                    fn (*safe.getComponent());
            });
    }

    ToneWebView& owner;
};

namespace
{
juce::WebBrowserComponent::Options browserOptions()
{
    // Persistent profile: signing in once is enough, and picking the next tone
    // no longer starts from a blank session. It lives next to the rest of our
    // data instead of in WebView2's default location, which a plugin process
    // may not be allowed to write to.
    auto profile = Tone3000Client::dataDir().getChildFile ("webview");
    profile.createDirectory();

    return juce::WebBrowserComponent::Options()
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withWinWebView2Options (
            juce::WebBrowserComponent::Options::WinWebView2()
                .withUserDataFolder (profile)
                .withStatusBarDisabled()
                .withBackgroundColour (juce::Colour (0xff0b0c0e)));
}
} // namespace

bool ToneWebView::isSupported()
{
    // Cached: the check actually creates a WebView2 handle, which is not free.
    static const bool supported =
        juce::WebBrowserComponent::areOptionsSupported (browserOptions());
    return supported;
}

#else // no embedded browser in this build

class ToneWebView::Browser : public juce::Component {};

bool ToneWebView::isSupported() { return false; }

#endif

//==============================================================================
ToneWebView::ToneWebView (Tone3000Client& c) : client (c)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    closeButton.getProperties().set ("ghost", true);
    closeButton.setTooltip ("Close without choosing (Esc)");
    closeButton.onClick = [this] { cancel(); };
    addAndMakeVisible (closeButton);

    backButton.getProperties().set ("ghost", true);
    backButton.setTooltip ("Back");
    addAndMakeVisible (backButton);

    reloadButton.getProperties().set ("ghost", true);
    reloadButton.setTooltip ("Reload");
    addAndMakeVisible (reloadButton);

    externalButton.getProperties().set ("ghost", true);
    externalButton.setTooltip ("Start over in your normal browser instead");
    addAndMakeVisible (externalButton);

#if JUCE_WEB_BROWSER
    backButton.onClick   = [this] { if (browser != nullptr) browser->goBack(); };
    reloadButton.onClick = [this] { if (browser != nullptr) browser->refresh(); };

    externalButton.onClick = [this] { handOverToSystemBrowser(); };
#else
    backButton.setEnabled (false);
    reloadButton.setEnabled (false);
    externalButton.setEnabled (false);
#endif
}

ToneWebView::~ToneWebView()
{
    if (running)
        client.cancelEmbeddedAuth();
}

//==============================================================================
bool ToneWebView::begin (const juce::String& promptParams_, const juce::String& newTitle)
{
#if JUCE_WEB_BROWSER
    juce::String error;
    if (! client.beginEmbeddedAuth (promptParams_, session, error))
    {
        finishWithError (error);
        return false;
    }

    running = true;
    title = newTitle;
    promptParams = promptParams_;
    currentHost = "tone3000.com";
    status = "Loading TONE3000...";

    if (browser == nullptr)
    {
        browser = std::make_unique<Browser> (*this, browserOptions());
        addAndMakeVisible (*browser);
        resized();
    }

    browser->goToURL (session.authorizeUrl);
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    repaint();
    return true;
#else
    juce::ignoreUnused (promptParams_, newTitle);
    finishWithError ("This build has no embedded browser");
    return false;
#endif
}

void ToneWebView::openToneFlow (const juce::String& promptParams_, const juce::String& newTitle,
                                std::function<void (Tone3000Client::Tone, juce::String)> done)
{
    toneCallback = std::move (done);
    connectCallback = nullptr;
    begin (promptParams_, newTitle);
}

// The escape hatch: if a page refuses to behave in an embedded view (some
// identity providers do), the user can finish in their normal browser. That
// means a FRESH authorisation - this one's PKCE challenge is tied to a redirect
// only this panel can catch, while the system-browser path opens the
// localhost:53682 listener that receives it there.
void ToneWebView::handOverToSystemBrowser()
{
    if (! running)
        return;

    const auto params = promptParams;
    running = false;
    client.cancelEmbeddedAuth();
    setVisible (false);

#if JUCE_WEB_BROWSER
    if (browser != nullptr)
        browser->goToURL ("about:blank");
#endif

    if (auto cb = std::exchange (toneCallback, nullptr))
        client.runPromptFlowInSystemBrowser (params, std::move (cb));
    else if (auto cc = std::exchange (connectCallback, nullptr))
        client.connect (std::move (cc));
}

void ToneWebView::openConnect (std::function<void (bool, juce::String)> done)
{
    connectCallback = std::move (done);
    toneCallback = nullptr;
    begin (Tone3000Client::connectParams(), "Sign in to TONE3000");
}

void ToneWebView::cancel()
{
    if (! running)
    {
        setVisible (false);
        return;
    }

    running = false;
    client.cancelEmbeddedAuth();
    setVisible (false);

#if JUCE_WEB_BROWSER
    // Leave the view on a blank page: the picker should not still be sitting
    // there, half scrolled, the next time the panel opens.
    if (browser != nullptr)
        browser->goToURL ("about:blank");
#endif

    // Same wording the system-browser path uses when the picker is closed, so
    // the store's "not an error" check keeps working.
    if (auto cb = std::exchange (toneCallback, nullptr))
        cb ({}, "No tone was chosen");
    else if (auto cc = std::exchange (connectCallback, nullptr))
        cc (false, "Sign-in cancelled");
}

void ToneWebView::finishWithError (const juce::String& error)
{
    running = false;
    setVisible (false);

    if (auto cb = std::exchange (toneCallback, nullptr))
        cb ({}, error);
    else if (auto cc = std::exchange (connectCallback, nullptr))
        cc (false, error);
}

void ToneWebView::handleRedirect (const juce::String& callbackUrl)
{
    if (! running)
        return;

    running = false;
    updateStatus ("Finishing...");

    // The panel closes right away: what is left is a token exchange with no UI.
    setVisible (false);

    if (auto cb = std::exchange (toneCallback, nullptr))
    {
        client.finishEmbeddedTone (session, callbackUrl, std::move (cb));
    }
    else if (auto cc = std::exchange (connectCallback, nullptr))
    {
        client.finishEmbeddedConnect (session, callbackUrl, std::move (cc));
    }
    else
    {
        // Nobody is waiting - do not leave the client reserved.
        client.cancelEmbeddedAuth();
    }
}

void ToneWebView::setCurrentUrl (const juce::String& url)
{
    const auto host = hostOf (url);
    if (host == currentHost)
        return;
    currentHost = host;
    repaint();
}

void ToneWebView::updateStatus (const juce::String& s)
{
    if (status == s)
        return;
    status = s;
    repaint();
}

//==============================================================================
bool ToneWebView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        cancel();
        return true;
    }
    return false;
}

void ToneWebView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0c0e));

    // ---- header
    g.setFont (ui::uiFont (17.0f, true));
    g.setColour (ui::textBright);
    g.drawText (title, 22, 12, 300, 22, juce::Justification::centredLeft);

    // Where we are + what is going on, on one line under the title.
    g.setFont (ui::monoFont (10.5f));
    g.setColour (ui::textDim);
    auto line = currentHost;
    if (status.isNotEmpty())
        line += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + status;
    g.drawText (line, 22, 32, getWidth() - 44, 14, juce::Justification::centredLeft);

    // TONE3000 wordmark (attribution): the content below is theirs.
    if (brandLogo.isValid())
    {
        const float logoH = 15.0f;
        const float logoW = logoH * brandLogo.getWidth() / (float) brandLogo.getHeight();
        g.drawImage (brandLogo,
                     juce::Rectangle<float> (externalButton.getX() - 16.0f - logoW,
                                             (kHeaderH - logoH) * 0.5f, logoW, logoH),
                     juce::RectanglePlacement::centred);
    }

    g.setColour (juce::Colour (0xff24262a));
    g.fillRect (0, kHeaderH, getWidth(), 1);

#if ! JUCE_WEB_BROWSER
    g.setFont (ui::uiFont (14.0f));
    g.setColour (ui::textDim);
    g.drawText ("The embedded browser is not available in this build.",
                getLocalBounds().withTrimmedTop (kHeaderH), juce::Justification::centred);
#endif
}

void ToneWebView::resized()
{
    const int right = getWidth() - 16;
    closeButton.setBounds (right - 30, 13, 30, 30);
    externalButton.setBounds (closeButton.getX() - 8 - 150, 13, 150, 30);

    // Navigation sits after the title, out of the way of the wordmark.
    backButton.setBounds (getWidth() / 2 - 34, 13, 30, 30);
    reloadButton.setBounds (getWidth() / 2, 13, 30, 30);

#if JUCE_WEB_BROWSER
    if (browser != nullptr)
        browser->setBounds (getLocalBounds().withTrimmedTop (kHeaderH + 1));
#endif
}
