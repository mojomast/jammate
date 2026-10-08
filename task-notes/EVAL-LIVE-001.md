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

## Correction pass — independent BLOCK on 07021ae

The independent review blocked `07021ae` before measurement. The additive
correction contract and protocol amendment were committed first
(`a293c20`), before any changed tool was reevaluated. The original
`predeclared.json`, `protocol.sha256` and every pre-existing
`docs/research/live-jam-replay/` artifact are preserved byte-for-byte; updated
pins are in `tool-pins-after-correction.sha256` and a separate labelled
self-test receipt is in `correction-selftest-receipt.json`.

Concrete fixes:

- **C1 async UI coalescing.** The harness no longer seeds the expected cursor
  from `readJamLiveState` nor asserts exact equality on it. True per-callback
  advancement comes from the audio-owner getter
  `DrumEngine::injectedSamplePosition()` read on the callback-owner thread
  outside the armed region; the facade cursor is checked only for
  coalescing-tolerant monotonicity and never exceeding the produced cursor.
  Pressure/unpaced cells record `receipt_measured=false` with `null` lag.
- **C2 link/backend.** Preflight detects the exact `JAM_LIVE_BTRACK_AVAILABLE`
  macro (not the substring `Backend`); the build extracts the real Standalone
  link closure (pinned, `--start-group/--end-group`) and requires the default
  usable backend to be exactly `experimentalBTrack`; otherwise
  `awaiting-backend`. `setJamTrackerForTesting`/`requestStopNow` seams are
  detected and the injected scenario is compiled only when present.
- **M5 smoke scope.** `--scope smoke|full|diagnostic`; smoke is exactly the 4
  preregistered IDs, full requires 54, diagnostic needs a reason and cannot
  claim the full matrix.
- **M6 timeout.** Bounded subprocess timeout (default 300 s) producing
  `status=timed-out`, `invoked_binary=true`, `measured_partial`, logs preserved.
- **M7 synthetic rejection.** Default validation rejects synthetic evidence;
  `--allow-synthetic-selftest` accepts a labelled self-test.
- **M8 allocation scope.** Callback-thread-path only; worker allocations
  unmeasured; callback findings listed and status `measured-findings`; hidden
  findings fail.
- **M9 lag metrics.** receipt-horizon, receipt-event and produced-reported, null
  exactly when unmeasured; repeated observations are not new receipts.
- **M10 Stop contract + source pins.** `Stop`/`Reset` = bounded
  next-serviced-block stop (`requestStopNow`); `StopAtNextBar` deferred.
  `--source-pin-overrides` accepts exact preregistered hashes for changed
  pipeline headers while immutable frozen headers must still match.
- **Supplemental scenarios.** `default_clean_long` (16 s) and
  `injected_join_stop_resync` (guarded) preregistered and recorded separately;
  the 54-cell matrix proves callback coverage only.

Post-correction executed self-tests: validator unit tests **67/67**; labelled
synthetic tree passes **3405** hard checks (0 failures) under
`--allow-synthetic-selftest` and is rejected by default; instrumentation and
support self-tests pass; facade tests pass; harness smoke-compiles both with and
without the injected seam; preflight against the current non-live product fails
closed with `backend_macro` among the missing items.

Counts are stated explicitly per run; the earlier "41" is historical and not
reused.

## Second correction pass — independent BLOCK on a3ecf1f

The second review blocked `a3ecf1f`; the additive second contract was committed
at `549d759` before any second-corrected tool was reevaluated. Originals remain
byte-for-byte unchanged; new pins and a fresh labelled receipt are added
separately.

- **N1** readiness bootstrap: prepare first, bounded off-callback poll for a
  coherent prepared tag, release before cells; bootstrap excluded from counters;
  exact `experimentalBTrack` required; self-check failure fails closed before any
  measurement (build and runner).
- **N2** findings set: one finding per phase for ANY alloc/free/lock family, so
  free-only/lock-only are consistent; findings are a real RT gate failure.
- **N3** scenario gates: full scope requires both preregistered scenarios with
  real backend/join/steps/StopNow/resync/reprepare/shutdown; absent seam is
  `join_proof_unavailable` (gate FAIL, known reason code); smoke is partial.
- **N4** override allowlist: exactly the three changed compile seams pinned to
  their exact hashes; unknown/immutable/duplicate/non-preregistered/mismatch fail
  closed. The orchestrator's `docs/research/live-jam-replay-source-pins.json`
  (whose three hashes match the pipeline source exactly) is the conforming
  input.
- **N5** link metadata required; `missing_link_metadata_tool` fails closed.
- **N6** reported-cursor start recorded before the have-flag via a tested helper.
- **N7** truthful timeout: `measured_partial=false` with counters unmeasured is
  valid; `true` requires preserved parsed cells + log hashes; deadline ≤300 s.
- **N8** audio-owner gate: `audio_owner_start == block`,
  `audio_owner_end == (warm_blocks+1)*block`, real advance, no
  mismatches/backwards, `immutable_pins_ok`.

Gates: structural AND rt_gate AND join_gate; the CLI passes only when all pass.
Post-correction: validator unit tests **96/96**; labelled synthetic full tree
passes all gates; smoke is structural-only; override allowlist and link-metadata
fail-closed tests pass; harness smoke-compiles with and without the injected
seam.

Counts are stated explicitly per run.

## Narrow third correction — independent BLOCK on 852ddb6

One outcome-recording bug: `runDefaultCleanLong` set `backend_kind` only when a
join occurred, so a legitimate no-lock default run wrote an empty identity. Fixed
via the pure `LiveJamObserved.h` helper: capture the actual backend from the
first coherent prepared state, independent of join; record unavailable/unknown
as-is; fail closed on a mid-session backend change; never overwrite the first
identity. Join/steps/audio stay separate; default no-lock is diagnostic (the
injected scenario is the join proof; no ≥95% claim). `--timeout-s` rejects
non-finite/≤0/>300 (exit 64).

Executed: validator unit tests **103/103**; labelled synthetic full tree passes
all gates; default no-join diagnostic; backend-change fail-closed; timeout input
bounded. Link-only against the actual integrated product is **LIVE-READY** with
the full real closure (including `jam-btrack`/`btrack`/`kiss_fft`/`samplerate`),
all self-checks and facade tests green; the runtime was **not** invoked
(`link-receipt-actual.json`). Remaining: the actual runtime gates
(full matrix, `default_clean_long`, `injected_join_stop_resync`) await the
orchestrator.

## Fourth correction — input timeline (observed before actual measurement)

`InputGen::fill` advanced the shared WAV position inside the channel loop and
ignored the WAV sample rate: stereo advanced the mono source twice per audio
frame (doubled tempo, block discontinuity) and a 48 kHz fixture played fast on a
96 kHz device. Fixed via the shared `src/ReplayInput.h` (one source position per
audio frame; `sourcePosition = deviceFrame*wavSampleRate/deviceSampleRate`;
coherent mono replication; advance `N` once; absolute device-frame clock;
off-callback resampling; true 120 BPM builtin clean in device time; LCG once per
frame for noise; invalid rates rejected). Cells/scenarios declare
`input_signal_kind`/`input_source_rate`/`device_rate`/`channel_mapping`; the
validator requires `device_rate == rate`. `make_synthetic_evidence.py` WAV bytes
are unchanged.

Executed: validator unit tests **107/107**; pure `ReplayInput` tests (ramp
fs=4/device=8 wrapped sequence; mono/stereo phase; advance N not 2N; 48→96 step
0.5; 44.1→48; repeated wrap; fs=48 beat at device frame 24000/48000; chunk
invariance; builtin clean absolute-time phase; deterministic noise; zero-rate
rejection). Link-only against the actual integrated product is **LIVE-READY**
with the full closure and all self-checks; runtime **not** invoked
(`link-receipt-actual-4.json`). Remaining: the actual runtime gates await the
orchestrator.

## Fifth correction — actual-full-001 defects (A state latch, B injected proof)

The first actual full run (preserved read-only at
`/home/mojo/projects/build-EVAL-LIVE-001-integration/actual-full-001`) showed 108
validator failures: all 54 cells `state_end.prepared=false`/`sampleRate=0`, and
`join_gate=false`; RT gate passed on all 54 cells and the default-long gate
passed (BTrack join, steps). The evidence was **not** edited; the corrected
validator still reports actual-001 as a truthful failure
(`actual-001-recheck.json`, evidence sha `284d265b…`).

- **Defect A**: the final `readJamLiveState` result was used even when false.
  Fixed with the `StateLatch` helper (baseline prepared poll pre-callback;
  update only on true; never reset on false; `baseline_prepared` required).
- **Defect B**: the injected scenario was unpaced, so it never joined and its
  StopNow/resync/reprepare proofs were vacuous. Fixed with real-time pacing,
  first+second actual join (engine playing + steps), deferred StopAtNextBar,
  serviced-block StopNow, real resync effect, session-generation change and a
  coherent released payload. Engine steps/playing carry the proof, not RMS.

Executed: validator unit tests **113/113**; pure `StateLatch` tests; labelled
synthetic full tree passes all gates; link-only against the actual product is
LIVE-READY (runtime not invoked). Remaining: a truthful actual-002 run by the
orchestrator.

## Sixth correction — real resync phase proof

The injected `resync_effect_observed` was a false positive (ordinary join passed
as resync). Fixed with an engine-phase proof: third actual join without resync,
wait for baseline `injectedNextStep in [2,14]`, submit `ResyncNextBar` alone,
assert `injectedNextStep()==1`, `lastStepSample in [submitCursor, observedEnd)`
and a positive command-count delta. The injected gate requires the recorded phase
fields; a forged flag cannot pass. Default-long gate is identity + actual
audio-owner only.

Executed: validator unit tests **121/121**; link-only against the actual product
LIVE-READY (runtime not invoked); preserved actual-001 still fails truthfully.

## Seventh correction — drum-only zero-input window

Added a declared zero-input window after the resync proof (silence written
exact-zero by the caller; 0.5 s wash + 1.0 s measured) that records the real
processor output RMS/peak/nonzero blocks, steps delta, engine playing,
`samplesLoaded()==true`, `useVst==false` and `allocator_coverage=unmeasured`.
The injected gate requires the window proof. INJECTED CONTRACT evidence, not a
real-guitar/physical claim. WAV bytes and source phase metadata unchanged.

Executed: validator unit tests **127/127**; link-only against the actual product
LIVE-READY (runtime not invoked); preserved actual-001 still fails truthfully.

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
