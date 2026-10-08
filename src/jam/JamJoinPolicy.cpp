#include "JamJoinPolicy.h"

namespace jam
{

JamJoinPolicy::JamJoinPolicy (const JamJoinPolicyConfig& config) noexcept
    : config_ (config)
{
}

void JamJoinPolicy::reset() noexcept
{
    requestedRunning_ = false;
    engaged_ = false;
    joinPending_ = false;
    stopPending_ = false;
}

JamJoinDecision JamJoinPolicy::update (const ClockSnapshot& clock,
                                       bool requestedRunning,
                                       bool discontinuity) noexcept
{
    JamJoinDecision decision;

    // A discontinuity invalidates the grid the engine was rendering. Drop the
    // engaged state so a later Start/lock cannot be mistaken for a live join.
    if (discontinuity)
    {
        engaged_ = false;
        joinPending_ = false;
        stopPending_ = false;
        requestedRunning_ = requestedRunning;
        if (config_.stopOnDiscontinuity)
            decision.action = JamJoinAction::clearNow;
        return decision;
    }

    // Explicit Stop (or StopAtNextBar): commit a bounded stop and clear the
    // engaged state. The intent is recorded so a later Start is a fresh join.
    if (! requestedRunning)
    {
        if (engaged_ || joinPending_)
        {
            if (! stopPending_)
            {
                decision.action = JamJoinAction::stopAtNextBar;
                stopPending_ = true;
            }
            engaged_ = false;
            joinPending_ = false;
        }
        requestedRunning_ = false;
        return decision;
    }

    // Start requested. A Start that cancels an unlanded Stop is a fresh intent:
    // drop the pending stop and let the lock gate the rejoin below.
    if (stopPending_)
    {
        // The engine may already be stopping at the next bar; a new join
        // command supersedes it inside the bridge (requestJoinAtNextBar clears a
        // pending stop), so no extra action is issued here.
        stopPending_ = false;
    }

    const ClockLockState lock = clock.lockState;

    if (lock == ClockLockState::Lost)
    {
        // Loss: stop safely, but keep the running intent so a recovered lock can
        // rejoin. Only an explicit UI Stop clears the intent.
        const bool wasEngaged = engaged_ || joinPending_;
        engaged_ = false;
        joinPending_ = false;
        requestedRunning_ = true;
        if (wasEngaged && config_.stopOnLost)
            decision.action = JamJoinAction::stopAtNextBar;
        return decision;
    }

    // Holdover HOLDS: the drummer is anchored on the last grid; do nothing.
    if (lock == ClockLockState::Holdover)
    {
        requestedRunning_ = true;
        return decision;
    }

    // Locked and not engaged: request exactly one join at the next bar. The
    // Acquiring case falls through with no action (wait for a usable lock).
    if (lock == ClockLockState::Locked && ! engaged_ && ! joinPending_)
    {
        decision.action = JamJoinAction::joinAtNextBar;
        joinPending_ = true;
    }

    requestedRunning_ = true;
    return decision;
}

void JamJoinPolicy::notifyPlaybackEcho (bool playing) noexcept
{
    if (playing)
    {
        // The engine is really rendering: the join has landed.
        if (joinPending_)
        {
            joinPending_ = false;
            engaged_ = true;
        }
        stopPending_ = false;
    }
    else
    {
        // The engine is silent: a committed stop has landed, and an engaged flag
        // without a pending join can only mean the engine dropped out under us.
        if (stopPending_)
            stopPending_ = false;
        if (engaged_ && ! joinPending_)
            engaged_ = false;
    }
}

} // namespace jam
