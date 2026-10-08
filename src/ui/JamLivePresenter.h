#pragma once

#include "JamOverlay.h"
#include "../jam/JamLiveInterface.h"

#include <cstdint>

// Message-thread-only mapper between the frozen jam::IJamLiveControl facade and
// the JUCE JamOverlay presentation.
//
// It owns exactly one UI reader cache, never starts/joins workers or touches the
// renderer, and never reads plain DrumEngine/clock/analyzer state. A failed or
// unchanged read keeps the whole previous state; the cold default is
// zero/not-prepared/unavailable with no invented BPM or confidence.
class JamLivePresenter
{
public:
    explicit JamLivePresenter (jam::IJamLiveControl& control) noexcept;

    // One coherent-read attempt. Returns true when a fresh state was published;
    // false leaves the previous whole state in place (no retry, no blocking).
    bool poll() noexcept;

    // Map and submit one UI intent. False means unsupported in this build or the
    // bounded queue rejected it; neither updates the cached state as if applied.
    bool submit (const JamUiIntent&) noexcept;

    const JamViewState& viewState() const noexcept { return view; }
    bool lastSubmitAccepted() const noexcept { return lastAccepted; }
    const juce::String& lastFeedback() const noexcept { return feedback; }
    bool hasLiveState() const noexcept { return haveLive; }

    // Local accepted-intent latch: selection/queued status only, never proof of
    // sound. Lets a fast second click cancel a not-yet-echoed Start.
    bool startIntentLatched() const noexcept { return intentRunning; }
    bool startQueued() const noexcept { return startPending; }
    bool stopQueued() const noexcept { return stopPending; }

    // Pure intent -> frozen command mapping (independently testable).
    static bool mapIntent (const JamUiIntent&, jam::JamLiveCommand& out) noexcept;

    // Diagnostics for independent mapping tests.
    std::uint64_t pollCount() const noexcept { return polls; }
    std::uint64_t readFailureCount() const noexcept { return readFailures; }
    std::uint64_t submitCount() const noexcept { return submits; }
    std::uint64_t rejectedCount() const noexcept { return rejects; }
    int lastCommandType() const noexcept { return lastCommand; }
    double lastCommandValue() const noexcept { return lastCommandValue_; }

    // A freshly recreated editor starts cold; the worker publishes into the
    // processor's own latest-value cache independently, so this only resets the
    // per-editor presentation cache.
    void resetCache() noexcept;

private:
    void present() noexcept;
    juce::String nextIntentText() const;
    juce::String statusText() const;
    juce::String composeDiagnostics() const;

    jam::IJamLiveControl& control;
    jam::JamLiveState live {};
    JamViewState view {};
    bool haveLive = false;
    bool lastAccepted = false;
    // Accepted-intent latch, separate from the audio-owner echo. Reset only on a
    // device generation change or prepared release, never on a stale/raced read.
    bool intentRunning = false;
    bool startPending = false, stopPending = false;
    bool haveGeneration = false;
    std::uint64_t lastSeenGeneration = 0;
    juce::String feedback;
    std::uint64_t polls = 0, readFailures = 0, submits = 0, rejects = 0;
    int lastCommand = -1;
    double lastCommandValue_ = 0.0;
};
