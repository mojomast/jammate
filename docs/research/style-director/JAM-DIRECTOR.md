# Jam Director (DIRECTOR-001)

`src/jam/JamDirector.{h,cpp}` — deterministic, bounded musical policy for the
adaptive drummer. DEVPLAN DIRECTOR-001, SPEC.md 13.3/14/15; adaptive seam in
`docs/research/ADAPTIVE-WAVE-CONTRACT.md`.

## Ownership boundary

The director owns **musical decisions**: which style-tier groove should play,
when a fill/break is wanted, and how hard the drums are hit. It does **not** own
the live transport lifecycle. `JamJoinPolicy` remains the single owner of
join/stop and the audio-owner echo; the director only reads the clock lock state,
the playback echo and a `lifecycleAllowsPerformance` boolean (which the session
sets from `requestedRunning && !stopPending`). The director never issues a stop
and never claims a change landed before the caller confirms it.

## Data flow

```text
ClockSnapshot ─┐
TransportPosition (explicit echo) ─┤
energy / onset / silence features ─┼─> JamDirector::update() ─> DirectorDecision
DirectorSettings ──────────────────┤                              ├─ JamIntent
sessionGeneration / audioCursor ───┤                              └─ QueuedBarChange (proposal)
discontinuity / lifecycle flag ────┘
                                            caller publishes via bridge bool
                                            JamDirector::acknowledgePublication(accepted)
```

## Intent / ack / retry

- `update()` proposes at most one `QueuedBarChange`.
- The same change is re-emitted on every subsequent tick until
  `acknowledgePublication()` is called.
- `acknowledgePublication(true)` commits selection/repetition state (advances the
  committed groove/fill, the anti-repeat ring and the fill gap).
- `acknowledgePublication(false)` keeps the proposal pending and retries it
  unchanged. A rejected publication never advances state and is never latched.
- `publishPending(fn)` is the ergonomic helper: it calls `fn(change)` only when a
  change is pending AND the director is still safely `Playing`, feeds the bool
  back, and returns whether it was accepted. It refuses to publish once a
  safety transition (Holdover/Lost/lifecycle) has cancelled the proposal.

## Forget / cancel

`cancelPending()` forgets an unacknowledged proposal without touching any live
command queue. It is invoked automatically on:

- explicit Stop (`notifyStopRequested()`),
- clock **Lost** (state -> Reacquiring),
- clock **Holdover** (a pending fill decided while Locked is not carried through
  a confidence dip),
- a pending **fill** whose confidence has fallen below
  `fillConfidenceThreshold`,
- input **discontinuity**,
- a non-zero **sessionGeneration** change (session boundary),
- `lifecycleAllowsPerformance == false`.

These safety checks run on **every** tick, including a repeated `audioCursor`
(the idempotent fast path still runs the state machine and `enforcePendingSafety`
before holding the envelope/phrase/RNG). A user Reset should call
`cancelPending()` explicitly. Committed groove/fill are not rewritten by cancel;
a later proposal naturally supersedes them.

## State machine (`DirectorState`)

`Idle -> Listening -> ReadyToJoin -> Playing -> Holdover -> Reacquiring ->
Stopping -> Idle`

- `Idle`: nothing until `notifySessionStarted()`.
- `Listening`: waits for a Locked clock at or above `joinConfidenceThreshold`.
- `ReadyToJoin`: lock is usable; the live policy joins.
- `Playing`: playback echo engaged; the director proposes changes.
- `Holdover`: short loss holds the current pattern, cancels a pending proposal
  and suppresses fills.
- `Reacquiring`: long loss; holds, pending forgotten.
- `Stopping`: **terminal**. A transient echo returning does not revive Playing.
  The parent exits it with `notifyStopCompleted()` once the actual stopped echo
  has arrived (`!playing && !policy.stopPending`), or with `reset()`. Neither
  touches the transport, so join/stop ownership stays with JamJoinPolicy.
  `notifyStopCompleted()` clears the committed selection so a restart proposes a
  groove again.

Only `Playing` (plus Locked clock and lifecycle permission) can propose.

## Bar boundaries

The **Musical Clock bar phase is the single authoritative bar-advance source**.
The explicit transport is a confirmation/re-anchor source only: a transport
`bar` increment never adds a bar of its own (which previously double-counted one
physical bar when a lagged `bar` increment landed on the tick after the phase
wrap). A transport skip (`delta > 1`) or backward move (`delta < 0`) re-baselines
the phase tracker so the next genuine wrap is measured from the new position,
and never fabricates bar counts. A flagged discontinuity likewise re-baselines.

## Musical behaviour

- **Intensity envelope.** `target = settings.intensity01 + (energy01 - 0.5) *
  0.4`, clamped; smoothed with `DirectorConfig.energyAttack` / `.energyRelease`.
- **Complexity envelope.** glides to `settings.complexity01`.
- **Tiers.** envelope < 0.34 -> Low, < 0.67 -> Medium, else High, with
  `intensityDeadband` hysteresis; promotion to High requires
  `highEnergySustainTicks` of sustained energy.
- **Groove selection.** seeded SplitMix64; excludes the current groove and the
  last `effectiveRepetitionWindowBars()` committed grooves, so no immediate
  repeat where alternatives exist. The window is the active style's
  `minRepetitionDistanceBars`, falling back to
  `DirectorConfig::minimumRepetitionDistance` only when the style value is <= 0.
- **Phrase boundaries.** every 4 bars; the boundary is the musical reason to
  rotate the groove and the preferred fill placement.
- **Fills.** chance = `fillBaseChance (+ fillPhraseBoundaryBonus at a phrase)`,
  scaled by `fillAmount01 / 0.30`; a strong onset at a phrase adds
  `0.10 * onsetStrength`. Below `fillConfidenceThreshold` fills are hard
  suppressed (counted); between it and `joinConfidenceThreshold` they are scaled
  by `fillConfidenceSuppression`. `kDirectorMinFillGapBars` spaces them out.
- **Explicit user actions.** `requestFill` / `requestBreak` are edge-latched and
  honoured at the next proposal while playback is engaged.

## Determinism and idempotency

- No threads, no time source, no I/O, no allocation, no STL containers; fixed
  arrays only.
- The only entropy is a seeded SplitMix64, so identical settings + input
  sequences reproduce identical decisions (`jamdirector.identicalSeedsReproduceDecisions`).
- When `DirectorInputs.audioCursor` is non-zero, a repeated value means the
  control worker ran again for the same audio position: the envelope, phrase
  counter and RNG do **not** advance and a committed bar is **not** re-decided; a
  pending proposal is still re-emitted for retry. Pass `0` in unit tests.

## Inspection

`report()` returns `DirectorReport`: `state`, full `settings`, `tier`,
`committedGroove`/`committedFill`, `pendingGroove`/`pendingFill`,
`pendingChange`, `pendingBreak`/`pendingCrash`, `repetitionWindowBars`,
`intensityEnvelope01`/`complexityEnvelope01`, `barsObserved`, and counters
(`acceptedPublications`, `rejectedPublications`, `cancelledPublications`,
`fillsEmitted`, `fillsSuppressed`, `staleResets`). The embedded `intent` is
complete, including `requestCrash` and `sectionIndex`.

## Configuration

The musical tunables remain in `DirectorConfig` (`src/jam/JamConfig.h`): join
threshold, energy attack/release, deadband, sustained-energy ticks, fill chances
and confidence gates, minimum repetition distance and random seed.

`JamDirector.h` also defines named policy defaults that do not yet have a home in
`DirectorConfig` (`kDirectorPhraseLengthBars`, tier edges, min fill gap, neutral
fill amount, onset bonus, complexity gains). These are musical tunables, not
structural constants; they live in the director only because this task does not
own `JamConfig.h`. Recommended follow-up (parent-owned): promote them into
`DirectorConfig`.

## Limitations

- The director is not yet wired into `LiveJamSession`/`DrumClockBridge`; that is
  the orchestrator's integration task. The adaptive seam names
  `DrumClockBridge::requestBarChange(const QueuedBarChange&)`, which did not
  exist at handoff time.
- The state machine exposes `Stopping` but deliberately does not stop the
  transport; the parent calls `notifyStopCompleted()`/`reset()` on the stopped
  echo.
- The `QueuedBarChange` payload carries groove/fill/intensity/swing/humanize
  only. `requestBreak` and `requestCrash` are `JamIntent`-level signals with no
  render path yet; the UI for those actions should stay disabled until an
  integration exists.
- Groove/fill selection uses the catalog's curated tiers only.
