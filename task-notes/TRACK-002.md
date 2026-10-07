# TRACK-002

## Goal

Integrate **aubio 0.4.9** as the second beat-tracker candidate behind
`jam::IRhythmTracker`, so SPEC.md §12's comparison has more than one entry and
G3 has a control against which BTrack is judged. Deliver it exactly like
TRACK-001: vendored, pinned, licence-explicit, behind a structural GPL boundary,
with a sample-rate decision, a phase decision, a causality proof, tests that
were actually executed, and measured metrics. Per DEVPLAN.md: *"Do not claim
superiority; provide measurements."* This note does that.

## Base commit

`c68df60d5aaf138f24070976c0ab7f0e9aa2ce9c`
(*merge(EVAL-001 repair): true silence declared separately; core membership
usable*). Branch `wp/TRACK-002`. Worktree
`/home/mojo/projects/worktrees/TRACK-002`.

## Files changed

Created only (nothing existing was modified; `git status` clean before commit):

- `third_party/aubio/` — vendored aubio 0.4.9 (`src/` byte-identical to
  upstream), plus `COPYING`, `AUTHORS`, `README.md`, `VERSION`, `PINNED_SHA`,
  `VENDORED-PATCHES.md`, `config.h.cmake`, `CMakeLists.txt`. **980 KB on disk
  (715 KB of content).**
- `src/aubio/AubioBackend.h` — interface + config; no GPL header leaks.
- `src/aubio/AubioBackend.cpp` — the adapter; the only TU that includes
  `aubio.h`.
- `tests/jam/AubioBackendTests.cpp` — suite `AubioBackend` (ctest
  `jam.AubioBackend`), 11 cases.
- `task-notes/TRACK-002.md` — this file.

No file under `src/jam/`, `third_party/BTrack/`, `src/btrack/`, any CMake file,
`testdata/`, `SPEC.md`, `DEVPLAN.md` or `docs/**` was touched.

## Contract implemented

- `id()` → stable string `"aubio"`.
- `reset(double sampleRate)` accepts any rate in `[8000, 192000]`, is
  deterministic (test 3 compares every field of every observation across two
  runs), and is the only place that allocates — aubio's `new_aubio_tempo()` and
  `new_fvec()` calls are there.
- `process(const AnalysisFrame&)` consumes up to `kMaxAnalysisBlock` (2048) mono
  samples, returns exactly one `RhythmObservation`, never blocks, performs no
  file/network I/O, and uses no hidden global state. A partial hop is carried
  across blocks in a preallocated 512-float `fvec_t`.
- The backend supplies **evidence**: `bpmCandidate`, `beatEvent`,
  `beatPhase01`/`phaseValid`, `beatConfidence01`, `onsetStrength01`,
  `transientDensity01`, `energyRmsDbfs`, `silence`, `inputSampleTime` (on the
  device sample clock). It never writes a drum tempo.
- Tunables live in `AubioBackendConfig`, not as scattered literals.

## Disk impact

**Measured before designing anything:**

```
$ df -h /home
/dev/sda1  197G  181G  8.5G  96% /            # 8.5 GB free
$ du -sh third_party/*        # pre-existing
228K  third_party/BTrack
9.6M  third_party/libsamplerate     # already there for BTrack
```

**Measured options before cloning** (upstream tag `0.4.9`, blobless view via a
throwaway `git clone --depth 1` in `/tmp`, *outside the repo*):

| subset | size |
|---|---|
| whole working tree | 2.4 MB (`src` 860 KB, `python` 712 KB, `doc` 288 KB, `tests` 228 KB) |
| `src/` only | 860 KB |
| the 26 files the tempo path compiles | ~300 KB |

All options fit comfortably in 8.5 GB, so this was never a disk-forced decision;
the smallest defensible tree was still chosen for auditability (below).

**Measured after vendoring:**

```
$ du -sh third_party/aubio
980K  third_party/aubio        # 715 KB of content, 115 source/header files
$ df -h /home
/dev/sda1  197G  181G  8.4G  96% /
```

Repo impact is **+980 KB** (rounded by the filesystem; <0.02 % of the free
space). Scratch clones and builds live in `/tmp/opencode` and are deleted on
completion; they are not part of the deliverable.

## Vendoring decisions

**Chosen: vendor the complete upstream `src/` tree verbatim (byte-identical),
compile a curated 26-file subset, and generate `config.h`.**

- `third_party/aubio/src/` is **byte-identical to upstream**. Verified with
  `diff -r third_party/aubio/src <upstream>:src` — no source patches. The whole
  `src/` (860 KB) is kept rather than pruned so that the audit is a one-line
  `diff -r` and so a future worker can see the real upstream file set. This is
  smaller than the 9.6 MB libsamplerate table already vendored for BTrack, and
  matches BTrack's own precedent (vendor the tree, build a subset).
- **Left out:** `python/`, `doc/`, `tests/`, `examples/`, `scripts/`, `wscript`,
  `Makefile`, `setup.py`, packaging metadata — none contain C sources the
  tempo/onset path uses, and together they are ~1.5 MB of the 2.4 MB tree.
  `COPYING` is retained so the licence travels with the code.
- **Pinned:** `third_party/aubio/PINNED_SHA` = `90bd27a23123fcc524c31787c9c8fc0ae4c79378`
  (annotated tag `0.4.9`, tag object `319186517bfc0dd5c015241c091b849f209e0483`).
- **Licence:** GPL-3.0-or-later (`COPYING`, © 2003-2019 Paul Brossier and
  contributors). Compatible with this project's AGPLv3 only inside the
  already-open path (SPEC.md §25.6). Same structural boundary as BTrack:
  `third_party/aubio` is referenced only from `src/aubio/`; nothing under
  `src/jam/` includes an aubio header (verified by grep); `jam-core` builds with
  `JAM_ENABLE_AUBIO=OFF` and produces **zero aubio symbols** (verified, below).
- **No new external dependency.** With no FFT/IO backend macros defined, aubio
  falls through to its bundled Ooura FFT (`src/spectral/ooura_fft8g.c`).
  FFTW3/IPP/Accelerate/libsndfile/libav are neither vendored nor enabled. The
  only library is `-lm`, already used.
- **Compiled subset (26 files):** core vectors, `spectral/{fft,phasevoc,
  specdesc,ooura_fft8g,awhitening,statistics}`, `onset/{onset,peakpicker}`,
  `tempo/{tempo,beattracking}`, `utils/{hist,scale,parameter,log,windll}`,
  `temporal/{biquad,filter,a_weighting,c_weighting}`. `io/`, `pitch/`, `notes/`,
  `synth/` are vendored but not compiled (io needs libsndfile/libav; none is on
  the tempo path). The list is explicit in `third_party/aubio/CMakeLists.txt`,
  not globbed, so a licence review covers exactly what links.
- `config.h` is **generated** by `configure_file()` from `config.h.cmake`, not
  committed — the same pattern BTrack uses for libsamplerate. `src/` stays
  pristine.

`third_party/aubio/VENDORED-PATCHES.md` records all of the above, plus the three
adapter-relevant findings below, in the BTrack format.

## Sample-rate decision

**Decision: feed audio at the true device rate with a fixed 512-sample hop. No
resampling, no hop scaling.**

### Why this is correct for aubio (and different from BTrack)

aubio has **no hard-coded analysis rate**. Its beat tracker works in
detection-function (DF) frames of `hop_size` samples:

- the 120 BPM prior is `rayparam = 60 * samplerate / 120 / hop_size`
  (`beattracking.c:65`);
- the tempo is `60 * samplerate / (hop_size * bp)` (`beattracking.c:424`).

Both scale with the rate, so a fixed hop is unbiased at any rate. This is also
exactly how upstream's own example uses it: `examples/aubiotrack.c` hard-codes
`buffer_size = 1024; hop_size = 512` for every input rate. The failure mode is
*declaring the wrong rate*: telling aubio `44100` while feeding 48 kHz audio
biases the estimate by `44100/48000 - 1 = -8.1 %`.

### Why not scale the hop

The obvious alternative — scale the hop to keep the DF frame duration at
aubio's 44.1 kHz value (`hop = round(512 * rate / 44100) = 557` at 48 kHz) — was
**measured and is worse** on this corpus. It changes aubio's autocorrelation
window `next_power_of_two(5.8 * rate / hop)` (`tempo.c:188`): at 48 kHz / 512
that is 1024 DF frames (~10.9 s), while 48 kHz / 557 gives 512 DF frames
(~6.0 s). The shorter window loses the contentious fixtures:

| 48 kHz config (raw aubio, whole corpus, nominal/`bpmStart` truth) | mean \|relerr\| | core mean | core max | fixtures >10 % error |
|---|---|---|---|---|
| **win 1024 / hop 512 (chosen)** | 2.34 % | **1.26 %** | **1.4 %** | accelerando (ramp) |
| win 1024 / hop 557 (scaled) | 7.52 % | 7.61 % | 32.8 % | accelerando, clean_sixteenths, palm_mute_metal, power_chords_distorted, ritardando |

(These are *not* sample-rate bias — both configs declare the rate correctly; they
differ in the analysis window. The chosen config keeps aubio's own default hop.)

### Unbiasedness evidence (adapter, synthetic click train, 128-frame blocks)

| true BPM | 48 kHz reported | rel err | 44.1 kHz reported | rel err |
|---|---|---|---|---|
| 80 | 80.626 | +0.78 % | 80.717 | +0.90 % |
| 100 | 101.019 | +1.02 % | 101.189 | +1.19 % |
| 120 | 121.599 | +1.33 % | 121.938 | +1.61 % |
| 126 | 127.680 | +1.33 % | 128.126 | +1.69 % |
| 140 | 142.022 | +1.44 % | 142.405 | +1.72 % |
| 160 | 162.752 | +1.72 % | 162.757 | +1.72 % |

The estimator rounds the beat period to the DF-frame grid and carries a small
**systematic positive bias of +0.8 % to +1.7 %**, roughly independent of rate;
48 kHz and 44.1 kHz agree to <0.4 %. **Tolerance: ±4 %** (`kClickTempoToleranceRel`)
— it covers the measured worst case with ~2.3× headroom and still catches the
−8.1 % mis-declared-rate trap by >2×. Tests 1 and 2 pin this at 48 kHz and
44.1 kHz respectively.

### The hop in device samples

`AubioBackend::analysisHopSamples()` is fixed at 512;
`AubioBackend::deviceHopSamples()` returns it. `framingLatencySeconds(rate)` is
`512 / rate` (10.67 ms at 48 kHz).

## Phase decision

**Use aubio's exposed beat *position*; do not free-run a phase.**

aubio exposes:

- `aubio_tempo_get_last()` — sample time of the most recent beat, including the
  sub-hop offset `ROUND(frac * hop_size)` computed causally in
  `aubio_tempo_do()` (`tempo.c:92-99`);
- `aubio_tempo_get_period()` — beat period in samples (`hop_size * bp`).

There is **no** `aubio_tempo_get_phase()` and no phase getter in
`beattracking.h` (checked in the pinned tree). So the adapter normalises the
exposed beat position against the exposed period: a beat re-arms an anchor
(`anchorDevice`, `periodSamples`), and at any observation the phase is
`fmod((frame.sampleTime - anchorDevice) / periodSamples, 1)`, wrapped into
`[0,1)`.

This is stronger than BTrack's adapter, which must re-arm an invented phase at
the block boundary because BTrack exposes nothing. It is weaker than a dedicated
phase output, and both are stated plainly in `AubioBackend.h` and
`VENDORED-PATCHES.md`.

- `phaseValid` is **false before the first beat** (no anchor exists) and
  **false during held silence**; true otherwise.
- `beatPhase01` is measured at `observations.inputSampleTime`, so the Musical
  Clock's `applyPhaseCorrection()` (`MusicalClock.cpp:372`) compares like with
  like. (BTrack labels an end-of-block phase with the block-start time; the
  aubio adapter avoids that half-block offset.)
- Test 8 pins: false before the first beat, `beatPhase01 ∈ [0,1)` always, a beat
  re-arms phase to exactly 0, `phaseValid` becomes true after acquisition.

## Causality decision

**Mode used: the causal online API only — `aubio_tempo_do()` and pure getters.
There is no blocking or non-causal prediction call.**

The brief warned about `aubio_beattracker_set_btstate`/`get_btstate` and a
non-causal "prediction" mode. **Neither exists in aubio 0.4.9**: grep for
`btstate`/`set_state`/`get_state` over `src/` returns nothing. The public entry
point is `aubio_tempo_do()` (`tempo.c:57`), which consumes exactly one hop and
returns. What *does* exist is `aubio_beattracking_checkstate()`
(`beattracking.c:286`), the context-dependent-model state machine, called
**inline** from `aubio_beattracking_do()` (`beattracking.c:183`) — it is not a
wait. The adapter calls only:

`new_aubio_tempo`, `aubio_tempo_set_silence`, `aubio_tempo_do`,
`aubio_tempo_get_last`, `get_period`, `get_bpm`, `get_confidence`,
`del_aubio_tempo` (plus `new_fvec`/`del_fvec`). None can block.

Proved at the seam by test 9 (`beatsAreNeverWithheldForFutureAudio`), two ways:

1. every emitted beat's `inputSampleTime` is `<=` the end-of-frame device time
   of the observation that carried it, and the beat is at most one hop plus one
   block old (`frameEnd - beatTime <= 512 + n`) — a beat never points at audio
   not yet received, and is never held for more than the accumulator depth;
2. a shorter run's beat stream is an **exact prefix** of a longer run's, so no
   already-received beat is deferred or rewritten when future audio arrives.

## Measured metrics

**Latency.** Adapter framing latency is **one hop = 512 samples = 10.67 ms** at
48 kHz (the accumulator releases a beat when the hop containing it completes;
partial hops are not forwarded). Measured beat *alignment* on a 40 s, 120 BPM
click train at 48 kHz / 128-frame blocks: `n=64`, **max |error| = 79.9 ms**,
mean = −26.1 ms, least-squares slope = **−0.27 ms/s** (i.e. bounded — aubio
re-anchors its grid periodically; there is no monotone accumulation). For scale
one beat period at 120 BPM is 500 ms, so max error is 16 % of a beat.

**CPU.** RTF **0.0080** (0.80 % of one core) — the 19 corpus fixtures,
218.3 s of 48 kHz audio processed in ~1.75 s CPU at `-O2`, 128-frame blocks,
measured with `std::clock()`. For reference BTrack's adapter measured RTF 0.030
on the same class of machine; aubio is ~3.8× cheaper here, but that is a machine
and build-flag comparison, not a claim of superiority (DEVPLAN).

**Allocation.** `process()` adds **zero** C++ heap allocations, proved in test 11
by counting raw `operator new`/`delete` and requiring exactly 0 for both
128-frame and 512-frame framing. aubio's causal path makes no `malloc` either:
every buffer and FFT scratch is created in `new_aubio_tempo()` in `reset()`
(inspected; `aubio_tempo_do` and its callees only read/write preallocated
`fvec_t`/`cvec_t`). So unlike BTrack's ~4 KiB-per-hop pass-by-value allocation,
aubio contributes none per hop.

**Corpus beat rate and BPM** (adapter, 48 kHz, 128-frame blocks, truth =
`nominalBpm` or, for ramps, `bpmStart`):

| fixture | truth | reported | rel err | beats | beats/s |
|---|---|---|---|---|---|
| accelerando (ramp 100→146) | 108* | 129.74 | +20.1 %* | 19 | 1.50 |
| arpeggio | 120 | 121.69 | +1.4 % | 17 | 1.50 |
| blues_shuffle | 108 | 109.26 | +1.2 % | 17 | 1.36 |
| clean_eighths | 126 | 127.60 | +1.3 % | 17 | 1.56 |
| clean_sixteenths | 126 | 127.54 | +1.2 % | 18 | 1.66 |
| compound_6_8 | 96 | 96.90 | +0.9 % | 11 | 1.24 |
| line_input_clipping | 126 | 127.48 | +1.2 % | 18 | 1.66 |
| line_input_low_level | 126 | 127.41 | +1.1 % | 7 | 0.64 |
| missing_downbeats | 120 | 121.69 | +1.4 % | 17 | 1.50 |
| noisy_microphone | 126 | 127.51 | +1.2 % | 18 | 1.66 |
| palm_mute_metal | 126 | 127.50 | +1.2 % | 5 | 0.46 |
| power_chords_distorted | 126 | 127.56 | +1.2 % | 16 | 1.47 |
| ritardando (ramp 146→100) | 152* | 146.81 | −3.4 %* | 25 | 1.94 |
| sparse_single_notes | 112 | 113.45 | +1.3 % | 16 | 1.33 |
| stop_start | 132 | 133.65 | +1.2 % | 17 | 1.07 |
| sustained_chords | 96 | 97.28 | +1.3 % | 10 | 0.88 |
| syncopated_funk | 112 | 113.20 | +1.1 % | 17 | 1.41 |
| tapping_muting_only | 112 | 113.30 | +1.2 % | 9 | 0.75 |
| waltz_3_4 | 138 | 140.02 | +1.5 % | 14 | 1.53 |

\* The two ramp fixtures are a linear tempo change; a single-point truth is not
meaningful, so their large "errors" are the ramp, not bias. Across the **11
`core` fixtures SPEC §19 is phrased against, mean |rel err| = 1.26 %, max =
1.4 %** — inside SPEC §19's *"locked BPM relative error <= 2 % on steady-tempo
core fixtures"* target. Notable comparisons: `compound_6_8` is read correctly as
the dotted quarter (96 BPM), where BTrack locked to triple time (+45 %);
`clean_sixteenths` and `power_chords_distorted` stay on the beat instead of
double time.

## CMake wiring required

`third_party/aubio/CMakeLists.txt` defines the `option(JAM_ENABLE_AUBIO)` and
the `aubio` and `jam-aubio` targets. `jam-core/CMakeLists.txt` is
orchestrator-owned; apply two changes, mirroring the BTrack wiring exactly.

**(1) Exclude the suite from the dependency-free glob** (next to the existing
`REMOVE_ITEM` for BTrackBackendTests.cpp at line 51):

```cmake
list(REMOVE_ITEM JAM_TEST_SOURCES "${JAM_ROOT}/tests/jam/AubioBackendTests.cpp")
```

**(2) Add the optional backend block** (after the `if (JAM_ENABLE_BTRACK) ... endif()`):

```cmake
# Optional GPL third-party tracker backend (TRACK-002). Same licence boundary
# and OFF-by-default policy as JAM_ENABLE_BTRACK: src/aubio/ is the only place
# aubio is referenced, and jam-core must build with this OFF and produce zero
# aubio symbols.
option(JAM_ENABLE_AUBIO "Build the GPL aubio backend behind IRhythmTracker" OFF)
if (JAM_ENABLE_AUBIO)
    add_subdirectory(${JAM_ROOT}/third_party/aubio ${CMAKE_CURRENT_BINARY_DIR}/aubio)
    if (EXISTS ${JAM_ROOT}/tests/jam/AubioBackendTests.cpp)
        enable_testing()
        add_executable(jamAubioTests
            ${JAM_ROOT}/tests/jam/JamTestMain.cpp
            ${JAM_ROOT}/tests/jam/AubioBackendTests.cpp)
        target_include_directories(jamAubioTests PRIVATE
            ${JAM_ROOT}/tests/jam ${JAM_ROOT}/tests)
        target_link_libraries(jamAubioTests PRIVATE jam-aubio)
        if (NOT MSVC)
            target_compile_options(jamAubioTests PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME jam.AubioBackend COMMAND jamAubioTests AubioBackend.)
    else()
        message(STATUS "jam-core: JAM_ENABLE_AUBIO=ON but no AubioBackendTests.cpp yet")
    endif()
endif()
```

Notes for the applier:

- `third_party/aubio/CMakeLists.txt` also declares the same `option()` and
  `return()`s when OFF, so a bare `add_subdirectory` is harmless; the option is
  declared here so `if (JAM_ENABLE_AUBIO)` is defined before it is read (the
  BTrack block does the same).
- The `jam-aubio` target defines `JAM_AUBIO_ENABLED=1` **PUBLIC**; that is the
  discriminator that makes `tests/jam/AubioBackendTests.cpp` compile to a no-op
  everywhere except `jamAubioTests`. Without change (1) the file still compiles
  to nothing in `jamTests` (verified), but change (1) is what keeps the target
  graph honest.
- `add_test` name is exactly `jam.AubioBackend`, command filter `AubioBackend.`.
- Suggested build/test invocation:
  `cmake -S jam-core -B build -G Ninja -DJAM_ENABLE_AUBIO=ON && cmake --build build && ctest --test-dir build --output-on-failure`

I validated this exact fragment in a throwaway CMake project that mimics
`jam-core` (building the real `src/jam`, `third_party/aubio`, `src/aubio` and
the real test file):

```
-- jam-aubio: aubio 0.4.9 (GPLv3-or-later) enabled — ...
[36/37] Linking CXX executable jamAubioTests
1/1 Test #1: jam.AubioBackend .................   Passed    2.63 sec
100% tests passed out of 1
```

OFF build, same project: `find boff -iname '*aubio*'` → none;
`nm boff/libjam-core.a | grep -i aubio` → zero aubio symbols.

## Tests executed / Test results

**Standalone (fully within my control; no CMake), from
`/tmp/opencode/abtest`:**

```bash
# 26 aubio C objects (gcc -std=c99 -DHAVE_CONFIG_H=1, generated config.h)
# then:
g++ -std=c++17 -Wall -Wextra -Wpedantic -DJAM_AUBIO_ENABLED=1 \
    -I<generated-config> -I third_party/aubio/src -I src -I tests/jam -I tests \
    -c src/aubio/AubioBackend.cpp
g++ <same flags> -c tests/jam/AubioBackendTests.cpp
g++ ... tests/jam/JamTestMain.cpp
g++ obj/*.o -lm -o run && ./run AubioBackend.
```

Result — **all 11 cases green, 38948 checks, 0 failures**:

```
[suite] AubioBackend
  PASS AubioBackend.tempoIsUnbiasedOnAClickTrainAt48k
  PASS AubioBackend.tempoIsUnbiasedOnAClickTrainAt44k1
  PASS AubioBackend.resetIsDeterministicFieldForField
  PASS AubioBackend.beatStreamIsIndependentOfBlockFraming
  PASS AubioBackend.trailingPartialBlockIsCarriedNotFabricated
  PASS AubioBackend.idIsStableAndNonEmpty
  PASS AubioBackend.digitalSilenceProducesNoBeatsAndNoTempo
  PASS AubioBackend.phaseIsBoundedAndReArmsOnBeat
  PASS AubioBackend.beatsAreNeverWithheldForFutureAudio
  PASS AubioBackend.beatTimingDoesNotAccumulateLag
  PASS AubioBackend.allocationsAreZeroOnTheProcessPath

11 tests, 38948 checks, 0 failed check(s) in 0 test(s)
```

**CMake validation (the fragment above):** `ctest` → `1/1 jam.AubioBackend
Passed`.

**Warnings:** `src/aubio/AubioBackend.cpp` and
`tests/jam/AubioBackendTests.cpp` both compile with **0 warnings** under
`-Wall -Wextra -Wpedantic` (the test's global `new`/`delete` replacement
suppresses the known GCC `-Wmismatched-new-delete` false positive in-file, as
BTrack's test does). Third-party aubio objects compile with `-w`.

**Regression:** the real `jam-core` default (OFF) build is untouched and green —
6/6 suites (`AnalysisAudioRing`, `DrumTransportAdapter`, `MusicalClock`,
`RhythmCorpus`, `RhythmEvalMetrics`, `RtSignal`); my test file, still globbed
into `jamTests`, compiles to an 816-byte empty object and changes nothing.

## Evidence

**Prove it can fail** — break the sample-rate declaration in a `/tmp` copy of the
adapter (`static_cast<uint_t>(std::lround(r))` → `static_cast<uint_t>(44100)`,
i.e. declare 44.1 kHz for 48 kHz audio), rebuild, run:

```
=== BROKEN: 48k test ===
    FAIL .../AubioBackendTests.cpp:260
      CHECK_NEAR failed: lastPositiveBpm (r) ~= 120.0
    actual:   111.771950
    expected: 120.000000
    tolerance: 4.800000
  FAIL AubioBackend.tempoIsUnbiasedOnAClickTrainAt48k
1 tests, 1 checks, 1 failed check(s) in 1 test(s)   exit=1

=== BROKEN: 44.1k test (should still pass; 44100 is correct there) ===
  PASS AubioBackend.tempoIsUnbiasedOnAClickTrainAt44k1   exit=0
```

Revert (the real file was never modified; the break lived in `/tmp`), and the
full suite is green again (results above). This is the same shape as TRACK-001's
evidence: the 48 kHz case detects the trap, the 44.1 kHz case correctly does
not. (The observed error is −6.9 %, not the theoretical −8.1 %, because aubio's
+1.4 % inherent bias partly offsets it; still >1.7× the ±4 % tolerance.)

**Vendored-source integrity:**

```
$ diff -r third_party/aubio/src <upstream 0.4.9>:src
src is byte-identical to upstream
```

**Boundary:** `grep -rn aubio src/jam/` → nothing; OFF build produces zero aubio
symbols (above).

## Known limitations

1. **Phase is derived from aubio's beat *position*, not a phase output.** aubio
   0.4.9 has no phase getter, so the adapter normalises `get_last()` against
   `get_period()`. This is more than BTrack gives and less than a dedicated
   phase field; it is stated as such in the header.
2. **Beat timing oscillates, bounded at ~16 % of a beat period.** aubio's grid
   carries the +0.8…+1.7 % period bias and periodically re-anchors; measured max
   |error| 79.9 ms at 120 BPM over 40 s (no accumulation, slope −0.27 ms/s).
   This is a real aubio property the F-measure/phase metrics will see, not an
   adapter artefact.
3. **Low-transient fixtures yield few beats.** `palm_mute_metal` (5 beats),
   `line_input_low_level` (7), `tapping_muting_only` (9), `sustained_chords`
   (10) over ~11 s: aubio still reports the right tempo but fires few
   confirmations. Worth scoring explicitly in the ADR.
4. **`aubio_tempo_get_last()` is a 32-bit counter.** aubio's internal
   `total_frames` wraps at 2^32 samples (~24.8 h at 48 kHz). The adapter tracks
   the wrap and keeps the device timeline monotone; a session beyond 24.8 h is
   otherwise unmeasured.
5. **No `RhythmObservation` change was needed to meet the contract.** A latency
   field would still be *useful* — see Integration notes — but aubio fits the
   frozen seam fairly, so this is a proposal, not a blocker.
6. `compound_6_8`, `accelerando`, `ritardando` were run with a single-point
   truth for reporting only; they are ramp/meter fixtures and must be scored by
   whatever method the harness defines, not by this table.

## Integration notes

- **What ANALYSIS-001 must provide.** Call `reset(deviceRate)` once, then feed
  contiguous `AnalysisFrame`s of mono audio at exactly that rate with
  `sampleTime` = the device sample clock of the first sample. The adapter does
  no resampling; block sizes 1..2048 are supported (128 is ideal).
- **`sampleRate` to pass to `reset()`.** The device rate as configured (48 kHz
  in the reference rig), **not** 44.1 kHz. `reset()` clamps to `[8000,192000]`;
  `sourceSampleRate` echoes the rate passed.
- **Latency to expect / fairness.** Adapter framing 10.67 ms at 48 kHz (one
  hop); measured beat alignment mean −26 ms, max ~80 ms. The harness must
  compensate for a backend's latency before scoring beat events, or it will
  reward/penalise by the wrong asymmetry — the same problem TRACK-001 flagged
  for BTrack's ~23 ms.
- **How EVAL-002 should interpret the evidence.**
  - `bpmCandidate` is aubio's continuous estimate clamped to `[40,240]`, present
    from the first period estimate (not only after a beat) and held through
    silence; aubio carries a ≈+1 % positive bias on this corpus.
  - `beatEvent` is true iff aubio emitted a tactus this block **and** the held
    silence state is clear; `inputSampleTime` on a beat is the exact detected
    beat time (device clock).
  - `phaseValid` is false before the first beat and during held silence; phase
    is normalised from aubio's beat position, so score it against aubio's own
    beat events.
  - `silence` is a 50 ms held state at −60 dBFS. aubio's own internal silence
    gate is left at its default −90 dBFS so its grid is not suppressed through
    quiet-but-real passages; the -60 gate is applied at the observation level.
  - Allocation attribution: aubio contributes **zero** per-hop allocation
    (unlike BTrack's ~4 KiB).
- **Proposed (NOT applied) change to the frozen type.** `RhythmObservation` has
  no latency field, so a fair G3 comparison must carry latency out-of-band.
  A minimal, non-breaking addition would be:
  `float processingLatencySamples = 0.0f;` (device samples from the last input
  sample of the block to the audio instant the observation refers to), set to
  ≤512 by this adapter and ~1024·(rate/44100) by BTrack's. I did **not** edit
  `RhythmTypes.h`; reporting it per the brief.

## Final commit SHA

- Deliverables commit: `fef77ec8f63454ebb703d3fb9529d57d45f13bce` —
  *feat(track-002): vendored aubio 0.4.9 backend behind IRhythmTracker*
- SHA-reporting commit: the commit that fills in the line above (this file is
  the only change).
