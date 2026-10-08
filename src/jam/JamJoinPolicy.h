// Minimal Jam join / follow policy for the first audible live slice.
//
// INT-LIVE-001. This is deliberately NOT the full Jam Director (DEVPLAN 19):
// that owns style, intensity, fills and pattern selection. The live slice needs
// only the one decision the contract names — "the minimal director joins only
// from a usable clock lock, holds through holdover, and stops safely on
// loss/reset" — expressed as a pure, deterministic state machine so it can be
// driven without threads, audio or timers.
//
// INTENT / SENT / ECHO-ACK ARE SEPARATE
//   A join is only "sent" once the bridge accepted the command (queue space),
//   and only "engaged" once the audio owner echoes real playback. A rejected
//   command stays wanted and is retried; it is never latched as pending-forever.
//   A stop wanted persists until the audio owner echoes that it actually
//   stopped — a still-playing echo does NOT clear it.
//
// ONE ACCEPTED STOP ACROSS A PERSISTENT LOSS
//   A sustained Lost clock must NOT re-issue a Clear every control tick. The
//   stop intent/sent state is preserved across Lost: exactly one accepted stop
//   is issued (retried only while the bridge rejects it) until the stopped echo
//   arrives. After that, while still Lost, no new Clear and no Join is issued;
//   the running intent may recover only once the clock is Locked again. An idle
//   Lost (no engagement, no pending join, no wanted stop) issues nothing.
//
// STOP-KIND UPGRADE
//   StopNextBar -> StopNow (or Reset) MUST publish an immediate bounded
//   cancel/Clear even when a bar stop was already accepted: the engine will
//   apply the Clear after the queued bar stop and cancel its delayed event.
//   StopNow -> StopAtNextBar is never downgraded. A Start during an accepted
//   stop is an intent to re-arm: the accepted stop is respected and the join is
//   re-armed only after the stopped echo.
//
// THREADING: pure data, owned and called by the single control worker (or by a
// deterministic test). No allocation, no locks, no time source.

#pragma once

#include "RhythmTypes.h"

#include <cstdint>

namespace jam
{

/** Which stop the user asked for. `now` is the bounded next-serviced-block stop
    (the bridge publishes a cancel/clear); `nextBar` is the musical next-bar
    stop. */
enum class JamStopKind : int
{
    none = 0,
    now,      // JamLiveCommandType::Stop / Reset / Loss
    nextBar   // JamLiveCommandType::StopAtNextBar
};

/** One action the session must attempt through the bridge this tick. The session
    reports the bridge's accept/reject back through notifyJoinAccepted() /
    notifyStopAccepted(). */
enum class JamJoinAction : int
{
    none = 0,
    joinAtNextBar,   // bridge.requestJoinAtNextBar(groove)
    stopNow,         // bridge.requestStopNow()
    stopAtNextBar    // bridge.requestStopAtNextBar()
};

inline const char* toString (JamJoinAction a) noexcept
{
    switch (a)
    {
        case JamJoinAction::none:          return "none";
        case JamJoinAction::joinAtNextBar: return "joinAtNextBar";
        case JamJoinAction::stopNow:       return "stopNow";
        case JamJoinAction::stopAtNextBar: return "stopAtNextBar";
    }
    return "unknown";
}

struct JamJoinPolicyConfig
{
    // Loss (clock Lost) safely stops the grid. The intent stays running, so a
    // freshly re-locked clock can rejoin.
    bool stopOnLost = true;

    // A discontinuity forces a cancel stop and clears the engagement.
    bool stopOnDiscontinuity = true;
};

struct JamJoinDecision
{
    JamJoinAction action = JamJoinAction::none;
};

/** Deterministic join/holdover/loss policy. */
class JamJoinPolicy
{
public:
    explicit JamJoinPolicy (const JamJoinPolicyConfig& config = {}) noexcept;

    /** Forget all state. Called on prepare. */
    void reset() noexcept;

    // --- accepted user intent (one control owner) ---------------------------

    /** Start: run, joining once a usable lock exists. It abandons only an
        UNLANDED stop (wanted but not yet accepted); an accepted stop is
        respected and the join waits for the stopped echo. */
    void notifyStart() noexcept;

    /** Stop: requestedRunning=false and a bounded stop is wanted. A `now` stop
        upgrades (and overrides) a pending/accepted `nextBar` stop; a `nextBar`
        stop never downgrades an immediate one. Any future join is cancelled. */
    void notifyStop (JamStopKind kind) noexcept;

    /** Reset: forget the belief, cancel everything, force an immediate stop that
        must be published even if the engine is already silent (to flush). */
    void notifyReset() noexcept;

    // --- per-tick decision --------------------------------------------------

    /** Advance the policy one tick.
        @param clock          current stable clock belief
        @param discontinuity  true when this tick saw a grid discontinuity
        @returns the single action to attempt this tick */
    JamJoinDecision update (const ClockSnapshot& clock, bool discontinuity) noexcept;

    // --- bridge accept/reject feedback -------------------------------------

    void notifyJoinAccepted (bool accepted) noexcept;
    void notifyStopAccepted (bool accepted) noexcept;

    /** Audio-owner echo. A playing echo confirms an outstanding join; it never
        clears a pending stop. A stopped echo confirms a pending stop. */
    void notifyPlaybackEcho (bool playing) noexcept;

    // --- observers ----------------------------------------------------------

    bool requestedRunning() const noexcept { return requestedRunning_; }
    /** Join wanted or sent, but not yet confirmed playing. */
    bool joinPending() const noexcept { return joinWanted_ || joinSent_; }
    bool joinSent() const noexcept { return joinSent_; }
    bool engaged() const noexcept { return engaged_; }
    /** Stop wanted or sent, not yet confirmed stopped. */
    bool stopPending() const noexcept { return stopWanted_ || stopSent_; }

private:
    JamJoinPolicyConfig config_;

    bool requestedRunning_ = false;
    bool joinWanted_ = false;    // we want to play; retried until accepted
    bool joinSent_ = false;      // bridge accepted a join; awaiting echo
    bool engaged_ = false;       // echo confirmed playing
    bool stopWanted_ = false;    // we want to stop; retried until accepted
    bool stopSent_ = false;      // bridge accepted a stop; awaiting echo stop
    JamStopKind stopKind_ = JamStopKind::none;
    bool stopForced_ = false;    // Reset: emit even if the engine is silent
    bool stopCancelNeeded_ = false; // an engine join/engagement must be cancelled
    bool echoPlaying_ = false;   // last audio-owner echo
};

} // namespace jam
