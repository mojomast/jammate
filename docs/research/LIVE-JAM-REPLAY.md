# LIVE-JAM-REPLAY — actual-processor first-live-Jam replay harness + evidence validator

**Task:** EVAL-LIVE-001 (actual-processor first live Jam replay runner + evidence
validator with a predeclared test matrix)
**Worktree:** `/home/mojo/projects/worktrees/EVAL-LIVE-001-replay`
**Branch:** `wp/EVAL-LIVE-001-replay`
**Base:** `88893e24be328f131b5df673078ff934a46ed5ab` (contract freeze of the live
Jam command/telemetry facade)
**Task note:** `task-notes/EVAL-LIVE-001.md`
**Tooling:** `tools/live-jam-replay/**` (new)
**Test:** `tests/LiveJamProcessorTests.cpp` (new; not wired into shared CMake)
**Artifacts:** `docs/research/live-jam-replay/**`

## Status of this handoff

This worktree delivers the **replay harness, the frozen predeclared conditions
matrix, the evidence validator and their executable self-tests**. The
**actual-processor live measurement is not performed here**, because this base's
product shared archive predates the merged live pipeline and does not define the
frozen facade. The runner detects that, writes an `awaiting-product` receipt and
exits 3 **without invoking any binary**. Scope: **harness ready, awaiting actual
product.**

What *is* executed and committed here:

| Artifact | Result |
|---|---|
| Instrumentation self-check (`instrument_selfcheck`) | **PASS** (gate + exact detectors + authoritative nothrow/aligned folding) |
| Replay support self-test (`replay_support_selftest`) | **PASS** (JSON serializer null/NaN policy; 16-bit WAV round-trip; fail-closed on 8-bit/truncated) |
| Frozen facade / live-session contract test (`live_jam_facade_tests`, real `jam-core`) | **PASS** |
| Harness smoke compile against the frozen headers (`-c`) | **PASS** |
| Validator unit tests (`test_validate_evidence.py`) | **41/41 PASS** |
| Validator over a full synthetic evidence tree | **PASS** (0 hard failures) |
| Preflight against the current non-live product | **FAIL-CLOSED**: `awaiting-product` |

## Why no stub processor

`LIVE-JAM-CONTRACT.md` requires the replay to take an explicit freshly built
product/shared archive and to verify hashes; it must not substitute a stub
processor for actual callback evidence. No processor, editor, engine or frozen
interface source is modified and **no stub processor is ever created**. The
harness compiles against the frozen declarations and links only against a real
product archive that actually defines the facade; the runner refuses otherwise.

## Thread and time model of the replay

- One replay thread calls the real `prepareToPlay` / `processBlock` /
  `releaseResources` and submits commands through the frozen
  `submitJamCommand`. This is a **non-device** replay: the callback and command
  submission share one thread, so cross-thread races are not exercised (same
  limitation as the RT-002 probe).
- The processor-owned analyzer worker is separate and receives wall-clock time
  between callbacks: **enabled** cells are real-time paced with a `sleep_until`
  deadline *outside* the armed callback. No sleep ever occurs inside the
  callback.
- `enabled_pressure` is the intentional offline fast-loop starvation cell: it
  runs without pacing to expose queue pressure/drops, and it never claims worker
  or clock receipt. It is deliberately separate from the paced cells.
- `disabled` is the control condition: the pipeline is not started, but the
  processor may still service an attached bridge, so the control **does not
  claim a zero-overhead baseline**; the reported contrast is "pipeline disabled
  vs enabled" at the same block/rate/input.

## Frozen facade and semantics exercised

The replay reads only `JamLiveState` through `readJamLiveState` (single consumer,
read after each callback) and writes only through `submitJamCommand`. It never
reads plain engine/clock/analyzer state. The commands exercised: `Start`,
`TapTempo`, `ResyncNextBar`, `StopAtNextBar`, `Stop`, `Reset`, plus a dedicated
semantics sequence (`Start` → stop-at-next-bar → `Start` → immediate `Stop` →
release → re-prepare) that distinguishes next-bar stop from immediate stop and
observes the session generation across re-prepare.

Two semantics were verified against the **actual** engines and corrected during
development (see the task note):

1. **Silence does not produce a zero BPM.** The shipped `MusicalClock` publishes
   its configured `fallbackBpm` (100.0) while no belief exists. That is an
   explicit fallback, not tracker-derived tempo. The honest no-fabrication
   signal is `lockState == Acquiring` and `confidence01 == 0`, and the director
   never joins from silence. The predeclared truth expectation and the validator
   check these, not a zero BPM.
2. **`StopAtNextBar` is a pending commitment, not an immediate stop.** The
   `DrumClockBridge` keeps playing until the bar boundary; the frozen `Stop`
   semantics are left unspecified by the interface, so the harness records both
   paths rather than assuming one.

## Predeclared conditions matrix (frozen before any measurement)

`tools/live-jam-replay/predeclared.json` freezes, before any inference:

- rates `{48000, 96000}`; blocks `{128, 512, 4096}` (4096 is above the 2048
  analysis tap ceiling, so it exercises oversized-callback chunking);
- pipelines `{disabled, enabled, enabled_pressure}`; inputs
  `{clean, noise, silence}` — 54 cells;
- pacing rule (`enabled` realtime, others fast), warm-block rule, smoke subset,
  realtime budget;
- callback allocation/free/lock metric definitions, including the authoritative
  C++/C totals that fold the nothrow/aligned `new` forms (RT-005 lesson);
- audio-clock semantics rules: exact one-block advance per callback including
  while stopped; `lastReceiptSampleTime >= lastInputHorizonSampleTime >=
  lastEventSampleTime`; block-resolution receipt only, no sub-block or device
  latency claim;
- fixture manifest schema, sample-exact checksum, and the "generated outside
  git, never guitar recordings" rule;
- fail-closed requirements (live definitions, archive symbols, backend signals,
  archive/source identity, product-wrong-source detection);
- in-scope and out-of-scope verification claims.

## Synthetic fixtures (not guitar recordings)

`tools/live-jam-replay/make_synthetic_evidence.py` generates deterministic
16-bit PCM WAVs (`strum_120`, `click_120`, `noise`, `silence`) **outside git**
and a `manifest.json` carrying each file's `sha256` and a `sample_exact_checksum`
(the sha256 of the raw little-endian int16 stream, independent of container
metadata). The harness can consume them through an **explicit dictionary**
(`--fixture-clean/--fixture-noise/--fixture-silence`, supplied by the runner),
never by assuming an injection API. The built-in deterministic generators are
used only when no fixture path is given; each cell records `input_source`.

## Evidence schema and validator

`tools/live-jam-replay/validate_evidence.py` is standalone and portable. It
validates the **recorded** evidence (pins, counters, semantics, exact matrix,
fixture identity) with no local hard paths and no binary artifacts. Optional
`--source` / `--predeclared` / `--fixtures-dir` add explicit local cross-checks;
without them the recorded values are still cross-checked against each other.

Hard checks include: exact 54-cell matrix; authoritative counter consistency;
zero overflow (dropped detail evidence is never clean); audio monotonicity and
exact one-block advance; receipt/horizon/event ordering; silence no-lock and
no-confidence; control cells not reporting running/playing; finite measured
fields with explicit `null` only for unmeasured values; fixture hashes and
sample-exact checksums; product/source identity; and instrument self-check.

The validator also accepts `awaiting-product` / `awaiting-backend` receipts and
fails closed unless they name the missing items, explicitly do not claim clean,
and did not invoke a binary. `test_validate_evidence.py` proves 41 adversarial
mutations fail closed (NaN/Infinity, missing/extra cells, counter inconsistency,
overflow, timestamp-order violations, silence lock/confidence, control-cell
state, identity gaps, fixture mismatches, status/shape violations).

## Reproduce

```sh
cd /home/mojo/projects/worktrees/EVAL-LIVE-001-replay
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp

# Unit tests (validator, adversarial mutations)
python3 -m unittest tools/live-jam-replay/test_validate_evidence.py

# Build self-tests + smoke compile + fail-closed preflight (exits 3 until the
# live product is available)
python3 tools/live-jam-replay/build_replay.py \
  --source "$PWD" \
  --product-build /home/mojo/projects/build-INT-DRUM-001-integration/product \
  --out /home/mojo/projects/build-EVAL-LIVE-001-worker/build

# Runner (refuses to overwrite a non-empty --out; never invokes a stale binary)
python3 tools/live-jam-replay/run_replay.py \
  --source <merged-pipeline-source> \
  --product-build <freshly-built-product> \
  --fixtures-dir <generated-fixtures> \
  --out <fresh-evidence-dir>
```

When the merged live pipeline exists, `run_replay.py` invokes the linked harness
and validates `evidence.json`; otherwise it writes `evidence.json` with status
`awaiting-product` and exits 3.

## Correction pass (independent BLOCK on 07021ae)

The independent review blocked `07021ae` before measurement. The additive
correction contract (`docs/research/live-jam-replay/CORRECTION-CONTRACT.md`) and
`tools/live-jam-replay/protocol-amendment.json` were committed **before** any
changed tool was reevaluated. The originals (`predeclared.json`,
`protocol.sha256`, and every pre-existing artifact under
`docs/research/live-jam-replay/`) are preserved byte-for-byte; updated pins are
recorded separately in `tool-pins-after-correction.sha256`.

What changed:

- **C1 async UI coalescing.** Per-callback advancement is now measured from the
  audio-owner plain getter `DrumEngine::injectedSamplePosition()` on the
  callback-owner thread, outside the armed region. `readJamLiveState` is treated
  as a coalescing-tolerant report (`reported_monotonic`, `reported_future`,
  `coalesced_reads`, `skipped_publications`, `worker_cursor_lag`); the expected
  cursor is never seeded from it. Pressure/unpaced cells may have no receipt,
  recorded as `receipt_measured=false` with `null` lag fields, never a measured
  zero.
- **M8 allocation scope.** Counters are the callback-thread path only; worker
  allocations are explicitly unmeasured. Callback findings are listed and the
  status becomes `measured-findings`; hidden findings fail validation.
- **M9 lag metrics.** `receipt_availability_lag = receipt − horizon`,
  `event_delay = receipt − event`, `worker_cursor_lag = produced − reported`;
  `null` exactly when unmeasured; repeated identical observations are not new
  receipts.
- **M5 smoke scope.** `--scope smoke|full|diagnostic` with the exact expected
  cell set; smoke is the 4 preregistered IDs, full requires all 54, diagnostic
  needs a reason and cannot claim the full matrix.
- **M6 timeout.** The runner enforces a preregistered bounded timeout (default
  300 s) and emits `status=timed-out` with `invoked_binary=true`,
  `measured_partial`, and preserved logs.
- **M7 synthetic rejection.** Default validation rejects synthetic evidence;
  `--allow-synthetic-selftest` accepts a clearly labelled self-test.
- **C2 backend/link.** Preflight detects the exact `JAM_LIVE_BTRACK_AVAILABLE`
  macro (not the substring `Backend`), extracts the real Standalone link closure
  (pinned by hash, grouped with `--start-group/--end-group`), and requires the
  default usable backend to be exactly `experimentalBTrack`; otherwise
  `awaiting-backend`.
- **M10 Stop contract.** `Stop`/`Reset` are the bounded next-serviced-block stop
  (`requestStopNow`) that cancels a future join; `StopAtNextBar` stays deferred.
  `--source-pin-overrides` accepts exact preregistered hashes for changed
  pipeline headers while the immutable headers must still match.
- **Supplemental scenarios.** `default_clean_long` (16 s) and
  `injected_join_stop_resync` (guarded by the pipeline
  `setJamTrackerForTesting` seam) are preregistered and recorded separately;
  the 54-cell matrix proves callback coverage only.

Post-correction self-tests: validator unit tests **67/67**, a labelled synthetic
tree passes with **3405** hard checks (0 failures) under
`--allow-synthetic-selftest` and is rejected by default, the instrumentation and
support self-tests pass, the facade tests pass, the harness smoke-compiles
(both with and without the injected seam), and preflight against the current
non-live product fails closed with `backend_macro` among the missing items.

## Second correction pass (independent BLOCK on a3ecf1f)

The second review blocked `a3ecf1f`. The additive second contract
(`docs/research/live-jam-replay/CORRECTION-CONTRACT-2.md`,
`protocol-amendment-2.json`) was committed at `549d759` **before** any
second-corrected tool was reevaluated. Originals remain byte-for-byte unchanged.

- **N1 readiness bootstrap.** The harness prepares first (proper cold publish),
  then polls `readJamLiveState` off-callback (≤2 s, 1 ms sleep) for a coherent
  prepared tag, then releases before the cells; bootstrap is outside the armed
  region and excluded from counters. The default actual backend must be exactly
  `experimentalBTrack`. Instrument/support/facade self-check failure fails
  closed in both build and runner before any measurement.
- **N2 findings set.** One finding per phase when ANY allocator/free/lock family
  occurred; free-only and lock-only runs are now consistent between harness and
  validator. Findings are a real RT gate failure (`measured-findings`), reported
  as such.
- **N3 scenario gates.** Full scope requires `default_clean_long` (actual
  experimentalBTrack + real audio-owner advancement) and
  `injected_join_stop_resync` (injectedTest + join, steps, StopNow, resync,
  reprepare, shutdown). A missing seam yields `join_proof_unavailable` (gate
  FAIL with a known reason code), never a hard pass. Smoke is partial and does
  not claim first-audible verified.
- **N4 override allowlist.** Exactly three changed compile seams may be
  overridden, pinned to their exact hashes; unknown/immutable/duplicate/
  non-preregistered/mismatch all fail closed. The orchestrator's
  `docs/research/live-jam-replay-source-pins.json` is the conforming input.
- **N5 link metadata.** Complete real link metadata is required;
  `missing_link_metadata_tool` fails closed; no static-list guess.
- **N6 reported cursor start.** Recorded via a tested helper before the
  have-flag.
- **N7 timeout partial.** Truthful `measured_partial=false` with counters
  unmeasured is accepted; `true` requires a preserved parsed cells hash and log
  hash; deadline ≤300 s.
- **N8 audio-owner gate.** Every measured cell must show `audio_owner_start ==
  block`, `audio_owner_end == (warm_blocks+1)*block`, a real positive advance,
  no mismatches/backwards; `immutable_pins_ok` must be true.

Gates: `structural` AND `rt_gate` (no findings) AND `join_gate` (full scope
requires the injected join scenario). The CLI passes only when all three pass.

Post-second-correction self-tests: validator unit tests **96/96**; labelled
synthetic full tree passes with all three gates; smoke is structurally valid but
does not pass the join gate; the override allowlist accepts the orchestrator's
conforming file and rejects unknown/immutable/duplicate/non-preregistered
paths; link-metadata unavailability fails closed; the harness smoke-compiles
both with and without the injected seam.

## Narrow third correction (independent BLOCK on 852ddb6)

One outcome-recording bug: `runDefaultCleanLong` assigned `backend_kind` only
inside `!joinObserved && drumsPlaying`, so a legitimate 16 s no-lock default run
wrote an empty `backend_kind` and failed identity falsely. Fixed: the actual
backend is captured from the first coherent prepared state (and each successful
read) independent of join, via the pure `LiveJamObserved.h` helper; an
unavailable/unknown backend is recorded as-is (never blind-copied), a mid-session
backend change is a fail-closed discrepancy, and the wrong identity is never
overwritten by an eventual experimental value. `joinObserved`/`steps_fired`/audio
metrics remain separate measured outcomes; a default no-lock run is a diagnostic
quality outcome recorded exactly (the injected scenario is the first-slice join
proof; no ≥95% claim). Also `--timeout-s` now rejects non-finite/≤0/>300 before
any subprocess (exit 64).

Post-fix: validator unit tests **103/103**; labelled synthetic full tree passes
all gates; default no-join is diagnostic; backend-change fails closed; timeout
input bounded. **Link-only** verification against the actual integrated product
(`/home/mojo/projects/build-INT-LIVE-001-integration/product`, source
`product-source`, orchestrator's conforming override file) is **LIVE-READY**:
instrument/support/facade self-checks pass, injection seam detected, and the
harness linked with the full real closure
(`SharedCode`, `nam`, `Assets`, `jam-btrack`, `jam-core`, `btrack`, `kiss_fft`,
`samplerate`). The runtime was **not** invoked; no measurement is claimed
(`link-receipt-actual.json`).

## Limitations (not claimed)

- Not a whole-program allocation-safety proof. It is a bounded matrix over the
  exercised callback path.
- Non-device, effectively single-threaded callback: not latency, dropout, device
  or Windows/ASIO evidence, and it does not establish the >=95% within-two-bar
  useful-lock target.
- Callback overhead is instrumented **wall** time (hook overhead included), not
  CPU time and not a deadline.
- Synthetic strum/click/noise fixtures are not guitar recordings and do not
  establish real-guitar tracker quality.
- The actual live measurement is pending the merged pipeline; this handoff only
  proves the harness, the protocol freeze and the validator.
