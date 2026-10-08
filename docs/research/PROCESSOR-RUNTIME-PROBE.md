# PROCESSOR-RUNTIME-PROBE — bounded runtime probe of the real processor callback

**Task:** RT-002 (bounded processor runtime probe)
**Worktree:** `/home/mojo/projects/worktrees/RT-002-processor-probe`, branch
`wp/RT-002-processor-probe`, base `cd9f97f`.
**Task note:** `task-notes/RT-002.md`.
**Probe sources:** `tools/processor-probe/**` (new; no shared source, CMake, docs
or ledger was modified).
**Artifacts:** `docs/research/processor-probe/` (logs, CSV, JSON findings,
failure checks, symbol resolution, environment/identity, manifest).

This document reports **executed** measurements of the genuine
`GuitarCompanionProcessor::processBlock` as linked into the pinned build. It
does not restate the audit; it measures it.

**Integration verification (2026-10-08):** accepted corrected worker `6e89b1c`.
An independent build and run under `/home/mojo/projects/build-RT-002-integration`
reproduced all 26 CSV cases exactly except instrumented wall time. Audio-driven
scene restoration occurred at **30.5 ms**; no-audio short remained unapplied,
and no-audio long restored at **257.1 ms**. All 15 CLI/output checks pass, plus
failure injection for a failing `tee` and an incorrect immutable source pin.
Integration fixes preserve both pipeline statuses, hash artifacts after final
status writes, verify sources against immutable `677ce9f`, detect JSON/CSV write
and close errors, and run the actual instrumentation self-check in every mode.
The no-audio dispatch bound is its requested window +25 ms (the audio bound stays
unchanged), correcting the use of the audio bound for the longer control window.
The worker artifacts below retain their original source/binary pins; current
source, binary, scratch artifact hashes and findings are in
`processor-probe/integration-verification.json`.

## Contract and scope

- SPEC.md §7.1 (audio-thread forbidden list), §18.1 (zero callback heap
  allocations/deallocations, zero callback blocking locks/waits), §18.2 (timing),
  §22 (diagnostics never log from the callback).
- `docs/research/RT-REACHABILITY.md` findings **F1** (scene callback message
  posting), **F2** (DrumEngine `MidiBuffer` growth) and **F6** (unaudited
  third-party `processBlock`).
- `docs/research/LOCAL-LINUX-BUILD.md` for the build environment.
- The callback source itself was **not modified** for this probe. Legacy
  callbacks were not edited even where a measurement is positive.

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
pinned JUCE module object. `tools/processor-probe/build_probe.sh` reads the exact
production compile flags from that build's `build.ninja` and links a small probe
executable. **It does not rebuild JUCE and does not write into the plugin build
directory.**

The worktree `src/` include is prepended to the production include list so the
worktree headers always win, and `build_probe.sh` **fails closed** unless every
source file the probe compiles against (`PluginProcessor.{h,cpp}`,
`DrumEngine.{h,cpp}`, `rt/RtSignal.h`) is byte-identical to the main checkout the
archive was compiled from. It writes `source-pin.txt` recording those hashes, the
known pins (base `cd9f97f`, RT-001-F2 implementation `677ce9f`) and the archive
hashes. The probe constructs `GuitarCompanionProcessor` directly and calls its
real `processBlock`; **no editor is ever created** (`getActiveEditor()` stays
`nullptr` throughout).

### Instrumentation (`tools/processor-probe/src/RtProbeInstrumentation.*`)

Fixed, preallocated, allocation-free instrumentation:

- Global replacement of the C++ `new`/`new[]`/nothrow/aligned and matching
  `delete` families, routed through `__real_malloc`/`__real_free`.
- Linker `--wrap` interposition of C `malloc`/`calloc`/`realloc`/`free`, so C++
  and C allocations are counted under **separate** categories.
- `--wrap` interposition of `pthread_mutex_lock`/`trylock`/`unlock` and
  `pthread_cond_clockwait`, counting acquisitions, wait time and lock records.
- A single `thread_local bool` arm flag; every counter is a static `std::atomic`
  with a compile-time `is_always_lock_free` assertion; every detail record lives
  in a fixed array (best-effort, with overflow counters). Nothing allocates or
  locks inside a hook.
- The max-lock-time publish is a bounded relaxed load/conditional store. There is
  **no CAS retry loop** in the instrumentation.
- `free(NULL)`/`delete nullptr` are defined no-ops, counted separately from heap
  frees. Caller addresses are stored file-relative for `addr2line`.

C-semantics notes: `malloc(0)`/`realloc(ptr,0)`/`new(0)` are forwarded unchanged
to the real allocator (no `n ? n : 1` rewrite); a `realloc` is reported as a
`c:realloc` call only and does not imply a free-counter increment (any internal
free happens inside libc). Direct calls to `posix_memalign`/`aligned_alloc` from
shared libraries are outside the `--wrap` set; C++ aligned `new` **is** measured,
C `posix_memalign` is not.

### Instrument self-check (run before any processor measurement)

The probe runs `rtprobe::runSelfCheck` first and **refuses to report processor
results and exits non-zero if it fails**. It proves both the gate and the
detectors with **exact** expected counts (not "at least"):

- an unarmed region performing new/delete, malloc family and mutex/cond
  operations records **nothing**;
- an armed region records exactly `new=2, new[]=2, delete-family=4` (unsized and
  sized variants covered), `malloc=1, calloc=1, realloc=1, free=2`,
  `lock=2, unlock=2, trylock=1, cond=1`.

Executed output (`instrument-selfcheck.txt`):

```
armed exact: new=2 new[]=2 del=1 del[]=2 del(sized)=1 malloc=1 calloc=1 realloc=1 free=2 lock=2 unlock=2 trylock=1 cond=1
self-check PASS (gate + exact new/delete/malloc/lock detection)
```

### Measurement protocol

For each case: `prepareToPlay(rate, block)` (not measured), then

- **cold** — the very first `processBlock` after prepare, armed explicitly;
- **warm** — 256 consecutive `processBlock` calls. **Every callback gets a
  freshly filled, deterministic, finite, non-zero input written OUTSIDE the armed
  region**; only `processBlock` is armed, per block, with counters accumulating
  across the run.

Drum cases use the internal sampler: the processor constructor loads the embedded
GM kit and the probe installs a deterministic 4-bar pattern. The seeded input for
a `rate`/`block` pair does **not** depend on the drum mode, so the stopped and
playing cases at the same rate/block process an identical input sequence and can
be compared directly.

To show the built-in sampler really schedules and renders voices (rather than
only setting `engine.playing`), each warm block records the drum-bus activity
(`uiMixPeak` exchange) and the number of voice triggers (`uiVoiceFlash`
exchange), and the mean output RMS. `drums[active=256 voices=N] outRms` is
reported per case; the playing case's RMS is compared against the identical-input
stopped case.

Bounded matrix duration is `257 * block / rate` seconds per case (1 cold + 256
warm). At 120 BPM a full 4-bar cycle is 8 s, so **no case covers a complete 4-bar
cycle**; durations here are 0.17 s–2.97 s.

The probe never sleeps and never polls a fabricated worker status.

## Executed evidence

Environment: Debian 13, Linux 6.12 x86_64, GCC 14.2, JUCE pin `91ad83ae`, NAM
pin `1f42f885`, Release, no audio device, no X (headless JUCE message loop).
Full identity in `docs/research/processor-probe/env-info.txt`; artifact hashes in
`manifest.sha256`.

### 1. Instrument self-check

**PASS** with exact counts (above). Prerequisite for the dry zeros to mean "the
detectors were armed and found nothing".

### 2. Dry built-in guitar chain — rates × blocks × drums

18 cases (3 rates × 3 block sizes × drums stopped/playing). **Every case: cold
0/0/0 and warm 0/0/0 allocator calls, frees, and mutex/cond operations.**

| rates | blocks | drums | cold alloc/free/lock | warm alloc/free/lock (256 blocks) |
|---|---|---|---|---|
| 44100 / 48000 / 96000 | 64 / 128 / 512 | stopped and playing | 0 / 0 / 0 | 0 / 0 / 0 |

Drum rendering evidence (deterministic, identical input, `outRms` is the mean
output RMS over the 256 warm blocks):

| rate/block | `active` blocks | voice events | outRms stopped | outRms playing |
|---|---|---|---|---|
| 44100/64 | 256 | 2 | 0.14125 | 0.20423 |
| 44100/128 | 256 | 4 | 0.14147 | 0.20151 |
| 44100/512 | 256 | 22 | 0.14177 | 0.19347 |
| 48000/64 | 256 | 2 | 0.14034 | 0.21142 |
| 48000/128 | 256 | 4 | 0.14242 | 0.20537 |
| 48000/512 | 256 | 20 | 0.14187 | 0.19677 |
| 96000/64 | 256 | 0 | 0.14122 | 0.24854 |
| 96000/128 | 256 | 2 | 0.14230 | 0.21027 |
| 96000/512 | 256 | 10 | 0.14166 | 0.19687 |

All 256/256 blocks carried drum-bus audio, and the playing RMS exceeds the
identical-input stopped RMS in every pair. (`voice events` count blocks with a
new sampler hit; at 96 kHz/64 the 16th-note step is ~12000 samples, so few hits
fall inside the 0.17 s run — the `active=256` and RMS difference show the
sampler is rendering regardless of trigger counting.)

One benign artifact is recorded, not hidden: with drums **playing**, each block
emits exactly one `free(NULL)` no-op (256 per case) from
`GuitarCompanionProcessor::processDrums` — the `juce::HeapBlock` destructor of
the temporary `AudioBuffer` view calls `std::free(data)` unconditionally. It is a
defined no-op, not a heap deallocation, and is counted separately from real
frees (`symbols.txt`).

This closes, for the built-in path and exercised matrix, the callback-side
questions the audit left conditional for `DrumEngine` (F2): the internal-sampler
path adds no MIDI events and allocates nothing, with or without drums playing.
It does **not** claim F2 is whole-closed — the engine's separate MIDI path and
the intentionally MIDI-free internal sampler are unchanged.

### 3. NAM model callback path — naturally available example capture

A real, pinned NAM example model from the NAM submodule
(`third_party/NeuralAmpModelerCore/example_models/lstm.nam`, 2307 bytes,
sha256 `df9f78c4…`) was loaded through the processor's own public
`loadModelAsync`, consumed by the real `processBlock` swap, and reported live by
the processor. No download was performed.

The model became active during the dry run, i.e. the activation swap occurred
inside a dry warm block and was **not separately isolated**. The NAM "cold"
below is therefore the first NAM processBlock **after `prepareToPlay`**, not the
activation swap. (The swap itself performs no callback allocation: `pendingModels`
exchange plus `unique_ptr::reset` of a previously-null lane pointer.)

| case | block | cold alloc/free | warm alloc/free (256 blocks) | allocs / host sample |
|---|---|---|---|---|
| nam 48000 | 128 | 256 / 256 | 65536 / 65536 | 2.0 |
| nam 48000 | 512 | 1024 / 1024 | 262144 / 262144 | 2.0 |
| nam 96000 (resampled) | 128 | 128 / 128 | 32768 / 32768 | 1.0 |
| nam 96000 (resampled) | 512 | 512 / 512 | 131072 / 131072 | 1.0 |

At 96 kHz the resampler feeds the 48 kHz model at half the host rate, so 1.0
host-sample allocation = **2.0 allocations per model sample** — the same rate as
at 48 kHz. The same counts occur with drums playing (`nam+drums` is 8 of the 8
cases with warm allocations). Resolved call sites (`symbols.txt`):

- `nam::lstm::LSTMCell::process_` — `calloc(48)` immediately followed by `free`
- `nam::lstm::LSTM::_process_sample` / `LSTM::process` — `malloc(12)` immediately
  followed by `free`

These are C-allocator calls, not C++ `new`/`delete`, occurring on the probe
thread **inside** the armed `processBlock` region (the loader thread's TLS arm
flag is false, so loader work is not counted). This is the concrete, executed
form of the audit's **F6 P1** concern. It is model-specific — `lstm.nam` is not a
production capture and other architectures were not measured.

Hosted VST3: no third-party plugin binary is present, so that case is
**unmeasured** (recorded, not downloaded).

### 4. Scene-ready flag → processor-owned Timer, editor absent — discriminated

The public deferred scene path is exercised: save section 0 through
`saveSceneForSection(0)`, change the live `inputGain` from `0.0` to `-16.8`, then
`applySceneForSection(0)`. The audio-driven run then makes 11 real `processBlock`
calls (12 ms fade-out) to raise the atomic `sceneReadyToApply` flag, and a genuine
JUCE message loop is run. Instrumentation is armed over those 11 blocks with
per-block fill outside the armed region.

The attribution of the restoration to the **processor-owned `juce::Timer`** (as
opposed to the no-device `Timer::callAfterDelay` safety net at 12 + 250 ms =
262 ms) is established by a three-invocation differential, because a single
`runDispatchLoop` cannot be restarted:

| invocation | audio drive | window | restored? | restore from arm | processor timer? |
|---|---|---|---|---|---|
| `full` scene | yes (flag raised) | 100 ms | **yes** | 32.5 ms | yes |
| `scene-noaudio-short` | no | 150 ms | **no** | — | n/a |
| `scene-noaudio-long` | no | 400 ms | **yes** | 256.2 ms | fallback (262 ms) |

- With no audio and a 150 ms window (< 262 ms) the scene is **not** restored, so
  the fallback cannot be responsible for the audio-driven restoration at 32.5 ms.
- With no audio and a 400 ms window the fallback restores at ~256 ms, confirming
  its existence and timing.
- With audio, restoration at 32.5 ms is far below 262 ms and occurs only through
  the real JUCE message dispatch; the processor-owned Timer consumed the flag.

A message-thread observer (our own `MessageManager::callAsync` chain, never a
manual invocation of the timer callback or of the flag consumer) records the
first observation time. The loop bound is also an async message chain rather than
a `juce::Timer`, precisely because a freshly started `juce::Timer` can fire early
once the timer thread has been idle; using async messages removes the need for a
separate timer-thread prewarm and keeps the window deterministic. The chain is
capped (tick cap + wall deadline) and holds a shared state object, so no
`this`-captured callback can dangle after the case returns. Raw output:

```
scene[audio]: saved=0.000000 changed=-16.799999 before=-16.799999 after=0.000000
              restored=1 restore@32.5 ms (loop@32.2 ms) dispatch=100.0 ms
scene[audio]: blocks=11 signal(alloc=0 free=0 lock=0 trylock=0 cond=0 unlock=0)
              stopBefore=0 stopAfter=1 dispatchInBound=1 editorNull=1
scene[noaudio]: restored=0 dispatch=150.0 ms   (short window)
scene[noaudio]: restored=1 restore@256.2 ms dispatch=400.0 ms   (long window)
```

`hasStopMessageBeenSent()` is `false` before each loop. The editor is never
created (`getActiveEditor()==nullptr` before and after). The signalling blocks
allocate/lock nothing.

## Machine-verified findings

`docs/research/processor-probe/findings.json` combines the three invocations and
computes the checks below; `tools/processor-probe/run_probe.sh` exits non-zero
unless all pass.

| check | value |
|---|---|
| self-check pass | true |
| dry cases non-zero (of 18) | 0 |
| NAM cases with callback allocation (of 8) | 8 |
| NAM allocs per model sample (48 kHz) | 2.0 |
| scene restored, audio-driven, < 262 ms | true |
| scene restore elapsed (audio) | 32.5 ms |
| scene signal block alloc/free/lock zero | true |
| no-audio short window restored | false |
| no-audio long window restored (fallback) | true |
| editor created | false |
| timer-vs-fallback discriminated | true |

Per-case categories (C++ vs C, locks/trylock/cond/unlock, no-op frees, overflow)
are in `processor-probe-combos.csv`.

## Honest limitations

- This is a **non-device** probe: `processBlock` is invoked directly on the
  probe's thread, single-threaded and sequential. It is **not** latency, dropout,
  or device-timing evidence, and says nothing about Windows/ASIO.
- Reported per-block times are instrumented **wall time** around the armed
  `processBlock` call (hook overhead included). They are **not** CPU time and are
  **not** a latency gate.
- The NAM result is specific to the upstream example `lstm.nam`; production
  captures and other architectures were not measured.
- The activation swap for NAM was not separately isolated (it occurred inside a
  dry warm block); its callback allocation is zero by observation, but it is
  reported as not separately measured.
- Symbol-level interposition leaves allocations performed wholly inside shared
  `libc`/`libstdc++` (e.g. `strdup`) and platform-aligned allocators called
  directly by shared libraries outside the measured set; `std::mutex` locked
  entirely inside `libstdc++` is likewise not intercepted. The callback path was
  observed to use `juce::CriticalSection` (interposed) and atomics.
- The scene attribution is a bounded differential + timing argument as above; no
  source/binary breakpoint was used. It is reported at that strength only.
- A clean dry result over this bounded matrix is **not** a whole-program "safe"
  claim. **G1 remains partial.**

## Reproduce and failure checks

```sh
cd /home/mojo/projects/worktrees/RT-002-processor-probe
export PATH=/tmp/opencode/venv/bin:$PATH

tools/processor-probe/build_probe.sh      # read-only archive reuse + source-pin check
tools/processor-probe/run_probe.sh        # 3 bounded invocations + combined findings
tools/processor-probe/check_probe.sh      # malformed CLI + output-failure checks
```

`check_probe.sh` (manually executed; no CMake/ctest wiring) verifies exit 64 on
malformed `--warm-blocks`/unknown options/unknown scene mode, exit 5 on an
unwriteable CSV path, and exit 0 plus expected evidence on the bounded contract
cases; its output is archived as `failure-checks.log`.

Artifacts in `docs/research/processor-probe/`:

| file | content |
|---|---|
| `processor-probe-run.log` | full human-readable run |
| `processor-probe-combos.csv` | per-case totals with per-kind categories |
| `findings.json` | combined machine-verified checks |
| `findings-full.json`, `scene-noaudio-{short,long}.json` | per-invocation JSON |
| `scene-noaudio-{short,long}.log` | differential logs |
| `instrument-selfcheck.txt` | exact self-check extract |
| `symbols.txt` | caller-offset → symbol resolution |
| `env-info.txt` | host/tool/pin/source/archive/binary identity |
| `failure-checks.log` | CLI/output failure checks |
| `exit-status.txt`, `manifest.sha256` | statuses and hashes of all inputs/outputs |
