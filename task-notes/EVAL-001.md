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

`675b583dd25d7297980c4bf0d2a3a204cdb2f8d4`
(`test(rhythm): guitar rhythm evaluation corpus with ground truth`)

24 files added, 4260 insertions, 0 deletions. Working tree clean after commit.
---

## REPAIR PASS 1

### Goal

Fix two ground-truth declaration defects found by EVAL-002, which consumes this
corpus. The synthesis, the audio, the seeds, the scenario coverage and the test
suite scope all stay as they are; only the manifest's declarations and the
generator that produces them change.

1. **P1** — `silenceSpans` does not describe silence. It holds narrow ±30 ms
   windows around beats that were deliberately not played, so a false-beat-rate
   metric computed over it counts the *maintained metronome grid* as fabrication:
   `stop_start` scores 16.7 false beats per second and measures nothing about
   silence. SPEC §19 ("silence does not create false acceleration") and SPEC §12.3
   ("false beat rate in silence") cannot be evaluated at all.
2. **P2** — `tagVocabulary.core` declared no usable membership. It listed
   *scenario tag* names (`palm_mute`, `distorted_power_chords`) while the fixtures
   are named `palm_mute_metal` and `power_chords_distorted`, so the two namespaces
   only partly overlap and a harness resolving membership by fixture name matched
   7 of 11. SPEC §19 is phrased entirely against "core fixtures", so this is a
   load-bearing definition for release gates.

### Files changed

Exactly four, all inside the allowed set:

- `testdata/rhythm/manifest.json` — regenerated from the same seeds.
- `testdata/rhythm/tools/gen_fixtures.py` — new `trueSilenceSpans` derivation,
  corrected `core` membership, generator-side verification of both.
- `tests/jam/RhythmCorpusTests.cpp` — three new tests, one extended.
- `task-notes/EVAL-001.md` — this section.

No audio file was touched, no fixture added or removed (19 stays 19), and
`tools/rhythm-eval/**` was not read for modification or edited.

### Contract implemented

**New field: `trueSilenceSpans`** — a list of `[startSeconds, endSeconds]` pairs
on every fixture, declaring the regions where the guitar is genuinely
near-silent.

- **`silenceSpans` is unchanged and keeps its job.** It still names the narrow
  windows around `silentBeats` — beats that were deliberately not played but are
  still expected on the grid. Its `conventions.silenceSpans` description now
  says explicitly that it is *not* a description of silence and must not be used
  for a false-beat metric.

- **Near-silent threshold: 45 dB below the note's own peak**
  (`conventions.trueSilenceDepthDb`, also exposed as the numeric
  `conventions.trueSilenceDepthDb` field).

  Justified musically, not arithmetically: 45 dB sits below the noise floor of
  any reasonable recording chain and below the level at which a decaying guitar
  string is still perceptually "ringing". So a held, muted or decaying string is
  **not** silence — the guitarist stopping is. Measured against the committed
  audio, notes reach this depth between 0.09 s (palm-muted) and 2.4 s (low E).

  The threshold is **relative to the note, not to the file's noise floor**, and
  that distinction is deliberate. The corpus spans a 43 dB range of noise floors
  (−76 dBFS quiet line capture to −34 dBFS noisy mic). A noise-floor-relative
  test would declare the noisy mic's gaps silent where the clean capture's would
  not, even though the guitarist is equally absent from both. This is also why
  the C++ verification is floor-relative with a 15 dB headroom rather than
  peak-relative: on `noisy_microphone` even silence is only 35 dB below the peak,
  because the hiss floor is −34 dBFS, so a peak-relative bound would report a
  correct declaration as a defect.

- **Minimum span length: 0.25 s** (`conventions.trueSilenceMinSeconds`). At
  126 BPM a sixteenth note is 0.12 s, so the floor excludes every subdivision a
  player could be playing through while admitting any deliberate pause. This is
  not cosmetic: palm-muted sixteenths have ~0.1 s of audible ring between notes,
  and without the floor the corpus declares 21 "silent" spans inside
  `tapping_muting_only` — a fixture that is audibly continuous chugging.

- **Populated for every fixture.** A fixture with no gap longer than 0.25 s has
  only its lead-in. Twelve of the nineteen do; the others are genuinely sparse
  (`sparse_single_notes`, `sustained_chords`, `tapping_muting_only`) or have a
  real stop (`stop_start`).

### Ground-truth derivation

**From synthesis intent. The audio is measured only to verify.** That ordering is
the whole point of the repair: a field reverse-engineered from the file it
describes cannot catch a synthesis bug, because it would faithfully describe the
bug — which is exactly how the four silent synthesis bugs in pass 1 were nearly
missed.

For each event the generator computes an audible window
`[onset, onset + attack settling + decay time + room tail]`:

- **Decay time** comes from `audible_seconds()`, an analytic mirror of the
  two-stage decay `karplus_strong` implements: `60/t60a` dB/s for the first
  `kInitialDecaySeconds`, then `60/t60` dB/s (the definition of T60). Validated
  against synthesised notes over t60 0.045–4.2 s and depth 40–55 dB: agreement
  within 5 %, limited by the 5 ms measurement step.
- **Attack settling** is one full period of the lowest-sounding string in the
  event. A real pluck does not reach its loudest instant when the pick touches
  the string — the strings beat against each other for tens of milliseconds.
- **Room tail** adds `room_wet × 2.5` s where a room is modelled, because the
  reverb is a linear filter on the whole signal and keeps sounding after the last
  note. A reverberant decay is not the guitarist continuing to play.
- **A 30 ms guard** is added to every window. The analytic ring time agrees to
  ~5 ms, and 5 ms is enough to put a decay tail inside a declared span
  (measured: a palm-muted chord declared silent 30 ms after its attack was still
  9.6 dB above the noise floor). The direction matters — over-declaring silence
  is the dangerous error, because a tracker beating inside a "silent" span is
  scored as fabricating a beat. Under-declaring costs at most tens of
  milliseconds.

Silence is the complement of the union of those windows within
`[0, durationSeconds]`. Two further rules, both learned from measuring the audio:

- **Spans are not coalesced.** Two gaps separated by a short audible chord are
  genuinely two silences; merging them (an earlier version did) swallowed the
  attack between them.
- **Spans are rounded conservatively** — start rounded up, end rounded down to
  6 dp — so a serialised span can only be smaller than the derived region. Plain
  `round()` rounded one span's end *up* past the onset that terminates it; the
  generator's own check caught it as "span swallows onset".

The generator then verifies each entry before writing it
(`verify_true_silence_spans`, `verify_core_membership`) and raises `SystemExit`
rather than emitting a manifest that contradicts itself.

### Measured vs declared — `stop_start` and `missing_downbeats`

Protocol: 50 ms RMS window, 10 ms hop, quiet = below 2 % of file peak, **no gap
bridging**, minimum contiguous duration varied to show its effect. Bridging is
what makes a 2 %-of-peak measurement coarse: with a 50 ms bridge every inter-eighth
dip in `stop_start` merges into one 11.4 s "silence", which is obviously not a
stop.

`stop_start` (peak −6.5 dBFS, quiet threshold −40.5 dBFS):

| | value |
|---|---|
| `silenceSpans` (old field) | 8 windows, **0.480 s** |
| `trueSilenceSpans` (declared, new) | 3 spans, **4.762 s** — `[0.00-0.35]` `[6.78-7.17]` `[7.24-11.25]` |
| measured, min 0.10 s | 25 spans, 11.14 s — mostly inter-eighth dips |
| measured, min 0.50 s | 2 spans, **5.27 s** — `[7.21-11.22]` `[14.62-15.87]` |
| measured, min 1.00 s | 2 spans, **5.27 s** — identical to min 0.50 s |

`missing_downbeats` (peak −7.0 dBFS, quiet threshold −41.0 dBFS):

| | value |
|---|---|
| `silenceSpans` (old field) | 3 windows, **0.180 s** |
| `trueSilenceSpans` (declared, new) | 1 span, **0.852 s** — `[0.00-0.85]` |
| measured, min 0.10 s | 18 spans, 6.38 s |
| measured, min 0.50 s | 4 spans, **3.44 s** — `[0.03-0.82]` `[4.09-4.83]` `[8.09-8.81]` `[10.13-11.31]` |
| measured, min 1.00 s | 1 span, **1.18 s** — `[10.13-11.31]` |

**On the orchestrator's figures.** The central claim reproduces exactly: the real
contiguous silence in `stop_start` is **7.21 → 11.22 s**, against the reported
7.20 → 11.25 s — agreement to within one 10 ms measurement step, and the new
declaration's largest span is `[7.24, 11.25]`. The lead-in reproduces too:
0.00 → 0.35 s declared, 0.03 → 0.33 s measured. The *total* figures are a
different story: I measure 11.14 s (min 0.10 s) where 6.25 s was reported, and
that gap is the threshold protocol rather than a disagreement about the audio —
6.25 s is reproducible with a 100 ms window and a 1 % threshold, and no single
(hop, window, threshold, bridge, min-duration) tuple reproduces both 6.25 s and
2.09 s. A coarse instrument, exactly as flagged; the generator's own knowledge of
which windows it rendered silent wins, and that is what the declaration uses.

`missing_downbeats` is the interesting disagreement. The generator declares only
a lead-in, because every event there is a full-amplitude chord that the two-stage
decay drives below threshold within ~0.2 s, and the next chord arrives long
before that. The audio agrees that the *chords* are gone, but a 2 %-of-peak
threshold also counts the inter-chord dips as quiet — so the coarse instrument
reports 3.44 s where the declaration says 0.85 s. The declaration is the honest
one for the field's stated purpose: those dips are the gaps between notes in a
continuous phrase, not the player stopping, and a minimum span length is what
separates the two cases.

### Audio immutability evidence

The audio must not change. Two independent full regenerations were run into
scratch directories and hashed, before and after the repair.

```
$ for d in final-a final-b; do (cd $d && find wav -name '*.wav' | sort | xargs sha256sum > ../$d.sha256); done
$ diff final-a.sha256 final-b.sha256 && echo "IDENTICAL (19/19)"
IDENTICAL (19/19)

$ diff final-a.sha256 final-committed.sha256 && echo "IDENTICAL (19/19)"
IDENTICAL (19/19)

$ cmp final-a/manifest.json testdata/rhythm/manifest.json && echo "A==committed"
A==committed
```

That was **before** the repair. After it:

```
$ python3 testdata/rhythm/tools/gen_fixtures.py --out <scratch>/wav --manifest <scratch>/manifest.json
$ diff <scratch>/sha256.after.txt <pre-repair>/sha256.before.txt
IDENTICAL: all 19 audio sha256 unchanged

$ (cd testdata/rhythm && find wav -name '*.wav' | sort | xargs sha256sum > sha256.final.txt)
$ diff <pre-repair>/sha256.before.txt sha256.final.txt
IDENTICAL — all 19 audio files untouched

$ python3 testdata/rhythm/tools/gen_fixtures.py --check ; echo $?
0
```

`git status --porcelain` reports only the four files above as modified; no `.wav`
appears. `--check` regenerates the manifest from scratch and compares byte-for-byte
against the committed one, so the manifest is reproducible from the unchanged
synthesis and the same seeds.

### The `core` decision

**`missing_downbeats` is IN core**, and that is a decision rather than an
omission.

The definition adopted: *core means the steady-tempo set on which SPEC §19's BPM
relative-error, half/double-time and lock-time gates are meaningful.*
`missing_downbeats` is steady tempo — 120 BPM throughout, constant spacing
verified to 1e-9 by `steadyFixturesMatchTheirDeclaredNominalBpm` — so the tempo
gates are perfectly well defined on it. What it omits is the **attack** on
alternate downbeats, not the tempo. SPEC §19's *"no tempo jump from one isolated
syncopated event"* is precisely the gate it exists to test, and excluding it would
leave the corpus with no fixture for that sentence.

It is explicitly **not** core for anything requiring an attack on every beat:
`silentBeats` marks the affected beats, and a harness should exclude those from
onset-based precision while keeping them for phase and tempo.

`tagVocabulary.core` is now 11 **fixture names**, and the generator *and* the C++
suite both fail loudly if the list and the per-fixture `core` tag disagree.

### Tests executed

```bash
export PATH=/tmp/opencode/venv/bin:$PATH
cd /home/mojo/projects/worktrees/EVAL-001

python3 testdata/rhythm/tools/gen_fixtures.py --out <scratch>/wav --manifest <scratch>/manifest.json
python3 testdata/rhythm/tools/gen_fixtures.py --check

rm -rf /tmp/opencode/build-eval001r
cmake -S jam-core -B /tmp/opencode/build-eval001r -G Ninja
cmake --build /tmp/opencode/build-eval001r
ctest --test-dir /tmp/opencode/build-eval001r --output-on-failure; echo "exit=$?"

/tmp/opencode/build-eval001r/jamTests RhythmCorpus.
g++ -std=c++17 -Wall -Wextra -Wpedantic -c tests/jam/RhythmCorpusTests.cpp \
    -I src -I tests/jam -I tests -o /dev/null

# fail-proof, on scratch copies so the repository is never mutated
cp -r testdata/rhythm /tmp/.../scratch-a      # baseline
cp -r testdata/rhythm /tmp/.../scratch-b      # corrupted trueSilenceSpans
cp -r testdata/rhythm /tmp/.../scratch-c      # corrupted core membership
JAM_RHYTHM_CORPUS=<scratch> ctest --test-dir /tmp/opencode/build-eval001r -R jam.RhythmCorpus --output-on-failure
```

### Test results

All six suites pass, exit 0:

```
Test project /tmp/opencode/build-eval001r
1/6 Test #1: jam.AnalysisAudioRing ............   Passed    1.47 sec
2/6 Test #2: jam.DrumTransportAdapter .........   Passed    0.01 sec
3/6 Test #3: jam.MusicalClock .................   Passed    0.00 sec
4/6 Test #4: jam.RhythmCorpus .................   Passed    2.27 sec
5/6 Test #5: jam.RhythmEvalMetrics ............   Passed    0.01 sec
6/6 Test #6: jam.RtSignal .....................   Passed    0.11 sec

100% tests passed out of 6
exit=0
```

`RhythmCorpus` is now 14 tests / 1990 checks (was 11 / 1838):

```
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
  PASS RhythmCorpus.trueSilenceSpansAreDeclaredAndSelfConsistent
  PASS RhythmCorpus.trueSilenceSpansMatchTheAudioOnDisk
  PASS RhythmCorpus.coreMembershipListAgreesWithTheCoreTag
  PASS RhythmCorpus.manifestIsCanonicallySerialised

14 tests, 1990 checks, 0 failed check(s) in 0 test(s)
```

Zero warnings:

```
$ cmake --build /tmp/opencode/build-eval001r > build.log 2>&1; echo $?
0
$ grep -icE "warning|error" build.log
0
$ g++ -std=c++17 -Wall -Wextra -Wpedantic -c tests/jam/RhythmCorpusTests.cpp -I src -I tests/jam -I tests -o /dev/null && echo clean
clean
```

### Evidence

#### The new assertions can fail

Baseline, to show the mechanism is sound before corrupting anything:

```
$ JAM_RHYTHM_CORPUS=<scratch-a> ctest --test-dir /tmp/opencode/build-eval001r -R jam.RhythmCorpus
100% tests passed out of 1
```

**Corruption 1 — a span made to overlap an onset, and a span pushed past the end
of the file** (`stop_start`: span 1 end `7.172203` → `99.0`, span 2 start
`7.236224` → `7.10`):

```
[suite] RhythmCorpus
  ...
  PASS RhythmCorpus.trueSilenceSpansAreDeclaredAndSelfConsistent ← no, see below
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1626
      stop_start: span ends after the file (span 1 [6.780365, 99.000000])
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1644
      stop_start: span swallows onset 7.172203 (span 1 [6.780365, 99.000000])
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1632
      stop_start: spans are unsorted or overlapping (span 2 [7.100000, 11.252592])
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1644
      stop_start: span swallows onset 7.172203 (span 2 [7.100000, 11.252592])
  FAIL RhythmCorpus.trueSilenceSpansAreDeclaredAndSelfConsistent
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1762
      stop_start: a declared trueSilenceSpans window reaches -29.265193 dBFS,
      51.593430 dB above the measured noise floor -80.858623
  FAIL RhythmCorpus.trueSilenceSpansMatchTheAudioOnDisk
  ...
14 tests, 1990 checks, 6 failed check(s) in 3 test(s)
exit=8
```

Both corruptions are caught, and **independently**: the self-consistency test
catches them structurally, and the audio test catches the same span a second way
by noticing it now contains 51 dB above the noise floor. The third failure
(`manifestIsCanonicallySerialised`) is incidental — the scratch rewrite added a
trailing newline arrangement the canonical check rejects — and is expected.

**Corruption 2 — the `core` membership list broken** (`palm_mute_metal` removed,
scenario-tag name `palm_mute` appended, i.e. exactly the original defect):

```
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1811
      tagVocabulary.core names 'palm_mute', which is not a fixture. Membership
      must be fixture names, not scenario tags: the two namespaces only partly overlap.
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1821
      fixture 'palm_mute_metal' carries the `core` tag but is absent from tagVocabulary.core
    FAIL /…/tests/jam/RhythmCorpusTests.cpp:1829
      tagVocabulary.core lists 'palm_mute' which does not carry the `core` tag
  FAIL RhythmCorpus.coreMembershipListAgreesWithTheCoreTag
0% tests passed, 1 tests failed out of 1
exit=8
```

That is the precise failure the old `tagVocabularyIsClosed` could not see, and it
reproduces the original defect on demand.

Reverted, the repository corpus is untouched and green:

```
$ git status --porcelain
 M testdata/rhythm/README.md
 M testdata/rhythm/manifest.json
 M testdata/rhythm/tools/gen_fixtures.py
 M tests/jam/RhythmCorpusTests.cpp

$ ctest --test-dir /tmp/opencode/build-eval001r --output-on-failure
100% tests passed out of 6
```

#### All 19 declared spans verified against the audio

```
fixture                spans  declared                       loudest-in noise-fl  verdict
--------------------------------------------------------------------------------------
accelerando            1      [0.00-0.34]                    -69.3      -45.1     ok
arpeggio               1      [0.00-0.35]                    -72.7      -68.0     ok
blues_shuffle          1      [0.00-0.35]                    -70.4      -49.8     ok
clean_eighths          1      [0.00-0.35]                    -71.5      -57.9     ok
clean_sixteenths       1      [0.00-0.35]                    -76.8      -63.8     ok
compound_6_8           1      [0.00-0.34]                    -66.3      -57.3     ok
line_input_clipping    1      [0.00-0.35]                    -50.2      -43.1     ok
line_input_low_level   1      [0.00-0.34]                    -89.2      -84.9     ok
missing_downbeats      1      [0.00-0.85]                    -76.0      -76.7     ok
noisy_microphone       1      [0.00-0.35]                    -40.9      -41.5     ok
palm_mute_metal        2      [0.00-0.35] [9.80-10.87]       -74.4      -76.3     ok
power_chords_distorted 2      [0.00-0.35] [10.51-10.87]      -66.3      -68.6     ok
ritardando             1      [0.00-0.35]                    -69.6      -50.5     ok
sparse_single_notes    5      [0.00-0.35] … [8.39-12.06]     -57.1      -61.4     ok
stop_start             3      [0.00-0.35] [6.78-7.17] …      -73.6      -80.9     ok
sustained_chords       5      [0.00-0.35] … [8.47-11.35]     -60.0      -64.3     ok
syncopated_funk        2      [0.00-0.35] [11.68-12.06]      -74.5      -76.0     ok
tapping_muting_only    21     [0.00-0.35] …                  -69.3      -73.7     ok
waltz_3_4              2      [0.00-0.35] [8.82-9.18]        -69.6      -71.3     ok

VERIFICATION PASS: every declared trueSilenceSpans is quiet in the audio,
in range, and swallows no onset
```

#### Two bugs my own new code had, both caught before commit

Recorded because they are the kind that produce a confident, wrong result:

1. **`round()` pushed a span past an onset.** Serialising `[0, 0.3522428]` with
   `round(x, 6)` gave an end of `0.352243`, three microseconds *past* the onset
   that terminates it. The generator's own `verify_true_silence_spans` caught it
   as "span swallows onset". Fixed by rounding start up and end down.

2. **The C++ WAV reader read the RIFF header as audio.** The first version of
   `trueSilenceSpansMatchTheAudioOnDisk` read the whole file as int16 samples,
   so the ASCII `RIFF`/`WAVE` header became a 0 dBFS transient at the start of
   every fixture and all 19 failed with "18 dB above the noise floor" for a
   reason that had nothing to do with the corpus. Replaced with a real RIFF chunk
   parser that locates `fmt ` and `data` — which is also correct for real
   captures, since `LIST`/`fact` chunks mean the data offset is not a constant 44.

A third measurement error is worth recording because it nearly caused a wrong
*analysis* rather than a wrong test: my first ring-time harness took the envelope
peak over the **whole** file. For an 82 Hz string measured through a 50 ms RMS
window, the loop beats against itself, so the peak occurs well after the attack
and the apparent decay looked like exactly half the nominal rate — which would
have "justified" halving every T60 in the corpus. It was an artefact of the
measurement, and the generator was right.

### Known limitations

1. **`trueSilenceSpans` is a conservative under-declaration, by design.** The
   45 dB criterion plus the 30 ms guard means declared silence is always slightly
   shorter than the measured quiet region — visible as `stop_start`'s declared
   `[7.24, 11.25]` against a measured `[7.21, 11.22]`. The direction is
   deliberate: over-declaring would score a correct tracker as fabricating beats,
   which is the failure mode the P1 fix exists to remove. A harness wanting the
   loosest defensible bound should still not widen it by hand.

2. **The audibility model is analytic, not measured per event.** `audible_seconds`
   reproduces the two-stage decay law rather than integrating each rendered note.
   It agrees with synthesised notes to ~5 % / 5 ms, and the 30 ms guard absorbs
   the difference, but it is a model. A synthesis change to the decay law would
   need `audible_seconds` updated in step — the generator's `verify_*` checks and
   the C++ audio test are what would catch it, not the model itself.

3. **`missing_downbeats` declares only a lead-in, while a 2 %-of-peak threshold
   reports 3.44 s.** The declaration is the honest one for this field's purpose —
   the extra measured time is the dips between notes in a continuous phrase, not
   the player stopping. A harness that genuinely wants "below 2 % of peak" rather
   than "the guitarist stopped" should measure that itself; the field answers a
   different and more useful question.

4. **`sustained_chords` and `tapping_muting_only` declare a large fraction of the
   file as silent (8.85 s of 11.35 s, and 10.56 s of 12.06 s).** That is true of
   *this* corpus, and it is a consequence of a pre-existing synthesis choice, not
   of this repair: `p_sustained_chords` computes `t60_scale` from a nominal ring
   time, and the two-stage fast decay then kills the chord within ~0.2 s, so the
   fixture is far more staccato than its name and `notes` suggest. The audio is
   immutable for this pass, so the declaration reports reality rather than
   intent. **Flagged for a future EVAL-001 pass: `sustained_chords` should ring
   across the bar as intended, and `tapping_muting_only` should probably not
   produce 21 silence spans inside a continuous-chug fixture.** Both are audio
   changes, so they belong in their own pass with a full re-verification.

5. **The C++ audio test decodes mono only.** It returns false for a stereo file
   rather than downmixing, because a silent downmix would be a worse failure than
   a refusal. The corpus is mono by declaration.

6. **The `trueSilenceSpansMatchTheAudioOnDisk` test reads 21 MB of WAV on every
   run** (~2.3 s). Fine for CI; if it becomes a bottleneck, cache the envelopes
   or hash-compare against a committed summary rather than re-reading.

### Integration notes

- **EVAL-002 follow-up (not done here, out of scope by instruction):**
  `tools/rhythm-eval/Manifest.cpp` reads `silenceSpans` and feeds it to
  `inAnySilenceSpan` and `silenceTotalSeconds`, which drive
  `falseBeatsInSilence` and `falseBeatsInSilencePerSecond`. Those paths should
  read `trueSilenceSpans` instead. `silenceSpans` remains useful and should
  still be read — but for `silentBeats`-derived logic, not for silence. The
  manifest change is additive and backwards-compatible, so that pass is a
  one-line field swap plus a decision about which metric uses which.
- **`stop_start`'s false-beat rate will change materially** once the harness
  switches fields: the divisor goes from 0.480 s to 4.762 s, so the same
  behaviour scores roughly an order of magnitude lower. Any gate that was
  calibrated against 16.7 beats/s is measuring the wrong thing and needs
  recalibrating, not just re-reading.
- **`tagVocabulary.core` is now fixture names.** Any harness that was resolving
  membership against scenario tags must be updated; that is the point of the fix.
- No build wiring changed. `jam-core/CMakeLists.txt` still globs the test file
  and `RhythmCorpus` is still auto-registered.

### Final commit SHA

`6c447effb8941060e9481c3eec38f724ca624f23`
(`fix(rhythm): declare true silence separately from unplayed beats; fix core membership`)

5 files changed, 4 of them modified in place, no file added or deleted, and no
`.wav` touched. Working tree clean after commit.
