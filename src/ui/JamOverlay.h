#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../LookAndFeel.h"
#include <array>

// Message-thread-only UI seam. No tracker, transport or audio device ownership.
struct JamViewState
{
    enum class Lock { acquiring, locked, holdover, lost };
    bool running = false;
    int style = 1, mode = 3;
    std::array<double, 4> amounts { 65.0, 40.0, 30.0, 70.0 };
    double candidateBpm = 118.4, clockBpm = 120.0;
    float confidence = 0.82f, inputLevel = 0.0f;
    Lock lock = Lock::acquiring;
    int bar = 1, beat = 1;
    juce::String nextIntent = "Ready to join";
    juce::String diagnostics = "No audio device attached. All telemetry is simulated.";
};

struct JamUiIntent
{
    enum class Kind { startStop, style, mode, intensity, complexity, fillAmount,
                      followTightness, tap, resync, half, doubleTempo, fill, breakBar, stopNextBar };
    Kind kind;
    double value = 0.0;
};

class JamOverlay final : public juce::Component
{
public:
    JamOverlay();
    ~JamOverlay() override;
    void setViewState (const JamViewState&); // Silent control refresh: never emits intent.
    const JamViewState& getViewState() const { return state; }
    std::function<void (const JamUiIntent&)> onIntent;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void emit (JamUiIntent::Kind, double = 0.0);
    void paintContent (juce::Graphics&);
    class Content final : public juce::Component
    {
    public:
        explicit Content (JamOverlay& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        JamOverlay& owner;
    };
    RigLookAndFeel look;
    JamViewState state;
    juce::Viewport viewport;
    Content content { *this };
    juce::ComboBox style, mode;
    std::array<juce::Slider, 4> amounts;
    juce::TextButton start { "START DEMO" }, diagnostics { "SHOW DIAGNOSTICS" };
    std::array<juce::TextButton, 7> actions;
    bool showDiagnostics = false;
    juce::Rectangle<int> statusArea, controlsArea, actionArea, diagnosticsArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JamOverlay)
};
