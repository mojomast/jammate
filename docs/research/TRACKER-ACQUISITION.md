# Tracker acquisition — causal diagnosis of both real adapters (TRACK-004)

This is a **diagnostic**: it selects no tracker, tunes no gate, proposes no ADR
and performs no live integration. **G3 stays OPEN.** It exists to explain, from
causal per-block evidence, *why* both real integrations fail SPEC 19's
acquisition gate and why BTrack's locked-BPM error is −2.34 %, without touching
`src/`, `vendor/`, any CMake file, the corpus, or the shared harness/scoring.

- Base: `main` `6287288` (`merge(EVAL-005): integrate corrected paired tracker
  robustness evidence`).
- Corpus: the untouched original 19-fixture `testdata/rhythm`; all 19 WAV
  sha256 and byte sizes re-verified (`tracker-acquisition/wav-hashes.txt`).
- Backends: the EVAL-005 main-core plugin builds, whose adapter sources are
  byte-identical to this base (`librhythm-eval-btrack.so` sha256
  `41e6476e…`, `librhythm-eval-aubio.so` sha256 `61336277…`). See
  `tracker-acquisition/provenance.json`.
- New tool: `tools/tracker-diagnostics/**`. **No tracker source is compiled
  into the tool binary**; the tracker plugins are separate build artifacts
  loaded at run time through the existing `jam_rhythm_create()` /
  `jam_rhythm_destroy()` convention. That is a structural source-separation
  choice; it is **not** a statement about copyright obligations, and dynamic
  loading alone establishes no legal compatibility conclusion.

## 1. What was measured, and how

`tools/tracker-diagnostics` drives one `IRhythmTracker` at a time over one WAV
and records **every block's raw `RhythmObservation`** beside the block's device
clock: BPM, phase validity, silence, confidence, onset strength, RMS, beat
event, reported device sample, block start and block end. Two clocks are kept
exactly as EVAL-004 defined them:

- **event time** — the backend's causal reported device time (aubio sub-hop
  tactus, BTrack analysis-hop start); fallback to block start only when
  non-causal;
- **causal availability** — the device time at which `process()` returned (the
  block end).

The same block vector is reduced by the **unmodified** `rhythmeval::toSeries()`
and scored by the **unmodified** `rhythmeval::scoreFixture()`. The scorer's
acquisition rule (`Metrics.cpp::findFirstLockFrom`) is restated independently in
`AcquisitionReplay.cpp` and asserted to agree with `scoreFixture`; the CLI
**exits non-zero if any fixture disagrees**. **No lock-run start/end semantics
were changed.**

**Missing stays missing.** Every optional measurement carries an explicit
measured flag. When it is false the value is an **empty CSV cell / JSON null**;
a *measured* zero is written as `0`. This applies to a failed lock's acquisition
bars/seconds and lock timestamps, to the BPM relative error when the fixture has
no nominal BPM, and to the BPM ratio / phase / octave fields when there is no
match or no phase-valid tempo sample. The flags are exported beside the values
(`bpmMeasured`, `medianBpmErrorMeasured`, `ratioMeasured`, `phaseMeasured`, and
the `*Measured` JSON booleans).

Reproduce:

```bash
tools/tracker-diagnostics/build.sh                 # tool + arithmetic tests + config shim
tools/tracker-diagnostics/run-corpus.sh            # writes docs/research/tracker-acquisition
/home/mojo/projects/build-TRACK-004/diag/TraceReplayTests
```

**Cross-check against EVAL-004/EVAL-005.** The diagnostic reproduces the
committed EVAL-004 primary numbers exactly (uncompensated, 128-frame):

| run | F mean | acq core | worst core BPM err | reported beats | causal avail mean |
|---|---|---|---|---|---|
| BTrack 128 | 0.7099 | 4/11 | 0.0234 | 400 | 12.9 ms |
| aubio 128 | 0.5357 | 7/11 | 0.0133 | 288 | 5.7 ms |

## 2. The headline: BTrack's acquisition failures are dominated by a *BPM-report* failure

For BTrack's core acquisition failures the predicted beats line up positionally
with the ground-truth grid for **16–19 consecutive beats** (`longestMatchRun`),
and the failing clause is the 2 % tempo agreement. The trace exports a per-beat
tally of exactly which clause failed over that longest match run (agreeing /
outside-band / missing-sample / phase-invalid / BPM-invalid):

- at **512** framing, **five** core failures (arpeggio, clean_eighths,
  clean_sixteenths, missing_downbeats, palm_mute_metal) have **only** the
  numeric-band clause failing (`0 / N / 0 / 0 / 0`), so their reason is
  `TempoOutsideBand`;
- at **128** framing, **four** are numeric-only; the fifth, `palm_mute_metal`,
  mixes 9 numeric-band with 9 phase-invalid tempo samples and is labelled
  `TempoAgreementFailure` (the generic mixed label), **not** a numeric-only
  claim.

The traces show BTrack's beats are at ≈126 BPM while its reported
`bpmCandidate` is the quantised 123.05. On `clean_eighths` (nominal 126) the
steady inter-beat interval is ≈0.477 s while the scored `lockedBpm` is
123.046875, so every beat's tempo check fails and no lock is ever confirmed.

Aubio is the mirror image: its median tempo sits inside 2 % (≈ +0.9 % to
+1.3 %), so most of its failures are **no sustained forward-advancing match
run**, not tempo. Two of its core failures are flipped by the adapter's
held-silence gate (§5). `aubio/sustained_chords` is a genuine mixed-magnitude
case: its median is +1.3 % (3 of the 4 beats in the match run agree, 1 is
outside the 2 % band), and it is labelled `TempoOutsideBand` only because the
numeric band is the sole failing clause type there.

## 3. Per-core-fixture reason table (128-frame primary)

Full 44-row table (both framings) with lock start/confirmation, the clause tally
and the silence-gate experiment: `tracker-acquisition/core-reasons.md`. Reasons
are machine-assigned by fixed priority from trace evidence.

| backend | fixture | acq | bars | lock start (s) | lock confirm (s) | confirm avail (s) | match/tempo run | clause a/out/miss/ph/bpm | median BPM | BPM err | reason |
|---|---|---|---|---|---|---|---|---|---|---|---|
| btrack | arpeggio | 0 |  |  |  |  | 19/0 | 0/19/0/0/0 | 117.454 | 0.021 | TempoOutsideBand |
| btrack | blues_shuffle | 1 | 1.995 | 4.783 | 6.455 | 6.469 | 15/5 | 9/6/0/0/0 | 107.666 | 0.003 | AcquiredWithin2Bars |
| btrack | clean_eighths | 0 |  |  |  |  | 16/0 | 0/16/0/0/0 | 123.047 | 0.023 | TempoOutsideBand |
| btrack | clean_sixteenths | 0 |  |  |  |  | 19/0 | 0/19/0/0/0 | 123.047 | 0.023 | TempoOutsideBand |
| btrack | missing_downbeats | 0 |  |  |  |  | 18/0 | 0/18/0/0/0 | 117.454 | 0.021 | TempoOutsideBand |
| btrack | palm_mute_metal | 0 |  |  |  |  | 18/0 | 0/9/0/9/0 | 123.047 | 0.023 | TempoAgreementFailure |
| btrack | power_chords_distorted | 0 |  |  |  |  | 0/0 | 0/0/0/0/0 | 123.047 | 0.023 | NoMatchingBeats |
| btrack | sparse_single_notes | 1 | 1.256 | 3.042 | 4.656 | 4.669 | 15/15 | 15/0/0/0/0 | 109.957 | 0.018 | AcquiredWithin2Bars |
| btrack | stop_start | 1 | 0.261 | 0.824 | 2.159 | 2.173 | 15/6 | 6/8/0/1/0 | 129.199 | 0.021 | AcquiredWithin2Bars |
| btrack | sustained_chords | 1 | 2.744 | 7.210 | 9.718 | 9.731 | 6/4 | 5/1/0/0/0 | 95.703 | 0.003 | AcquiredAfter2Bars |
| btrack | syncopated_funk | 1 | 1.002 | 2.496 | 4.110 | 4.123 | 18/16 | 16/2/0/0/0 | 109.957 | 0.018 | AcquiredWithin2Bars |
| aubio | arpeggio | 1 | 1.254 | 2.858 | 4.346 | 4.352 | 15/15 | 15/0/0/0/0 | 121.400 | 0.012 | AcquiredWithin2Bars |
| aubio | blues_shuffle | 1 | 1.258 | 3.145 | 4.804 | 4.811 | 15/15 | 15/0/0/0/0 | 108.885 | 0.008 | AcquiredWithin2Bars |
| aubio | clean_eighths | 1 | 1.249 | 2.729 | 4.137 | 4.139 | 15/15 | 15/0/0/0/0 | 127.598 | 0.013 | AcquiredWithin2Bars |
| aubio | clean_sixteenths | 0 |  |  |  |  | 3/3 | 3/0/0/0/0 | 127.543 | 0.012 | NoSustainedMatchRun |
| aubio | missing_downbeats | 1 | 1.504 | 3.358 | 4.851 | 4.853 | 15/14 | 14/0/0/1/0 | 121.439 | 0.012 | AcquiredWithin2Bars |
| aubio | palm_mute_metal | 0 |  |  |  |  | 3/1 | 1/0/0/2/0 | 127.497 | 0.012 | NoSustainedMatchRun |
| aubio | power_chords_distorted | 0 |  |  |  |  | 2/2 | 2/0/0/0/0 | 127.560 | 0.012 | NoSustainedMatchRun |
| aubio | sparse_single_notes | 1 | 1.254 | 3.037 | 4.630 | 4.640 | 15/15 | 15/0/0/0/0 | 113.184 | 0.011 | AcquiredWithin2Bars |
| aubio | stop_start | 1 | 1.744 | 3.521 | 4.868 | 4.875 | 11/10 | 10/0/0/1/0 | 133.697 | 0.013 | AcquiredWithin2Bars |
| aubio | sustained_chords | 0 |  |  |  |  | 4/3 | 3/1/0/0/0 | 97.276 | 0.013 | TempoOutsideBand |
| aubio | syncopated_funk | 1 | 1.501 | 3.566 | 5.160 | 5.163 | 10/9 | 9/0/0/1/0 | 113.009 | 0.009 | AcquiredWithin2Bars |

Reason counts, core, 128-frame:

| reason | BTrack | aubio |
|---|---|---|
| AcquiredWithin2Bars | 4 | 7 |
| AcquiredAfter2Bars (delayed) | 1 (`sustained_chords`) | 0 |
| TempoOutsideBand (numeric clause the sole failure) | 4 | 1 (`sustained_chords`) |
| TempoAgreementFailure (mixed clause types) | 1 (`palm_mute_metal`) | 0 |
| NoSustainedMatchRun | 0 | 3 |
| NoMatchingBeats | 1 (`power_chords_distorted`) | 0 |
| InsufficientBeatEvents | 0 | 0 |

`NoSustainedMatchRun` is deliberately precise generic wording: the beats do not
form a four-beat forward-advancing positional run, and the trace cannot prove
*whether* the cause is gaps, duplicate/extra beats or a grid alias without
further evidence; the tally and the matched count are reported with it. It is
**not** asserted to be a proven phase conflict.

**Insufficient beat events** (fewer than 4 emitted beats) never fires on a core
fixture, and it is **not** the same claim as "insufficient onset evidence". On
this corpus most core fixtures emit many beats; `aubio/palm_mute_metal` emits 5
at 128 frames, so a low beat count is not the general cause. No claim is made
that every core fixture emits at least 15 beats.

**Octave mismatch** never fires on a core fixture, but the test is the *median*
`ratioToTruth`, so this shows the median lock is not an octave, **not** that no
transient octave excursion ever occurs. The label stays as a distinct code path
because transient octave jumps are a known failure mode elsewhere.

## 4. BTrack's −2.34 %: a report artefact, scoped

**Answer, stated at the strength the evidence supports:** the observed
`123.046875` is exactly a value of BTrack's integer-hop tempo grid, and the
tested scalar tempo reports are unchanged when the device resampler is bypassed.
That makes the device resampler **not the source of the tested bias**; it does
not prove full-trajectory equivalence across all rates, and it does not
establish the internal reason BTrack's comb selects a one-hop-longer lag.

### 4.1 Arithmetic (`experiments/btrack_grid.csv`)

BTrack 1.0.7 `calculateTempo()` (`third_party/BTrack/src/BTrack.cpp:435,438`)
maps its 41 candidates `2·maxIndex+80` (80…160 BPM, step 2) to an **integer**
number of 512-sample hops and then recomputes BPM from that integer:

```
beatPeriod = round(60·44100 / (candidate·512))
reported   = 60 / ((512/44100)·beatPeriod)
```

| hop lag | reported BPM | = 2646000/(512·lag) |
|---|---|---|
| 41 | 126.048018 | 126 is representable, 0.038 % away |
| 42 | **123.046875** | the observed BTrack value for a 126 BPM truth |

`123.046875 = 2646000 / (512·42)` exactly and `126.048018 = 2646000 / (512·41)`
exactly. **High confidence:** the observed value is a member of this grid and
126 is representable one hop away. The estimator committed to 42 hops
(≈ 0.4876 s) for a true period of 41.016 hops (≈ 0.4762 s).

### 4.2 What the rate experiment does and does not show

A deterministic 24 s click train (the pinned backend-test waveform) was swept
over 118…134 BPM through the **real adapter** at two device rates
(`experiments/btrack/click-rate48000/`, `click-rate44100/`). At 48000 Hz the
adapter resamples to 44.1 kHz; at 44100 Hz the adapter's linear step is 1.0, so
it emits a **one-sample-delayed identity**, i.e. no device-rate conversion.

- The **tested scalar outputs** are identical: max |Δ reportedBpmLast| = 0.000000
  and max |Δ reportedBpmMedian| = 0.000000 across the whole sweep. Requested
  126 → 123.046875 at both rates.
- The diagnostic stores only the last and median reported BPM per requested
  tempo, **not** the block-by-block sequence, so no claim of full-trajectory
  equality is made.
- Because the tested bias survives the 44.1 kHz no-device-conversion path, the
  device resampler is not its source. This is scoped to the tested scalar
  reports; the `why candidate 122/124 is selected rather than the
  representable 126` step remains **unproven** (it is internal to BTrack's
  comb/observation model, and no vendor patch was tested).
- The pre-existing header claims in `src/btrack/BTrackBackend.h` about resampler
  bias are superseded for the *tested* path by this scoped evidence only; they
  were not edited.

### 4.3 Beat placement in the tested examples

In the tested examples the mean inter-beat interval tracks the requested tempo,
not the reported one:

| requested | reported BPM | mean beat interval | ratio vs truth |
|---|---|---|---|
| 126 | 123.046875 | 0.476483 s | 1.00061 |

and on the real `clean_eighths` trace the steady inter-beat interval is ≈ 0.477 s
(≈ 125.7 BPM). This is **not** a global unbiased-placement claim; it is the
evidence that in these examples the beats follow the onset grid while
`getCurrentTempoEstimate()` returns the quantised HMM tempo.

**This is why the diagnosis uses the per-beat / per-block trajectory, not the
whole-clip median.** A whole-clip `lockedBpm` alone cannot tell whether the
tracker mistimed the beats or merely misreported the tempo; the trajectory
(`*/state/<fixture>.csv`, `*/beats/<fixture>.csv`) separates the two.

### 4.4 Consequence, stated without selecting a tracker

The SPEC 19 BPM gate is defined on the **reported locked BPM**; BTrack fails it,
and that same reported value is what blocks most of its core acquisition locks.
The diagnosis does not authorise relaxing the gate or silently substituting a
beat-interval-derived tempo: either would be a gate/semantics change requiring
its own task and ADR review (see §8).

## 5. Silence-gate suppression (bounded config experiment, NOT default evidence)

The adapters hold a −60 dBFS / 50 ms silence state and suppress beat events while
it is latched. Re-running the whole corpus with the adapter constructed at
`silenceRmsDbfs = −120` (gate effectively disabled) via a **separate
diagnostic-only shim** (`tools/tracker-diagnostics/ConfigPlugin.cpp`) shows the
gate suppresses real detections but flips few core acquisitions:

- beat counts rise on many fixtures (e.g. aubio `palm_mute_metal` 5 → 19,
  BTrack `line_input_low_level` 18 → 23);
- **aubio `palm_mute_metal` and aubio `sustained_chords` acquire only with the
  gate disabled** (0 → 1, at 2.998 and 2.249 bars);
- BTrack's core acquisition count is unchanged (4/11), because its failures are
  BPM-report failures that the gate does not touch.

These numbers are labelled `NOT default evidence` in
`experiments/silence-gate-off/**` and in `core-reasons.md` and must not be read
as a proposal to change the default gate. `tapping_muting_only` is **not**
grouped here with `sustained_chords` as a synthesis defect: the independent
acoustic review records silent occupancy, not a proven defect, and a separate
sustain repair is pending.

## 6. Framing check: 128 vs 512

Rerunning at 512-frame blocks changes almost nothing for BTrack and only the
acquisition boundary for aubio:

| | BTrack 128 | BTrack 512 | aubio 128 | aubio 512 |
|---|---|---|---|---|
| F mean | 0.7099 | 0.6948 | 0.5357 | 0.5244 |
| acq core | 4/11 | 4/11 | 7/11 | 7/11 |
| worst core BPM err | 0.0234 | 0.0234 | 0.0133 | 0.0129 |
| causal avail mean | 12.9 ms | 17.0 ms | **5.7 ms** | **5.7 ms** |
| reported beats | 400 | 390 | 288 | 281 |

The gate *fraction* is unchanged for both backends, but **per-fixture framing
differs**:

- BTrack `sustained_chords` does **not** cross the 2-bar line downward at 512;
  it **fails to lock entirely** (stored row: `acq=0`, longest match run 2,
  reason `NoSustainedMatchRun`). At 128 it locks but late (2.744 bars).
- aubio `missing_downbeats` 1.504 → 1.255 bars and `stop_start` 1.744 → 1.497
  bars at 512.
- aubio's causal-availability mean does **not** grow with the block size
  (5.7 ms at both) because its availability comes from the fixed 512-sample hop,
  not the frame size; BTrack's does grow (12.9 → 17.0 ms) because its block end
  is the reported analysis-hop boundary.

Full per-fixture detail: `core-reasons.md` and `*/block512/fixtures.csv`.

## 7. Event time vs causal availability (acquisition)

Acquisition is scored on the **event** clock (what the scorer has always done).
The trace adds the **causal availability** of the confirming beat so a real-time
consumer can see when the lock could actually have been *known*:

- confirming beats are available a few ms after their event time — e.g. the
  `sparse_single_notes` BTrack lock confirms at event 4.656 s / available
  4.669 s, and aubio at 4.630 s / 4.640 s (`clean_eighths` does not lock under
  BTrack, so it has no confirming beat).

Availability is never folded into the event time and never changes a lock. The
parallel series is in every `*/beats/<fixture>.csv` and every block row of
`*/state/<fixture>.csv`.

## 8. Missing evidence / limits

1. `AcquiredAfter2Bars` vs `AcquiredWithin2Bars` is a threshold on the scorer's
   own `acquisitionBars` (`≤ 2.0`); the diagnostic does not redefine it.
2. `power_chords_distorted` has zero matches for BTrack and 2/2 for aubio; the
   trace shows a detection/phase problem, but this note does not adjudicate the
   corpus's distorted-chord synthesis.
3. `sustained_chords` carries a pending sustain-repair caveat; the aubio
   silence-gate flip on it may be entangled with it. No fixture is labelled a
   synthesis defect here.
4. `InsufficientBeatEvents` is never the core cause; `octaveSuspect` is never the
   median core cause. Both are available code paths, and the absence of a median
   octave does not exclude transient octave excursions.
5. BTrack's internal *selection* of hop lag 42 was not forced to 41 (no vendor
   change), so the "why not 41" step is unproven.
6. Everything here is the synthetic corpus; SPEC 20 real-guitar play tests are
   still required.
7. The config experiment used the default-only adapter knob `silenceRmsDbfs`;
   no other knob was varied.
8. The EVAL-005 robustness note's "bimodal BPM" observation was between a ~0 %
   and a ~2.34 % value, **not** octave / half-time locks; it is not used here as
   octave evidence.

## 9. Proposed narrow next repair contract (for a future, separately-scoped task)

This note repairs nothing. If a follow-up is authorised, the narrowest testable
contracts consistent with this evidence are:

1. **BTrack BPM-report investigation (measurement first).** With the vendor
   unchanged, characterise whether reporting a tempo derived from the *emitted
   beat intervals* is stable and unbiased on the pinned click sweep and the
   corpus. This is an adapter-evidence question (SPEC 9.2: evidence, not a
   command), not a gate change, and must be labelled a variant. **Do not** merge
   it as default without an ADR; do not relax the 2 % gate.
2. **aubio non-locking fixtures.** For `clean_sixteenths`, `palm_mute_metal`
   and `power_chords_distorted`, the reason is `NoSustainedMatchRun`; audit the
   onset grid against the declared truth (the corpus onset audit already pending
   in `CORPUS-ACOUSTIC-REVIEW.md`) before attributing the miss to aubio.
3. **Silence-gate semantics.** Decide, with a SPEC-level rationale, whether the
   adapter gate should count as part of integration quality on the two aubio
   fixtures it flips; do not silently change the default.
4. **Framing.** Treat 128-frame as primary; 512-frame boundary movement on aubio
   and the BTrack `sustained_chords` lock loss are documented, not gated.

Each contract must state the exact artifact it will produce and must not touch
this note's gate definitions.

## 10. Artifact layout and bounds

`docs/research/tracker-acquisition/` (≈ 1.75 MiB, budget 5 MiB):

```
provenance.json                 base SHA, corpus/plugin hashes, G3=OPEN, experiment label
wav-hashes.txt                  19/19 original WAV sha256 + size OK
core-reasons.csv / .md          the per-core fixture reason tables (both framings + gate-off)
btrack/block128/{summary,acquisition}.json, fixtures.csv, beats/*.csv, state/*.csv
btrack/block512/{summary,acquisition}.json, fixtures.csv, beats/*.csv
aubio/block128/… , aubio/block512/… (same layout)
experiments/btrack_grid.csv                 the exact tempo->lag staircase
experiments/{btrack,aubio}/click-rate{48000,44100}/click_bpm_sweep_rate*.csv
experiments/silence-gate-off/{btrack,aubio}-block128/{summary.json,fixtures.csv}   NOT default
```

`--state-max-rows` is a **sampling target, not a hard bound**: every beat block,
every silence-state transition and the first/last block is always kept, so a
fixture can exceed it. The actual maxima are recorded in each `summary.json`
(`stateRowsMax`, `stateRowsTotal`, `stateRowsTargetExceeded`); at 128 frames the
maxima are 614 (BTrack) and 599 (aubio) against the 500 target. `beats/*.csv`
keeps every emitted beat with its exact event time, causal availability and
reported device sample.

CLI validation is strict: `--block` must be an integer in
`[1, kMaxAnalysisBlock]`; `0`, out-of-range and malformed values are rejected
with a non-zero exit rather than silently running a different block than the
summary reports. The rejection rule is unit-tested (`parseBlockFrames`).

## 11. Tests

`tools/tracker-diagnostics/tests/TraceReplayTests.cpp` (60 checks, 0 failures),
run by `build.sh`, pins:

- the exact BTrack grid arithmetic (`lag 42 → 123.046875`, `lag 41 →
  126.048018`; candidate 126 → 41 hops; 122/124 → 42 hops);
- lock-run start/confirmation timestamps and the separate availability clock;
- rejection of a 2.34 % tempo-disagreeing run while the positional match run is
  still long, with the numeric band as the sole failing clause;
- rejection of duplicate/backtracking beats;
- agreement of the independent replay with `rhythmeval::scoreFixture` and
  classification of the 123.05-on-126 case as `TempoOutsideBand`;
- missing-vs-zero serialisation (`optionalNumber`), a fixture with no nominal
  BPM, a zero-beat series, a phase-invalid tempo series, and a fabricated
  scorer mismatch (the detector fires, and the CLI exits non-zero on one);
- strict block-size rejection (`0`, `> kMax`, malformed, negative, empty);
- that `TraceRunner` reproduces the unmodified `BackendRunner` (beat times,
  availability, tempo/silence/phase, diagnostics) on a non-trivial fake backend.

## Handoff

G3 remains **OPEN**. No tracker is selected, no ADR, no production wiring. The
evidence tree above plus `task-notes/TRACK-004.md` are the handoff; the branch
head SHA is recorded in the task note.
