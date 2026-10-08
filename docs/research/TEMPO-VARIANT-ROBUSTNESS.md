# Fixed BPM-report variant: paired robustness (TRACK-006)

**The unchanged variant has mixed results and clear regressions on short,
perturbed windows.** Default BTrack and the variant emit byte-identical beat
series on all 24 clips. The variant loses six per-clip acquisitions, gains one,
and substantially worsens the reported BPM on the carved silence gap and three
noise clips. This is diagnostic evidence. **G3 remains OPEN; no backend is
selected and no gate or default changes.**

## Executed contract and provenance

Base: `45fa333`, branch `wp/TRACK-006-variant-robustness`. All three backends were
executed over the unchanged EVAL-003 derived manifest using the **current
EVAL-007 integration scorer**, block **128**, uncompensated, no legacy stamps.
The manifest contains **24 total clips: 22 perturbations and two matched parent
baselines**, not 24 perturbations plus additional parents. Every perturbation
uses its declared same-parent, exactly same-source-truncation baseline; no
full-length original parent is substituted. All 24 WAV hashes, byte sizes and
WAV framing agree with the manifest; the manifest hash matches EVAL-005's
historical pin. Both original parent WAVs are also re-hashed, and each baseline's
PCM is byte-identical to its declared original-parent source slice.
The two source windows are 5 seconds;
padding and speed warps legitimately change some output lengths.

Read-only binaries were reused, without rebuilding:

| binary | actual SHA-256 |
|---|---|
| `/home/mojo/projects/build-EVAL-007-integration/cli/rhythm-eval` | `caba565f6c6d8482443edf8d2a0a7ec40b6327ba99ccb2898f7213f8c28de552` |
| `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-btrack.so` | `41e6476e60ab10832e961126fd6a17b13667cefa30c3ecd926d268c76bd65c6d` |
| `/home/mojo/projects/build-EVAL-005/main-core/librhythm-eval-aubio.so` | `61336277f3d13d7fe958cc4896c50b8761619186d0d6d6e3bc524decba593c41` |
| `/home/mojo/projects/build-TRACK-005-integration/libtempo-variant-btrack.so` | `920a30f9bb2c81b8ed743f817beb5dc51e9f90070de5ca0788a46ff1223a6717` |
| `/home/mojo/projects/build-TRACK-004-integration/diag/tracker-diagnostics` | `75b0d73bdfedb5028b65a0e171d371c7ea3bc97ed6cb0b5ed60c84b30f3edee2` |

`tempo-variant-robustness/provenance.json` records the executed argv, actual
binary/source/archive hashes and authenticated input hashes. Current scorer
source hashes match EVAL-007 integration pins. The five fixed variant source
hashes match both TRACK-005 provenance and integration pins; corrected combined
freeze remains `cd8291e6d8ad49cae64fe43768ddc88e70679a2835dad78e62b5644b0503ec77`.
The BTrack/aubio adapter headers and sources match the read-only EVAL-005 build
source export. Default BTrack and aubio reproduce **all 24 historical EVAL-007
per-fixture fields exactly except CPU time**. Historical artifacts are retained.

The predeclared method remains `60 / median(last 4 consecutive valid emitted
beat intervals)`, ready after five events; fallback forwards base BPM. Accepted
interval range remains `[0.25, 1.50]` seconds, with no octave correction, truth
input, future audio or parameter search. See
[TRACK-005 method and earlier regressions](TEMPO-REPORT-VARIANT.md).

## Same-frame comparison

All BPM errors below are **whole-clip scorer measurements**, as percentages;
they are not ready-only variant accuracy. `NA` stays missing. Acquisition is
per clip, with no core denominator or release-gate conclusion. `B/V/A` means
default BTrack / fixed variant / aubio. The clean rows pair with
`clean_eighths__baseline`; the funk rows pair with `syncopated_funk__baseline`.
Exact fixture IDs and all per-backend paired deltas are in
`comparison.json` and `degradation.{json,csv}`.

| parent / perturbation | BPM error B / V / A (%) | acquired B / V / A | ready variant beats |
|---|---|---|---|
| clean / baseline | 2.3438 / 2.3429 / 1.4505 | no / no / yes | 5 |
| clean / noise 20 dB | 2.3438 / 1.1668 / 1.4575 | no / no / yes | 5 |
| clean / noise 10 dB | 2.3438 / 1.1690 / 1.4460 | no / no / yes | 5 |
| clean / noise 0 dB | 0.0381 / **11.7945** / 36.7759 | **yes / no** / no | 5 |
| clean / level −20 dB | 2.3438 / 1.2722 / 1.4506 | no / no / yes | 6 |
| clean / level −40 dB | 2.3438 / 0.0378 / 1.4484 | no / no / no | 5 |
| clean / level −60 dB | NA / NA / NA | no / no / no | 0 |
| clean / clipping 0.5 | 2.3438 / 2.3429 / 1.4081 | no / no / yes | 5 |
| clean / clipping 0.25 | 0.0381 / 0.0378 / 1.3442 | **yes / no** / yes | 5 |
| clean / clipping 0.125 | 0.0381 / 0.0378 / 1.3548 | **yes / no** / yes | 5 |
| clean / onset offset 40 ms | 2.3438 / 0.0378 / 1.3858 | **no / yes** / yes | 6 |
| clean / onset offset 80 ms | 2.3438 / 0.0356 / 1.3053 | no / no / yes | 5 |
| clean / leading silence 0.5 s | 2.3438 / 0.0378 / 1.3802 | no / no / yes | 6 |
| clean / trailing silence 0.5 s | 2.3438 / 2.3429 / 1.4505 | no / no / yes | 5 |
| clean / silence gap 1 s | 2.3438 / **34.3751** / 1.4132 | no / no / no | 3 |
| clean / drop every 2 | 0.0381 / 0.0381 / 1.5342 | no / no / yes | 5 |
| clean / drop every 4 | 2.3438 / 0.0400 / 1.1472 | no / no / yes | 5 |
| clean / syncopation burst 1 | 2.3438 / 2.3429 / 1.4505 | no / no / yes | 5 |
| clean / syncopation burst 4 | 2.3438 / 2.3429 / 1.3964 | no / no / yes | 5 |
| clean / tempo step 1.25 | NA / NA / NA | no / no / no | 4 |
| clean / tempo step 0.85 | NA / NA / NA | no / no / no | 6 |
| funk / baseline | 1.8243 / 0.3093 / 0.8024 | **yes / no** / no | 5 |
| funk / noise 10 dB | 1.8243 / **2.8568** / 0.7848 | **yes / no** / no | 4 |
| funk / noise 0 dB | 1.8243 / **2.8568** / 1.2689 | **yes / no** / no | 4 |

Per-clip acquisition counts are BTrack **6/24**, variant **1/24**, aubio
**15/24**. The variant loses `clean_eighths__noise_snr0db`, `clip_0.25`,
`clip_0.125`, and all three funk clips; it gains only
`clean_eighths__offset_40ms` (1.5019 bars). Whole-clip BPM errors are within
the documented 2% diagnostic band on **7/21**, **12/21**, **20/21** measured
steady clips respectively. These counts describe overlapping synthetic windows,
not independent trials or release-gate pass rates.

### Actual regressions and causal availability

1. **Carved silence gap:** the variant's whole-clip locked BPM is **82.68734**
   versus default **123.046875**, truth 126. Its first ready value becomes
   available at **3.728 s**, after an emitted interval of **1.4860625 s**.
   That interval is inside the frozen 1.50-second bound, so it is accepted and
   does not reset the ring. The three ready beats report **82.68734, 82.68734,
   95.70173 BPM**. This is low/sub-octave reporting, not an exact half-time
   lock. Whole-clip BPM error worsens by **32.0314 percentage points** versus
   default and **32.0323 points** versus the variant's matched baseline.
2. **Clean 0 dB noise:** default acquires at 1.4863 bars; the variant does not.
   Variant locked BPM becomes **111.138985**, error **11.7945%**, versus
   default **126.048019**, error **0.0381%**. Ready BPM spans 108.8004–123.0480.
3. **Funk noise 10/0 dB:** variant locked BPM **108.800362** has **2.8568%**
   error versus default **1.8243%** and variant baseline **0.3093%**. Both lose
   acquisition. Ready-only BPM spans **97.5081–109.9572**, all below the 112 BPM
   truth, first ready availability **3.032 s**.
4. **Clipping 0.25/0.125 and funk baseline:** whole-clip BPM error improves,
   yet acquisition is lost. The retained diagnostic clauses show
   `TempoOutsideBand` with only **5/7**, **5/7**, and **3/7** agreeing beats in
   the longest match runs. A good whole-clip median does not prove that the
   acquisition confirmation window had acceptable BPM. No acquisition rule
   was relaxed to hide these failures.

No half/double-time classification changes occur on these inputs. The earlier
original `sustained_chords` regression remains historical evidence; that fixture
is absent from this two-parent derived set, so this run neither retests nor
repairs it.

## Readiness, fallback and exact beats

The current CLI constructs **one plugin instance**, resetting it per fixture in
manifest order. Its original per-block log is retained losslessly as
`raw-method/instance_0.csv.gz`. The tool verifies every block index, exact
expected block count and each reset boundary before splitting the log. It checks
every unready block forwards base BPM and every missing interval has an empty
value. `method-beats.csv` carries event and causal availability clocks;
`method-summary.json` carries ready/fallback block and beat counts, first-ready
availability, ready-only BPM range/median and stale-ready non-beat counts.

23/24 clips become ready, at availability times **2.264–3.728 s**; the −60 dB
clip emits no beats and never becomes ready. On the other clips the first four
events use fallback. No gap/malformed/out-of-order reset occurs; all subsequent
intervals are within the fixed range. Readiness indicates method availability,
not confidence or correctness; between beats the ready candidate persists stale.

The current-process method log's beat event/block-end/block-index tuple matches
the diagnostic beat CSV on every emitted beat. All 24 default/variant CSV pairs
are **byte-identical**, including device timestamps and availability; hashes are
in `beat-equality.json`. Current detection, phase, silence counts, timing and
allocation counts also agree per fixture. Only BPM-dependent scored fields
change; CPU time is a resource measurement. The diagnostic binary is an auxiliary
beat-series producer with historical scoring/coverage semantics. **Current
scores and coverage come exclusively from `raw/*/block128/results.json`.**

## Missingness, coverage and limitations

- 3240 paired metric rows (3 × 24 × 45). BTrack: **868 measured / 202 missing /
  10 noise-unassessed**; variant: **853 / 217 / 10**; aubio: **890 / 180 / 10**.
  Missing locks, acquisition times, phase matches and absent nominal BPM remain
  `null`/empty in derived evidence. Raw CLI placeholders are not zero-passes.
- Five noise clips carry `NotAssessedStructuralNoise`, with raw counts retained.
  Inherited structural spans do not establish acoustic silence. The legacy raw
  `trueSilenceMeasured` flag denotes span presence; use the current coverage and
  informative flag. Paired silence differences on noise are blocked.
- Only the carved gap has measured silence-acceleration evidence for BTrack and
  the variant: both report **0 BPM increase**, with no false beat inside the
  true-silence spans. Aubio has insufficient acceleration evidence. The severe
  low-BPM regression therefore coexists with a zero acceleration proxy; neither
  value establishes the SPEC silence gate. Other acceleration cells are missing.
- The tempo-step clips have no nominal BPM; their BPM error is missing. They
  have different durations, and whole-clip medians do not establish causal step
  following. The raw ready trajectory is available, without a new response gate.
- Offset/leading/trailing-silence windows shift content or length. Their raw
  count/acquisition deltas retain the inherited commensurability caveats.
- Derived syncopation-burst fixtures do not populate the scorer's name/tag-based
  `hasSyncopation` proxy; it remains missing. No Musical Clock or audition runs.
- Synthetic, short windows; zero core fixtures. No absolute SPEC 19 acquisition,
  BPM or half/double release gate is measured. Real guitar, longer sparse/gap
  windows, live clock behavior and Windows/hardware tests remain necessary.
- Same framing/scorer does not equalize backend internals: BTrack resamples to
  44.1 kHz. The offline variant logger adds resource overhead, so CPU measurements
  are retained without a speed ranking. No statistical independence is claimed.

## Reproduction and validation

Run from `/home/mojo/projects/worktrees/TRACK-006-variant-robustness`:

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
python3 tools/tempo-variant/run_paired.py --out "$TMPDIR/TRACK-006-reproduction"
python3 tools/tempo-variant/tests/test_paired.py
python3 -m unittest discover -s tools/tempo-variant/tests -p 'test_*.py'
python3 tools/rhythm-eval/tools/test_run_robustness.py
/home/mojo/projects/build-TRACK-005-integration/TempoVariantTests
```

Choose a nonexistent output directory; the tool refuses to overwrite evidence
and fails closed on pins, pairing, input bytes, backend identity, framing,
fixture completeness, stale coverage, method-log gaps or beat mismatch. Executed:
**9 paired tests**, **17 total variant Python tests**, **29 robustness tests**,
**67 C++ checks**, and **22 fail-closed CLI checks**, all passing. Paired tests authenticate retained artifacts
and the decompressed raw log, and exercise malformed evidence and missing/noise
semantics. Evidence tree is approximately **3.91 MiB**, below 5 MiB before small
test receipts/README. `artifact-hashes.txt` authenticates retained run artifacts.

## Next action

A separately scoped, predeclared follow-up should characterize the accepted
long/irregular intervals and readiness/acquisition interaction on longer paired
sparse/gap/noise windows. Any proposed guard needs a **new named variant and
freeze**, preserving this evidence and the fixed method's regular-material gains.
No production integration or backend selection follows from this measurement.
