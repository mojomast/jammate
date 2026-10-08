// Minimal Jam join / follow policy for the first audible live slice.
//
// INT-LIVE-001. This is deliberately NOT the full Jam Director (DEVPLAN 19):
// that owns style, intensity, fills and pattern selection. The live slice needs
// only the one decision the contract names — "the minimal director joins only
// from a usable clock lock, holds through holdover, and stops safely on
// loss/reset" — expressed as a pure, deterministic state machine so it can be
// driven without threads, audio or timers.
//
// WHAT IT DOES
//   - Maps the accepted Start/Stop intent plus the current ClockSnapshot to one
//     of a small set of actions the session applies through the MusicClock and
//     the DrumClockBridge. It never sets a drum tempo, never touches the audio
//     callback and never reads engine state.
//   - Join happens once, from a Locked clock, and is "pending" until the audio
//     owner echoes that the engine actually started. This is what lets
//     JamLiveState distinguish requestedRunning / joinPending / drumsPlaying
//     instead of claiming sound from a scheduled command.
//   - Holdover HOLDS: the drummer keeps playing on the last anchored grid
//     (explicit holdover policy). Only Loss or an explicit discontinuity stops
//     the grid.
//   - Discontinuity (device re-prepare, backwards cursor, tracker reset) clears
//     the engaged state and asks for a safe stop; it never auto-resumes.
//   - An explicit UI Stop sets requestedRunning=false; a later Start is a new
//     intent, never an automatic resume.
//
// THREADING: pure data, owned and called by the single control worker (or by a
// deterministic test). No allocation, no locks, no time source.

#pragma once

#include "RhythmTypes.h"

#include <cstdint>

namespace jam
{

/** One action the session must apply for the current tick. */
enum class JamJoinAction : int
{
    none = 0,
    joinAtNextBar,   // bridge.requestJoinAtNextBar(groove)
    stopAtNextBar,   // bridge.requestStopAtNextBar()
    clearNow         // a discontinuity already cleared the grid; stop is enough
};

inline const char* toString (JamJoinAction a) noexcept
{
    switch (a)
    {
        case JamJoinAction::none:          return "none";
        case JamJoinAction::joinAtNextBar: return "joinAtNextBar";
        case JamJoinAction::stopAtNextBar: return "stopAtNextBar";
        case JamJoinAction::clearNow:      return "clearNow";
    }
    return "unknown";
}

struct JamJoinPolicyConfig
{
    // A join is only ever requested from a Locked clock. Acquiring/Holdover are
    // not usable for a first join; Holdover keeps an already-engaged grid.
    bool joinOnlyWhenLocked = true;

    // Loss (clock Lost) safely stops the grid. The intent stays running, so a
    // freshly re-locked clock can rejoin without the user pressing Start again;
    // this is loss recovery, not an automatic resume after a UI Stop.
    bool stopOnLost = true;

    // A discontinuity (device re-prepare, cursor backwards, tracker reset)
    // clears the engaged state and asks for a safe stop.
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

    /** Advance the policy one tick.
        @param clock             current stable clock belief
        @param requestedRunning  accepted UI intent (Start=true / Stop=false)
        @param discontinuity     true when this tick saw a grid discontinuity
        @returns the single action to apply this tick */
    JamJoinDecision update (const ClockSnapshot& clock,
                            bool requestedRunning,
                            bool discontinuity) noexcept;

    /** Audio-owner echo: the engine really is (not) rendering. Confirms or
        abandons a pending join. Called every tick. */
    void notifyPlaybackEcho (bool playing) noexcept;

    bool requestedRunning() const noexcept { return requestedRunning_; }
    bool joinPending() const noexcept { return joinPending_; }
    bool engaged() const noexcept { return engaged_; }
    bool stopPending() const noexcept { return stopPending_; }

private:
    JamJoinPolicyConfig config_;

    bool requestedRunning_ = false;
    bool engaged_ = false;      // a join has been requested and not yet stopped
    bool joinPending_ = false;  // join requested, audio echo not seen yet
    bool stopPending_ = false;  // stop requested, echo not confirmed stopped
};

} // namespace jam
