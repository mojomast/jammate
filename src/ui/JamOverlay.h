#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../LookAndFeel.h"
#include <array>
#include <cstdint>

// Message-thread-only UI seam. No tracker, transport or audio device ownership.
//
// UI-LIVE-001: this view is fed either by the frozen live facade through
// JamLivePresenter (production) or, only when a caller explicitly opts in with
// setSimulatedPreview(true), by the isolated development preview tool. The
// default is zeroed / not prepared / unavailable: no candidate BPM, clock BPM,
// confidence or level is invented before the worker publishes a coherent state.
struct JamViewState
{
    enum class Lock { acquiring, locked, holdover, lost };

    // ---- live telemetry (zero/false until a coherent live read) ----
    bool hasState = false;           // at least one coherent facade read received
    bool prepared = false;
    bool requestedRunning = false;   // accepted start intent, not proof of sound
    bool joinPending = false;
    bool drumsPlaying = false;       // latest audio-owner echo, not scheduled intent
    bool tempoFrozen = false;
    int backend = 0;                 // jam::JamLiveBackend
    int failure = 0;                 // jam::JamLiveFailure
    int modeId = 1;                  // jam::TempoMode
    juce::String backendName, failureName;
    std::uint64_t sessionGeneration = 0;
    std::uint64_t audioSampleTime = 0;
    std::uint64_t receiptLagSamples = 0;
    bool receiptMeasured = false;
    std::uint64_t analysisDrops = 0, observationDrops = 0,
                  userCommandDrops = 0, drumCommandDrops = 0, discontinuities = 0;

    // ---- presentation (shared with the isolated preview wrapper) ----
    bool running = false;
    int style = 1, mode = 2;         // combo ids; production has 1 style / 3 modes
    std::array<double, 4> amounts { 0.0, 0.0, 0.0, 0.0 };
    double candidateBpm = 0.0, clockBpm = 0.0;
    float confidence = 0.0f, inputLevel = 0.0f;
    Lock lock = Lock::acquiring;
    int bar = 0, beat = 0;           // bar is 0 = unknown (no bar counter published)
    int beatsPerBar = 4, beatUnit = 4;
    float beatPhase01 = 0.0f, barPhase01 = 0.0f;
    juce::String nextIntent = "Not prepared";
    juce::String diagnostics;
    juce::String availability;       // clear "not implemented" text, never silence
    juce::String commandFeedback;    // visible dropped/rejected feedback
    bool simulated = false;          // demo telemetry only (preview opt-in)
};

// Typed intent emitted by the view. The production presenter maps only the
// kinds backed by the frozen command enum; the rest are rejected visibly.
struct JamUiIntent
{
    enum class Kind { startStop, style, mode, intensity, complexity, fillAmount,
                      followTightness, tap, resync, half, doubleTempo, fill, breakBar,
                      stopNextBar, freeze, resume, resyncBar, reset };
    Kind kind;
    double value = 0.0;
};

class JamOverlay final : public juce::Component
{
public:
    JamOverlay();
    ~JamOverlay() override;

    // Silent control refresh: never emits intent.
    void setViewState (const JamViewState&);
    const JamViewState& getViewState() const { return state; }

    std::function<void (const JamUiIntent&)> onIntent;
    std::function<void()> onClose;   // fired when the user dismisses the screen

    void open();
    void close();

    // Explicit preview opt-in. Default false = production presentation (only the
    // implemented Rock style and Fixed/Follow/Loose modes; unsupported controls
    // disabled with an availability note). true restores the MOD-003 demo
    // control set and the SIMULATED caption. Static preview compiles either way.
    void setSimulatedPreview (bool shouldSimulate);
    bool isSimulatedPreview() const { return simulatedPreview; }

    void paint (juce::Graphics&) override;
    void resized() override;
    // Escape dismisses the screen; anything else falls through to focus traversal.
    bool keyPressed (const juce::KeyPress&) override;

    // Public test/accessibility inspection (no pixel brittleness).
    juce::ComboBox& getStyleBox() { return style; }
    juce::ComboBox& getModeBox() { return mode; }
    juce::TextButton& getStartButton() { return start; }
    juce::TextButton& getActionButton (int index) { return actions[(size_t) index]; }
    juce::Slider& getAmountSlider (int index) { return amounts[(size_t) index]; }
    int getNumActions() const { return (int) actions.size(); }

private:
    void emit (JamUiIntent::Kind, double = 0.0);
    void configureControls();
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
    bool simulatedPreview = false;
    juce::Viewport viewport;
    Content content { *this };
    juce::ComboBox style, mode;
    std::array<juce::Slider, 4> amounts;
    juce::TextButton start { "START" }, diagnostics { "SHOW DIAGNOSTICS" };
    std::array<juce::TextButton, 11> actions;
    bool showDiagnostics = false;
    juce::Rectangle<int> headerArea, statusArea, telemetryArea, controlsArea,
                         actionArea, diagnosticsArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JamOverlay)
};
