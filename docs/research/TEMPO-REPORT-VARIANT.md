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
  `wp/TRACK-005-tempo-variant`. The later EVAL-007 silence-coverage correction is
  merged on `main` separately and is **not** rebased here; the primary gates,
  acquisition and BPM metrics are unaffected by it (the orchestrator verifies
  main-source-current metric consistency apart from the coverage flags).
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
`transientDensity01` are copied through unchanged, bit-for-bit (asserted by a
unit test that compares the raw bit patterns of every non-bpm field). No
confidence is invented: if the wrapped backend reports `beatConfidence01 == 0`,
the variant forwards `0`.

### Interval validity / reset policy (predeclared, fixed)

The interval is the difference of two **consecutive** emitted beat samples, in
seconds at the rate passed to `reset()`. The accepted window is the wrapped
adapter's **own declared tempo range** (`BTrackBackendConfig`: `minBpm = 40`,
`maxBpm = 240`), i.e. `[0.25, 1.50] s` — derived from the adapter configuration,
**not fitted to any corpus result**.

| interval event | policy |
|---|---|
| non-finite, `<= 0`, greater than the maximum (a missing/suppressed beat or silence gap) | interval **measured**; ring **reset** |
| below the minimum (above the declared maximum tempo) | interval **measured**; ring **reset** |
| duplicate / non-monotonic sample time (checked **before** any subtraction, so no unsigned wrap) | interval **missing**; ring **reset** |
| non-causal beat (reported after the block that produced it) | interval **missing**; ring **reset** |
| invalid frame (oversize block or overflowing block end) | interval **missing**; ring **reset** |
| between beats | the last candidate is **retained** (stale, not a new measurement) |

A **missing** interval is exported as an explicit `intervalMeasured = 0` flag with
an **empty** cell — never a fabricated or wrapped number.

There is **no** octave/half/double correction, **no** grid or truth snapping,
**no** truth tempo or range from any fixture, and **no** automatic parameter
sweep. The window is fixed at 4 intervals (5 events).

### Freeze and correction disclosure

The method and parameters were frozen after the click/math tests and before the
corpus run. The integrated revision `bb30496` froze the algorithm at combined
`sha256`
`7fccdd7f2dc32d9bbaebb8a5ac0db6f7cc989c0b386403b77adc97a8dac4ba0b`.
Review corrections changed **interval bookkeeping and validation only** — a
missing-vs-measured flag, order-before-unsigned-subtraction, and frame-overflow
rejection — plus the click/metadata tooling and tests; the 4-interval median
method, its window and the startup fallback are unchanged. The corrected combined
`sha256` is
`cd8291e6d8ad49cae64fe43768ddc88e70679a2835dad78e62b5644b0503ec77`
(per-file hashes in `tempo-variant/provenance.json`, which records both the
pre-correction and corrected hashes). The corrected variant plugin
`libtempo-variant-btrack.so` hashed
`920a30f9bb2c81b8ed743f817beb5dc51e9f90070de5ca0788a46ff1223a6717`.

## 2. Implementation and source separation

- `tools/tempo-variant/TempoVariant.{h,cpp}` — the decorator. The **wrapper's own
  arithmetic** is fixed and bounded: a 4-element ring, no allocation, no locking,
  no I/O, O(1) except a 4-element insertion sort. This describes the decorator
  only. The wrapped backend runs on the rhythm-analysis worker (not the audio
  callback) and may allocate; the optional log callback is called synchronously
  from `process()`, so the plugin's fstream logger is an **offline diagnostic**
  path and is never a real-time one.
- `tools/tempo-variant/TempoVariantPlugin.cpp` — a dlopen()-able plugin that
  composes the real, unmodified `jam::BTrackBackend` linked from the pinned
  EVAL-005 main-core static archives. It exports the existing
  `jam_rhythm_create()` / `jam_rhythm_destroy()` convention. No tracker source is
  compiled into any tool binary; structural separation, not a legal conclusion.
- `tools/tempo-variant/tempo-variant-click` — bounded click sweep and tempo-step
  CLI. It dlopens the **default** pinned plugin and runs it bare (baseline) and
  wrapped by the compiled-in decorator (variant). Every numeric argument is
  validated strictly and in full before any directory, library load or audio
  allocation; a bad value exits 2.
- `tools/tempo-variant/FixtureMetricsDump.cpp` — per-fixture full-metric JSON
  using the **unmodified** `rhythmeval` source maths. Its aggregate reproduces
  the diagnostic CLI's `summary.json` aggregate on every scored field (only the
  resource `cpuSecondsTotal` differs), the "source maths same" cross-check.

Corpus runs use the **unchanged** TRACK-004 integration binary
`/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics`
(sha256 `75b0d73b…`) with `--backend-lib` pointing at either the default pinned
BTrack plugin (sha256 `41e6476e…`, byte-identical to the TRACK-004 provenance) or
the variant plugin.

## 3. Truthful availability

`bpmCandidate` carries no readiness flag in `jam::RhythmObservation`, so method
availability is exported **separately**. The per-block `MethodRecord` carries
`ringCount`, `ready`, `intervalMeasured`, `intervalState`, `baseBpm` and
`variantBpm`; the plugin writes the raw log and `extract_method_history.py`
projects a per-beat CSV with the beat's **causal availability** (= block end).
When `ready` is false the variant value equals the forwarded base value, so a
fallback can never be mistaken for a derived report. No future audio and no truth
enter the method.

## 4. Click/math tests (run before the corpus)

`TempoVariantTests` — **67 checks, 0 failures**. They pin, on independent scripted
observation streams (no audio, no real backend):

- steady 126 with a `123.046875` base report → variant **126 exactly** on the 5th
  beat, and base forwarded for beats 0–3 (startup causality/fallback);
- the derived candidate persists on non-beat blocks once ready;
- a **single** missing beat (0.952 s) is inside the declared window and the
  4-median is robust to it; **two** missing beats move the median (raw);
- a `> 1.5 s` gap, a duplicate beat and a backwards beat all reset; the
  duplicate/backwards intervals are **marked missing with an empty cell**, not
  wrapped to a huge positive value;
- an oversize frame and an overflowing block end reset rather than wrap;
- a **valid block-size change between `process()` calls does not wipe** the
  legitimate ring history (only `reset()` does);
- `reset()` with a non-finite/zero/negative rate clears the ring and the
  wrapper's own arithmetic uses the documented finite-positive fallback;
- silence adds no intervals and retains the stale derived candidate;
- a half-time alias (63 BPM interval) is reported **as-is**; a double-time train
  above 240 BPM resets;
- every non-bpm field is forwarded **bit-exactly** with non-zero independent
  values (including `transientDensity01`);
- the generator's step anchor is the first beat at/after the nominal cut, the
  interval into the anchor is still the pre-step period and the first post-step
  interval starts at the anchor;
- the id is distinct from `btrack`.

`tests/test_click_lag.py` — **6 python tests, OK** — pin the response-lag
semantics: the primary lag uses the availability clock and, for the variant, a
hit must be `ready`, so an in-band fallback is not counted; the event time is
secondary; only the first hit is reported; an out-of-band later beat does not
change the first hit but does clear the trailing-band flag; the lag-in-beats
definition counts emitted beats from the reference through the hit.

`tools/tempo-variant/check_failclosed.sh` — **22 checks, 0 failures** — exercised
against the built binaries: every malformed or out-of-range argument
(`--block 0/2049/128x/-1/overflow`, `--rate nan/0/inf/out-of-range`,
`--seconds -1/too-big`, `--sweep nan/negative`, `--step-from == --step-to`,
`--step-seconds` outside the clip, bad/missing mode, missing backend) exits 2,
and a write to `/dev/full` is detected and fails closed (no shared CMake
touched).

### 24 s click sweep 118…134 at 44.1 kHz and 48 kHz

Requested 126 BPM:

| rate | default BTrack | variant | truth interval | mean interval | ratio |
|---|---|---|---|---|---|
| 44100 | **123.046875** (−2.34 %) | **126.048019** (+0.038 %) | 0.476190 s | 0.476483 s | 1.00061 |
| 48000 | **123.046875** (−2.34 %) | **126.050423** (+0.040 %) | 0.476190 s | 0.476483 s | 1.00061 |

Across 118…134 the variant's last-value relative error is **≤ 0.94 %** at both
rates; the default's is up to 2.68 % (at 118). The variant reproduces the
quantised value where the grid lands on it. Full tables:
`tempo-variant/click/click_sweep_rate{44100,48000}.csv`.

## 5. Corpus results (unchanged CLI, original 19 + repaired 19, block 128)

Beat-event series are **exactly equal** between baseline and variant on every
fixture at every framing tested (sha256 of every `beats/<fixture>.csv` agree).
Because only `bpmCandidate` changes, the beat-detection, phase and
false-beats-in-silence metrics are identical on this corpus; the per-fixture
metric diff confirms the only differing fields are bpm-dependent (acquisition,
`lockedBpm`, `bpmRelativeError`, syncopation tempo statistics, silence
acceleration). Note this is an **observation on this corpus**, not a structural
invariance: `halfDoubleTimeError` is computed from `lockedBpm`, so a variant that
reported an octave away *could* change it (none did here).

### Aggregates

| metric | original base | original variant | repaired base | repaired variant |
|---|---|---|---|---|
| acquisition core within 2 bars | **4/11** | **6/11** | **5/11** | **7/11** |
| acquisition-anytime (all 19) | 8/19 | 13/19 | 8/19 | 14/19 |
| worst core BPM rel. error | 0.0234 | **0.0077** | 0.0234 | **0.0077** |
| median steady BPM rel. error | 0.0212 | **0.0015** | 0.0212 | **0.0015** |
| mean steady BPM rel. error | 0.0432 | 0.0312 | 0.0432 | 0.0314 |
| F-measure mean | 0.7099 | 0.7099 | 0.7248 | 0.7248 |
| half/double error rate (core) | 0 | 0 | 0 | 0 |
| worst false beats/s in true silence | 1.366 | 1.366 | 1.366 | 1.366 |
| **max silence tempo increase (BPM)** | **0** | **44.28** | **0** | **0** |

At **512 framing, original only** (variant run once): core within 2 bars
**4/11 → 8/11**, acquisition-anytime 7/19 → 14/19, worst core BPM error
**0.0234 → 0.0077**, beat series still exactly equal.

### Acquisition flips

Original block 128, **SPEC 19 core within 2 bars** gained: `arpeggio`,
`clean_sixteenths`, `missing_downbeats`; lost: `sparse_single_notes` (base
within 2 bars, variant acquired late). Across **all 19** fixtures
acquisition-anytime gained 6 / lost 1 (`sustained_chords`) and 6 fixtures are
acquired but **late** (>2 bars): `clean_eighths`, `line_input_clipping`,
`line_input_low_level`, `noisy_microphone`, `sparse_single_notes`,
`waltz_3_4`.

The existing acquisition gate is scored on the **event clock** (unchanged). The
trace also reports the **causal confirmation availability** (block end) of the
confirming beat; e.g. variant `arpeggio` confirms at event 4.3538 s / available
4.3680 s, `clean_eighths` at 6.0720 / 6.0853, `missing_downbeats` at 4.8646 /
4.8773. Full per-fixture event and availability columns are in
`tempo-variant/compare/original-block128.csv`.

## 6. The failure mode, reported raw (variant is worse on one fixture)

On the **original** (defective, short-decay) `sustained_chords` the variant
**loses** acquisition (base acquired after 2 bars, variant `TempoOutsideBand`).
Precise numbers from the per-beat method history: the base `lockedBpm` is
`95.703125` (truth 96, ratio 0.997); the variant's derived values on the READY
beats are **66.256, 51.423 and 63.802 BPM** (ratios 0.69, 0.54 and 0.66 of the
96 BPM truth) — i.e. a **sub-octave / low-tempo** report, not an exact half-time
(0.5) value. The whole-clip `lockedBpm` median stays `95.703125` only because the
ready derived blocks are a minority of the clip and the rest forward the base
value; the tempo samples in the acquisition window are the out-of-band derived
ones, which is why the gate fails. This is the source of the
`maxSilenceTempoIncreaseBpm` regression to **44.28** on that fixture. On the
**repaired** (long-decay) `sustained_chords` the intervals are regular, the
derived values are `95.7`–`97.5`, it acquires, and the regression disappears.

**Interpretation, stated at the strength of the evidence:** the method repairs
the dominant BTrack BPM-report failure on regular material and on the repaired
sustained fixture, but on very sparse/irregular beat trains the 4-median can lock
onto a **sub-octave** value and it does not correct octaves. This limitation is
reported, not hidden.

## 7. Causal tempo-step response (no offline future)

A deterministic 24 s click train stepping **126 → 132 BPM** at the nominal cut
t = 12 s (44.1 kHz and 48 kHz). The generator changes the period only on the
first generated beat **at/after** 12 s (the anchor `12.004762 s`); the interval
into the anchor is still the pre-step period `0.476190 s`, and the first
post-step interval starts at the anchor with period `0.454545 s`. The variant
only reads intervals of beats already emitted, so every quantity is causally
available at its block end.

| quantity (44.1 kHz) | value |
|---|---|
| primary: variant first within ±2 % of 132, **availability** clock | 13.865215 s |
| lag from nominal 12 s (availability) | **1.865215 s** |
| lag from actual anchor 12.004762 s (availability) | 1.860454 s |
| lag in beats from nominal | **5** |
| lag in beats from anchor | 4 |
| secondary: variant first within ±2 %, **event** clock | 13.850703 s |
| event lag from nominal | 1.850703 s |
| every ready beat after the first hit stays in ±2 % to the end | true |
| default BTrack first within ±2 % of 132 | **never** |

48 kHz gives the same structure (`anchor 12.004762 s`, availability lag
1.864000 s, 5 beats from nominal; base never). Only the **first** within-band beat
is reported; no "settling" is claimed. The earlier "settles in exactly 5 beats"
statement is withdrawn — that is not what was measured. Summary and beats-only
trajectory: `tempo-variant/click/click_step_rate*_summary.csv` / `_beats.csv`,
with the generator truth in `_info.csv` / `_truth.csv`.

## 8. Comparison to default BTrack and the stored aubio benchmark

- **Default BTrack** is the baseline in every table above.
- **aubio**: the stored verified run
  [`tracker-acquisition/aubio/block128/summary.json`](tracker-acquisition/aubio/block128/summary.json)
  is referenced (not re-run) as `tempo-variant/reference/aubio-block128-summary.json`
  (7/11 core, worst core BPM error 0.0133). The variant does not reach aubio's
  core count on the original corpus (6/11), matches it on the repaired corpus
  (7/11), and improves BTrack's BPM error past aubio's on the steady set. No
  selection is made.

## 9. Limitations

1. Synthetic corpus only; SPEC 20 real-guitar tests remain required.
2. One algorithm, one fixed window (4 intervals), no parameter sweep.
3. The 4-median is robust to one outlier in the window but not two.
4. Sub-octave locking on very sparse trains (§6); no octave correction by design.
5. The silence-acceleration metric worsens on the defective original
   `sustained_chords`; unchanged on the repaired corpus.
6. Beat/phase metrics are identical **on this corpus**; `halfDoubleTimeError`
   depends on `lockedBpm` and is not structurally invariant.
7. `cpuSeconds` differs between the metrics dumper and the trace CLI (resource
   only, never scored).
8. The optional EVAL-003 derived-24 paired robustness run was not executed.
9. No tracker selection, no ADR, no gate/default change; G3 OPEN.

## 10. Proposed next contract (proposal only, from executed evidence)

Narrow and separately scoped: characterise the variant's sparse-train behaviour
and test a **predeclared** half-time/low-tempo guard (new variant, new freeze)
that removes the `sustained_chords` sub-octave lock without changing the
regular-material gains or the silence-acceleration metric, and without relaxing
the 2 % gate. Must not be merged as default without an ADR.

## 11. Artifact layout and bounds

`docs/research/tempo-variant/` — **522 KiB**, budget 5 MiB:

```
provenance.json                 base SHA, pre-correction + corrected freeze hashes,
                                plugin/CLI/pin hashes, G3=OPEN, evidence contract
wav-hashes.txt                  original 19 + repaired 19 byte hashes + 18/18 refs
tests-TempoVariantTests.txt     67 checks / 0 failures
tests-click-lag.txt             6 python checks / OK
tests-failclosed.txt            22 fail-closed checks / 0 failures
original/block128/{baseline,variant}/{fixtures.csv,acquisition.json,summary.json}
original/block512/{baseline,variant}/…            (beats-only run)
repaired/block128/{baseline,variant}/…
compare/{original-block128,repaired-block128,original-block512}.{csv,md}
method-history/{original,repaired}-block128{-fixtures,}.csv   per-beat readiness (intervalMeasured)
metrics/{base,var}-{original,repaired}.json       full per-fixture metrics with flags
click/click_sweep_rate{44100,48000}.csv
click/click_step_rate{44100,48000}_{summary,beats,info,truth}.csv
beats/original-block128/{baseline,variant}/*.csv  canonical exact-equality series
beats-sha/all.txt                                 sha256 of every beat set
reference/aubio-block128-summary.json             stored verified aubio run
```

## 12. Reproduction

```bash
tools/tempo-variant/build.sh                 # plugin + tests + click/metrics CLIs
tools/tempo-variant/run-corpus.sh            # writes docs/research/tempo-variant
/home/mojo/projects/build-TRACK-005/TempoVariantTests
python3 tools/tempo-variant/tests/test_click_lag.py
tools/tempo-variant/check_failclosed.sh
```

## 13. Tests executed

```
tools/tempo-variant/build.sh                          # clean, -Wall -Wextra -Wpedantic
/home/mojo/projects/build-TRACK-005/TempoVariantTests # 67 checks, 0 failures
python3 tools/tempo-variant/tests/test_click_lag.py   # 6 tests, OK
tools/tempo-variant/check_failclosed.sh               # 22 checks, 0 failures
tools/tempo-variant/run-corpus.sh                     # all runs; artifact 522 KiB < 5 MiB
```

Before the corrections: 41 checks, 0 failures (commit `bb30496`). After the
corrections: 67 checks, 0 failures, plus the 6 python and 22 fail-closed checks.
The pre-correction and corrected freeze hashes are both recorded in
`provenance.json`; the 4-interval median method is unchanged.

## Handoff

G3 remains **OPEN**. No tracker is selected, no ADR, no gate tuned, no default
changed, no production wiring. Both candidates still fail part of the gate
(original core 6/11 and 7/11 within 2 bars for the variant on original/repaired;
BTrack default 4/11 and 5/11). The evidence tree above plus
`task-notes/TRACK-005.md` are the handoff; the branch head SHA is recorded in the
task note.
