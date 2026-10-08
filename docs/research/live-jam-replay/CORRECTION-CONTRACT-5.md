# EVAL-LIVE-001 fifth correction contract — coherent state latch and real injected proof

**Base:** `88893e24be328f131b5df673078ff934a46ed5ab` · **Superseded tools:** `5c0d72e`
**Amendment:** `tools/live-jam-replay/protocol-amendment-5.json`
**Preserved raw:** `/home/mojo/projects/build-EVAL-LIVE-001-integration/actual-full-001` (read-only)

Committed before the corrected harness is reevaluated. Additive: all prior
protocol/pin/receipt/contract files are preserved byte-for-byte, and the
actual-full-001 raw artifacts are **never** rewritten.

## Observed defects (actual-full-001)

The validator reported 108 hard failures: all 54 cells `state_end.prepared=false`
/ `sampleRate=0`, and `join_gate=false`. The RT gate passed on all 54 cells
(cold/warm zero counters) and the default-long gate passed (BTrack join observed,
steps recorded). The failures are harness defects, not evidence to be edited.

### Defect A — coherent state latch

`runCell` used the result of `readJamLiveState(c.stateEnd)` even when it returned
false. The coherent latest-value slot returns false when there is no new
publication (or a sequence race), leaving a default zero state, even though the
per-callback reads were all good.

Fix: obtain a baseline prepared state (`prepared=true`, `sampleRate>0`) with a
bounded off-callback poll **before** the cold callback; initialize `state_start`
and `state_end` from it. Update `state_end` **only** when a read returns true;
never reset it on false; the final read is a temp that replaces `state_end` only
on true. The reported `state_end` is the last coherent snapshot (retains the seen
generation/prepared/rate/cursor), not an exact current-callback cursor. No
unbounded wait and no forced worker.

A pure `StateLatch` helper (shared by the harness and the support self-test)
retains the last valid state on false and fails explicitly if no baseline was
ever read.

### Defect B — injected scenario was unpaced and vacuous

`runInjectedJoinStop` had no sleep: 8 s of audio ran in milliseconds, the
workers could not track the timeline, the ring overflowed, and no join occurred.
`stopNowStopped` "passed" at block 1 without a join; `resyncAccepted` had no
servicing; the reprepare generations were not a real session-generation change.

Fix: pace the injected scenario in real time (`steady_clock.sleep_until` outside
the callback). Require a first join (engine `injectedPlaying` + steps grow)
within a bounded 8 s audio window; a deferred `StopAtNextBar` that actually
persists until the bar; a **second actual join before `StopNow`**; an actual
`StopNow` measured in serviced blocks after the command; an actual resync effect
observation; a session-generation change via prepare cold state; and a coherent
released payload (`prepared=false`, `drumsPlaying=false`) on shutdown. Command
acceptance alone is never proof.

The injected join gate now requires `firstJoin`/`secondJoin`, `steps_fired>0`,
`engine_playing_observed`, `stop_now_stopped`, `resync_accepted` **and**
`resync_effect_observed`, `session_generation_changed` and `released_confirmed`.
Engine steps and injected playing carry the proof; no output-RMS-only claim.

## Semantics unchanged

`structural AND rt_gate AND join_gate`; default no-lock remains diagnostic; the
54-cell matrix, pacing, warm counts and expected ids are unchanged. Only the
injected scenario is strengthened to prove what the gate already claimed. A
truthful actual-002 run in a different directory is required; failures are never
hidden by editing evidence.

## Scope

Only `tools/live-jam-replay/**`, `tests/LiveJamProcessorTests.cpp`,
`docs/research/LIVE-JAM-REPLAY*.md`, `docs/research/live-jam-replay/**` and
`task-notes/EVAL-LIVE-001.md`. No processor/editor/engine/core/shared CMake/UI or
ledger source. No agents. Link only; no measurement from the worker.
