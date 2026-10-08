# BPM-report variant from emitted beat intervals — diagnostic measurement (TRACK-005)

This is a **diagnostic measurement**. It selects no tracker, tunes no gate,
proposes no ADR and performs no live integration. **G3 stays OPEN.** It exists to
measure, from causal per-block evidence, whether reporting a tempo derived only
from the beat intervals BTrack has *already emitted* is stable and unbiased on
the pinned click sweep and the corpus — the first of the narrow next-repair
contracts proposed in
[`TRACKER-ACQUISITION.md`](TRACKER-ACQUISITION.md) §9.1.

- Base: `main` `bf61598` (`merge(TRACK-004): integrate causal acquisition
  diagnosis and fail-closed replay checks`), worktree
  `/home/mojo/projects/worktrees/TRACK-005-tempo-variant`, branch
  `wp/TRACK-005-tempo-variant`.
- Default adapter, default plugin, shared harness, CMake, corpus, ledger and the
  TRACK-004 artifacts are **not touched**. The variant is a separate plugin and a
  separate decorator source set under `tools/tempo-variant/**`.
- The **claim label is distinct**: the variant plugin's `id()` is
  `btrack-tempo-variant`, never `btrack`.

## 1. The predeclared method (frozen before the corpus was scored)

```
candidate = 60 / median( last 4 positive finite consecutive emitted beat-event
                         intervals )
readiness = true iff >= 5 beat events have been emitted and the ring currently
            holds 4 valid consecutive intervals
before readiness -> the wrapped backend's own bpmCandidate is forwarded unchanged
```

Exactly one field of the forwarded `RhythmObservation` changes: `bpmCandidate`.
`beatEvent`, `inputSampleTime` (the emitted beat timestamp), `onsetStrength01`,
`energyRmsDbfs`, `beatConfidence01`, `silence`, `phaseValid`, `beatPhase01` and
`transientDensity01` are copied through unchanged. No confidence is invented: if
the wrapped backend reports `beatConfidence01 == 0`, the variant forwards `0`.

### Interval validity / reset policy (predeclared, fixed)

The interval is the difference of two **consecutive** emitted beat samples, in
seconds at the rate passed to `reset()`. The accepted window is the wrapped
adapter's **own declared tempo range** (`BTrackBackendConfig`: `minBpm = 40`,
`maxBpm = 240`), i.e. `[0.25, 1.50] s`. This is derived from the adapter
configuration, **not fitted to any corpus result**.

| interval event | policy |
|---|---|
| non-finite, `<= 0`, non-monotonic sample time | **reset** the ring |
| `< 0.25 s` (above the declared maximum tempo) | **reset** the ring |
| `> 1.50 s` (missing beat, gate-suppressed beat, silence gap) | **reset** the ring |
| non-causal beat (reported after the block that produced it) | **reset** the ring, cannot anchor an interval |
| between beats | the last candidate is **retained** (stale, not a new measurement) |

There is **no** octave/half/double correction, **no** grid or truth snapping,
**no** truth tempo or range from any fixture, and **no** automatic parameter
sweep. The window is fixed at 4 intervals (5 events).

### Freeze

Algorithm and parameters were frozen after the click/math tests and before the
corpus run. Combined `sha256` of the algorithm + plugin source set:
`7fccdd7f2dc32d9bbaebb8a5ac0db6f7cc989c0b386403b77adc97a8dac4ba0b`
(per-file hashes in `tempo-variant/provenance.json`). The variant plugin binary
`libtempo-variant-btrack.so` hashed
`19c4bc67484b7add47124ae87ed89b2bc107e234c2f5054a724eb497428865fe` after the run.

## 2. Implementation and source separation

- `tools/tempo-variant/TempoVariant.{h,cpp}` — the decorator. Fixed 4-element
  ring, O(1) `process()`, **no heap allocation in `process()`**, no clock, no I/O
  except an optional diagnostic callback.
- `tools/tempo-variant/TempoVariantPlugin.cpp` — a dlopen()-able plugin that
  composes the real, unmodified `jam::BTrackBackend` linked from the pinned
  EVAL-005 main-core static archives (`libjam-btrack.a`, `libbtrack.a`,
  `libsamplerate.a`, `libkiss_fft.a`). It exports the existing
  `jam_rhythm_create()` / `jam_rhythm_destroy()` convention. No tracker source is
  compiled into any tool binary; this is a structural separation, not a legal
  conclusion.
- `tools/tempo-variant/tempo-variant-click` — bounded click sweep and tempo-step
  CLI. It dlopens the **default** pinned plugin and runs it bare (baseline) and
  wrapped by the compiled-in decorator (variant), so the two trajectories see the
  identical waveform.
- `tools/tempo-variant/FixtureMetricsDump.cpp` — per-fixture full-metric JSON
  using the **unmodified** `rhythmeval` source maths (`Metrics.cpp`,
  `Manifest.cpp`, `BackendRunner.cpp`). Its aggregate reproduces the diagnostic
  CLI's `summary.json` aggregate on every scored field (the only difference is
  the resource `cpuSecondsTotal`, which the trace-based CLI leaves at 0), which is
  the "source maths same" cross-check.

Corpus runs use the **unchanged** TRACK-004 integration binary
`/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics`
(sha256 `75b0d73b…`) with `--backend-lib` pointing at either the default pinned
BTrack plugin (sha256 `41e6476e…`) or the variant plugin. The baseline plugin
hash is byte-identical to the TRACK-004 provenance.

## 3. Truthful availability

`bpmCandidate` carries no readiness flag in `jam::RhythmObservation`, so method
availability is exported **separately**. The per-block `MethodRecord` carries
`ringCount`, `ready`, `intervalState`, `baseBpm` and `variantBpm`; the plugin
writes the raw log and `extract_method_history.py` projects a per-beat CSV with
the beat's **causal availability** (= block end). When `ready` is false the
variant value equals the forwarded base value, so a fallback can never be
mistaken for a derived report. No future audio and no truth enter the method.

## 4. Click/math tests (run before the corpus)

`TempoVariantTests` — **41 checks, 0 failures** (output committed as
`tests-TempoVariantTests.txt`). They pin, on independent scripted observation
streams (no audio, no real backend):

- steady 126 with a `123.046875` base report → variant **126 exactly** on the
  5th beat, and base forwarded for beats 0–3 (startup causality/fallback);
- the derived candidate persists on non-beat blocks once ready;
- a **single** missing beat (42000 samples at 44.1 kHz = 0.952 s) is inside the
  declared window and the 4-median is robust to it; **two** missing beats in the
  window move the median (raw, no correction);
- a `> 1.5 s` gap resets the ring and falls back to base;
- silence adds no intervals and retains the stale derived candidate;
- a half-time alias (63 BPM interval) is reported **as-is** (no octave
  correction); a double-time train above 240 BPM resets;
- `reset()` clears the ring; non-monotonic and non-causal beats reset;
- every non-bpm field is forwarded unchanged; the id is distinct from `btrack`.

### 24 s click sweep 118…134 at 44.1 kHz and 48 kHz

`tempo-variant-click --mode sweep`. Requested 126 BPM:

| rate | default BTrack `reportedBpmLast` | variant `reportedBpmLast` | truth interval | mean interval | ratio |
|---|---|---|---|---|---|
| 44100 | **123.046875** (−2.34 %) | **126.048019** (+0.038 %) | 0.476190 s | 0.476483 s | 1.00061 |
| 48000 | **123.046875** (−2.34 %) | **126.050423** (+0.040 %) | 0.476190 s | 0.476483 s | 1.00061 |

Across 118…134 the variant's last-value relative error is **≤ 0.94 %** at both
rates (worst at 128 requested); the default's is up to **2.68 %** (at 118). The
variant reproduces the same quantised value as the default where the beat grid
lands on it (e.g. request 123 → 123.046875), so it is not a blanket offset. Full
tables: `tempo-variant/click/click_sweep_rate{44100,48000}.csv`.

## 5. Corpus results (unchanged CLI, original 19 + repaired 19, block 128)

Beat-event series are **exactly equal** between baseline and variant on every
fixture at every framing tested (sha256 of every `beats/<fixture>.csv` agree; the
canonical original-block128 series are committed under
`tempo-variant/beats/`). Because only `bpmCandidate` changes, F-measure,
precision, recall, half/double-time rate, false-beats-in-silence and the phase
metrics are identical by construction; the per-fixture metric diff confirms the
only differing fields are bpm-dependent (acquisition, `lockedBpm`,
`bpmRelativeError`, syncopation tempo statistics, silence acceleration).

### Aggregates

| metric | original base | original variant | repaired base | repaired variant |
|---|---|---|---|---|
| acquisition core within 2 bars | **4/11** | **6/11** | **5/11** | **7/11** |
| worst core BPM rel. error | 0.0234 | **0.0077** | 0.0234 | **0.0077** |
| median steady BPM rel. error | 0.0212 | **0.0015** | 0.0212 | **0.0015** |
| mean steady BPM rel. error | 0.0432 | 0.0312 | 0.0432 | 0.0314 |
| F-measure mean | 0.7099 | 0.7099 | 0.7248 | 0.7248 |
| half/double error rate (core) | 0 | 0 | 0 | 0 |
| worst false beats/s in true silence | 1.366 | 1.366 | 1.366 | 1.366 |
| **max silence tempo increase (BPM)** | **0** | **44.28** | **0** | **0** |

At **512 framing, original only** (variant run once): core acquisition
**4/11 → 8/11**, worst core BPM error **0.0234 → 0.0077**, beat series still
exactly equal.

### Acquisition flips (event clock), original block 128

Gained (6): `arpeggio`, `clean_eighths`, `clean_sixteenths`,
`line_input_low_level`, `missing_downbeats`, `noisy_microphone`.
Lost (1): `sustained_chords`.

The existing acquisition gate is scored on the **event clock** (unchanged). The
trace also reports the **causal confirmation availability** (block end) of the
confirming beat; e.g. variant `arpeggio` confirms at event 4.3538 s / available
4.3680 s, `clean_eighths` at 6.0720 / 6.0853, `missing_downbeats` at 4.8646 /
4.8773. Full per-fixture start/confirm event and availability columns are in
`tempo-variant/compare/original-block128.csv` (and `.md`). The acquisition gate's
"run start" is still backdated by the scorer exactly as before; the availability
column is reported separately and is never folded into the event time.

## 6. The failure mode, reported raw (variant is worse on one fixture)

On the **original** (defective, short-decay) `sustained_chords` the variant
**loses** acquisition (base acquired after 2 bars, variant `TempoOutsideBand`).
The per-beat method history shows why: the sparse, irregular beat intervals are
`0.743, 0.453, 1.358, 1.068 s` → the 4-median `0.905 s` → a **half-time-ish
63–66 BPM**, with no octave correction, versus the base report `95.703125`. A
later window produces `51.4 BPM`. This is also the source of the
`maxSilenceTempoIncreaseBpm` regression to **44.28** on that fixture: the derived
tempo is far below the base tempo before/after a true-silence span. On the
**repaired** (long-decay) `sustained_chords` the intervals are regular enough
that the variant reports `96.60` and acquires, and the silence-acceleration
regression disappears.

**Interpretation, stated at the strength of the evidence:** the method repairs
the dominant BTrack *BPM-report* failure on regular material (arpeggio, eighths,
sixteenths, missing downbeats, low level, noisy mic) and on the repaired
sustained fixture, but it can lock onto a **sub-octave** median on very sparse,
irregular beat trains and it does not correct octaves. This is a real limitation,
not hidden.

## 7. Causal tempo-step response (no offline future)

A deterministic 24 s click train stepping **126 → 132 BPM** at t = 12 s (44.1 kHz
and 48 kHz). The variant only reads intervals of beats already emitted, so its
response is genuinely causal:

| rate | variant first within ±2 % of 132 | variant lag | lag in beats | default BTrack within ±2 % |
|---|---|---|---|---|
| 44100 | 13.8507 s | **1.8507 s** | 5 beats | never |
| 48000 | 13.8507 s | **1.8507 s** | 5 beats | never |

The default BTrack never reaches 132 within ±2 % on this step (it reports 129.20
after the change). The medians-of-4 settle in exactly 5 beats after the change,
which is the expected causality: two new intervals move the median, four replace
the window. Startup (from silence) reaches `within ±2 %` of 126 within ≈ 2.5 s
(5 beats) on the same train. Beats-only trajectories and the summary are in
`tempo-variant/click/click_step_rate*_beats.csv` / `_summary.csv`.

## 8. Comparison to default BTrack and the stored aubio benchmark

- **Default BTrack** is the baseline in every table above (same pinned plugin
  the TRACK-004 evidence used).
- **aubio**: the current stored verified run
  [`tracker-acquisition/aubio/block128/summary.json`](tracker-acquisition/aubio/block128/summary.json)
  is referenced (not re-run) as `tempo-variant/reference/aubio-block128-summary.json`.
  It acquired 7/11 core with worst core BPM error 0.0133. The variant still does
  not reach aubio's acquisition count on the original corpus (6/11) and matches
  it on the repaired corpus (7/11); it improves BTrack's BPM error past aubio's
  on the steady set. No selection is made and no cross-backend claim beyond these
  numbers is offered.

## 9. Limitations

1. Synthetic corpus only; SPEC 20 real-guitar tests remain required.
2. One algorithm, one fixed window (4 intervals), no parameter sweep; the choice
   is predeclared, not optimised. A different window was **not** cherry-picked,
   because no window sweep was run.
3. The 4-median is robust to one outlier in the window but not two; a single
   missing beat is reported as an accepted (doubled) interval, which is honest
   but not corrected.
4. Sub-octave locking on very sparse trains (see §6) is a real failure; no octave
   correction is applied by design.
5. The silence-acceleration metric worsens on the defective original
   `sustained_chords`; it is unchanged on the repaired corpus.
6. `cpuSeconds` differs between my metrics dumper and the trace CLI (resource
   only, never scored); all scored fields agree.
7. No tracker selection, no ADR, no gate change, no production wiring; G3 OPEN.
8. The optional EVAL-003 derived-24 paired robustness run was **not executed**
   (scope/time budget); it is the natural next check.

## 10. Proposed next contract (proposal only, from executed evidence)

Narrow and separately scoped: **characterise the variant's sparse-train
behaviour and a bounded octave guard, without touching the default or the gate.**
Concretely, on the pinned click sweep and the corpus, measure whether an explicit,
predeclared half-time detection (e.g. a candidate below a declared fraction of the
recent base report, or a two-interval alternation check) removes the
`sustained_chords` sub-octave lock without changing the regular-material gains,
and whether it removes the `maxSilenceTempoIncreaseBpm` regression. Any such
change is a new variant and a new freeze; it must not be merged as default and
must not relax the 2 % gate.

## 11. Artifact layout and bounds

`docs/research/tempo-variant/` — **514 KiB**, budget 5 MiB:

```
provenance.json                 base SHA, freeze hashes, plugin/CLI hashes, G3=OPEN
wav-hashes.txt                  original 19 + repaired refs 18/18 OK
tests-TempoVariantTests.txt     41 checks / 0 failures
original/block128/{baseline,variant}/{fixtures.csv,acquisition.json,summary.json}
original/block512/{baseline,variant}/…            (beats-only run)
repaired/block128/{baseline,variant}/…
compare/{original-block128,repaired-block128,original-block512}.{csv,md}
method-history/{original,repaired}-block128{-fixtures,}.csv   per-beat readiness
metrics/{base,var}-{original,repaired}.json       full per-fixture metrics
click/click_sweep_rate{44100,48000}.csv
click/click_step_rate{44100,48000}_{summary,beats}.csv
beats/original-block128/{baseline,variant}/*.csv  canonical exact-equality series
beats-sha/all.txt                                 sha256 of every beat set
reference/aubio-block128-summary.json             stored verified aubio run
```

## 12. Reproduction

```bash
tools/tempo-variant/build.sh                 # plugin + tests + click/metrics CLIs
tools/tempo-variant/run-corpus.sh            # writes docs/research/tempo-variant
/home/mojo/projects/build-TRACK-005/TempoVariantTests
```

## 13. Tests executed

```
tools/tempo-variant/build.sh                         # clean, -Wall -Wextra -Wpedantic
/home/mojo/projects/build-TRACK-005/TempoVariantTests # 41 checks, 0 failures
tools/tempo-variant/run-corpus.sh                    # all runs; artifact 514 KiB < 5 MiB
```

The tests are independent of the real backend (scripted observation streams) and
pin the method arithmetic, the startup fallback, the missing-beat/gap/reset
semantics, silence retention, the half-time (no correction) and double-time
(reset) aliases, reset, non-monotonic and non-causal handling, and full
non-bpm-field pass-through.

## Handoff

G3 remains **OPEN**. No tracker is selected, no ADR, no gate tuned, no default
changed, no production wiring. The evidence tree above plus `task-notes/TRACK-005.md`
are the handoff; the branch head SHA is recorded in the task note.
