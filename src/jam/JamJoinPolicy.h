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
//   join stays wanted and is retried on the next tick; it is never latched as
//   pending-forever. A stop wanted persists until the audio owner echoes that
//   it actually stopped — a still-playing echo does NOT clear it, so a stop
//   cannot be silently dropped.
//
// WHAT IT DOES
//   - a join is requested only from a `Locked` clock;
//   - `Holdover` HOLDS: the drummer keeps the last anchored grid;
//   - `Lost` stops the grid safely (cancelling any future join) and keeps the
//     running intent, so a re-locked clock rejoins (loss recovery, not an
//     automatic resume after a UI Stop);
//   - an explicit UI Stop (`stopNow`) or StopAtNextBar commits a bounded stop;
//     a later Start is a new intent, never an automatic resume;
//   - a discontinuity clears the engagement and forces a cancel stop.
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

    /** Forget all state. Called on prepare / explicit Reset. */
    void reset() noexcept;

    // --- accepted user intent (one control owner) ---------------------------

    /** Start: run, joining once a usable lock exists. Cancels an unlanded stop
        intent, but never reactivates a join cancelled by a prior Stop. */
    void notifyStart() noexcept;

    /** Stop: requestedRunning=false and a bounded stop is wanted. Any future
        join is cancelled immediately. */
    void notifyStop (JamStopKind kind) noexcept;

    /** Reset: forget the belief, cancel everything, force an immediate stop. */
    void notifyReset() noexcept;

    // --- per-tick decision --------------------------------------------------

    /** Advance the policy one tick.
        @param clock          current stable clock belief
        @param discontinuity  true when this tick saw a grid discontinuity
        @returns the single action to attempt this tick */
    JamJoinDecision update (const ClockSnapshot& clock, bool discontinuity) noexcept;

    // --- bridge accept/reject feedback -------------------------------------

    /** The bridge accepted (true) or rejected (false) the join command. A
        rejected join remains wanted and is retried next tick. */
    void notifyJoinAccepted (bool accepted) noexcept;

    /** The bridge accepted (true) or rejected (false) the stop command. A
        rejected stop remains wanted and is retried next tick. */
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
};

} // namespace jam
