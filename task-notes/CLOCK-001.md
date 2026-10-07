# CLOCK-001

## Goal

Implement the Musical Clock / Entrainment Engine: the single component that
turns tracker `RhythmObservation` evidence into the stable
`ClockSnapshot` belief the Jam Director and drummer follow. Includes the
deterministic synthetic test suite that proves the SPEC 19 rhythm-quality
targets.

## Base commit

`48f301c8fd046429fe55519ec3a2d20a2fccaa44`

Branch `wp/CLOCK-001-clock`.

## Files changed

Created:

- `src/jam/MusicalClock.h` — public API + private state. Only `<cstddef>`,
  `<cstdint>` and the frozen jam headers; no STL containers, no JUCE.
- `src/jam/MusicalClock.cpp` — implementation.
- `tests/jam/MusicalClockTests.cpp` — suite `MusicalClock`, 16 scenarios.
- `task-notes/CLOCK-001.md` — this file.

Modified (additive only):

- `src/jam/JamConfig.h` — four new `ClockConfig` fields, appended after the
  existing meter fields. **No existing field value was changed.**

New `ClockConfig` fields (all commented at the point of declaration):

| Field | Default | Justification |
|---|---|---|
| `double defaultSampleRate` | `48000.0` | Rate assumed by TapTempo / Resync commands that can arrive before the first `advance()` (SPEC 5.6). Avoids a bare literal in `.cpp`. |
| `TempoMode defaultMode` | `TempoMode::Follow` | SPEC 5.3 initial mode; SPEC 5.4 expects Loose to become the shipping default later, which should be a config edit. |
| `float holdoverConfidenceDecayPerSecond` | `0.12f` | SPEC 10.1 Holdover "reduce confidence over time" must continue while observations are absent, so it is per second of audio, not per observation. |
| `double tapTempoResetSeconds` | `2.0` | SPEC 5.6/10.2 tap UX: a gap longer than this starts a new tap sequence. |

## Contract implemented

`MusicalClock` is a plain deterministic class. Public API:

```cpp
explicit MusicalClock (const ClockConfig& config = {});
void observe (const RhythmObservation& obs);
void advance (uint64_t samples, double sampleRate);
void command (const ClockCommand& cmd);
ClockSnapshot snapshot() const;
void reset();
void setMode (TempoMode mode);
TempoMode mode() const noexcept;
```

The published `generation` increments only when a published field actually
changes, so repeated `snapshot()` calls with no intervening `observe` /
`advance` / `command` return the same generation.

### Scenario -> test map (all 16 required)

| # | Scenario | Test |
|---|---|---|
| 1 | perfect 120 BPM | `MusicalClock.perfect120` |
| 2 | noisy 120 BPM | `MusicalClock.noisy120` |
| 3 | extra offbeat onsets | `MusicalClock.extraOffbeatOnsets` |
| 4 | missing beats | `MusicalClock.missingBeats` |
| 5 | 120 -> 130 ramp | `MusicalClock.ramp120to130` |
| 6 | half-time candidate | `MusicalClock.halfTimeCandidate` |
| 7 | double-time candidate | `MusicalClock.doubleTimeCandidate` |
| 8 | two-bar silence | `MusicalClock.twoBarSilence` |
| 9 | prolonged silence | `MusicalClock.prolongedSilence` |
| 10 | explicit resync | `MusicalClock.explicitResync` |
| 11 | freeze / unfreeze | `MusicalClock.freezeAndUnfreeze` |
| 12 | tap tempo | `MusicalClock.tapTempo` |
| 13 | Fixed / Follow / Loose | `MusicalClock.modesDifferInReactivity` |
| 14 | half / double manual | `MusicalClock.manualHalfDouble` |
| 15 | determinism | `MusicalClock.deterministic` |
| 16 | no raw bypass | `MusicalClock.singleWildObservationDoesNotMoveTempo` |

### Numeric behaviour chosen, with reasoning

All tunables come from `ClockConfig` (`src/jam/JamConfig.h`). The original
defaults are used unchanged. Only structural values remain as named constants
in `.cpp` (`kSecondsPerMinute`, `kHalfTimeRatio`, `kDoubleTimeRatio`,
`kPhaseHalfTurn`).

**Tempo slew.** Per usable observation, `bpm` moves toward the clamped
candidate by at most `bpm * maxSlewRatioPerObservation[mode]`. Follow = 2%/obs,
Loose = 0.4%/obs, Fixed = 0 (Fixed ignores tracker tempo entirely). This is
already SPEC 10.3; the implementation just enforces it. Slew (rather than a
snap) is what makes a tracked tempo change audible as drift, satisfying
"no abrupt audible discontinuities" (SPEC 19).

**Phase correction.** In Locked only. The error is measured at the
observation's own `inputSampleTime`, so correctness does not depend on whether
the caller advanced before or after observing. Absorbed per observation is
`clamp(error * phaseCorrectionGain[mode], +/- maxPhaseErrorAbsorbed)`:
Follow gain 0.25, Loose gain 0.06, Fixed gain 0 (phase free-runs on the user
tempo). Clamping to 0.35 of a beat is what prevents a single syncopated onset
from dragging the grid. Phase is not corrected while Acquiring; it is snapped
onto the most recent phase evidence exactly once, at lock.

**Confidence rise/fall.** `observe` attacks toward the tracker confidence at
`confidenceAttackRate` (0.35) or releases at `confidenceReleaseRate` (0.08).
Time-driven decay (`holdoverConfidenceDecayPerSecond`) applies only in
Holdover/Lost, because no observations arrive during silence. Holdover clamps
confidence to `holdoverConfidenceFloor` (0.25) so the drummer stays anchored
but "less certain"; this is why the two-bar-silence test can assert decay but
not to zero.

**Half/double confirmation.** A candidate within `metricAmbiguityRatio` (5.5%)
of half or double the belief is a metric twin. In Locked it is counted, never
applied on first sight; after `metricConfirmationObservations` (5) consecutive
twins the belief is multiplied by 0.5/2.0 at once (an octave re-interpretation,
not a slew), and `beatsElapsed_` is scaled to keep wall-clock coherent. Any
non-twin observation resets the twin streak. Twin handling precedes the
isolated-jump gate, so a half-time reading is never miscounted as a wild jump.

**Isolated-jump rejection.** A change above `isolatedJumpRejectRatio` (11%) is
rejected until seen `isolatedJumpReinforceCount` (3) times in the same
direction. On reinforcement: while Locked it still slews (no discontinuity);
while Acquiring it adopts immediately so acquisition is not slowed. A single
wild observation (test 16) therefore cannot move the published tempo.

**Acquisition / lock.** Lock requires all of: confidence >=
`lockConfidenceThreshold` (0.65), at least `minimumLockedObservations` (4)
tempo-agreeing observations within `tempoAgreementTolerance` (3%), at least 4
phase-valid observations, and `acquireWindowSeconds` (8 s) of fresh-evidence
time. In Fixed mode tempo agreement is skipped (tempo is user-owned) but the
clock still locks phase. The 8 s default is interpreted as the config author's
comment states it ("2 bars at 60 BPM"), i.e. a hard minimum evidence window.

**Holdover / Lost transitions.** Time-driven in `advance()`.
Locked -> Holdover at `holdoverEnterSeconds` (1.5 s) with no usable evidence;
Holdover -> Lost at `lostEnterSeconds` (6 s). Return to Locked from Holdover
only if confidence is still above the lock threshold; otherwise Acquiring.

**Lost exit policy (judgement call).** SPEC 10.1 only says "Return to
Acquiring". Chosen policy: stay Lost while evidence is absent (so Lost is
observable and the drummer can stop at a boundary), and on the first fresh
usable observation transition to Acquiring with acquisition counters reset.
This is documented in code.

**Meter.** `beatsPerBar`/`beatUnit` from config; `beatInBar` is
`1 + floor(beatsElapsed) mod beatsPerBar`; `beatPhase01` and `barPhase01` are
both advanced by `advance()` from the single `beatsElapsed_` position.

**Commands.** `TapTempo` averages tap intervals (per-second-decay gap reset),
sets tempo and phase immediately, locks, bypassing slew. `ResyncNextBeat` /
`ResyncNextBar` align a beat / downbeat exactly at the requested sample time.
`HalfTime` / `DoubleTime` apply the octave factor immediately. `FreezeTempo`
gates only tracker *evidence*; `ResumeFollow` releases it. `Reset` clears all
history.

## Tests executed

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
cd /home/mojo/projects/worktrees/CLOCK-001-clock
rm -rf /tmp/opencode/build-clock001
cmake -S jam-core -B /tmp/opencode/build-clock001 -G Ninja
cmake --build /tmp/opencode/build-clock001
ctest --test-dir /tmp/opencode/build-clock001 --output-on-failure
/tmp/opencode/build-clock001/jamTests MusicalClock.
/tmp/opencode/build-clock001/jamTests Nope.
```

## Test results

- ctest: `jam.MusicalClock` **Passed**, exit **0** (only suite present; the
  `AnalysisAudioRing` suite of another worker is not in this worktree).
- 16 tests, 93 checks, 0 failures.
- Zero compiler warnings under `-Wall -Wextra -Wpedantic`.
- Filter `Nope.` matches zero tests -> prints the error and exits **2**.

## Evidence

### Full clean build (warning scan)

```
[0/2] Re-checking globbed directories...
[1/7] Building CXX object CMakeFiles/jam-core.dir/.../src/jam/JamCore.cpp.o
[2/7] Building CXX object CMakeFiles/jam-core.dir/.../src/jam/MusicalClock.cpp.o
[3/7] Linking CXX static library libjam-core.a
[4/7] Building CXX object CMakeFiles/jamTests.dir/.../tests/jam/JamTestMain.cpp.o
[5/7] Building CXX object CMakeFiles/jamTests.dir/.../tests/jam/MusicalClockTests.cpp.o
[6/7] Linking CXX executable jamTests
$ grep -iE "warning|error" fullbuild.log
no warnings/errors
```

### ctest green

```
Test project /tmp/opencode/build-clock001
    Start 1: jam.MusicalClock
1/1 Test #1: jam.MusicalClock .................   Passed    0.00 sec

100% tests passed out of 1
exit=0
```

### Direct binary `jamTests MusicalClock.`

```
[suite] MusicalClock
  PASS MusicalClock.perfect120
  PASS MusicalClock.noisy120
  PASS MusicalClock.extraOffbeatOnsets
  PASS MusicalClock.missingBeats
  PASS MusicalClock.ramp120to130
  PASS MusicalClock.halfTimeCandidate
  PASS MusicalClock.doubleTimeCandidate
  PASS MusicalClock.twoBarSilence
  PASS MusicalClock.prolongedSilence
  PASS MusicalClock.explicitResync
  PASS MusicalClock.freezeAndUnfreeze
  PASS MusicalClock.tapTempo
  PASS MusicalClock.modesDifferInReactivity
  PASS MusicalClock.manualHalfDouble
  PASS MusicalClock.deterministic
  PASS MusicalClock.singleWildObservationDoesNotMoveTempo

16 tests, 93 checks, 0 failed check(s) in 0 test(s)
exit=0
```

### Zero-match guard

```
$ jamTests Nope.
ERROR: filter "Nope." matched zero tests
exit=2
```

### Deliberately broken, then reverted

Perturbed `perfect120` expected BPM from 120 to 90:

```
    FAIL .../MusicalClockTests.cpp:172
      CHECK_NEAR failed: snapshot.bpm ~= 90.0
    actual:   120.000000
    expected: 90.000000
    tolerance: 1.200000
  FAIL MusicalClock.perfect120
  ...
16 tests, 93 checks, 1 failed check(s) in 1 test(s)

0% tests passed, 1 tests failed out of 1
exit=8
```

After reverting the assertion, ctest is green again (exit 0), as reproduced
above. The suite can fail.

## Known limitations

Judgement calls rather than derivations from SPEC, and SPEC-underspecified
points:

1. **`SetMode` command carrier.** `RhythmTypes::ClockCommand` has no `arg0`
   field although the enum comment names one for `SetMode`. The comment says
   `arg0 = int(TempoMode)`. I read `tapSampleTime` as the mode index in
   `command()`, and marked this `// SPEC-UNDERSPECIFIED:` in code. The clean
   path for the orchestrator is the `setMode()` method. If this is wrong, the
   frozen type needs a change and CLOCK-001 should not be the place.
2. **Freeze vs explicit command.** SPEC 10.2 freezes tempo against *evidence*
   but does not say whether Half/Double/Tap/Resync may override a freeze. I let
   explicit human commands win and marked it `// SPEC-UNDERSPECIFIED:` in code.
   Only FreezeTempo/ResumeFollow change the freeze flag.
3. **Lost exit policy.** Stay Lost until fresh evidence, then Acquiring (see
   above). Not specified beyond "Return to Acquiring".
4. **Reinforced jump while Locked slews rather than snaps.** A defensible
   reading of "rejected unless reinforced"; it keeps transitions non-abrupt.
5. **Acquire window is a hard minimum.** `acquireWindowSeconds` is treated as
   wall-clock fresh-evidence time, per the config author's comment. At 120 BPM
   this is ~4 bars, slightly more conservative than SPEC's "1-2 bars". It is a
   config value the orchestrator owns and can retune.
6. **No `attemptLock` promotion in Fixed.** Fixed mode never moves tempo from
   evidence by design, so no tempo is "acquired"; it still locks phase.
7. **Bar phase at lock.** The integer beat count is preserved across lock, so
   the bar does not restart; only the fractional phase is snapped. This keeps
   `barPhase01` continuous but means `beatInBar` at lock is whatever the
   free-running count reached rather than always 1. Tap tempo does align a beat
   (phase 0) but does not force a downbeat.
8. **Meter is config-only.** There is no meter command in the frozen command
   set, so `beatsPerBar`/`beatUnit` cannot change at runtime.
9. **Sample-rate mapping.** Phase/time conversions assume the clock's
   `sampleRate` and the observation timeline share one sample clock
   (`obs.sourceSampleRate` is not used to rescale `inputSampleTime`). If the
   analysis chain resamples, the orchestrator must keep `advance()` and
   `inputSampleTime` on the same device timeline.

## Integration notes

The Jam Director must be wired to exactly these calls, on the
rhythm-analysis / control thread (SPEC 7.3), never on the audio callback:

- construct once with the orchestrator-owned `ClockConfig`;
- call `advance(numSamples, sampleRate)` once per analysis block (or any
  monotonic audio cadence) — this is the only source of time;
- call `observe(obs)` once per `RhythmObservation` published by the analyzer,
  ideally after advancing to that observation's sample time;
- call `command(cmd)` for each dequeued `ClockCommand` (SPEC 8.3);
- read `snapshot()` and publish it as the latest-value double buffer
  (SPEC 8.2). The director must treat it as read-only.

What the director still needs that this component does not provide:

- The transport/join policy (SPEC 15) is out of scope; the clock only reports
  `lockState` and `confidence01`.
- No `JamIntent` or `DrumTransport` coupling exists here.
- `ClockSnapshot` has no "predicted sample time of next beat/bar". If the
  scheduler needs it, derive it from `bpm` + `beatPhase01` + `beatsPerBar`, or
  request a snapshot extension centrally.
- `tempoFrozen` is reported so the director can keep a frozen drummer through
  Lost (SPEC 10.1).

Hard invariants preserved: there is exactly one tempo belief; no public method
writes `RhythmObservation::bpmCandidate` through; the class is deterministic,
header-clean, and contains no unbounded containers.

## Final commit SHA

Implementation commit (contains `MusicalClock.h/.cpp`, the test suite and the
`JamConfig.h` additions): `2d8688497f1330981b9112ff346aedb4f23bb263`.
The tip of `wp/CLOCK-001-clock` is the immediately following documentation
commit that records this line.
