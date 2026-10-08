# Tracker acquisition — causal diagnosis of both real adapters (TRACK-004)

This is a **diagnostic**: it selects no tracker, tunes no gate, proposes no ADR
and performs no live integration. **G3 stays OPEN.** It exists to explain, from
causal per-block evidence, *why* both real integrations fail SPEC 19's
acquisition gate and why BTrack's locked-BPM error is −2.34 %, without touching
`src/`, `vendor/`, any CMake file, the corpus, or the shared harness/scoring.

- Base: `main` `6287288` (`merge(EVAL-005): integrate corrected paired tracker
  robustness evidence`).
- Corpus: the untouched original 19-fixture `testdata/rhythm`; all 19 WAV
  sha256 and byte sizes re-verified (`tracker-acquisition/wav-hashes.txt`).
- Backends: the EVAL-005 main-core plugin builds, whose adapter sources are
  byte-identical to this base (`librhythm-eval-btrack.so` sha256
  `41e6476e…`, `librhythm-eval-aubio.so` sha256 `61336277…`). See
  `tracker-acquisition/provenance.json`.
- New tool: `tools/tracker-diagnostics/**` (JUCE-free, GPL-free binary; backends
  are `dlopen`ed through the existing plugin convention).

## 1. What was measured, and how

`tools/tracker-diagnostics` drives one `IRhythmTracker` at a time over one WAV
and records **every block's raw `RhythmObservation`** beside the block's device
clock: BPM, phase validity, silence, confidence, onset strength, RMS, beat
event, reported device sample, block start and block end. Two clocks are kept
exactly as EVAL-004 defined them:

- **event time** — the backend's causal reported device time (aubio sub-hop
  tactus, BTrack analysis-hop start); fallback to block start only when
  non-causal;
- **causal availability** — the device time at which `process()` returned (the
  block end).

The same block vector is reduced by the **unmodified** `rhythmeval::toSeries()`
and scored by the **unmodified** `rhythmeval::scoreFixture()`, so the trace and
the scorer can never disagree about what was scored. The scorer's acquisition
rule (`Metrics.cpp::findFirstLockFrom`: 4 consecutive predicted beats, each
within 70 ms of a distinct forward-advancing truth beat, each with a
phase-valid tempo sample within 2 % of local truth) is restated independently in
`AcquisitionReplay.cpp` and asserted to agree with `scoreFixture` on every real
fixture and on synthetic series (`tests/TraceReplayTests.cpp`). **No lock-run
start/end semantics were changed.**

Reproduce:

```bash
tools/tracker-diagnostics/build.sh                 # tool + arithmetic tests + config shim
tools/tracker-diagnostics/run-corpus.sh            # writes docs/research/tracker-acquisition
/home/mojo/projects/build-TRACK-004/diag/TraceReplayTests
```

**Cross-check against EVAL-004/EVAL-005.** The diagnostic reproduces the
committed EVAL-004 primary numbers exactly (uncompensated, 128-frame):

| run | F mean | acq core | worst core BPM err | reported beats | causal avail mean |
|---|---|---|---|---|---|
| BTrack 128 | 0.7099 | 4/11 | 0.0234 | 400 | 12.9 ms |
| aubio 128 | 0.5357 | 7/11 | 0.0133 | 288 | 5.7 ms |

## 2. The headline: BTrack's acquisition failures are a *BPM-report* failure, not a beat-placement failure

For five of BTrack's seven core acquisition failures the predicted beats line up
positionally with the ground-truth grid for **16–19 consecutive beats**
(`longestMatchRun`), and the *only* failing clause of the lock test is the 2 %
tempo agreement (`longestTempoRun = 0`). The traces show BTrack's beats are at
≈126 BPM while its reported `bpmCandidate` is the quantised 123.05.

Example, `clean_eighths` (nominal 126), BTrack, 128-frame: after the first four
erratic beats the inter-beat interval settles at **0.47746 s ≈ 125.7 BPM**
(mean over the next 16 beats), i.e. the beats are where the truth is, yet the
scored `lockedBpm` is **123.046875** (−2.34 %), so every beat's tempo check
fails and no lock is ever confirmed. The emitter and the tempo estimator are
decoupled in BTrack.

Aubio is the mirror image: its tempo sits inside 2 % (median +0.9 % to +1.3 %)
so tempo agreement is not its problem; its failures are **phase conflicts**
(the beats land between truth beats) plus two **silence-gate** cases (§5).

## 3. Per-core-fixture reason table (128-frame primary)

Full 44-row table (both framings) with lock start/confirmation and the
silence-gate experiment: `tracker-acquisition/core-reasons.md`. Reasons are
machine-assigned by fixed priority from trace evidence; the priority and the
supporting numbers live in `AcquisitionReplay.cpp::diagnoseFixture`.

| backend | fixture | acq | bars | lock start (s) | lock confirm (s) | confirm avail (s) | match/tempo run | median BPM | BPM err | reason |
|---|---|---|---|---|---|---|---|---|---|---|
| btrack | arpeggio | 0 | — | — | — | — | 19/0 | 117.454 | 0.021 | LockTempoAgreementFailure |
| btrack | blues_shuffle | 1 | 1.995 | 4.783 | 6.455 | 6.469 | 15/5 | 107.666 | 0.003 | AcquiredWithin2Bars |
| btrack | clean_eighths | 0 | — | — | — | — | 16/0 | 123.047 | 0.023 | LockTempoAgreementFailure |
| btrack | clean_sixteenths | 0 | — | — | — | — | 19/0 | 123.047 | 0.023 | LockTempoAgreementFailure |
| btrack | missing_downbeats | 0 | — | — | — | — | 18/0 | 117.454 | 0.021 | LockTempoAgreementFailure |
| btrack | palm_mute_metal | 0 | — | — | — | — | 18/0 | 123.047 | 0.023 | LockTempoAgreementFailure |
| btrack | power_chords_distorted | 0 | — | — | — | — | 0/0 | 123.047 | 0.023 | NoMatchingBeats |
| btrack | sparse_single_notes | 1 | 1.256 | 3.042 | 4.656 | 4.669 | 15/15 | 109.957 | 0.018 | AcquiredWithin2Bars |
| btrack | stop_start | 1 | 0.261 | 0.824 | 2.159 | 2.173 | 15/6 | 129.199 | 0.021 | AcquiredWithin2Bars |
| btrack | sustained_chords | 1 | 2.744 | 7.210 | 9.718 | 9.731 | 6/4 | 95.703 | 0.003 | AcquiredAfter2Bars |
| btrack | syncopated_funk | 1 | 1.002 | 2.496 | 4.110 | 4.123 | 18/16 | 109.957 | 0.018 | AcquiredWithin2Bars |
| aubio | arpeggio | 1 | 1.254 | 2.858 | 4.346 | 4.352 | 15/15 | 121.400 | 0.012 | AcquiredWithin2Bars |
| aubio | blues_shuffle | 1 | 1.258 | 3.145 | 4.804 | 4.811 | 15/15 | 108.885 | 0.008 | AcquiredWithin2Bars |
| aubio | clean_eighths | 1 | 1.249 | 2.729 | 4.137 | 4.139 | 15/15 | 127.598 | 0.013 | AcquiredWithin2Bars |
| aubio | clean_sixteenths | 0 | — | — | — | — | 3/3 | 127.543 | 0.012 | PhaseConflictOrDropouts |
| aubio | missing_downbeats | 1 | 1.504 | 3.358 | 4.851 | 4.853 | 15/14 | 121.439 | 0.012 | AcquiredWithin2Bars |
| aubio | palm_mute_metal | 0 | — | — | — | — | 3/1 | 127.497 | 0.012 | PhaseConflictOrDropouts |
| aubio | power_chords_distorted | 0 | — | — | — | — | 2/2 | 127.560 | 0.012 | PhaseConflictOrDropouts |
| aubio | sparse_single_notes | 1 | 1.254 | 3.037 | 4.630 | 4.640 | 15/15 | 113.184 | 0.011 | AcquiredWithin2Bars |
| aubio | stop_start | 1 | 1.744 | 3.521 | 4.868 | 4.875 | 11/10 | 133.697 | 0.013 | AcquiredWithin2Bars |
| aubio | sustained_chords | 0 | — | — | — | — | 4/3 | 97.276 | 0.013 | LockTempoAgreementFailure |
| aubio | syncopated_funk | 1 | 1.501 | 3.566 | 5.160 | 5.163 | 10/9 | 113.009 | 0.009 | AcquiredWithin2Bars |

Reason counts, core, 128-frame:

| reason | BTrack | aubio |
|---|---|---|
| AcquiredWithin2Bars | 4 | 7 |
| AcquiredAfter2Bars (delayed) | 1 (`sustained_chords`) | 0 |
| LockTempoAgreementFailure | 5 | 1 (`sustained_chords`) |
| PhaseConflictOrDropouts | 0 | 3 |
| NoMatchingBeats | 1 (`power_chords_distorted`) | 0 |
| InsufficientBeatEvents | 0 | 0 |

**Insufficient onset evidence** never fires on a core fixture: every core clip
emits ≥ 15 beats within 128-frame analysis except `aubio/palm_mute_metal` (5)
and `aubio/tapping_muting_only` (9, not core). It is a real code path, just not
the cause here — recorded as missing evidence rather than a fabricated reason.

**Octave mismatch** never fires on a core fixture (no `ratioToTruth` within 10 %
of 0.5 or 2.0); only the plain 2 % BPM band fails. It is kept as a distinct
label because the robustness work saw bimodal `/2` locks elsewhere.

## 4. BTrack's −2.34 %: quantised lag report, *not* the device resampler

**Answer: 123.05 vs 126 is a tempo *estimate* artefact of BTrack's integer-hop
comb grid. It is not caused by the adapter's 48 kHz→44.1 kHz device resampler,
and it does not reflect the beat times, which are correctly placed.**
Confidence: **high** for the resampler exclusion and for the exact grid values;
**medium** for the claim that the *selection* of lag 42 rather than 41 is
internal to BTrack's comb/observation model (we did not patch the vendor to
force lag 41, so the "why 42" step is bounded, not proven).

### 4.1 Arithmetic (`experiments/btrack_grid.csv`)

BTrack 1.0.7 `calculateTempo()` (`third_party/BTrack/src/BTrack.cpp:435,438`)
maps its 41 candidates `2·maxIndex+80` (80…160 BPM, step 2) to an **integer**
number of 512-sample hops and then recomputes BPM from that integer:

```
beatPeriod = round(60·44100 / (candidate·512))
reported   = 60 / ((512/44100)·beatPeriod)
```

The resulting *allowed* reported values near the corpus are an exact staircase:

| hop lag | reported BPM | = 2646000/(512·lag) |
|---|---|---|
| 41 | 126.048018 | true 126 is only 0.038 % away |
| 42 | **123.046875** | the observed BTrack value |

`123.046875 = 2646000 / (512·42)` exactly, and `126.048018 = 2646000 / (512·41)`
exactly. So 126 **is** representable by the grid — the estimator committed to
42 hops (≈ 0.4876 s), not the true 41.016 hops (≈ 0.4762 s).

### 4.2 Direct experiment: the device resampler is excluded

A deterministic 24 s click train (the pinned backend-test waveform) was swept
over 118…134 BPM through the **real adapter** at two device rates
(`experiments/btrack/click-rate48000/` and `click-rate44100/`):

- At 48000 Hz the adapter resamples to 44.1 kHz; at 44100 Hz its resampler is a
  pass-through (`step = 1`).
- **The reported BPM sequence is identical to the last digit at both rates**
  (max |Δreported| = 0.000000 over the whole sweep). Requested 126 → 123.046875
  at both 48 k and 44.1 k.

If the device resampler introduced the bias, removing it (44.1 k feed) would
change the estimate. It does not. The resampler is therefore **not** the cause.
(For contrast, aubio's estimate *does* move with the feed rate, by up to
0.45 BPM across the sweep, because its autocorrelation window length changes with
the rate — aubio is the rate-sensitive one.)

### 4.3 Direct experiment: beat placement is unbiased

The sweep's mean inter-beat interval tracks the requested tempo, not the
reported one:

| requested | reported BPM | mean beat interval | ratio vs truth |
|---|---|---|---|
| 126 | 123.046875 | 0.476483 s | 1.00061 |

and on the real `clean_eighths` trace the steady inter-beat interval is 0.47746 s
(≈ 125.7 BPM). BTrack's `beatDueInCurrentFrame()` beats follow the onset grid;
`getCurrentTempoEstimate()` returns the quantised HMM tempo. On this corpus the
2.34 % lives entirely in the latter.

### 4.4 Consequence, stated without selecting a tracker

The SPEC 19 BPM gate is defined on the **reported locked BPM**; BTrack fails it,
and that same reported value is what blocks 5/11 core acquisition locks. The
diagnosis does not authorise "fixing" it by relaxing the gate or by silently
substituting a beat-interval-derived tempo: either would be a gate/semantics
change requiring its own task and ADR review (see §9).

## 5. Silence-gate suppression (bounded config experiment, NOT default evidence)

The adapters hold a −60 dBFS / 50 ms silence state and suppress beat events while
it is latched. Re-running the whole corpus with the adapter constructed at
`silenceRmsDbfs = −120` (gate effectively disabled) via a **separate
diagnostic-only shim** (`tools/tracker-diagnostics/ConfigPlugin.cpp`) shows the
gate suppresses real detections but flips few core acquisitions:

- beat counts rise on many fixtures (e.g. aubio `palm_mute_metal` 5 → 19,
  `tapping_muting_only` 9 → 17, BTrack `line_input_low_level` 18 → 23);
- **aubio `palm_mute_metal` and aubio `sustained_chords` acquire only with the
  gate disabled** (0 → 1, at 2.998 and 2.249 bars);
- BTrack's core acquisition count is unchanged (4/11), because its failures are
  BPM-report failures that the gate does not touch.

These numbers are labelled `NOT default evidence` in
`experiments/silence-gate-off/**` and in `core-reasons.md` and must not be read
as a proposal to change the default gate.

## 6. Framing check: 128 vs 512

Rerunning at 512-frame blocks changes almost nothing for BTrack and only the
acquisition boundary for aubio:

| | BTrack 128 | BTrack 512 | aubio 128 | aubio 512 |
|---|---|---|---|---|
| F mean | 0.7099 | 0.6948 | 0.5357 | 0.5244 |
| acq core | 4/11 | 4/11 | 7/11 | 7/11 |
| worst core BPM err | 0.0234 | 0.0234 | 0.0133 | 0.0129 |
| causal avail mean | 12.9 ms | 17.0 ms | 5.7 ms | 5.7 ms |
| reported beats | 400 | 390 | 288 | 281 |

The causal-availability latency grows with the block size (the block end moves
later) exactly as EVAL-004 documented, while BTrack's event times and acquisition
are framing-independent. Aubio's acquisition on `missing_downbeats` and
`stop_start` shifts slightly (1.504 → 1.255 bars; 1.744 → 1.497) and BTrack
`sustained_chords` crosses the 2-bar line downward, so the acquisition *boundary*
is mildly framing-sensitive even though the gate count is not. Full per-fixture
detail: `core-reasons.md` and `*/block512/fixtures.csv`.

## 7. Event time vs causal availability (acquisition)

Acquisition is scored on the **event** clock (what the scorer has always done).
The trace adds the **causal availability** of the confirming beat so a real-time
consumer can see when the lock could actually have been *known*:

- BTrack's lock runs confirm ~12–14 ms after the confirming beat's event time
  (e.g. `clean_eighths`-class runs: event 4.656 s → available 4.669 s);
- aubio's confirm ~1–8 ms after (e.g. 4.630 → 4.640 s).

Availability is never folded into the event time and never changes a lock. The
parallel series is in every `*/beats/<fixture>.csv` and every block row of
`*/state/<fixture>.csv`.

## 8. Missing evidence / limits

1. `AcquiredAfter2Bars` vs `AcquiredWithin2Bars` is a threshold on the scorer's
   own `acquisitionBars` (`≤ 2.0`); the diagnostic does not redefine it.
2. `power_chords_distorted` has zero matches for BTrack (it reads ~90.7 BPM at
   the start) and 2/2 for aubio; the trace shows a real detection/phase problem,
   but this note does not adjudicate the corpus's distorted-chord synthesis.
3. `sustained_chords` and `tapping_muting_only` carry the known two-stage-fast-
   decay synthesis caveat (`CORPUS-ACOUSTIC-REVIEW.md`); the aubio silence-gate
   flip on `sustained_chords` may be entangled with it.
4. `insufficient onset evidence` is never the core cause; the label is available
   but unpopulated. Octave mismatch is likewise absent on core.
5. BTrack's internal *selection* of hop lag 42 was not forced to 41 (no vendor
   change), so the last causal step ("why not 41") is bounded inference, not a
   patched experiment.
6. Everything here is the synthetic corpus; SPEC 20 real-guitar play tests are
   still required.
7. The config experiment used the default-only adapter knob `silenceRmsDbfs`;
   no other knob was varied.

## 9. Proposed narrow next repair contract (for a future, separately-scoped task)

This note repairs nothing. If a follow-up is authorised, the narrowest testable
contracts consistent with this evidence are:

1. **BTrack BPM-report investigation (measurement first).** With the vendor
   unchanged, characterise whether reporting a tempo derived from the *emitted
   beat intervals* is stable and unbiased on the pinned click sweep and the
   corpus. This is an adapter-evidence question (SPEC 9.2: evidence, not a
   command), not a gate change, and must be labelled a variant. **Do not** merge
   it as default without an ADR; do not relax the 2 % gate.
2. **aubio phase-conflict fixtures.** For `clean_sixteenths`, `palm_mute_metal`
   and `power_chords_distorted`, independently audit the onset grid against the
   declared truth (the corpus onset audit already pending in
   `CORPUS-ACOUSTIC-REVIEW.md`) before attributing the miss to aubio.
3. **Silence-gate semantics.** Decide, with a SPEC-level rationale, whether the
   adapter gate should count as part of integration quality on the two aubio
   fixtures it flips; do not silently change the default.
4. **Framing.** Treat 128-frame as primary; 512-frame boundary movement on aubio
   is documented, not gated.

Each contract must state the exact artifact it will produce and must not touch
this note's gate definitions.

## 10. Artifact layout and bounds

`docs/research/tracker-acquisition/` (≈ 1.7 MiB, budget 5 MiB):

```
provenance.json                 base SHA, corpus/plugin hashes, G3=OPEN, experiment label
wav-hashes.txt                  19/19 original WAV sha256 + size OK
core-reasons.csv / .md          the per-core fixture reason tables (both framings + gate-off)
btrack/block128/{summary,acquisition}.json, fixtures.csv, beats/*.csv, state/*.csv
btrack/block512/{summary,acquisition}.json, fixtures.csv, beats/*.csv
aubio/block128/… , aubio/block512/… (same layout)
experiments/btrack_grid.csv                 the exact tempo->lag staircase
experiments/{btrack,aubio}/click-rate{48000,44100}/click_bpm_sweep_rate*.csv
experiments/silence-gate-off/{btrack,aubio}-block128/{summary.json,fixtures.csv}   NOT default
```

`state/*.csv` is decimated to ≤ 500 rows per fixture (`--state-max-rows`) but
**always keeps every beat block, every silence-state transition, and the first
and last block**, so the causal trace is preserved while the tree stays bounded.
`beats/*.csv` keeps every emitted beat with its exact event time, causal
availability and reported device sample.

## 11. Tests

`tools/tracker-diagnostics/tests/TraceReplayTests.cpp` (35 checks, 0 failures),
run by `build.sh`, pins:

- the exact BTrack grid arithmetic (`lag 42 → 123.046875`, `lag 41 →
  126.048018`; candidate 126 → 41 hops; 122/124 → 42 hops);
- lock-run start/confirmation timestamps and the separate availability clock;
- rejection of a 2.34 % tempo-disagreeing run while the positional match run is
  still 6;
- rejection of duplicate/backtracking beats;
- agreement of the independent replay with `rhythmeval::scoreFixture` and
  classification of the 123.05-on-126 case as `LockTempoAgreementFailure`;
- that `TraceRunner` reproduces the unmodified `BackendRunner` (beat times,
  availability, tempo/silence/phase, diagnostics) on a non-trivial fake backend.

## Handoff

G3 remains **OPEN**. No tracker is selected, no ADR, no production wiring. The
evidence tree above plus `task-notes/TRACK-004.md` are the handoff; the branch
head SHA is recorded in the task note.
