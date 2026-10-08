# Baseline vs tempo-variant — original-block512

- beat-event series exact equality (sha256 of every beats/*.csv): **YES**

- acquisition flips: 7 (gained 7, lost 0)

- gained: arpeggio, clean_eighths, clean_sixteenths, line_input_low_level, missing_downbeats, noisy_microphone, palm_mute_metal

- lost: (none)


## Aggregate (event clock)

| metric | baseline | variant |
|---|---|---|
| acquisitionCoreWithin2Bars | 4 | 8 |
| acquisitionCoreEvaluated | 11 | 11 |
| acquisitionCorePassFraction | 0.363636364 | 0.727272727 |
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

## Per-fixture acquisition and tempo

| fixture | core | base acq | var acq | base confirm ev/avail | var confirm ev/avail | base BPM | var BPM | base reason | var reason |
|---|---|---|---|---|---|---|---|---|---|
| accelerando | 0 | 0 | 0 | / | / | 114.84375 | 117.4551392 | TempoOutsideBand | TempoOutsideBand |
| arpeggio | 1 | 0 | 1 | / | 4.35375/4.373333333 | 117.4538345 | 120.1852875 | TempoOutsideBand | AcquiredWithin2Bars |
| blues_shuffle | 1 | 1 | 1 | 6.455145833/6.474666667 | 5.897875/5.92 | 107.6660156 | 107.6675797 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| clean_eighths | 1 | 0 | 1 | / | 5.596/5.610666667 | 123.046875 | 126.0476608 | TempoOutsideBand | AcquiredWithin2Bars |
| clean_sixteenths | 1 | 0 | 1 | / | 5.12/5.141333333 | 123.046875 | 126.0476608 | TempoOutsideBand | AcquiredWithin2Bars |
| compound_6_8 | 0 | 0 | 0 | / | / | 139.6748352 | 143.5549774 | NoSustainedMatchRun | NoSustainedMatchRun |
| line_input_clipping | 0 | 1 | 1 | 7.012416667/7.029333333 | 5.584395833/5.6 | 126.0480194 | 126.0476608 | AcquiredAfter2Bars | AcquiredAfter2Bars |
| line_input_low_level | 0 | 0 | 1 | / | 5.596/5.610666667 | 123.046875 | 126.0476608 | TempoOutsideBand | AcquiredAfter2Bars |
| missing_downbeats | 1 | 0 | 1 | / | 4.35375/4.373333333 | 117.4538345 | 118.8045273 | TempoOutsideBand | AcquiredWithin2Bars |
| noisy_microphone | 0 | 0 | 1 | / | 5.584395833/5.6 | 123.046875 | 126.0476608 | TempoOutsideBand | AcquiredAfter2Bars |
| palm_mute_metal | 1 | 0 | 1 | / | 4.167979167/4.181333333 | 123.046875 | 126.0476608 | TempoOutsideBand | AcquiredWithin2Bars |
| power_chords_distorted | 1 | 0 | 0 | / | / | 123.046875 | 126.0476608 | NoMatchingBeats | NoMatchingBeats |
| ritardando | 0 | 0 | 0 | / | / | 135.999176 | 132.5112762 | TempoOutsideBand | TempoOutsideBand |
| sparse_single_notes | 1 | 1 | 1 | 4.655604167/4.672 | 6.791833333/6.805333333 | 109.9567795 | 111.1389847 | AcquiredWithin2Bars | AcquiredAfter2Bars |
| stop_start | 1 | 1 | 1 | 2.159458333/2.176 | 2.159458333/2.176 | 129.1992188 | 132.5112762 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| sustained_chords | 1 | 0 | 0 | / | / | 95.703125 | 95.703125 | NoSustainedMatchRun | NoSustainedMatchRun |
| syncopated_funk | 1 | 1 | 1 | 4.1099375/4.128 | 5.712104167/5.728 | 109.9567795 | 111.1389847 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| tapping_muting_only | 0 | 1 | 1 | 3.564270833/3.584 | 3.564270833/3.584 | 109.9567795 | 112.346405 | AcquiredWithin2Bars | AcquiredWithin2Bars |
| waltz_3_4 | 0 | 1 | 1 | 2.9489375/2.965333333 | 4.702041667/4.714666667 | 135.999176 | 135.9998169 | AcquiredWithin2Bars | AcquiredAfter2Bars |
