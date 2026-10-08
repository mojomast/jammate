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
