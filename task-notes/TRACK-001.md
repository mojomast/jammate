# TRACK-001

## Goal

Implement `jam::BTrackBackend : jam::IRhythmTracker` so the vendored BTrack 1.0.7
can be run over the guitar corpus as the first candidate in the SPEC.md §12
tracker shootout, with tests, an explicit sample-rate decision, a deliberate
phase decision, and measured metrics.

## Base commit

`a8da4f2` — *feat(deps): vendor BTrack 1.0.7 behind an optional,
licence-explicit seam*

Branch `wp/TRACK-001`. Worktree `/home/mojo/projects/worktrees/TRACK-001`.

## Files changed

Created only (nothing existing was modified):

- `src/btrack/BTrackBackend.h` — interface + config; no GPL header leaks.
- `src/btrack/BTrackBackend.cpp` — the adapter; the only TU that includes
  `BTrack.h`.
- `tests/jam/BTrackBackendTests.cpp` — suite `BTrackBackend` (ctest
  `jam.BTrackBackend`), 10 cases.
- `task-notes/TRACK-001.md` — this file.

`git status` is clean and the diff touches no file under `src/jam/`,
`third_party/`, or any CMake file. See **Known limitations** for the two
infrastructure defects this exposed.

## Contract implemented

- `reset(double sampleRate)` accepts any rate in `[8000, 192000]`, is
  deterministic (test 3 compares every field of every observation across two
  runs), and is the only place that allocates.
- `process(const AnalysisFrame&)` consumes up to `kMaxAnalysisBlock` (2048)
  mono samples, returns exactly one `RhythmObservation`, and never blocks.
- `id()` returns the stable string `"btrack"`.
- The backend supplies **evidence**: `bpmCandidate`, `beatEvent`,
  `beatPhase01`/`phaseValid`, `beatConfidence01`, `onsetStrength01`,
  `transientDensity01`, `energyRmsDbfs`, `silence`, `inputSampleTime`. It never
  writes a tempo.
- Preallocated in `reset()`; no allocation is added on the analysis path by the
  adapter (test 10). The silence threshold, BPM clamp and confidence scale live
  in `BTrackBackendConfig`, not as scattered literals.

## Sample-rate decision

**Decision: (b) — resample the incoming audio to 44.1 kHz and feed BTrack its
native hop 512 / frame 1024 at that rate.** Hop scaling (option a) is
*ineffective* for BTrack 1.0.7, and VENDORED-PATCHES.md's claim that the two are
equivalent is wrong; see the derivation below.

### Why (a) cannot work

`BTrack::resampleOnsetDetectionFunction()` (BTrack.cpp:356-389) resamples the
internal onset-detection buffer to a **fixed 512 points**. That buffer holds
`onsetDFBufferSize = 512·512 / hop` ODF samples, each one analysis hop long, so
its time span is `512·512 / sampleRate` seconds and the resampled point spacing
is therefore

```
(512·512 / fs) / 512 = 512 / fs   seconds, independent of hop.
```

`calculateTempo()` (BTrack.cpp:394) hard-codes `tempoToLagFactor = 60·44100/512`,
i.e. it assumes point spacing `512/44100`. The mapping is only correct when the
ODF is at **44.1 kHz**; changing the hop does not change the point spacing, so it
cannot remove the bias. Measured (scratch copy with the resample restored), a
120 BPM click train / the 126 BPM `clean_eighths` fixture:

| fed at | hop | reported BPM | error |
|---|---|---|---|
| 44.1 kHz | 512 | 123.05 | −2.34 % |
| 48 kHz | 512 (uncorrected) | 114.84 | **−8.85 %** |
| 48 kHz | 557 (`512·48000/44100`) | 113.11 | **−10.23 %** |
| 48 kHz | 559 | 112.70 | −10.55 % |

Hop scaling is not just unhelpful, it is worse (coarser ODF), exactly as the
`512/fs` derivation predicts. Only presenting the ODF at 44.1 kHz fixes it.

### Numbers

| device rate | resample? | BTrack rate | hop | frame | device hop | adapter latency |
|---|---|---|---|---|---|---|
| 44 100 | no (`step = 1`) | 44 100 | 512 | 1024 | 512 | 23.22 ms (one frame) |
| 48 000 | yes → 44 100 | 44 100 | 512 | 1024 | ≈557.3 | 23.22 ms + ~1 device sample |

- Analysis latency: one BTrack frame = `1024/44100` = **23.22 ms** worst case;
  the beat predictor keeps measured *onset* alignment inside one hop
  (`512/44100` = **11.61 ms**), asserted by test 9. The resampler is a causal
  linear interpolator adding ~1 device sample (< 0.03 ms at 48 kHz).
- CPU cost: the resampler is 1 multiply-add per output sample; measured
  end-to-end **RTF 0.030** (60 s of audio processed in 1.80 s ≈ 3 % of one core)
  at 48 kHz / 128-frame blocks on this machine.
- Bias evidence: tests 1 and 2 assert the reported BPM is within **±4 %** of a
  known click-train BPM at 48 kHz and 44.1 kHz. The tolerance is the measured
  worst case across 90-140 BPM click trains at 44.1 kHz (−2.9 %, BTrack quantises
  to a ~2 BPM grid and carries a small systematic negative bias) plus headroom;
  it still catches the −8.8 % trap by >2×. The corpus agrees: steady fixtures
  land within −2.4 % after the resample (see Measured metrics).

The adapter is **not** hard-coded to 44.1 kHz or 48 kHz: `reset(r)` sets
`step = r/44100`, so any supported device rate is resampled correctly, and
`deviceHopSamples()` exposes the resulting device-domain hop.

### The load-bearing vendor defect (blocks the suite — not edited)

`BTrack::processOnsetDetectionFunctionSample()` **calls**
`resampleOnsetDetectionFunction()` on every beat (BTrack.cpp:259), but vendor
patch 1 wrapped the function body in `#if BTRACK_WITH_LIBSAMPLERATE` and that
macro is 0, so the function is a **no-op** (`#else (void) this;`). Its own
comment calls that path "unreachable by construction"; it is not. The consequence
is that `resampledOnsetDF` is never written, `calculateTempo()` runs on all
zeros, and BTrack reports a fixed **79.5 BPM** (and free-runs beats at that
tempo) for *every* input — at any hop, any sample rate, with a working adapter
or not. Demonstrated with a raw BTrack probe:

```
44100 Hz, hop 512, true 120 BPM -> final tempo 79.507
48000 Hz, hop 512, true 120 BPM -> final tempo 79.507
48000 Hz, hop 557, true 120 BPM -> final tempo 80.516
44100 Hz, hop 512, true 140 BPM -> final tempo 79.507
```

VENDORED-PATCHES.md §"Patch 1" says the function is "used only by the non-causal,
offline beat-time helpers". That audit missed line 259. **BTrack 1.0.7 cannot
track at all as vendored.** The minimal third patch is to restore the causal
identity copy at BTrack.cpp:386 — 3 lines, inside the pinned file, so by the
task's instruction it is **reported here and not applied**:

```cpp
#else
    // at hop 512 / fs 44100, onsetDFBufferSize == 512, so this is upstream's
    // identity copy of the ODF buffer that calculateTempo() reads.
    for (int i = 0; i < 512; ++i)
        resampledOnsetDF[i] = (i < onsetDFBufferSize) ? onsetDF[i] : 0.0;
#endif
```

(The adapter only ever presents 44.1 kHz at hop 512, so the identity copy is the
exact fix for this product; the general libsamplerate path is unnecessary here.)
With that one fix applied **in a /tmp copy only**, the whole suite is green
(evidence below).

## Phase decision

**Derived phase (not fabricated), and `phaseValid` is false when it is not
trustworthy.**

BTrack exposes `beatDueInCurrentFrame()` (a bool) and *no phase whatsoever*
(VENDORED-PATCHES.md; BTrack.h:81). Rather than invent one, the adapter derives
phase from the timing of BTrack's beat events relative to its estimated beat
period: a beat re-arms `beatPhase01` to 0, and between beats the phase advances
by `blockDuration / beatPeriod` using the current BPM. `phaseValid` is true only
once a beat has established a tempo **and** the adapter is not in silence; before
the first beat, and during silence, it is false so the Musical Clock will not
lock on phase-free evidence (SPEC.md §9.3). Test 8 pins: false before the first
beat, `beatPhase01 ∈ [0,1)` always, a beat re-arms phase to exactly 0, and
`phaseValid` becomes true after acquisition. This is a real measurement of where
the clock is in the beat, not a value the algorithm does not support.

## Framing / buffering model

- `AnalysisFrame` carries ≤2048 mono samples at an arbitrary block size. The
  adapter accumulates analysis samples in a preallocated 512-double `hopBuf`
  and calls `BTrack::processAudioFrame` once per complete 512-sample hop; the
  partial remainder is carried across blocks (tests 4 and 5). At 48 kHz the
  input is first resampled to 44.1 kHz with a persistent-phase linear
  interpolator (`step = rate/44100`, one `prevIn` history sample), so the same
  underlying audio yields the same hop sequence regardless of block size.
- `inputSampleTime` is the **device sample clock**, never a frame counter:
  - for a normal observation it is `frame.sampleTime`;
  - for a beat it is the device time of the analysis hop that produced it,
    `captureStart + round(hopStartAnalysisIndex · step)`, which is
    framing-independent (proved by test 4 comparing beat streams for block sizes
    1/127/128/512/1024/2048). This is the value the Musical Clock compares
    against `advance()` on the device timeline.
- `sourceSampleRate` is the rate passed to `reset()` (the rate the backend was
  fed, per the IRhythmTracker.h contract); `frame.sourceSampleRate` is assumed
  equal to it.
- Silence is a **held** state: block RMS below the configured threshold
  (`-60 dBFS`) latches silence for 50 ms. This is longer than the 23 ms framing
  latency, so a beat emitted just after its onset is not misclassified as a beat
  in silence, while a genuinely silent gap suppresses BTrack's free-running
  cumulative-score predictions (SPEC.md §19). Beats are suppressed whenever the
  held silence flag is set.
- Maximum end-to-end framing latency: `framingLatencySeconds()` = **23.22 ms**
  (one 1024-sample analysis frame); best case one hop (11.61 ms).

## Measured metrics

- **Latency**: adapter framing 23.22 ms worst case / 11.61 ms best case; the
  resampler adds ~1 device sample. Test 9 asserts every post-acquisition beat's
  `inputSampleTime` is within one hop (11.61 ms) of the true onset and that the
  mean error does not drift between the first and last fifth of a 40 s run.
- **CPU**: RTF 0.030 (60 s audio / 1.80 s wall ≈ 3 % of one core) at
  48 kHz/128 on the dev machine.
- **Allocation**: the adapter adds **zero** allocations per block — proved in
  test 10 by measuring the same audio as 768×128-sample blocks and 192×512-sample
  blocks and getting *identical* allocation counts. The count is non-zero because
  upstream BTrack passes its cumulative-score `CircularBuffer` **by value** into
  `calculateNewCumulativeScoreValue()` (BTrack.cpp:743), heap-copying 4 KiB once
  per hop (~86/s at 44.1 kHz). This is permitted on the analysis thread but must
  be reported to EVAL-002's allocation metric; it is a BTrack property, not the
  adapter's. It is not fixed here (pinned vendor).
- **Corpus beat-event rate / BPM** (corrected BTrack, 48 kHz corpus resampled by
  the adapter to 44.1 kHz, 128-frame blocks):

  | fixture | truth | reported | rel err | beats/s |
  |---|---|---|---|---|
  | clean_eighths | 126 | 123.05 | −2.34 % | 1.93 |
  | clean_sixteenths | 126 | 123.05 | −2.34 % | 2.02 |
  | blues_shuffle | 108 | 105.47 | −2.34 % | 1.77 |
  | arpeggio | 120 | 117.45 | −2.12 % | 1.94 |
  | palm_mute_metal | 126 | 123.05 | −2.34 % | 1.56 |
  | power_chords_distorted | 126 | 123.05 | −2.34 % | 1.93 |
  | noisy_microphone | 126 | 123.05 | −2.34 % | 2.02 |
  | syncopated_funk | 112 | 109.96 | −1.82 % | 1.74 |
  | sustained_chords | 96 | 95.70 | −0.31 % | 0.97 |
  | tapping_muting_only | 112 | 109.96 | −1.82 % | 1.58 |
  | waltz_3_4 | 138 | 136.00 | −1.45 % | 2.07 |
  | compound_6_8 | 96 | 139.67 | **+45.49 %** | 2.03 |
  | missing_downbeats | 120 | 117.45 | −2.12 % | 1.85 |
  | sparse_single_notes | 112 | 112.35 | +0.31 % | 1.82 |
  | stop_start | 132 | 129.20 | −2.12 % | 1.57 |
  | line_input_clipping | 126 | 123.05 | −2.34 % | 2.02 |
  | line_input_low_level | 126 | 126.05 | +0.04 % | 1.66 |

  `compound_6_8` locks to the eighth-note subdivision (triple time) — a real
  BTrack limitation for the ADR, not an adapter bug. Note these numbers are from
  a scratch build with the vendor fix, because the committed vendor cannot track;
  see the blocker.

## Tests executed

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
cd /home/mojo/projects/worktrees/TRACK-001
rm -rf /tmp/opencode/build-track001
cmake -S jam-core -B /tmp/opencode/build-track001 -G Ninja -DJAM_ENABLE_BTRACK=ON
cmake --build /tmp/opencode/build-track001
ctest --test-dir /tmp/opencode/build-track001 --output-on-failure; echo "exit=$?"
```

Plus the default-OFF build and a `/tmp` copy with the 3-line vendor fix.

## Test results

**Committed tree (vendor as pinned): `ctest` exit 8 — `jam.BTrackBackend`
red on the three tempo/beat-timing cases; the other five suites pass.** The
failure is entirely the vendor no-op; all 10 tracker cases are written to the
contract and pass once the vendor fix is applied (below).

```
1/6 Test #1: jam.BTrackBackend ................***Failed    6.37 sec
  FAIL BTrackBackend.tempoIsUnbiasedOnAClickTrainAt48k
      actual: 79.507210   expected: 120.000000   tolerance: 4.800000
  FAIL BTrackBackend.tempoIsUnbiasedOnAClickTrainAt44k1
      actual: 79.507210   expected: 120.000000   tolerance: 4.800000
  PASS BTrackBackend.resetIsDeterministicFieldForField
  PASS BTrackBackend.beatStreamIsIndependentOfBlockFraming
  PASS BTrackBackend.trailingPartialBlockIsCarriedNotFabricated
  PASS BTrackBackend.idIsStableAndNonEmpty
  PASS BTrackBackend.digitalSilenceProducesNoBeatsAndNoTempo
  PASS BTrackBackend.derivedPhaseIsBoundedAndReArmsOnBeat
  FAIL BTrackBackend.beatTimingDoesNotAccumulateLag
      actual: 0.247750   bound: 0.011610
  PASS BTrackBackend.allocationsComeOnlyFromBTrackAndAreFramingIndependent
10 tests, 3 failed check(s) in 3 test(s)
2/6 jam.AnalysisAudioRing Passed   3/6 jam.DrumTransportAdapter Passed
4/6 jam.MusicalClock Passed        5/6 jam.RhythmCorpus Passed
6/6 jam.RtSignal Passed
83% tests passed, 1 tests failed out of 6
```

**Same sources with the 3-line vendor fix applied in a /tmp copy only: green.**

```
1/6 jam.BTrackBackend  ... Passed
... 6/6 ...
100% tests passed out of 6
exit=0
```

**Default `-DJAM_ENABLE_BTRACK` OFF build: green and GPL-free.**

```
5/5 tests passed out of 5
exit=0
```
No `third_party/BTrack` source is compiled, and no `btrack`/`kiss_fft` library
is produced; the only `BTrack` string in the OFF build graph is the
`BTrackBackendTests.cpp` translation unit, which `#ifdef USE_KISS_FFT`s to an
empty object and links no GPL symbol.

## Evidence

**Prove the suite can fail** (equivalent of "break the hop scaling": break the
sample-rate correction in the /tmp green copy by forcing `step = 1.0`, then
revert):

```
step = 1.0; // BREAK-FOR-EVIDENCE: pretend 48 kHz is 44.1 kHz
FAIL BTrackBackend.tempoIsUnbiasedOnAClickTrainAt48k
    actual: 107.666016   expected: 120.000000   tolerance: 4.800000
1 failed check(s) in 1 test(s)  -> ctest red
... revert ...
100% tests passed out of 6
```

(At 44.1 kHz `step = 1` is correct, so that case still passes — which is the
point: the 48 kHz case is the one that detects the trap.)

**Vendor defect demonstrated at the raw BTrack API** (no adapter), see the table
in the sample-rate section: every configuration reports ~79.5 BPM regardless of
the true tempo.

**Warnings:** `src/btrack/BTrackBackend.cpp` compiles with **0 warnings** under
`-Wall -Wextra -Wpedantic`; `tests/jam/BTrackBackendTests.cpp` likewise (one
false-positive `-Wmismatched-new-delete` from the global `new`/`delete`
replacement idiom, suppressed in-file with an explanation). Third-party
`BTrack`/`kiss_fft` warnings are out of scope and were not observed during the
build.

## Known limitations

1. **The pinned BTrack cannot estimate tempo (blocker).** Patch 1 disabled the
   causal `resampleOnsetDetectionFunction()` call at BTrack.cpp:259; the
   function body is a no-op, so `calculateTempo()` sees zeros and reports a
   fixed ~79.5 BPM. This is why the committed suite is red on tests 1, 2 and 9.
   I did **not** edit `third_party/BTrack/` (task instruction). The 3-line fix
   is above and proven green in a scratch copy. Until it lands, the ON build is
   not a usable tracker and the corpus cannot be scored.
2. **`jam-core/CMakeLists.txt` double-registers the BTrack suite (blocker, not
   fixed).** Its `file(GLOB JAM_TEST_SOURCES tests/jam/*.cpp)` includes
   `BTrackBackendTests.cpp` into the dependency-free `jamTests` target and its
   suite-discovery loop greps the file for the JamTest registration macro, so
   `jam.BTrackBackend` is added twice (fatal `add_test` collision) and
   `jamTests` fails to link `BTrackBackend`. I did not edit CMake. The
   recommended one-line fix is
   `list(REMOVE_ITEM JAM_TEST_SOURCES "${JAM_ROOT}/tests/jam/BTrackBackendTests.cpp")`
   at the top of the `if (JAM_ENABLE_BTRACK)` block. So that the frozen tree is
   still buildable and testable, the test file is guarded by `#ifdef
   USE_KISS_FFT` (propagated only through the `btrack` target) and registers with
   a local `BT_TEST` macro the discovery regex does not match; outside
   `jamBTrackTests` it compiles to nothing. This should be replaced by the CMake
   fix at integration time.
3. **`VENDORED-PATCHES.md` is wrong about option (a).** It offers hop scaling
   as an equivalent alternative; the `512/fs` analysis above and the
   −10.23 % measurement show it is not. Worth correcting in the vendor README.
4. **BTrack allocates ~4 KiB per hop** (pass-by-value `CircularBuffer`), and
   **locks to triple time on compound 6/8**. Both are BTrack findings for the
   ADR, neither is hidden.
5. The corpus BPM numbers above come from the scratch build with the vendor fix;
   they cannot be reproduced from the committed tree until blocker 1 is resolved.

## Integration notes

- **What ANALYSIS-001 must provide.** Call `reset(sampleRate)` once with the
  *device* rate (e.g. `48000`), then feed `AnalysisFrame`s of mono audio at
  exactly that rate, with `sampleTime` = the device sample clock of the first
  sample and monotonically contiguous. The adapter does all resampling; do not
  pre-resample. Block sizes 1..2048 are supported (the device's 128 is ideal).
- **`sampleRate` to pass to `reset()`.** The device rate as configured, not
  44.1 kHz. `reset()` clamps to `[8000, 192000]`; `sourceSampleRate` on the
  observation echoes the rate you passed.
- **Latency to expect.** Adapter framing 23.22 ms worst case, 11.61 ms best
  case, plus ~1 device sample of resampler. Expose
  `BTrackBackend::framingLatencySeconds()` to the harness; budget for the
  23.22 ms in the analysis queue sizing.
- **How the harness (EVAL-002) must interpret the evidence.**
  - `bpmCandidate` is zero until the first beat establishes a tempo; afterwards
    it is BTrack's estimate clamped to `[40, 240]`. It is held (not zeroed)
    through silence. BTrack's own grid is 80-160 BPM, so it cannot express
    slower or faster estimates; the harness should score relative error and
    half/double-time accordingly, and note that within its range BTrack carries
    a systematic ≈−2 % bias.
  - `beatEvent` is true iff a BTrack beat fired in the block **and** the adapter
    is not in held silence; `inputSampleTime` on a beat is the device time of
    the analysis hop that produced it.
  - `phaseValid` is false before the first beat and during held silence. The
    harness must **not** score a locked phase when it is false. This is derived
    phase (BTrack has none), so phase error should be scored relative to the
    beat events the same backend emits.
  - `silence` is a held state with a 50 ms latch, threshold −60 dBFS.
  - Allocation count from EVAL-002 should be attributed carefully: ~1 × 4 KiB
    per hop is BTrack's, not the adapter's.
- **Fairness / assumptions the harness cannot infer.**
  - The adapter deliberately targets 44.1 kHz, BTrack's only unbiased reference
    rate. An alternative adapter that scales the hop instead is measurably
    biased (−10 %) even with a correct BTrack; do not compare the two as if
    they were the same algorithm.
  - Scoring assumes the caller honours `reset()`'s rate contract and that
    `sampleTime` is the true device clock. A frame counter here would corrupt
    phase; the test suite pins against it.
  - The committed vendor is broken; **no corpus numbers can be produced from
    this tree until blocker 1 is fixed.** That is the single most important
    thing for the orchestrator to action before G3.

## Final commit SHA

- Deliverables commit: `17fe481de94d6b91b1c0b084f3d177c53376c59d` —
  *feat(track-001): BTrack backend, 44.1 kHz-corrected, with tests*
- SHA-reporting commit: *the commit that fills in the line above* (this file is
  the only change).
