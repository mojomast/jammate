// UI-LIVE-001 development harness: runs the Jam mapping/control tests against a
// deterministic mock facade and can render a labelled test-fixture snapshot.
// No physical audio input, real device or real pipeline is used here.
#include "../../tests/TestHarness.h"

#include "../../src/ui/JamOverlay.h"
#include "../../src/ui/JamLivePresenter.h"

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <cstdio>
#include <cstring>

// Preview-only font bridge (production provides embedded Archivo/JetBrains Mono
// in PluginEditor.cpp). The isolated harness uses installed system fonts.
namespace ui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    return juce::Typeface::createSystemTypefaceFor (juce::Font (
        juce::FontOptions ("DejaVu Sans", 14.0f, bold ? juce::Font::bold : juce::Font::plain)));
}
juce::Typeface::Ptr monoTypeface (bool bold)
{
    return juce::Typeface::createSystemTypefaceFor (juce::Font (
        juce::FontOptions ("DejaVu Sans Mono", 14.0f, bold ? juce::Font::bold : juce::Font::plain)));
}
}

namespace th
{
std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

static int caseFailures = 0;
static int totalFailures = 0;

void fail (const char* file, int line, const char* expr, const std::string& note)
{
    ++caseFailures;
    ++totalFailures;
    std::printf ("      FAIL %s:%d  %s%s%s\n", file, line, expr,
                 note.empty() ? "" : "  -- ", note.c_str());
}

void info (const std::string& msg) { std::printf ("      info: %s\n", msg.c_str()); }
} // namespace th

namespace
{
// Deterministic mock facade: drives the real presenter mapping with fixture
// telemetry, never a physical audio input.
class FixtureControl final : public jam::IJamLiveControl
{
public:
    bool submitJamCommand (const jam::JamLiveCommand& c) noexcept override
    {
        last = c;
        return true;
    }
    bool readJamLiveState (jam::JamLiveState& out) const noexcept override
    {
        if (! publish) return false;
        out = state;
        return true;
    }
    bool publish = true;
    jam::JamLiveState state {};
    jam::JamLiveCommand last {};
};

jam::JamLiveState fixtureState()
{
    jam::JamLiveState s;
    s.prepared = true;
    s.requestedRunning = true;
    s.drumsPlaying = true;
    s.backend = jam::JamLiveBackend::experimentalBTrack;
    s.mode = jam::TempoMode::Follow;
    s.candidateBpm = 118.4f;
    s.clock.bpm = 120.0;
    s.clock.confidence01 = 0.82f;
    s.clock.lockState = jam::ClockLockState::Locked;
    s.clock.beatInBar = 2;
    s.clock.beatsPerBar = 4;
    s.inputPeak = 0.42f;
    s.sessionGeneration = 3;
    s.audioSampleTime = 100000;
    s.lastReceiptSampleTime = 98900;
    s.receiptMeasured = true;
    return s;
}

int writeOverlay (const juce::File& file, const JamViewState& s, bool simulated = false)
{
    JamOverlay overlay;
    overlay.setSimulatedPreview (simulated);   // only the demo fixture opts in
    overlay.setViewState (s);
    overlay.setSize (1060, 920);   // tall enough to show every control
    auto image = overlay.createComponentSnapshot (overlay.getLocalBounds());
    file.deleteFile();
    juce::FileOutputStream stream (file);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::printf ("FAIL snapshot write %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("PASS snapshot %s %dx%d (label: test fixture)\n",
                 file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}

int renderFixtureSnapshots (const juce::File& dir)
{
    if (! dir.exists())
        dir.createDirectory();

    FixtureControl mock;
    mock.state = fixtureState();
    JamLivePresenter armed (mock);
    armed.poll();

    FixtureControl coldMock;
    coldMock.publish = false;
    JamLivePresenter cold (coldMock);
    cold.poll();

    // Explicit demo opt-in: the same component, loaded with a simulated state.
    JamViewState demo;
    demo.hasState = true;
    demo.simulated = true;
    demo.running = true;
    demo.style = 1;
    demo.mode = 3;
    demo.amounts = { 65.0, 40.0, 30.0, 70.0 };
    demo.candidateBpm = 118.4;
    demo.clockBpm = 120.0;
    demo.confidence = 0.82f;
    demo.inputLevel = 0.35f;
    demo.lock = JamViewState::Lock::locked;
    demo.beat = 2;
    demo.beatsPerBar = 4;
    demo.backendName = "demo";
    demo.nextIntent = "Playing (demo)";

    int rc = 0;
    rc |= writeOverlay (dir.getChildFile ("jam-live-testfixture.png"), armed.viewState());
    rc |= writeOverlay (dir.getChildFile ("jam-live-cold-testfixture.png"), cold.viewState());
    rc |= writeOverlay (dir.getChildFile ("jam-live-demo-testfixture.png"), demo, true);
    return rc;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (int i = 1; i < argc; ++i)
        if (std::strcmp (argv[i], "--snapshot") == 0 && i + 1 < argc)
            return renderFixtureSnapshots (juce::File (juce::String (argv[i + 1])));

    const char* filter = (argc > 1 && argv[1][0] != '-') ? argv[1] : nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    int ran = 0, failedCases = 0;

    for (const auto& c : th::registry())
    {
        if (filter != nullptr && std::strstr (c.name, filter) == nullptr)
            continue;
        ++ran;
        th::caseFailures = 0;
        std::printf ("  [ run  ] %s\n", c.name);
        try                      { c.fn(); }
        catch (const th::Abort&) { /* REQUIRE already logged it */ }
        catch (const std::exception& e) { th::fail (__FILE__, __LINE__, "unexpected std::exception", e.what()); }
        catch (...)              { th::fail (__FILE__, __LINE__, "unexpected exception", {}); }
        if (th::caseFailures > 0)
            ++failedCases;
        std::printf ("  [ %s ] %s\n", th::caseFailures == 0 ? " ok  " : "FAILED", c.name);
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        std::chrono::steady_clock::now() - t0).count();
    std::printf ("\n%d case(s) run in %lld ms, %d failed case(s), %d failed check(s)\n",
                 ran, (long long) ms, failedCases, th::totalFailures);
    if (ran == 0)
    {
        std::printf ("ERROR: the filter matched no test case\n");
        return 1;
    }
    return th::totalFailures == 0 ? 0 : 1;
}
