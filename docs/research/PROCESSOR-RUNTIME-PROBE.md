# PROCESSOR-RUNTIME-PROBE — bounded runtime probe of the real processor callback

**Task:** RT-002 (bounded processor runtime probe)
**Worktree:** `/home/mojo/projects/worktrees/RT-002-processor-probe`, branch
`wp/RT-002-processor-probe`, base `cd9f97f`.
**Task note:** `task-notes/RT-002.md`.
**Probe sources:** `tools/processor-probe/**` (new; no shared source, CMake, docs or
ledger was modified).
**Artifacts:** `docs/research/processor-probe/`.

This document reports **executed** measurements of the genuine
`GuitarCompanionProcessor::processBlock` as linked into the pinned build. It does
not restate the audit; it measures it.

## Contract and scope

- SPEC.md §7.1 (audio-thread forbidden list), §18.1 (zero callback heap
  allocations/deallocations, zero callback blocking locks/waits), §18.2 (timing),
  §22 (diagnostics never log from the callback).
- `docs/research/RT-REACHABILITY.md` findings **F1** (scene callback message
  posting), **F2** (DrumEngine `MidiBuffer` growth) and **F6** (unaudited
  third-party `processBlock`).
- `docs/research/LOCAL-LINUX-BUILD.md` for the build environment.
- The callback source itself was **not modified** for this probe.

Out of scope (stated, not claimed): physical audio device / latency / dropout
behaviour, Windows/ASIO, host transport timing, production NAM captures, hosted
VST3 plugins, and a whole-program allocation proof.

## Method

### Reusing the real binary, read-only

The probe links, read-only, against the already-built, self-contained shared-code
archive from the resumption build:

```
/home/mojo/projects/guitars-build-resume/plugin/
  GuitarCompanion_artefacts/Release/libGuitar Companion_SharedCode.a
  libnam_core.a
  libGuitarCompanionAssets.a
```

The archive already contains the compiled `PluginProcessor.cpp.o` plus every
pinned JUCE module object, so `tools/processor-probe/build_probe.sh` reads the
exact production compile flags from that build's `build.ninja` and links a small
probe executable. **It does not rebuild JUCE and does not write into the plugin
build directory.** The worktree `src/` was verified byte-identical to the main
checkout's `src/` from which the archive was compiled.

The probe constructs `GuitarCompanionProcessor` directly and calls its real
`processBlock`. No mock, no stub, and **no editor is ever created**
(`getActiveEditor()` stays `nullptr` throughout).

### Instrumentation (`tools/processor-probe/src/RtProbeInstrumentation.*`)

Fixed, preallocated, allocation-free instrumentation:

- Global replacement of the C++ `new`/`new[]`/nothrow/aligned and matching
  `delete` families, routed through `__real_malloc`/`__real_free`.
- Linker `--wrap` interposition of the C `malloc`/`calloc`/`realloc`/`free`
  families, so C++ and C allocations are counted under **separate** categories.
- `--wrap` interposition of `pthread_mutex_lock`/`trylock`/`unlock` and
  `pthread_cond_clockwait`, counting acquisitions, waits and total/maximum
  waited nanoseconds.
- A single `thread_local bool` arm flag. `arm()`/`disarm()` are the only control
  operations; every counter is a static `std::atomic`; every detail record lives
  in a fixed array (best-effort, with an overflow counter). Nothing allocates or
  locks inside a hook, and no hook records unless the measuring code has armed
  the current thread.
- `free(NULL)`/`delete nullptr` are defined no-ops and are counted separately
  from heap frees. Recorded caller addresses are stored as file-relative
  offsets (caller minus the main-module load bias), so they can be resolved
  with `addr2line`.

### Instrument self-check (run before any processor measurement)

The probe runs `rtprobe::runSelfCheck` first and **refuses to report processor
results if it fails**. The check proves both the gate and the detectors:

- An **unarmed** region performing `new`/`delete`, `malloc`/`free` and a mutex
  lock/unlock records **nothing**.
- An **armed** region performing the same operations records each family under
  the correct category. The unsized C++ families are exercised through direct
  `::operator new`/`::operator delete` calls so the compiler cannot elide them.

Executed self-check output (`instrument-selfcheck.txt`):

```
armed: new=2 new[]=2 del=1 del[]=2 del(sized)=1 malloc=1 calloc=1 realloc=1 free=2 lock=1 unlock=1
self-check PASS (gating + known new/delete/malloc/lock detection)
```

### Measurement protocol

For each case: `prepareToPlay(rate, block)` (not measured), then

- **cold** — the very first `processBlock` after prepare, armed explicitly;
- **warm** — a bounded run of 256 consecutive `processBlock` calls, armed as one
  region (so any single block's allocation appears in the totals).

Input `AudioBuffer`/`MidiBuffer` storage is allocated outside the armed regions.
"Dry" means the amp block is bypassed (`ampOn=0`), exercising the built-in guitar
chain; drums use the internal sampler (embedded GM kit loaded by the processor
constructor), either stopped or playing a deterministic 4-bar pattern.

The probe never sleeps and never polls a fabricated worker status. Cases run
once; there is no statistical claim, only observed counts over the bounded run.

## Executed evidence

Environment: Debian 13, Linux 6.12 x86_64, GCC 14.2, JUCE pin `91ad83ae`,
NAM pin `1f42f885`, Release, no audio device, no X (the JUCE message loop runs
headless). Full identity in `docs/research/processor-probe/env-info.txt`.

### 1. Instrument self-check

**PASS** (above). This is the prerequisite that lets the dry zeros below mean
"the detectors were armed and found nothing", not "the hooks were absent".

### 2. Dry built-in guitar chain — rates × blocks × drums

18 cases (3 rates × 3 block sizes × drums stopped/playing), each cold + 256 warm
blocks. **Every case: 0 allocator calls, 0 frees, 0 mutex/cond operations**, in
both the cold first callback and the warm run.

| rates | blocks | drums | cold alloc/free/lock | warm alloc/free/lock (256 blocks) |
|---|---|---|---|---|
| 44100 / 48000 / 96000 | 64 / 128 / 512 | stopped and playing | 0 / 0 / 0 | 0 / 0 / 0 |

Per-case CSV: `docs/research/processor-probe/processor-probe-combos.csv`.
Representative cost (not a timing gate): 9–25 µs/block at 64–128 samples,
72–86 µs/block at 512 samples, CPU-time on this host.

One benign artifact is recorded, not hidden: with drums **playing**, each block
emits exactly one `free(NULL)` no-op (256 per case; 3328 total) from
`GuitarCompanionProcessor::processDrums` — specifically the `juce::HeapBlock`
destructor of the temporary `AudioBuffer` view, which calls `std::free(data)`
unconditionally. It is a defined no-op, not a heap deallocation, and is counted
separately from real frees. Resolved in `docs/research/processor-probe/symbols.txt`.

This closes, for the built-in path and the exercised rate/block set, the
callback-side questions the audit left conditional for `DrumEngine` (F2): the
internal-sampler path adds no MIDI events and allocates nothing, with or without
drums playing.

### 3. NAM model callback path — naturally available example capture

A real, pinned NAM example model from the repository's NAM submodule
(`third_party/NeuralAmpModelerCore/example_models/lstm.nam`, 2307 bytes,
sha256 `df9f78c4…`) was loaded through the processor's own public
`loadModelAsync`, consumed by the real `processBlock` swap, and reported live by
the processor (`hasModelLoaded(0)==true`). No download was performed.

Unlike the built-in path, the NAM LSTM path **does allocate on the callback**:

| case | block | cold alloc/free | warm alloc/free over 256 blocks | per model-sample |
|---|---|---|---|---|
| nam 48000 | 128 | 256 / 256 | 65536 / 65536 | 2 allocs/sample |
| nam 48000 | 512 | 1024 / 1024 | 262144 / 262144 | 2 allocs/sample |
| nam 96000 (resampled) | 128 | 128 / 128 | 32768 / 32768 | 2 allocs/model-sample |
| nam 96000 (resampled) | 512 | 512 / 512 | 131072 / 131072 | 2 allocs/model-sample |

The same counts occur with drums playing (`nam+drums` is 8 of the 8 cases with
warm allocations). Resolved call sites:

- `nam::lstm::LSTMCell::process_` — `calloc(48)` immediately followed by `free`
- `nam::lstm::LSTM::_process_sample` / `LSTM::process` — `malloc(12)` immediately
  followed by `free`

These are C-allocator calls, not C++ `new`/`delete`, and they occur on the probe
thread **inside** the armed `processBlock` region (the loader thread's own
thread-local arm flag is false, so loader work is not counted). This is the
concrete, executed form of the audit's **F6 P1** concern: a third-party
`processBlock` reachable from the fork's callback allocates per sample for this
model. It is model-specific — `lstm.nam` is not a production capture, and other
architectures were not measured.

Hosted VST3: no third-party plugin binary is present in the repository or build,
so the hosted-plugin case is **unmeasured** (recorded, not downloaded).

### 4. Scene-ready flag delivered to the processor-owned Timer, editor absent

This exercises `RT-REACHABILITY` **F1** after the SignalFlag/Timer integration.

- Section 0 scene saved through the public `saveSceneForSection(0)`; the live
  `inputGain` parameter changed from `0.0` to `-16.8` (observable public state).
- `applySceneForSection(0)` (public) armed the audio-thread envelope.
- 11 real `processBlock` calls completed the 12 ms fade-out and raised the
  atomic `sceneReadyToApply` flag. Measured over those blocks (armed):
  **0 allocations, 0 frees, 0 locks**.
- Immediately afterwards the scene was **not yet applied**
  (`beforeDispatch = -16.8`): no message loop had run, and the no-device safety
  net (due at 12 ms + 250 ms = 262 ms) cannot have fired.
- The genuine JUCE message dispatch loop was then run headless for a bounded
  **100 ms** (< 262 ms), with `hasStopMessageBeenSent()==false` at entry. The
  processor's own `juce::Timer` (started in its constructor) consumed the flag
  and applied the scene: `afterDispatch = 0.0`, i.e. the saved rig restored.
- `getActiveEditor()==nullptr` before and after: **the editor was never
  created**.

This is a differential proof of delivery through the processor-owned Timer and
the real JUCE message dispatch, not a manual invocation of the timer callback or
the flag consumer. Raw output:

```
scene: saved=0.000000 changed=-16.799999 beforeDispatch=-16.799999 afterDispatch=0.000000
scene: blocks=11 signalAlloc=0 signalFrees=0 signalLocks=0 dispatch=100.0 ms
       stopSentBefore=0 stopSentAfter=1 appliedBefore=1 appliedAfter=1 editorNull=1
```

## Findings summary

| # | Finding | Status |
|---|---|---|
| 1 | Instrument gates correctly and detects known new/delete, malloc/free and mutex operations | **measured** |
| 2 | Built-in guitar chain, dry and with built-in drums stopped/playing, 44.1/48/96 kHz, blocks 64/128/512: zero allocations, zero frees, zero callback locks | **measured** (bounded set) |
| 3 | Drums playing emits one benign `free(NULL)` no-op per block from the `processDrums` temporary `AudioBuffer` (`HeapBlock` destructor) | **measured** |
| 4 | NAM LSTM `processBlock` allocates ~2 C-heap allocations per sample (`calloc(48)` + `malloc(12)`), freed immediately; reachable from the real callback | **measured for `lstm.nam`** |
| 5 | Scene-ready atomic flag is consumed by the processor-owned Timer through the real JUCE message loop, with the editor never created, and the signalling blocks allocate/lock nothing | **measured** |
| 6 | Hosted VST3 callback behaviour | **unmeasured** (no plugin present) |
| 7 | Whole-processor / production-capture allocation freedom | **not established** |

## Honest limitations

- This is a **non-device** probe: `processBlock` is invoked directly on the
  probe's thread. It is **not** latency, dropout, or device-timing evidence, and
  says nothing about Windows/ASIO.
- The NAM result is specific to the upstream example `lstm.nam`; production
  captures and other NAM architectures were not measured, and the result does
  not generalise without running them.
- The interposer is symbol-level: allocations performed wholly inside shared
  `libc`/`libstdc++` on behalf of an inlined call (e.g. `strdup`) and locks taken
  by `std::mutex` entirely inside `libstdc++` are outside the instrumented set.
  The fork's callback path uses `juce::CriticalSection` (interposed) and
  atomics; no `std::mutex` was observed in the callback.
- The measurements are single-threaded and sequential; no concurrent UI/loader
  contention was exercised during the armed regions.
- A clean dry result over this bounded matrix is **not** a whole-program "safe"
  claim. G1 remains partial.

## Reproduce

```sh
cd /home/mojo/projects/worktrees/RT-002-processor-probe
export PATH=/tmp/opencode/venv/bin:$PATH

# Build (reuses the prebuilt archive read-only; writes only to the scratch dir)
tools/processor-probe/build_probe.sh

# Run with isolated HOME/XDG and disk-backed TMPDIR; writes the artifacts
tools/processor-probe/run_probe.sh
```

Artifacts written to `docs/research/processor-probe/`:

| file | content |
|---|---|
| `processor-probe-run.log` | full human-readable run |
| `processor-probe-combos.csv` | per-case totals |
| `instrument-selfcheck.txt` | self-check extract |
| `symbols.txt` | caller-offset → symbol resolution |
| `env-info.txt` | host/tool/pin/archive/binary SHA identity |

Artifact identity is recorded in `env-info.txt`; the probe binary sha256 at the
time of this run was `c2d514adedd7c7e10a146161a0d5c28246dcc50169848481484622b5c8f2628e`
and the linked shared-code archive sha256 was
`46edfe5dcda44acf28c690b5a2a0eeb3401f6f7aff0f557d1863003d96f6b147`.
