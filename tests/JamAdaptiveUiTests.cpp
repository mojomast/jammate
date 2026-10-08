// Adaptive intent and telemetry checks; mock state is never physical evidence.
#include "TestHarness.h"
#include "../src/ui/JamLivePresenter.h"
#include <limits>

namespace
{
struct AdaptiveControl : jam::IJamLiveControl
{
    jam::JamLiveState state {};
    bool accept = true;
    int submissions = 0;
    bool submitJamCommand (const jam::JamLiveCommand&) noexcept override
    { ++submissions; return accept; }
    bool readJamLiveState (jam::JamLiveState& out) const noexcept override
    { out = state; return true; }
};
}

TEST_CASE (uiLive_adaptiveInvalidInputNeverReachesQueue)
{
    AdaptiveControl c;
    JamLivePresenter p (c);
    using K = JamUiIntent::Kind;
    for (const auto& intent : { JamUiIntent { K::style, 0.0 }, { K::style, 7.0 },
                               { K::style, 1.5 }, { K::intensity, -1.0 },
                               { K::complexity, 101.0 },
                               { K::fillAmount, std::numeric_limits<double>::quiet_NaN() } })
        CHECK (! p.submit (intent));
    CHECK (c.submissions == 0);
    CHECK (p.lastFeedback().containsIgnoreCase ("invalid"));
    jam::JamLiveCommand command;
    CHECK (JamLivePresenter::mapIntent ({ K::mode, std::numeric_limits<double>::max() }, command));
    CHECK (command.value == 2.0);
    CHECK (JamLivePresenter::mapIntent ({ K::mode, -std::numeric_limits<double>::max() }, command));
    CHECK (command.value == 0.0);
}

TEST_CASE (uiLive_adaptiveQueueIntentDoesNotInventAppliedSettings)
{
    AdaptiveControl c;
    c.state.styleIndex = 3;
    c.state.intensity01 = 0.75f;
    c.state.complexity01 = 0.25f;
    c.state.fillAmount01 = 0.5f;
    JamLivePresenter p (c);
    REQUIRE (p.poll());
    CHECK (p.viewState().style == 4);
    CHECK (p.viewState().amounts[0] == 75.0);
    REQUIRE (p.submit ({ JamUiIntent::Kind::style, 6.0 }));
    CHECK (p.viewState().style == 4); // accepted queue is not applied state
    c.accept = false;
    CHECK (! p.submit ({ JamUiIntent::Kind::intensity, 10.0 }));
    CHECK (p.viewState().amounts[0] == 75.0);
    c.state.styleIndex = 5;
    c.state.intensity01 = 0.1f;
    REQUIRE (p.poll());
    CHECK (p.viewState().style == 6);
    CHECK (p.viewState().amounts[0] > 9.9 && p.viewState().amounts[0] < 10.1);
}
