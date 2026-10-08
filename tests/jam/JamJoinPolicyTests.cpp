// Unit tests for jam::JamJoinPolicy — the minimal live-slice director
// (INT-LIVE-001, LIVE-JAM-CONTRACT.md).
//
// The policy is a pure state machine: no threads, no time source, no audio.
// These tests pin the contract the session depends on:
//   - one join, only from a Locked clock;
//   - join is pending until the audio-owner echo confirms it;
//   - Holdover HOLDS (the drummer keeps its anchored grid);
//   - Loss stops safely but keeps the running intent so a recovered lock can
//     rejoin;
//   - an explicit UI Stop stops and never auto-resumes;
//   - a discontinuity clears the engagement.

#include "JamTest.h"

#include "jam/JamJoinPolicy.h"

#include <cstdint>

using jam::ClockLockState;
using jam::ClockSnapshot;
using jam::JamJoinAction;
using jam::JamJoinPolicy;

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

} // namespace

JAM_TEST (jamjoinpolicy, noJoinWhileAcquiring)
{
    JamJoinPolicy policy;
    const auto d = policy.update (snapshotWith (ClockLockState::Acquiring), true, false);
    CHECK (d.action == JamJoinAction::none);
    CHECK (! policy.joinPending());
    CHECK (! policy.engaged());
}

JAM_TEST (jamjoinpolicy, joinFromLockedOnlyOnce)
{
    JamJoinPolicy policy;

    const auto d1 = policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (d1.action == JamJoinAction::joinAtNextBar);
    CHECK (policy.joinPending());
    CHECK (! policy.engaged());

    // Still pending: a second tick must not spam another join.
    const auto d2 = policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (d2.action == JamJoinAction::none);
    CHECK (policy.joinPending());

    // The audio echo confirms real playback.
    policy.notifyPlaybackEcho (true);
    CHECK (! policy.joinPending());
    CHECK (policy.engaged());

    const auto d3 = policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (d3.action == JamJoinAction::none);
    CHECK (policy.engaged());
}

JAM_TEST (jamjoinpolicy, holdoverHoldsEngagedGrid)
{
    JamJoinPolicy policy;
    policy.update (snapshotWith (ClockLockState::Locked), true, false);
    policy.notifyPlaybackEcho (true);
    CHECK (policy.engaged());

    // Holdover: the grid is anchored on the last belief; keep playing.
    const auto d = policy.update (snapshotWith (ClockLockState::Holdover), true, false);
    CHECK (d.action == JamJoinAction::none);
    CHECK (policy.engaged());
    CHECK (! policy.joinPending());

    // A fresh usable lock while still engaged must not re-join.
    const auto d2 = policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (d2.action == JamJoinAction::none);
}

JAM_TEST (jamjoinpolicy, lostStopsButKeepsIntentAndCanRecover)
{
    JamJoinPolicy policy;
    policy.update (snapshotWith (ClockLockState::Locked), true, false);
    policy.notifyPlaybackEcho (true);
    CHECK (policy.engaged());

    const auto lost = policy.update (snapshotWith (ClockLockState::Lost), true, false);
    CHECK (lost.action == JamJoinAction::stopAtNextBar);
    CHECK (! policy.engaged());
    CHECK (! policy.joinPending());
    CHECK (policy.requestedRunning());

    // A recovered lock rejoins without a new Start, because the intent survived.
    const auto recovered = policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (recovered.action == JamJoinAction::joinAtNextBar);
    CHECK (policy.joinPending());
}

JAM_TEST (jamjoinpolicy, stopClearsAndDoesNotAutoResume)
{
    JamJoinPolicy policy;
    policy.update (snapshotWith (ClockLockState::Locked), true, false);
    policy.notifyPlaybackEcho (true);

    const auto stop = policy.update (snapshotWith (ClockLockState::Locked), false, false);
    CHECK (stop.action == JamJoinAction::stopAtNextBar);
    CHECK (! policy.engaged());
    CHECK (! policy.requestedRunning());

    // Still stopped on later ticks, even with a perfect lock and an echo.
    policy.notifyPlaybackEcho (false);
    const auto later = policy.update (snapshotWith (ClockLockState::Locked), false, false);
    CHECK (later.action == JamJoinAction::none);
    CHECK (! policy.engaged());
    CHECK (! policy.joinPending());
}

JAM_TEST (jamjoinpolicy, stopBeforeEchoCancelsPendingJoin)
{
    JamJoinPolicy policy;
    policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (policy.joinPending());

    const auto stop = policy.update (snapshotWith (ClockLockState::Locked), false, false);
    CHECK (stop.action == JamJoinAction::stopAtNextBar);
    CHECK (! policy.joinPending());
    CHECK (! policy.engaged());
}

JAM_TEST (jamjoinpolicy, discontinuityClearsAndFailsClosed)
{
    JamJoinPolicy policy;
    policy.update (snapshotWith (ClockLockState::Locked), true, false);
    policy.notifyPlaybackEcho (true);

    const auto d = policy.update (snapshotWith (ClockLockState::Locked), true, true);
    CHECK (d.action == JamJoinAction::clearNow);
    CHECK (! policy.engaged());
    CHECK (! policy.joinPending());

    // Next tick with a clean Locked clock rejoins, because the intent survived.
    const auto again = policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (again.action == JamJoinAction::joinAtNextBar);
}

JAM_TEST (jamjoinpolicy, echoFalseAbandonsEngagedBeforeRejoin)
{
    JamJoinPolicy policy;
    policy.update (snapshotWith (ClockLockState::Locked), true, false);
    policy.notifyPlaybackEcho (true);
    CHECK (policy.engaged());

    policy.notifyPlaybackEcho (false);
    CHECK (! policy.engaged());

    const auto d = policy.update (snapshotWith (ClockLockState::Locked), true, false);
    CHECK (d.action == JamJoinAction::joinAtNextBar);
}

JAM_TEST (jamjoinpolicy, resetForgetsEverything)
{
    JamJoinPolicy policy;
    policy.update (snapshotWith (ClockLockState::Locked), true, false);
    policy.notifyPlaybackEcho (true);
    policy.reset();

    CHECK (! policy.requestedRunning());
    CHECK (! policy.engaged());
    CHECK (! policy.joinPending());

    // After reset, only an explicit Start can join.
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), false, false).action
           == JamJoinAction::none);
    CHECK (policy.update (snapshotWith (ClockLockState::Locked), true, false).action
           == JamJoinAction::joinAtNextBar);
}
