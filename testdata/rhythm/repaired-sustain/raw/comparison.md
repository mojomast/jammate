# Original vs repaired manifest — real CLI runs (EVAL-006)

Both manifests were run with the current-main CLI and the two unchanged plugin shims at 128-frame blocks, uncompensated, no legacy stamping. Raw output is under `raw/`.

- Denominators: **19 fixtures, 11 core fixtures** on both manifests (unchanged).

## `btrack`

corpus ids: original `eval001-guitar-corpus`, repaired `eval006-sustain-repair`

| `sustained_chords` metric | original | repaired | delta |
|---|---:|---:|---:|
| predictedBeats | 11 | 17 | 6 |
| truthBeats | 16 | 16 | 0 |
| truePositives | 6 | 12 | 6 |
| falsePositives | 5 | 5 | 0 |
| falseNegatives | 10 | 4 | -6 |
| precision | 0.545455 | 0.705882 | 0.160428 |
| recall | 0.375 | 0.75 | 0.375 |
| fMeasure | 0.444444 | 0.727273 | 0.282828 |
| acquired | yes | yes | - |
| acquisitionSeconds | 6.85979 | 3.77154 | -3.08825 |
| acquisitionBars | 2.74392 | 1.50862 | -1.2353 |
| hasBpmLock | yes | yes | - |
| lockedBpm | 95.7031 | 95.7031 | 0 |
| bpmRelativeError | 0.00309245 | 0.00309245 | 0 |
| halfDoubleTimeError | no | no | - |
| phaseMeasured | yes | yes | - |
| phaseMeanAbsMs | 10.1389 | 35.9722 | 25.8333 |
| phaseP95AbsMs | 15.2083 | 66.0625 | 50.8542 |
| trueSilenceMeasured | yes | yes | - |
| trueSilenceSeconds | 8.73417 | 1.43 | -7.30417 |
| falseBeatsInTrueSilence | 8 | 1 | -7 |
| falseBeatsInTrueSilencePerSecond | 0.915943 | 0.699301 | -0.216642 |
| falseBeatCoverage | CorpusDefect | CorpusDefect | - |

| corpus aggregate | original | repaired | delta |
|---|---:|---:|---:|
| fixtures | 19 | 19 | 0 |
| coreFixtures | 11 | 11 | 0 |
| acquisitionCoreEvaluated | 11 | 11 | 0 |
| acquisitionCoreWithin2Bars | 4 | 5 | 1 |
| acquisitionCorePassFraction | 0.363636 | 0.454545 | 0.0909091 |
| bpmRelErrorCoreEvaluated | 11 | 11 | 0 |
| bpmRelErrorWorstCore | 0.0234375 | 0.0234375 | 0 |
| fMeasureMean | 0.709906 | 0.724791 | 0.0148857 |
| precisionMean | 0.703854 | 0.712298 | 0.00844357 |
| recallMean | 0.723465 | 0.743202 | 0.0197368 |
| trueSilenceMeasuredFixtures | 17 | 17 | 0 |
| trueSilenceNoSilenceFixtures | 0 | 0 | 0 |
| trueSilenceCorpusDefectFixtures | 2 | 2 | 0 |

SPEC 19 gate booleans (not a selection): original `{"bpm2CoreEvaluated": true, "gateAcquire95Core": false, "gateBpm2Core": false, "gateHalfDouble5Core": true}`, repaired `{"bpm2CoreEvaluated": true, "gateAcquire95Core": false, "gateBpm2Core": false, "gateHalfDouble5Core": true}`.

## `aubio`

corpus ids: original `eval001-guitar-corpus`, repaired `eval006-sustain-repair`

| `sustained_chords` metric | original | repaired | delta |
|---|---:|---:|---:|
| predictedBeats | 10 | 18 | 8 |
| truthBeats | 16 | 16 | 0 |
| truePositives | 5 | 10 | 5 |
| falsePositives | 5 | 8 | 3 |
| falseNegatives | 11 | 6 | -5 |
| precision | 0.5 | 0.555556 | 0.0555556 |
| recall | 0.3125 | 0.625 | 0.3125 |
| fMeasure | 0.384615 | 0.588235 | 0.20362 |
| acquired | no | yes | - |
| acquisitionSeconds | 0 | 5.62229 | 5.62229 |
| acquisitionBars | 0 | 2.24892 | 2.24892 |
| hasBpmLock | yes | yes | - |
| lockedBpm | 97.2761 | 97.2881 | 0.0119323 |
| bpmRelativeError | 0.013293 | 0.0134173 | 0.000124295 |
| halfDoubleTimeError | no | no | - |
| phaseMeasured | yes | yes | - |
| phaseMeanAbsMs | 24.7708 | 25.5521 | 0.78125 |
| phaseP95AbsMs | 42.6667 | 66.3542 | 23.6875 |
| trueSilenceMeasured | yes | yes | - |
| trueSilenceSeconds | 8.73417 | 1.43 | -7.30417 |
| falseBeatsInTrueSilence | 7 | 1 | -6 |
| falseBeatsInTrueSilencePerSecond | 0.80145 | 0.699301 | -0.102149 |
| falseBeatCoverage | CorpusDefect | CorpusDefect | - |

| corpus aggregate | original | repaired | delta |
|---|---:|---:|---:|
| fixtures | 19 | 19 | 0 |
| coreFixtures | 11 | 11 | 0 |
| acquisitionCoreEvaluated | 11 | 11 | 0 |
| acquisitionCoreWithin2Bars | 7 | 7 | 0 |
| acquisitionCorePassFraction | 0.636364 | 0.636364 | 0 |
| bpmRelErrorCoreEvaluated | 11 | 11 | 0 |
| bpmRelErrorWorstCore | 0.013293 | 0.0134173 | 0.000124295 |
| fMeasureMean | 0.535727 | 0.546444 | 0.0107168 |
| precisionMean | 0.654673 | 0.657597 | 0.00292398 |
| recallMean | 0.472478 | 0.488925 | 0.0164474 |
| trueSilenceMeasuredFixtures | 17 | 17 | 0 |
| trueSilenceNoSilenceFixtures | 0 | 0 | 0 |
| trueSilenceCorpusDefectFixtures | 2 | 2 | 0 |

SPEC 19 gate booleans (not a selection): original `{"bpm2CoreEvaluated": true, "gateAcquire95Core": false, "gateBpm2Core": true, "gateHalfDouble5Core": true}`, repaired `{"bpm2CoreEvaluated": true, "gateAcquire95Core": false, "gateBpm2Core": true, "gateHalfDouble5Core": true}`.

Raw result hashes:

- `raw/original/btrack/block128/results.json` ab17c7ba9a23469f5f2323e41a8089c4cad7ddfb08a8091c2856a3bbe0872f33
- `raw/repaired/btrack/block128/results.json` 2e4cebf9028f8a1173b6c18d53ea44e07dfe988e6af2d839e286c7df75fe6c54
- `raw/original/aubio/block128/results.json` 0f2bd13b3c5881ff9a7fe52bb59dc04ba162f9d22c3a60cdacc37ad642e45c56
- `raw/repaired/aubio/block128/results.json` deb430e59710263b8016b5fbd4d17dad78785e90337f5b90d67a5727cc7c4e91

The shared CLI still classifies `sustained_chords` as `CorpusDefect` by fixture name. That classification is a harness hard-code outside this task's ownership; the repaired fixture's silence is measured independently and is genuine (see `docs/research/SUSTAIN-REPAIR.md`).
