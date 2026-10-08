# EVAL-006 — scoped sustain repair (corpus follow-up)

## Goal

Remove the one measured defect that made the EVAL-001 corpus unusable for the
`sustained_chords` scenario: the fixture's chord attacks collapsed by 30–35 dB
within 300 ms and reached the noise floor by 500 ms, so 8.73 s of the 11.35 s
file was declared "true silence". Produce a deterministic replacement whose
chords physically persist across the ~2.5 s event spacing, keep the truth event
times / meter / duration identical, re-derive its acoustic silence independently,
audit `tapping_muting_only` rather than excluding it, and run the current-main
CLI on the original and repaired manifests for both backends.

## Base and scope

- Base commit `6287288` (`merge(EVAL-005) …`). Worktree
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
  was modified. `git status` shows only the new paths above.
- No selection ADR, no production wiring, no gate tuning. G3 stays **OPEN**.

## What was consumed

- `docs/research/CORPUS-ACOUSTIC-REVIEW.md` (the independent defect measurement)
  and `testdata/rhythm/tools/gen_fixtures.py` + `testdata/rhythm/manifest.json`
  (read-only) for the physical model, seeded RNG and original ground truth.
- The **current-main** CLI and plugins in the read-only EVAL-005 build roots
  `/home/mojo/projects/build-EVAL-005/main-{cli,core}` (`ad7872f`/main sources,
  jam-core both backends ON).

## Contract implemented

`repair_sustained.py` (stdlib only) imports the committed generator read-only and
re-renders the exact base event list with only the decay changed:
`mix_t60 = 1.0` (the model's own free-string T60 table) and per-event
`t60_scale = 1.0` (removing `p_sustained_chords`' artificial "decay shorter than
the bar" trim). Beat grid, onsets, meter, chords, velocities and duration are
byte-identical to the base fixture (asserted by test).

It writes `testdata/rhythm/repaired-sustain/`:

- `sustained_chords.wav` (1 089 644 B ≤ 2 MiB, sha256 `23b8cf21…`);
- `manifest.json` with a **new corpus id** `eval006-sustain-repair`, base-manifest
  hash, and explicit replacement provenance; the eighteen untouched fixtures are
  referenced as `../wav/<name>.wav` with `sha256`/`bytes`/truth inherited
  verbatim, so **all 19 original WAV/manifest hashes and historical results
  survive** and no original WAV is duplicated.

**Independent `trueSilenceSpans`.** For the repaired fixture only, silence is
re-measured from the PCM with a documented model-free criterion (50 ms RMS
frames, 10 ms hop; sounding iff level ≥ max(5th-percentile floor + 6 dB,
99th-percentile ref − 45 dB); spans ≥ 0.25 s, conservatively rounded, never
containing an onset). Result `[[0.0, 0.3], [10.22, 11.35]]` = 1.43 s. The
`silentBeats` / `silenceSpans` **missing-onset** fields are unchanged, preserving
the distinction between "no attack on this beat" and "acoustically silent".

**Tapping audit.** `onset_energy_audit` reports spacing and post-onset energy for
`tapping_muting_only`; 30 onsets, median gap 0.398 s, minimum energy rise 34.3 dB.
The fixture is **not** excluded or repaired — high silent occupancy is a property
of tap playing.

## Tests executed

```
python3 tools/rhythm-eval/tools/test_repair_sustained.py
Ran 31 tests ... OK
```

Coverage: new corpus id / replacement provenance; 18 originals referenced (not
copied) with matching hashes and truth; **all 19 original WAVs re-hashed against
the base manifest**; only one WAV in the subtree; ≤ 2 MiB; new
WAV hash/size match; event times / meter / duration preserved; core denominator
stays 11. **Acoustic**: every onset is above the file's sounding threshold at
+0.5 s and +1.5 s; the original fails the same probes; no clipping; no
edit/splice step. **Silence**: recomputation matches the manifest; declared spans
are actually quiet; spans are onset-free; the repaired spans differ from the
original 8.73 s artifact; a missing-onset window is proven *not* silent.
**Reproducibility**: `--check` returns 0 and a subprocess run reproduces the
manifest and WAV bytes. **Failure detection**: truncated duration, fast decay,
clipping and an injected splice each fail their validators. **Raw evidence**:
both backends' core denominators are 11, every non-sustained fixture's scored
metrics are byte-identical (only wall-clock `cpuSeconds` moves), and sustained
F-measure improves for both. The real CLI runs referenced below were produced by
the tool's own documented command, not by the test suite.

## Real CLI runs (current main, both backends, block 128, uncompensated)

```
rhythm-eval --corpus testdata/rhythm                     --out raw/original/<b>/block128 \
            --backend <b> --backend-lib librhythm-eval-<b>.so --block 128
rhythm-eval --corpus testdata/rhythm/repaired-sustain    --out raw/repaired/<b>/block128 \
            --backend <b> --backend-lib librhythm-eval-<b>.so --block 128
```

Raw output is committed under `testdata/rhythm/repaired-sustain/raw/`
(`original|repaired/{btrack,aubio}/block128/`), with `run.txt` recording the exact
command and exit code.

- **Denominators unchanged: 19 fixtures, 11 core fixtures** on both manifests and
  both backends.
- `sustained_chords` F-measure: BTrack **0.444 → 0.727**, aubio **0.385 → 0.588**;
  aubio acquires a lock (`acquired` no → yes). Declared true silence 8.734 s →
  1.430 s.
- Aggregate `fMeasureMean`: BTrack 0.710 → 0.725, aubio 0.536 → 0.546; core
  acquisition beyond 2 bars BTrack 4/11 → 5/11, aubio 7/11 → 7/11.
- **No SPEC 19 gate flips**; no pass is claimed. BTrack still fails acquisition
  and the 2 % BPM gate. This is a corpus-validity result, not a tracker verdict.

## Harness issues detected (reported, not rewritten)

1. The shared CLI's `isCorpusDefectiveSilenceFixture` hard-codes
   `sustained_chords` / `tapping_muting_only` by name, so the repaired fixture is
   still reported `CorpusDefect`. The hard-code is now stale for the repaired
   fixture (whose silence is genuine and independently measured) but is outside
   this task's ownership. Removing it is a separate harness change.
2. BTrack's phase error on the repaired fixture rises (10.1 → 36.0 ms mean): the
   softer/slower-bloom sustained attack is matched later against the humanised
   grid. Reported as measured; the F-measure still improves.

## Limitations

Synthetic (EVAL-001 physical model only; see the corpus README's limitations, all
of which still apply). The independent silence criterion is a stated RMS rule,
not a physical law. Only the replaced fixture is re-measured; the other eighteen
keep generator-derived spans. `cpuSeconds` is wall-clock and not comparable.

## Handoff

Authoritative evidence: `docs/research/SUSTAIN-REPAIR.md`,
`testdata/rhythm/repaired-sustain/raw/` (audit, comparison, four CLI runs) and the
two new tools. Re-derive with:

```
python3 tools/rhythm-eval/tools/repair_sustained.py --check
python3 tools/rhythm-eval/tools/repair_sustained.py --audit
python3 tools/rhythm-eval/tools/repair_sustained.py --compare
python3 tools/rhythm-eval/tools/test_repair_sustained.py
```

## Final commit SHA

- Implementation + evidence + this note: `<FILLED BY NEXT COMMIT>`.
