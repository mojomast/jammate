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
    joinWanted_ = false;
    joinSent_ = false;
    engaged_ = false;
    stopWanted_ = false;
    stopSent_ = false;
    stopKind_ = JamStopKind::none;
}

void JamJoinPolicy::notifyStart() noexcept
{
    requestedRunning_ = true;
    // A Start is a fresh intent: an unlanded stop is abandoned. It does NOT
    // revive a join that a prior Stop cancelled — the join is re-armed only when
    // update() next sees a usable Locked clock.
    stopWanted_ = false;
    stopSent_ = false;
    stopKind_ = JamStopKind::none;
}

void JamJoinPolicy::notifyStop (JamStopKind kind) noexcept
{
    requestedRunning_ = false;
    stopKind_ = kind;
    // Cancel any future join immediately (Stop/Reset/Lost must not leave a
    // queued join that could resurrect playback).
    joinWanted_ = false;
    joinSent_ = false;
    stopWanted_ = true;
    // A previous stop may already be in flight; keep stopSent_ so we do not
    // publish a second stop command until the first is resolved.
}

void JamJoinPolicy::notifyReset() noexcept
{
    requestedRunning_ = false;
    joinWanted_ = false;
    joinSent_ = false;
    engaged_ = false;
    stopWanted_ = true;
    stopSent_ = false;
    stopKind_ = JamStopKind::now;
}

void JamJoinPolicy::notifyJoinAccepted (bool accepted) noexcept
{
    if (accepted)
        joinSent_ = true;
    // On rejection, joinWanted_ stays true and update() retries next tick.
}

void JamJoinPolicy::notifyStopAccepted (bool accepted) noexcept
{
    if (accepted)
        stopSent_ = true;
    // On rejection, stopWanted_ stays true and update() retries next tick.
}

void JamJoinPolicy::notifyPlaybackEcho (bool playing) noexcept
{
    if (playing)
    {
        // Real playback confirms an outstanding join. It must NOT clear a
        // pending stop: the engine can still be rendering the final bar of a
        // next-bar stop, and clearing here would drop the stop forever.
        if (joinWanted_ || joinSent_)
        {
            joinWanted_ = false;
            joinSent_ = false;
            engaged_ = true;
        }
    }
    else
    {
        // The engine really stopped: resolve a pending stop.
        if (stopSent_ || stopWanted_)
        {
            stopSent_ = false;
            stopWanted_ = false;
        }
        // The engine dropped out under us without a stop: re-arm a join if the
        // running intent survives, so recovery is automatic.
        if (engaged_)
        {
            engaged_ = false;
            if (requestedRunning_)
                joinWanted_ = true;
        }
    }
}

JamJoinDecision JamJoinPolicy::update (const ClockSnapshot& clock, bool discontinuity) noexcept
{
    JamJoinDecision decision;

    if (discontinuity)
    {
        joinWanted_ = false;
        joinSent_ = false;
        engaged_ = false;
        stopSent_ = false;
        stopWanted_ = true;
        stopKind_ = JamStopKind::now;
        if (config_.stopOnDiscontinuity)
            decision.action = JamJoinAction::stopNow;
        return decision;
    }

    if (! requestedRunning_)
    {
        // Commit a wanted stop, retrying until the bridge accepts it. Once
        // accepted (stopSent_) wait for the stopped echo.
        if (stopWanted_ && ! stopSent_)
            decision.action = (stopKind_ == JamStopKind::nextBar)
                                  ? JamJoinAction::stopAtNextBar
                                  : JamJoinAction::stopNow;
        return decision;
    }

    // Running intent.
    if (stopWanted_ || stopSent_)
    {
        // A Start cancelled an unlanded stop; the engine is about to be rejoined.
        stopWanted_ = false;
        stopSent_ = false;
        stopKind_ = JamStopKind::none;
    }

    if (clock.lockState == ClockLockState::Lost)
    {
        if (config_.stopOnLost)
        {
            joinWanted_ = false;
            joinSent_ = false;
            engaged_ = false;
            stopSent_ = false;
            stopWanted_ = true;
            stopKind_ = JamStopKind::now;
            decision.action = JamJoinAction::stopNow;
        }
        return decision;
    }

    // Holdover HOLDS: keep the anchored grid, no action.
    if (clock.lockState == ClockLockState::Holdover)
        return decision;

    // Locked: want a join, retry until the bridge accepts it.
    if (clock.lockState == ClockLockState::Locked && ! engaged_)
    {
        joinWanted_ = true;
        if (! joinSent_)
            decision.action = JamJoinAction::joinAtNextBar;
    }

    return decision;
}

} // namespace jam
