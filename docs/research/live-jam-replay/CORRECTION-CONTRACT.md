# EVAL-LIVE-001 correction contract (additive, frozen before reevaluation)

**Task:** EVAL-LIVE-001 · **Base:** `88893e24be328f131b5df673078ff934a46ed5ab`
**Superseded handoff:** `07021ae` (BLOCKed before actual measurement)
**Amendment:** `tools/live-jam-replay/protocol-amendment.json`

This document and the amendment are committed **before** any changed tool is
reevaluated or measured. They are additive: the original
`tools/live-jam-replay/predeclared.json`, `docs/research/live-jam-replay/protocol.sha256`
and every existing artifact under `docs/research/live-jam-replay/` are preserved
byte-for-byte. Updated tool pins are recorded separately after the fix.

The corrections below are **producer-semantics fixes**, not relaxations of any
callback counter.

## C1 — asynchronous UI coalescing (blocking)

`readJamLiveState` is a worker-owned coherent latest-value slot: a failed or
unchanged read leaves the caller's previous whole state, so it is **not** a
synchronous per-callback cursor. The harness must not seed the expected cursor
from it and must not assert `state.audioSampleTime == expected + block`.

- True per-callback advancement is measured from the **audio-owner plain engine
  getter** `DrumEngine::injectedSamplePosition()`, read on the callback-owner
  thread immediately after `processBlock`, outside the instrumented region.
  `injSample_` advances by exactly one block every callback while the bridge is
  attached, whether or not the injected transport is playing.
- The facade cursor is checked only for **coalescing-tolerant monotonicity** and
  must never exceed the produced audio-owner cursor.
- Distinct metrics: `audio_owner_delta_mismatches`, `audio_owner_backward`,
  `reported_monotonic`, `reported_future`, `skipped_publications`,
  `coalesced_reads`, `worker_cursor_lag`.
- For unpaced/pressure cells the snapshot may legitimately be unchanged and no
  receipt may exist: recorded as `receipt_measured=false` and `null` lag fields,
  never a measured zero.

## C2 — link closure and backend identity (blocking)

- Link the actual default BTrack dependency closure from the target's real link
  metadata, pinned by hash, with explicit nam-core/jam/jam-btrack/btrack/kiss_fft/
  samplerate archives and `--start-group/--end-group`; no whole-archive-everything
  and no unnecessary third-party global `new` collisions.
- Preflight detects the exact `JAM_LIVE_BTRACK_AVAILABLE` define/macro, not the
  substring `Backend`. The default usable backend must be exactly
  `experimentalBTrack`; `injectedTest` is only accepted in an explicit injection
  case. An invalid/unavailable backend yields `awaiting-backend`, never a
  measured-clean claim.

## M5 — smoke scope

Smoke mode generates exactly the four preregistered IDs and declares
`scope=smoke` with its exact expected list. The validator enforces the subset
exactly (missing/extra fail); the full matrix requires all 54. Filtered runs
declare `scope=diagnostic` and never claim the full 54.

## M6 — bounded timeout

The runner enforces a preregistered bounded overall timeout (realtime 180 s +
120 s overhead, hard cap 300 s). A timeout yields `status=timed-out`,
`invoked_binary=true`, `measured_partial=true`, preserves logs/counters, and
terminates the child so no worker/backend destructor hangs unbounded.

## M7 — synthetic evidence

Default validation rejects `synthetic:true`. Synthetic trees are accepted only
under the explicit `--allow-synthetic-selftest` flag and are labelled
unmistakably as a synthetic self-test, never an actual clean run.

## M8 — allocation scope

Counters are the **callback-thread path only** (thread-local arming). Worker
allocations are explicitly unmeasured (`null`); no pipeline-wide zero is claimed.
Legitimate cold-callback allocations are a positive finding with status
`measured-findings`, never hidden and never a measurement failure.

## M9 — lag metrics

- `receipt_availability_lag = lastReceiptSampleTime − lastInputHorizonSampleTime`
  (exact uint64; negative/order violation rejected).
- `event_delay = lastReceiptSampleTime − lastEventSampleTime`.
- `worker_cursor_lag = produced_audio_cursor − reported_cursor`.
- All `null` when no receipt is measured; a truly measured same-block zero is
  valid, not unknown. Repeated identical observations are not new receipts
  (keyed by generation + event + horizon + receipt).

## M10 — Stop contract and source pins

**Decision:** `Stop`/`Reset` are a bounded stop at the next **serviced** audio
block that cancels any future join and restores manual drum availability
(`bridge.requestStopNow()`). `StopAtNextBar` remains a distinct deferred
next-bar stop (`bridge.requestStopAtNextBar()`). The harness must not assert
immediate stop until actual audio-owner echo events verify it.

The pipeline's new bounded bridge/engine API changes
`src/jam/DrumClockBridge.h` and `src/PluginProcessor.h` hashes. A preregistered
`--source-pin-overrides` JSON may supply exact new hashes for named changed files
only; the immutable headers (`JamLiveInterface.h`, `RhythmTypes.h`,
`MusicalClock.h`, `JamConfig.h`) must still match their original hashes. An
arbitrary unknown source may never skip the original pin.

## Count correction and supplemental scenarios

- Test count is stated explicitly per run (old claim 41; orchestrator independent
  42). Historical counts are not reused.
- The 54-cell matrix proves callback coverage only. Supplemental preregistered
  scenarios (`default_clean_long` 16 s; `injected_join_stop_resync` via the
  pipeline `setJamTrackerForTesting` seam) prove join/stop/resync/reprepare/
  shutdown. If the injection seam is absent the scenario is recorded unmeasured;
  the docs will not claim end-to-end join from the matrix alone.

## Scope

Only new files under `tools/live-jam-replay/**`, `tests/LiveJamProcessorTests.cpp`,
`docs/research/LIVE-JAM-REPLAY*.md`, `docs/research/live-jam-replay/**` and
`task-notes/EVAL-LIVE-001.md` change. No processor, editor, `JamLiveInterface`,
engine, core, shared CMake/CI or ledger source is modified. No agents.
