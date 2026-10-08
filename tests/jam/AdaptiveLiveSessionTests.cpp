#include "JamTest.h"
#include "jam/LiveJamSession.h"
#include <limits>
#include <memory>

namespace
{
struct QuietTracker final : jam::IRhythmTracker
{
    void reset (double) override {}
    const char* id() const noexcept override { return "adaptive-session-test"; }
    jam::RhythmObservation process (const jam::AnalysisFrame&) override
    { return {}; }
};

void checkCoalescedStopStart (jam::JamLiveCommandType stopType, bool sameTickFill = false)
{
    jam::LiveJamSession session;
    REQUIRE (session.setTracker (std::make_unique<QuietTracker>(), jam::JamLiveBackend::injectedTest));
    REQUIRE (session.prepare (48000.0, 512, false));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::Start, 0.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::TapTempo, 0.0 }));
    session.stepControlForTesting();
    session.publishAudioCursor (24000);
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::TapTempo, 0.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::SetFillAmount, 0.0 }));
    session.stepControlForTesting();
    jam::DrumClockCommand command;
    while (session.drumCommandQueue().pop (command)) {}
    jam::DrumPlaybackEcho echo;
    echo.sessionGeneration = session.currentGeneration();
    echo.attached = echo.injectedActive = echo.injectedPlaying = true;
    echo.samplePosition = 24512;
    echo.groove = 0;
    session.publishDrumEcho (echo);
    session.publishAudioCursor (24512);
    jam::ObservationEnvelope evidence;
    evidence.observation.inputSampleTime = 24512;
    evidence.observation.sourceSampleRate = evidence.sourceSampleRate = 48000.0;
    evidence.observation.bpmCandidate = 120.0f;
    evidence.observation.beatConfidence01 = 0.99f;
    evidence.observation.energyRmsDbfs = -20.0f;
    evidence.inputHorizonSampleTime = evidence.blockStartSampleTime = 24512;
    evidence.streamGeneration = 1;
    session.injectObservationForTesting (evidence);
    session.stepControlForTesting();
    while (session.drumCommandQueue().pop (command)) {}

    REQUIRE (session.submitCommand ({ stopType, 0.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::Start, 0.0 }));
    if (sameTickFill)
        REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::RequestFill, 0.0 }));
    session.publishAudioCursor (25024);
    session.stepControlForTesting();
    bool fill = false;
    while (session.drumCommandQueue().pop (command))
    {
        CHECK (command.type != jam::DrumClockCommandType::Clear
               && command.type != jam::DrumClockCommandType::StopAtBar);
        fill = fill || (command.type == jam::DrumClockCommandType::BarChange && command.fill >= 0);
    }
    if (! sameTickFill)
        REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::RequestFill, 0.0 }));
    session.publishAudioCursor (25536);
    session.stepControlForTesting();
    while (session.drumCommandQueue().pop (command))
        fill = fill || (command.type == jam::DrumClockCommandType::BarChange && command.fill >= 0);
    CHECK (fill);
    jam::JamLiveState state;
    REQUIRE (session.readState (state));
    CHECK (state.requestedRunning && state.drumsPlaying);
    CHECK (state.clock.lockState == jam::ClockLockState::Locked);
}
}

JAM_TEST (AdaptiveLiveSession, sameTickStopStartKeepsDirectorResponsive)
{
    checkCoalescedStopStart (jam::JamLiveCommandType::Stop);
}

JAM_TEST (AdaptiveLiveSession, sameTickBarStopStartKeepsDirectorResponsive)
{
    checkCoalescedStopStart (jam::JamLiveCommandType::StopAtNextBar);
}

JAM_TEST (AdaptiveLiveSession, sameTickStopStartFillRetainsNewPulse)
{
    checkCoalescedStopStart (jam::JamLiveCommandType::Stop, true);
}

JAM_TEST (AdaptiveLiveSession, sameTickBarStopStartFillRetainsNewPulse)
{
    checkCoalescedStopStart (jam::JamLiveCommandType::StopAtNextBar, true);
}

JAM_TEST (AdaptiveLiveSession, selectedStyleJoinsAndFillWaitsForActualPlayback)
{
    jam::LiveJamSession session;
    REQUIRE (session.setTracker (std::make_unique<QuietTracker>(), jam::JamLiveBackend::injectedTest));
    REQUIRE (session.prepare (48000.0, 512, false));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::SetStyle, 3.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::SetFillAmount, 0.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::Start, 0.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::TapTempo, 0.0 }));
    session.stepControlForTesting();
    session.publishAudioCursor (24000);
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::TapTempo, 0.0 }));
    session.stepControlForTesting();
    jam::DrumClockCommand command;
    std::uint64_t joinSample = 0;
    bool join = false, prematureChange = false;
    while (session.drumCommandQueue().pop (command))
    {
        if (command.type == jam::DrumClockCommandType::JoinAtBar)
        {
            join = true;
            joinSample = command.sampleTime;
            CHECK (command.groove == jam::StyleCatalog::style (jam::StyleId::Funk)
                                    .grooves[static_cast<int> (jam::GrooveTier::Medium)][0].index);
        }
        prematureChange = prematureChange || command.type == jam::DrumClockCommandType::BarChange;
    }
    REQUIRE (join);
    CHECK (! prematureChange);
    jam::ObservationEnvelope evidence;
    evidence.observation.inputSampleTime = joinSample;
    evidence.observation.sourceSampleRate = evidence.sourceSampleRate = 48000.0;
    evidence.observation.bpmCandidate = 120.0f;
    evidence.observation.beatConfidence01 = 0.99f;
    evidence.observation.energyRmsDbfs = -20.0f;
    evidence.inputHorizonSampleTime = joinSample;
    evidence.blockStartSampleTime = joinSample;
    evidence.streamGeneration = 1;
    for (std::uint64_t cursor = 48000; cursor < joinSample; cursor += 24000)
    {
        session.publishAudioCursor (cursor);
        evidence.observation.inputSampleTime = evidence.inputHorizonSampleTime
            = evidence.blockStartSampleTime = cursor;
        session.injectObservationForTesting (evidence);
        session.stepControlForTesting();
    }
    session.publishAudioCursor (joinSample);
    evidence.observation.inputSampleTime = evidence.inputHorizonSampleTime
        = evidence.blockStartSampleTime = joinSample;
    session.injectObservationForTesting (evidence);
    jam::DrumPlaybackEcho echo;
    echo.sessionGeneration = session.currentGeneration();
    echo.attached = echo.injectedActive = echo.injectedPlaying = true;
    echo.samplePosition = joinSample;
    echo.groove = jam::StyleCatalog::style (jam::StyleId::Funk)
                  .grooves[static_cast<int> (jam::GrooveTier::Medium)][0].index;
    session.publishDrumEcho (echo);
    session.stepControlForTesting();
    while (session.drumCommandQueue().pop (command)) {}
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::RequestFill, 0.0 }));
    session.publishAudioCursor (joinSample + 512);
    evidence.observation.inputSampleTime = evidence.inputHorizonSampleTime
        = evidence.blockStartSampleTime = joinSample + 512;
    session.injectObservationForTesting (evidence);
    session.stepControlForTesting();
    bool fill = false;
    while (session.drumCommandQueue().pop (command))
    {
        if (command.type == jam::DrumClockCommandType::BarChange && command.fill >= 0)
        {
            fill = true;
            CHECK (command.sampleTime > joinSample + 512);
        }
    }
    REQUIRE (fill);
    jam::JamLiveState state;
    REQUIRE (session.readState (state));
    CHECK (state.adaptiveChangePending);
    CHECK (! state.fillPlaying); // command publication is not audio playback
    echo.fillPlaying = true;
    echo.samplePosition = session.audioCursor();
    session.publishDrumEcho (echo);
    session.stepControlForTesting();
    REQUIRE (session.readState (state));
    CHECK (state.fillPlaying);
    // A Locked label alone cannot authorize a fill after confidence decays.
    // Keep fresh usable energy (no Holdover), but feed weak beat confidence.
    evidence.observation.beatConfidence01 = 0.0f;
    std::uint64_t cursor = joinSample + 512;
    for (int i = 0; i < 20; ++i)
    {
        cursor += 512;
        session.publishAudioCursor (cursor);
        evidence.observation.inputSampleTime = evidence.inputHorizonSampleTime
            = evidence.blockStartSampleTime = cursor;
        session.injectObservationForTesting (evidence);
        session.stepControlForTesting();
        while (session.drumCommandQueue().pop (command)) {}
    }
    REQUIRE (session.readState (state));
    REQUIRE (state.clock.lockState == jam::ClockLockState::Locked);
    REQUIRE (state.clock.confidence01 < jam::DirectorConfig {}.fillConfidenceThreshold);
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::RequestFill, 0.0 }));
    session.publishAudioCursor (++cursor);
    session.stepControlForTesting();
    while (session.drumCommandQueue().pop (command))
        CHECK (command.type != jam::DrumClockCommandType::BarChange || command.fill < 0);
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::Stop, 0.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::RequestFill, 0.0 }));
    session.publishAudioCursor (++cursor);
    session.stepControlForTesting();
    bool clear = false;
    while (session.drumCommandQueue().pop (command))
    {
        clear = clear || command.type == jam::DrumClockCommandType::Clear;
        CHECK (command.type != jam::DrumClockCommandType::BarChange);
    }
    CHECK (clear);
}

JAM_TEST (AdaptiveLiveSession, malformedControlsCannotConsumeBoundedQueue)
{
    jam::LiveJamSession session;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto& c : { jam::JamLiveCommand { jam::JamLiveCommandType::SetStyle, -1.0 },
                          { jam::JamLiveCommandType::SetStyle, 6.0 },
                          { jam::JamLiveCommandType::SetStyle, 0.5 },
                          { jam::JamLiveCommandType::SetIntensity, nan },
                          { jam::JamLiveCommandType::SetComplexity, 1.1 },
                          { jam::JamLiveCommandType::SetFillAmount, -0.1 },
                          { jam::JamLiveCommandType::SetMode, nan },
                          { static_cast<jam::JamLiveCommandType> (999), 0.0 } })
        CHECK (! session.submitCommand (c));
    for (std::size_t i = 0; i < jam::kJamLiveCommandCapacity; ++i)
        CHECK (session.submitCommand ({ jam::JamLiveCommandType::SetIntensity, 0.5 }));
    CHECK (! session.submitCommand ({ jam::JamLiveCommandType::RequestFill, 0.0 }));
}

JAM_TEST (AdaptiveLiveSession, workerSettingsAndPlaybackEchoStaySeparate)
{
    jam::LiveJamSession session;
    REQUIRE (session.setTracker (std::make_unique<QuietTracker>(), jam::JamLiveBackend::injectedTest));
    REQUIRE (session.prepare (48000.0, 512, false));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::SetStyle, 3.0 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::SetIntensity, 0.75 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::SetComplexity, 0.25 }));
    REQUIRE (session.submitCommand ({ jam::JamLiveCommandType::SetFillAmount, 0.0 }));
    session.stepControlForTesting();
    jam::JamLiveState state;
    REQUIRE (session.readState (state));
    CHECK (state.styleIndex == 3);
    CHECK_NEAR (state.intensity01, 0.75f, 0.0001);
    CHECK_NEAR (state.complexity01, 0.25f, 0.0001);
    CHECK (state.fillAmount01 == 0.0f);
    CHECK (! state.drumsPlaying);
    CHECK (state.activeGroove == jam::kNoLibraryEntry);
    CHECK (! state.fillPlaying);
    // A cold/stale generation cannot report an applied groove or fill.
    jam::DrumPlaybackEcho echo;
    echo.sessionGeneration = session.currentGeneration() - 1;
    echo.attached = echo.injectedActive = echo.injectedPlaying = true;
    echo.groove = 63;
    echo.fillPlaying = true;
    session.publishDrumEcho (echo);
    session.stepControlForTesting();
    REQUIRE (session.readState (state));
    CHECK (! state.drumsPlaying && ! state.fillPlaying);
    CHECK (state.activeGroove == jam::kNoLibraryEntry);
}
