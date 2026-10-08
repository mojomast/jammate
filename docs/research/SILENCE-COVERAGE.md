# Bounded silence-coverage correction — EVAL-007

Scoped correction of the shared rhythm-eval scorer's true-silence coverage
classification. The by-NAME `CorpusDefect` exclusion (`sustained_chords`,
`tapping_muting_only`) that EVAL-006 invalidated is replaced by coverage keyed on
the **declared WAV sha256** (audio identity), and a derived-noise clip whose
silence spans are structural is reported as an explicit non-assessed status.

This is a **diagnostic/scorer-coverage** artifact. It selects no tracker, tunes no
gate, wires no production path and makes no live-safety claim. G3 stays **OPEN**.

## The defect being corrected

`tools/rhythm-eval/Metrics.cpp` classified the true-silence false-beat metric by
fixture name:

```cpp
bool isCorpusDefectiveSilenceFixture (const RhythmTruth& truth)
{
    return truth.name == "sustained_chords"
           || truth.name == "tapping_muting_only";
}
```

EVAL-006 measured both halves of that assumption wrong:

- `docs/research/SUSTAIN-REPAIR.md` re-rendered `sustained_chords` with a real
  decay (WAV sha256 `23b8cf21…`) while keeping the same name and ground truth.
  Its 1.43 s of true silence is independently derived from the PCM. The old list
  labelled that repaired fixture `CorpusDefect` anyway — censoring a valid probe.
- The independent acoustic review (`CORPUS-ACOUSTIC-REVIEW.md`) found
  `tapping_muting_only`'s high silent occupancy is a property of tap playing, not
  a synthesis defect: 30 onsets, median gap 0.398 s, every attack a measurable
  energy rise (minimum +34.3 dB). It must not be excluded.

A name cannot decide coverage when one name can name two different recordings.

## Contract implemented (owned paths only)

Owned and touched:

```
tools/rhythm-eval/Metrics.h
tools/rhythm-eval/Metrics.cpp
tools/rhythm-eval/Manifest.cpp
tests/jam/RhythmEvalMetricsTests.cpp      (one stale assertion updated)
tests/jam/RhythmSilenceCoverageTests.cpp  (new suite)
docs/research/SILENCE-COVERAGE.md         (this file)
docs/research/silence-coverage/**         (this new artifact dir)
task-notes/EVAL-007.md
```

No corpus WAV, generator, historical result, vendor file, shared CMake file,
ledger, `HANDOFF.md`, `DEVPLAN.md`, `src/jam/**`, or the robustness aggregator
(`tools/rhythm-eval/tools/run_robustness.py`) was modified.

### 1. Audio identity

`RhythmTruth` and `FixtureMetrics` now carry the declared manifest `sha256`
(`Manifest.cpp::toTruth` copies it). It is serialised as `sourceSha256` in the
per-fixture JSON and CSV so every score records which audio it came from.

The declared hash is an **identity string, not authentication of the PCM**. The
harness trusts manifests that were verified externally; a run that needs
audio-identity evidence must re-hash the inputs itself, which the evidence section
does. Comparing hashes here is corpus bookkeeping, not a copyright or
authenticity conclusion.

### 2. Hash-keyed defect registry

`isCorpusDefectiveSilenceFixture` is deleted. In its place is a reviewed registry
whose match key is the exact WAV hash, with a citation carried into the output:

| name (citation only) | sha256 (match key) | citation |
|---|---|---|
| `sustained_chords` | `e4b9297fca341e70a639fc6a51fbd9e884c1321ee4fc802c5c3497feeefe6442` | EVAL-006 `docs/research/SUSTAIN-REPAIR.md` / `CORPUS-ACOUSTIC-REVIEW.md` |

Consequences, all pinned by tests:

- original fast-decay bytes → `CorpusDefect`, **even if renamed** (hash identity);
- repaired same-name render `23b8cf21…` → `Measured` when its own spans are valid;
- `tapping_muting_only` `5e3b516a…` → `Measured` (no registry entry; onset audit);
- unknown / missing / empty hash → `Measured` (never excluded by name);
- no declared spans → `NoTrueSilence` **regardless of the registry**.

### 3. Structural-noise coverage

A derived noise clip (`scenarioTags` contain `derived` and `noise`) inherits its
parent's `trueSilenceSpans`, but the added floor fills what used to be quiet, so
those spans are structural, not a re-measured acoustic stop. Name/source tags
alone cannot establish physical silence, so the new status
`FalseBeatCoverage::NotAssessedStructuralNoise` is emitted. The base
`noisy_microphone` fixture uses the qualifier `noisy`, not `noise`, and stays
`Measured`.

No numeric silence-occupancy threshold is introduced. The raw counts
(`falseBeatsInTrueSilence`, `falseBeatsInTrueSilencePerSecond`, the unplayed-beat
counters) are still computed and aggregated; only the coverage verdict changes.

### 4. Enum / output compatibility

`FalseBeatCoverage` gained `NotAssessedStructuralNoise = 3` **appended** after the
existing `Measured=0`, `NoTrueSilence=1`, `CorpusDefect=2`, so persisted
integer/string values for existing consumers are unchanged. `toString` renders the
new name. `falseBeatMetricInformative` keeps its documented meaning (true iff
`Measured`) and is therefore false for the new status. The aggregate JSON gains
`trueSilenceStructuralNoiseFixtures`; the CSV gains the raw-count columns
`falseBeatsInTrueSilence` and `falseBeatsInUnplayedBeatWindows`. These additive
schema changes are the exact compatibility surface; no existing field silently
changes meaning.

## Tests executed

Independent jam-core build, both trackers enabled, built from the task sources:

```
cmake -S jam-core -B build-EVAL-007/jam-core-enabled -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DJAM_CORE_BUILD_TESTS=ON \
      -DJAM_ENABLE_BTRACK=ON -DJAM_ENABLE_AUBIO=ON
ctest --output-on-failure
```

Result: **15/15 suites pass** — the 14 pre-existing suites plus the new
`jam.RhythmSilenceCoverage`:

```
jam.BTrackBackend  jam.AubioBackend  jam.AnalysisAudioRing  jam.BackendRunner
jam.DrumTransportAdapter  jam.MusicalClock  jam.RhythmCorpus  jam.RhythmDerived
jam.RhythmEvalMetrics  jam.RhythmSilenceCoverage  jam.RtSignal
jam.RhythmDerivedGenerator  jam.RhythmRobustness  jam.RhythmSustainRepair
jam.BeatNetResearch
```

The new suite (`tests/jam/RhythmSilenceCoverageTests.cpp`, 8 tests / 61 checks)
pins:

- original hash → `CorpusDefect` with citation and raw count retained; repaired
  same-name hash → `Measured`;
- renamed original hash stays `CorpusDefect`; unknown and missing hash are
  `Measured`;
- `tapping_muting_only` hash is `Measured`;
- derived-noise spans → `NotAssessedStructuralNoise` with raw counts not erased,
  while base `noisy_microphone` stays `Measured`;
- no spans → `NoTrueSilence` regardless of a defect hash;
- aggregate counts, JSON (`sourceSha256`, coverage, citation, raw count, measured
  flag) and CSV propagation;
- **re-hash of the three reviewed WAVs from bytes** matching the registry hash.

`RhythmEvalMetricsTests.cpp::silenceCoverageIsNotADurationThreshold` was updated:
its two synthetic `sustained_chords` / `tapping_muting_only` expectations (no
hash) now assert `Measured`, because a name alone no longer excludes a fixture.

## CLI evidence — both backends, block 128, uncompensated

The CLI is built from the **task sources** at base `664041c`:

```
cmake -S tools/rhythm-eval -B build-EVAL-007/cli -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXE_LINKER_FLAGS=-rdynamic
```

The backend plugins are the pinned, read-only EVAL-005 build roots
(`build-EVAL-005/main-core`, source `main ad7872f`); they contain only the tracker
adapters, not the scorer.

Six runs (2 backends × original-19 / repaired-19 / derived-24) are committed under
`docs/research/silence-coverage/raw/<corpus>/<backend>/block128/` (1.8 MiB, bounded
≤ 5 MiB), each with `results.json`, `summary.md`, `fixtures.csv`,
`per_fixture/*` and a `run.txt` recording the exact command and exit code:

```
rhythm-eval --corpus <corpus> --out .../raw/<corpus>/<backend>/block128 \
            --backend <btrack|aubio> --backend-lib .../librhythm-eval-<b>.so --block 128
```

### Coverage result

| corpus | backend | fixtures | Measured | NoTrueSilence | CorpusDefect | StructuralNoise |
|---|---|---:|---:|---:|---:|---:|
| original | btrack | 19 | 18 | 0 | 1 | 0 |
| original | aubio | 19 | 18 | 0 | 1 | 0 |
| repaired | btrack | 19 | 19 | 0 | 0 | 0 |
| repaired | aubio | 19 | 19 | 0 | 0 | 0 |
| derived | btrack | 24 | 19 | 0 | 0 | 5 |
| derived | aubio | 24 | 19 | 0 | 0 | 5 |

`original` keeps exactly one `CorpusDefect` (`sustained_chords`, hash
`e4b9297f…`); `tapping_muting_only` is now `Measured`; the repaired
`sustained_chords` is `Measured`; the five derived noise clips are
`NotAssessedStructuralNoise`.

### Non-coverage metrics are unchanged (0 diffs)

Every non-coverage scored metric was compared, per fixture and per aggregate,
against the prior artefacts (`testdata/rhythm/repaired-sustain/raw/{original,repaired}`
and `docs/research/robustness/raw`), excluding only `cpuSeconds` and the
coverage/identity fields:

| corpus | backend | fixtures | non-coverage diffs | coverage changes |
|---|---|---:|---:|---:|
| original | btrack | 19 | 0 | 1 |
| original | aubio | 19 | 0 | 1 |
| repaired | btrack | 19 | 0 | 2 |
| repaired | aubio | 19 | 0 | 2 |
| derived | btrack | 24 | 0 | 5 |
| derived | aubio | 24 | 0 | 5 |

The covered fields differ only as intended (`tapping_muting_only` and repaired
`sustained_chords` `CorpusDefect → Measured`; derived noise `Measured →
NotAssessedStructuralNoise`) plus the aggregate counters and wall-clock
`cpuSeconds`. The machine-readable record is
`docs/research/silence-coverage/comparison.json` / `.md`.

The same comparison was run through the **unmodified** robustness aggregator
(`run_robustness.py --backend btrack=<new results.json>`): 2160 rows, 24 pairs,
0 hard errors; the only differing rows are `resources.cpuSeconds` (wall clock)
and the `silence.falseBeatCoverage` string for the 10 noise-clip rows. Gate
booleans and every other curve value are unchanged. The aggregator path itself
was not modified.

### Gates — unchanged, no tracker selected

| corpus | backend | acq within 2 bars | gate 95% | BPM worst core | gate BPM 2% | F mean | gate h/d 5% |
|---|---|---:|---|---:|---|---:|---|
| original | btrack | 4/11 | FAIL | 0.0234 | FAIL | 0.7099 | PASS |
| original | aubio | 7/11 | FAIL | 0.0133 | PASS | 0.5357 | PASS |
| repaired | btrack | 5/11 | FAIL | 0.0234 | FAIL | 0.7248 | PASS |
| repaired | aubio | 7/11 | FAIL | 0.0134 | PASS | 0.5464 | PASS |
| derived | btrack | 0/0 | NOT-MEASURED | 0.0000 | NOT-MEASURED | 0.6253 | NOT-MEASURED |
| derived | aubio | 0/0 | NOT-MEASURED | 0.0000 | NOT-MEASURED | 0.5389 | NOT-MEASURED |

These reproduce the EVAL-006 numbers exactly. Both backends still fail the core
acquisition gate (4/11 and 7/11); **no tracker is selected**, and the SPEC 19
"silence does not create false acceleration" gate remains provably
**NOT-MEASURED** (the acceleration diagnostic is a raw proxy only).
`bpmRelErrorWorstCore` and `fMeasureMean` per input corpus are unchanged.

## Provenance

| object | sha256 |
|---|---|
| base commit | `664041c2c40bc5f47e6b4c8491462bc7b6718cc6` |
| CLI (`build-EVAL-007/cli/rhythm-eval`) | `464488226991a30ab92685ff43d9b83429b8b8c661076c35088a5ea1389ec6b0` |
| `librhythm-eval-btrack.so` (EVAL-005 pin) | `41e6476e60ab10832e961126fd6a17b13667cefa30c3ecd926d268c76bd65c6d` |
| `librhythm-eval-aubio.so` (EVAL-005 pin) | `61336277f3d13d7fe958cc4896c50b8761619186d0d6d6e3bc524decba593c41` |
| `testdata/rhythm/manifest.json` | `06fe2c4356dd411a90b5e4948ebeb00eeb68d82f42d26b5ad25f4797c530fab1` |
| `testdata/rhythm/repaired-sustain/manifest.json` | `9b0104553d8356f82eebb986f66735e16cf1d2d4623b4d66b1048a693798486a` |
| `testdata/rhythm/derived/manifest.json` | `3bb6d350f534b6d6f3208ca1ef5c0f29cf7c7069b3a9771c13f191385c5cc3cd` |

All 62 declared manifest WAV entries across the three manifests were independently
re-hashed from bytes: **0 mismatches**. The three reviewed identities are also
re-hashed inside `jam.RhythmSilenceCoverage.registryHashesMatchTheCommittedAudio`.

## Executed vs not

Executed here: source edits, the 15-suite enabled jam-core build/ctest, the
new 8-test coverage suite, the six pinned-plugin CLI runs, the per-fixture and
aggregate non-coverage comparison, the all-62-inputs re-hash, and the aggregator
compatibility re-derivation.

**Not** done here (deliberately, to avoid overlap): diagnosing the acquisition
gate in an isolated tool copy; probing the actual audio processor on the RT path;
changing any shared CMake, corpus, generator, raw result, tracker, vendor,
application or shared path; the existing robustness aggregator; the ledger /
`HANDOFF.md` / `DEVPLAN.md`; any tracker selection or ADR.

## Limitations

- The registry hash is the declared manifest hash. It is verified here by a
  separate re-hash, but the scorer itself never opens the WAV; an unverified
  manifest could still declare a hash that does not match its bytes.
- The identity check is corpus bookkeeping. It is not a copyright, licensing or
  authenticity conclusion, and it does not establish physical realism of any
  fixture.
- `NotAssessedStructuralNoise` is triggered by the `derived`+`noise` provenance
  tags. It is a structural label, not a per-clip acoustic measurement; a clip
  with those tags is excluded from the measured worst regardless of its actual
  PCM.
- The derived clips are 5.0 s windows and are in no core denominator; their
  acquisition rows are per-clip only.
- `cpuSeconds` is wall clock and not comparable across runs.

## Handoff

Authoritative evidence: this report, `docs/research/silence-coverage/`
(`comparison.{json,md}` and the six raw CLI runs), the new
`tests/jam/RhythmSilenceCoverageTests.cpp`, and the scorer changes in
`tools/rhythm-eval/{Metrics.h,Metrics.cpp,Manifest.cpp}`. Reproduce with the
commands above; see `task-notes/EVAL-007.md` for the round summary.
