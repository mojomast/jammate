# EVAL-006 — scoped sustain repair (corpus follow-up)

## Goal

Remove the one measured defect that made the EVAL-001 corpus unusable for the
`sustained_chords` scenario: the fixture's chord attacks collapsed by 30–35 dB
within 300 ms and reached the noise floor by 500 ms, so 8.73 s of the 11.35 s
file was declared "true silence". Produce a deterministic long-decay replacement,
keep the truth event times / meter / duration identical, re-derive its acoustic
silence independently, audit `tapping_muting_only` rather than excluding it, and
run the current-main CLI on the original and repaired manifests for both
backends.

## Base and scope

- Task git base `6287288` (`merge(EVAL-005) …`). Worktree
  `/home/mojo/projects/worktrees/EVAL-006-sustain`, branch
  `wp/EVAL-006-sustain`.
- Only these **new** paths are owned and touched:

  ```
  tools/rhythm-eval/tools/repair_sustained.py
  tools/rhythm-eval/tools/test_repair_sustained.py
  testdata/rhythm/repaired-sustain/**
  docs/research/SUSTAIN-REPAIR.md
  task-notes/EVAL-006.md
  ```

- No old corpus WAV, generator, manifest, historical result, shared harness
  (`tools/rhythm-eval/*.cpp/.h`), CMake file, ledger, `src/` or `vendor/` file
  was modified.
- No selection ADR, no production wiring, no gate tuning. G3 stays **OPEN**.

## What was consumed

- `docs/research/CORPUS-ACOUSTIC-REVIEW.md` (the independent defect measurement)
  and `testdata/rhythm/tools/gen_fixtures.py` + `testdata/rhythm/manifest.json`
  (read-only) for the model, seeded RNG and original ground truth.
- The **tooling pin** is the current-main source export `ad7872f` (not this
  task's git base): the CLI and plugins used are the read-only EVAL-005 build
  roots `/home/mojo/projects/build-EVAL-005/main-{cli,core}`, jam-core both
  backends ON. `ad7872f` (tooling) and `6287288` (task base) are distinct.

## Contract implemented

`repair_sustained.py` (stdlib only) imports the committed generator read-only and
re-renders the same event list with only the decay changed: `mix_t60 = 1.0` (the
model's own free-string T60 table) and per-event `t60_scale = 1.0` (removing
`p_sustained_chords`' artificial "decay shorter than the bar" trim). This yields
a **long-decay synthetic fixture**; the rendered waveform and normalisation
differ (no claim that amplitude or transients are unchanged), while the ground
truth is preserved: beat grid, onsets, meter, chords, velocities and duration are
the base generator's.

It writes `testdata/rhythm/repaired-sustain/`:

- `sustained_chords.wav` (1 089 644 B ≤ 2 MiB, sha256 `23b8cf21…`);
- `manifest.json` with a **new corpus id** `eval006-sustain-repair`, base-manifest
  hash, and explicit replacement provenance; the eighteen untouched fixtures are
  referenced by a path computed relative to the **actual `--out`** (so a custom
  output directory resolves) with `sha256`/`bytes`/truth inherited verbatim, so
  **all 19 original WAV/manifest hashes and historical results survive** and no
  original WAV is duplicated. The committed default references remain `../wav/…`.

`build()`/`--check` **self-validate** the generated fixture before accepting it:
duration preserved, no clipping, gross-step guard, onset-local envelope presence,
acoustic persistence at +0.5 s/+1.5 s, and the true-silence span invariants.
`--check` regenerates into scratch and compares the manifest (after normalising
layout-dependent reference paths) and the WAV bytes.

**Independent `trueSilenceSpans`.** For the repaired fixture only, silence is
re-measured from the PCM with a documented model-free criterion (50 ms RMS
frames, 10 ms hop; sounding iff level ≥ max(5th-percentile floor + 6 dB,
99th-percentile ref − 45 dB); spans ≥ 0.25 s, conservatively rounded, never
containing an onset). Result `[[0.0, 0.3], [10.22, 11.35]]` = 1.43 s. The
`silentBeats` / `silenceSpans` **missing-onset** fields are unchanged, preserving
the distinction between "no attack on this beat" and "acoustically silent".

**Tapping audit.** `onset_energy_audit` reports spacing and post-onset energy for
`tapping_muting_only`; 30 onsets, median gap 0.398 s, minimum energy rise 34.3 dB.
The fixture is **not** excluded or repaired.

## Tests executed

```
python3 tools/rhythm-eval/tools/test_repair_sustained.py
Ran 37 tests ... OK
```

Coverage: new corpus id / replacement provenance; 18 originals referenced (not
copied) with matching hashes and truth; **all 19 original WAVs re-hashed against
the base manifest**; only one WAV in the subtree; ≤ 2 MiB; new WAV hash/size
match; event times / meter / duration preserved; core denominator stays 11.
**Acoustic**: every onset above the file's sounding threshold at +0.5 s and
+1.5 s; the original fails the same probes; no clipping; gross-step guard; an
independent onset-local envelope audit shows every declared onset is a real
energy rise. **Silence**: recomputation matches the manifest; declared spans are
quiet; spans are onset-free; the repaired spans differ from the original 8.73 s
artifact; a missing-onset window is proven *not* silent. **Reproducibility**:
`--check` returns 0; a subprocess run reproduces the manifest and WAV bytes; a
**custom `--out` resolves and re-hashes all 19 entries**. **Failure detection**:
in-memory mutants *and* physical bytes (a truncated WAV, a clipped WAV, a spliced
WAV, a wrong-sample-rate WAV) are each rejected. **Raw evidence**: both backends'
core denominators are 11, every non-sustained fixture's scored metrics are
byte-identical (only wall-clock `cpuSeconds` moves), and sustained F-measure
improves for both.

## Real CLI runs (current-main CLI/plugins, both backends, block 128, uncompensated)

```
rhythm-eval --corpus testdata/rhythm                     --out raw/original/<b>/block128 \
            --backend <b> --backend-lib librhythm-eval-<b>.so --block 128
rhythm-eval --corpus testdata/rhythm/repaired-sustain    --out raw/repaired/<b>/block128 \
            --backend <b> --backend-lib librhythm-eval-<b>.so --block 128
```

Raw output is committed under `testdata/rhythm/repaired-sustain/raw/`
(`original|repaired/{btrack,aubio}/block128/`), with `run.txt` recording the
exact command and exit code. A custom-`--out` corpus was also driven by the same
CLI for both backends: exit 0, `fixtureCount` 19 (`raw/custom-out-verify.txt`).

- **Denominators unchanged: 19 fixtures, 11 core fixtures** on both manifests and
  both backends.
- `sustained_chords` grid-beat F-measure: BTrack **0.444 → 0.727**, aubio
  **0.385 → 0.588**; aubio acquires a lock (no → yes). Declared true silence
  8.734 s → 1.430 s. (Detection is scored against the 16 metric-grid beats, not
  the 4 declared strum onsets.)
- Aggregate `fMeasureMean`: BTrack 0.710 → 0.725, aubio 0.536 → 0.546; core
  acquisition within 2 bars BTrack 4/11 → 5/11, aubio 7/11 → 7/11.
- **No SPEC 19 gate flips**; no pass is claimed. This is a corpus-validity
  result, not a tracker verdict.

## Harness issues detected (reported, not rewritten)

1. The shared CLI's `isCorpusDefectiveSilenceFixture` hard-codes
   `sustained_chords` / `tapping_muting_only` by name, so the repaired fixture is
   still reported `CorpusDefect` even though its 1.43 s of silence is genuine and
   independently measured. This is a recorded follow-up only; the shared scorer
   is **not** changed.
2. BTrack's phase error on the repaired fixture rises (10.1 → 36.0 ms mean): the
   softer/slower-bloom attack is matched later against the humanised grid.
   Reported as measured; the F-measure still improves.

## Limitations

Synthetic (EVAL-001 model only; the corpus README's limitations all still apply).
The independent silence criterion, the gross-step guard and the envelope audit
are descriptive/stated rules, not physical proofs; no claim of established
physical realism or of a real performance. Only the replaced fixture is
re-measured; the other eighteen keep generator-derived spans. `cpuSeconds` is
wall-clock and not comparable.

## Handoff

Authoritative evidence: `docs/research/SUSTAIN-REPAIR.md`,
`testdata/rhythm/repaired-sustain/raw/` (audit, comparison, custom-out check,
four CLI runs) and the two new tools. Re-derive with:

```
python3 tools/rhythm-eval/tools/repair_sustained.py --check
python3 tools/rhythm-eval/tools/repair_sustained.py --audit
python3 tools/rhythm-eval/tools/repair_sustained.py --compare
python3 tools/rhythm-eval/tools/test_repair_sustained.py
```

## Final commit SHA

- Round 1 (implementation + evidence + note): `26b5a20`.
- Round 2 (integration-review corrections — custom `--out`, wording/overclaim,
  build-time validators, physical-byte tests, tooling-pin wording):
  `d904287b`.
- The note-SHA update is the subsequent commit on `wp/EVAL-006-sustain`; the
  branch head is the handoff SHA reported to the orchestrator.
