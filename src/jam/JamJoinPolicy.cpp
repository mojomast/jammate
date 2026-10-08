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
    stopForced_ = false;
    stopCancelNeeded_ = false;
    echoPlaying_ = false;
}

void JamJoinPolicy::notifyStart() noexcept
{
    requestedRunning_ = true;

    // A Start abandons an UNLANDED stop (wanted but not accepted). An accepted
    // stop is respected: it cannot be retroactively cancelled without an engine
    // API, so the join waits for the stopped echo and re-arms then.
    if (! stopSent_)
    {
        stopWanted_ = false;
        stopForced_ = false;
        stopKind_ = JamStopKind::none;
        stopCancelNeeded_ = false;
    }
}

void JamJoinPolicy::notifyStop (JamStopKind kind) noexcept
{
    requestedRunning_ = false;

    // Cancel any future join immediately (Stop/Reset/Lost must not leave a
    // queued join that could resurrect playback). Remember that an accepted
    // join/engagement may still be in the engine so a cancel Clear is emitted
    // even when the audio echo has not started yet.
    stopCancelNeeded_ = stopCancelNeeded_ || joinSent_ || joinWanted_ || engaged_;
    joinWanted_ = false;
    joinSent_ = false;

    const bool immediate = (kind == JamStopKind::now);

    if (! stopWanted_)
    {
        stopWanted_ = true;
        stopKind_ = kind;
        stopSent_ = false;
        return;
    }

    if (immediate && stopKind_ == JamStopKind::nextBar)
    {
        // Upgrade: an immediate stop must publish a bounded cancel/Clear even if
        // the bar stop was already accepted, so the engine's delayed bar stop is
        // invalidated. Never downgrade an immediate stop.
        stopKind_ = JamStopKind::now;
        stopSent_ = false;
        return;
    }

    if (! immediate && stopKind_ == JamStopKind::now)
    {
        // StopNow -> StopAtNextBar: no extra event, no downgrade.
        return;
    }

    // Same kind while already wanted/sent: coalesce.
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
    stopForced_ = true;          // flush even if the engine is already silent
    stopCancelNeeded_ = true;
}

void JamJoinPolicy::notifyJoinAccepted (bool accepted) noexcept
{
    if (accepted)
        joinSent_ = true;
}

void JamJoinPolicy::notifyStopAccepted (bool accepted) noexcept
{
    if (accepted)
    {
        stopSent_ = true;
        stopForced_ = false;
        stopCancelNeeded_ = false; // the cancel is in flight; wait for the echo
    }
}

void JamJoinPolicy::notifyPlaybackEcho (bool playing) noexcept
{
    echoPlaying_ = playing;

    if (playing)
    {
        // Real playback confirms an outstanding join. It must NOT clear a
        // pending stop: the engine can still be rendering the final bar of a
        // next-bar stop.
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
        if (stopWanted_ || stopSent_)
        {
            stopWanted_ = false;
            stopSent_ = false;
            stopForced_ = false;
            stopCancelNeeded_ = false;
        }

        // The engine dropped out under us without a stop: re-arm a join if the
        // running intent survives.
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
        if (! stopSent_)
        {
            stopWanted_ = true;
            stopKind_ = JamStopKind::now;
            stopForced_ = false;
            if (config_.stopOnDiscontinuity)
                decision.action = JamJoinAction::stopNow;
        }
        // If a stop is already accepted, the bridge already published a Clear
        // for this discontinuity; do not issue a second one.
        return decision;
    }

    if (! requestedRunning_)
    {
        if (stopWanted_ && ! stopSent_)
        {
            // Do not emit a redundant Clear when the engine is already silent and
            // there is nothing to cancel; a Reset forces it (to flush).
            const bool needed = stopForced_ || stopCancelNeeded_ || echoPlaying_
                                || engaged_ || joinWanted_ || joinSent_;
            if (! needed)
            {
                stopWanted_ = false;
                return decision;
            }
            decision.action = (stopKind_ == JamStopKind::nextBar)
                                  ? JamJoinAction::stopAtNextBar
                                  : JamJoinAction::stopNow;
        }
        return decision;
    }

    // Running intent.

    // An accepted stop is respected: wait for the stopped echo before rejoining.
    if (stopSent_)
        return decision;

    // A wanted-but-unaccepted stop (e.g. a Lost stop whose request was rejected)
    // is retried until the bridge accepts it.
    if (stopWanted_)
    {
        decision.action = (stopKind_ == JamStopKind::nextBar)
                              ? JamJoinAction::stopAtNextBar
                              : JamJoinAction::stopNow;
        return decision;
    }

    if (clock.lockState == ClockLockState::Lost)
    {
        // Only stop if something is actually engaged/playing/joining. An idle
        // Lost issues nothing, so a persistent Lost cannot spam the queue.
        const bool active = engaged_ || echoPlaying_ || joinWanted_ || joinSent_;
        joinWanted_ = false;
        joinSent_ = false;
        engaged_ = false;
        if (active && config_.stopOnLost)
        {
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
