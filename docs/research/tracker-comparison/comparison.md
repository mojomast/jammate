# Beat-tracker comparison — EVAL-004 (timing-corrected)

Backend vs backend evidence for the tracker ADR (G3), produced from the EVAL-004
timing worktree. **This directory is new; EVAL-002R's `docs/research/results-btrack.json`,
`summary-btrack.md`, `fixtures-btrack.csv` and `per-fixture/` are untouched and
remain the historical record.** They are also, as shown below, the record of the
runner defect EVAL-004 fixes.

## What changed relative to EVAL-002R

1. `BackendRunner` no longer overwrites `RhythmObservation::inputSampleTime` with
   the frame start. A backend's causal reported device time (aubio's sub-hop
   tactus position, BTrack's analysis-hop start) is preserved; the block start is
   only a fallback for a non-causal report.
2. Every beat and tempo sample now also carries its **causal availability** (the
   block end, i.e. when `process()` returned), as a separate series. Event
   accuracy and causal availability latency are no longer confounded.
3. The true-silence false-beat counter is a **diagnostic**, not SPEC 19's
   "silence does not create false acceleration" gate; coverage is per-fixture
   (no silence -> not measured; known synthesis defect -> corpus defect), not a
   `>= 50 %` duration threshold.
4. Recovery after stop/start is measured from the end of the **real stop**
   (`trueSilenceSpans`), not from `+/-30 ms` unplayed-beat windows.
5. A missing BPM lock on a core fixture cannot let the 2 % gate "pass"; phase
   with no matched beats is labelled missing, not perfect.
6. Syncopation-stability and Follow-ramp SPEC gates are marked NOT-MEASURED
   (they need the Musical Clock and audition); their raw measures are preserved.

## Provenance

- Base commit: `eac59ba` (`build(jam-core): wire optional aubio adapter and
  isolated backend suite`), worktree `/home/mojo/projects/worktrees/EVAL-004-timing`,
  branch `wp/EVAL-004-timing`. The implementation commit SHA is recorded in
  `task-notes/EVAL-004.md`.
- Build (PATH=`/tmp/opencode/venv/bin:$PATH`), Release, GCC 14:
  ```bash
  cmake -S jam-core -B /tmp/opencode/eval004-core -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DJAM_ENABLE_BTRACK=ON -DJAM_ENABLE_AUBIO=ON
  cmake --build /tmp/opencode/eval004-core
  ctest --test-dir /tmp/opencode/eval004-core --output-on-failure     # 9/9
  cmake -S tools/rhythm-eval -B /tmp/opencode/eval004-cli -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-rdynamic
  cmake --build /tmp/opencode/eval004-cli
  tools/rhythm-eval/build-btrack-plugin.sh /tmp/opencode/eval004-core
  tools/rhythm-eval/build-aubio-plugin.sh  /tmp/opencode/eval004-core
  ```
- Runs (identical corpus, all 19 fixtures; uncompensated is always variant 0):
  ```bash
  CLI=/tmp/opencode/eval004-cli/rhythm-eval
  # primary, 128-frame analysis blocks, BTrack adapter framing latencies offered
  $CLI --corpus testdata/rhythm --out .../btrack/block128 --backend btrack \
       --backend-lib .../librhythm-eval-btrack.so --block 128 \
       --compensate-latency 0.02322 --compensate-latency 0.01161
  # primary, 128-frame, aubio framing latency (one 512-device-sample hop at 48 kHz)
  $CLI --corpus testdata/rhythm --out .../aubio/block128 --backend aubio \
       --backend-lib .../librhythm-eval-aubio.so --block 128 \
       --compensate-latency 0.0106666667
  # framing diagnostic, 512-frame blocks, uncompensated
  $CLI ... --block 512   (same per backend)
  # defect reproduction: stamp every beat at its block start (pre-EVAL-004)
  $CLI ... --block 128 --legacy-block-stamped-beats   (same per backend)
  ```
  `-rdynamic` exports the CLI's global `operator new/delete` so the counter sees
  allocations inside the `dlopen`ed plugin.

## Backend configuration is NOT identical (fair-comparison caveat)

| | BTrack | aubio |
|---|---|---|
| feed | resampled device 48 kHz -> 44.1 kHz analysis (`BTrack::calculateTempo` hard-codes 44100) | device rate, no resample |
| analysis hop / window | 512 / 1024 analysis samples (~557 device samples, 11.6 ms at 48 kHz) | 512 / 1024 device samples (10.7 ms at 48 kHz) |
| phase | derived from beat-event timing vs estimated period | `aubio_tempo_get_last` position + `get_period` anchor |
| internal silence gate | none in BTrack | aubio's own gate left at its -90 dBFS default |
| adapter held-silence gate | -60 dBFS block RMS, 50 ms holdoff | -60 dBFS block RMS, 50 ms holdoff |
| device audio resampler | adapter's persistent-phase linear interpolation | none |
| internal onset-function resampler | BTrack's libsamplerate SINC best quality | none |

The adapter held-silence gate is re-armed per *block*, so the block size changes
when silence latches (see the framing table). This is an adapter configuration
difference, not a tracker difference, and it is why the primary run is fixed at
128 frames. Both adapters report the same onset-strength definition (block energy
flux) so ODF scaling is not a confound.

## Primary results — uncompensated, 128-frame blocks

| metric | BTrack | aubio |
|---|---|---|
| F-measure mean (18/19 fixtures matched) | 0.7099 | 0.5357 |
| precision / recall mean | 0.7039 / 0.7235 | 0.6547 / 0.4725 |
| acquisition within 2 bars, core | 4/11 = 0.3636 | 7/11 = 0.6364 |
| BPM rel. error, worst core | 0.0234 (123.05 vs 126, `clean_eighths`) | 0.0133 (`sustained_chords`) |
| core fixtures with a BPM lock | 11/11 | 11/11 |
| half/double-time errors, core | 0/11 | 0/11 |
| mean signed / abs / p95 phase (matched fixtures) | +2.64 / 10.87 / 22.01 ms | -3.07 / 14.55 / 26.43 ms |
| causal availability latency, mean / worst | 12.88 / 14.25 ms | 5.73 / 10.62 ms |
| events in true silence, worst measured coverage | 1.366/s (`sparse_single_notes`, 8 held-grid beats) | 1.366/s (`sparse_single_notes`, 8 held-grid beats) |
| silence tempo-increase max (diagnostic) | +0.000 BPM (4 evaluated, 15 insufficient) | +0.140 BPM (4 evaluated, 15 insufficient) |
| ramp local-tempo rel. error mean / worst | 0.0839 / 0.3432 | 0.1945 / 0.8027 |
| syncopation max step fraction | 0.0000 | 0.0016 |
| CPU total / C++ new-delete allocations | 1.66 s / 38 761 | 0.46 s / 76 |

Timing diagnostics on every beat of the primary run: BTrack **400 reported, 0
block-start, 0 non-causal, 0 rate-mismatch**; aubio **288 reported, 0 block-start,
0 non-causal, 0 rate-mismatch**. Both adapters honour the documented device
clock; no reported device timestamp was discarded.

### SPEC 19 gates (uncompensated, 128-frame)

| SPEC 19 gate | BTrack | aubio |
|---|---|---|
| acquire useful lock within 2 bars for >= 95 % of core | **FAIL** 4/11 | **FAIL** 7/11 |
| locked BPM rel. error <= 2 % on core | **FAIL** 2.34 % | **PASS** 1.33 % |
| half/double-time errors < 5 % on core | **PASS** 0/11 | **PASS** 0/11 |
| no tempo jump from one isolated syncopated event | NOT-MEASURED | NOT-MEASURED |
| silence does not create false acceleration | NOT-MEASURED | NOT-MEASURED |
| explicit resync establishes new phase within boundary | NOT-MEASURED | NOT-MEASURED |
| Follow handles gradual ramps without audible discontinuity | NOT-MEASURED | NOT-MEASURED |
| Loose Follow measurably less reactive than Follow | NOT-MEASURED | NOT-MEASURED |
| stop/start recovery without audio-device restart | NOT-INFORMATIVE (offline) | NOT-INFORMATIVE (offline) |

Neither backend passes the acquisition gate. aubio is the only one that passes the
BPM gate, because BTrack's 44.1 kHz work-around leaves a documented -2.34 % bias
while aubio's rate-normalised estimator sits at 1.33 %. **This is not a tracker
selection: G3 stays OPEN and no production wiring or ADR is produced here.**

## Discarded timestamp bug effect (the headline)

The EVAL-002R runner set `obs.inputSampleTime = frame.sampleTime` after
`process()` (old `BackendRunner.cpp:180-185`), so every beat was scored at its
block start. Reproducing that with `--legacy-block-stamped-beats` on the same
backend/corpus gives:

| BTrack, 128-frame blocks | mean signed phase | mean abs | mean p95 | F |
|---|---|---|---|---|
| fixed (reported device time preserved) | **+2.64 ms** | **10.87 ms** | **22.01 ms** | 0.7099 |
| legacy (block-stamped, EVAL-002R) | +12.38 ms | 16.15 ms | 28.16 ms | 0.7099 |

The legacy row is **exactly the EVAL-002R published BTrack numbers**
(+12.381 / 16.150 / 28.159 ms). The apparent ~12 ms BTrack latency in that record
was therefore an artifact of the runner, not the backend: with the reported hop
timestamps preserved, BTrack's mean signed phase is +2.6 ms and its absolute
phase error drops 33 % (16.15 -> 10.87 ms), p95 22 %. F is unchanged only because
the 70 ms match tolerance absorbs the ~10 ms shift.

| aubio, 128-frame blocks | mean signed phase | mean abs | mean p95 | F |
|---|---|---|---|---|
| fixed | -3.07 ms | 14.55 ms | 26.43 ms | 0.5357 |
| legacy | -1.72 ms | 14.38 ms | 25.61 ms | 0.5379 |

aubio's sub-hop offset is small relative to a 128-frame block, so its own defect
effect is ~1.4 ms signed and +0.002 F; it is not negligible but it is an order of
magnitude smaller than BTrack's because BTrack reports on a coarse 11.6 ms
analysis-hop grid that the block stamp replaced with a finer 2.67 ms grid.

### Compensation is now demonstrably wrong

With corrected event times, compensating the documented framing latencies
overshoots:

| variant | BTrack signed / abs / p95 | aubio signed / abs / p95 |
|---|---|---|
| uncompensated | +2.64 / 10.87 / 22.01 ms | -3.07 / 14.55 / 26.43 ms |
| documented framing latency | 11.61 ms -> -7.79 / 13.86 / 28.56 | 10.67 ms -> -13.38 / 20.34 / 32.73 |
| worst-case frame | 23.22 ms -> -17.96 / 22.70 / 35.26 | — |

The uncompensated run is the primary and no gate flips under any variant. The
harness never selects a shift to improve a gate; it only offers the documented
values, and they now make phase worse.

## Framing-size diagnostic (128 vs 512)

| run | F | acq core | BPM worst core | trueSil worst/s | causal mean | reported beats |
|---|---|---|---|---|---|---|
| BTrack 128 | 0.7099 | 4/11 | 0.0234 | 1.366 | 12.88 ms | 400 |
| BTrack 512 | 0.6948 | 4/11 | 0.0234 | 1.025 | 17.03 ms | 390 |
| aubio 128 | 0.5357 | 7/11 | 0.0133 | 1.366 | 5.73 ms | 288 |
| aubio 512 | 0.5244 | 7/11 | 0.0129 | 1.366 | 5.73 ms | 281 |

Two effects are visible and both are *adapter* properties, not runner
bookkeeping:

- Causal availability latency grows with the block size (the block end moves
  later), which is exactly why event time and availability must be separate.
- F and the reported-beat count move slightly with the block size because the
  adapters' held-silence gate is re-armed and decremented per block, so a coarser
  block changes when silence latches and which beats are suppressed. The runner's
  own event times are framing-independent (BTrack's `analysisIndex`-derived hop
  clock; aubio's hop accumulator).

## Resource metrics scope (do not over-claim)

The allocation counter is the CLI's global C++ `operator new`/`delete` overrides.
It counts **C++ allocations only, including the CLI's own**, and reports 38 761
(BTrack) vs 76 (aubio) over the corpus. BTrack's count is dominated by upstream
`BTrack` passing a cumulative-score `CircularBuffer` by value and heap-copying it
once per analysis hop; aubio preallocates in `reset()`. This is **not** a count of
C `malloc` traffic (aubio/BTrack/libsamplerate C code is invisible to it) and it
is **not** a real-time-safety claim: both backends' `process()` run on the
analysis worker, not the audio callback.

## Limitations

1. Synthetic corpus (see `testdata/rhythm/README.md`); a bad score here is a
   reason to retest with real guitar, not a verdict.
2. The true-silence metric counts held-grid beats in declared silence. On
   `sparse_single_notes` both backends read 1.366/s = 8 held beats; that is the
   sparse fixture doing its job, which is why the SPEC gate is NOT-MEASURED
   rather than a count-based FAIL.
3. `sustained_chords` has a measured synthesis-duration defect. Short percussive
   events in `tapping_muting_only` are not necessarily defective merely because
   silent occupancy is high; its exclusion remains a conservative harness caveat,
   pending an independent onset audit. See `../CORPUS-ACOUSTIC-REVIEW.md`.
4. Syncopation stability, ramp continuity, resync and Loose-Follow reactivity
   remain unmeasurable offline; they need the Musical Clock and audition.
5. `analysis queue overrun count` and `platform/build complexity` (SPEC 12.3) are
   harness/integration outputs, not corpus metrics.
