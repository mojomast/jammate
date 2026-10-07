# EVAL-001

## Goal

Build the evidence set used to choose the production rhythm tracker: a
19-fixture guitar rhythm corpus with machine-readable ground truth, a
deterministic generator that produces it, and a C++ suite that validates it.

`SPEC.md` §12 forbids choosing a tracker permanently until it has been tested on
guitar-specific material, and `DEVPLAN.md`'s EVAL-001 / gate **G3** require a
guitar corpus with ground truth before BTrack, aubio and BeatNet can be compared.
The application listens to a guitar through an interface, line input or
microphone — not a mastered stereo song — so a corpus scored on commercial songs
would measure the wrong thing. None of that corpus existed, which meant the
tracker decision could not be made at all.

## Base commit

`f04c055` (`arch(jam-core): library identity is a positional index, not a
string id`). Branch `wp/EVAL-001`.

## Files changed

Created, all within the allowed set:

- `testdata/rhythm/manifest.json` — the machine-readable index.
- `testdata/rhythm/README.md` — what each case probes, why it matters for
  tracker selection, how to regenerate, the seed policy, and the limitations.
- `testdata/rhythm/tools/gen_fixtures.py` — deterministic generator, Python
  standard library only.
- `testdata/rhythm/wav/*.wav` — 19 fixtures, 16-bit mono PCM @ 48 kHz.
- `tests/jam/RhythmCorpusTests.cpp` — suite `RhythmCorpus`, 11 tests.
- `task-notes/EVAL-001.md` — this file.

**No existing file was modified.** `tests/jam/RhythmCorpusTests.cpp` is
auto-discovered by the existing `jam-core/CMakeLists.txt` `GLOB
CONFIGURE_DEPENDS` and its `JAM_TEST(RhythmCorpus, ...)` registrations
auto-register the `jam.RhythmCorpus` ctest entry, so no build file needed editing
and none was touched.

## Contract implemented

### Fixture synthesis

Karplus-Strong per string (shaped noise burst into a delay line with a lowpass
feedback loop, plus a one-pole allpass for fractional-delay pitch accuracy),
summed through a physically-motivated capture chain: amp drive → cabinet tone →
body resonance (mic only) → room (mic only) → capture EQ → noise/hum → level →
limiter → 16-bit PCM.

Per-fixture character varies pick/strum burst length and brightness, string
count, open-position fret offsets, per-string velocity spread, pick hardness, and
a small detune. **Dynamics are real**: every event carries its own velocity,
phrases accent and decay, and amplitude is never constant.

Impairments applied on top: line-level noise (white + pink), mic EQ tilt (body
resonances, 95 Hz high-pass, 7.4 kHz low-pass), 50/100/150 Hz mains hum on line
inputs, hard clipping at full scale, and low input level.

Format: 48 000 Hz, mono, 16-bit PCM. Fully deterministic — fixed seeds derived
from the fixture name, nothing reading the clock, pid or filesystem.

### The 19 required cases

One fixture per SPEC §12.2 bullet / DEVPLAN EVAL-001 case, each carrying the
scenario tag plus qualifiers. **There is no duplicate in the list to collapse**:
SPEC lists 19 bullets, DEVPLAN lists the same 19, and the two line-input
variants probe genuinely different failures (gain staging vs input overload), so
they are separate fixtures. 19 bullets → 19 fixtures.

### Ground truth

Per fixture: `sampleRate`, `durationSeconds`, `meter` (numerator, denominator,
beatUnit, beatsPerBar), `nominalBpm` or `bpmStart`/`bpmEnd`, `beats`,
`onsets`, `downbeats`, `silentBeats`, `silenceSpans`, `subdivision`,
`scenarioTags`, `notes`, `license: "CC0-1.0"`, `provenance`, `sha256`, and
`signal` (measured peak/RMS/clipped-fraction read back from the committed bytes).

`beats` for the ramp cases are **non-uniform**, derived from the analytic
integral of the tempo function — see below.

### Manifest shape

Single object with `schemaVersion` (1), a `generator` object recording name,
version, language, dependency list, synthesis method, **seed policy** and
command, plus `corpus`, `conventions`, `tagVocabulary`, totals and the
`fixtures` array. Written with `sort_keys=True` and `separators=(",", ":")` on a
single line so it does not churn in git diffs.

## Fixture inventory

19 fixtures, 218.271663 s total, **20 954 918 bytes (21.0 MB)** on disk.

| Fixture | BPM | Meter | Tags | Onsets | Beats | Seconds | Bytes | pk/rms dBFS |
|---|---|---|---|---|---|---|---|---|
| `accelerando` | 108→146 | 4/4 | `tempo_ramp` `humanised` | 48 | 24 | 12.63 | 1 212 246 | −6.0/−22.8 |
| `arpeggio` | 120 | 4/4 | `core` `steady_tempo` `humanised` | 81 | 20 | 11.35 | 1 089 644 | −7.0/−32.3 |
| `blues_shuffle` | 108 | 4/4 | `core` `steady_tempo` `swing` `humanised` | 40 | 20 | 12.46 | 1 196 310 | −6.0/−23.3 |
| `clean_eighths` | 126 | 4/4 | `core` `steady_tempo` `humanised` | 20 | 20 | 10.87 | 1 043 930 | −6.0/−25.6 |
| `clean_sixteenths` | 126 | 4/4 | `core` `steady_tempo` `humanised` | 80 | 20 | 10.87 | 1 043 930 | −7.0/−29.3 |
| `compound_6_8` | 96 | 6/8 | `steady_tempo` `humanised` | 36 | 12 | 8.85 | 849 644 | −6.5/−22.8 |
| `line_input_clipping` | 126 | 4/4 | `clipping` `line_input` `steady_tempo` | 20 | 20 | 10.87 | 1 043 930 | −0.0/−12.1 |
| `line_input_low_level` | 126 | 4/4 | `low_level` `line_input` `steady_tempo` `humanised` | 20 | 20 | 10.87 | 1 043 930 | −28.0/−53.9 |
| `missing_downbeats` | 120 | 4/4 | `core` `steady_tempo` `humanised` | 17 | 20 | 11.35 | 1 089 644 | −7.0/−33.9 |
| `noisy_microphone` | 126 | 4/4 | `noisy` `microphone` `steady_tempo` | 20 | 20 | 10.87 | 1 043 930 | −6.0/−24.3 |
| `palm_mute_metal` | 126 | 4/4 | `core` `steady_tempo` `distorted` `humanised` | 80 | 20 | 10.87 | 1 043 930 | −6.0/−40.6 |
| `power_chords_distorted` | 126 | 4/4 | `core` `steady_tempo` `distorted` `humanised` | 40 | 20 | 10.87 | 1 043 930 | −4.0/−28.0 |
| `ritardando` | 152→100 | 4/4 | `tempo_ramp` `humanised` | 48 | 24 | 12.90 | 1 238 672 | −6.0/−23.3 |
| `sparse_single_notes` | 112 | 4/4 | `core` `steady_tempo` `humanised` | 9 | 20 | 12.06 | 1 158 216 | −6.0/−31.1 |
| `stop_start` | 132 | 4/4 | `core` `steady_tempo` `contains_silence` `humanised` | 24 | 32 | 15.90 | 1 526 008 | −6.5/−35.1 |
| `sustained_chords` | 96 | 4/4 | `core` `steady_tempo` | 4 | 16 | 11.35 | 1 089 644 | −6.0/−31.9 |
| `syncopated_funk` | 112 | 4/4 | `core` `steady_tempo` `humanised` | 46 | 20 | 12.06 | 1 158 216 | −6.5/−36.0 |
| `tapping_muting_only` | 112 | 4/4 | `steady_tempo` `distorted` `humanised` | 30 | 20 | 12.06 | 1 158 216 | −7.0/−42.9 |
| `waltz_3_4` | 138 | 3/4 | `steady_tempo` `humanised` | 18 | 18 | 9.18 | 880 948 | −6.5/−33.4 |

10 fixtures are `core` (SPEC §19's "core fixtures"); 17 are `steady_tempo`;
2 are `tempo_ramp`.

## Ground-truth method

**`beats`** is the *metric grid* — the notated beat unit of the meter,
`beatsPerBar` per bar. Constant spacing on steady fixtures.

**`onsets`** are *as-played perceptual attacks*: the start of a strum, chord,
single note or tap, including the timing humanisation actually rendered (±4–10 ms
depending on pattern) plus a per-string detune of up to ±5 cents. The six string
plucks inside one strum are **one** onset, not six — a scorer expecting six would
penalise a correct tracker.

**`silentBeats`** indexes into `beats` with no onset within ±30 ms; the metric
grid is still declared there, because the point of `missing_downbeats` and
`stop_start` is that a tracker must hold a grid it cannot hear.

**`silenceSpans`** is the coalesced `[startSeconds, endSeconds]` of
`silentBeats`. **`downbeats`** indexes the meter's strong beat.
**`subdivision`** gives `perBeat` and its BPM so a harness can score a finer
grid. **`signal`** holds peak/RMS dBFS and clipped-sample fraction *measured from
the committed bytes*, so a fixture tagged `clipping` is verifiably clipped
(`line_input_clipping` measures 1.18 % flat-topped samples).

### Ramp beat derivation — the part that had to be right

The tempo is linear in time over a declared window:

```
bpm(t) = bpm0 + s·(t − t0),     s = (bpm1 − bpm0)/(t1 − t0)
Φ(t)   = ∫ bpm(u)/60 du = (bpm0·d + s·d²/2)/60,     d = t − t0
```

Beat *k* is where `Φ(t) = k`. The generator solves this by **Newton iteration**
from a linear first guess (converges to full double precision in a few steps, no
branch selection needed). Two details carry the correctness:

1. The antiderivative is offset by **`t0`**. Writing it `bpm0·t + s·t²/2` drifts
   by `s·t0·d/60`, which put the last beat ~0.02 beats early. This was a real bug,
   caught by an independent audit, and the C++ test now re-integrates the tempo
   function across every gap independently and fails unless each gap spans
   exactly 1.000000 beat.
2. **`t1` is solved in closed form** so the last beat lands exactly on it. Because
   the tempo is linear, its mean over `[t0, t1]` is the midpoint value, so
   `Φ(t1) = (t1 − t0)·(bpm0 + bpm1)/120`; setting that to `count − 1` gives
   `t1` in one step. Every beat then lies strictly inside the ramp — nothing is
   clamped and there is no tail region needing a tempo definition the audio never
   exercises.

Measured: `accelerando` spacing runs **0.5506 s → 0.4130 s** (1.33× ratio);
`ritardando` is the same curve reversed. Both are checked against > 1.20× by the
test.

## Determinism evidence

Two independent full generation runs, plus the committed corpus, hashed with
`sha256sum`:

```
$ for d in final-a final-b; do (cd $d && find wav -name '*.wav' | sort | xargs sha256sum > ../$d.sha256); done
$ diff final-a.sha256 final-b.sha256 && echo "IDENTICAL (19/19)"
IDENTICAL (19/19)

$ diff final-a.sha256 final-committed.sha256 && echo "IDENTICAL (19/19)"
IDENTICAL (19/19)

$ cmp final-a/manifest.json final-b/manifest.json && echo "A==B"
A==B manifest
$ cmp final-a/manifest.json testdata/rhythm/manifest.json && echo "A==committed"
A==committed
```

The 19 hashes, identical across A, B and the committed tree:

```
ad7548c2bc2daba75267f88b9b78c86c0875543dabb407f542e170b154a858c6  wav/accelerando.wav
be29f477b61b2e4fe9bfa3b4defa32fd257bc6894d757bd6f036d8936fe241d0  wav/arpeggio.wav
0d6840d2d761f707eefed67b1cad628baa7499d285b81cf4fbea272ba29c2598  wav/blues_shuffle.wav
7b2281c7cc02d05167ff3aba76a675e303153b403865df9954deefc453d6c402  wav/clean_eighths.wav
8a760276dbc41110e11a03e7c2ca6a81db0abea33203d7817415548f01be34a9  wav/clean_sixteenths.wav
a95db9aafdd2a12026bc0820f31094b453e40c31542c4d40a2b374165cb592bd  wav/compound_6_8.wav
97c07ae951c1e71941bb0f11e6e5ac9671360a5021538fe854842a263da3bf23  wav/line_input_clipping.wav
f98f2036d0ad6df6de9778a0dd923d3ad564b22bdc1b8884213e299c3b873b12  wav/line_input_low_level.wav
5dfa999b03f77037760c3ee40bd5b5d4e5373bec1c6289ae125184bd65362d76  wav/missing_downbeats.wav
d4d70266c0902a5524aef8d1729ea25087a98bc2ed0c985453af6cc778a0a9fe  wav/noisy_microphone.wav
9cc9cbb80762f23555a7bd92a1ed38b99f3e43cd554832431016398f81531eb3  wav/palm_mute_metal.wav
9fc83b4496cb18ff1c304c0cb75994bbfcd2af9b2a3cc36e4602907d3fea1d4a  wav/power_chords_distorted.wav
e8cf0485734549c33558386ed07511edda620bfef0f2da74be3e1732ccbef1ce  wav/ritardando.wav
c4cec9d1e1b75b92b95ff55292639cd7ad057318565dde26a40bddee90f1bc6d  wav/sparse_single_notes.wav
74324931db424b8df842c63cf2add26afe62338e27da5c7664e5d0ed9b98ef31  wav/stop_start.wav
e4b9297fca341e70a639fc6a51fbd9e884c1321ee4fc802c5c3497feeefe6442  wav/sustained_chords.wav
ee8ebb3c2f01c845cd53d4d2ea89d6aa145cafa17818f373366c91e1c94396b9  wav/syncopated_funk.wav
5e3b516af5c65315e9315f5e5d0e2d6cced2f9a45ed0cd9285226abc4a013cb0  wav/tapping_muting_only.wav
37c837c3bcc43bf6dc49aa00a44b69fe998175d4da72cf9c5551e7f63e247895  wav/waltz_3_4.wav
```

The generator also self-checks:

```
$ python3 testdata/rhythm/tools/gen_fixtures.py --check
...
manifest matches (19 fixtures)
check exit=0
```

## Tests executed

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
cd /home/mojo/projects/worktrees/EVAL-001

# generate
python3 testdata/rhythm/tools/gen_fixtures.py --out testdata/rhythm/wav
python3 testdata/rhythm/tools/gen_fixtures.py --check

# build + test
rm -rf /tmp/opencode/build-eval001
cmake -S jam-core -B /tmp/opencode/build-eval001 -G Ninja
cmake --build /tmp/opencode/build-eval001
ctest --test-dir /tmp/opencode/build-eval001 --output-on-failure; echo "exit=$?"

# focused
/tmp/opencode/build-eval001/jamTests RhythmCorpus.
/tmp/opencode/build-eval001/jamTests Nope.

# extra warning strictness on the new TU
g++ -std=c++17 -Wall -Wextra -Wpedantic -c tests/jam/RhythmCorpusTests.cpp \
  -I src -I tests/jam -I tests -o /dev/null
```

## Test results

`ctest` **exit 0**, all three suites pass, pre-existing suites unaffected in the
same binary:

```
Test project /tmp/opencode/build-eval001
    Start 1: jam.AnalysisAudioRing
1/3 Test #1: jam.AnalysisAudioRing ............   Passed    1.48 sec
    Start 2: jam.MusicalClock
2/3 Test #2: jam.MusicalClock .................   Passed    0.01 sec
    Start 3: jam.RhythmCorpus
3/3 Test #3: jam.RhythmCorpus .................   Passed    0.50 sec

100% tests passed out of 3
exit=0
```

```
[suite] RhythmCorpus
  PASS RhythmCorpus.manifestParsesAndDescribesTheWholeCorpus
  PASS RhythmCorpus.everySha256MatchesTheFileOnDisk
  PASS RhythmCorpus.beatsArePresentStrictlyIncreasingAndInsideTheFile
  PASS RhythmCorpus.steadyFixturesMatchTheirDeclaredNominalBpm
  PASS RhythmCorpus.rampFixturesHaveGenuinelyNonUniformBeats
  PASS RhythmCorpus.allNineteenRequiredScenarioTagsArePresentExactlyOnce
  PASS RhythmCorpus.tagVocabularyIsClosed
  PASS RhythmCorpus.meterLicenseAndProvenanceAreDeclaredEverywhere
  PASS RhythmCorpus.meterSpecificFixturesUseTheIntendedMeter
  PASS RhythmCorpus.coreTagMarksTheFixturesSpec19ScoresAgainst
  PASS RhythmCorpus.manifestIsCanonicallySerialised

11 tests, 1838 checks, 0 failed check(s) in 0 test(s)

ERROR: filter "Nope." matched zero tests
zero-match exit=2
```

Zero warnings under `-Wall -Wextra -Wpedantic`:

```
$ cmake --build /tmp/opencode/build-eval001 > build.log 2>&1; echo "build exit=$?"
build exit=0
$ grep -icE "warning|error" build.log
0
$ g++ -std=c++17 -Wall -Wextra -Wpedantic -c tests/jam/RhythmCorpusTests.cpp \
    -I src -I tests/jam -I tests -o /dev/null && echo "Wpedantic clean"
Wpedantic clean
```

## Evidence

### Corpus size on disk

```
$ du -sh testdata/rhythm
21M	testdata/rhythm
$ du -sb testdata/rhythm
21148543	testdata/rhythm
```

21 MB total (`testdata/rhythm/wav` alone is 20 954 918 bytes; the remainder is
the manifest, README and generator). **This exceeds the ~4 MB the task contract
asked for** — see Known limitations item 1 for the arithmetic and the options.

### The suite can fail — three independent corruptions

Performed on scratch copies via `JAM_RHYTHM_CORPUS`, so the repository was never
mutated. Baseline first, to show the mechanism itself is sound:

```
$ JAM_RHYTHM_CORPUS=/tmp/opencode/scratch-ok ctest -R jam.RhythmCorpus
100% tests passed out of 1
```

**1. One corrupted SHA-256** (`ad7548c2…` → all zeros):

```
$ JAM_RHYTHM_CORPUS=/tmp/opencode/scratch-bad ctest -R jam.RhythmCorpus --output-on-failure
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1067
      CHECK_EQ failed: actual == f.sha256
    actual:   "ad7548c2bc2daba75267f88b9b78c86c0875543dabb407f542e170b154a858c6"
    expected: "0000000000000000000000000000000000000000000000000000000000000000"
  FAIL RhythmCorpus.everySha256MatchesTheFileOnDisk

0% tests passed, 1 tests failed out of 1
exit=8
```

**2. A wrong `nominalBpm`** (arpeggio 120 → 97), the exact bug the contract asks
to be caught:

```
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1148
      arpeggio: beat gap 0.500000s does not match nominalBpm 97.000000 (expected 0.618557s)
  FAIL RhythmCorpus.steadyFixturesMatchTheirDeclaredNominalBpm
```

**3. A ramp flattened to a uniform grid** — `accelerando` gaps were
`0.5506 … 0.4130`, forced to a single `0.5506` everywhere:

```
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1100
      CHECK failed: f.beats.back() <= f.durationSeconds
  FAIL RhythmCorpus.beatsArePresentStrictlyIncreasingAndInsideTheFile

    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1227
      accelerando: beat gap 1 spans 1.017673 beats of tempo function, not 1.000000
  FAIL RhythmCorpus.rampFixturesHaveGenuinelyNonUniformBeats
```

Note that corruption 3 was caught by **two** independent checks — the
re-integrated tempo phase and the in-range assertion — which is the intended
redundancy. After restoring, the repository corpus is untouched and green:

```
$ git status --porcelain testdata/rhythm/manifest.json
$ ctest --test-dir /tmp/opencode/build-eval001 --output-on-failure
100% tests passed out of 3
```

### Ground truth is actually findable in the audio

The most important property of this corpus is that the declared ground truth
corresponds to something a real detector can find. I verified this with a
throwaway audit (not part of the deliverable) using a standard STFT spectral-flux
onset detector — 1024-sample Hann, 256-sample hop, log-compressed magnitude,
half-wave-rectified frame difference, threshold at 2.5 × median flux, matching
within ±30 ms. Every declared onset must be found and **no undescribed peak may
exist**, because a peak the manifest does not describe means the audio contains
rhythm the ground truth omits.

Final state, all 19 fixtures:

```
fixture                  grid                         ons  miss spur jump  rms      clip    onmin offmax
accelerando   ramp 0.4191..0.5775s x1.378             24    0    0    0  -22.38  0.0000    3.06    1.65
arpeggio      const 0.5357s err=1e-09                 33    0    0    0  -33.23  0.0000    4.91    2.31
blues_shuffle const 0.6000s err=6e-16                 16    0    0    0  -23.82  0.0000    3.92    1.80
clean_eighths const 0.5000s err=2e-16                  8    0    0    0  -25.62  0.0000    5.28    1.65
clean_sixteenths const 0.5000s err=2e-16              32    0    0    0  -30.14  0.0000    4.68    1.76
compound_6_8  const 0.7143s err=1e-09                 12    0    0    0  -24.49  0.0000    3.77    1.85
line_input_clipping  const 0.5000s err=2e-16          8    0    0    0  -13.19  0.0182    5.31    1.86
line_input_low_level const 0.5000s err=2e-16          8    0    0    0  -54.25  0.0000    7.11    1.91
missing_downbeats     const 0.5357s err=1e-09          7    0    0    0  -34.27  0.0000    6.03    1.90
noisy_microphone      const 0.5000s err=2e-16          8    0    0    0  -25.20  0.0000    3.65    1.58
palm_mute_metal       const 0.5000s err=2e-16         32    0    0    0  -42.76  0.0000   15.29    1.53
power_chords_distorted const 0.5000s err=2e-16       16    0    0    0  -27.94  0.0000   18.07    2.10
ritardando   ramp 0.4191..0.5775s x1.378             16    0    0    0  -23.21  0.0000    3.17    1.71
sparse_single_notes   const 0.6000s err=6e-16          4    0    0    0  -30.12  0.0000   11.29    1.59
stop_start            const 0.6000s err=6e-16          4    0    0    0  -41.36  0.0000   26.91    1.48
sustained_chords      const 0.7143s err=1e-09          2    0    0    0  -32.25  0.0000   10.96    1.83
syncopated_funk       const 0.5714s err=1e-09         19    0    0    0  -32.73  0.0000   10.42    2.17
tapping_muting_only   const 0.6000s err=6e-16         12    0    0    0  -43.85  0.0000   13.21    1.38
waltz_3_4             const 0.4545s err=1e-09          6    0    0    0  -34.87  0.0000    7.68    2.11

ALL AUDIT CHECKS PASS
```

The weakest declared onset across the whole corpus sits at **≥ 3.06 ×** its file's
median flux while the strongest non-onset frame reaches **≤ 2.31 ×**, so the
2.5 × threshold separates them everywhere. `jump = 0` also confirms every
`silentBeat` really has no new attack (energy still decaying from the previous
onset), and the ramp grids integrate to exactly one beat per gap.

### Four synthesis bugs this audit caught

All four were silent — the audio rendered, the manifest looked correct, and only
checking that the ground truth was *findable* exposed them. This is the concrete
argument for that check existing.

1. **Damping off by a factor of `freq`.** The delay-line write head makes one
   trip per string *period*, so the per-visit gain must be
   `10^(−3/(t60·freq))`, not `10^(−3/(t60·sr))`. The latter is ~50× too slow on
   the low E (~2 dB/s instead of ~14 dB/s): every string drones on, and each new
   onset is buried under the previous note's ring until the ground truth is
   unfindable. Verified after the fix — measured T60 matches the declared table
   on all six open strings to within 1 % (high E 1.91 s vs 1.90 declared; the
   others 4.20/3.88/3.39/2.89/2.30 vs 4.20/3.90/3.40/2.90/2.30).
2. **Single-exponential decay.** A real string loses energy fastest right after
   the pluck — the saddle transmits a force proportional to string velocity back
   into the string, largest when displacement is largest. With one exponential a
   chord strummed every eighth lands on a ring only ~7 dB down and the attacks
   stop articulating. Now two-stage: 18 % of T60 for the first 100 ms.
3. **Reverb RT60 of 1.69 s** — a *hall*, not a room. The comb bank feedback
   (0.72/0.68/0.63) smeared the gap between eighths until consecutive attacks
   merged into one wash. Real small-room RT60 is 0.4–0.6 s; the bank now measures
   ~0.42 s, and the wet/dry mix is a genuine crossfade rather than `x + wet·x`
   (which was scaling, not mixing).
4. **Duplicate onsets.** Two pattern positions could jitter onto or clamp to the
   same instant, declaring an onset pair no detector can resolve and inflating
   the expected count. Onsets are now de-duplicated to 12 ms.

Each fix is commented in `gen_fixtures.py` at the site, with the reasoning and
the wrong alternative spelled out, so the bug cannot be reintroduced silently.

### Generation cost

Pure-Python Karplus-Strong initially took **25 minutes** for the corpus because
every string was synthesised out to the end of the file. Capping each note to its
audible tail (110 dB below peak, plus a 0.25 s floor — beyond which the
float→16-bit conversion discards it anyway) brought that to **2m45s** with no
change to the output bytes, confirmed by the SHA-256 equality above.

## Known limitations

1. **The corpus is 21 MB, not the ~4 MB the contract asked for.** This is a real
   conflict in the contract, not a choice. 48 kHz / 16-bit / mono is 96 000 B/s,
   so 4 MB buys 41.7 s total = **2.2 s per fixture** across 19 files. Two bars of
   4/4 at any plausible tempo is 4–6 s, so a 2.2 s fixture cannot contain the two
   bars SPEC §19 needs for acquisition plus any steady state to measure.
   Meeting 4 MB would mean shipping 19 files that cannot support a single metric
   they exist for — which the brief itself calls out as worse than a missing
   entry ("a corpus entry that does not actually probe what it claims is worse
   than a missing one"). I used the shortest length that still admits
   "acquire within 2 bars" + ≥ 2 bars steady. **Options for the orchestrator:**
   (a) accept 21 MB; (b) reduce the fixture set and treat the rest as a
   follow-on task; (c) drop to 24 kHz, which halves it to ~11 MB but makes the
   onsets less sharp for every tracker. I did not pick one silently.
   `--profile smoke` (2-bar, ~11 MB) exists for cheap CI but is explicitly not
   valid G3 evidence.
2. **These fixtures are synthetic, and that is a hard limit on what they prove.**
   There are no real strings, pickups, amplifier, room, fret noise, intonation
   error, pick or player. The plucked-string model is a reasonable approximation
   of a note's *spectrum and decay*, and that is the whole of its competence. It
   does not model sympathetic resonance between strings, string-to-string
   coupling, pick position varying across a strum, real pickup nonlinearity,
   cable capacitance, or the many small physical effects that make a real guitar
   sound like a guitar. **A tracker that scores well here may still fail on a
   real instrument, and one that scores badly here deserves a second look with
   real audio before being discarded.** Results from this corpus are evidence, not
   a verdict, and SPEC §20 (musical-quality gate) still requires repeated
   real-guitar play tests that nothing here can substitute for.
3. **Impairment levels are plausible, not measured.** Noise floor, hum, clipping
   ratio and room decay were chosen to be unambiguous enough to isolate the
   variable under test. There is no claim that −34 dBFS hiss corresponds to any
   particular microphone.
4. **Humanisation is a model, not a player.** Uniform ±4–10 ms jitter plus ±5
   cents detune. Real playing has far larger and highly structured deviation
   (rushing, dragging, uneven across a phrase). Anything measuring *how a human
   plays* rather than *whether a tracker finds the beat* is out of scope.
5. **Some fixtures are deliberately easier than life.** `sustained_chords` and
   `stop_start` have fewer onsets than beats, and `sparse_single_notes` has 9
   onsets in 20 beats. That is the point of those cases, but it means a tracker
   can pass them by being conservative, so they should not be read as evidence of
   responsiveness on their own.
6. **A learned tracker may behave differently on synthetic audio for reasons
   unrelated to guitar physics.** This corpus is a controlled probe, not a
   representative sample.
7. **The 6/8 beat unit is a judgement call.** I scored `compound_6_8` on the
   **dotted quarter** (2 beats/bar) with the three eighths exposed via
   `subdivision`, because that is what a human drummer counts in 6/8. A harness
   that instead expects 6 eighth-beats per bar should read `subdivision`. The
   alternative reading is defensible; it just must be chosen deliberately.
8. **`missing_downbeats` beats are scored even though nothing is audible there.**
   `silentBeats` marks them. A harness should exclude them from onset-based
   precision but keep them for phase and tempo, or SPEC §19's gates become
   untestable on that fixture.
9. **`--check` verifies, it does not repair.** It regenerates and compares; on
   mismatch it exits non-zero with instructions. The repo corpus is currently in
   sync (`check exit=0`).
10. **EVAL-003 will extend this corpus.** The manifest shape was designed for
    that: adding a fixture is one dict in `fixtures()` plus regeneration. The
    scenarios DEVPLAN §14 lists that are *not* SPEC §12.2 cases (silence, octave
    ambiguity, tempo step, syncopation burst, noisy onset injection) are **not**
    present yet — `syncopated_funk` and the two ramps overlap partially, but
    EVAL-003's distinct scenarios need their own fixtures, which is EVAL-003's
    to add.

## Integration notes

**EVAL-002 (`tools/rhythm-eval/`) — what it should read.**

- `testdata/rhythm/manifest.json` is the input contract. Read `fixtures[]`;
  each entry's `file` is relative to the corpus directory.
- **Use `beats` for phase/tempo/acquisition scoring and `onsets` for
  onset-detection precision/recall.** They are deliberately different arrays —
  `onsets` are perceptual attacks and `beats` are the metric grid. Scoring
  onsets against beats is the most likely way to produce confidently wrong
  numbers.
- `silentBeats` excludes beats with no audible attack (`missing_downbeats`,
  `stop_start`); `silenceSpans` gives the time ranges directly for the
  false-beat-rate-in-silence metric.
- `subdivision` exposes finer grids when a fixture needs them (eighths,
  sixteenths, compound-meter eighths, and `swingRatio` for the shuffle).
- For the ramp fixtures use `beats` as-is — they are already non-uniform and
  integrating the tempo again would double-count.
- `conventions.beatToleranceSeconds` (0.07) is the suggested F-measure tolerance.
- `signal` lets the harness assert an impairment is really present before
  trusting a number computed from it.
- Suggested F-measure tolerance is 70 ms, chosen well above the generator's
  ±10 ms humanisation so the tolerance measures the tracker, not the synthesis.

**ADR-TRACKER-001** must record `corpus.id` = `eval001-guitar-corpus`,
`corpus.version` = 1, and `generator.version` = 1, plus the manifest's own
hash, so a future decision is traceable to an exact corpus.

**Real recordings later.** `DEVPLAN.md` EVAL-001 sanctions local-only fixtures.
Conventions are documented in the README: a manifest entry whose `file` is a
local path rather than `wav/<name>.wav`, with real `sha256`/`bytes`, real
`license`/`provenance` (do **not** reuse `CC0-1.0` unless true), and a
`capture: real` qualifier added to `tagVocabulary.qualifier` in the same commit.
Such entries must live in a **separate** `manifest.local.json` merged at run
time — otherwise the committed suite's "every listed fixture file exists" check
fails on a fresh clone. That split is EVAL-002's call to make; the README
documents the field conventions so it can be made without guessing.

**Build wiring.** None needed and none added: `jam-core/CMakeLists.txt` globs
`tests/jam/*.cpp` with `CONFIGURE_DEPENDS` and derives ctest names from
`JAM_TEST(suite, ...)` registrations. Adding the file was sufficient. A reviewer
should confirm the suite appears in the discovered list at configure time.

**Protected files.** No file under `src/**`, `CMakeLists.txt`,
`jam-core/CMakeLists.txt`, `tests/jam/JamTest.h`, `tests/jam/JamTestMain.cpp`,
existing `tests/jam/*.cpp`, `SPEC.md`, `DEVPLAN.md`, `EXECUTION-LEDGER.md`,
`HANDOFF.md` or `docs/**` was created, modified or deleted. `git status` shows
only additions in the allowed paths.

## Final commit SHA

To be filled in after commit; reported below.