// ConfigPlugin — a diagnostic-only backend shim that lets the TRACK-004 driver
// instantiate the existing adapter with a NON-DEFAULT configuration, so the
// effect of one documented adapter knob can be *measured* rather than asserted.
//
// The shared adapter and the shared plugin (tools/rhythm-eval/BtrackPlugin.cpp,
// AubioPlugin.cpp) are NOT modified: this is a second, separate shim owned by
// tools/tracker-diagnostics. It is linked against the same pinned static
// archives. The knob is read from the environment so the existing
// jam_rhythm_create() signature is preserved:
//
//   JAM_DIAG_SILENCE_DBFS   adapter held-silence threshold (default -60 dBFS)
//
// Every run using this shim MUST be labelled --variant-not-default in the
// driver provenance. Its numbers are NOT the default evidence.
//
// Build once per backend (see build.sh): -DJAM_DIAG_BTRACK selects the adapter.

#include "jam/IRhythmTracker.h"

#ifdef JAM_DIAG_BTRACK
#  include "btrack/BTrackBackend.h"
#else
#  include "aubio/AubioBackend.h"
#endif

#include <cstdlib>

namespace
{
double envDouble (const char* name, double fallback)
{
    const char* s = std::getenv (name);
    if (s == nullptr || *s == '\0')
        return fallback;
    char* end = nullptr;
    const double v = std::strtod (s, &end);
    return (end != nullptr && *end == '\0') ? v : fallback;
}
} // namespace

extern "C" jam::IRhythmTracker* jam_rhythm_create()
{
    const double silenceDb = envDouble ("JAM_DIAG_SILENCE_DBFS", -60.0);
#ifdef JAM_DIAG_BTRACK
    jam::BTrackBackendConfig config;
    config.silenceRmsDbfs = static_cast<float> (silenceDb);
    return new jam::BTrackBackend (config);
#else
    jam::AubioBackendConfig config;
    config.silenceRmsDbfs = static_cast<float> (silenceDb);
    return new jam::AubioBackend (config);
#endif
}

extern "C" void jam_rhythm_destroy (jam::IRhythmTracker* tracker)
{
    delete tracker;
}
