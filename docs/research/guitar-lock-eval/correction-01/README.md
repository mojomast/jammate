# Corrected evidence — correction-01 (EVAL-GUITAR-009)

Additive corrected measurement for the review of commit `d64a598`. The
preregistered correction protocol is
`../../../tools/guitar-lock-eval/protocol/correction-01.json`; it was committed
before this directory was generated. The **original** `docs/research/guitar-lock-eval`
evidence (`results.json`, `traces/**`, protocol) is preserved byte-for-byte —
`run-correction.sh` asserts this and aborts if any original changed.

## Corrections applied

- **F1** real classification is now rejected (hard `provenance_contradicts_real`)
  and excluded from gate eligibility when its declared provenance/tags contain a
  generated/synthetic marker. This is a **declaration-consistency** rule: the
  classification, ownership, licence and provenance are declared fields, and no
  offline check can mathematically attest that a recording is a human
  performance. Audio identity (sha256/rate/duration) is verified; human origin
  is trusted from the declaration.
- **F2** a trace must be bound to the requested backend and be a real backend
  kind for a real gate; the derived candidate must have
  `parent_backend == "btrack"`. The binding is enforced in the CLI **and**
  independently in `evaluate_gate`, so a direct call cannot bypass it.
- **F3** the derived candidate records sha256 of the actual producer source
  (`b7d0954b...`); all-zero placeholder tool hashes are rejected.
- **F4** the phase-usability window is `criteria.acquisition_window_bars *
  meter.beats_per_bar`, not a hard-coded two bars. At the frozen default (2.0)
  this is numerically identical, so the default diagnostic numbers are
  unchanged.

## Corrected baseline (diagnostic, synthetic, block 128)

| backend | population | n | useful lock | fraction | half | double | false lock | no lock |
|---|---|---|---|---|---|---|---|---|
| btrack | diagnostic | 19 | 5 | 26.3% | 0 | 0 | 9 | 1 |
| aubio | diagnostic | 19 | 12 | 63.2% | 0 | 0 | 4 | 3 |
| diagnostic-beat-interval-bpm (derived) | diagnostic | 19 | 12 | 63.2% | 1 | 0 | 5 | 0 |
| btrack / aubio | **gate** | 0 | 0 | n/a | - | - | - | **FAIL (fail closed)** |

The corrected numbers match the original diagnostic numbers; the differences are
the candidate producer hash and generation timestamps. The real-guitar G3 gate
still fails closed because no representative real recording exists. Raw
corrected traces and per-beat CSVs are preserved under `traces/`.
