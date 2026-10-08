# Baseline vs tempo-variant — original-block128

- beat-event series exact equality (sha256 of every beats/*.csv): **YES**
- fixtures: 19 (expected 19); acquisition-anytime flips: 6 gained / 1 lost
- **SPEC 19 core within 2 bars**: 4 -> 6 of 11 (gained 3, lost 1)
- all-19 acquisition-anytime: 8 -> 13
- core gained: arpeggio, clean_sixteenths, missing_downbeats
- core lost: sparse_single_notes
- variant acquired but LATE (>2 bars): clean_eighths, line_input_clipping, line_input_low_level, noisy_microphone, sparse_single_notes, waltz_3_4

## Aggregate (event clock; from the unmodified CLI summary.json)

| metric | baseline | variant |
|---|---|---|
| acquisitionCoreWithin2Bars | 4 | 6 |
| acquisitionCoreEvaluated | 11 | 11 |
| bpmRelErrorMeanSteady | 0.0432364223 | 0.0312347957 |
| bpmRelErrorMedianSteady | 0.0212180456 | 0.0015440623 |
| bpmRelErrorWorstCore | 0.0234375 | 0.00768763678 |
| fMeasureMean | 0.709905541 | 0.709905541 |
| precisionMean | 0.703854044 | 0.703854044 |
| recallMean | 0.723464912 | 0.723464912 |
| halfDoubleErrorRateCore | 0 | 0 |
| falseBeatsInTrueSilencePerSecondWorstInformative | 1.36630454 | 1.36630454 |
| maxSilenceTempoIncreaseBpm | 0 | 44.2805214 |
| silenceAccelerationEvaluatedFixtures | 4 | 4 |
| phaseMeasuredFixtures | 18 | 18 |

## Per-fixture acquisition (event start / causal confirm availability)

| fixture | core | base acq (w2/late) | var acq (w2/late) | base confirm ev/avail | var confirm ev/avail | base reason | var reason |
|---|---|---|---|---|---|---|---|
| accelerando | 0 | 0(0/0) | 0(0/0) | / | / | TempoOutsideBand | TempoOutsideBand |
| arpeggio | 1 | 0(0/0) | 1(1/0) | / | 4.35375/4.368 | TempoOutsideBand | AcquiredWithin2Bars |
| blues_shuffle | 1 | 1(1/0) | 1(1/0) | 6.455145833/6.469333333 | 5.897875/5.912 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| clean_eighths | 1 | 0(0/0) | 1(0/1) | / | 6.072020833/6.085333333 | TempoOutsideBand | AcquiredAfter2Bars |
| clean_sixteenths | 1 | 0(0/0) | 1(1/0) | / | 5.12/5.133333333 | TempoOutsideBand | AcquiredWithin2Bars |
| compound_6_8 | 0 | 0(0/0) | 0(0/0) | / | / | NoSustainedMatchRun | NoSustainedMatchRun |
| line_input_clipping | 0 | 1(0/1) | 1(0/1) | 7.476833333/7.490666667 | 5.584395833/5.597333333 | AcquiredAfter2Bars | AcquiredAfter2Bars |
| line_input_low_level | 0 | 0(0/0) | 1(0/1) | / | 5.596/5.608 | TempoAgreementFailure | AcquiredAfter2Bars |
| missing_downbeats | 1 | 0(0/0) | 1(1/0) | / | 4.864583333/4.877333333 | TempoOutsideBand | AcquiredWithin2Bars |
| noisy_microphone | 0 | 0(0/0) | 1(0/1) | / | 6.072020833/6.085333333 | TempoOutsideBand | AcquiredAfter2Bars |
| palm_mute_metal | 1 | 0(0/0) | 0(0/0) | / | / | TempoAgreementFailure | TempoAgreementFailure |
| power_chords_distorted | 1 | 0(0/0) | 0(0/0) | / | / | NoMatchingBeats | NoMatchingBeats |
| ritardando | 0 | 0(0/0) | 0(0/0) | / | / | TempoOutsideBand | TempoOutsideBand |
| sparse_single_notes | 1 | 1(1/0) | 1(0/1) | 4.655604167/4.669333333 | 6.791833333/6.805333333 | AcquiredWithin2Bars | AcquiredAfter2Bars |
| stop_start | 1 | 1(1/0) | 1(1/0) | 2.159458333/2.173333333 | 2.159458333/2.173333333 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| sustained_chords | 1 | 1(0/1) | 0(0/0) | 9.717541667/9.730666667 | / | AcquiredAfter2Bars | TempoOutsideBand |
| syncopated_funk | 1 | 1(1/0) | 1(1/0) | 4.1099375/4.122666667 | 5.712104167/5.725333333 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| tapping_muting_only | 0 | 1(1/0) | 1(1/0) | 4.0983125/4.112 | 4.0983125/4.112 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| waltz_3_4 | 0 | 1(1/0) | 1(0/1) | 3.390104167/3.402666667 | 4.702041667/4.714666667 | AcquiredWithin2Bars | AcquiredAfter2Bars |
