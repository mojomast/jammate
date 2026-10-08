# Rhythm evaluation summary

- backend: `btrack`
- corpus: `eval006-sustain-repair`
- beat-match tolerance: 70 ms
- variants in this run: `uncompensated` (0.00 ms)
- fixtures per variant: 19 (11 core, 17 steady, 2 ramp)

## Variant `uncompensated` — uncompensated


**Per-fixture evidence.** `coverage` is the true-silence diagnostic's coverage (Measured / NoTrueSilence / CorpusDefect); `sil accel` is the silence tempo-increase diagnostic (blank when not measured); `causal lat` is the mean availability-minus-event delay of backend-reported beats. `p95 phase` is `n/a` when no predicted beat matched (a zero there is undefined, not perfect).

| fixture | core | coverage | pred | TP | P | R | F | acq bars | BPM err | h/d | trueSil F/s | sil accel bpm | unplayed F/s | off-grid F/s | recovery s | sync dev | ramp err | p95 phase ms | causal lat ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| accelerando |  | Measured | 25 | 9 | 0.360 | 0.375 | 0.367 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0758 | 46.80 | 13.00 |
| arpeggio | yes | Measured | 22 | 19 | 0.864 | 0.950 | 0.905 | - | 0.021 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 14.08 | 13.05 |
| blues_shuffle | yes | Measured | 22 | 16 | 0.727 | 0.800 | 0.762 | 1.99 | 0.003 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 18.98 | 12.97 |
| clean_eighths | yes | Measured | 21 | 17 | 0.810 | 0.850 | 0.829 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 25.64 | 12.80 |
| clean_sixteenths | yes | Measured | 22 | 19 | 0.864 | 0.950 | 0.905 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 21.16 | 12.72 |
| compound_6_8 |  | Measured | 20 | 6 | 0.300 | 0.500 | 0.375 | - | 0.455 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 13.81 | 12.91 |
| line_input_clipping |  | Measured | 22 | 17 | 0.773 | 0.850 | 0.810 | 2.99 | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 16.02 | 13.02 |
| line_input_low_level |  | Measured | 18 | 17 | 0.944 | 0.850 | 0.895 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 8.82 | 12.79 |
| missing_downbeats | yes | Measured | 21 | 18 | 0.857 | 0.900 | 0.878 | - | 0.021 | - | 0.000 | - | 11.111 | 0.000 | 0.000 | 0.0000 | 0.0000 | 20.02 | 12.95 |
| noisy_microphone |  | Measured | 22 | 16 | 0.727 | 0.800 | 0.762 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 9.01 | 12.77 |
| palm_mute_metal | yes | Measured | 19 | 18 | 0.947 | 0.900 | 0.923 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 8.82 | 12.98 |
| power_chords_distorted | yes | Measured | 21 | 0 | 0.000 | 0.000 | 0.000 | - | 0.023 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | n/a | 12.74 |
| ritardando |  | Measured | 28 | 15 | 0.536 | 0.625 | 0.577 | - | 0.000 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0920 | 65.51 | 12.85 |
| sparse_single_notes | yes | Measured | 22 | 15 | 0.682 | 0.750 | 0.714 | 1.26 | 0.018 | - | 1.366 | +0.00 | 11.111 | 0.000 | 0.000 | 0.0000 | 0.0000 | 38.16 | 12.88 |
| stop_start | yes | Measured | 26 | 22 | 0.846 | 0.688 | 0.759 | 0.26 | 0.021 | - | 0.000 | +0.00 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 16.82 | 12.83 |
| sustained_chords | yes | CorpusDefect | 17 | 12 | 0.706 | 0.750 | 0.727 | 1.51 | 0.003 | - | 0.699 | - | 6.944 | 0.000 | 0.000 | 0.0000 | 0.0000 | 66.06 | 12.67 |
| syncopated_funk | yes | Measured | 21 | 18 | 0.857 | 0.900 | 0.878 | 1.00 | 0.018 | - | 0.000 | - | 13.333 | 0.000 | 0.000 | 0.0182 | 0.0000 | 39.77 | 12.77 |
| tapping_muting_only |  | CorpusDefect | 18 | 17 | 0.944 | 0.850 | 0.895 | 1.00 | 0.018 | - | 0.502 | +0.00 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 6.63 | 12.68 |
| waltz_3_4 |  | Measured | 19 | 15 | 0.789 | 0.833 | 0.811 | 1.32 | 0.014 | - | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 10.94 | 12.90 |

## Aggregates

### `uncompensated`

- F-measure mean 0.7248 (precision 0.7123, recall 0.7432) over 19 fixtures with a ground-truth grid; phase measured on 18 fixture(s)
- BPM relative error (steady): mean 0.0432, median 0.0212; core worst 0.0234 over 11/11 core fixtures locked
- half/double-time error rate: 0.0000 (0/17), core 0.0000 (0/11)
- acquisition within 2 bars (core): 5/11 = 0.4545
- events in TRUE silence (diagnostic, not a gate): worst 1.3663/s over 19 fixtures; measured-coverage worst 1.3663/s over 17 fixture(s); coverage: 17 measured, 0 no-silence, 2 corpus-defect
- silence tempo-increase diagnostic: max +0.0000 BPM over 3 evaluated fixture(s); insufficient evidence on 16 fixture(s)
- events in UNPLAYED-BEAT windows (separate metric): worst 13.3333/s, off-grid worst 0.0000/s
- ramp local-tempo relative error (raw, not a gate): mean 0.0839, worst 0.3432 over 2 measured ramp fixture(s)
- syncopation max deviation 0.0182, max step 0.0000 (raw, not a gate; 1 fixture(s) measured)
- backend beat-timestamp mapping: 406 reported, 0 at block start, 0 non-causal fallbacks, 0 rate-mismatch blocks; causal availability delay mean 12.86 ms, worst 14.25 ms
- CPU 1.6216 s total, 38761 C++ new/delete allocations total

## Latency compensation effect

No compensation was requested (`--compensate-latency` absent or 0). The table below is therefore empty by design; re-run with a non-zero value to measure the effect.

**The orchestrator, not the harness, decides whether to compensate.** Compensation is a claim about the backend, and it can be as wrong as no compensation: subtracting a latency the backend does not actually have is just a bias in the other direction, and for a non-causal or tempo-adaptive backend the delay may not even be constant. This table exists so the ADR can see the size of the decision, not so the harness can make it.

## SPEC 19 gates

### `uncompensated`

| gate | result | reason |
|---|---|---|
| acquire useful lock within 2 bars for >= 95% of core fixtures | FAIL | 5/11 core fixtures acquired within 2 bars (fraction 0.4545) |
| locked BPM relative error <= 2% on steady-tempo core fixtures | FAIL | worst core BPM relative error 0.0234; 11/11 core fixtures locked |
| half/double-time errors < 5% on core fixtures | PASS | core half/double-time errors 0/11 (rate 0.0000) |
| no tempo jump from one isolated syncopated event | NOT-MEASURED | raw diagnostic: largest first difference of reported BPM on `syncopated_funk` is 0.0000 of nominal over 1 measured fixture(s). The fixture contains many syncopated events, so it cannot isolate ONE; no numeric threshold is claimed. |
| silence does not create false acceleration | NOT-MEASURED | raw diagnostic: 17/19 fixtures measured for true silence have worst 1.3663 events/s, worst measured-coverage 1.3663/s; silence tempo-increase max +0.0000 BPM over 3 evaluated fixture(s), 16 insufficient. SPEC forbids false ACCELERATION, not beat events during a short intentional holdover; the offline harness cannot attribute a tempo change to the silence rather than to legitimate tempo follow, so no gate is claimed. |
| explicit resync establishes new phase within the requested boundary | NOT-MEASURED | the offline harness has no resync command path; the Musical Clock is the only place this can be tested |
| Follow handles controlled gradual tempo ramps without abrupt audible discontinuities | NOT-MEASURED | raw diagnostic: mean local-tempo relative error 0.0839, worst 0.3432 over 2 measured ramp fixture(s). The SPEC target is 'without abrupt audible discontinuities', which requires the Musical Clock and audition; a 2 % backend error threshold would be fabricated (and is not the SPEC rule). |
| Loose Follow is measurably less reactive than Follow | NOT-MEASURED | requires the Musical Clock and a real controller; no clock runs in this offline harness |
| stop/start recovery succeeds without restarting the audio device | NOT-INFORMATIVE | offline proxy: worst post-stop lock recovery 0.0000 s; audio-device restart is not observable offline |

## Fixtures not informative for the true-silence false-beat metric

- `sustained_chords` (`uncompensated`): CORPUS DEFECT — 1.43 s (13%) of declared true silence is an artifact of the fixture's two-stage fast decay, not a performance. Its rate must not be read as a pass or a fail.
- `tapping_muting_only` (`uncompensated`): CORPUS DEFECT — 9.96 s (83%) of declared true silence is an artifact of the fixture's two-stage fast decay, not a performance. Its rate must not be read as a pass or a fail.

Criterion (stated, not hidden): a fixture is not informative when it has no true silence at all (NOT MEASURED), or when its declared true silence is a known corpus synthesis defect (`sustained_chords`, `tapping_muting_only`, whose two-stage fast decay ends each note in ~0.2 s). This replaces EVAL-002R's numeric '>= 50 % silence' censoring rule, which also censored genuine sparse playing (`sparse_single_notes` sits at 48.55 %).
