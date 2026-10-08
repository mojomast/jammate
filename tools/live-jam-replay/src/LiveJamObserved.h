// EVAL-LIVE-001 observed-backend capture helper.
//
// Pure, processor-free: it reads the actual backend tag from a real
// `jam::JamLiveState` and tracks first-vs-last within one prepared session,
// independent of any join or playback outcome. Used by the replay scenarios so
// a default run that legitimately never locks still records its real backend
// identity, and so a backend tag that changes mid-session is a fail-closed
// discrepancy rather than being silently overwritten.
#pragma once

#include "jam/JamLiveInterface.h"

namespace replay
{

inline const char* liveBackendName (jam::JamLiveBackend b) noexcept
{
    switch (b)
    {
        case jam::JamLiveBackend::unavailable:        return "unavailable";
        case jam::JamLiveBackend::experimentalBTrack: return "experimentalBTrack";
        case jam::JamLiveBackend::injectedTest:       return "injectedTest";
    }
    return "unknown";
}

struct BackendObservation
{
    bool observed = false;
    jam::JamLiveBackend first = jam::JamLiveBackend::unavailable;
    jam::JamLiveBackend last = jam::JamLiveBackend::unavailable;
    bool changed = false;

    void observe (const jam::JamLiveState& s) noexcept
    {
        if (! s.prepared) return;              // only coherent prepared states count
        if (! observed)
        {
            first = s.backend;
            observed = true;
        }
        last = s.backend;
        if (last != first) changed = true;
    }

    const char* label() const noexcept
    {
        return observed ? liveBackendName (first) : "unknown";
    }
};

// Coherent latest-value latch (Defect A). readJamLiveState returns false when
// there is no new publication; the latch retains the last valid state instead of
// exposing a default zero. A bounded prepared/released poll is used before the
// callback and on shutdown.
struct StateLatch
{
    jam::JamLiveState last {};
    bool have = false;

    bool updateFrom (bool ok, const jam::JamLiveState& s) noexcept
    {
        if (ok)
        {
            last = s;
            have = true;
        }
        return ok;
    }

    template <typename Reader, typename SleepFn>
    bool pollPrepared (Reader&& reader, int maxAttempts, SleepFn&& sleepFn) noexcept
    {
        for (int i = 0; i < maxAttempts; ++i)
        {
            jam::JamLiveState s {};
            if (reader (s) && s.prepared && s.sampleRate > 0.0)
            {
                last = s;
                have = true;
                return true;
            }
            if (i + 1 < maxAttempts) sleepFn();
        }
        return false;
    }

    template <typename Reader, typename SleepFn>
    bool pollReleased (Reader&& reader, int maxAttempts, SleepFn&& sleepFn) noexcept
    {
        for (int i = 0; i < maxAttempts; ++i)
        {
            jam::JamLiveState s {};
            if (reader (s) && ! s.prepared && ! s.drumsPlaying)
            {
                last = s;
                have = true;
                return true;
            }
            if (i + 1 < maxAttempts) sleepFn();
        }
        return false;
    }
};

} // namespace replay
