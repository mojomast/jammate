# TRACK-006 evidence

Analysis and exact reproduction: [TEMPO-VARIANT-ROBUSTNESS.md](../TEMPO-VARIANT-ROBUSTNESS.md).

- `raw/{btrack,btrack-tempo-variant,aubio}/block128/`: fresh current CLI JSON,
  CSV, per-fixture files, Markdown and command output; authoritative scores.
- `diagnostic/<backend>/`: pinned auxiliary beat-series run, including exact
  timestamp CSVs. Its historical score/coverage output is not current scoring.
- `beat-equality.json`: all 24 byte-identical default/variant beat pairs and hashes.
- `raw-method/instance_0.csv.gz`: lossless current scorer-process per-block method
  log; resets once per manifest fixture, split only after block validation.
- `method-beats.csv`, `method-summary.json`: readiness, fallback, interval state,
  stale-ready counts and event/availability clocks; no invented readiness in BPM.
- `degradation.{json,csv}`: unchanged EVAL-005 paired extraction semantics, 3240
  rows; missing stays missing and inherited noise silence remains unassessed.
- `comparison.json`: default/variant differences, actual regression flags and
  three-way measured per-clip comparison.
- `provenance.json`: executed commands, actual verified pins, source/archive
  hashes, input bytes/WAV framing, fixed-method freeze and historical cross-check.
- `artifact-hashes.txt`: hashes of retained evidence files (excluding itself).

The unchanged manifest has 24 total clips, including two exact 5-second parent
baselines and 22 perturbations. This is diagnostic-only; G3 OPEN, no selection.
