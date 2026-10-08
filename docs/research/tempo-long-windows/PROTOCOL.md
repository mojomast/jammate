# TRACK-007 predeclared protocol: longer-window synthetic characterization

Committed **before** any new fixture WAV is rendered or any tracker runs on new
material. Anything not stated here is not claimed. Changing a declared value after
this commit is a protocol deviation and must be reported as one.

## 1. Question and non-goals

TRACK-006 showed that the **unchanged fixed variant** (`60 / median(last 4
emitted intervals)`, ready after five events, base BPM forwarded before readiness)
has large regressions on short carved windows: a 1 s silence gap reports 82.687
BPM, clean 0 dB noise loses acquisition with 111.14 BPM, and funk noise reports
108.8 BPM. Those windows are only 5 s long and derived from one parent.

This follow-up asks, on **longer (about 33 s) paired synthetic windows** with sparse
attacks, a silence gap and additive noise, whether those behaviours persist, and
how often and when acquisition and readiness change. It is **diagnostic**. It
selects no backend, tunes no parameter, defines no guard, changes no default,
does not touch the fixed variant, adapters, scorer or metrics, and does not pool
results into a SPEC 19 pass/fail rate.

## 2. Base, branch and ownership

- Base commit `bcf540a` on `wp/TRACK-007-long-windows`; worktree
  `/home/mojo/projects/worktrees/TRACK-007-long-windows`.
- Owned: `tools/tempo-characterization/**`,
  `docs/research/TEMPO-LONG-WINDOWS.md`, `docs/research/tempo-long-windows/**`,
  `task-notes/TRACK-007.md`.
- Not edited: fixed variant, adapters, scorer/metrics, old corpus and artifacts,
  shared CMake, ledger, HANDOFF, DEVPLAN, processor, editor, DrumEngine.
- Scratch and WAVs live outside git in
  `/home/mojo/projects/guitars-build-resume/tmp/TRACK-007/`
  (`TMPDIR`; system `/tmp` is full). Only manifests, hashes, logs and results are
  committed.

## 3. Generator and provenance

| item | pin |
|---|---|
| synthesis source reused | `testdata/rhythm/tools/gen_fixtures.py` v1, sha256 `81df59dcac2b48f6861276561fd6dcd8eb892bbad1666257676e2c75682d5be8` |
| generator (this protocol) | `tools/tempo-characterization/gen_long_windows.py` v1, sha256 `9e76b10822f5ea732d39a3314fb87cb13869782f477924ccfe16e161c470e766` |

Reused unchanged from EVAL-001 v1: `Fixture`, `prepare`, `build_events`,
`compute_ground_truth`, `render` (Karplus-Strong open-string strings through the
mic capture chain, a 0.42 s room, −6 dBFS peak normalisation), `write_wav`,
`measure_wav` and `entry_for`. The sample-rate global is set per run to 48000 or
44100 Hz. Reused, but not as a corpus claim: the `Fixture` construction and seed
policy. New in this protocol: the strum pattern, the event-gap and sparse filters,
and the additive SNR noise. **These are synthetic clicks-and-plucks from the
existing guitar-like model. They are not recorded guitar, not a real-player
sample, and not an EVAL-001 corpus member.** Provenance is stated in each manifest
entry.

Seed policy: `seed = int(sha256("track007-long-windows|1|perf|<bpm>bpm")[:16], 16)`.
One performance per tempo is shared by all rates and by the regular, sparse and
gap controls, so the pattern humanisation draws are identical. The noise clip uses
the regular clip's rendered buffer plus its own noise seed `noise|<bpm>bpm`.

## 4. Fixed matrix (16 fixtures; no sweep)

Tempi: **96 BPM** and **126 BPM**, 4/4, quarter-note grid. Rates: **48000** and
**44100 Hz**, mono, 16-bit PCM. Controls:

| control | construction | ground truth |
|---|---|---|
| `regular` | one downstroke per quarter beat, chord cycle E A D G C Am per bar, velocity 1.0 on beat 1 and 0.8 otherwise, timing humanised ±8 ms | every grid beat has an onset |
| `sparse` | the regular events on beats 1 and 3 of each bar only (0-based beat index mod 4 in {0, 2}) | beats 2 and 4 are unplayed (structural) |
| `gap` | regular events with event time in [16.000, 17.000) s removed | beats inside the gap are unplayed (structural); no exact-zero silence |
| `noise` | the regular rendered buffer plus white Gaussian noise at **0 dB SNR** over the window RMS, re-normalised to −6 dBFS peak | identical onsets and beats to `regular` at the same tempo and rate |

Window lengths follow from the EVAL-001 `prepare()` formula with lead-in 0.35 s
and a 1.0 s tail: **13 bars at 96 BPM (about 33.85 s)** and **17 bars at 126 BPM
(about 33.73 s)**. Both are at least 32 s. Exact frames are in the committed
manifest. Capture settings match `clean_eighths` (mic, room 0.16, −66 dBFS floor,
−6 dBFS peak). Only the event pattern and noise differ.

Declared shape choices (not tuned): quarter strums rather than eighths keep one
attack per truth beat; the sparse pattern is half density; the 1 s gap is a
single event-free window at 16–17 s.

## 5. Ground truth and missingness

- Beats: the quarter-note metric grid from `prepare()`.
- Onsets: event times de-duplicated at 12 ms, as in EVAL-001.
- `silentBeats` and `silenceSpans`: unplayed beats within ±30 ms of no onset.
  These are **structural** unplayed windows, not acoustic silence.
- `trueSilenceSpans` is **empty for all 16 fixtures**. The capture chain keeps a
  −66 dBFS noise floor and room tail, so no exact-zero acoustic silence exists.
  Silence-acceleration and false-beat-in-true-silence metrics are therefore
  **NoTrueSilence / unmeasured**, and are reported as missing.
- Noise clips: no true silence, so the scorer reports `NoTrueSilence` for their
  false-beat coverage. This is the stated "noise silence unassessed" outcome. No
  noise clip is tagged `derived`, so no structural-noise exception is invoked.
- Missing measurements (no lock, no nominal BPM, not enough intervals) stay
  `null`/missing. They are never zero.

## 6. Scoring, backends and pins

Each fixture is scored at **block 128, uncompensated**, with the current pinned
CLI. Three backends: default BTrack, the fixed variant, and aubio. The diagnostic
beat series and method log come from the pinned diagnostic CLI for the default and
the variant. Pins (verified by hash at run time):

| role | path | sha256 |
|---|---|---|
| EVAL-007 scorer | `/home/mojo/projects/build-EVAL-007-integration/cli/rhythm-eval` | `caba565f6c6d8482443edf8d2a0a7ec40b6327ba99ccb2898f7213f8c28de552` |
| default BTrack plugin | `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-btrack.so` | `41e6476e60ab10832e961126fd6a17b13667cefa30c3ecd926d268c76bd65c6d` |
| aubio plugin | `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-aubio.so` | `61336277f3d13d7fe958cc4896c50b8761619186d0d6d6e3bc524decba593c41` |
| fixed variant | `/home/mojo/projects/build-TRACK-005-integration/libtempo-variant-btrack.so` | `920a30f9bb2c81b8ed743f817beb5dc51e9f90070de5ca0788a46ff1223a6717` |
| diagnostic CLI | `/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics` | `75b0d73bdfedb5028b65a0e171d371c7ea3bc97ed6cb0b5ed60c84b30f3edee2` |
| fixed-variant freeze (combined) | TRACK-005 `tempo-variant/provenance.json` | `cd8291e6d8ad49cae64fe43768ddc88e70679a2835dad78e62b5644b0503ec77` |

The variant's method is unchanged. Accepted interval range is `[0.25, 1.50]` s,
the ring holds four intervals, and readiness requires five emitted events. The
variant is `btrack-tempo-variant` with `--variant-not-default`.

Commands (run from the worktree; `PATH=/tmp/opencode/venv/bin:$PATH`,
`TMPDIR=/home/mojo/projects/guitars-build-resume/tmp`):

```bash
python3 tools/tempo-characterization/gen_long_windows.py --out "$TMPDIR/TRACK-007/corpus"
python3 tools/tempo-characterization/run_long_windows.py --corpus "$TMPDIR/TRACK-007/corpus" --out "$TMPDIR/TRACK-007/evidence"
python3 -m unittest discover -s tools/tempo-characterization/tests -p 'test_*.py'
```

## 7. Measured outcomes (declared before inference)

For each of 16 fixtures and each of 3 backends:

1. `acquired`, `acquisitionBars`, and **within 2 bars** := acquired and
   `acquisitionBars <= 2.0` (scorer fields; no re-definition).
2. `lockedBpm`, `bpmRelativeError` (whole-clip scorer measurement) and missing flags.
3. Variant readiness: first ready event time and availability time (block end);
   count of fallback beats before readiness; count of ready beats.
4. Persistence: among variant ready beats, the number of beats whose reported BPM is
   more than **2 %** from nominal, and the longest consecutive run of them.
5. Interval outliers from the emitted beat series of each backend. Expected interval
   is 1 beat for regular, gap and noise, and 2 beats for sparse. An interval outside
   ±10 % of the expected period is an outlier. An interval spanning the gap window
   (the last event before 16 s to the first at or after 17 s) is reported
   **separately** as a gap-spanning interval, not as an outlier.
6. Default and variant beat series: byte equality is **required**. Any mismatch
   invalidates that fixture's variant comparison and is a hard failure.

## 8. Exact comparison rules

Paired baseline: `regular` at the same tempo and rate. `sparse`, `gap` and `noise`
are each compared with it. The baseline is a comparator, not a claim of identical
audio, except that `noise` shares `regular`'s rendered audio.

- **Acquisition gained** (variant vs default): not acquired by default, acquired by
  the variant. **Lost**: acquired by default, not by the variant. Within-2-bar
  gains and losses are counted separately.
- **BPM regression**: both backends have a lock and nominal BPM, and
  `variantError − defaultError > 0.5` percentage points. **BPM gain**: the reverse
  with `> 0.5` points. Otherwise no change. The 0.5-point band is a stated
  diagnostic threshold, not a SPEC gate.
- Counts are per tempo, rate and control. **No pooled rate, no pass threshold,
  no significance claim, no core denominator.** Clips are overlapping synthetic
  views of one or two performances and are not independent trials.
- A `null` in either side makes that comparison **not evaluable**, not zero.

## 9. Hard validation (any failure stops the run)

- Manifest and every WAV hash, byte size, sample rate, channels, bit depth and
  frame count match the committed manifest. Generator and gen_fixtures pins match.
- Duration at least 32 s for every fixture; no full-scale clipping; finite samples.
- Noise realised SNR within 0.01 dB of 0 dB; noise onsets and beats equal regular.
- Sparse silent beats equal beats 2 and 4 of each bar. Gap silent beats include
  at least one beat in [16, 17) s. Regular has no silent beats.
- Scorer returns 16 fixtures per backend, source hash match, `blockFrames` 128,
  uncompensated only, no legacy stamps, and no core membership.
- Variant method log: one instance, exact block count per fixture, block indices in
  order, fallback equals base BPM, interval fields empty exactly when unmeasured.
- Default and variant beat series byte-identical on all 16 fixtures.

## 10. Next action after this protocol

Measure, then write `docs/research/TEMPO-LONG-WINDOWS.md`. Any candidate guard
requires a separate contract and new variant freeze. This protocol does not
authorise one.
