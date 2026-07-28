#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "LookAndFeel.h"
#include "Tone3000Client.h"

//==============================================================================
// TONE3000's own picker, embedded.
//
// The free API tier only lets us browse through TONE3000's OAuth prompt flows
// (select_tone / load_tone), which are web pages. Running them in the system
// browser meant leaving the app - and, in a plugin window, losing the host's
// focus - so this panel hosts the very same pages in a WebView2 view.
//
// How the round trip works here:
//   1. Tone3000Client::beginEmbeddedAuth() mints the PKCE challenge and the
//      authorize URL.
//   2. The view loads it. The user signs in and picks a tone on tone3000.com.
//   3. TONE3000 redirects to http://localhost:53682/callback?...  We catch that
//      in pageAboutToLoad() and CANCEL the navigation, so nothing is ever
//      requested and no local socket has to exist. The registered redirect URI
//      is unchanged: it is the one tied to the user's publishable key.
//   4. The captured URL goes back to the client, which exchanges the code.
//
// Cookies live in a persistent user-data folder, so the sign-in survives
// between sessions and picking a second tone does not ask for the password
// again.
//
// If the WebView2 runtime is missing (it ships with Windows 11 and with recent
// Edge, but a bare Windows 10 may not have it), isSupported() returns false and
// the caller falls back to the system-browser flow that always existed.
class ToneWebView : public juce::Component
{
public:
    explicit ToneWebView (Tone3000Client&);
    ~ToneWebView() override;

    /// False when there is no WebView2 (or the build has no embedded browser at
    /// all). The caller must then use the system-browser flow.
    static bool isSupported();

    /// Opens a prompt flow. `baseParams` must NOT carry an architecture filter:
    /// the panel owns that choice (see the segmented control in its header) and
    /// appends it, because TONE3000 only filters one architecture at a time.
    /// `title` is the panel heading. done() is called with the chosen tone, or
    /// with an error - "No tone was chosen" when the user just closed the panel.
    void openToneFlow (const juce::String& baseParams, const juce::String& title,
                       Tone3000Client::Architecture,
                       std::function<void (Tone3000Client::Tone, juce::String error)> done);

    /// Opens a plain sign-in (no picker).
    void openConnect (std::function<void (bool ok, juce::String error)> done);

    /// Closes the panel and cancels whatever flow is running.
    void cancel();

    /// TONE3000 wordmark drawn in the header (attribution).
    juce::Image brandLogo;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Browser;

    /// Starts an authorisation and shows the panel. Returns false (with the
    /// error already reported through the pending callback) if it cannot start.
    bool begin (const juce::String& promptParams, const juce::String& title);
    /// Called by the browser when the OAuth redirect is about to be loaded.
    void handleRedirect (const juce::String& callbackUrl);
    /// Called by the browser as navigation moves, to show where we are.
    void setCurrentUrl (const juce::String&);
    void finishWithError (const juce::String& error);
    void updateStatus (const juce::String&);

    /// Hands the running flow over to the system browser (which needs its own
    /// authorisation, because that one is caught by the localhost listener).
    void handOverToSystemBrowser();
    /// Reloads the picker under another architecture filter. Mints a fresh PKCE
    /// challenge (the authorize URL changes) without dropping the reservation.
    void setArchitecture (Tone3000Client::Architecture);
    /// baseParams + the architecture currently selected.
    juce::String currentParams() const;

    Tone3000Client& client;
    std::unique_ptr<Browser> browser;
    Tone3000Client::AuthSession session;
    juce::String baseParams;     // flow params WITHOUT the architecture filter
    Tone3000Client::Architecture architecture = Tone3000Client::Architecture::a1AndCustom;
    bool archPickerVisible = false;   // only the tone flows filter by architecture
    bool running = false;

    juce::String title, status, currentHost;

    std::function<void (Tone3000Client::Tone, juce::String)> toneCallback;
    std::function<void (bool, juce::String)> connectCallback;

    juce::TextButton closeButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")) };
    juce::TextButton backButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90")) };
    // U+21BB, not U+27F3: the UI font has no glyph for the latter and it drew
    // as a stray "e".
    juce::TextButton reloadButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xbb")) };
    juce::TextButton externalButton { juce::String (juce::CharPointer_UTF8 (
        "Open in browser \xe2\x86\x97")) };

    // Architecture picker. It has to be here, next to the catalogue, because
    // that is where the user hits TONE3000's "Not supported" notice.
    juce::TextButton archA1 { "A1 + CUSTOM" }, archA2 { "A2" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneWebView)
};
