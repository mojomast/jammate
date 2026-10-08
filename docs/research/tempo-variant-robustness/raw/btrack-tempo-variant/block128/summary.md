# Rhythm evaluation summary

- backend: `btrack-tempo-variant`
- corpus: `eval003-derived-perturbations`
- beat-match tolerance: 70 ms
- variants in this run: `uncompensated` (0.00 ms)
- fixtures per variant: 24 (0 core, 22 steady, 0 ramp)

## Variant `uncompensated` — uncompensated


**Per-fixture evidence.** `coverage` is the true-silence diagnostic's coverage (Measured / NoTrueSilence / CorpusDefect / NotAssessedStructuralNoise); `sil accel` is the silence tempo-increase diagnostic (blank when not measured); `causal lat` is the mean availability-minus-event delay of backend-reported beats. `p95 phase` is `n/a` when no predicted beat matched (a zero there is undefined, not perfect).

| fixture | core | coverage | pred | TP | P | R | F | acq bars | BPM err | h/d | trueSil F/s | sil accel bpm | unplayed F/s | off-grid F/s | recovery s | sync dev | ramp err | p95 phase ms | causal lat ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| clean_eighths__baseline |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.52 |
| clean_eighths__noise_snr20db |  | NotAssessedStructuralNoise | 9 | 6 | 0.667 | 0.600 | 0.632 | - | 0.012 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 14.39 | 12.36 |
| clean_eighths__noise_snr10db |  | NotAssessedStructuralNoise | 9 | 6 | 0.667 | 0.600 | 0.632 | - | 0.012 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 14.58 | 12.95 |
| clean_eighths__noise_snr0db |  | NotAssessedStructuralNoise | 9 | 4 | 0.444 | 0.400 | 0.421 | - | 0.118 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 26.02 | 13.23 |
| clean_eighths__level_-20db |  | Measured | 10 | 8 | 0.800 | 0.800 | 0.800 | - | 0.013 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.74 |
| clean_eighths__level_-40db |  | Measured | 9 | 8 | 0.889 | 0.800 | 0.842 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 21.16 | 12.58 |
| clean_eighths__level_-60db |  | Measured | 0 | 0 | 1.000 | 0.000 | 0.000 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | n/a | 0.00 |
| clean_eighths__clip_0.5 |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.73 |
| clean_eighths__clip_0.25 |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 14.75 | 12.62 |
| clean_eighths__clip_0.125 |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 14.75 | 12.62 |
| clean_eighths__offset_40ms |  | Measured | 10 | 9 | 0.900 | 0.900 | 0.900 | 1.50 | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 62.43 | 12.72 |
| clean_eighths__offset_80ms |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 13.12 | 12.83 |
| clean_eighths__lead_silence_0.5s |  | Measured | 10 | 9 | 0.900 | 0.900 | 0.900 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 43.79 | 13.00 |
| clean_eighths__trail_silence_0.5s |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.52 |
| clean_eighths__silence_gap_1s |  | Measured | 7 | 5 | 0.714 | 0.500 | 0.588 | - | 0.344 | - | 0.000 | +0.00 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.78 |
| clean_eighths__drop_every2 |  | Measured | 9 | 4 | 0.444 | 0.400 | 0.421 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 66.88 | 12.48 |
| clean_eighths__drop_every4 |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.000 | - | 0.000 | - | 8.333 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.57 |
| clean_eighths__syncop_burst1 |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.52 |
| clean_eighths__syncop_burst4 |  | Measured | 9 | 7 | 0.778 | 0.700 | 0.737 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.52 |
| clean_eighths__tempo_step_1.25 |  | Measured | 8 | 2 | 0.250 | 0.200 | 0.222 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.53 |
| clean_eighths__tempo_step_0.85 |  | Measured | 10 | 3 | 0.300 | 0.300 | 0.300 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 48.32 | 12.78 |
| syncopated_funk__baseline |  | Measured | 9 | 7 | 0.778 | 0.778 | 0.778 | - | 0.003 | - | 0.000 | - | 8.333 | 0.000 | 0.000 | 0.0000 | 0.0000 | 41.43 | 12.73 |
| syncopated_funk__noise_snr10db |  | NotAssessedStructuralNoise | 8 | 4 | 0.500 | 0.444 | 0.471 | - | 0.029 | - | 0.000 | - | 8.333 | 0.000 | 0.000 | 0.0000 | 0.0000 | 9.97 | 12.67 |
| syncopated_funk__noise_snr0db |  | NotAssessedStructuralNoise | 8 | 4 | 0.500 | 0.444 | 0.471 | - | 0.029 | - | 0.000 | - | 8.333 | 0.000 | 0.000 | 0.0000 | 0.0000 | 9.97 | 12.67 |

## Aggregates

### `uncompensated`

- F-measure mean 0.6253 (precision 0.6980, recall 0.5986) over 24 fixtures with a ground-truth grid; phase measured on 23 fixture(s)
- BPM relative error (steady): mean 0.0323, median 0.0117; core worst 0.0000 over 0/0 core fixtures locked
- half/double-time error rate: 0.0000 (0/21), core 0.0000 (0/0)
- acquisition within 2 bars (core): 0/0 = 0.0000
- events in TRUE silence (diagnostic, not a gate): worst 0.0000/s over 24 fixtures; measured-coverage worst 0.0000/s over 19 fixture(s); coverage: 19 measured, 0 no-silence, 0 corpus-defect, 5 structural-noise
- silence tempo-increase diagnostic: max +0.0000 BPM over 1 evaluated fixture(s); insufficient evidence on 23 fixture(s)
- events in UNPLAYED-BEAT windows (separate metric): worst 8.3333/s, off-grid worst 0.0000/s
- ramp local-tempo relative error (raw, not a gate): mean 0.0000, worst 0.0000 over 0 measured ramp fixture(s)
- syncopation max deviation 0.0000, max step 0.0000 (raw, not a gate; 0 fixture(s) measured)
- backend beat-timestamp mapping: 206 reported, 0 at block start, 0 non-causal fallbacks, 0 rate-mismatch blocks; causal availability delay mean 12.68 ms, worst 14.17 ms
- CPU 0.9840 s total, 35625 C++ new/delete allocations total

## Latency compensation effect

No compensation was requested (`--compensate-latency` absent or 0). The table below is therefore empty by design; re-run with a non-zero value to measure the effect.

**The orchestrator, not the harness, decides whether to compensate.** Compensation is a claim about the backend, and it can be as wrong as no compensation: subtracting a latency the backend does not actually have is just a bias in the other direction, and for a non-causal or tempo-adaptive backend the delay may not even be constant. This table exists so the ADR can see the size of the decision, not so the harness can make it.

## SPEC 19 gates

### `uncompensated`

| gate | result | reason |
|---|---|---|
| acquire useful lock within 2 bars for >= 95% of core fixtures | NOT-MEASURED | 0/0 core fixtures acquired within 2 bars (fraction 0.0000) |
| locked BPM relative error <= 2% on steady-tempo core fixtures | NOT-MEASURED | worst core BPM relative error 0.0000; 0/0 core fixtures locked |
| half/double-time errors < 5% on core fixtures | NOT-MEASURED | core half/double-time errors 0/0 (rate 0.0000) |
| no tempo jump from one isolated syncopated event | NOT-MEASURED | raw diagnostic: largest first difference of reported BPM on `syncopated_funk` is 0.0000 of nominal over 0 measured fixture(s). The fixture contains many syncopated events, so it cannot isolate ONE; no numeric threshold is claimed. |
| silence does not create false acceleration | NOT-MEASURED | raw diagnostic: 24/24 fixtures declare true silence (19 measured, 0 corpus-defect, 5 structural-noise); worst 0.0000 events/s, worst measured-coverage 0.0000/s; silence tempo-increase max +0.0000 BPM over 1 evaluated fixture(s), 23 insufficient. SPEC forbids false ACCELERATION, not beat events during a short intentional holdover; the offline harness cannot attribute a tempo change to the silence rather than to legitimate tempo follow, so no gate is claimed. |
| explicit resync establishes new phase within the requested boundary | NOT-MEASURED | the offline harness has no resync command path; the Musical Clock is the only place this can be tested |
| Follow handles controlled gradual tempo ramps without abrupt audible discontinuities | NOT-MEASURED | raw diagnostic: mean local-tempo relative error 0.0000, worst 0.0000 over 0 measured ramp fixture(s). The SPEC target is 'without abrupt audible discontinuities', which requires the Musical Clock and audition; a 2 % backend error threshold would be fabricated (and is not the SPEC rule). |
| Loose Follow is measurably less reactive than Follow | NOT-MEASURED | requires the Musical Clock and a real controller; no clock runs in this offline harness |
| stop/start recovery succeeds without restarting the audio device | NOT-INFORMATIVE | offline proxy: worst post-stop lock recovery 0.0000 s; audio-device restart is not observable offline |

## Fixtures not informative for the true-silence false-beat metric

- `clean_eighths__noise_snr20db` (`uncompensated`): NOT ASSESSED (STRUCTURAL NOISE) — the declared true-silence spans are inherited by a derived noise transform whose added floor fills what used to be quiet; they are not a re-measured acoustic stop. The raw count is reported but the rate is not a measured pass or fail.
- `clean_eighths__noise_snr10db` (`uncompensated`): NOT ASSESSED (STRUCTURAL NOISE) — the declared true-silence spans are inherited by a derived noise transform whose added floor fills what used to be quiet; they are not a re-measured acoustic stop. The raw count is reported but the rate is not a measured pass or fail.
- `clean_eighths__noise_snr0db` (`uncompensated`): NOT ASSESSED (STRUCTURAL NOISE) — the declared true-silence spans are inherited by a derived noise transform whose added floor fills what used to be quiet; they are not a re-measured acoustic stop. The raw count is reported but the rate is not a measured pass or fail.
- `syncopated_funk__noise_snr10db` (`uncompensated`): NOT ASSESSED (STRUCTURAL NOISE) — the declared true-silence spans are inherited by a derived noise transform whose added floor fills what used to be quiet; they are not a re-measured acoustic stop. The raw count is reported but the rate is not a measured pass or fail.
- `syncopated_funk__noise_snr0db` (`uncompensated`): NOT ASSESSED (STRUCTURAL NOISE) — the declared true-silence spans are inherited by a derived noise transform whose added floor fills what used to be quiet; they are not a re-measured acoustic stop. The raw count is reported but the rate is not a measured pass or fail.

Criterion (stated, not hidden): a fixture is not informative when (a) it has no true-silence spans at all (`NoTrueSilence`), (b) it is a derived noise perturbation whose inherited spans are structural (`NotAssessedStructuralNoise`), or (c) its declared WAV sha256 is in the reviewed fast-decay defect registry (`CorpusDefect`, EVAL-006 `docs/research/SUSTAIN-REPAIR.md`). Classification is by WAV identity, not fixture name: the repaired `sustained_chords` render with hash `23b8cf21…` measures its own independently derived spans, while the original `e4b9297f…` bytes remain a defect even if renamed. The hash is the declared manifest hash, not an authentication of the PCM. No numeric silence-occupancy threshold is used; this replaces EVAL-002R's '>= 50 % silence' censoring rule, which also censored genuine sparse playing (`sparse_single_notes` sits at 48.55 %).
