# DEVICE-002 — physical interface, latency and play-session evidence tooling

## Goal

Give the adaptive wave a usable, fail-closed way to collect and validate the
physical-interface evidence the SPEC keeps separate from software tests:
loop-back round-trip/monitoring latency, explicit play-trial evidence, and the
SPEC 21.5 hardware matrix. The tooling must never fabricate a human or hardware
run, must keep source/config/interface/driver/rate/block identity and raw hashes,
and must distinguish a measured physical delay from a callback estimate.

## Base and scope

- Base `984ad1d` (`docs: freeze adaptive Flash wave ownership and integration
  seam`); branch `wp/DEVICE-002`; worktree
  `/home/mojo/projects/worktrees/DEVICE-002`.
- Owned and added only:

  ```
  tools/device-validation/**            (new, stdlib only)
  docs/research/device-validation/**    (new)
  task-notes/DEVICE-002.md              (new)
  ```

- Not touched: shared CMake/CI, `EXECUTION-LEDGER.md`, `HANDOFF.md`,
  `DEVPLAN.md`, `SPEC.md`, any product source, any earlier evidence, any
  tracker/corpus. No build was performed (Python-only tests; disk ~0.5 MB).

## What was built

- `init_session.py` — one command writes `session.json` (product/build/interface/
  driver/rate/block identity, empty measurement list) plus `raw/` and
  `measurements/`; refuses a non-empty dir, never marks anything measured.
- `ingest_latency.py` — reads actual WAV bytes and estimates physical round-trip
  latency with a bounded normalised matched filter (preferred, two channels in
  one file, cancels recording-start offset) or a single-channel threshold onset.
  Records explicit rejection reasons for silence, ambiguity, non-finite samples,
  clipping, rate/channel mismatch and interface/condition mismatch.
- `ingest_play_trial.py` — settings, timing, useful-lock, dropouts, callback and
  the six SPEC 20 judgements. A numeric is marked `measured` only with a hashed
  raw receipt; `useful-lock yes` requires a measured two-bar window and
  time-to-lock; judgements are always `operator-report`.
- `ingest_functional.py` — disconnect/reconnect, input channel change, silent and
  clipped input; a raw receipt is mandatory.
- `validate_evidence.py` — re-derives every latency from the WAV, re-hashes every
  raw file and receipt, cross-checks record identity against the session,
  enforces the non-empty SPEC 21.5 matrix, Windows-ASIO backend+driver+physical
  measurement, the 12 ms ASIO 48k/128 target and all six play trials, and
  excludes `synthetic: true` from every physical gate.
- `make_synthetic_evidence.py` — labelled synthetic session for tooling tests.
- `probe_environment.py` — read-only availability probe.
- `schema/*.json`, `README.md`, and a 45-test stdlib suite.

## Contract decisions

- **Physical vs callback.** `analysis.physical_roundtrip_latency_ms` exists only
  when a clean recording and an unambiguous estimator produce it; callback p50/
  p99 live only in the play-trial `callback` block. A callback estimate can never
  satisfy the monitoring-latency gate.
- **No arbitrary assertions.** Any measured number without its raw receipt, any
  unmeasured field carrying a value, and any edited latency/valid/reasons/
  target_met value are hard validator failures. Identical interface-consistency
  logic is shared by ingest and validator so records reproduce exactly.
- **Synthetic.** Synthetic self-tests are accepted only with
  `--allow-synthetic-selftest`, are reported under `synthetic_selftest`, and are
  excluded from `hardware_matrix` and `play_trials`; a fully well-formed
  synthetic matrix still fails every physical gate.
- Exit codes: 0 gate pass, 1 hard failure, 2 no hard failure but empty/
  incomplete/failing matrix.

## Exact commands executed

```sh
# compile check
python3 -m py_compile tools/device-validation/*.py tools/device-validation/tests/*.py

# full suite (stdlib, Python 3.13.5, no third-party installs)
python3 -m unittest discover -s tools/device-validation/tests -v
# -> Ran 45 tests ... OK

# labelled synthetic example (committed)
python3 tools/device-validation/make_synthetic_evidence.py \
  --out docs/research/device-validation/example/synthetic-session
python3 tools/device-validation/validate_evidence.py \
  --session docs/research/device-validation/example/synthetic-session \
  --allow-synthetic-selftest --gate all \
  --out docs/research/device-validation/example/synthetic-session/report.json \
  --summary-md docs/research/device-validation/example/synthetic-session/summary.md
# -> exit 2

# read-only environment probe (no installs)
python3 tools/device-validation/probe_environment.py \
  --out docs/research/device-validation/environment.json
```

## Example diagnostic results (synthetic, not hardware)

```
make_synthetic_evidence: clean latency recovered=2.0 ms expected=2.0 ms
latency-asio_48k_128       valid=True  lat=2.0   reasons=(none)
latency-asio_48k_64        valid=False reasons=ambiguous-onset,interface-block-mismatch
latency-asio_48k_256       valid=False reasons=silent,no-correlation-peak,interface-block-mismatch
latency-wasapi_low_latency valid=False reasons=clipped-samples,interface-backend-mismatch
latency-nonfinite          valid=False reasons=nonfinite-samples,no-correlation-peak
latency-ratemismatch       valid=False reasons=sample-rate-mismatch

validate --allow-synthetic-selftest --gate all:
  hard_errors=0  matrix_empty=true
  hardware_matrix=FAIL  monitoring_latency=FAIL  play_trials=FAIL
  synthetic_selftest=PASS  overall_pass=false  exit=2   (11 synthetic records)
```

Unit-test coverage of the failure modes: recovery within tolerance of a known
lag; ambiguous pair; silence; clipping; non-finite float; sample-rate mismatch;
ASIO missing driver; over-target 13 ms latency failing the monitoring gate;
edited latency value; tampered raw bytes; interface identity mismatch; play
`unmeasured-value`; `useful-lock yes` without a measured window; empty matrix
never passing; synthetic matrix never passing a physical gate.

## Local availability (factual)

`docs/research/device-validation/environment.json` records: Linux
6.12.105, Python 3.13.5, `dev_snd_present=false`, `proc_asound_cards=null`,
`pipewire_socket=false`, and only `ffmpeg`/`ffplay` present among the probed
capture programs (`arecord`, `aplay`, `pw-record`, `pw-cli`, `pactl`, `sox`,
`jackd` all absent). No audio interface, ASIO driver or Windows host exists
here, so **no physical latency, play-trial or ASIO evidence was produced**. No
packages were installed.

## Outstanding physical evidence (requires an interface)

1. SPEC 21.5 latency cells on the reference Windows ASIO interface at 48 kHz /
   64, 128 and 256 frames and a supported WASAPI low-latency configuration.
2. SPEC 18.3 `<= 12 ms` end-to-end monitoring measurement (the `asio_48k_128`
   cell) from a real loop-back recording.
3. The four edge conditions with raw receipts.
4. The six SPEC 20 play trials on real guitar with a raw diagnostics trace
   backing timing, lock, dropout and callback numbers.
5. A real WASAPI reference so the Windows matrix is complete.

The recipes (Linux `arecord`/`pw-record`/`ffmpeg`; Windows Standalone, ASIO and
DShow) and the direct-monitoring/calibration caveats are in
`tools/device-validation/README.md`.

## Executed vs not

Executed: tool + schema + docs implementation, compile check, 45-test suite,
synthetic example generation and validation, environment probe.

Not done (deliberately): any build; any edit to shared CMake/CI, ledger,
`HANDOFF.md`/`DEVPLAN.md`/`SPEC.md`, product source or earlier evidence; any
physical/ASIO/play claim; any package installation.

## Handoff

Authoritative artefacts: `tools/device-validation/` (tools, schemas, tests,
README), `docs/research/device-validation/README.md`,
`docs/research/device-validation/environment.json` and
`docs/research/device-validation/example/synthetic-session/`
(`report.json`, `summary.md`, raw WAVs).

## Correction round 1 — independent review of `dbd261d`

The independent review verdict was FIX (severe false passes). Protocol and
per-finding closures: `docs/research/device-validation/CORRECTION-PROTOCOL.md`.
All changes are additive; the round-1 synthetic example
(`example/synthetic-session/`) and `environment.json` are preserved byte-for-byte
and a corrected example is added (`example-corrected/`).

Closed holes (each has an adversarial test in
`tests/test_adversarial.py`, TEST-ONLY fixtures in temp dirs):

- **F1 canonical** — expected rate/channels and target are derived from the
  condition spec + session interface; contradicting `analysis.params` is a hard
  error; `asio_48k_128` is always 12 ms. A 13 ms/`target_ms=100` and a
  44.1 kHz/`expected_rate=44100` record now fail.
- **F2 typed measured** — `measured: true, value: null` is a hard error; an
  all-null play trial cannot pass.
- **F3 monitoring** — latency cells require `monitoring == software-app`; identity
  cross-check now includes input/output device and monitoring.
- **F4 parseable receipts** — numeric measured values gate only with a parseable
  `receipt/1.0` whose metric value and interface identity are re-checked;
  unparseable/zero-byte receipts are `receipt-attested` and never gate;
  functional outcomes require a parseable receipt. Judgements stay
  `operator-report`.
- **F5 crash-safe** — malformed params/analysis become hard errors and the
  validator keeps processing every record.
- **F6 worst-case** — a failing observation is never masked by a passing one;
  conflicts are reported.
- **F7 estimator** — polarity-inverted returns recover the correct lag; the
  ambiguity window is independent of the template width and the reference
  template is auto-sized; 96-vs-200 and 96-vs-1000 sample paths are rejected as
  ambiguous.

Extra rules from the review: a `synthetic` session forces every record out of
every physical gate even when the record flag is false; physical records require
a 40-hex source SHA; raw paths must be session-contained and relative; the
callback deadline gate needs a gated `p99 <= 0.70 × block` (over-target is a
failed cell, missing is unmeasured); required top-level keys, `synthetic` and
provenance are validated in code.

### Commands executed

```sh
python3 -m py_compile tools/device-validation/*.py tools/device-validation/tests/*.py
python3 -m unittest discover -s tools/device-validation/tests -v
# -> Ran 75 tests ... OK

python3 tools/device-validation/make_synthetic_evidence.py \
  --out docs/research/device-validation/example-corrected/synthetic-session
python3 tools/device-validation/validate_evidence.py \
  --session docs/research/device-validation/example-corrected/synthetic-session \
  --allow-synthetic-selftest --gate all \
  --out docs/research/device-validation/example-corrected/synthetic-session/report.json \
  --summary-md docs/research/device-validation/example-corrected/synthetic-session/summary.md
# -> hard_errors=0, matrix_empty=true, all physical gates FAIL,
#    synthetic_selftest=PASS, exit=2
```

### Correction commit SHA

- `ae8e71ca76f5d5712225f8d847d7410b771cd84f` (tools + schemas + tests + docs +
  corrected example).

## Final commit SHA

- Round 1 (implementation + tests + example + docs):
  `f9c0b3c9efd93ffe17fe440dae28bef07684c708`.
- Correction round 1 (review closures):
  `ae8e71ca76f5d5712225f8d847d7410b771cd84f`.
- The task-note commit is the branch head reported to the orchestrator.
