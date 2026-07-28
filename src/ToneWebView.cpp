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

    // TONE3000 filters by ONE architecture at a time, and omitting the filter
    // is not neutral - it means "A1 + Custom", hiding every A2-only tone behind
    // a "Not supported" notice. Guitar Companion plays both, so the choice is the
    // user's and it lives right here.
    for (auto* b : { &archA1, &archA2 })
    {
        b->getProperties().set ("chip", true);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setTooltip ("TONE3000 shows one model architecture at a time. "
                       "Guitar Companion loads both - switch here if a tone says "
                       "\"Not supported\".");
        addChildComponent (*b);
    }
    archA1.onClick = [this] { setArchitecture (Tone3000Client::Architecture::a1AndCustom); };
    archA2.onClick = [this] { setArchitecture (Tone3000Client::Architecture::a2); };

    // TONE3000 publishes in five formats and this app reads two, but their
    // filter takes one value - so the picker shows captures or IRs, never both,
    // and never the three we cannot open.
    for (auto* b : { &fmtNam, &fmtIr })
    {
        b->getProperties().set ("chip", true);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setTooltip ("NAM captures or impulse responses. TONE3000 filters one "
                       "format at a time; the formats Guitar Companion cannot open "
                       "(AIDA-X, Proteus, Amped Roots) stay out either way.");
        addChildComponent (*b);
    }
    fmtNam.onClick = [this] { setFormat (Tone3000Client::Format::nam); };
    fmtIr.onClick  = [this] { setFormat (Tone3000Client::Format::ir); };
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
juce::String ToneWebView::currentParams() const
{
    if (! archPickerVisible)
        return baseParams;   // a plain sign-in browses nothing

    // Architecture only means something for NAM captures: an IR has no model
    // architecture, and sending both would filter the IR list down to nothing.
    return baseParams
           + Tone3000Client::formatParam (format)
           + (format == Tone3000Client::Format::nam
                  ? Tone3000Client::architectureParam (architecture)
                  : juce::String());
}

// Repaints the chips to match the current scope and hides the architecture pair
// when it does not apply.
void ToneWebView::refreshScope()
{
    const bool nam = format == Tone3000Client::Format::nam;

    fmtNam.setVisible (archPickerVisible);
    fmtIr.setVisible (archPickerVisible);
    archA1.setVisible (archPickerVisible && nam);
    archA2.setVisible (archPickerVisible && nam);

    fmtNam.getProperties().set ("chipActive", nam);
    fmtIr.getProperties().set ("chipActive", ! nam);
    archA1.getProperties().set ("chipActive", architecture == Tone3000Client::Architecture::a1AndCustom);
    archA2.getProperties().set ("chipActive", architecture == Tone3000Client::Architecture::a2);

    for (auto* b : { &fmtNam, &fmtIr, &archA1, &archA2 })
        b->repaint();

    resized();
    repaint();
}

bool ToneWebView::begin (const juce::String& newBaseParams, const juce::String& newTitle)
{
#if JUCE_WEB_BROWSER
    baseParams = newBaseParams;

    juce::String error;
    if (! client.beginEmbeddedAuth (currentParams(), session, error))
    {
        finishWithError (error);
        return false;
    }

    running = true;
    title = newTitle;
    currentHost = "tone3000.com";
    status = "Loading TONE3000...";

    refreshScope();

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
    juce::ignoreUnused (newBaseParams, newTitle);
    finishWithError ("This build has no embedded browser");
    return false;
#endif
}

void ToneWebView::openToneFlow (const juce::String& newBaseParams, const juce::String& newTitle,
                                Tone3000Client::Format fmt, Tone3000Client::Architecture arch,
                                std::function<void (Tone3000Client::Tone, juce::String)> done)
{
    toneCallback = std::move (done);
    connectCallback = nullptr;
    format = fmt;
    architecture = arch;
    archPickerVisible = true;
    begin (newBaseParams, newTitle);
}

// Switching a filter means a different authorize URL, so a fresh PKCE challenge
// - but the SAME reservation: the flow the caller is waiting on has not ended,
// we are only showing it a different slice of the catalogue.
void ToneWebView::reloadScope()
{
    refreshScope();

    juce::String error;
    if (! client.renewEmbeddedAuth (currentParams(), session, error))
    {
        finishWithError (error);
        return;
    }

    updateStatus ("Loading TONE3000...");
#if JUCE_WEB_BROWSER
    if (browser != nullptr)
        browser->goToURL (session.authorizeUrl);
#endif
}

void ToneWebView::setArchitecture (Tone3000Client::Architecture arch)
{
    if (! running || arch == architecture)
        return;

    architecture = arch;
    reloadScope();
}

void ToneWebView::setFormat (Tone3000Client::Format fmt)
{
    if (! running || fmt == format)
        return;

    format = fmt;
    reloadScope();
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

    const auto params = currentParams();
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
    archPickerVisible = false;   // a plain sign-in browses nothing
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
    g.drawText (line, 22, 32, 250, 14, juce::Justification::centredLeft);

    // Naming the clusters out loud: TONE3000 marks anything outside the chosen
    // scope as "Not supported", which reads like the tone is broken rather than
    // filtered out. The widths stop at the next cluster so the labels never
    // run into each other.
    if (archPickerVisible)
    {
        g.setFont (ui::monoFont (9.5f));
        g.setColour (ui::textFaint);
        g.drawText ("FORMAT", fmtNam.getX(), 2, archA1.getX() - fmtNam.getX() - 10, 12,
                    juce::Justification::centredLeft);
        if (archA1.isVisible())
            g.drawText ("ARCHITECTURE", archA1.getX(), 2, 180, 12,
                        juce::Justification::centredLeft);
    }

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

    // Navigation right after the title, then the two scope clusters between it
    // and the wordmark: FORMAT first (it decides whether ARCHITECTURE applies
    // at all), ARCHITECTURE after it.
    backButton.setBounds (286, 13, 30, 30);
    reloadButton.setBounds (320, 13, 30, 30);

    fmtNam.setBounds (370, 14, 54, 28);
    fmtIr.setBounds (fmtNam.getRight() + 6, 14, 44, 28);

    archA1.setBounds (500, 14, 108, 28);
    archA2.setBounds (archA1.getRight() + 6, 14, 46, 28);

#if JUCE_WEB_BROWSER
    if (browser != nullptr)
        browser->setBounds (getLocalBounds().withTrimmedTop (kHeaderH + 1));
#endif
}
