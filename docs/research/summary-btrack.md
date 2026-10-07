# Rhythm evaluation summary

- backend: `btrack`
- corpus: `eval001-guitar-corpus`
- beat-match tolerance: 70 ms
- variants in this run: `uncompensated` (0.00 ms), `compensated-23.22ms` (23.22 ms), `compensated-11.61ms` (11.61 ms)
- fixtures per variant: 19 (11 core, 17 steady, 2 ramp)

## Variant `uncompensated` — uncompensated

| fixture | core | inform | pred | TP | P | R | F | acq bars | BPM err | h/d | trueSil F/s | unplayed F/s | unplayed off-grid F/s | recovery s | sync dev | ramp err | p95 phase ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| accelerando |  | yes | 25 | 9 | 0.360 | 0.375 | 0.367 | -1.00 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0755 | 56.45 |
| arpeggio | yes | yes | 22 | 19 | 0.864 | 0.950 | 0.905 | -1.00 | 0.021 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 20.67 |
| blues_shuffle | yes | yes | 22 | 16 | 0.727 | 0.800 | 0.762 | 1.75 | 0.003 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 28.67 |
| clean_eighths | yes | yes | 21 | 17 | 0.810 | 0.850 | 0.829 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 18.76 |
| clean_sixteenths | yes | yes | 22 | 19 | 0.864 | 0.950 | 0.905 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 30.95 |
| compound_6_8 |  | yes | 20 | 6 | 0.300 | 0.500 | 0.375 | -1.00 | 0.455 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 23.33 |
| line_input_clipping |  | yes | 22 | 17 | 0.773 | 0.850 | 0.810 | 2.75 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 19.14 |
| line_input_low_level |  | yes | 18 | 17 | 0.944 | 0.850 | 0.895 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 19.52 |
| missing_downbeats | yes | yes | 21 | 18 | 0.857 | 0.900 | 0.878 | -1.00 | 0.021 | - | 0.000 | 11.111 | 0.000 | 0.000 | 0.0000 | 0.0000 | 24.67 |
| noisy_microphone |  | yes | 22 | 16 | 0.727 | 0.800 | 0.762 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 19.14 |
| palm_mute_metal | yes | yes | 19 | 18 | 0.947 | 0.900 | 0.923 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 19.52 |
| power_chords_distorted | yes | yes | 21 | 0 | 0.000 | 0.000 | 0.000 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 0.00 |
| ritardando |  | yes | 28 | 15 | 0.536 | 0.625 | 0.577 | 0.51 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0920 | 56.22 |
| sparse_single_notes | yes | yes | 22 | 15 | 0.682 | 0.750 | 0.714 | 1.26 | 0.018 | - | 1.366 | 9.722 | 0.000 | 0.000 | 0.0000 | 0.0000 | 47.14 |
| stop_start | yes | yes | 26 | 22 | 0.846 | 0.688 | 0.759 | 0.27 | 0.021 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 15.82 |
| sustained_chords | yes | NO | 11 | 6 | 0.545 | 0.375 | 0.444 | 2.01 | 0.003 | - | 0.801 | 5.556 | 0.000 | 0.000 | 0.0000 | 0.0000 | 23.33 |
| syncopated_funk | yes | yes | 21 | 18 | 0.857 | 0.900 | 0.878 | 0.77 | 0.018 | - | 0.000 | 13.333 | 0.000 | 0.000 | 0.0182 | 0.0000 | 50.57 |
| tapping_muting_only |  | NO | 18 | 17 | 0.944 | 0.850 | 0.895 | 0.75 | 0.018 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 15.52 |
| waltz_3_4 |  | yes | 19 | 15 | 0.789 | 0.833 | 0.811 | 1.01 | 0.014 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.42 |

## Variant `compensated-23.22ms` — latency compensation 23.22 ms

| fixture | core | inform | pred | TP | P | R | F | acq bars | BPM err | h/d | trueSil F/s | unplayed F/s | unplayed off-grid F/s | recovery s | sync dev | ramp err | p95 phase ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| accelerando |  | yes | 25 | 10 | 0.400 | 0.417 | 0.408 | -1.00 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0751 | 62.18 |
| arpeggio | yes | yes | 22 | 19 | 0.864 | 0.950 | 0.905 | -1.00 | 0.021 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.22 |
| blues_shuffle | yes | yes | 22 | 16 | 0.727 | 0.800 | 0.762 | 1.99 | 0.003 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 23.89 |
| clean_eighths | yes | yes | 21 | 17 | 0.810 | 0.850 | 0.829 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 37.98 |
| clean_sixteenths | yes | yes | 22 | 19 | 0.864 | 0.950 | 0.905 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.41 |
| compound_6_8 |  | yes | 20 | 6 | 0.300 | 0.500 | 0.375 | -1.00 | 0.455 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 23.22 |
| line_input_clipping |  | yes | 22 | 17 | 0.773 | 0.850 | 0.810 | 2.98 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 28.84 |
| line_input_low_level |  | yes | 18 | 17 | 0.944 | 0.850 | 0.895 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 16.65 |
| missing_downbeats | yes | yes | 21 | 18 | 0.857 | 0.900 | 0.878 | -1.00 | 0.021 | - | 0.000 | 11.111 | 0.000 | 0.000 | 0.0000 | 0.0000 | 18.55 |
| noisy_microphone |  | yes | 22 | 16 | 0.727 | 0.800 | 0.762 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.41 |
| palm_mute_metal | yes | yes | 19 | 18 | 0.947 | 0.900 | 0.923 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.03 |
| power_chords_distorted | yes | yes | 21 | 0 | 0.000 | 0.000 | 0.000 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 0.00 |
| ritardando |  | yes | 28 | 13 | 0.464 | 0.542 | 0.500 | -1.00 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0920 | 68.45 |
| sparse_single_notes | yes | yes | 22 | 15 | 0.682 | 0.750 | 0.714 | 1.25 | 0.018 | - | 1.537 | 12.500 | 0.000 | 0.000 | 0.0000 | 0.0000 | 23.92 |
| stop_start | yes | yes | 26 | 23 | 0.885 | 0.719 | 0.793 | 0.25 | 0.021 | - | 0.210 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 28.98 |
| sustained_chords | yes | NO | 11 | 7 | 0.636 | 0.438 | 0.519 | -1.00 | 0.003 | - | 0.916 | 5.556 | 0.000 | 0.000 | 0.0000 | 0.0000 | 59.45 |
| syncopated_funk | yes | yes | 21 | 18 | 0.857 | 0.900 | 0.878 | 1.00 | 0.018 | - | 0.000 | 16.667 | 0.000 | 0.000 | 0.0182 | 0.0000 | 27.35 |
| tapping_muting_only |  | NO | 18 | 17 | 0.944 | 0.850 | 0.895 | -1.00 | 0.018 | - | 1.706 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 18.65 |
| waltz_3_4 |  | yes | 19 | 15 | 0.789 | 0.833 | 0.811 | 1.31 | 0.014 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 24.35 |

## Variant `compensated-11.61ms` — latency compensation 11.61 ms

| fixture | core | inform | pred | TP | P | R | F | acq bars | BPM err | h/d | trueSil F/s | unplayed F/s | unplayed off-grid F/s | recovery s | sync dev | ramp err | p95 phase ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| accelerando |  | yes | 25 | 9 | 0.360 | 0.375 | 0.367 | -1.00 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0751 | 44.84 |
| arpeggio | yes | yes | 22 | 19 | 0.864 | 0.950 | 0.905 | -1.00 | 0.021 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 16.28 |
| blues_shuffle | yes | yes | 22 | 16 | 0.727 | 0.800 | 0.762 | 1.99 | 0.003 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.06 |
| clean_eighths | yes | yes | 21 | 17 | 0.810 | 0.850 | 0.829 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 26.37 |
| clean_sixteenths | yes | yes | 22 | 19 | 0.864 | 0.950 | 0.905 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 19.34 |
| compound_6_8 |  | yes | 20 | 6 | 0.300 | 0.500 | 0.375 | -1.00 | 0.455 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 11.72 |
| line_input_clipping |  | yes | 22 | 17 | 0.773 | 0.850 | 0.810 | 2.99 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.23 |
| line_input_low_level |  | yes | 18 | 17 | 0.944 | 0.850 | 0.895 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 7.91 |
| missing_downbeats | yes | yes | 21 | 18 | 0.857 | 0.900 | 0.878 | -1.00 | 0.021 | - | 0.000 | 11.111 | 0.000 | 0.000 | 0.0000 | 0.0000 | 18.39 |
| noisy_microphone |  | yes | 22 | 16 | 0.727 | 0.800 | 0.762 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 7.91 |
| palm_mute_metal | yes | yes | 19 | 18 | 0.947 | 0.900 | 0.923 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 7.91 |
| power_chords_distorted | yes | yes | 21 | 0 | 0.000 | 0.000 | 0.000 | -1.00 | 0.023 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 0.00 |
| ritardando |  | yes | 28 | 15 | 0.536 | 0.625 | 0.577 | -1.00 | 0.000 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0920 | 65.19 |
| sparse_single_notes | yes | yes | 22 | 15 | 0.682 | 0.750 | 0.714 | 1.26 | 0.018 | - | 1.366 | 11.111 | 0.000 | 0.000 | 0.0000 | 0.0000 | 35.53 |
| stop_start | yes | yes | 26 | 22 | 0.846 | 0.688 | 0.759 | 0.26 | 0.021 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 17.37 |
| sustained_chords | yes | NO | 11 | 6 | 0.545 | 0.375 | 0.444 | 2.74 | 0.003 | - | 0.916 | 5.556 | 0.000 | 0.000 | 0.0000 | 0.0000 | 15.28 |
| syncopated_funk | yes | yes | 21 | 18 | 0.857 | 0.900 | 0.878 | 1.00 | 0.018 | - | 0.000 | 13.333 | 0.000 | 0.000 | 0.0182 | 0.0000 | 38.96 |
| tapping_muting_only |  | NO | 18 | 17 | 0.944 | 0.850 | 0.895 | 1.00 | 0.018 | - | 0.703 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 7.04 |
| waltz_3_4 |  | yes | 19 | 15 | 0.789 | 0.833 | 0.811 | 1.32 | 0.014 | - | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | 0.0000 | 12.74 |

## Aggregates

### `uncompensated`

- F-measure mean 0.7099 (precision 0.7039, recall 0.7235)
- BPM relative error (steady): mean 0.0432, median 0.0212, core worst 0.0234
- half/double-time error rate: 0.0000 (0/17), core 0.0000 (0/11)
- acquisition within 2 bars (core): 4/11 = 0.3636
- false beats in TRUE silence: worst 1.3663/s over 19 fixtures; worst informative 1.3663/s over 17 informative fixtures
- false beats in UNPLAYED-BEAT windows (separate metric): worst 13.3333/s, off-grid worst 0.0000/s
- ramp local-tempo relative error: mean 0.0838, worst 0.3409
- syncopation max deviation 0.0182, max step 0.0000
- CPU 8.2236 s total, 38742 allocations total

### `compensated-23.22ms`

- F-measure mean 0.7137 (precision 0.7090, recall 0.7262)
- BPM relative error (steady): mean 0.0432, median 0.0212, core worst 0.0234
- half/double-time error rate: 0.0000 (0/17), core 0.0000 (0/11)
- acquisition within 2 bars (core): 4/11 = 0.3636
- false beats in TRUE silence: worst 1.7063/s over 19 fixtures; worst informative 1.5371/s over 17 informative fixtures
- false beats in UNPLAYED-BEAT windows (separate metric): worst 16.6667/s, off-grid worst 0.0000/s
- ramp local-tempo relative error: mean 0.0836, worst 0.3409
- syncopation max deviation 0.0182, max step 0.0000
- CPU 8.2236 s total, 38742 allocations total

### `compensated-11.61ms`

- F-measure mean 0.7099 (precision 0.7039, recall 0.7235)
- BPM relative error (steady): mean 0.0432, median 0.0212, core worst 0.0234
- half/double-time error rate: 0.0000 (0/17), core 0.0000 (0/11)
- acquisition within 2 bars (core): 4/11 = 0.3636
- false beats in TRUE silence: worst 1.3663/s over 19 fixtures; worst informative 1.3663/s over 17 informative fixtures
- false beats in UNPLAYED-BEAT windows (separate metric): worst 13.3333/s, off-grid worst 0.0000/s
- ramp local-tempo relative error: mean 0.0836, worst 0.3409
- syncopation max deviation 0.0182, max step 0.0000
- CPU 8.2236 s total, 38742 allocations total

## Latency compensation effect

Positive mean phase means the predicted beats are LATE. Compensation subtracts the requested seconds from every predicted beat before scoring, so a correctly compensated backend moves the mean phase toward zero. Delta is `compensated - uncompensated`.

| metric | uncompensated | compensated | delta |
|---|---|---|---|
| `compensated-23.22ms` | | | |
| F-measure mean | 0.7099 | 0.7137 | +0.0038 |
| precision mean | 0.7039 | 0.7090 | +0.0052 |
| recall mean | 0.7235 | 0.7262 | +0.0027 |
| mean signed phase (ms) | 12.381 | -8.910 | -21.291 |
| mean |phase| (ms) | 16.150 | 14.916 | -1.234 |
| mean p95 |phase| (ms) | 28.159 | 29.529 | +1.370 |
| acquisition within 2 bars (core fraction) | 0.3636 | 0.3636 | +0.0000 |
| BPM rel error core worst | 0.0234 | 0.0234 | +0.0000 |
| false beats/s true silence (worst informative) | 1.3663 | 1.5371 | +0.1708 |
| false beats/s unplayed windows (worst, off-grid) | 0.0000 | 0.0000 | +0.0000 |
| `compensated-11.61ms` | | | |
| F-measure mean | 0.7099 | 0.7099 | +0.0000 |
| precision mean | 0.7039 | 0.7039 | +0.0000 |
| recall mean | 0.7235 | 0.7235 | +0.0000 |
| mean signed phase (ms) | 12.381 | 1.312 | -11.070 |
| mean |phase| (ms) | 16.150 | 10.587 | -5.563 |
| mean p95 |phase| (ms) | 28.159 | 21.504 | -6.655 |
| acquisition within 2 bars (core fraction) | 0.3636 | 0.3636 | +0.0000 |
| BPM rel error core worst | 0.0234 | 0.0234 | +0.0000 |
| false beats/s true silence (worst informative) | 1.3663 | 1.3663 | +0.0000 |
| false beats/s unplayed windows (worst, off-grid) | 0.0000 | 0.0000 | +0.0000 |

**The orchestrator, not the harness, decides whether to compensate.** Compensation is a claim about the backend, and it can be as wrong as no compensation: subtracting a latency the backend does not actually have is just a bias in the other direction, and for a non-causal or tempo-adaptive backend the delay may not even be constant. This table exists so the ADR can see the size of the decision, not so the harness can make it.

## SPEC 19 gates

### `uncompensated`

| gate | result | reason |
|---|---|---|
| acquire useful lock within 2 bars for >= 95% of core fixtures | FAIL | 4/11 core fixtures acquired within 2 bars (fraction 0.3636) |
| locked BPM relative error <= 2% on steady-tempo core fixtures | FAIL | worst core BPM relative error 0.0234 (11 core fixtures evaluated) |
| half/double-time errors < 5% on core fixtures | PASS | core half/double-time errors 0/11 (rate 0.0000) |
| no tempo jump from one isolated syncopated event | PASS | max first-difference of reported BPM 0.0000 of nominal |
| silence does not create false acceleration | FAIL | worst informative true-silence false-beat rate 1.3663/s (17 informative fixtures; SPEC gives no numeric threshold, any fabricated beat is read as a failure) |
| explicit resync establishes new phase within the requested boundary | NOT-MEASURED | the offline harness has no resync command path; the Musical Clock is the only place this can be tested |
| Follow handles controlled gradual tempo ramps without abrupt audible discontinuities | FAIL | mean local-tempo relative error 0.0838 over 2 ramp fixtures; audible discontinuity is not measurable offline |
| Loose Follow is measurably less reactive than Follow | NOT-MEASURED | requires the Musical Clock and a real controller; no clock runs in this offline harness |
| stop/start recovery succeeds without restarting the audio device | NOT-INFORMATIVE | offline proxy: worst post-stop lock recovery 0.0000 s; audio-device restart is not observable offline |

### `compensated-23.22ms`

| gate | result | reason |
|---|---|---|
| acquire useful lock within 2 bars for >= 95% of core fixtures | FAIL | 4/11 core fixtures acquired within 2 bars (fraction 0.3636) |
| locked BPM relative error <= 2% on steady-tempo core fixtures | FAIL | worst core BPM relative error 0.0234 (11 core fixtures evaluated) |
| half/double-time errors < 5% on core fixtures | PASS | core half/double-time errors 0/11 (rate 0.0000) |
| no tempo jump from one isolated syncopated event | PASS | max first-difference of reported BPM 0.0000 of nominal |
| silence does not create false acceleration | FAIL | worst informative true-silence false-beat rate 1.5371/s (17 informative fixtures; SPEC gives no numeric threshold, any fabricated beat is read as a failure) |
| explicit resync establishes new phase within the requested boundary | NOT-MEASURED | the offline harness has no resync command path; the Musical Clock is the only place this can be tested |
| Follow handles controlled gradual tempo ramps without abrupt audible discontinuities | FAIL | mean local-tempo relative error 0.0836 over 2 ramp fixtures; audible discontinuity is not measurable offline |
| Loose Follow is measurably less reactive than Follow | NOT-MEASURED | requires the Musical Clock and a real controller; no clock runs in this offline harness |
| stop/start recovery succeeds without restarting the audio device | NOT-INFORMATIVE | offline proxy: worst post-stop lock recovery 0.0000 s; audio-device restart is not observable offline |

### `compensated-11.61ms`

| gate | result | reason |
|---|---|---|
| acquire useful lock within 2 bars for >= 95% of core fixtures | FAIL | 4/11 core fixtures acquired within 2 bars (fraction 0.3636) |
| locked BPM relative error <= 2% on steady-tempo core fixtures | FAIL | worst core BPM relative error 0.0234 (11 core fixtures evaluated) |
| half/double-time errors < 5% on core fixtures | PASS | core half/double-time errors 0/11 (rate 0.0000) |
| no tempo jump from one isolated syncopated event | PASS | max first-difference of reported BPM 0.0000 of nominal |
| silence does not create false acceleration | FAIL | worst informative true-silence false-beat rate 1.3663/s (17 informative fixtures; SPEC gives no numeric threshold, any fabricated beat is read as a failure) |
| explicit resync establishes new phase within the requested boundary | NOT-MEASURED | the offline harness has no resync command path; the Musical Clock is the only place this can be tested |
| Follow handles controlled gradual tempo ramps without abrupt audible discontinuities | FAIL | mean local-tempo relative error 0.0836 over 2 ramp fixtures; audible discontinuity is not measurable offline |
| Loose Follow is measurably less reactive than Follow | NOT-MEASURED | requires the Musical Clock and a real controller; no clock runs in this offline harness |
| stop/start recovery succeeds without restarting the audio device | NOT-INFORMATIVE | offline proxy: worst post-stop lock recovery 0.0000 s; audio-device restart is not observable offline |

## Fixtures not informative for the true-silence false-beat metric

- `sustained_chords` (`uncompensated`): 8.73 s of true silence, 77% of the fixture — almost no playing to fabricate against, so a low rate is trivial and must not be read as a pass.
- `tapping_muting_only` (`uncompensated`): 9.96 s of true silence, 83% of the fixture — almost no playing to fabricate against, so a low rate is trivial and must not be read as a pass.
- `sustained_chords` (`compensated-23.22ms`): 8.73 s of true silence, 77% of the fixture — almost no playing to fabricate against, so a low rate is trivial and must not be read as a pass.
- `tapping_muting_only` (`compensated-23.22ms`): 9.96 s of true silence, 83% of the fixture — almost no playing to fabricate against, so a low rate is trivial and must not be read as a pass.
- `sustained_chords` (`compensated-11.61ms`): 8.73 s of true silence, 77% of the fixture — almost no playing to fabricate against, so a low rate is trivial and must not be read as a pass.
- `tapping_muting_only` (`compensated-11.61ms`): 9.96 s of true silence, 83% of the fixture — almost no playing to fabricate against, so a low rate is trivial and must not be read as a pass.

Criterion (stated, not hidden): a fixture is not informative when true silence covers at least half its duration. On this corpus that is exactly `sustained_chords` and `tapping_muting_only`, whose two-stage fast decay ends each note in ~0.2 s, so the declared true silence is most of the file.
