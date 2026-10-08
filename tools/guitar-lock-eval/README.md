# guitar-lock-eval (EVAL-GUITAR-009)

A Python-standard-library evaluator for SPEC 19's **useful lock**: a lock is
useful only when the reported tempo is correct **and** the phase is usable,
acquired within **two bars**. A `Locked` label alone is not enough.

This tool is a diagnostic/evaluation instrument. It:
- distinguishes **real / synthetic / derived** audio and fails closed when a
  recording is mislabelled or its evidence is missing, empty, corrupt, NaN or
  unrepresentative;
- validates the actual bytes (sha256, sample rate, channels, frames, duration)
  before any measurement;
- keeps **event / horizon / receipt** timing distinct;
- reuses the existing `tracker-diagnostics` binary and the existing
  `librhythm-eval-{btrack,aubio}.so` plugins (loaded through the existing
  `jam_rhythm_create`/`jam_rhythm_destroy` convention) to generate traces from
  actual audio — no canned results;
- reports half-time, double-time, holdover and false locks as **separate**
  labels;
- never edits `src/`, CMake, the ledger or any existing evidence.

## Commands

```bash
# 1. import one recording (private paths are allowed and never committed)
tools/guitar-lock-eval/guitar-lock-eval import \
    --manifest work/import.json --id my_take --audio /private/take.wav \
    --classification real --annotation take.annotations.json \
    --license "CC-BY-4.0" --ownership "me" --tags core

# 2. validate the manifest against the actual bytes (fail closed)
tools/guitar-lock-eval/guitar-lock-eval validate --manifest work/import.json --audio-root .

# 3. one-command compare (BTrack / aubio / derived candidate)
tools/guitar-lock-eval/guitar-lock-eval evaluate \
    --manifest work/import.json --audio-root . --backends btrack,aubio \
    --tracker-diagnostics /home/mojo/projects/build-EVAL-GUITAR-009/diag/tracker-diagnostics \
    --traces-out work/traces --summary-md work/summary.md \
    --json-out work/results.json --per-fixture-csv work/per-fixture.csv \
    --provenance-out work/provenance.json

# or regenerate the whole synthetic diagnostic baseline + evidence
tools/guitar-lock-eval/run-baseline.sh

# tests (no backend needed; synthetic traces demonstrate the scorer only)
tools/guitar-lock-eval/run-tests.sh
```

## The protocol is frozen before measurement

`protocol/useful-lock-protocol.json` fixes the criteria (2 % BPM band, 70 ms
beat tolerance, one-bar lock run, two-bar acquisition window, 95 % gate,
half/double band, fail-closed rules) and is committed before any trace is
generated. `evaluate` hashes it before and after the run and fails if it changed.

## Annotation file

Real recordings are imported with an annotation file:

```json
{
  "schema": "guitar-lock-eval/annotation/1.0",
  "source": "human",
  "license": "CC-BY-4.0",
  "tempo_profile": "constant",
  "nominal_bpm": 120.0,
  "meter": {"numerator": 4, "denominator": 4, "beats_per_bar": 4},
  "beats": [0.5, 1.0, 1.5],
  "onsets": [0.5, 1.0, 1.5],
  "true_silence_spans": [[8.0, 10.0]]
}
```

A `real` classification requires a `human`/`manual` annotation source and
explicit ownership/licence. Real recordings are **private by default**: the
manifest stores a path, sha256, rate and duration; the WAV is never copied into
the repository.

## Gate vs diagnostic

The >=95 % gate is computed only over representative, human-annotated, licensed,
steady 4/4 real recordings. Synthetic and derived recordings — including the
committed `testdata/rhythm` corpus — are reported as a clearly labelled
diagnostic and can never pass the gate. An empty or incomplete gate population
fails closed.

## Derived diagnostic candidate

`evaluate` derives a causal **beat-interval BPM** candidate from the BTrack
trace (60 / median of the last ≤4 inter-beat intervals) entirely inside this new
tool. It is labelled `derived`, scored beside the real backends, and is never a
production promotion. Its trace records the sha256 of the actual producer
source; all-zero placeholder hashes are rejected.

## Trust boundary

Classification, ownership, licence and provenance are **declared** fields. A
`real` recording whose declared provenance/tags contain a generated/synthetic
marker is rejected (`provenance_contradicts_real`) and excluded from the gate,
but this is a **declaration-consistency** rule: no offline byte check can
mathematically attest that the audio is a human performance. The evaluator
verifies what the bytes can establish — sha256, sample rate, channels, sample
width, frame count, duration — and trusts the human-origin declaration.

Trace identity is bound to the slot it is used in: a trace must declare the
requested backend and a `real` backend kind to count for a real gate backend, and
the derived candidate must declare `parent_backend == "btrack"`. This is enforced
in the CLI and independently inside `evaluate_gate`.

## Corrections

Review of handoff `d64a598` returned FIX. The additive correction (protocol
`protocol/correction-01.json`, evidence `docs/research/guitar-lock-eval/correction-01/`,
tests `tests/test_review_fixes.py`) fixes provenance-marker consistency (F1),
backend/kind binding (F2), the candidate producer hash (F3) and the
criteria-driven phase window (F4). The original frozen protocol and evidence are
never rewritten; `run-correction.sh` asserts they are unchanged.

Host-absolute defaults can be overridden with `GLE_TRACKER_DIAGNOSTICS`,
`GLE_BTRACK_LIB`, `GLE_AUBIO_LIB`, `GLE_PROTOCOL`, `GLE_WORKDIR`.

## Layout

```
tools/guitar-lock-eval/
  guitar-lock-eval          executable shim
  glock/                    the package (wav, integrity, manifest, trace,
                            scoring, gate, adapters, report, cli)
  protocol/                 frozen useful-lock criteria + additive correction-01.json
  tests/                    synthetic-only unit tests (scorer demonstrably)
  run-baseline.sh           one-command baseline + evidence regeneration
  run-correction.sh         additive corrected baseline (originals preserved)
  run-tests.sh              test runner
docs/research/guitar-lock-eval/   protocol, evidence, baseline results
docs/research/guitar-lock-eval/correction-01/   corrected (additive) evidence
```
