# Device-validation correction protocol (round 1)

Base: `f9c0b3c` (round-1 tooling). This correction responds to the independent
review of `dbd261d`, which found the validator could pass fabricated or
misconfigured evidence. The changes are additive; the round-1 synthetic example
under `example/synthetic-session/` is preserved byte-for-byte as history, and a
corrected example is added under `example-corrected/`.

All fixtures used by the tests are TEST-ONLY and generated in temp directories.
No physical record is fabricated anywhere.

## Findings and closures

### F1 (HIGH) — canonical thresholds

The validator no longer takes `expected_rate`, `expected_channels` or
`target_ms` from the record's own `analysis.params`.

* `device_lib.canonical_latency_expectations(condition, interface)` derives the
  expected rate from the immutable condition spec (48 kHz for every latency
  cell) and the target from the spec (`asio_48k_128` is **always 12 ms**).
* `required_channels` derives the channel count from the channel indices.
* Any contradiction between the record params and the canonical values is a
  hard error (`params-expected-rate-tampered`, `params-target-ms-tampered`,
  `params-expected-channels-tampered`), and the latency is re-derived with the
  canonical params.
* Consequence: a 13 ms `asio_48k_128` with `target_ms=100` (or `null`) cannot
  pass; a 44.1 kHz WAV declared `expected_rate=44100` against a 48 kHz interface
  cannot pass.

### F2 (HIGH) — measured values must be real

`_check_measured_field` requires a measured field to carry a finite,
correctly-typed value (`number` or `nonneg_int`). `measured: true` with
`value: null` is a hard error (`measured-value-type`). A record whose play
numbers are all null cannot pass any play gate.

### F3 (HIGH) — software monitoring and full identity

`interface_reasons` marks a latency cell invalid when
`monitoring != "software-app"` (`monitoring-not-software-app`), because a
hardware direct-monitor loop-back never exercises the application's monitoring
path. Identity cross-checking now includes `input_device`, `output_device` and
`monitoring` (plus os/backend/driver/rate/block).

### F4 (MED) — parseable receipts only

A numeric measured value is gated only when backed by a parseable
`device-validation/receipt/1.0` document whose metric value and interface
identity the validator re-checks (`receipt-not-parseable`,
`receipt-metric-missing`, `receipt-metric-mismatch`,
`receipt-identity-mismatch`, `receipt-session-mismatch`,
`receipt-interface-mismatch`). A hashed but unparseable receipt is recorded as
`receipt-attested`, is reported, and never gates. Functional outcomes require a
parseable receipt too. Human judgements remain `operator-report`.

### F5 (MED) — malformed params never crash

`device_lib.validate_latency_params` structurally validates the params block;
problems become hard errors (`analysis-params-invalid`) and derivation is
skipped. Derivation is additionally wrapped so a `KeyError`/`TypeError` from a
hostile record becomes an `analysis-error` hard error and the validator keeps
processing all remaining records.

### F6 (MED) — worst-case aggregation

`build_matrix` aggregates **all** observations per condition: any `fail` makes
the cell `fail`, then `unmeasured`, and only an all-`pass` set is `pass`.
Conflicting observations are reported under `conflicts` and in the summary, so a
20 ms regression is not masked by a 2 ms pass.

### F7 (MED) — polarity and ambiguity

* Correlation is matched on `|correlation|`, so an inverted-polarity return is
  found at the correct lag.
* The ambiguity separation (`ambiguity_sep_ms`, default 2 ms) is independent of
  the template width, and the reference template is auto-sized to the actual
  burst (not a fixed 20 ms of trailing silence), so nearby distinct paths
  (96 vs 200 samples) and far equal paths (96 vs 1000) are both rejected as
  `ambiguous-onset` instead of returning an arbitrary lag.

## Additional rules from the review

* A session marked `synthetic: true` forces every record out of every physical
  gate even when the record's own `synthetic` flag is false.
* Physical records require a 40-hex git `source_sha` (no missing/null).
* Raw paths must be session-contained and relative (no absolute, no `..`).
* The callback deadline gate requires a gated p99 measurement;
  `p99 > 0.70 × block` is a failed cell (like an over-target latency), not a
  pass. Missing gated p99/time-lock/timing/dropout metrics make a play cell
  `unmeasured`, which cannot pass a release gate.
* Required top-level keys, the `synthetic` boolean and provenance labels are
  validated in code, not only by the cosmetic JSON Schema files.

## Verification

`python3 -m unittest discover -s tools/device-validation/tests` — 75 tests,
including one adversarial repro per finding above. The corrected synthetic run
still reports `hard_errors=0`, all physical gates FAIL, `synthetic_selftest`
PASS, exit 2.
