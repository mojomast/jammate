# EVAL-LIVE-001 — actual-processor first live Jam replay harness + evidence validator

## Goal and base

Build the actual-processor replay harness and fail-closed evidence validator for
the first live Jam slice, with the conditions matrix and truth pinned **before**
any measurement, against the frozen `submitJamCommand` / `readJamLiveState`
facade and the real `GuitarCompanionProcessor::prepareToPlay` / `processBlock` /
`releaseResources`.

- Worktree: `/home/mojo/projects/worktrees/EVAL-LIVE-001-replay`
- Branch: `wp/EVAL-LIVE-001-replay`
- Base: `88893e24be328f131b5df673078ff934a46ed5ab` (contract freeze)
- Tooling: `tools/live-jam-replay/**` (new)
- Test: `tests/LiveJamProcessorTests.cpp` (new; not wired into shared CMake)
- Docs: `docs/research/LIVE-JAM-REPLAY.md`, `docs/research/live-jam-replay/**`

## Status: harness ready, awaiting actual product

The base product shared archive predates the merged live pipeline and does not
define the frozen facade (`submitJamCommand` / `readJamLiveState` absent; no
`facade_definition` in `PluginProcessor.cpp`). The runner detects this,
writes an `awaiting-product` receipt and exits 3 **without invoking any
binary**. The actual live measurement will occur once the pipeline/UI merge
lands and the orchestrator points the runner at the freshly built product and
merged source.

Executed and committed here:

| Check | Result |
|---|---|
| `instrument_selfcheck` | PASS (gate + exact detectors + nothrow/aligned authoritative folding) |
| `replay_support_selftest` | PASS (JSON null/NaN policy; WAV round-trip; 8-bit/truncated fail closed) |
| `live_jam_facade_tests` (real `jam-core`) | PASS |
| harness smoke compile (`-c`, frozen headers) | PASS |
| `test_validate_evidence.py` | 41/41 PASS |
| validator over synthetic evidence | PASS (0 hard failures) |
| preflight vs current non-live product | fail-closed `awaiting-product` |

## What was done

- `tools/live-jam-replay/predeclared.json` freezes the conditions matrix
  (rates 48/96k; blocks 128/512/4096; pipelines disabled/enabled/enabled_pressure;
  inputs clean/noise/silence; 54 cells), pacing and warm-block rules, callback
  allocation/free/lock metrics (with authoritative C++/C totals), audio-clock
  timestamp rules, fixture manifest schema and truth expectations, fail-closed
  requirements and in/out-of-scope claims.
- `src/LiveJamReplay.cpp` is the real replay: it constructs
  `GuitarCompanionProcessor`, replays the callback with the copied RT-002
  instrumentation armed only around `processBlock`, reads `JamLiveState` once
  per callback, records cold+warm C++/C alloc/free/lock counters, exact
  one-block audio-sample advance, receipt/horizon/event ordering, generation and
  drop counters, output RMS/peak, and callback wall time. Enabled cells are
  real-time paced with sleeps outside the callback; `enabled_pressure` is the
  separate offline fast-loop starvation cell; disabled is the control.
- `src/ReplaySupport.h` + `src/SupportSelfTest.cpp` extract and execute the JSON
  serializer and the 16-bit WAV reader without the processor (no stub).
- `src/InstrumentSelfCheck.cpp` links only the copied instrumentation and proves
  the gate/detectors, including nothrow/aligned folding.
- `tests/LiveJamProcessorTests.cpp` asserts the frozen POD/enum/telemetry
  contracts and, against the real `jam-core`, that silence acquires no lock or
  confidence and that `StopAtNextBar` is deferred while immediate `Stop` is not.
- `validate_evidence.py` validates the recorded evidence fail-closed and
  portable, with optional explicit local cross-checks; it also validates
  `awaiting-product` / `awaiting-backend` receipts.
- `test_validate_evidence.py` proves 41 independent adversarial mutations fail.
- `make_synthetic_evidence.py` generates deterministic synthetic fixtures
  (outside git) and a manifest with sha256 + sample-exact checksums.
- `build_replay.py` / `run_replay.py` / `replay_lib.py` implement the fail-closed
  build/preflight/run orchestration, reusing the product's archives and JUCE
  objects (no full rebuild of shared dependencies), refusing a non-empty `--out`.

## Files

- `tools/live-jam-replay/predeclared.json`
- `tools/live-jam-replay/replay_lib.py`, `build_replay.py`, `run_replay.py`
- `tools/live-jam-replay/validate_evidence.py`, `test_validate_evidence.py`,
  `make_synthetic_evidence.py`
- `tools/live-jam-replay/README.md`
- `tools/live-jam-replay/src/{LiveJamReplay.cpp,ReplaySupport.h,SupportSelfTest.cpp,InstrumentSelfCheck.cpp,RtProbeInstrumentation.h,RtProbeInstrumentation.cpp}`
- `tests/LiveJamProcessorTests.cpp`
- `docs/research/LIVE-JAM-REPLAY.md`, `docs/research/live-jam-replay/**`
- `task-notes/EVAL-LIVE-001.md`

No processor, editor, `JamLiveInterface`, scorer, drum engine, core, old test,
raw-history, shared CMake/CI or ledger file was modified. `third_party/` is
untouched. `RtProbeInstrumentation.{h,cpp}` are copied byte-identical from
`tools/processor-probe/src` (hashes in `predeclared.json`).

## Exact commands

```sh
cd /home/mojo/projects/worktrees/EVAL-LIVE-001-replay
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp

python3 -m unittest tools/live-jam-replay/test_validate_evidence.py

python3 tools/live-jam-replay/build_replay.py \
  --source "$PWD" \
  --product-build /home/mojo/projects/build-INT-DRUM-001-integration/product \
  --out /home/mojo/projects/build-EVAL-LIVE-001-worker/build
# -> self-checks PASS; preflight NOT LIVE-READY; exit 3 (awaiting product)

python3 tools/live-jam-replay/run_replay.py \
  --source "$PWD" \
  --product-build /home/mojo/projects/build-INT-DRUM-001-integration/product \
  --out /home/mojo/projects/build-EVAL-LIVE-001-worker/run-awaiting
# -> awaiting-product receipt, invoked_binary=false, exit 3

python3 tools/live-jam-replay/make_synthetic_evidence.py \
  --out /home/mojo/projects/build-EVAL-LIVE-001-worker/synth
python3 tools/live-jam-replay/validate_evidence.py \
  --evidence /home/mojo/projects/build-EVAL-LIVE-001-worker/synth/evidence.json \
  --predeclared tools/live-jam-replay/predeclared.json
# -> hard_pass=True
```

## Pins and hashes

- Base `88893e24be328f131b5df673078ff934a46ed5ab`.
- Frozen `src/jam/JamLiveInterface.h`
  `b24cf6c878e13ab2ce5fa1a3b56afeca31c14008e4e14e918528716f1cf56dc6`,
  `src/jam/RhythmTypes.h`
  `1ca042858c4c0ba9f014510212866384a09c9d304a88015dd6507e6a1f7e2b5d`.
- Copied instrumentation `RtProbeInstrumentation.h`
  `7cda7ca1d7511482f60a8e6fa6a1624714eddebbd475fc9731bf6d424da09959`,
  `RtProbeInstrumentation.cpp`
  `9472b3202ce4b1e7221bcd959c7f2c72eb547ec9c0110c2526515e8f0db07f0b`
  (byte-identical to `tools/processor-probe/src`).
- Protocol file hashes: `docs/research/live-jam-replay/protocol.sha256`.
- Self-test receipts: `docs/research/live-jam-replay/harness-ready.json`,
  `docs/research/live-jam-replay/synthetic-validation.json`.

## Failures and corrections

- **`StopAtNextBar`/silence semantics against the real clock.** The first
  contract draft and validator assumed silence yields `clock.bpm == 0`. The real
  `MusicalClock` publishes its configured `fallbackBpm` (100.0) while no belief
  exists. This is an explicit fallback, not tracker-derived tempo. The facade
  test, `predeclared.json` truth and the validator were corrected to assert
  `lockState == Acquiring` and `confidence01 == 0` (and no drums from silence),
  which is the honest no-fabrication signal. The `SetMode`-without-BPM test uses
  a zeroed `fallbackBpm` to prove no other source sets the tempo.
- **Facade test `commandQueue()` copy.** An early assertion copied the deleted
  non-copyable command queue; removed. `-Wfloat-equal` is suppressed for the
  intentional exact-zero telemetry comparisons.
- **Ninja compile-rule matching.** The product compile rule has dependencies
  after the colon, so an `endswith(':')` match failed; matching the
  `PluginProcessor.cpp.o:` object target fixed production-flag extraction.
- **Fixture id hex check.** The validator initially required fixture `id` to be
  64-hex; corrected to require only `sha256`/`sample_exact_checksum` hex and a
  non-empty `id`/`path`.

## Contract note — frozen `Stop` semantics

`JamLiveInterface.h` does not specify whether `Stop` is immediate or next-bar
while `StopAtNextBar` explicitly is next-bar. The replay therefore records both
paths rather than assuming one: the semantics sequence observes `StopAtNextBar`
as a deferred pending stop and immediate `Stop` separately, and the validator
only asserts the predeclared truth. This is recorded as an explicit,
unresolved contract question for the orchestrator; no interface change is made
here.

## Limitations (not claimed)

- The actual live measurement is pending the merged pipeline. Only the harness,
  the frozen protocol and the validator are proven here.
- Non-device, effectively single-threaded callback: not latency, dropout, device
  or Windows/ASIO evidence, and it does not establish the >=95% within-two-bar
  useful-lock target.
- Callback overhead is instrumented wall time (hook overhead included), not CPU
  time and not a deadline. `disabled` is a control, not a zero-overhead baseline.
- Synthetic strum/click/noise fixtures are not guitar recordings.
- A clean callback matrix is not a whole-program allocation-safety proof.
