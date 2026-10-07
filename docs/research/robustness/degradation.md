# Robustness degradation curves — EVAL-005

Both real tracker integrations (BTrack and aubio) driven over the unchanged EVAL-003 deterministic derived corpus at the primary uncompensated 128-frame setting. Every clip is paired against its declared `pairedBaseline` (same parent, same 5.0 s truncation).

This is a **diagnostic** document: no tracker is selected, no gate is tuned, and there is no rolled-up overall grade. G3 stays open.

## Provenance

- Derived manifest: `testdata/rhythm/derived/manifest.json` (sha256 `3bb6d350f534b6d6f3208ca1ef5c0f29cf7c7069b3a9771c13f191385c5cc3cd`)
- Derived corpus id: `eval003-derived-perturbations`; base manifest sha256 `06fe2c4356dd411a90b5e4948ebeb00eeb68d82f42d26b5ad25f4797c530fab1`
- `btrack`: results `docs/research/robustness/raw/btrack/block128/results.json` (sha256 `ffc227187f206883f17b45a941f6e8ea7a43aff676c10369965505fa540873cb`); blockFrames 128; variant `uncompensated`
- `aubio`: results `docs/research/robustness/raw/aubio/block128/results.json` (sha256 `dd37918020e24249ab5f9a2fec82a1cfb33601aaae01ecf5270c6e226c775eeb`); blockFrames 128; variant `uncompensated`

## Reproduction

```bash
# EVAL-005 primary run: uncompensated, 128-frame analysis block
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-EVAL-005/tmp
```

## Global caveats

- Derived clips are 5.0 s windows. They can compare a backend to its own paired baseline; they CANNOT establish the SPEC 19 absolute '>= 95 % of core fixtures acquire within 2 bars' release gate, and derived clips are not in any core denominator (they carry 'core_parent', not 'core'). Acquisition is per-clip only.
- No gate threshold is invented for detection, CPU, silence or ramp. Only the documented SPEC 19 locked-BPM <= 2 % number appears, as a labelled per-row BPM diagnostic normalisation; there is no rolled-up overall grade.
- Syncopation-stability and holdover/ramp behaviour are reported as raw backend proxies only; they are NOT clock gates offline (the Musical Clock/audition is not in this harness).

## Metadata warnings

- 5 noise clips inherit STRUCTURAL trueSilenceSpans from the parent; the added floor fills what was near-silent. Their silence metrics are marked not_assessed_noise and MUST NOT be read as SPEC 19 silence results.
- Derived clips are 5.0 s windows and carry 'core_parent', not 'core'; they are excluded from any SPEC 19 core denominator and cannot establish the absolute acquisition release gate.

## Validation

- pairs checked: 24; baselines: 2; hard errors: 0; duplicate rows: 0

## Coverage

| backend | fixtures present/total | rows measured | rows missing | proxy (not gate) | not assessed (noise) |
|---|---|---|---|---|---|
| btrack | 24/24 | 868 | 202 | 0 | 10 |
| aubio | 24/24 | 890 | 180 | 0 | 10 |

## Degradation curves

`NA` means the metric is not measured for that clip (missing lock, no phase match, no lock, or a blocked silence assessment). It is never a zero-pass.

### aubio — clean_eighths — baseline

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| window=5.0s | 0.6667 | 1 | 0.5 | 5 | yes | 1.2491 | 0.0145 | yes | 15.425 | 0 | 0.0102 | 4 |

### aubio — clean_eighths — noise

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| snrDb=0 | 0.2353 | 0.2857 | 0.2 | 7 | no | NA | 0.3678 | yes | 17.9732 | 0 | 0.0102 | 4 |
| snrDb=10 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2496 | 0.0145 | yes | 14.2583 | 0 | 0.01 | 4 |
| snrDb=20 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2492 | 0.0146 | yes | 15.1583 | 0 | 0.01 | 4 |

> caveat: trueSilenceSpans is STRUCTURAL only: an added noise floor fills what was near-silent. Silence metrics are NOT assessed from this clip; raw counts are shown uncaveated only as counts.

### aubio — clean_eighths — level

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gainDb=-60 | 0 | 1 | 0 | 0 | no | NA | NA | no | NA | 0 | 0.0098 | 4 |
| gainDb=-40 | 0.1818 | 1 | 0.1 | 1 | no | NA | 0.0145 | yes | 1.7649 | 0 | 0.0097 | 4 |
| gainDb=-20 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2491 | 0.0145 | yes | 15.425 | 0 | 0.01 | 4 |

### aubio — clean_eighths — clipping

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| thresholdFraction=0.125 | 0.6667 | 1 | 0.5 | 5 | yes | 1.4967 | 0.0135 | yes | 12.6774 | 0 | 0.01 | 4 |
| thresholdFraction=0.25 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2497 | 0.0134 | yes | 13.2292 | 0 | 0.01 | 4 |
| thresholdFraction=0.5 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2492 | 0.0141 | yes | 14.7125 | 0 | 0.01 | 4 |

### aubio — clean_eighths — onset_offset

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| offsetMs=40 | 0.6667 | 1 | 0.5 | 5 | yes | 1.25 | 0.0139 | yes | 13.0024 | 0 | 0.0103 | 4 |
| offsetMs=80 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2511 | 0.0131 | yes | 11.0607 | 0 | 0.0101 | 4 |

> caveat: Content window is shifted by a real sample delay (length preserved). Absolute detection counts are not commensurable in the same sample window.

### aubio — clean_eighths — leading_silence

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| silenceSeconds=0.5 | 0.75 | 1 | 0.6 | 6 | yes | 1.2455 | 0.0138 | yes | 18.2798 | 0 | 0.0111 | 4 |

> caveat: Content window is prepended with silence (length +0.5 s). Absolute detection counts and acquisition time are not commensurable with the baseline.

### aubio — clean_eighths — trailing_silence

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| silenceSeconds=0.5 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2491 | 0.0145 | yes | 15.425 | 0 | 0.011 | 4 |

> caveat: Duration is +0.5 s longer (silence appended). Absolute counts are not length-commensurable; detection is over a longer window.

### aubio — clean_eighths — silence_gap

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gapSeconds=1 | 0.4615 | 1 | 0.3 | 3 | no | NA | 0.0141 | yes | 22.9821 | 0 | 0.0093 | 4 |

> caveat: Edit-onsets perturbation. SPEC 19 'silence does not create false acceleration' is NOT-MEASURED offline; the tempo proxy is raw only, not a clock gate.

### aubio — clean_eighths — drop_onset

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| everyKth=2 | 0.6667 | 1 | 0.5 | 5 | yes | 1.4965 | 0.0153 | yes | 14.069 | 0 | 0.0099 | 4 |
| everyKth=4 | 0.6667 | 1 | 0.5 | 5 | yes | 1.498 | 0.0115 | yes | 9.8315 | 0 | 0.0099 | 4 |

> caveat: Edit-onsets perturbation: the metric grid is unchanged but as-played onsets are removed, so recall/F degrade against an unchanged beat truth.

### aubio — clean_eighths — syncopation_burst

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| burstCount=1 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2491 | 0.0145 | yes | 15.425 | 0 | 0.01 | 4 |
| burstCount=4 | 0.6667 | 1 | 0.5 | 5 | yes | 1.2492 | 0.014 | yes | 14.675 | 0 | 0.01 | 4 |

> caveat: Edit-onsets perturbation. SPEC 19 'no tempo jump from one isolated syncopated event' is NOT-MEASURED offline; the step proxy is raw only, not a clock gate.

### aubio — clean_eighths — tempo_step

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ratio=0.85 | 0.125 | 0.1667 | 0.1 | 6 | no | NA | NA | yes | 20.6176 | 0 | 0.0118 | 4 |
| ratio=1.25 | 0 | 0 | 0 | 3 | no | NA | NA | yes | NA | 0 | 0.009 | 4 |

> caveat: Duration differs from the paired baseline (tape-speed warp). Detection counts, acquisition and silence occupancy are NOT length-commensurable; compare tempo/phase within the clip.

### aubio — syncopated_funk — baseline

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| window=5.0s | 0.6154 | 1 | 0.4444 | 4 | no | NA | 0.008 | yes | 4.2664 | 0 | 0.0099 | 4 |

### aubio — syncopated_funk — noise

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| snrDb=0 | 0.6154 | 1 | 0.4444 | 4 | no | NA | 0.0127 | yes | 7.3363 | 0 | 0.01 | 4 |
| snrDb=10 | 0.6154 | 1 | 0.4444 | 4 | no | NA | 0.0078 | yes | 4.1726 | 0 | 0.01 | 4 |

> caveat: trueSilenceSpans is STRUCTURAL only: an added noise floor fills what was near-silent. Silence metrics are NOT assessed from this clip; raw counts are shown uncaveated only as counts.

### btrack — clean_eighths — baseline

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| window=5.0s | 0.7368 | 0.7778 | 0.7 | 9 | no | NA | 0.0234 | yes | 13.3933 | 0 | 0.0364 | 1481 |

### btrack — clean_eighths — noise

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| snrDb=0 | 0.4211 | 0.4444 | 0.4 | 9 | yes | 1.4863 | 0.0004 | yes | 14.6734 | 0 | 0.0377 | 1481 |
| snrDb=10 | 0.6316 | 0.6667 | 0.6 | 9 | no | NA | 0.0234 | yes | 11.4444 | 0 | 0.037 | 1481 |
| snrDb=20 | 0.6316 | 0.6667 | 0.6 | 9 | no | NA | 0.0234 | yes | 9.5104 | 0 | 0.0368 | 1481 |

> caveat: trueSilenceSpans is STRUCTURAL only: an added noise floor fills what was near-silent. Silence metrics are NOT assessed from this clip; raw counts are shown uncaveated only as counts.

### btrack — clean_eighths — level

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gainDb=-60 | 0 | 1 | 0 | 0 | no | NA | NA | no | NA | 0 | 0.042 | 1481 |
| gainDb=-40 | 0.8421 | 0.8889 | 0.8 | 9 | no | NA | 0.0234 | yes | 10.9487 | 0 | 0.0349 | 1481 |
| gainDb=-20 | 0.8 | 0.8 | 0.8 | 10 | no | NA | 0.0234 | yes | 15.7961 | 0 | 0.0364 | 1481 |

### btrack — clean_eighths — clipping

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| thresholdFraction=0.125 | 0.7368 | 0.7778 | 0.7 | 9 | yes | 1.2486 | 0.0004 | yes | 7.7649 | 0 | 0.0364 | 1481 |
| thresholdFraction=0.25 | 0.7368 | 0.7778 | 0.7 | 9 | yes | 1.2486 | 0.0004 | yes | 10.1293 | 0 | 0.0361 | 1481 |
| thresholdFraction=0.5 | 0.7368 | 0.7778 | 0.7 | 9 | no | NA | 0.0234 | yes | 16.7088 | 0 | 0.036 | 1481 |

### btrack — clean_eighths — onset_offset

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| offsetMs=40 | 0.9 | 0.9 | 0.9 | 10 | no | NA | 0.0234 | yes | 20.8816 | 0 | 0.0362 | 1481 |
| offsetMs=80 | 0.7368 | 0.7778 | 0.7 | 9 | no | NA | 0.0234 | yes | 7.5638 | 0 | 0.0355 | 1481 |

> caveat: Content window is shifted by a real sample delay (length preserved). Absolute detection counts are not commensurable in the same sample window.

### btrack — clean_eighths — leading_silence

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| silenceSeconds=0.5 | 0.9 | 0.9 | 0.9 | 10 | no | NA | 0.0234 | yes | 14.7688 | 0 | 0.0377 | 1524 |

> caveat: Content window is prepended with silence (length +0.5 s). Absolute detection counts and acquisition time are not commensurable with the baseline.

### btrack — clean_eighths — trailing_silence

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| silenceSeconds=0.5 | 0.7368 | 0.7778 | 0.7 | 9 | no | NA | 0.0234 | yes | 13.3933 | 0 | 0.0376 | 1524 |

> caveat: Duration is +0.5 s longer (silence appended). Absolute counts are not length-commensurable; detection is over a longer window.

### btrack — clean_eighths — silence_gap

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gapSeconds=1 | 0.5882 | 0.7143 | 0.5 | 7 | no | NA | 0.0234 | yes | 31.4762 | 0 | 0.0324 | 1481 |

> caveat: Edit-onsets perturbation. SPEC 19 'silence does not create false acceleration' is NOT-MEASURED offline; the tempo proxy is raw only, not a clock gate.

### btrack — clean_eighths — drop_onset

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| everyKth=2 | 0.4211 | 0.4444 | 0.4 | 9 | no | NA | 0.0004 | yes | 59.1935 | 0 | 0.0358 | 1481 |
| everyKth=4 | 0.7368 | 0.7778 | 0.7 | 9 | no | NA | 0.0234 | yes | 18.4366 | 0 | 0.0357 | 1481 |

> caveat: Edit-onsets perturbation: the metric grid is unchanged but as-played onsets are removed, so recall/F degrade against an unchanged beat truth.

### btrack — clean_eighths — syncopation_burst

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| burstCount=1 | 0.7368 | 0.7778 | 0.7 | 9 | no | NA | 0.0234 | yes | 13.3933 | 0 | 0.0363 | 1481 |
| burstCount=4 | 0.7368 | 0.7778 | 0.7 | 9 | no | NA | 0.0234 | yes | 13.3933 | 0 | 0.0358 | 1481 |

> caveat: Edit-onsets perturbation. SPEC 19 'no tempo jump from one isolated syncopated event' is NOT-MEASURED offline; the step proxy is raw only, not a clock gate.

### btrack — clean_eighths — tempo_step

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ratio=0.85 | 0.3 | 0.3 | 0.3 | 10 | no | NA | NA | yes | 30.5442 | 0 | 0.0402 | 1523 |
| ratio=1.25 | 0.2222 | 0.25 | 0.2 | 8 | no | NA | NA | yes | 36.9762 | 0 | 0.0319 | 1434 |

> caveat: Duration differs from the paired baseline (tape-speed warp). Detection counts, acquisition and silence occupancy are NOT length-commensurable; compare tempo/phase within the clip.

### btrack — syncopated_funk — baseline

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| window=5.0s | 0.7778 | 0.7778 | 0.7778 | 9 | yes | 1.0015 | 0.0182 | yes | 14.2024 | 0 | 0.035 | 1481 |

### btrack — syncopated_funk — noise

| param | F | P | R | pred | acq? | acqBars | BPMerr | bpmLock | phaseAbsMs | sil/s | CPU s | allocs |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| snrDb=0 | 0.4706 | 0.5 | 0.4444 | 8 | yes | 1.2453 | 0.0182 | yes | 5.8147 | 0 | 0.0369 | 1481 |
| snrDb=10 | 0.4706 | 0.5 | 0.4444 | 8 | yes | 1.2453 | 0.0182 | yes | 5.8147 | 0 | 0.0363 | 1481 |

> caveat: trueSilenceSpans is STRUCTURAL only: an added noise floor fills what was near-silent. Silence metrics are NOT assessed from this clip; raw counts are shown uncaveated only as counts.

## BPM diagnostic normalisation (documented SPEC 19 <= 2 % only)

`bpm.spec2pctNormalized` = `bpmRelativeError / 0.02` (1.0 == at the documented gate, >1 outside); `bpm.spec2pctWithin` is the boolean. Both are `NA` when there is no measured, comparable BPM lock. This is the ONLY normalisation in this file; nothing is rolled up into an overall grade.

## Files

- `degradation.csv` — tidy long rows, the authoritative raw record.
- `degradation.json` — same rows plus provenance, coverage and validation.
- `degradation.md` — this file.
