# tools/processor-probe — RT-002 bounded processor runtime probe

Independent, local probe for measuring the **real** `GuitarCompanionProcessor`
audio callback: cold/warm allocations and frees (C++ `new`/`delete` kept apart
from the C `malloc` family), pthread mutex/trylock/unlock/cond operations, and
delivery of the scene-ready atomic flag through the processor's own
`juce::Timer` — with a no-audio differential that discriminates the processor
Timer from the no-device fallback. No mock, no stub, no editor.

It reuses the already-built, read-only shared-code archive from the resumption
build; it does **not** rebuild JUCE and does **not** write into the plugin build
directory. It modifies no shared source.

## Layout

```
tools/processor-probe/
  build_probe.sh                 build (production flags; source-pin fail-closed)
  run_probe.sh                   3 bounded invocations + combined findings.json
  check_probe.sh                 malformed-CLI / output-failure checks
  src/RtProbeInstrumentation.h   allocation/lock counters + arming + self-check
  src/RtProbeInstrumentation.cpp global new/delete, wrapped malloc family, wrapped pthread
  src/ProcessorProbe.cpp         self-check, dry matrix, NAM, scene differential
```

## Build

```sh
export PATH=/tmp/opencode/venv/bin:$PATH     # cmake/ninja live here on this host
tools/processor-probe/build_probe.sh
```

Env overrides:

- `PLUGIN_BUILD_DIR` (default `/home/mojo/projects/guitars-build-resume/plugin`)
- `RT002_OUT` (default `<parent of PLUGIN_BUILD_DIR>/probe/build`)
- `RT002_MAIN_REPO` (default `/home/mojo/projects/guitars`) — archive source pin
- `RT002_SYSROOT_LIB` (default `<parent of PLUGIN_BUILD_DIR>/sysroot/usr/lib/x86_64-linux-gnu`)

The script reads the exact `FLAGS`/`INCLUDES`/`DEFINES` of the production
`PluginProcessor.cpp` translation unit from the existing `build.ninja`, prepends
the worktree `src/`, and **fails closed** unless the pinned processor/drum/rt
sources are byte-identical to the main checkout the archive was built from. It
writes `source-pin.txt` (source hashes + archive hashes + known pins) and links
against `libGuitar Companion_SharedCode.a`, `libnam_core.a`,
`libGuitarCompanionAssets.a` and the sysroot system libraries with `--wrap` for
the malloc family and pthread mutex/cond entry points.

## Run

```sh
tools/processor-probe/run_probe.sh     # full + two scene differential invocations
tools/processor-probe/check_probe.sh   # CLI/output failure checks
```

Env overrides: `RT002_WARM` (warm blocks, default 256), `RT002_NAM_MODEL`
(defaults to the pinned NAM submodule's `example_models/lstm.nam`; missing ⇒ NAM
recorded unmeasured), `RT002_ARTIFACTS` (default `docs/research/processor-probe`),
`RT002_SCRATCH`, `RT002_TIMEOUT`.

Each invocation uses an isolated `HOME`/`XDG_*` and a disk-backed `TMPDIR`. No X
display is needed. `run_probe.sh` combines the three invocations into
`findings.json` and exits non-zero unless every machine-verified check passes.

## Measurement notes

- Every callback gets a freshly written, deterministic, finite, non-zero input
  **outside** the armed region; only `processBlock` is armed (per block,
  counters accumulating).
- Drum cases install a deterministic 4-bar pattern; drum-bus activity, voice
  triggers and output RMS are recorded, and the playing RMS is compared against
  the identical-input stopped case to show the sampler actually renders.
- Bounded matrix duration is `257 * block / rate` seconds per case; it does not
  cover a full 4-bar cycle at 120 BPM.
- Per-block times are instrumented **wall** time (hook overhead included), not
  CPU time and not a latency gate.

## Scope

Non-device probe on one sequential thread: not latency/dropout/device-timing
evidence and nothing about Windows/ASIO. The NAM result is model-specific. A
clean dry result is not a whole-program allocation-safety claim. See
`docs/research/PROCESSOR-RUNTIME-PROBE.md` for full results and limitations.
