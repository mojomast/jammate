# EVAL-GUITAR-009 — representative-guitar useful-lock evaluator

## Goal

Build an executable, end-to-end evaluator for SPEC 19's "acquire useful lock
within 2 bars for >= 95 % of core fixtures", where a **useful lock** means
correct BPM **and** usable phase within the two-bar window, not merely a
`Locked` label. The evaluator imports guitar recordings with annotation and
provenance, validates the actual bytes, distinguishes real / synthetic / derived
input, keeps event / horizon / receipt timing separate, scores holdover,
half/double and false locks as distinct labels, and fails closed on missing,
empty, corrupt, NaN or unrepresentative evidence. It reuses the existing WAV /
backend replay / clock infrastructure rather than inventing results.

**G3 remains OPEN. No tracker is selected, no ADR, no gate tuned, no production
code changed.** No representative real guitar recording was available, so the
>=95 % gate cannot pass here and is reported as a fail-closed FAIL; the
committed baseline is a labelled diagnostic over the synthetic corpus.

## Base, branch, scope

- Base `984ad1d32c44157ad2d4128c47b86dc932535918`, worktree
  `/home/mojo/projects/worktrees/EVAL-GUITAR-009`, branch
  `wp/EVAL-GUITAR-009`.
- Owned and changed: **new** `tools/guitar-lock-eval/**`, **new**
  `docs/research/guitar-lock-eval/**`, **new** `task-notes/EVAL-GUITAR-009.md`.
- **Not touched:** `src/`, `third_party/`, any CMake/CI file, the execution
  ledger, shared docs, `testdata/` (read-only), and every existing tool/evidence
  tree. No new third-party dependency; Python standard library only.
- The replay adapter is the pre-existing `tools/tracker-diagnostics` binary
  (built, unmodified, into `/home/mojo/projects/build-EVAL-GUITAR-009/diag`),
  loading the pre-existing `librhythm-eval-{btrack,aubio}.so` plugins through
  the existing `jam_rhythm_create`/`jam_rhythm_destroy` convention. This is a
  structural reuse, **not** a copyright conclusion.

## Files added

```
tools/guitar-lock-eval/guitar-lock-eval            executable shim
tools/guitar-lock-eval/glock/{__init__,integrity,wav,manifest,trace,scoring,gate,adapters,report,cli}.py
tools/guitar-lock-eval/protocol/useful-lock-protocol.json   frozen criteria
tools/guitar-lock-eval/tests/{synth,test_scoring,test_trace_manifest,test_gate,test_cli}.py
tools/guitar-lock-eval/run-baseline.sh             one-command baseline + evidence
tools/guitar-lock-eval/run-tests.sh                test runner
tools/guitar-lock-eval/README.md
docs/research/guitar-lock-eval/README.md           protocol + baseline explanation
docs/research/guitar-lock-eval/{baseline-import.json,results.json,per-fixture.csv,
    summary.md,provenance.json,traces/**}
task-notes/EVAL-GUITAR-009.md
```

## Protocol frozen before measurement

`tools/guitar-lock-eval/protocol/useful-lock-protocol.json`
(sha256 `c92bbdf37ccfc85b4ebb63fa39cd6dfc4137dba4535a5a874f5fd3c6a28c5305`) fixes:

- BPM agreement `0.02` (SPEC 19), beat tolerance `0.07 s` (EVAL-002);
- lock run = one bar of the annotated meter; acquisition window = two bars;
- useful lock = acquired within the window AND `tempoCorrect` AND
  `phaseUsable` (mean |phase| <= 70 ms, p95 <= 150 ms);
- half/double band +/-10 %; holdover, half, double, false and no-lock separate;
- gate fraction `0.95` over representative, human-annotated, licensed, steady
  4/4 real recordings only; fail-closed rules for empty/missing/NaN/mismatched.

`evaluate` hashes the protocol before and after the run and fails if it changed.

## Method

- **Truth** comes from an annotation (real: human; baseline: the committed
  synthetic corpus manifest). The annotation carries beats, onsets, meter and
  tempo profile; `beats` must be strictly increasing and finite.
- **Identity** is validated against the actual bytes: sha256, sample rate,
  channels, sample width, frames and duration must match the declared manifest
  fields. Failures are hard.
- **Traces** keep three clocks: `event_seconds` (device time of the audio),
  `horizon_seconds` (the block end at which the backend returned the evidence)
  and a `receipt` record (`wall_utc` + block-resolution `audio_seconds`); a
  measured receipt must be `>= max(horizon)`, and an unmeasured receipt must be
  explicit null. Missing timing is null + a measured flag, never a zero.
- **Scoring** restates the unmodified EVAL-002/TRACK-004 lock rule (forward-
  advancing beats within tolerance, each with a phase-valid in-band tempo) with
  the run length set to one annotated bar; acquisition is the event time of the
  run's first beat. The useful-lock verdict adds BPM correctness and phase
  usability. False locks split into `phase-ok/tempo-wrong` and
  `tempo-ok/phase-wrong`; half/double and holdover are separate.
- **Diagnostic candidate** derives a causal beat-interval BPM (60 / median of
  the last <= 4 inter-beat intervals) from the BTrack trace, entirely inside
  this new tool. It is labelled `derived`, never a production promotion.

## Replay adapter (actual generated traces)

`build_rhythm_manifest` writes a rhythm-eval-compatible staging manifest and
**symlinks** the audio (private WAVs are never copied or committed), then runs:

```
/home/mojo/projects/build-EVAL-GUITAR-009/diag/tracker-diagnostics \
  --corpus <staging/corpus> --out <runs/backend> --backend <btrack|aubio> \
  --backend-lib <.../librhythm-eval-<backend>.so> --block 128 \
  --trace-files all --state-max-rows 1000000
```

Each run's beats/state CSVs are reduced to a bounded trace JSON (all beats;
tempo samples compacted by changes + nearest-to-beat + stride 16 + endpoints)
with an explicit receipt. The raw per-beat CSV is copied beside it.

## Tests executed

```
tools/guitar-lock-eval/run-tests.sh
# Ran 37 tests ... OK   (Python stdlib unittest; synthetic traces only)

/home/mojo/projects/build-EVAL-GUITAR-009/diag/TraceReplayTests
# TraceReplayTests: 63 checks, 0 failures   (the reused adapter's own suite)
```

The Python suite pins: a perfect lock is useful; half-time and double-time are
separate and not useful; a phase-wrong/tempo-ok trace is a false lock; a lock
after two bars is not useful; no-beat and ramp (no nominal BPM) cases; holdover;
malicious/escaping paths; NaN/duplicate-key JSON rejection; real-without-
annotation; synthetic-mislabelled-real; derived-without-parent; audio sha/size
mismatch; trace schema / event-after-horizon / non-monotonic / receipt-before-
horizon / unmeasured-receipt-non-null / derived-without-parent; empty gate;
missing trace; 95 % vs 90 % boundary; and the CLI exit codes (0 diagnostic,
1 gate-fail, 2 integrity).

## Evidence runs (diagnostic baseline, synthetic)

```
tools/guitar-lock-eval/run-baseline.sh
# artifact: docs/research/guitar-lock-eval  (~0.9 MiB)
```

- Corpus: `testdata/rhythm`, 19/19 WAV identities re-validated (sha256, rate,
  channels, frames, duration).
- Adapter binary sha256 `e3fd542f209b51737c5109edc039ddf485f50cdc0468a4af99e037085294923d`;
  BTrack lib `41e6476e...`, aubio lib `61336277...` (identical to the TRACK-004
  evidence plugin hashes).
- Traces were generated from the actual audio, then the whole comparison was
  re-run from the committed traces alone and reproduced exactly.

| backend | population | n | useful lock | fraction | half | double | false lock | no lock |
|---|---|---|---|---|---|---|---|---|
| btrack | diagnostic | 19 | 5 | 26.3% | 0 | 0 | 9 | 1 |
| aubio | diagnostic | 19 | 12 | 63.2% | 0 | 0 | 4 | 3 |
| diagnostic-beat-interval-bpm | diagnostic | 19 | 12 | 63.2% | 1 | 0 | 5 | 0 |
| btrack / aubio | **gate** | 0 | 0 | n/a | - | - | - | **FAIL (empty real population)** |

Artifact sha256: `baseline-import.json` `a97a342d...`, `results.json`
`0a6f4bb7...`, `provenance.json` `3832d140...`, `per-fixture.csv` `fa791faa...`,
`summary.md` `1e4ef3bb...`.

## Findings

1. **The gate cannot pass without real evidence.** No representative real
   recording exists, so the gate population is empty and the run fails closed —
   exactly the required behaviour. An empty template cannot pass 95 %.
2. **Cross-check against the unmodified C++ evidence holds.** BTrack's
   `clean_eighths` reports `123.046875` BPM (2.34 % high) and is a **false lock**
   (phase correct, tempo wrong); `blues_shuffle` acquires at 1.749 bars and is
   useful. These match TRACK-004's `123.046875` / `AcquiredWithin2Bars` rows.
3. **The derived candidate is scored, not promoted.** The beat-interval BPM
   candidate recovers useful lock on `clean_sixteenths`/`palm_mute_metal` where
   aubio did not, loses it on fixtures aubio held, and labels
   `sustained_chords` a half-time lock. This is a diagnostic comparison from
   actual traces; no `src/` file changed.
4. **Timing is distinct and enforced.** Event <= horizon and receipt >= horizon
   are validated; a receipt-before-horizon or an unmeasured receipt carrying a
   value is a hard failure.

## Limitations / open blockers

1. **No representative real guitar recordings were available.** The evaluator
   supports importing private files by path + sha256 without committing WAVs,
   but the G3 useful-lock gate stays **OPEN** and fails closed until such a set
   is imported with human annotations.
2. The committed baseline is **synthetic** (the committed `testdata/rhythm`
   corpus) and is labelled diagnostic; its 19-fixture population and useful-lock
   definition are not the EVAL-002 core-gate numbers.
3. The derived candidate is a trace-level diagnostic; no tracker selection, ADR
   or production wiring follows from it.
4. Tempo samples in the committed traces are compacted (documented in each
   trace); the full per-block state remains in the external build root.
5. `wave`-based import supports mono 16/24/32-bit PCM; other formats are
   rejected explicitly and would need an external conversion step.

## Handoff

- Branch head at handoff: the tip of `wp/EVAL-GUITAR-009` (this note is in it).
- Protocol sha256 `c92bbdf3...`; base `984ad1d3...`.
- Evidence: `docs/research/guitar-lock-eval/`; tool: `tools/guitar-lock-eval/`.
- Reproduce: `tools/guitar-lock-eval/run-tests.sh` then
  `tools/guitar-lock-eval/run-baseline.sh`.
- G3 OPEN; no tracker selected; no gate tuned; no production change.

## Correction round — review of d64a598

Independent review returned FIX with four findings. The repair is **additive**:
the frozen protocol `protocol/useful-lock-protocol.json` and the original
evidence under `docs/research/guitar-lock-eval/` are preserved byte-for-byte.
A preregistered correction protocol
`tools/guitar-lock-eval/protocol/correction-01.json` was committed before the
corrected run, and corrected evidence lives in the new
`docs/research/guitar-lock-eval/correction-01/` (regenerated by
`tools/guitar-lock-eval/run-correction.sh`, which asserts the originals are
unchanged).

| id | fix |
|---|---|
| F1 | `validate_manifest` emits hard `provenance_contradicts_real` and `gate_eligibility` excludes a real recording whose declared provenance/tags contain a generated/synthetic marker. Explicit trust boundary: classification/ownership/licence/provenance are **declared**; the check is declaration consistency and cannot attest human origin from the bytes. |
| F2 | `validate_trace` binds `expected_backend`/`expected_kind`/`expected_parent`; the CLI requires a real kind for a real backend; `evaluate_gate` independently fails closed unless every gate score is exactly the requested backend with `backend_kind == "real"`. |
| F3 | the derived candidate records sha256 of its actual producer source (`b7d0954b...`); `validate_trace` rejects all-zero placeholder tool hashes. |
| F4 | the phase window is `criteria.acquisition_window_bars * meter.beats_per_bar`, not a hard-coded two bars (a no-op at the default 2.0). |

Adversarial tests reproduce each reviewer case in
`tools/guitar-lock-eval/tests/test_review_fixes.py`. The corrected synthetic
baseline reproduces the original diagnostic numbers (btrack 5/19, aubio 12/19,
candidate 12/19 with 1 half-time) and the real-guitar gate still FAILS closed
(empty population). Host-absolute defaults gained `GLE_TRACKER_DIAGNOSTICS`,
`GLE_BTRACK_LIB`, `GLE_AUBIO_LIB`, `GLE_PROTOCOL`, `GLE_WORKDIR` overrides.

Correction verification: `tools/guitar-lock-eval/run-tests.sh` — 48 tests
(37 original + 11 adversarial), 0 failures; `run-correction.sh` —
corrected traces carry a real producer hash, originals asserted unchanged,
gate FAIL (fail closed). See `docs/research/guitar-lock-eval/CORRECTION-01.md`.
