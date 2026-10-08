// UI-LIVE-001: mapping, control and real-canvas layout tests for the live Jam
// screen.
//
// These tests are deliberately independent of the real processor: a mock
// jam::IJamLiveControl records submitted commands, can refuse the queue, and
// publishes whole state snapshots. No audio pipeline is claimed here; the real
// callback evidence is owned by the integration worker.
#include "TestHarness.h"

#include "../src/ui/JamLivePresenter.h"
#include "../src/ui/JamOverlay.h"

#include <vector>

namespace
{
using K = JamUiIntent::Kind;

class MockJamControl final : public jam::IJamLiveControl
{
public:
    bool submitJamCommand (const jam::JamLiveCommand& c) noexcept override
    {
        commands.push_back (c);
        return ! rejectAll;
    }

    bool readJamLiveState (jam::JamLiveState& out) const noexcept override
    {
        ++reads;
        if (! publish)
            return false;                 // coherent read unavailable
        out = state;
        return true;
    }

    mutable int reads = 0;
    bool publish = false, rejectAll = false;
    jam::JamLiveState state {};
    std::vector<jam::JamLiveCommand> commands;
};

jam::JamLiveState makeLive()
{
    jam::JamLiveState s;
    s.prepared = true;
    s.requestedRunning = true;
    s.joinPending = true;
    s.drumsPlaying = false;
    s.backend = jam::JamLiveBackend::experimentalBTrack;
    s.mode = jam::TempoMode::Follow;
    s.candidateBpm = 118.4f;
    s.clock.bpm = 120.0;
    s.clock.confidence01 = 0.9f;
    s.clock.lockState = jam::ClockLockState::Locked;
    s.clock.beatInBar = 3;
    s.clock.beatsPerBar = 4;
    s.clock.beatUnit = 4;
    s.inputPeak = 0.5f;
    s.sessionGeneration = 7;
    s.audioSampleTime = 100000;
    s.lastReceiptSampleTime = 99000;
    s.receiptMeasured = true;
    s.analysisDrops = 1;
    s.observationDrops = 2;
    s.userCommandDrops = 3;
    s.drumCommandDrops = 4;
    s.discontinuities = 5;
    return s;
}

void ensureJuceInitialised()
{
    // Component construction needs the message manager; getInstance is idempotent.
    juce::MessageManager::getInstance();
}

int actionIndexNamed (JamOverlay& o, const juce::String& name)
{
    for (int i = 0; i < o.getNumActions(); ++i)
        if (o.getActionButton (i).getName() == name)
            return i;
    return -1;
}

jam::JamLiveCommand lastCommand (const MockJamControl& m)
{
    REQUIRE (! m.commands.empty());
    return m.commands.back();
}
}

//==============================================================================
TEST_CASE (uiLive_coldDefaultIsUnavailable)
{
    MockJamControl mock;
    JamLivePresenter p (mock);
    CHECK (! p.poll());                       // no coherent read yet
    const auto& v = p.viewState();
    CHECK (! v.hasState);
    CHECK (! v.prepared);
    CHECK (! v.running);
    CHECK (v.candidateBpm == 0.0);
    CHECK (v.clockBpm == 0.0);
    CHECK (v.confidence == 0.0f);
    CHECK (v.inputLevel == 0.0f);
    CHECK (v.bar == 0 && v.beat == 0);
    CHECK (v.backendName == "unavailable");
    CHECK (v.statusText == "NOT CONNECTED");
    CHECK (v.availability.containsIgnoreCase ("not implemented"));
    CHECK (v.diagnostics.isNotEmpty());
    CHECK (p.readFailureCount() == 1);
}

TEST_CASE (uiLive_coherentReadMapsTelemetry)
{
    MockJamControl mock;
    mock.publish = true;
    mock.state = makeLive();
    JamLivePresenter p (mock);

    REQUIRE (p.poll());
    const auto& v = p.viewState();
    CHECK (v.hasState);
    CHECK (v.prepared);
    CHECK (v.requestedRunning);
    CHECK (v.running);                        // requested only, never claimed as sound
    CHECK (! v.drumsPlaying);
    CHECK (v.joinPending);
    CHECK (v.lock == JamViewState::Lock::locked);
    CHECK (v.candidateBpm > 118.0 && v.candidateBpm < 119.0);
    CHECK (v.clockBpm == 120.0);
    CHECK (v.confidence > 0.89f);
    CHECK (v.inputLevel > 0.49f);
    CHECK (v.beat == 3 && v.beatsPerBar == 4);
    CHECK (v.sessionGeneration == 7);
    CHECK (v.receiptMeasured);
    CHECK (v.receiptLagSamples == 1000);
    CHECK (v.analysisDrops == 1 && v.observationDrops == 2);
    CHECK (v.userCommandDrops == 3 && v.drumCommandDrops == 4);
    CHECK (v.discontinuities == 5);
    CHECK (v.backendName == "experimental BTrack");
    CHECK (v.mode == 2);                      // TempoMode::Follow -> combo id 2
    CHECK (v.nextIntent.containsIgnoreCase ("waiting"));
}

TEST_CASE (uiLive_failedReadRetainsWholePreviousState)
{
    MockJamControl mock;
    mock.publish = true;
    mock.state = makeLive();
    JamLivePresenter p (mock);
    REQUIRE (p.poll());
    const double firstBpm = p.viewState().clockBpm;
    CHECK (p.viewState().hasState);

    // Worker stops publishing and its state drifts: the UI must keep the whole
    // previous snapshot, not a partial merge.
    mock.publish = false;
    mock.state.clock.bpm = 999.0;
    mock.state.prepared = false;
    CHECK (! p.poll());
    CHECK (p.viewState().clockBpm == firstBpm);
    CHECK (p.viewState().prepared);
    CHECK (p.readFailureCount() == 1);
}

//==============================================================================
TEST_CASE (uiLive_statusStringsSeparateIntentFromEcho)
{
    MockJamControl mock;
    JamLivePresenter p (mock);

    mock.publish = false;
    p.poll();
    CHECK (p.viewState().statusText == "NOT CONNECTED");

    mock.publish = true;
    mock.state = jam::JamLiveState {};
    mock.state.prepared = true;
    mock.state.sessionGeneration = 1;
    p.poll();
    CHECK (p.viewState().statusText == "PREPARED - STOPPED");

    mock.state.requestedRunning = true;
    mock.state.joinPending = true;
    mock.state.drumsPlaying = false;
    p.poll();
    CHECK (p.viewState().statusText == "ARMED - WAITING FOR THE CLOCK");

    mock.state.joinPending = false;
    p.poll();
    CHECK (p.viewState().statusText == "ARMED - LISTENING");

    // Only the audio-owner echo is allowed to say PLAYING.
    mock.state.drumsPlaying = true;
    p.poll();
    CHECK (p.viewState().statusText == "PLAYING (AUDIO ECHO)");

    // Accepted intent that has not been echoed must never claim sound.
    mock.state.drumsPlaying = false;
    mock.state.requestedRunning = false;
    p.poll();
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (p.viewState().statusText == "START QUEUED - WAITING FOR THE ENGINE");
    CHECK (! p.viewState().drumsPlaying);
}

TEST_CASE (uiLive_intentToFrozenCommand)
{
    jam::JamLiveCommand c;
    CHECK (JamLivePresenter::mapIntent ({ K::startStop, 0.0 }, c) && c.type == jam::JamLiveCommandType::Start);
    CHECK (JamLivePresenter::mapIntent ({ K::tap, 0.0 }, c) && c.type == jam::JamLiveCommandType::TapTempo);
    CHECK (JamLivePresenter::mapIntent ({ K::resync, 0.0 }, c) && c.type == jam::JamLiveCommandType::ResyncNextBeat);
    CHECK (JamLivePresenter::mapIntent ({ K::resyncBar, 0.0 }, c) && c.type == jam::JamLiveCommandType::ResyncNextBar);
    CHECK (JamLivePresenter::mapIntent ({ K::half, 0.0 }, c) && c.type == jam::JamLiveCommandType::HalfTime);
    CHECK (JamLivePresenter::mapIntent ({ K::doubleTempo, 0.0 }, c) && c.type == jam::JamLiveCommandType::DoubleTime);
    CHECK (JamLivePresenter::mapIntent ({ K::freeze, 0.0 }, c) && c.type == jam::JamLiveCommandType::FreezeTempo);
    CHECK (JamLivePresenter::mapIntent ({ K::resume, 0.0 }, c) && c.type == jam::JamLiveCommandType::ResumeFollow);
    CHECK (JamLivePresenter::mapIntent ({ K::stopNextBar, 0.0 }, c) && c.type == jam::JamLiveCommandType::StopAtNextBar);
    CHECK (JamLivePresenter::mapIntent ({ K::reset, 0.0 }, c) && c.type == jam::JamLiveCommandType::Reset);

    // Mode combo ids 1..3 map to TempoMode Fixed/Follow/Loose; out of range clamps.
    CHECK (JamLivePresenter::mapIntent ({ K::mode, 1.0 }, c) && c.type == jam::JamLiveCommandType::SetMode && c.value == 0.0);
    CHECK (JamLivePresenter::mapIntent ({ K::mode, 2.0 }, c) && c.value == 1.0);
    CHECK (JamLivePresenter::mapIntent ({ K::mode, 3.0 }, c) && c.value == 2.0);
    CHECK (JamLivePresenter::mapIntent ({ K::mode, 9.0 }, c) && c.value == 2.0);
    CHECK (JamLivePresenter::mapIntent ({ K::mode, 0.0 }, c) && c.value == 0.0);

    // Unsupported controls are never silently queued.
    CHECK (! JamLivePresenter::mapIntent ({ K::style, 2.0 }, c));
    CHECK (! JamLivePresenter::mapIntent ({ K::intensity, 70.0 }, c));
    CHECK (! JamLivePresenter::mapIntent ({ K::complexity, 40.0 }, c));
    CHECK (! JamLivePresenter::mapIntent ({ K::fillAmount, 30.0 }, c));
    CHECK (! JamLivePresenter::mapIntent ({ K::followTightness, 60.0 }, c));
    CHECK (! JamLivePresenter::mapIntent ({ K::fill, 0.0 }, c));
    CHECK (! JamLivePresenter::mapIntent ({ K::breakBar, 0.0 }, c));
}

TEST_CASE (uiLive_submitRejectIsVisibleAndNotApplied)
{
    MockJamControl mock;
    mock.rejectAll = true;
    JamLivePresenter p (mock);
    CHECK (! p.submit ({ K::tap, 0.0 }));
    CHECK (p.rejectedCount() == 1);
    CHECK (p.lastFeedback().containsIgnoreCase ("rejected"));
    CHECK (! p.viewState().commandFeedback.isEmpty());
    CHECK (! p.viewState().running);          // nothing applied
    CHECK (p.lastCommandType() == (int) jam::JamLiveCommandType::TapTempo);

    // Unsupported intent: no command reaches the facade at all.
    const auto before = mock.commands.size();
    CHECK (! p.submit ({ K::fill, 50.0 }));
    CHECK (mock.commands.size() == before);
    CHECK (p.lastFeedback().containsIgnoreCase ("not implemented"));
}

//==============================================================================
// L2: fast double/triple click must toggle off the LOCAL accepted-intent latch,
// not the stale audio echo.
TEST_CASE (uiLive_fastDoubleClickCancelsPendingStart)
{
    MockJamControl mock;
    mock.publish = true;
    mock.state.prepared = true;
    mock.state.sessionGeneration = 5;
    mock.state.requestedRunning = false;
    JamLivePresenter p (mock);
    p.poll();

    REQUIRE (p.submit ({ K::startStop, 0.0 }));   // no poll in between
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Start);
    CHECK (p.startIntentLatched());
    CHECK (p.viewState().running);                // selection only
    CHECK (p.viewState().statusText.startsWith ("START QUEUED"));

    // Second click before any echo: must be a Stop, cancelling the pending join.
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Stop);
    CHECK (! p.startIntentLatched());
    CHECK (p.stopQueued());
    CHECK (p.viewState().statusText.startsWith ("STOP QUEUED"));

    // A stale read with the same generation and requestedRunning == false must
    // NOT resurrect or clear the accepted request incorrectly.
    p.poll();
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Start);   // toggle again

    // Triple-click pattern: Start, Stop, Start.
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Stop);
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Start);
}

TEST_CASE (uiLive_rejectedStartIsNotPretendedAccepted)
{
    MockJamControl mock;
    mock.publish = true;
    mock.state.prepared = true;
    mock.state.sessionGeneration = 5;
    mock.rejectAll = true;
    JamLivePresenter p (mock);
    p.poll();

    CHECK (! p.submit ({ K::startStop, 0.0 }));
    CHECK (! p.startIntentLatched());
    CHECK (! p.viewState().running);
    CHECK (! p.viewState().statusText.startsWith ("START QUEUED"));
    CHECK (p.lastFeedback().containsIgnoreCase ("rejected"));
}

TEST_CASE (uiLive_generationResetReleasesPendingLatch)
{
    MockJamControl mock;
    mock.publish = true;
    mock.state.prepared = true;
    mock.state.sessionGeneration = 5;
    JamLivePresenter p (mock);
    p.poll();
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (p.startIntentLatched());

    // Device re-prepare bumps the generation: the pending latch is released.
    mock.state.sessionGeneration = 6;
    mock.state.requestedRunning = false;
    p.poll();
    CHECK (! p.startIntentLatched());
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Start);   // fresh start, not a stop

    // Prepared release also releases it.
    mock.state.prepared = false;
    p.poll();
    CHECK (! p.startIntentLatched());
}

//==============================================================================
// B1: at the real 1100x700 canvas every primary Start/correction control must be
// completely inside the visible viewport before any scrolling.
TEST_CASE (uiLive_primaryControlsVisibleAtRealCanvas)
{
    ensureJuceInitialised();
    JamOverlay o;
    o.setViewState (JamViewState {});
    o.setSize (1100, 700);

    const auto view = o.viewportVisibleArea();
    CHECK (view.getWidth() == 1100);
    CHECK (view.getHeight() == 700);

    std::vector<const juce::Component*> primary;
    primary.push_back (&o.getStartButton());
    const char* names[] { "TAP", "RESYNC", "RESYNC BAR", "HALF", "DOUBLE",
                          "FREEZE", "RESUME", "STOP NEXT BAR", "RESET" };
    for (auto* n : names)
    {
        const int i = actionIndexNamed (o, n);
        REQUIRE (i >= 0);
        primary.push_back (&o.getActionButton (i));
    }

    for (auto* c : primary)
    {
        const auto area = o.visibleControlArea (*c);
        CHECK (view.contains (area));                 // fully visible, before scroll
        CHECK (area.getHeight() >= 24);               // no zero-height trick
        CHECK (area.getWidth() >= 60);
        CHECK (area.getY() >= 0 && area.getBottom() <= 700);
    }
    for (size_t i = 0; i < primary.size(); ++i)
        for (size_t j = i + 1; j < primary.size(); ++j)
            CHECK (! o.visibleControlArea (*primary[i]).intersects (o.visibleControlArea (*primary[j])));

    // Sections are non-superposed, non-zero and the action strip is on-screen.
    const auto h = o.getHeaderArea(), s = o.getStatusArea(), t = o.getTelemetryArea();
    const auto a = o.getActionArea(), c = o.getControlsArea();
    CHECK (h.getHeight() > 0 && s.getHeight() > 0 && t.getHeight() > 0);
    CHECK (a.getHeight() > 0 && c.getHeight() > 0);
    CHECK (! h.intersects (s));
    CHECK (! s.intersects (t));
    CHECK (! t.intersects (a));
    CHECK (! a.intersects (c));
    CHECK (a.getBottom() <= view.getBottom());        // action strip never needs scroll
    CHECK (c.getBottom() <= view.getBottom());        // advanced controls also on-screen
}

//==============================================================================
TEST_CASE (uiLive_overlayProductionPresentation)
{
    ensureJuceInitialised();
    JamOverlay o;
    o.setViewState (JamViewState {});

    CHECK (! o.isSimulatedPreview());
    CHECK (o.getStyleBox().getNumItems() == 1);
    CHECK (o.getStyleBox().getItemText (0) == "Rock");
    CHECK (! o.getStyleBox().isEnabled());           // surfaced but not implemented
    CHECK (o.getModeBox().getNumItems() == 3);
    CHECK (o.getModeBox().getItemText (1) == "Follow");
    CHECK (o.getModeBox().isEnabled());

    for (int i = 0; i < 4; ++i)
        CHECK (! o.getAmountSlider (i).isEnabled());

    const int tap = actionIndexNamed (o, "TAP");
    const int fill = actionIndexNamed (o, "FILL");
    const int brk = actionIndexNamed (o, "BREAK");
    REQUIRE (tap >= 0 && fill >= 0 && brk >= 0);
    CHECK (o.getActionButton (tap).isEnabled());
    CHECK (! o.getActionButton (fill).isEnabled());
    CHECK (! o.getActionButton (brk).isEnabled());

    // Accessibility: every control carries a name + title/description.
    for (int i = 0; i < o.getNumActions(); ++i)
    {
        CHECK (o.getActionButton (i).getName().isNotEmpty());
        CHECK (o.getActionButton (i).getTitle().isNotEmpty());
        CHECK (o.getActionButton (i).getDescription().isNotEmpty());
    }
    CHECK (o.getStartButton().getName() == "StartStop");
    CHECK (o.getStartButton().getDescription().isNotEmpty());

    o.setSize (1100, 700);
    CHECK (o.getStartButton().getWidth() >= 60);
    for (int i = 0; i < o.getNumActions(); ++i)
    {
        CHECK (o.getActionButton (i).getWidth() >= 60);
        CHECK (o.getActionButton (i).getHeight() >= 24);
    }
}

TEST_CASE (uiLive_overlayPreviewOptInRestoresDemoControls)
{
    ensureJuceInitialised();
    JamOverlay o;
    o.setSize (1100, 700);
    o.setSimulatedPreview (true);
    CHECK (o.isSimulatedPreview());
    CHECK (o.getStyleBox().getNumItems() == 4);
    CHECK (o.getStyleBox().isEnabled());
    CHECK (o.getModeBox().getNumItems() == 5);
    for (int i = 0; i < 4; ++i)
        CHECK (o.getAmountSlider (i).isEnabled());
    CHECK (o.getActionButton (actionIndexNamed (o, "FILL")).isEnabled());
    CHECK (o.getActionButton (actionIndexNamed (o, "BREAK")).isEnabled());
}

TEST_CASE (uiLive_overlayEscapeClosesOnceTabFallsThrough)
{
    ensureJuceInitialised();
    JamOverlay o;
    o.setSize (1100, 700);
    int closes = 0;
    o.onClose = [&closes] { ++closes; };

    o.open();
    CHECK (o.isVisible());
    CHECK (! o.keyPressed (juce::KeyPress (juce::KeyPress::tabKey)));   // traversal, not consumed
    CHECK (o.isVisible());                                              // Tab did not close
    CHECK (o.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (! o.isVisible());
    CHECK (closes == 1);
    o.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));          // already closed
    CHECK (closes == 1);                                                // onClose fires once
}

// The real posted-click (triggerClick + message pump) path runs in the isolated
// tools/live-jam-ui harness: stopping the MessageManager dispatch loop poisons
// callAsync for the rest of a shared test binary, so it is kept out of this file.
TEST_CASE (uiLive_overlayClicksDrivePresenter)
{
    ensureJuceInitialised();
    MockJamControl mock;
    JamLivePresenter p (mock);
    JamOverlay o;
    o.onIntent = [&p] (const JamUiIntent& i) { p.submit (i); };
    o.setViewState (p.viewState());
    o.setSize (1100, 700);

    // Mode is the implemented enumeration control: selecting Loose sends SetMode.
    o.getModeBox().setSelectedId (3, juce::sendNotificationSync);
    const auto modeCmd = lastCommand (mock);
    CHECK (modeCmd.type == jam::JamLiveCommandType::SetMode);
    CHECK (modeCmd.value == 2.0);

    // TAP action maps to the frozen TapTempo command.
    const int tap = actionIndexNamed (o, "TAP");
    REQUIRE (tap >= 0);
    o.getActionButton (tap).onClick();
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::TapTempo);

    // Disabled controls: a programmatic intensity change is rejected, not queued,
    // and the disabled buttons report disabled (real clicks are covered in the
    // harness).
    const int fill = actionIndexNamed (o, "FILL");
    REQUIRE (fill >= 0);
    CHECK (! o.getActionButton (fill).isEnabled());
    const auto beforeAmount = mock.commands.size();
    o.getAmountSlider (0).setValue (55.0, juce::sendNotificationSync);
    CHECK (mock.commands.size() == beforeAmount);
    CHECK (p.lastFeedback().containsIgnoreCase ("not implemented"));
}
