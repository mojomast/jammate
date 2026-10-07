# Independent acoustic review of the original EVAL-001 corpus

Reviewed against the unchanged `c68df60` corpus during resumption. This is a
measurement of the PCM data, independent of the generator's decay model.
No WAVs or ground-truth arrays were changed for this review.

## Protocol

Decode mono 16-bit PCM with Python `wave`/`struct`. For an onset time `t`,
measure RMS in `[t + offset, t + offset + 0.050)` seconds, rounding endpoints
to the nearest sample. Convert normalized PCM RMS to dBFS with
`20 * log10(max(rms, 1e-12))`. No filtering, noise subtraction or onset detection
is applied. Later windows may include another event; this matters for tapping.

## Measured evidence

| sustained_chords onset (s) | offset 0 ms | 100 ms | 300 ms | 500 ms | 800 ms |
|---|---:|---:|---:|---:|---:|
| 0.345645334 | -20.73 | -21.28 | -56.00 | -63.27 | -64.39 |
| 2.855720545 | -21.75 | -19.38 | -50.85 | -63.60 | -62.71 |
| 5.341555485 | -21.36 | -17.83 | -51.99 | -63.63 | -62.02 |

The four chord events occupy an 11.35 s recording; declared true silence totals
8.734 s. The measured attack energy has collapsed by 30–35 dB after 300 ms and
approaches the capture noise floor after 500 ms. It cannot stand in for sustained
chord playing across the roughly 2.5 s event spacing. A scoped generator/audio
repair with independently checked duration is required before using it as that
scenario's acceptance evidence. Historical tracker runs remain attached to the
original corpus hashes.

| tapping_muting_only onset (s) | offset 0 ms | 100 ms | 300 ms | 500 ms | 800 ms |
|---|---:|---:|---:|---:|---:|
| 0.351095090 | -29.46 | -36.93 | -73.26 | -35.80 | -72.89 |
| 0.481400610 | -36.93 | -73.71 | -71.06 | -71.72 | -71.49 |
| 0.886531329 | -35.80 | -71.97 | -73.03 | -32.60 | -71.67 |

This recording contains 30 percussive onsets across 12.064286 s; true silence
totals 9.963 s. Brief taps and muted attacks can legitimately have high silent
occupancy, unlike sustained chords. The 500 ms energy above includes subsequent
attacks. Silent occupancy alone does not prove this fixture is defective or
justify excluding its false-beat measurements. Its rhythmic usefulness needs
an onset-spacing/energy audit, rather than an automatic 50%-silence cutoff.

## Scoring implications

Keep raw observations and event-in-silence counts for every fixture, along with
silence coverage and corpus caveats. Grid holdover during a gap is intentional
in the clock specification; event counts alone do not measure the SPEC §19
requirement that silence must not create **false acceleration**. Mostly-silent
material can be very informative about hallucinated detections, even when it is
not informative about tracking continuous playing. Corpus limitations and
metric coverage must be reported separately.
