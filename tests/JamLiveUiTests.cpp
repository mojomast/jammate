// UI-LIVE-001: mapping and control tests for the live Jam screen.
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

TEST_CASE (uiLive_startStopFollowsAcceptedIntentEcho)
{
    MockJamControl mock;
    JamLivePresenter p (mock);
    p.poll();
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Start);

    // Once Start is accepted the same button must schedule a Stop, not re-arm.
    mock.state = makeLive();
    mock.state.requestedRunning = true;
    mock.publish = true;
    p.poll();
    REQUIRE (p.submit ({ K::startStop, 0.0 }));
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::Stop);
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

    o.setSize (1100, 700);                        // fixed design canvas
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
    o.setSize (1060, 720);
    o.setSimulatedPreview (true);
    CHECK (o.isSimulatedPreview());
    CHECK (o.getStyleBox().getNumItems() == 4);
    CHECK (o.getStyleBox().isEnabled());
    CHECK (o.getModeBox().getNumItems() == 5);
    CHECK (o.getActionButton (actionIndexNamed (o, "FILL")).isEnabled());
    CHECK (o.getActionButton (actionIndexNamed (o, "BREAK")).isEnabled());
}

TEST_CASE (uiLive_overlayClicksDrivePresenter)
{
    ensureJuceInitialised();
    MockJamControl mock;
    JamLivePresenter p (mock);
    JamOverlay o;
    o.onIntent = [&p] (const JamUiIntent& i) { p.submit (i); };
    o.setViewState (p.viewState());
    o.setSize (1060, 720);

    // Mode is the implemented enumeration control: selecting Loose sends SetMode.
    o.getModeBox().setSelectedId (3, juce::sendNotificationSync);
    REQUIRE (! mock.commands.empty());
    const auto modeCmd = lastCommand (mock);
    CHECK (modeCmd.type == jam::JamLiveCommandType::SetMode);
    CHECK (modeCmd.value == 2.0);

    // TAP action button maps to the frozen TapTempo command (invoke the wired
    // handler directly; triggerClick would post asynchronously).
    const int tap = actionIndexNamed (o, "TAP");
    REQUIRE (tap >= 0);
    o.getActionButton (tap).onClick();
    CHECK (lastCommand (mock).type == jam::JamLiveCommandType::TapTempo);

    // A programmatic intensity change is not a supported command: rejected, not
    // queued (the control is disabled in production, so no user can emit it).
    const auto before = mock.commands.size();
    o.getAmountSlider (0).setValue (55.0, juce::sendNotificationSync);
    CHECK (mock.commands.size() == before);
    CHECK (p.lastFeedback().containsIgnoreCase ("not implemented"));
}
