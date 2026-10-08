# EVAL-007 scored-metric comparison to prior artifacts

- Base commit: `664041c2c40bc5f47e6b4c8491462bc7b6718cc6`
- New CLI sha256: `464488226991a30ab92685ff43d9b83429b8b8c661076c35088a5ea1389ec6b0`
- Re-hashed all 62 declared manifest WAV entries against bytes: **0 mismatches**.

All non-coverage scored metrics (detection, acquisition, BPM, half/double, phase, ramp, syncopation, silence-acceleration proxy, timing diagnostics, allocation counts) are byte-identical to the prior artifacts on each respective input corpus. Only `falseBeatCoverage` / `falseBeatMetricInformative` / the new identity and citation fields / aggregate coverage counters differ, plus wall-clock `cpuSeconds`.

| corpus | backend | fixtures | noncoverage diffs | coverage changes |
|---|---|---:|---:|---:|
| original | btrack | 19 | 0 | 1 |
| original | aubio | 19 | 0 | 1 |
| repaired | btrack | 19 | 0 | 2 |
| repaired | aubio | 19 | 0 | 2 |
| derived | btrack | 24 | 0 | 5 |
| derived | aubio | 24 | 0 | 5 |

## Gate state (unchanged, no tracker selected)

| corpus | backend | acq within 2 bars | gate 95% | BPM worst core | gate BPM 2% | F mean | gate h/d 5% |
|---|---|---:|---|---:|---|---:|---|
| original | btrack | 4/11 | False | 0.0234 | False | 0.7099 | True |
| original | aubio | 7/11 | False | 0.0133 | True | 0.5357 | True |
| repaired | btrack | 5/11 | False | 0.0234 | False | 0.7248 | True |
| repaired | aubio | 7/11 | False | 0.0134 | True | 0.5464 | True |
| derived | btrack | 0/0 | False | 0.0000 | False | 0.6253 | False |
| derived | aubio | 0/0 | False | 0.0000 | False | 0.5389 | False |

## Coverage changes (the correction's only scored-output change)

- `original/btrack` `tapping_muting_only`: `CorpusDefect` → `Measured`
- `original/aubio` `tapping_muting_only`: `CorpusDefect` → `Measured`
- `repaired/btrack` `sustained_chords`: `CorpusDefect` → `Measured`
- `repaired/btrack` `tapping_muting_only`: `CorpusDefect` → `Measured`
- `repaired/aubio` `sustained_chords`: `CorpusDefect` → `Measured`
- `repaired/aubio` `tapping_muting_only`: `CorpusDefect` → `Measured`
- `derived/btrack` `clean_eighths__noise_snr0db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/btrack` `clean_eighths__noise_snr10db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/btrack` `clean_eighths__noise_snr20db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/btrack` `syncopated_funk__noise_snr0db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/btrack` `syncopated_funk__noise_snr10db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/aubio` `clean_eighths__noise_snr0db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/aubio` `clean_eighths__noise_snr10db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/aubio` `clean_eighths__noise_snr20db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/aubio` `syncopated_funk__noise_snr0db`: `Measured` → `NotAssessedStructuralNoise`
- `derived/aubio` `syncopated_funk__noise_snr10db`: `Measured` → `NotAssessedStructuralNoise`
