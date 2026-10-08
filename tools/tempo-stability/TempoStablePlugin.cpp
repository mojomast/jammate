// TempoStablePlugin — dlopen()-able candidate backend (TRACK-008).
//
// Plugin convention (same as the pinned default/variant plugins):
//
//   extern "C" jam::IRhythmTracker* jam_rhythm_create();
//   extern "C" void               jam_rhythm_destroy(jam::IRhythmTracker*);
//
// It composes the real, unmodified jam::BTrackBackend (linked from the pinned
// EVAL-005 main-core static archives: libjam-btrack.a + libbtrack.a +
// libsamplerate.a + libkiss_fft.a) inside the TempoStableTracker decorator, so
// the SAME diagnostic/scorer binary can be pointed at this .so instead of the
// default btrack plugin. No shared adapter/harness source is edited; the
// tracker source is not compiled into any tool binary.
//
// The .so's id() is "btrack-tempo-stable" — distinct from the default "btrack"
// and from the old variant "btrack-tempo-variant" — so a candidate run can never
// be mistaken for default or for the previously frozen variant. This is the
// honest source label: it is a BTrack backend plus the confirmation-gated
// median decorator, not a new tracker.
//
// Optional per-block method logging: if JAM_TEMPO_STABILITY_LOG_DIR is set, each
// constructed instance writes a raw CSV there named instance_<N>.csv, N being the
// construction order in this process. Diagnostics only.

#include "TempoStable.h"
#include "MethodLog.h"

#include "btrack/BTrackBackend.h"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{

std::atomic<unsigned> g_instanceCounter {0};

std::string instancePath (const std::string& dir, unsigned index)
{
    return dir + "/instance_" + std::to_string (index) + ".csv";
}

} // namespace

extern "C" jam::IRhythmTracker* jam_rhythm_create()
{
    tempo_stable::MethodLogFn log;

    const char* dir = std::getenv ("JAM_TEMPO_STABILITY_LOG_DIR");
    if (dir != nullptr && *dir != '\0')
    {
        const unsigned index = g_instanceCounter.fetch_add (1);
        auto logger = std::make_shared<tempo_stable::CsvMethodLog> (
            instancePath (dir, index));
        log = [logger] (const tempo_stable::MethodRecord& rec) { (*logger) (rec); };
    }

    auto inner = std::unique_ptr<jam::IRhythmTracker> (new jam::BTrackBackend());
    return new tempo_stable::TempoStableTracker (std::move (inner),
                                                 std::move (log));
}

extern "C" void jam_rhythm_destroy (jam::IRhythmTracker* tracker)
{
    delete tracker;
}
