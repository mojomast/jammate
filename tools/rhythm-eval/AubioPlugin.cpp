// C-linkage shim exposing the GPL aubio adapter through the plugin convention
// the evaluation CLI already documents (task-notes/EVAL-002.md, "CLI plugin
// convention"):
//
//   extern "C" jam::IRhythmTracker* jam_rhythm_create();
//   extern "C" void               jam_rhythm_destroy(jam::IRhythmTracker*);
//
// It exists so the aubio backend can be loaded by dlopen() at run time instead
// of being linked into the CLI. That keeps the tool binary free of GPL code
// (SPEC.md section 25.6) while still letting the real backend be scored over the
// real corpus. It mirrors tools/rhythm-eval/BtrackPlugin.cpp exactly; it is
// built only by build-aubio-plugin.sh.
//
// The header this includes deliberately does NOT pull in aubio.h (pimpl), so
// this translation unit stays behind the same licence boundary as
// src/aubio/AubioBackend.cpp.

#include "aubio/AubioBackend.h"

extern "C" jam::IRhythmTracker* jam_rhythm_create()
{
    return new jam::AubioBackend();
}

extern "C" void jam_rhythm_destroy (jam::IRhythmTracker* tracker)
{
    delete tracker;
}
