# Sustained-chord corpus repair — EVAL-006

Scoped replacement of the single defective `sustained_chords` fixture, with an
independent PCM duration/energy audit and real current-main CLI evidence for both
tracker integrations. This directory is new; the EVAL-001 corpus, its generator,
its manifest, the shared harness, CMake and the execution ledger were **not
modified** — the eighteen untouched fixtures are referenced by relative path, not
copied, and their hashes are preserved.

This is a **diagnostic/corpus repair** artifact. It selects no tracker, tunes no
gate, wires no production path, and makes no live-safety claim. G3 stays **OPEN**.

## The measured defect

`docs/research/CORPUS-ACOUSTIC-REVIEW.md` measured the committed
`testdata/rhythm/wav/sustained_chords.wav` (`sha256 e4b9297f…`) independently of
the generator's decay model. The declared range is five bars at 96 BPM — four
chord events at 0.3456 / 2.8557 / 5.3416 / 7.8430 s, ~2.5 s apart — but the
attacks collapse almost immediately:

| offset after onset | 0 ms | 100 ms | 300 ms | 500 ms | 800 ms |
|---|---:|---:|---:|---:|---:|
| measured level (dBFS) | −20.7…−21.8 | −17.8…−21.3 | −51…−56 | −63.3…−63.6 | −62…−64.4 |

The generator's `p_sustained_chords` deliberately limits the ring to 42 % of a
bar (`ring = 0.42 · bar`, `t60_scale = ring / 4.83 ≈ 0.217`) so the chord stops
before the next attack and leaves "genuine quiet". That is defensible for a
*staccato* test but wrong for a fixture named and tagged `sustained_chords`: it
declared **8.734 s of 11.35 s (77 %)** as `trueSilenceSpans`, which is a
synthesis artifact, not a performance. The shared CLI already flags the fixture
by name as `CorpusDefect` (`Metrics.cpp` `isCorpusDefectiveSilenceFixture`) with
the comment *"Remove this list when the corpus is repaired."*

## The repair

`tools/rhythm-eval/tools/repair_sustained.py` re-renders the same event list
through the base generator's Karplus-Strong model and seeded RNG, changing only
the two decay parameters:

| parameter | base | repaired | why |
|---|---|---|---|
| `mix_t60` (global decay trim) | 0.6 | **1.0** | the model's own free-string T60 table; a gently strummed open chord is not damped faster than the unloaded string |
| per-event `t60_scale` | ≈0.217 | **1.0** | removes the artificial "decay shorter than the bar" trim |

This produces a **long-decay synthetic fixture**. The synthesis inputs other than
those two decay parameters are unchanged, but the whole render runs the capture
chain and peak-normalises again, so **the output waveform and its normalisation
differ**; no claim is made that amplitude or transient shape is unchanged. What
*is* unchanged is the ground truth: the repaired fixture has the same beat grid,
onset times, meter, chord shapes, velocities and duration as the base
(duration and truth arrays asserted equal by test):

- duration 11.35 s, 544 800 frames, 48 kHz mono 16-bit;
- beats `0.35 + i·0.625`, onsets
  `0.345645334 / 2.855720545 / 5.341555485 / 7.842999215` — identical to the base
  manifest;
- `scenarioTags`, `silentBeats`, `silenceSpans`, `downbeats` unchanged.

The output is a new corpus subtree:

```
testdata/rhythm/repaired-sustain/
  sustained_chords.wav    1 089 644 B  sha256 23b8cf21…  (<= 2 MiB)
  manifest.json           corpus id `eval006-sustain-repair`
  raw/                    real CLI runs, audit and comparison
```

The new manifest references the eighteen originals by a path computed relative
to the **actual** `--out`, so a custom output directory still resolves (see
below). Their `sha256`, `bytes`, truth arrays and `trueSilenceSpans` are
inherited verbatim, so **all 19 original WAV/manifest hashes and every historical
result remain valid**. Only one new WAV exists in the subtree.

## Independent PCM duration / energy audit

Measured directly from the committed 16-bit bytes (50 ms RMS frames, 10 ms hop,
`s/32768`), independent of the generator's decay model:

| quantity | original | repaired |
|---|---:|---:|
| duration | 11.35 s / 544 800 frames | 11.35 s / 544 800 frames |
| peak | −6.001 dBFS | −6.001 dBFS |
| RMS | −31.935 dBFS | −29.017 dBFS |
| clipped-sample fraction | 0.0 | **0.0** |
| max single-sample step | 0.192 | **0.154** (gross-step guard bound 0.90) |
| declared true silence | 8.734 s (5 spans) | **1.43 s (2 spans)** |

The max-sample-step figure is a **gross-step guard only**: it rejects a gross
edit/splice step, it does not prove the absence of audible clicks or of an onset
discontinuity, because a band-limited transient may legitimately move several
tenths of full scale between adjacent 48 kHz samples.

**Independent onset-local envelope audit.** For each declared onset, the 10 ms
RMS rise from the window immediately before the attack to the window immediately
after it:

| onset (s) | pre-attack 10 ms | rise at +10 ms |
|---|---:|---:|
| 0.345645334 | −67.66 dBFS | +46.63 dB |
| 2.855720545 | −59.47 dBFS | +37.53 dB |
| 5.341555485 | −59.35 dBFS | +36.65 dB |
| 7.842999215 | −58.39 dBFS | +37.55 dB |

Every declared onset is a measurable energy rise (weakest +36.65 dB). This is a
presence check, not an arbitrary dB threshold claimed as a physical law.

**Acoustic persistence.** For each onset, the 50 ms level at +0.5 s and +1.5 s
after the attack, against the file's own sounding threshold (−59.78 dBFS =
max(5th-percentile floor + 6 dB, 99th-percentile ref − 45 dB)):

| onset (s) | level at +0.5 s | level at +1.5 s |
|---|---:|---:|
| 0.345645334 | −30.11 dBFS | −45.02 dBFS |
| 2.855720545 | −29.98 dBFS | −46.20 dBFS |
| 5.341555485 | −28.04 dBFS | −44.75 dBFS |
| 7.842999215 | −31.71 dBFS | −45.27 dBFS |

All eight probes are 14–30 dB **above** the sounding threshold: the fixture is
acoustically present both beyond 0.5 s and at least 1.5 s after every attack.
The original fixture fails the same eight probes (all at ≈−63 dBFS, 5 dB below
its own threshold) — the test asserts both directions. `build()`/`--check`
re-run these validators before accepting a generated fixture, so a bad render
cannot ship.

## Independent `trueSilenceSpans` derivation

For the repaired fixture only, `trueSilenceSpans` is recomputed from the PCM with
a documented model-free criterion (not the generator's fast-decay formula):

1. 50 ms RMS frames, 10 ms hop;
2. `L_ref` = 99th percentile of frame level (−17.17 dBFS), `L_floor` = 5th
   percentile (−65.78 dBFS);
3. a frame is **sounding** iff `level ≥ max(L_floor + 6 dB, L_ref − 45 dB)` =
   −59.78 dBFS;
4. maximal silent runs ≥ 0.25 s become spans, rounded conservatively (start up,
   end down), and are verified never to contain an onset.

Result: `[[0.0, 0.3], [10.22, 11.35]]` — the lead-in before the first strum and
the final ring-out, i.e. 1.43 s of genuine acoustic silence.

**The distinction is preserved and is the point.** `silentBeats` (12) and
`silenceSpans` (12 ±30 ms windows around unplayed grid beats) are still declared
unchanged: those beats get **no new attack**, but the chord is still ringing, so
they are *not* acoustic silence. Measuring a false-beat rate over those windows
would count correct grid holdover as fabrication — exactly the conflation the
corpus README warns against. The repaired file keeps the conceptual
missing-onset field and adds a separately-measured acoustic field.

### Harness limitation (reported, not patched)

The shared CLI still labels `sustained_chords` `CorpusDefect` by *fixture name*
(`tools/rhythm-eval/Metrics.cpp`, `isCorpusDefectiveSilenceFixture`). That
hard-code is outside this task's ownership and now stale for the repaired
fixture, whose silence is genuine and independently measured. Removing the list
is a separate harness change and is recorded as a follow-up only. The raw CLI
output therefore shows `falseBeatCoverage = CorpusDefect` for both the original
and repaired sustained fixture even though only the repaired fixture's silence
is a real performance. No gate is read from it here.

## `tapping_muting_only` audit — high silent occupancy is not a defect

`CORPUS-ACOUSTIC-REVIEW.md` warned that brief percussive taps legitimately have
high silent occupancy and must not be excluded on that basis. The independent
onset-spacing/energy audit (`raw/audit.json`) confirms the fixture is
rhythmically usable and it is left untouched (`sha256 5e3b516a…`):

- 30 declared onsets over 12.064 s; inter-onset gaps median 0.398 s, mean
  0.351 s, IQR 0.397 s, min 0.128 s, max 0.541 s — a regular grid with the
  expected short within-beat taps;
- every tap produces a large measurable energy rise: median post-onset level
  −34.6 dBFS, **minimum rise 34.3 dB** over the preceding 50 ms, so all 30
  attacks are real transients, not noise.

So this fixture is **not** excluded and **not** repaired. Its silent occupancy is
a property of tap playing, not evidence of a defect.

## Real CLI evidence — current main, both backends, block 128, uncompensated

The CLI and plugins are the read-only EVAL-005 build roots
`/home/mojo/projects/build-EVAL-005/main-{cli,core}`, built from the main source
export `ad7872f` (jam-core release, both backends ON). That is the **tooling
pin**; this task's git base is the separate commit `6287288`. Run at `--block
128`, uncompensated, no legacy stamping. Exact commands and elapsed times live
in each `raw/<corpus>/<backend>/block128/run.txt`.

| corpus | backend | `results.json` sha256 |
|---|---|---|
| original | btrack | `ab17c7ba…` |
| original | aubio | `0f2bd13b…` |
| repaired | btrack | `2e4cebf9…` |
| repaired | aubio | `deb430e5…` |

**Denominators are unchanged: 19 fixtures, 11 core fixtures on both manifests
and both backends.** A test asserts this, and also that every non-sustained
fixture's scored metrics are byte-identical across the two runs (only the
non-scored wall-clock `cpuSeconds` moves), so the only audio change is the
sustain fixture.

**Custom `--out` verified.** A fresh corpus built with
`repair_sustained.py --out <scratch>` was driven by the same CLI for both
backends: exit 0, `fixtureCount` 19 each. The tool's own test additionally
resolves and re-hashes all 19 entries from a custom output directory. See
`raw/custom-out-verify.txt`.

### `sustained_chords` deltas

Detection is scored against the **metric-grid beats** (16 of them), not against
the 4 declared strum onsets; the F-measure improvement below is in grid-beat
matching.

| metric | btrack orig → rep | aubio orig → rep |
|---|---|---|
| predicted beats / truth grid | 11 → 17 / 16 | 10 → 18 / 16 |
| true / false positives | 6 → 12 / 5 → 5 | 5 → 10 / 5 → 8 |
| precision | 0.545 → 0.706 | 0.500 → 0.556 |
| recall | 0.375 → 0.750 | 0.313 → 0.625 |
| **F-measure (grid beats)** | **0.444 → 0.727 (+0.283)** | **0.385 → 0.588 (+0.204)** |
| acquired | yes → yes | **no → yes** |
| acquisition seconds | 6.86 → 3.77 | − → 5.62 |
| locked BPM error | 0.0031 (unchanged) | 0.0133 → 0.0134 |
| phase mean abs (ms) | 10.1 → 36.0 | 24.8 → 25.6 |
| declared true silence (s) | 8.734 → 1.430 | 8.734 → 1.430 |

The defect understated both backends' detection on this scenario; with a
long-decay fixture the same trackers match more grid beats, and aubio now
acquires a lock it never acquired before. This is a **corpus** difference, not a
tracker verdict: the phase error rises for BTrack (a softer, slower-bloom attack
is matched later against the humanised grid), and non-sustained fixture results
are unchanged.

### Aggregate and gates (not a selection)

| aggregate | btrack orig → rep | aubio orig → rep |
|---|---|---|
| `fMeasureMean` | 0.710 → 0.725 | 0.536 → 0.546 |
| `recallMean` | 0.723 → 0.743 | 0.472 → 0.489 |
| acquisition within 2 bars (core) | 4/11 → 5/11 | 7/11 → 7/11 |
| worst core BPM error | 0.0234 (unchanged) | 0.0133 → 0.0134 |
| SPEC 19 gate booleans | unchanged | unchanged |

No SPEC 19 gate flips (BTrack still fails acquisition and the 2 % BPM gate;
aubio still fails acquisition). **No pass is claimed from this repair.** The
only claim is that the sustained-chord scenario is now a valid long-decay
synthetic probe with measured persistence.

Full tables: [`raw/comparison.md`](../../testdata/rhythm/repaired-sustain/raw/comparison.md)
and [`raw/comparison.json`](../../testdata/rhythm/repaired-sustain/raw/comparison.json).

## Reproducibility and provenance

- Base manifest `testdata/rhythm/manifest.json` sha256 `06fe2c43…` (unmodified).
- Repaired manifest `testdata/rhythm/repaired-sustain/manifest.json` sha256
  `9b010455…`; repaired WAV sha256 `23b8cf21…` (seeded audio, unchanged by the
  wording/metadata corrections).
- `python3 tools/rhythm-eval/tools/repair_sustained.py --check` regenerates the
  corpus into scratch, re-runs the fixture validators, and compares the manifest
  (after normalising layout-dependent reference paths) and the WAV bytes:
  byte-reproducible.
- `--audit` prints the PCM audit into `raw/audit.json`; `--compare` regenerates
  `raw/comparison.{json,md}`.
- Test suite `tools/rhythm-eval/tools/test_repair_sustained.py`: **37 tests**,
  all green — identity/preservation (including re-hashing all 19 original WAVs),
  persistence/no-clipping/gross-step guard, onset-local envelope presence,
  independent silence re-derivation, reproducibility and custom-`--out`
  resolution+hash of all 19 entries, tapping audit, raw-delta checks, and
  **physical-byte** negative tests (a truncated WAV, a clipped WAV, a spliced
  WAV and a wrong-sample-rate WAV are each rejected).

## Limitations

- **Synthetic.** The repaired chord is the EVAL-001 Karplus-Strong model with its
  own free-string T60. This is a long-decay synthetic fixture with measured
  persistence, **not** established physical realism and **not** a real
  performance; the EVAL-001 README's "What this corpus can and cannot tell you"
  applies in full.
- The repaired `trueSilenceSpans` is an artifact of *this* documented RMS
  criterion on *this* fixture; the 45 dB depth / 6 dB margin / 0.25 s floor are
  stated constants, not a physical law. The gross-step guard and the envelope
  audit are descriptive, not physical proofs.
- The 18 untouched fixtures keep their original generator-derived
  `trueSilenceSpans`; only the replaced fixture is re-measured.
- The shared CLI's by-name `CorpusDefect` list still tags the repaired fixture
  (see above); the scorer is not changed. Follow-up only.
- The `raw/` CLI runs are resource diagnostics on a shared machine; `cpuSeconds`
  is wall-clock and not comparable across runs.

## Handoff

Authoritative evidence: this report, `testdata/rhythm/repaired-sustain/raw/`
(`audit.json`, `comparison.{json,md}`, `custom-out-verify.txt`, four CLI runs) and
the two new tools. The base corpus and every historical result are untouched.
G3 stays open; no tracker selected, no production wiring, no ADR.
