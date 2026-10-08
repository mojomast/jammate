// TempoVariantPlugin — dlopen()-able variant backend (TRACK-005).
//
// Exposes the existing plugin convention:
//
//   extern "C" jam::IRhythmTracker* jam_rhythm_create();
//   extern "C" void               jam_rhythm_destroy(jam::IRhythmTracker*);
//
// It composes the real, unmodified jam::BTrackBackend (linked from the pinned
// EVAL-005 main-core static archives: libjam-btrack.a + libbtrack.a +
// libsamplerate.a + libkiss_fft.a) inside the TempoVariantTracker decorator, so
// the SAME diagnostic binary can be pointed at this .so instead of the default
// btrack plugin. No shared adapter/harness source is edited; the tracker source
// is not compiled into any tool binary (structural separation, not a legal
// conclusion).
//
// The .so's `id()` is "btrack-tempo-variant" — distinct from the default
// "btrack" — so a variant run can never be mistaken for default evidence.
//
// Optional per-block method logging: if JAM_TEMPO_VARIANT_LOG_DIR is set, each
// constructed instance writes a raw CSV there named instance_<N>.csv, N being
// the construction order in this process (the CLI constructs one backend per
// fixture in manifest order). Diagnostics only.

#include "TempoVariant.h"
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
    tempo_variant::MethodLogFn log;

    const char* dir = std::getenv ("JAM_TEMPO_VARIANT_LOG_DIR");
    if (dir != nullptr && *dir != '\0')
    {
        const unsigned index = g_instanceCounter.fetch_add (1);
        auto logger = std::make_shared<tempo_variant::CsvMethodLog> (
            instancePath (dir, index));
        log = [logger] (const tempo_variant::MethodRecord& rec) { (*logger) (rec); };
    }

    auto inner = std::unique_ptr<jam::IRhythmTracker> (new jam::BTrackBackend());
    return new tempo_variant::TempoVariantTracker (std::move (inner),
                                                   std::move (log));
}

extern "C" void jam_rhythm_destroy (jam::IRhythmTracker* tracker)
{
    delete tracker;
}
