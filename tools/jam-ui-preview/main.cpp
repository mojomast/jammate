#include "../../src/ui/JamOverlay.h"
#include <iostream>
#include <stdexcept>

// Preview-only font bridge: production provides embedded Archivo/JetBrains Mono
// in PluginEditor.cpp. This isolated executable uses installed system fonts.
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

// The timer and musical scheduling are deliberately confined to this dev wrapper.
class Simulator final : private juce::Timer
{
public:
    explicit Simulator (JamOverlay& v) : view (v)
    {
        // UI-LIVE-001: production defaults are now zero/unavailable, so the
        // preview must EXPLICITLY opt in to the demo control set and seed the
        // demo telemetry it asserts against (no fake production defaults).
        view.setSimulatedPreview (true);
        state.candidateBpm = 118.4;
        state.clockBpm = 120.0;
        state.confidence = 0.82f;
        state.style = 1;
        state.mode = 3;
        state.amounts = { 65.0, 40.0, 30.0, 70.0 };
        state.lock = JamViewState::Lock::acquiring;
        state.nextIntent = "Ready to join";
        state.diagnostics = "No audio device attached. All telemetry is simulated.";
        view.onIntent = [this] (const JamUiIntent& i) { handle (i); };
        publish(); startTimerHz (30);
    }
    ~Simulator() override { stopTimer(); view.onIntent = {}; }
    JamViewState state;
    int ticks = 0, intents = 0;
    double phase = 0.0;
    juce::String pending;
    double lastTap = 0.0;
    void handle (const JamUiIntent& i)
    {
        ++intents;
        using K = JamUiIntent::Kind;
        const auto n = static_cast<int> (i.kind);
        if (n >= static_cast<int> (K::intensity) && n <= static_cast<int> (K::followTightness))
            state.amounts[static_cast<size_t> (n - static_cast<int> (K::intensity))] = i.value;
        switch (i.kind)
        {
            case K::startStop:
                state.running = ! state.running; pending.clear();
                state.lock = state.running ? JamViewState::Lock::acquiring : JamViewState::Lock::lost;
                state.confidence = state.running ? 0.86f : 0.0f;
                if (! state.running) state.inputLevel = 0.0f;
                state.nextIntent = state.running ? "Join next bar" : "Ready to join";
                if (state.running) { phase = 0.0; state.bar = state.beat = 1; }
                break;
            case K::style: state.style = static_cast<int> (i.value); break;
            case K::mode: state.mode = static_cast<int> (i.value); break;
            case K::half: state.clockBpm = juce::jmax (30.0, state.clockBpm * 0.5); break;
            case K::doubleTempo: state.clockBpm = juce::jmin (300.0, state.clockBpm * 2.0); break;
            case K::tap:
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (lastTap > 0.0 && now - lastTap >= 200.0 && now - lastTap <= 2000.0)
                    state.clockBpm = 60000.0 / (now - lastTap);
                lastTap = now; state.nextIntent = "Tap received (demo)"; break;
            }
            case K::resync: phase = 0.0; state.beat = 1; state.nextIntent = "Resynced (demo)"; break;
            case K::fill: pending = "Fill next bar"; break;
            case K::breakBar: pending = "Break next bar"; break;
            case K::stopNextBar: pending = "Stop next bar"; break;
            default: break;
        }
        if (pending.isNotEmpty()) state.nextIntent = pending;
        publish();
    }
    void publish() { view.setViewState (state); }
private:
    void timerCallback() override
    {
        ++ticks;
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dt = lastTick == 0.0 ? 0.0 : (now - lastTick) / 1000.0;
        lastTick = now;
        state.inputLevel = state.running ? 0.25f + 0.5f * static_cast<float> (std::abs (std::sin (ticks * 0.07))) : 0.0f;
        state.candidateBpm = state.clockBpm + std::sin (ticks * 0.02) * 1.6;
        state.confidence = state.running ? 0.86f : 0.0f;
        state.lock = ! state.running ? JamViewState::Lock::lost
                   : state.bar == 1 ? JamViewState::Lock::acquiring : JamViewState::Lock::locked;
        if (state.running)
        {
            phase += dt * state.clockBpm / 60.0;
            while (phase >= 1.0)
            {
                phase -= 1.0;
                if (++state.beat > 4)
                {
                    state.beat = 1; ++state.bar;
                    if (pending == "Stop next bar") state.running = false;
                    state.nextIntent = pending.isNotEmpty() ? pending.replace ("next bar", "now (demo)") : "Keep the groove";
                    pending.clear();
                }
            }
        }
        publish();
    }
    JamOverlay& view;
    double lastTick = 0.0;
};

class PreviewApplication final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Jam UI / SIMULATION"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    void initialise (const juce::String& args) override
    {
        window = std::make_unique<Window>();
        simulator = std::make_unique<Simulator> (window->overlay);
        if (args.startsWith ("--verify "))
        {
            output = juce::File (args.fromFirstOccurrenceOf ("--verify ", false, false).trim());
            output.createDirectory();
            startTimer (100);
        }
    }
    void shutdown() override { stopTimer(); simulator.reset(); window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    struct Window final : juce::DocumentWindow
    {
        Window() : DocumentWindow ("Jam UI / SIMULATION", ui::bg, allButtons)
        {
            setUsingNativeTitleBar (true); setContentNonOwned (&overlay, false);
            setResizable (true, false); setResizeLimits (580, 480, 1600, 1200);
            centreWithSize (1060, 720); setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        JamOverlay overlay;
    };
    static void require (bool ok, const char* why)
    {
        if (! ok) throw std::runtime_error (why);
    }
    juce::Component* find (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = find (*c.getChildComponent (i), name)) return r;
        return nullptr;
    }
    void click (const juce::String& name)
    {
        auto* b = dynamic_cast<juce::TextButton*> (find (window->overlay, name));
        require (b != nullptr, "missing action"); b->onClick();
    }
    void checkLayout (juce::Component& c)
    {
        std::vector<juce::Rectangle<int>> controls;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
        {
            auto* child = c.getChildComponent (i);
            if (dynamic_cast<juce::Button*> (child) || dynamic_cast<juce::Slider*> (child)
                || dynamic_cast<juce::ComboBox*> (child))
            {
                require (c.getLocalBounds().contains (child->getBounds()), "control outside content");
                require (child->getWidth() >= 60 && child->getHeight() >= 24, "control too small");
                for (auto previous : controls) require (! previous.intersects (child->getBounds()), "controls overlap");
                controls.push_back (child->getBounds());
            }
            else checkLayout (*child);
        }
    }
    void capture (const char* name, int width, int height)
    {
        window->setSize (width, height);
        checkLayout (window->overlay);
        auto image = window->overlay.createComponentSnapshot (window->overlay.getLocalBounds());
        auto file = output.getChildFile (name);
        require (! file.existsAsFile() || file.deleteFile(), "old PNG removal failed");
        juce::FileOutputStream stream (file);
        require (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream), "PNG write failed");
        std::cout << "PASS layout + screenshot " << name << " " << image.getWidth() << "x" << image.getHeight() << std::endl;
    }
    void timerCallback() override
    {
        try
        {
            auto& s = *simulator;
            if (step == 0)
            {
                const int before = s.intents;
                window->overlay.setViewState (s.state);
                require (before == s.intents, "state refresh emitted intent");
                click ("StartStop"); require (s.state.running, "start failed");
                click ("HALF"); require (s.state.clockBpm == 60.0, "half failed");
                click ("DOUBLE"); require (s.state.clockBpm == 120.0, "double failed");
                auto* slider = dynamic_cast<juce::Slider*> (find (window->overlay, "INTENSITY"));
                require (slider != nullptr, "missing slider");
                slider->setValue (77, juce::sendNotificationSync);
                require (s.state.amounts[0] == 77, "slider intent failed");
                auto* box = dynamic_cast<juce::ComboBox*> (find (window->overlay, "Style"));
                require (box != nullptr, "missing style");
                box->setSelectedId (2, juce::sendNotificationSync);
                require (s.state.style == 2, "style intent failed");
                auto* mode = dynamic_cast<juce::ComboBox*> (find (window->overlay, "Mode"));
                require (mode != nullptr, "missing mode");
                mode->setSelectedId (4, juce::sendNotificationSync);
                require (s.state.mode == 4, "mode intent failed");
                mode->setSelectedId (3, juce::sendNotificationSync);
                for (int i = 1; i < 4; ++i)
                {
                    const char* names[] { "INTENSITY", "COMPLEXITY", "FILL AMOUNT", "FOLLOW TIGHTNESS" };
                    auto* control = dynamic_cast<juce::Slider*> (find (window->overlay, names[i]));
                    require (control != nullptr, "missing amount");
                    const double old = s.state.amounts[static_cast<size_t> (i)];
                    control->setValue (old + 1, juce::sendNotificationSync);
                    require (s.state.amounts[static_cast<size_t> (i)] == old + 1, "amount intent failed");
                    control->setValue (old, juce::sendNotificationSync);
                }
                click ("TAP"); click ("RESYNC"); click ("FILL");
                require (s.pending == "Fill next bar", "fill failed");
                capture ("jam-shell-wide.png", 1060, 720);
            }
            if (step == 5)
            {
                require (s.ticks > 5 && s.state.inputLevel > 0, "timer did not drive telemetry");
                click ("TAP"); require (s.state.clockBpm > 60 && s.state.clockBpm < 200, "tap interval BPM failed");
                for (int width : { 580, 800, 1000, 1016, 1280 })
                {
                    window->setSize (width, 600); checkLayout (window->overlay);
                    std::cout << "PASS resize sweep " << width << "x600" << std::endl;
                }
                click ("BREAK"); require (s.pending == "Break next bar", "break failed");
                capture ("jam-shell-minimum.png", 580, 480);
                capture ("jam-shell-compact.png", 640, 540);
                // Scroll to prove compact performance actions remain reachable.
                auto* vp = dynamic_cast<juce::Viewport*> (window->overlay.getChildComponent (0));
                require (vp != nullptr, "missing viewport");
                vp->setViewPosition (0, 400);
                capture ("jam-shell-compact-actions.png", 640, 540);
                vp->setViewPosition (0, 0);
                click ("Diagnostics");
                capture ("jam-shell-diagnostics.png", 1060, 860);
                click ("STOP NEXT BAR");
                require (s.pending == "Stop next bar", "stop queue failed");
                s.state.beat = 4; s.phase = 0.99;
            }
            if (step == 7)
            {
                require (! s.state.running && s.pending.isEmpty(), "bar-boundary stop failed");
                capture ("jam-shell-stopped.png", 1060, 860);
                // Exercise all lock-state presentations without an engine.
                for (auto lock : { JamViewState::Lock::acquiring, JamViewState::Lock::locked,
                                   JamViewState::Lock::holdover, JamViewState::Lock::lost })
                {
                    s.state.lock = lock; s.publish();
                    require (window->overlay.getViewState().lock == lock, "lock presentation failed");
                }
                click ("StartStop"); click ("StartStop"); require (! s.state.running, "immediate stop failed");
                std::cout << "PASS silent refresh, controls, timer telemetry, queued bar stop, lock states; intents=" << s.intents << std::endl;
                stopTimer(); quit();
            }
            ++step;
        }
        catch (const std::exception& e)
        {
            std::cerr << "FAIL " << e.what() << std::endl;
            setApplicationReturnValue (1); stopTimer(); quit();
        }
    }
    int step = 0;
    juce::File output;
    std::unique_ptr<Window> window;
    std::unique_ptr<Simulator> simulator;
};

START_JUCE_APPLICATION (PreviewApplication)
