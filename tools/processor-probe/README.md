# tools/processor-probe — RT-002 bounded processor runtime probe

Independent, local probe for measuring the **real** `GuitarCompanionProcessor`
audio callback: cold/warm allocations and frees (C++ `new`/`delete` kept apart
from the C `malloc` family), pthread mutex/cond operations, and delivery of the
scene-ready atomic flag through the processor's own `juce::Timer`. No mock, no
stub, no editor.

It reuses the already-built, read-only shared-code archive from the resumption
build; it does **not** rebuild JUCE and does **not** write into the plugin build
directory. It modifies no shared source.

## Layout

```
tools/processor-probe/
  build_probe.sh                 build the probe (reads production flags from build.ninja)
  run_probe.sh                   run it with isolated HOME/XDG and disk-backed TMPDIR
  src/RtProbeInstrumentation.h   allocation/lock counters + arming + self-check
  src/RtProbeInstrumentation.cpp global new/delete, wrapped malloc family, wrapped pthread
  src/ProcessorProbe.cpp         cases: self-check, dry matrix, NAM, scene/Timer proof
```

## Build

```sh
export PATH=/tmp/opencode/venv/bin:$PATH     # cmake/ninja live here on this host
tools/processor-probe/build_probe.sh
```

Env overrides:

- `PLUGIN_BUILD_DIR` (default `/home/mojo/projects/guitars-build-resume/plugin`)
- `RT002_OUT` (default `<parent of PLUGIN_BUILD_DIR>/probe/build`)

The script reads the exact `FLAGS`/`INCLUDES`/`DEFINES` of the production
`PluginProcessor.cpp` translation unit from the existing `build.ninja`, then
compiles the two probe translation units and links them against
`libGuitar Companion_SharedCode.a`, `libnam_core.a`, `libGuitarCompanionAssets.a`
and the sysroot system libraries.

The link uses `-Wl,--wrap=` for `malloc`, `calloc`, `realloc`, `free`,
`pthread_mutex_lock`, `pthread_mutex_trylock`, `pthread_mutex_unlock` and
`pthread_cond_clockwait`, and defines the global C++ `operator new`/`delete`
families so the archive's references resolve to the probe's counters.

## Run

```sh
tools/processor-probe/run_probe.sh
```

Env overrides:

- `RT002_WARM` warm blocks per case (default 256)
- `RT002_NAM_MODEL` NAM model path (defaults to the pinned NAM submodule's
  `example_models/lstm.nam`; if it does not exist the NAM case is recorded
  unmeasured)
- `RT002_ARTIFACTS` output directory (default `docs/research/processor-probe`)
- `RT002_SCRATCH`, `RT002_TIMEOUT`

The run uses an isolated `HOME`/`XDG_*` and a disk-backed `TMPDIR` (the default
`/tmp` is a small full tmpfs on this host). No X display is needed: the probe
uses JUCE's headless message loop and never creates a window or editor.

## Scope

This is a non-device probe: `processBlock` is invoked directly on the probe
thread. It is not latency/dropout/device-timing evidence and says nothing about
Windows/ASIO. The NAM result is model-specific. A clean dry result is not a
whole-program allocation-safety claim. See
`docs/research/PROCESSOR-RUNTIME-PROBE.md` for full results and limitations.
