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
