# EVAL-007 — bounded scorer coverage correction

## Goal

Correct the shared rhythm-eval scorer's true-silence coverage. EVAL-006 measured
the old by-NAME `CorpusDefect` exclusion (`sustained_chords`,
`tapping_muting_only`) invalid: the repaired `sustained_chords` reuses the name
with genuine, independently measured silence, and `tapping_muting_only`'s sparse
occupancy was found to be a property of tap playing, not a defect. Replace the
name gate with coverage keyed on the **declared WAV sha256** carried as audio
identity, add an explicit `NotAssessedStructuralNoise` status for derived-noise
clips whose inherited spans are structural, and keep every other scored metric,
threshold, acquisition definition, holdover count and the NOT-MEASURED
silence-acceleration gate unchanged.

## Base and scope

- Task git base `664041c` (`merge(EVAL-006) …`). Worktree
  `/home/mojo/projects/worktrees/EVAL-007-silence-coverage`, branch
  `wp/EVAL-007-silence-coverage`.
- Owned and touched:

  ```
  tools/rhythm-eval/Metrics.h
  tools/rhythm-eval/Metrics.cpp
  tools/rhythm-eval/Manifest.cpp
  tests/jam/RhythmEvalMetricsTests.cpp        (one stale assertion updated)
  tests/jam/RhythmSilenceCoverageTests.cpp    (new suite)
  docs/research/SILENCE-COVERAGE.md
  docs/research/silence-coverage/**           (new artifact dir)
  task-notes/EVAL-007.md
  ```

- Not touched: corpora, audio, generators, raw results, trackers, `vendor/`,
  application, shared CMake, ledger, `HANDOFF.md`, `DEVPLAN.md`, the robustness
  aggregator. No tracker selected, no production wiring, no gate tuning. G3 open.

## Contract implemented

1. **Identity.** `RhythmTruth`/`FixtureMetrics` carry the declared manifest
   `sha256` (`Manifest.cpp::toTruth`), serialised as `sourceSha256` in JSON/CSV.
   The declared hash is an identity string, **not** PCM authentication; the
   harness trusts externally verified manifests and the evidence runs re-hash.
2. **Hash-keyed defect registry.** `isCorpusDefectiveSilenceFixture` is removed.
   A single reviewed entry (`sustained_chords`, original WAV sha256
   `e4b9297f…`, citation `docs/research/SUSTAIN-REPAIR.md` /
   `CORPUS-ACOUSTIC-REVIEW.md`) is matched by hash, so a renamed copy stays
   `CorpusDefect` and the repaired same-name render is `Measured`.
3. **Structural noise.** `FalseBeatCoverage::NotAssessedStructuralNoise` is
   appended (`= 3`) for derived `noise`+`derived` clips; raw counts are retained,
   the informative flag is false, and no numeric occupancy threshold is used.
4. **No-spans.** `NoTrueSilence` wins over the registry.
5. **Propagation.** Per-fixture JSON/CSV carry identity, coverage, citation and
   raw counts; aggregate JSON gains `trueSilenceStructuralNoiseFixtures`; the
   Markdown summary reports the new status and the hash-based criterion.
6. **Compatibility.** Existing enum values/strings are unchanged; new value is
   appended. `falseBeatMetricInformative` keeps its documented `== Measured`
   meaning. CSV gains raw-count columns; these additive changes are the exact
   compatibility surface reported.

## Tests

New `tests/jam/RhythmSilenceCoverageTests.cpp` (suite `RhythmSilenceCoverage`,
8 tests / 61 checks): original vs repaired same-name hash; renamed original hash;
unknown/missing-hash compatibility; tapping `Measured`; derived-noise
`NotAssessedStructuralNoise` with raw counts kept; no-spans `NoTrueSilence`;
aggregate + JSON + CSV propagation without a misleading measured flag; and a
**re-hash of the three reviewed WAVs from bytes**. One stale assertion in
`RhythmEvalMetricsTests.cpp` (name-only synthetic fixtures) was updated to the new
identity semantics.

## Commands executed

Independent jam-core build, both trackers enabled, from the task sources:

```
cmake -S jam-core -B build-EVAL-007/jam-core-enabled -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DJAM_CORE_BUILD_TESTS=ON \
      -DJAM_ENABLE_BTRACK=ON -DJAM_ENABLE_AUBIO=ON
cmake --build build-EVAL-007/jam-core-enabled -j2
ctest --test-dir build-EVAL-007/jam-core-enabled --output-on-failure
```

Result: **15/15 suites pass** (14 pre-existing + new `jam.RhythmSilenceCoverage`).

CLI built from the task sources and driven with the pinned read-only EVAL-005
plugins, block 128, uncompensated, over original-19 / repaired-19 / derived-24:

```
cmake -S tools/rhythm-eval -B build-EVAL-007/cli -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-rdynamic
rhythm-eval --corpus <c> --out docs/research/silence-coverage/raw/<c>/<b>/block128 \
            --backend <b> --backend-lib build-EVAL-005/main-core/librhythm-eval-<b>.so \
            --block 128
```

Raw output (1.8 MiB ≤ 5 MiB) plus `comparison.{json,md}` and the report
`docs/research/SILENCE-COVERAGE.md` are committed under
`docs/research/silence-coverage/`.

## Results

- Coverage: original keeps one `CorpusDefect` (`sustained_chords` `e4b9297f…`),
  `tapping_muting_only` is now `Measured`, repaired `sustained_chords` is
  `Measured`, and the five derived noise clips are
  `NotAssessedStructuralNoise`.
- **All non-coverage scored metrics are byte-identical to the prior artefacts**
  (`testdata/rhythm/repaired-sustain/raw/{original,repaired}`,
  `docs/research/robustness/raw`) on each input corpus: 0 diffs across 19+19+24
  fixtures × 2 backends, per fixture and aggregate. Only coverage/identity fields
  and wall-clock `cpuSeconds` differ.
- The **unmodified** robustness aggregator re-derives 2160 rows / 24 pairs / 0
  hard errors from the new derived runs; the only differing rows are
  `resources.cpuSeconds` and the `silence.falseBeatCoverage` string for the 10
  noise-clip rows.
- Gates reproduce EVAL-006 (btrack 4/11 and 5/11 acquisition; aubio 7/11; BPM
  worst core 0.0234 / 0.0133 / 0.0134); both acquire gates still FAIL, no tracker
  selected; the silence-acceleration gate stays NOT-MEASURED.
- All **62** declared WAV entries across the three manifests re-hashed from
  bytes: 0 mismatches.

## Executed vs not

Executed: scorer/test edits, 15-suite enabled jam-core ctest, six pinned CLI
runs, per-fixture/aggregate comparison, all-inputs re-hash, aggregator
compatibility re-derivation.

Not done (deliberately, to avoid overlap): acquisition-gate diagnosis in an
isolated tool copy (separate worker); RT-path probing of the actual processor
(RT worker); any change to shared CMake, corpora, generators, raw results,
trackers, vendor, application, ledger, `HANDOFF.md`/`DEVPLAN.md`, or the
robustness aggregator; tracker selection/ADR.

## Handoff

Authoritative evidence: `docs/research/SILENCE-COVERAGE.md`,
`docs/research/silence-coverage/` (`comparison.{json,md}` + six raw runs),
`tests/jam/RhythmSilenceCoverageTests.cpp`, and the scorer changes in
`tools/rhythm-eval/{Metrics.h,Metrics.cpp,Manifest.cpp}`.

## Final commit SHA

- Round 1 (implementation + tests + evidence + report + this note):
  `3873be025eaa1af04f116851da1e50d2fc306d01`.
- The note-SHA update is the subsequent commit on
  `wp/EVAL-007-silence-coverage`; the branch head is the handoff SHA reported to
  the orchestrator.
