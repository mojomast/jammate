# Baseline vs tempo-variant — original-block512

- beat-event series exact equality (sha256 of every beats/*.csv): **YES**
- fixtures: 19 (expected 19); acquisition-anytime flips: 7 gained / 0 lost
- **SPEC 19 core within 2 bars**: 4 -> 8 of 11 (gained 5, lost 1)
- all-19 acquisition-anytime: 7 -> 14
- core gained: arpeggio, clean_eighths, clean_sixteenths, missing_downbeats, palm_mute_metal
- core lost: sparse_single_notes
- variant acquired but LATE (>2 bars): line_input_clipping, line_input_low_level, noisy_microphone, sparse_single_notes, waltz_3_4

## Aggregate (event clock; from the unmodified CLI summary.json)

| metric | baseline | variant |
|---|---|---|
| acquisitionCoreWithin2Bars | 4 | 8 |
| acquisitionCoreEvaluated | 11 | 11 |
| bpmRelErrorMeanSteady | 0.0432364223 | 0.0312347957 |
| bpmRelErrorMedianSteady | 0.0212180456 | 0.0015440623 |
| bpmRelErrorWorstCore | 0.0234375 | 0.00768763678 |
| fMeasureMean | 0.694841981 | 0.694841981 |
| precisionMean | 0.702095461 | 0.702095461 |
| recallMean | 0.70504386 | 0.70504386 |
| halfDoubleErrorRateCore | 0 | 0 |
| falseBeatsInTrueSilencePerSecondWorstInformative | 1.0247284 | 1.0247284 |
| maxSilenceTempoIncreaseBpm | 0 | 0 |
| silenceAccelerationEvaluatedFixtures | 4 | 4 |
| phaseMeasuredFixtures | 18 | 18 |

## Per-fixture acquisition (event start / causal confirm availability)

| fixture | core | base acq (w2/late) | var acq (w2/late) | base confirm ev/avail | var confirm ev/avail | base reason | var reason |
|---|---|---|---|---|---|---|---|
| accelerando | 0 | 0(0/0) | 0(0/0) | / | / | TempoOutsideBand | TempoOutsideBand |
| arpeggio | 1 | 0(0/0) | 1(1/0) | / | 4.35375/4.373333333 | TempoOutsideBand | AcquiredWithin2Bars |
| blues_shuffle | 1 | 1(1/0) | 1(1/0) | 6.455145833/6.474666667 | 5.897875/5.92 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| clean_eighths | 1 | 0(0/0) | 1(1/0) | / | 5.596/5.610666667 | TempoOutsideBand | AcquiredWithin2Bars |
| clean_sixteenths | 1 | 0(0/0) | 1(1/0) | / | 5.12/5.141333333 | TempoOutsideBand | AcquiredWithin2Bars |
| compound_6_8 | 0 | 0(0/0) | 0(0/0) | / | / | NoSustainedMatchRun | NoSustainedMatchRun |
| line_input_clipping | 0 | 1(0/1) | 1(0/1) | 7.012416667/7.029333333 | 5.584395833/5.6 | AcquiredAfter2Bars | AcquiredAfter2Bars |
| line_input_low_level | 0 | 0(0/0) | 1(0/1) | / | 5.596/5.610666667 | TempoOutsideBand | AcquiredAfter2Bars |
| missing_downbeats | 1 | 0(0/0) | 1(1/0) | / | 4.35375/4.373333333 | TempoOutsideBand | AcquiredWithin2Bars |
| noisy_microphone | 0 | 0(0/0) | 1(0/1) | / | 5.584395833/5.6 | TempoOutsideBand | AcquiredAfter2Bars |
| palm_mute_metal | 1 | 0(0/0) | 1(1/0) | / | 4.167979167/4.181333333 | TempoOutsideBand | AcquiredWithin2Bars |
| power_chords_distorted | 1 | 0(0/0) | 0(0/0) | / | / | NoMatchingBeats | NoMatchingBeats |
| ritardando | 0 | 0(0/0) | 0(0/0) | / | / | TempoOutsideBand | TempoOutsideBand |
| sparse_single_notes | 1 | 1(1/0) | 1(0/1) | 4.655604167/4.672 | 6.791833333/6.805333333 | AcquiredWithin2Bars | AcquiredAfter2Bars |
| stop_start | 1 | 1(1/0) | 1(1/0) | 2.159458333/2.176 | 2.159458333/2.176 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| sustained_chords | 1 | 0(0/0) | 0(0/0) | / | / | NoSustainedMatchRun | NoSustainedMatchRun |
| syncopated_funk | 1 | 1(1/0) | 1(1/0) | 4.1099375/4.128 | 5.712104167/5.728 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| tapping_muting_only | 0 | 1(1/0) | 1(1/0) | 3.564270833/3.584 | 3.564270833/3.584 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| waltz_3_4 | 0 | 1(1/0) | 1(0/1) | 2.9489375/2.965333333 | 4.702041667/4.714666667 | AcquiredWithin2Bars | AcquiredAfter2Bars |
