// Unit tests for jam::JamJoinPolicy — the minimal live-slice director
// (INT-LIVE-001, LIVE-JAM-CONTRACT.md).
//
// The policy is a pure state machine: no threads, no time source, no audio.
// These tests pin the contract the session depends on:
//   - one join, only from a Locked clock, retried until the bridge accepts it;
//   - join is pending until the audio-owner echo confirms it (intent, sent and
//     echo-ack are separate);
//   - Holdover HOLDS;
//   - Loss stops safely but keeps the running intent so a recovered lock can
//     rejoin;
//   - an explicit UI Stop/Reset cancels any future join and stops, and the
//     pending stop is NOT cleared by a still-playing echo;
//   - a discontinuity forces a cancel stop.

#include "JamTest.h"

#include "jam/JamJoinPolicy.h"

#include <cstdint>

using jam::ClockLockState;
using jam::ClockSnapshot;
using jam::JamJoinAction;
using jam::JamJoinPolicy;
using jam::JamStopKind;

namespace
{

ClockSnapshot snapshotWith (ClockLockState lock)
{
    ClockSnapshot s;
    s.generation = 1;
    s.bpm = 120.0;
    s.beatPhase01 = 0.0;
    s.barPhase01 = 0.0;
    s.beatsPerBar = 4;
    s.confidence01 = 0.9f;
    s.lockState = lock;
    return s;
}

void engage (JamJoinPolicy& policy)
{
    policy.notifyStart();
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::joinAtNextBar);
    policy.notifyJoinAccepted (true);
    policy.notifyPlaybackEcho (true);
    CHECK (policy.engaged());
}

} // namespace

JAM_TEST (jamjoinpolicy, noJoinWhileAcquiring)
{
    JamJoinPolicy policy;
    policy.notifyStart();
    const auto d = policy.update (snapshotWith (ClockLockState::Acquiring), false);
    CHECK (d.action == JamJoinAction::none);
    CHECK (! policy.joinPending());
    CHECK (! policy.engaged());
}

JAM_TEST (jamjoinpolicy, joinFromLockedOnlyOnce)
{
    JamJoinPolicy policy;
    policy.notifyStart();

    const auto d1 = policy.update (snapshotWith (ClockLockState::Locked), false);
    CHECK (d1.action == JamJoinAction::joinAtNextBar);
    CHECK (policy.joinPending());

    // Accepted: waiting for the echo, no second join.
    policy.notifyJoinAccepted (true);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
    CHECK (policy.joinPending());

    policy.notifyPlaybackEcho (true);
    CHECK (policy.engaged());
    CHECK (! policy.joinPending());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, rejectedJoinIsRetriedNotLatched)
{
    JamJoinPolicy policy;
    policy.notifyStart();

    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::joinAtNextBar);
    policy.notifyJoinAccepted (false); // queue full

    // Still wanted, and retried next tick.
    CHECK (policy.joinPending());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::joinAtNextBar);

    policy.notifyJoinAccepted (true);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, holdoverHoldsEngagedGrid)
{
    JamJoinPolicy policy;
    engage (policy);

    const auto d = policy.update (snapshotWith (ClockLockState::Holdover), false);
    CHECK (d.action == JamJoinAction::none);
    CHECK (policy.engaged());

    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, lostStopsButKeepsIntentAndCanRecover)
{
    JamJoinPolicy policy;
    engage (policy);

    const auto lost = policy.update (snapshotWith (ClockLockState::Lost), false);
    CHECK (lost.action == JamJoinAction::stopNow);
    CHECK (! policy.engaged());
    CHECK (! policy.joinPending());
    CHECK (policy.requestedRunning());
    policy.notifyStopAccepted (true);

    // The accepted stop is respected: the join waits for the stopped echo.
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
    policy.notifyPlaybackEcho (false);

    // A recovered lock rejoins without a new Start.
    const auto recovered = policy.update (snapshotWith (ClockLockState::Locked), false);
    CHECK (recovered.action == JamJoinAction::joinAtNextBar);
    CHECK (policy.joinPending());
}

JAM_TEST (jamjoinpolicy, stopNowCancelsJoinAndPersistsUntilStopped)
{
    JamJoinPolicy policy;
    engage (policy);

    policy.notifyStop (JamStopKind::now);
    CHECK (! policy.joinPending()); // future join cancelled immediately

    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopNow);
    policy.notifyStopAccepted (true);

    // A still-playing echo (the engine renders to its stop boundary) must NOT
    // clear the pending stop.
    policy.notifyPlaybackEcho (true);
    CHECK (policy.stopPending());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);

    // The stopped echo resolves it.
    policy.notifyPlaybackEcho (false);
    CHECK (! policy.stopPending());
    CHECK (! policy.engaged());

    // No automatic resume.
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
    CHECK (! policy.joinPending());
}

JAM_TEST (jamjoinpolicy, stopAtNextBarCarriesItsKind)
{
    JamJoinPolicy policy;
    engage (policy);

    policy.notifyStop (JamStopKind::nextBar);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopAtNextBar);
}

JAM_TEST (jamjoinpolicy, rejectedStopIsRetried)
{
    JamJoinPolicy policy;
    engage (policy);

    policy.notifyStop (JamStopKind::now);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopNow);
    policy.notifyStopAccepted (false); // queue full
    CHECK (policy.stopPending());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopNow);
    policy.notifyStopAccepted (true);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, stopBeforeEchoCancelsPendingJoin)
{
    JamJoinPolicy policy;
    policy.notifyStart();
    policy.update (snapshotWith (ClockLockState::Locked), false);
    policy.notifyJoinAccepted (true);
    CHECK (policy.joinPending());

    policy.notifyStop (JamStopKind::now);
    CHECK (! policy.joinPending());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopNow);
}

JAM_TEST (jamjoinpolicy, discontinuityForcesCancelStop)
{
    JamJoinPolicy policy;
    engage (policy);

    const auto d = policy.update (snapshotWith (ClockLockState::Locked), true);
    CHECK (d.action == JamJoinAction::stopNow);
    CHECK (! policy.engaged());
    CHECK (! policy.joinPending());
    CHECK (policy.stopPending());
}

JAM_TEST (jamjoinpolicy, echoFalseDoesNotSpamAJoinBeforeItsBar)
{
    JamJoinPolicy policy;
    policy.notifyStart();
    policy.update (snapshotWith (ClockLockState::Locked), false);
    policy.notifyJoinAccepted (true); // accepted, bar not reached yet

    // The engine is not playing yet: this must not clear the sent join or make
    // update() publish another join every tick.
    policy.notifyPlaybackEcho (false);
    CHECK (policy.joinPending());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, resetForgetsEverythingAndStops)
{
    JamJoinPolicy policy;
    engage (policy);

    policy.notifyReset();
    CHECK (! policy.requestedRunning());
    CHECK (! policy.engaged());
    CHECK (! policy.joinPending());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopNow);

    // After reset only an explicit Start can join.
    policy.notifyStopAccepted (true);
    policy.notifyPlaybackEcho (false);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
    policy.notifyStart();
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::joinAtNextBar);
}

JAM_TEST (jamjoinpolicy, engineDropoutReArmsJoinWhileRunning)
{
    JamJoinPolicy policy;
    engage (policy);

    // The engine dropped out with no stop requested: re-arm the join.
    policy.notifyPlaybackEcho (false);
    CHECK (! policy.engaged());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::joinAtNextBar);
}

//============================================================================
// P2: a persistent Lost must issue exactly ONE accepted stop, not one Clear per
// control tick, and must not re-issue after the stopped echo.
//============================================================================

JAM_TEST (jamjoinpolicy, lostIssuesExactlyOneAcceptedStop)
{
    JamJoinPolicy policy;
    engage (policy);

    int stopActions = 0;
    for (int i = 0; i < 10; ++i)
    {
        const auto d = policy.update (snapshotWith (ClockLockState::Lost), false);
        if (d.action == JamJoinAction::stopNow)
        {
            ++stopActions;
            policy.notifyStopAccepted (true);
        }
    }
    CHECK_EQ (stopActions, 1);
    CHECK (policy.stopPending());
}

JAM_TEST (jamjoinpolicy, lostRetriesBoundedUntilAccepted)
{
    JamJoinPolicy policy;
    engage (policy);

    int attempts = 0;
    for (int i = 0; i < 10; ++i)
    {
        const auto d = policy.update (snapshotWith (ClockLockState::Lost), false);
        if (d.action == JamJoinAction::stopNow)
            ++attempts;
        policy.notifyStopAccepted (false); // queue full
    }
    CHECK_EQ (attempts, 10); // retried every tick while rejected

    const auto accepted = policy.update (snapshotWith (ClockLockState::Lost), false);
    CHECK (accepted.action == JamJoinAction::stopNow);
    policy.notifyStopAccepted (true);
    CHECK (policy.update (snapshotWith (ClockLockState::Lost), false).action
           == JamJoinAction::none);
    CHECK (policy.stopPending());
}

JAM_TEST (jamjoinpolicy, lostAwaitingEchoDoesNotReClearAndAfterAckNoMore)
{
    JamJoinPolicy policy;
    engage (policy);

    CHECK (policy.update (snapshotWith (ClockLockState::Lost), false).action
           == JamJoinAction::stopNow);
    policy.notifyStopAccepted (true);

    // Still playing: the stop waits, no re-clear.
    for (int i = 0; i < 10; ++i)
    {
        policy.notifyPlaybackEcho (true);
        CHECK (policy.update (snapshotWith (ClockLockState::Lost), false).action
               == JamJoinAction::none);
    }

    policy.notifyPlaybackEcho (false); // stopped ack
    CHECK (! policy.stopPending());

    // Still Lost: no new Clear and no Join.
    for (int i = 0; i < 20; ++i)
        CHECK (policy.update (snapshotWith (ClockLockState::Lost), false).action
               == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, lostIdleIssuesNothing)
{
    JamJoinPolicy policy;
    policy.notifyStart(); // running, but never joined (Acquiring then Lost)

    for (int i = 0; i < 10; ++i)
        CHECK (policy.update (snapshotWith (ClockLockState::Lost), false).action
               == JamJoinAction::none);
    CHECK (! policy.stopPending());
}

JAM_TEST (jamjoinpolicy, recoveredLockedJoinsOnceAfterStoppedEcho)
{
    JamJoinPolicy policy;
    engage (policy);

    CHECK (policy.update (snapshotWith (ClockLockState::Lost), false).action
           == JamJoinAction::stopNow);
    policy.notifyStopAccepted (true);
    policy.notifyPlaybackEcho (false);

    const auto d = policy.update (snapshotWith (ClockLockState::Locked), false);
    CHECK (d.action == JamJoinAction::joinAtNextBar);
    policy.notifyJoinAccepted (true);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none); // exactly one join
    policy.notifyPlaybackEcho (true);
    CHECK (policy.engaged());
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

//============================================================================
// P3: stop-kind upgrade. StopNow overrides an accepted bar stop; StopNow is
// never downgraded; Start respects an accepted stop.
//============================================================================

JAM_TEST (jamjoinpolicy, stopNowUpgradesAcceptedBarStop)
{
    JamJoinPolicy policy;
    engage (policy);

    policy.notifyStop (JamStopKind::nextBar);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopAtNextBar);
    policy.notifyStopAccepted (true); // bar stop accepted

    // StopNow must still publish an immediate cancel/Clear.
    policy.notifyStop (JamStopKind::now);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopNow);
    policy.notifyStopAccepted (true);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, stopAtNextBarNeverDowngradesStopNow)
{
    JamJoinPolicy policy;
    engage (policy);

    policy.notifyStop (JamStopKind::now);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopNow);
    policy.notifyStopAccepted (true);

    policy.notifyStop (JamStopKind::nextBar); // must not downgrade or emit
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, startRespectsAcceptedStopThenJoinsAfterEcho)
{
    JamJoinPolicy policy;
    engage (policy);

    policy.notifyStop (JamStopKind::nextBar);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::stopAtNextBar);
    policy.notifyStopAccepted (true);

    // Start cannot retroactively cancel an accepted stop: it waits.
    policy.notifyStart();
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false).action
           == JamJoinAction::none);

    policy.notifyPlaybackEcho (false); // the bar stop fired
    const auto d = policy.update (snapshotWith (ClockLockState::Locked), false);
    CHECK (d.action == JamJoinAction::joinAtNextBar);
}
