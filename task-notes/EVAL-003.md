# EVAL-003 — Robustness perturbations (derived corpus)

## Goal

Add the robustness axis to the tracker comparison: hold a base fixture's audio
constant, vary exactly one thing, and record how the score degrades. DEVPLAN.md
§14 / gate G3 ask for noise, level, clipping, timing and syncopation probes plus
a normalised table. The base corpus already contains whole-fixture examples of
every SPEC.md §12.2 case, but two fixtures are two different performances — they
cannot show *where* a backend falls over under identical degradation. This task
builds the controlled perturbation set and proves it is what it claims to be.

**Scope note (important).** This work package was narrowed by the orchestrator:
it is *bounded deterministic perturbation generation and integrity/semantic
testing only*. The CLI integration, the degradation-curve emitter and the
normalised comparison table are owned by EVAL-004 (metrics/time correction) and
are **not produced here** — see "Not done here, and why" below. Nothing in
`tools/rhythm-eval/{Metrics,Manifest,BackendRunner,main}.*`,
`tests/jam/RhythmEvalMetricsTests.cpp`, any CMake file, the base corpus, `src/`
or `docs/` was modified.

## Base commit

Worktree `/home/mojo/projects/worktrees/EVAL-003`, branch `wp/EVAL-003`.

- Fork point: `c68df60`
- `main` at the time of work: `eac59ba` (aubio adapter + revised eval harness).
  `git merge main` was a **fast-forward**, so the branch was brought up to
  `eac59ba` with no reset and no deletion.
- Work commit (generator + derived set + tests): `a067c12`.
- Final branch tip: recorded in the EVAL-003 handoff response.

## Files changed

New, all inside the allowed set (nothing else appears in `git status`):

```
tools/rhythm-eval/tools/make_derived.py        # generator, stdlib only
tools/rhythm-eval/tools/test_make_derived.py   # 16 stdlib tests
testdata/rhythm/derived/                        # 24 WAVs + manifest.json
tests/jam/RhythmDerivedTests.cpp                # suite jam.RhythmDerived
task-notes/EVAL-003.md
```

No CMake change was needed: `jam-core/CMakeLists.txt` already globs
`tests/jam/*.cpp` and discovers suites from `JAM_TEST(...)`, so
`RhythmDerivedTests.cpp` auto-registers as the ctest entry `jam.RhythmDerived`.
The `testdata/rhythm/derived/` directory is new and separate from the base
corpus; `testdata/rhythm/{wav,manifest.json,tools,README.md}` are untouched.

## Contract implemented

`make_derived.py` reads a finished base fixture out of
`testdata/rhythm/manifest.json`, truncates it to a **5.0 s** window (recorded per
file), applies **one** documented perturbation, and writes the WAV plus a
manifest entry into `testdata/rhythm/derived/`. Standard library only (`wave`,
`struct`, `math`, `random`, `json`, `hashlib`, `argparse`); no numpy.

### Audio-vs-score semantics (the part that is easy to get wrong)

A perturbation is classified by how the *audio timeline* relates to the *score
timeline*, and the manifest records it in `groundTruth.policy` /
`groundTruth.timingAffectsTruth`:

| policy | perturbations | beats | onsets | audio timeline |
|---|---|---|---|---|
| `inherit` | noise, level, clipping, baseline | copied from baseline exactly | copied exactly | unchanged length |
| `edit-onsets` | drop_onset, syncopation_burst, silence_gap | unchanged | added/removed | unchanged length |
| `shift` | onset_offset, leading_silence | `+shift` exactly | `+shift` exactly | delayed / prepended |
| `append-silence` | trailing_silence | unchanged | unchanged | appended |
| `warp` | tempo_step | `phi(t)` | `phi(t)` | resampled by `phi` |

The rule enforced in code and re-derived in the tests: **the score may only move
as much as the audio moved.** `onset_offset` is a real sample delay (drop-tail,
length-preserving), so it moves the beats. Leading silence is a real prepend, so
it moves the beats. Trailing silence adds silence after the last event, so it
does not. The tempo step is a coherent tape-speed warp
`phi(t) = t` for `t <= Ts`, `Ts + (t-Ts)/r` after, applied to both the samples
and the event times — never a beat re-labelling.

`trueSilenceSpans` is carried through the same timeline change (shift / warp /
append / carved gap) so it names where the silence actually is; for the `noise`
perturbation it is *structural* only and the added floor is documented, because
noise destroys "near-silent by construction".

## Perturbation set

Parents: `clean_eighths` (clean steady 4/4, one onset per beat) carries the full
grid; `syncopated_funk` (onset-rich, ghost notes) carries a short noise curve so
the axis is not a single-recording artefact. Window `[0.0, 5.0 s)` for both.
`sustained_chords` and `tapping_muting_only` were deliberately **not** used as
parents: they declare 8.73 s / 9.96 s of their duration genuinely silent, so they
are not informative for anything that depends on continuous playing (as flagged
in the brief).

| name | parameter values | ground truth | bytes |
|---|---|---|---|
| `clean_eighths__baseline` | window 5.0 s | inherit | 480 044 |
| `clean_eighths__noise_snr{20,10,0}db` | SNR ∈ {20, 10, 0} dB | inherit | 480 044 ×3 |
| `clean_eighths__level_{-20,-40,-60}db` | gain ∈ {−20, −40, −60} dB | inherit | 480 044 ×3 |
| `clean_eighths__clip_{0.5,0.25,0.125}` | ceiling ∈ {0.5, 0.25, 0.125} FS | inherit | 480 044 ×3 |
| `clean_eighths__offset_{40,80}ms` | delay ∈ {40, 80} ms | shift by N/sr | 480 044 ×2 |
| `clean_eighths__lead_silence_0.5s` | +0.5 s | shift by +0.5 s | 528 044 |
| `clean_eighths__trail_silence_0.5s` | +0.5 s | append-silence | 528 044 |
| `clean_eighths__silence_gap_1s` | 1.0 s carve | edit-onsets | 480 044 |
| `clean_eighths__drop_every{2,4}` | K ∈ {2, 4} | edit-onsets | 480 044 ×2 |
| `clean_eighths__syncop_burst{1,4}` | 1 isolated, 4 over 2 bars | edit-onsets | 480 044 ×2 |
| `clean_eighths__tempo_step_{1.25,0.85}` | r ∈ {1.25, 0.85} at beat 4 | warp | 427 292 / 526 508 |
| `syncopated_funk__baseline` | window 5.0 s | inherit | 480 044 |
| `syncopated_funk__noise_snr{10,0}db` | SNR ∈ {10, 0} dB | inherit | 480 044 ×2 |

Implementation specifics:

- **noise** — white Gaussian, scaled to a *measured* target SNR against the
  window RMS; measured SNR is recomputed from committed bytes and matches.
- **clipping** — drive the window to full scale, then hard-clip at the ceiling.
  Clipped-sample fraction is measured, not intended.
- **onset_offset** — delay-and-drop-tail, so length is preserved exactly (the
  test checks `out[i] == parent[i-n]` and `out[:n]==0`).
- **drop_onset** — remove the attack by **linear interpolation across
  `[t-5 ms, t+60 ms]`**, endpoints preserved. No whole sustained note is masked
  (the window is shorter than a beat, never reaches the next onset, and earlier
  notes' tails carry across). The only side effect is that the local noise floor
  inside that window is smoothed — documented in the manifest note.
- **syncopation_burst** — overlay a real onset copied from the parent at an
  offbeat, at 0.55 gain; injected times become ground-truth onsets. Maps to
  SPEC §19 "no tempo jump from one isolated syncopated event".
- **tempo_step** — consumes the whole source window and emits `phi([0,D])`; no
  event dropped, no silence padded. Default output length is the natural warped
  length (shorter when `r>1`, longer when `r<1`).

## Disk impact

| | bytes |
|---|---|
| base corpus (`testdata/rhythm/`) | 20 954 918 |
| derived WAVs (24 clips) | 11 610 856 (11.07 MiB) |
| derived `manifest.json` | 52 225 |
| **derived total** | **11 663 081 (11.12 MiB)** |

Budget was ≤ ~15 MB; committed usage is 11.12 MiB, i.e. 0.56× the base corpus.
Per-file truncation is recorded (`truncation.sourceStartFrame` /
`sourceEndFrame` / `frames`), and the window is declared in
`conventions.windowSeconds`.

**Grid reductions (reported, not hidden).** A fuller sweep on paper would have
used a 8–10 s window, a full grid on *both* parents, and 5 noise / 5 level / 4
clip / 3 offset / 3 burst / 3 tempo points. To stay under budget with a second
parent, I reduced to: one parent with the full grid and one with a 2-point noise
curve; noise 3 points {20,10,0}; level 3 {−20,−40,−60}; clipping 3
{0.5,0.25,0.125}; offset 2 {40,80 ms}; tempo step 2 ratios at a single anchor;
window 5.0 s. Omitted values: SNR {30,−5}; gain {−10,−50}; ceiling {0.75}; a
third tempo anchor; full grids on `syncopated_funk`.

## Not done here, and why (contract limit, reported rather than faked)

The original brief asked for `docs/research/results-robustness.json`,
`summary-robustness.md`, `degradation.csv` and a normalised comparison table.
The corrected, orchestrator-steered scope for this WP removed CLI/normalisation
integration and reassigned scoring to EVAL-004. Concretely:

- `tools/rhythm-eval/main.cpp`, the `tools/rhythm-eval/CMakeLists.txt` CLI
  target, and `Metrics.*` are **not** editable by this worker, so a derived-aware
  comparison command cannot be wired here without violating ownership.
- I therefore did not emit degradation curves or a normalisation formula. A
  normalisation of the form `0 = at the SPEC §19 gate, 1 = comfortably inside`
  requires the per-metric gate thresholds from the scoring layer (EVAL-004), and
  inventing them here would be exactly the "invented gate thresholds" the brief
  forbids. **Recommendation for the integration consumer:** group derived
  fixtures by `parentFixture`, compare each to its `pairedBaseline`, and emit one
  curve row per (backend, perturbation kind, parameter, metric). The
  `groundTruth.policy` field tells the consumer which truth to use.

Also not countered as audio perturbations, deliberately:

- **octave ambiguity** is not an acoustic perturbation — it is a half/double-time
  *scoring* phenomenon, already measured by the existing metrics on the base
  corpus. Inventing an audio transform for it would counterfeit a metric.
- **gradual tempo ramp** already exists as committed base fixtures
  (`accelerando`, `ritardando`, analytic non-uniform grids); a derived ramp would
  duplicate them, and the new tempo *step* is the case the base corpus lacks.
- **noisy onset injection** beyond the syncopation burst was dropped (see grid
  reductions).

## Tests executed / Test results

Build (standalone jam-core, no GPL lanes; build root on `/home` because `/tmp`
is a full tmpfs):

```
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/build-EVAL-003/tmp
cmake -S jam-core -B /home/mojo/projects/build-EVAL-003/build -G Ninja
cmake --build /home/mojo/projects/build-EVAL-003/build
ctest --test-dir /home/mojo/projects/build-EVAL-003/build --output-on-failure
```

- Configure: auto-registered 7 suites, including `RhythmDerived`.
- Build: clean.
- `ctest`: **7/7 passed** (exit 0); existing suites unaffected
  (`RhythmCorpus`, `RhythmEvalMetrics`, `MusicalClock`, `AnalysisAudioRing`,
  `DrumTransportAdapter`, `RtSignal` all green).
- `jam.RhythmDerived`: **8 tests, 1456 checks, 0 failures.**
- My own source under `-Wall -Wextra -Wpedantic`: **zero warnings**
  (`g++ -std=c++17 -Wall -Wextra -Wpedantic -c tests/jam/RhythmDerivedTests.cpp`).
- Python: `python3 tools/rhythm-eval/tools/test_make_derived.py` →
  **16 tests, OK** (includes two-generation byte determinism and `--check`).
- `make_derived.py --check` against the committed set: **byte-reproducible**.

### What the C++ suite actually checks (independent of the generator)

- Manifest JSON shape; every tag in the closed derived vocabulary; every file
  present; declared `bytes` == file size; **SHA-256 recomputed** over the bytes.
- `corpus.baseManifestSha256` == the base `manifest.json` on disk, and every
  `parentSha256` == the base fixture's declared hash.
- One paired baseline per parent; every sibling points at it.
- Amplitude perturbations inherit `beats`/`onsets` **exactly** element-wise.
- Timing perturbations: exact `+shift`; warp recomputed from `phi`; post-anchor
  spacing == pre-anchor spacing ÷ ratio (a coherent non-uniform warp).
- Evidence edits keep the grid; drop is a subset, burst a superset, gap removes
  only inside the span.
- Acoustic: baseline PCM == parent frame slice; recomputed noise SNR; exact
  per-sample level mapping; recomputed clip fraction; exact sample delay; drop
  windows quieter / burst windows louder than baseline; carved gap near-silent.
- Required axes present (≥3 noise/level/clipping points, timing perturbations).

### Prove-it-can-fail

On a scratch copy of the committed derived set (committed files untouched):

- Setting the `offset_40ms` truth back to the baseline beats and relabelling the
  tempo step made `timingPerturbationsTransformTruth` **fail** (21 checks,
  exit 1).
- Flipping one byte of a committed WAV made `manifestIntegrity` **fail**
  (`sha == f.sha` mismatch, exit 1).

## Evidence

- Derived manifest SHA-256: `3bb6d350f534b6d6f3208ca1ef5c0f29cf7c7069b3a9771c13f191385c5cc3cd`
- Base manifest SHA-256 recorded in the derived manifest:
  `06fe2c4356dd411a90b5e4948ebeb00eeb68d82f42d26b5ad25f4797c530fab1`
- Work commit: `a067c12`
- The existing EVAL-002 manifest reader parses the derived manifest unchanged
  (verified with a throwaway `parsecheck` linked against
  `tools/rhythm-eval/Manifest.cpp`): 24 fixtures, `toTruth` OK. `tempoProfile`
  is `constant` / `linear-ramp` except `tempo-step` for the warped clips, which
  the reader accepts as an opaque string; `isSteady()` is then false for those,
  which is the correct reading.

## Known limitations

- `trueSilenceSpans` for the `noise` perturbations is structural, not
  re-measured: added noise fills what was near-silent. Consumers must not score
  silence metrics on those clips from that field (documented in the manifest).
- Tempo-step resampling is tape-speed, so pitch rises/falls with tempo. The
  measured quantity is onset timing; this is documented rather than
  pitch-corrected.
- Windows are 5.0 s. That is enough to acquire, show a step and hold/lose phase,
  but it is shorter than the base fixtures, so absolute acquisition times are not
  comparable to whole-fixture runs — only to the paired baseline.
- One parent carries the deep grid; `syncopated_funk` carries only a 2-point
  noise curve (budget).
- The `jam.RhythmDerived` suite runs inside `jamTests`, which the CMake lists
  compile with `-Wall -Wextra` only; `-Wpedantic` was checked manually.
- The Python tests are not wired into ctest (that would require editing
  `jam-core/CMakeLists.txt`, which this WP does not own); run them directly.

## Integration notes

- **Base corpus untouched.** `testdata/rhythm/{wav,manifest.json,tools,README.md}`
  show no diff.
- **Auto-registration.** `RhythmDerivedTests.cpp` is picked up by the existing
  `tests/jam/*.cpp` glob; no CMake edit.
- **For the comparison consumer (EVAL-004 / ADR):** point the harness at
  `testdata/rhythm/derived/manifest.json`, group by `parentFixture`, and always
  diff against `pairedBaseline`. Use `groundTruth.policy` to select the truth.
  The derived tag vocabulary is separate; derived clips are **not** in the base
  `tagVocabulary.core` denominator for the SPEC §19 "≥ 95% of core fixtures"
  gate (they carry `core_parent` instead).
- **Determinism:** safe to regenerate; fixed `sha256`-derived seeds, canonical
  JSON, and `--check` verifies bytes.

## Final commit SHA

Work commit `a067c12`; the branch tip that includes this note is reported in the
EVAL-003 handoff. `git log -1 --format=%H` on `wp/EVAL-003` gives the tip.
