# Per-core-fixture acquisition reasons (TRACK-004)

`silenceOff*` columns are the bounded adapter-config experiment (gate disabled); they are NOT default evidence. Empty cells are missing measurements (failed lock, no nominal BPM, no match).

Tempo-clause counts are over the longest forward-advancing positional match run: agreeing / outside-band / missing-sample / phase-invalid / BPM-invalid. A numeric-band reason is only claimed when that is the sole failing clause type.

| backend | block | fixture | acq | bars | lock start (s) | lock confirm (s) | confirm avail (s) | match/tempo run | clause a/out/miss/phase/bpm | median BPM | BPM err | ratio | reason | acq gate-off |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| btrack | 128 | arpeggio | 0 |  |  |  |  | 19/0 | 0/19/0/0/0 | 117.454 | 0.021 | 0.979 | TempoOutsideBand | 0 |
| btrack | 512 | arpeggio | 0 |  |  |  |  | 19/0 | 0/19/0/0/0 | 117.454 | 0.021 | 0.979 | TempoOutsideBand | 0 |
| btrack | 128 | blues_shuffle | 1 | 1.995 | 4.783 | 6.455 | 6.469 | 15/5 | 9/6/0/0/0 | 107.666 | 0.003 | 0.997 | AcquiredWithin2Bars | 1 |
| btrack | 512 | blues_shuffle | 1 | 1.995 | 4.783 | 6.455 | 6.475 | 15/5 | 9/6/0/0/0 | 107.666 | 0.003 | 0.997 | AcquiredWithin2Bars | 1 |
| btrack | 128 | clean_eighths | 0 |  |  |  |  | 16/0 | 0/16/0/0/0 | 123.047 | 0.023 | 0.977 | TempoOutsideBand | 0 |
| btrack | 512 | clean_eighths | 0 |  |  |  |  | 16/0 | 0/16/0/0/0 | 123.047 | 0.023 | 0.977 | TempoOutsideBand | 0 |
| btrack | 128 | clean_sixteenths | 0 |  |  |  |  | 19/0 | 0/19/0/0/0 | 123.047 | 0.023 | 0.977 | TempoOutsideBand | 0 |
| btrack | 512 | clean_sixteenths | 0 |  |  |  |  | 19/0 | 0/19/0/0/0 | 123.047 | 0.023 | 0.977 | TempoOutsideBand | 0 |
| btrack | 128 | missing_downbeats | 0 |  |  |  |  | 18/0 | 0/18/0/0/0 | 117.454 | 0.021 | 0.979 | TempoOutsideBand | 0 |
| btrack | 512 | missing_downbeats | 0 |  |  |  |  | 18/0 | 0/18/0/0/0 | 117.454 | 0.021 | 0.979 | TempoOutsideBand | 0 |
| btrack | 128 | palm_mute_metal | 0 |  |  |  |  | 18/0 | 0/9/0/9/0 | 123.047 | 0.023 | 0.977 | TempoAgreementFailure | 0 |
| btrack | 512 | palm_mute_metal | 0 |  |  |  |  | 18/0 | 0/18/0/0/0 | 123.047 | 0.023 | 0.977 | TempoOutsideBand | 0 |
| btrack | 128 | power_chords_distorted | 0 |  |  |  |  | 0/0 | 0/0/0/0/0 | 123.047 | 0.023 |  | NoMatchingBeats | 0 |
| btrack | 512 | power_chords_distorted | 0 |  |  |  |  | 0/0 | 0/0/0/0/0 | 123.047 | 0.023 |  | NoMatchingBeats | 0 |
| btrack | 128 | sparse_single_notes | 1 | 1.256 | 3.042 | 4.656 | 4.669 | 15/15 | 15/0/0/0/0 | 109.957 | 0.018 | 0.982 | AcquiredWithin2Bars | 1 |
| btrack | 512 | sparse_single_notes | 1 | 1.256 | 3.042 | 4.656 | 4.672 | 13/13 | 13/0/0/0/0 | 109.957 | 0.018 | 0.982 | AcquiredWithin2Bars | 1 |
| btrack | 128 | stop_start | 1 | 0.261 | 0.824 | 2.159 | 2.173 | 15/6 | 6/8/0/1/0 | 129.199 | 0.021 | 0.979 | AcquiredWithin2Bars | 1 |
| btrack | 512 | stop_start | 1 | 0.261 | 0.824 | 2.159 | 2.176 | 15/6 | 6/9/0/0/0 | 129.199 | 0.021 | 0.979 | AcquiredWithin2Bars | 1 |
| btrack | 128 | sustained_chords | 1 | 2.744 | 7.210 | 9.718 | 9.731 | 6/4 | 5/1/0/0/0 | 95.703 | 0.003 | 0.997 | AcquiredAfter2Bars | 1 |
| btrack | 512 | sustained_chords | 0 |  |  |  |  | 2/2 | 2/0/0/0/0 | 95.703 | 0.003 | 0.997 | NoSustainedMatchRun | 1 |
| btrack | 128 | syncopated_funk | 1 | 1.002 | 2.496 | 4.110 | 4.123 | 18/16 | 16/2/0/0/0 | 109.957 | 0.018 | 0.982 | AcquiredWithin2Bars | 1 |
| btrack | 512 | syncopated_funk | 1 | 1.002 | 2.496 | 4.110 | 4.128 | 18/16 | 16/2/0/0/0 | 109.957 | 0.018 | 0.982 | AcquiredWithin2Bars | 1 |
| aubio | 128 | arpeggio | 1 | 1.254 | 2.858 | 4.346 | 4.352 | 15/15 | 15/0/0/0/0 | 121.400 | 0.012 | 1.012 | AcquiredWithin2Bars | 1 |
| aubio | 512 | arpeggio | 1 | 1.254 | 2.858 | 4.346 | 4.352 | 15/15 | 15/0/0/0/0 | 121.400 | 0.012 | 1.012 | AcquiredWithin2Bars | 1 |
| aubio | 128 | blues_shuffle | 1 | 1.258 | 3.145 | 4.804 | 4.811 | 15/15 | 15/0/0/0/0 | 108.885 | 0.008 | 1.008 | AcquiredWithin2Bars | 1 |
| aubio | 512 | blues_shuffle | 1 | 1.258 | 3.145 | 4.804 | 4.811 | 15/15 | 15/0/0/0/0 | 108.885 | 0.008 | 1.008 | AcquiredWithin2Bars | 1 |
| aubio | 128 | clean_eighths | 1 | 1.249 | 2.729 | 4.137 | 4.139 | 15/15 | 15/0/0/0/0 | 127.598 | 0.013 | 1.013 | AcquiredWithin2Bars | 1 |
| aubio | 512 | clean_eighths | 1 | 1.249 | 2.729 | 4.137 | 4.139 | 15/15 | 15/0/0/0/0 | 127.598 | 0.013 | 1.013 | AcquiredWithin2Bars | 1 |
| aubio | 128 | clean_sixteenths | 0 |  |  |  |  | 3/3 | 3/0/0/0/0 | 127.543 | 0.012 | 1.012 | NoSustainedMatchRun | 0 |
| aubio | 512 | clean_sixteenths | 0 |  |  |  |  | 3/3 | 3/0/0/0/0 | 127.543 | 0.012 | 1.012 | NoSustainedMatchRun | 0 |
| aubio | 128 | missing_downbeats | 1 | 1.504 | 3.358 | 4.851 | 4.853 | 15/14 | 14/0/0/1/0 | 121.439 | 0.012 | 1.012 | AcquiredWithin2Bars | 1 |
| aubio | 512 | missing_downbeats | 1 | 1.255 | 2.861 | 4.353 | 4.363 | 15/15 | 15/0/0/0/0 | 121.439 | 0.012 | 1.012 | AcquiredWithin2Bars | 1 |
| aubio | 128 | palm_mute_metal | 0 |  |  |  |  | 3/1 | 1/0/0/2/0 | 127.497 | 0.012 | 1.012 | NoSustainedMatchRun | 1 |
| aubio | 512 | palm_mute_metal | 0 |  |  |  |  | 3/3 | 3/0/0/0/0 | 127.497 | 0.012 | 1.012 | NoSustainedMatchRun | 1 |
| aubio | 128 | power_chords_distorted | 0 |  |  |  |  | 2/2 | 2/0/0/0/0 | 127.560 | 0.012 | 1.012 | NoSustainedMatchRun | 0 |
| aubio | 512 | power_chords_distorted | 0 |  |  |  |  | 2/2 | 2/0/0/0/0 | 127.560 | 0.012 | 1.012 | NoSustainedMatchRun | 0 |
| aubio | 128 | sparse_single_notes | 1 | 1.254 | 3.037 | 4.630 | 4.640 | 15/15 | 15/0/0/0/0 | 113.184 | 0.011 | 1.011 | AcquiredWithin2Bars | 1 |
| aubio | 512 | sparse_single_notes | 1 | 1.254 | 3.037 | 4.630 | 4.640 | 14/14 | 14/0/0/0/0 | 113.184 | 0.011 | 1.011 | AcquiredWithin2Bars | 1 |
| aubio | 128 | stop_start | 1 | 1.744 | 3.521 | 4.868 | 4.875 | 11/10 | 10/0/0/1/0 | 133.697 | 0.013 | 1.013 | AcquiredWithin2Bars | 1 |
| aubio | 512 | stop_start | 1 | 1.497 | 3.073 | 4.419 | 4.427 | 11/11 | 11/0/0/0/0 | 133.697 | 0.013 | 1.013 | AcquiredWithin2Bars | 1 |
| aubio | 128 | sustained_chords | 0 |  |  |  |  | 4/3 | 3/1/0/0/0 | 97.276 | 0.013 | 1.013 | TempoOutsideBand | 1 |
| aubio | 512 | sustained_chords | 0 |  |  |  |  | 1/0 | 0/1/0/0/0 | 97.276 | 0.013 | 1.490 | NoSustainedMatchRun | 1 |
| aubio | 128 | syncopated_funk | 1 | 1.501 | 3.566 | 5.160 | 5.163 | 10/9 | 9/0/0/1/0 | 113.009 | 0.009 | 1.009 | AcquiredWithin2Bars | 1 |
| aubio | 512 | syncopated_funk | 1 | 1.253 | 3.034 | 4.628 | 4.629 | 10/10 | 10/0/0/0/0 | 113.009 | 0.009 | 1.009 | AcquiredWithin2Bars | 1 |
