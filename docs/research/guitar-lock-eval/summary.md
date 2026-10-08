# Representative-guitar useful-lock evaluation (EVAL-GUITAR-009)

- generated: `2026-10-08T12:35:47Z`
- base commit: `984ad1d32c44157ad2d4128c47b86dc932535918`
- protocol: `/home/mojo/projects/worktrees/EVAL-GUITAR-009/tools/guitar-lock-eval/protocol/useful-lock-protocol.json` sha256 `c92bbdf37ccfc85b4ebb63fa39cd6dfc4137dba4535a5a874f5fd3c6a28c5305`
- import manifest: `/home/mojo/projects/worktrees/EVAL-GUITAR-009/docs/research/guitar-lock-eval/baseline-import.json` sha256 `a97a342dadfd5be0298f5b9a6d42ae2f9dbaaee322c05c1ad4ab2e4adb72be55`
- block frames: 128
- evidence class: **diagnostic baseline (synthetic corpus); real-guitar G3 gate cannot pass without representative real recordings**
- protocol unchanged during measurement: yes

## Comparison (useful lock = correct BPM AND usable phase within two bars)

| backend | population | n | useful lock | fraction | half | double | false lock | no lock | gate |
|---|---|---|---|---|---|---|---|---|---|
| btrack | gate | 0 | 0 | n/a | 0 | 0 | 0 | 0 | FAIL |
| aubio | gate | 0 | 0 | n/a | 0 | 0 | 0 | 0 | FAIL |
| btrack | diagnostic | 19 | 5 | 26.3% | 0 | 0 | 9 | 1 | n/a (diagnostic) |
| aubio | diagnostic | 19 | 12 | 63.2% | 0 | 0 | 4 | 3 | n/a (diagnostic) |
| diagnostic-beat-interval-bpm | diagnostic | 19 | 12 | 63.2% | 1 | 0 | 5 | 0 | n/a (diagnostic) |

## btrack (gate)

Fail-closed reasons:
- empty gate population: no representative real annotated steady recording

Gate eligibility:
- accelerando: not eligible (classification=synthetic (gate needs real))
- arpeggio: not eligible (classification=synthetic (gate needs real))
- blues_shuffle: not eligible (classification=synthetic (gate needs real))
- clean_eighths: not eligible (classification=synthetic (gate needs real))
- clean_sixteenths: not eligible (classification=synthetic (gate needs real))
- compound_6_8: not eligible (classification=synthetic (gate needs real))
- line_input_clipping: not eligible (classification=synthetic (gate needs real))
- line_input_low_level: not eligible (classification=synthetic (gate needs real))
- missing_downbeats: not eligible (classification=synthetic (gate needs real))
- noisy_microphone: not eligible (classification=synthetic (gate needs real))
- palm_mute_metal: not eligible (classification=synthetic (gate needs real))
- power_chords_distorted: not eligible (classification=synthetic (gate needs real))
- ritardando: not eligible (classification=synthetic (gate needs real))
- sparse_single_notes: not eligible (classification=synthetic (gate needs real))
- stop_start: not eligible (classification=synthetic (gate needs real))
- sustained_chords: not eligible (classification=synthetic (gate needs real))
- syncopated_funk: not eligible (classification=synthetic (gate needs real))
- tapping_muting_only: not eligible (classification=synthetic (gate needs real))
- waltz_3_4: not eligible (classification=synthetic (gate needs real))

## aubio (gate)

Fail-closed reasons:
- empty gate population: no representative real annotated steady recording

Gate eligibility:
- accelerando: not eligible (classification=synthetic (gate needs real))
- arpeggio: not eligible (classification=synthetic (gate needs real))
- blues_shuffle: not eligible (classification=synthetic (gate needs real))
- clean_eighths: not eligible (classification=synthetic (gate needs real))
- clean_sixteenths: not eligible (classification=synthetic (gate needs real))
- compound_6_8: not eligible (classification=synthetic (gate needs real))
- line_input_clipping: not eligible (classification=synthetic (gate needs real))
- line_input_low_level: not eligible (classification=synthetic (gate needs real))
- missing_downbeats: not eligible (classification=synthetic (gate needs real))
- noisy_microphone: not eligible (classification=synthetic (gate needs real))
- palm_mute_metal: not eligible (classification=synthetic (gate needs real))
- power_chords_distorted: not eligible (classification=synthetic (gate needs real))
- ritardando: not eligible (classification=synthetic (gate needs real))
- sparse_single_notes: not eligible (classification=synthetic (gate needs real))
- stop_start: not eligible (classification=synthetic (gate needs real))
- sustained_chords: not eligible (classification=synthetic (gate needs real))
- syncopated_funk: not eligible (classification=synthetic (gate needs real))
- tapping_muting_only: not eligible (classification=synthetic (gate needs real))
- waltz_3_4: not eligible (classification=synthetic (gate needs real))

## btrack (diagnostic)

| recording | class | useful | acq bars | locked BPM | BPM err | phase mean abs ms | false lock | no lock |
|---|---|---|---|---|---|---|---|---|
| accelerando | synthetic | no |  | 126.048 |  | 29.2 | yes | no |
| arpeggio | synthetic | no |  | 117.454 | 2.12% | 8.7 | yes | no |
| blues_shuffle | synthetic | yes | 1.749 | 107.666 | 0.31% | 14.8 | no | no |
| clean_eighths | synthetic | no |  | 123.047 | 2.34% | 14.2 | yes | no |
| clean_sixteenths | synthetic | no |  | 123.047 | 2.34% | 12.9 | yes | no |
| compound_6_8 | synthetic | no |  | 139.675 | 45.49% | 4.8 | yes | no |
| line_input_clipping | synthetic | no | 2.748 | 126.048 | 0.04% | 10.9 | no | no |
| line_input_low_level | synthetic | no |  | 123.047 | 2.34% | 6.3 | yes | no |
| missing_downbeats | synthetic | no |  | 117.454 | 2.12% | 8.5 | yes | no |
| noisy_microphone | synthetic | no |  | 123.047 | 2.34% | 10.1 | yes | no |
| palm_mute_metal | synthetic | no |  | 123.047 | 2.34% | 6.7 | yes | no |
| power_chords_distorted | synthetic | no |  | 123.047 | 2.34% |  | no | yes |
| ritardando | synthetic | no | 0.291 | 135.999 |  | 20.3 | no | no |
| sparse_single_notes | synthetic | yes | 1.256 | 109.957 | 1.82% | 7.7 | no | no |
| stop_start | synthetic | no | 0.261 | 129.199 | 2.12% | 10.2 | no | no |
| sustained_chords | synthetic | no | 2.006 | 95.703 | 0.31% | 1.7 | no | no |
| syncopated_funk | synthetic | yes | 0.769 | 109.957 | 1.82% | 14.2 | no | no |
| tapping_muting_only | synthetic | yes | 1.002 | 109.957 | 1.82% | 2.8 | no | no |
| waltz_3_4 | synthetic | yes | 1.005 | 135.999 | 1.45% | 8.4 | no | no |

Gate eligibility:

## aubio (diagnostic)

| recording | class | useful | acq bars | locked BPM | BPM err | phase mean abs ms | false lock | no lock |
|---|---|---|---|---|---|---|---|---|
| accelerando | synthetic | no |  | 120.655 |  | 48.5 | no | yes |
| arpeggio | synthetic | yes | 1.254 | 121.400 | 1.17% | 4.2 | no | no |
| blues_shuffle | synthetic | yes | 1.258 | 108.885 | 0.82% | 14.7 | no | no |
| clean_eighths | synthetic | yes | 1.249 | 127.598 | 1.27% | 12.0 | no | no |
| clean_sixteenths | synthetic | no |  | 127.509 | 1.20% | 11.0 | yes | no |
| compound_6_8 | synthetic | no |  | 145.161 | 51.21% |  | yes | no |
| line_input_clipping | synthetic | yes | 1.255 | 127.476 | 1.17% | 4.8 | no | no |
| line_input_low_level | synthetic | yes | 1.497 | 127.407 | 1.12% | 2.7 | no | no |
| missing_downbeats | synthetic | yes | 1.255 | 121.439 | 1.20% | 8.3 | no | no |
| noisy_microphone | synthetic | yes | 1.252 | 127.511 | 1.20% | 5.4 | no | no |
| palm_mute_metal | synthetic | no |  | 127.389 | 1.10% |  | no | yes |
| power_chords_distorted | synthetic | no |  | 127.560 | 1.24% |  | yes | no |
| ritardando | synthetic | no |  | 140.629 |  | 32.0 | no | yes |
| sparse_single_notes | synthetic | yes | 1.254 | 113.184 | 1.06% | 4.6 | no | no |
| stop_start | synthetic | yes | 1.497 | 133.697 | 1.29% | 10.3 | no | no |
| sustained_chords | synthetic | no |  | 97.211 | 1.26% | 25.8 | yes | no |
| syncopated_funk | synthetic | yes | 1.253 | 113.009 | 0.90% | 4.3 | no | no |
| tapping_muting_only | synthetic | yes | 1.254 | 113.015 | 0.91% | 4.3 | no | no |
| waltz_3_4 | synthetic | yes | 1.998 | 140.056 | 1.49% | 3.0 | no | no |

Gate eligibility:

## diagnostic-beat-interval-bpm (diagnostic)

| recording | class | useful | acq bars | locked BPM | BPM err | phase mean abs ms | false lock | no lock |
|---|---|---|---|---|---|---|---|---|
| accelerando | synthetic | no |  | 117.455 |  | 29.2 | yes | no |
| arpeggio | synthetic | yes | 1.003 | 120.185 | 0.15% | 8.7 | no | no |
| blues_shuffle | synthetic | yes | 1.504 | 107.666 | 0.31% | 14.8 | no | no |
| clean_eighths | synthetic | yes | 1.998 | 126.048 | 0.04% | 14.2 | no | no |
| clean_sixteenths | synthetic | yes | 1.505 | 126.048 | 0.04% | 12.9 | no | no |
| compound_6_8 | synthetic | no |  | 143.555 | 49.54% | 4.8 | yes | no |
| line_input_clipping | synthetic | yes | 1.755 | 126.049 | 0.04% | 10.9 | no | no |
| line_input_low_level | synthetic | yes | 1.755 | 126.048 | 0.04% | 6.3 | no | no |
| missing_downbeats | synthetic | yes | 0.998 | 120.185 | 0.15% | 8.5 | no | no |
| noisy_microphone | synthetic | no | 2.004 | 126.048 | 0.04% | 10.1 | no | no |
| palm_mute_metal | synthetic | yes | 0.999 | 126.048 | 0.04% | 6.7 | no | no |
| power_chords_distorted | synthetic | no |  | 126.048 | 0.04% |  | yes | no |
| ritardando | synthetic | no |  | 126.048 |  | 20.3 | yes | no |
| sparse_single_notes | synthetic | no | 2.009 | 111.139 | 0.77% | 7.7 | no | no |
| stop_start | synthetic | yes | 1.244 | 132.511 | 0.39% | 10.2 | no | no |
| sustained_chords | synthetic | no |  | 51.423 | 46.43% | 1.7 | yes | no |
| syncopated_funk | synthetic | yes | 1.500 | 111.139 | 0.77% | 14.2 | no | no |
| tapping_muting_only | synthetic | yes | 1.251 | 112.346 | 0.31% | 2.8 | no | no |
| waltz_3_4 | synthetic | yes | 1.993 | 137.812 | 0.14% | 8.4 | no | no |

Gate eligibility:

