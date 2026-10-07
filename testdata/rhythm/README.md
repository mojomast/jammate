# Guitar rhythm evaluation corpus

`EVAL-001` — the evidence set used to choose the production beat tracker.

`SPEC.md` §12 says no tracker is chosen permanently until it has been tested on
guitar-specific material, and gate **G3** in `DEVPLAN.md` requires a guitar
corpus with ground truth before BTrack, aubio and BeatNet can be compared.
Without this directory the tracker decision cannot be made at all.

The application listens to a **guitar through an interface, line input or
microphone** — not to a mastered stereo song. Solo guitar has far weaker
low-frequency content, a narrower spectrum, and no drum-derived downbeat
structure, so a tracker that scores well on commercial recordings can still fail
completely here. This corpus is built to expose exactly that gap.

## No third-party or commercial audio is included

**Every fixture here is synthesised from scratch** by
[`tools/gen_fixtures.py`](tools/gen_fixtures.py), committed alongside the audio
it produced. Nothing was recorded, sampled, downloaded, traced, converted or
derived from any existing recording. `SPEC.md` §12.2 and `DEVPLAN.md` §31 both
restrict the committed corpus to owned/created or clearly redistributable
material; that requirement is satisfied by construction rather than by
provenance review, and every manifest entry is licensed `CC0-1.0` with a
`provenance` string saying so. This is the reason the corpus is synthetic
rather than a set of licensed samples: a licence could not have made
commercial songs meaningful evidence for *this* input anyway.

## Format

| Property | Value |
|---|---|
| Container | RIFF/WAVE, mono |
| Sample rate | 48 000 Hz (`SPEC.md` §17 reference configuration) |
| Bit depth | 16-bit PCM, little-endian |
| Duration | 8.8 – 15.9 s per fixture |
| Count | 19 fixtures, 218.3 s total, 20 954 918 bytes |
| Generator | Python 3 standard library only (`wave`, `struct`, `math`, `random`, `json`, `hashlib`, `argparse`) — no numpy/scipy |

### A deviation from the task contract, reported rather than resolved

The contract asked for the whole corpus under ~4 MB. That is not achievable
together with the mandated format. 48 kHz / 16-bit / mono is **96 000 bytes per
second**, so 4 MB buys 41.7 s in total — **2.2 s per fixture** across 19
fixtures. Two bars of 4/4 at any plausible tempo is 4–6 s, so a 2.2 s fixture
cannot contain the two bars `SPEC.md` §19 needs for acquisition plus any steady
state to measure afterwards. Meeting the size budget would have meant shipping
19 files that cannot support a single metric they exist for.

This corpus instead uses the shortest length that still admits *"acquire useful
lock within 2 bars"* (SPEC §19) plus at least 2 bars of steady state, which is
**21 MB**. If the size limit turns out to be hard, the honest reductions are a
fixture subset or a lower sample rate — both orchestrator decisions, not
something to fix by silently truncating audio. `--profile smoke` renders 2-bar
versions (about 11 MB) for cheap CI exercise; they are explicitly **not** valid
G3 evidence.

## What each fixture probes, and why it matters for tracker selection

`core` marks the fixtures SPEC §19's gates are phrased against; that set is
named explicitly in the manifest (`tagVocabulary.core`) so the denominator of
"≥ 95 % of core fixtures" is not left implicit.

| Fixture | BPM | Meter | Tags | What it probes |
|---|---|---|---|---|
| `clean_eighths` | 126 | 4/4 | `core` `steady_tempo` `humanised` | Baseline every tracker must pass: clear eighth attacks, real low end from the body, no clipping. Fails here, it fails everywhere. |
| `clean_sixteenths` | 126 | 4/4 | `core` `steady_tempo` `humanised` | The single most common failure on guitar input: does the tracker hold 126, or get pulled to double time by the denser grid? |
| `power_chords_distorted` | 126 | 4/4 | `core` `steady_tempo` `distorted` | Saturation generates harmonics above the fundamentals and pulls energy into the low mids, so spectral-flux detectors see a far denser spectrum than a clean guitar. |
| `palm_mute_metal` | 126 | 4/4 | `core` `steady_tempo` `distorted` | Almost no pitched harmonic content survives; close to percussive noise. Kills trackers that weight spectral flux over broadband energy. |
| `blues_shuffle` | 108 | 4/4 | `core` `steady_tempo` `swing` | Triplet swing (2:1). The third subdivision of every beat is empty and attacks are late, so any tracker assuming a straight eighth grid reads the wrong pulse. |
| `syncopated_funk` | 112 | 4/4 | `core` `steady_tempo` `humanised` | Attack deliberately missing on the last downbeat of each bar, plus ghost notes straddling beats. Directly tests the SPEC §19 gate *"no tempo jump from one isolated syncopated event"*. |
| `sparse_single_notes` | 112 | 4/4 | `core` `steady_tempo` `humanised` | Transient density far below anything in a pop recording. A tracker that requires regular onsets must still hold lock through the gaps. |
| `arpeggio` | 120 | 4/4 | `core` `steady_tempo` `humanised` | Sixteenth-note broken chords, one string per note, so each attack is a genuine transient rather than a strum smear. The classic double-time trap. |
| `sustained_chords` | 96 | 4/4 | `core` `steady_tempo` | Half-note chords plucked softly: slow bloom, long decay, one transient every four beats. Lock stability with almost no transient density and no per-beat re-attack. |
| `missing_downbeats` | 120 | 4/4 | `core` `steady_tempo` `humanised` | Alternating bars have **no attack on the downbeat at all**. The metric grid is unchanged, so a tracker that insists on hearing the downbeat must not lose lock or invent a tempo. `silentBeats` names the affected beats. |
| `stop_start` | 132 | 4/4 | `core` `steady_tempo` `contains_silence` | 3 bars playing, 2 bars of silence, 3 bars again, grid unbroken. Probes the SPEC §19 stop/start recovery gate and false-beats-in-silence (SPEC §12.3). |
| `accelerando` | 108→146 | 4/4 | `tempo_ramp` `humanised` | Linear tempo ramp. Beat times are the analytic integral of the tempo curve, so they are genuinely non-uniform. Follow mode must track it without stepping or oscillating. |
| `ritardando` | 152→100 | 4/4 | `tempo_ramp` `humanised` | The same integrator run backwards. Catches trackers that handle acceleration but lag, step or oscillate when the tempo falls. |
| `waltz_3_4` | 138 | 3/4 | `steady_tempo` `humanised` | Three beats per bar breaks the 4/4 prior every beat tracker is built on; bar phase drifts by a beat per bar if it is assumed. |
| `compound_6_8` | 96 | 6/8 | `steady_tempo` `humanised` | Compound duple, subdivided into three eighths. The scored grid is the **dotted quarter** (2 per bar). A tracker that locks to the eighths reports exactly triple tempo, so this separates *"understands compound meter"* from *"found a periodicity"*. |
| `noisy_microphone` | 126 | 4/4 | `noisy` `microphone` `steady_tempo` | Same material as `clean_eighths` through a noisy mic (hiss, rumble, handling ticks). Isolates **SNR** robustness from **level** robustness, which the next two vary separately. |
| `line_input_low_level` | 126 | 4/4 | `low_level` `line_input` `steady_tempo` `humanised` | Direct cable at −28 dBFS peak with 50/100/150 Hz mains hum. The gain-staging failure mode: everything is fine except there is far less of it. |
| `line_input_clipping` | 126 | 4/4 | `clipping` `line_input` `steady_tempo` | Driven past the interface ceiling and hard-clipped (1.1 % of samples flat-topped, measured). Clipping destroys the amplitude information a level-based onset detector relies on. |
| `tapping_muting_only` | 112 | 4/4 | `steady_tempo` `distorted` `humanised` | Palm-muted chugs and fret-hand taps only: broadband percussive noise, almost no pitched content. The realistic floor of what a guitar presents, and a check that a tracker is not secretly depending on harmonic material. |

### On the "19 required cases" count

`SPEC.md` §12.2 lists 19 bullets and `DEVPLAN.md` EVAL-001 lists the same 19.
The two line-input variants (`low level`, `clipping`) are separate bullets, and
they probe different failures (gain staging vs input overload), so they are
separate fixtures rather than one file with two settings. **There is no
duplicate to collapse**: 19 bullets, 19 fixtures.

## Regenerating

```bash
python3 testdata/rhythm/tools/gen_fixtures.py --out testdata/rhythm/wav
python3 testdata/rhythm/tools/gen_fixtures.py --check     # verify, do not write
python3 testdata/rhythm/tools/gen_fixtures.py --profile smoke --out /tmp/wav \
        --manifest /tmp/manifest.json
```

Roughly 2m45s for the full corpus in pure Python. `--check` regenerates from
scratch and compares byte-for-byte against what is on disk, so drift is a
non-zero exit code rather than a silent difference.

### Seed policy

Every random draw comes from a `random.Random` seeded from SHA-256 of the
generator version and the fixture name:

```
seed = int(sha256("<corpusId>|<generatorVersion>|<fixtureName>").hexdigest()[:16], 16)
```

One RNG per fixture, drawn in a fixed order (excitation → velocity → detune →
impairments). Nothing reads the clock, the process id, the filesystem or
`PYTHONHASHSEED`. Two independent generation runs produce byte-identical WAVs
and an identical manifest; this is verified in `task-notes/EVAL-001.md`. Changing
`GENERATOR_VERSION` changes every seed, which is deliberate: it makes a
generator change loudly invalidate every committed hash rather than silently
producing a corpus that only differs in the noise.

## Ground-truth method

Declared in `manifest.json` under `conventions`, and summarised here because a
harness that misreads these fields will produce confidently wrong numbers.

- **`beats`** — the **metric grid**: the notated beat unit of the meter,
  `beatsPerBar` per bar. Constant spacing on steady fixtures. On the two ramp
  fixtures the spacing follows the **analytic integral of the tempo function**
  and is genuinely non-uniform (see below).
- **`onsets`** — **as-played perceptual attacks**: the start of a strum, chord,
  single note or tap, including the timing humanisation that was actually
  rendered. The six string plucks inside one strum are **one** onset, not six —
  a scorer that expected six would penalise a correct tracker.
- **`silentBeats`** — indices into `beats` with no onset within ±30 ms. These
  beats are still scored, but a tracker has no transient to hear there.
- **`silenceSpans`** — coalesced `[startSeconds, endSeconds]` of `silentBeats`.
  **This is not a description of silence.** See the warning below.
- **`trueSilenceSpans`** — `[startSeconds, endSeconds]` regions where the guitar
  is genuinely near-silent. **This is the field to score silence against.**
- **`downbeats`** — indices into `beats` of the meter's strong beat.
- **`subdivision`** — `perBeat` subdivisions per beat and their nominal BPM, so a
  harness can also score a finer grid (eighths, sixteenths, compound eighths).
- **`signal`** — peak/RMS in dBFS and the clipped-sample fraction, **measured
  from the committed bytes**, not from the generator's intent. A fixture tagged
  `clipping` with no flat tops would be a lie; the measurement makes that
  checkable.
- **`beatToleranceSeconds: 0.07`** — suggested F-measure tolerance for SPEC §12.3
  beat-event scoring, chosen well above the generator's worst-case ±10 ms timing
  humanisation so the tolerance measures the *tracker*, not the humanisation.

### `silenceSpans` vs `trueSilenceSpans` — read this before scoring silence

These are two different things and conflating them breaks a SPEC §19 gate.

**`silenceSpans`** names narrow ±30 ms windows around beats the player
deliberately did **not** play. That is real, useful ground truth — it tells a
harness that a beat is still expected on a grid the audio does not reinforce.
But it is *not* silence, and consuming it as if it were measures nothing:

| Fixture | `silenceSpans` total | `trueSilenceSpans` total |
|---|---|---|
| `stop_start` | 0.480 s across 8 windows | **4.82 s** across 3 windows, incl. one contiguous 4.02 s gap |
| `missing_downbeats` | 0.180 s across 3 windows | 0.85 s |

Every beat of a tracker maintaining the correct grid falls inside one of the
`silenceSpans` windows, so a false-beat-rate-in-silence metric computed over them
scores **16.7 false beats per second on `stop_start`** — counting the correct
behaviour (holding time through a gap) as fabrication. The instrument could not
measure the gate it was built for.

**`trueSilenceSpans`** is the field that describes where the guitarist stopped.
SPEC §19's "silence does not create false acceleration" and SPEC §12.3's "false
beat rate in silence" must be measured against it.

### How `trueSilenceSpans` is derived, and why not from the audio

A note counts as **inaudible once it has fallen 45 dB below its own peak**
(`conventions.trueSilenceDepthDb`). The 45 dB figure is chosen musically, not
arithmetically: it sits below the noise floor of any reasonable recording chain
and below the level at which a decaying string is still perceptually "ringing",
so a held, muted or decaying string is not silence — the guitarist stopping is.

The threshold is deliberately **relative to the note, not to the file's noise
floor**. The corpus spans a 43 dB range of noise floors, from −76 dBFS (quiet
line capture) to −34 dBFS (noisy mic). A noise-floor-relative test would call the
noisy mic's gaps silent where the clean capture's would not, even though the
guitarist is equally absent from both.

A span is declared only when it is at least **0.25 s** long
(`conventions.trueSilenceMinSeconds`). At 126 BPM a sixteenth note is 0.12 s, so
the floor excludes every subdivision a player could be playing through while
admitting any deliberate pause. This matters: palm-muted sixteenths have ~0.1 s
of audible ring between notes, and without the floor the corpus would declare 80
"silent" spans inside a fixture that is audibly continuous chugging.

**Derivation is from synthesis intent, not from the WAV.** Each event's audible
window is `[onset, onset + attack settling + decay time + room tail]`, computed
from the generator's own decay model — the same two-stage law `karplus_strong`
implements — and silence is the complement of the union of those windows. The
audio is measured **afterwards, to verify**. That ordering is the whole point: a
field reverse-engineered from the file it describes cannot catch a synthesis
bug, because it would faithfully describe the bug. Measurement can disagree with
the declaration, and when it does, the declaration is wrong.

Two details in that derivation were learned by measuring the audio and are
commented at their site:

- **Attack settling.** A pluck does not reach its loudest instant when the pick
  touches the string; the strings beat against each other for tens of
  milliseconds. Anchoring the window at the first sample produced spans whose
  first 20–30 ms contained the loudest part of the note.
- **No coalescing.** Two gaps separated by a short audible chord are genuinely
  two silences. Merging them (an earlier version did) swallowed the attack
  between them and produced spans that overlapped real onsets.

Spans are also rounded **conservatively** — start rounded up, end rounded down
to 6 dp — so a serialised span can only ever be smaller than the derived region.
Plain `round()` pushed one span's end past the onset that terminates it, which
the generator's own check caught.

Every declared span is then verified against the committed audio by
`RhythmCorpus.trueSilenceSpansMatchTheAudioOnDisk`: inside a span the level must
stay within 15 dB of that fixture's measured noise floor.

This is the part that would invalidate every tempo-drift metric if done wrongly,
so it is derived rather than assumed.

The tempo is linear in time over a declared window `[t0, t1]`:

```
bpm(t) = bpm0 + s · (t − t0),        s = (bpm1 − bpm0) / (t1 − t0)
```

The beat grid is where the **integrated tempo phase** crosses an integer:

```
Φ(t) = ∫ bpm(u)/60 du = ( bpm0·d + s·d²/2 ) / 60,     d = t − t0
```

Beat *k* is the time where `Φ(t) = k`. This has a closed-form quadratic inverse,
but the generator solves it by **Newton iteration** from a linear first guess,
which converges to full double precision in a few steps and needs no branch
selection. Two details matter:

1. The antiderivative is offset by **`t0`**, not by zero. Writing it as
   `bpm0·t + s·t²/2` silently drifts by `s·t0·d/60`, which puts the last beat
   ~0.02 beats early. That bug was caught and fixed; the C++ test now
   re-integrates the tempo function across every gap independently and fails if
   any gap does not span exactly 1.000000 beat.
2. The window `t1` is chosen in **closed form** so the last beat lands exactly on
   it. Because the tempo is linear, its mean over `[t0, t1]` is the value at the
   midpoint, so `Φ(t1) = (t1 − t0)·(bpm0 + bpm1)/120`; setting that equal to
   `count − 1` solves for `t1` in one step. Every beat therefore lies strictly
   inside the ramp: nothing is clamped, and there is no tail region where the
   ground truth would need a tempo definition the audio never exercises.

Measured spacing range, `accelerando` 108→146 BPM: **0.5506 s down to 0.4130 s**,
a 1.33× ratio. `ritardando` is the same curve reversed.

### The `core` set — which fixtures SPEC §19 scores

SPEC §19 phrases every rhythm-quality gate against "core fixtures", so which
fixtures those are is a load-bearing definition for release gates, not a label.
`tagVocabulary.core` is therefore an explicit list of **fixture names**:

```
arpeggio, blues_shuffle, clean_eighths, clean_sixteenths, missing_downbeats,
palm_mute_metal, power_chords_distorted, sparse_single_notes, stop_start,
sustained_chords, syncopated_funk
```

The definition is: **core means the steady-tempo set on which SPEC §19's BPM
relative-error, half/double-time and lock-time gates are meaningful.**

**`missing_downbeats` is in**, and that is a decision rather than an oversight.
It is steady tempo — 120 BPM throughout, constant spacing verified to 1e-9 — so
the tempo gates are perfectly well defined on it. What it omits is the *attack*
on alternate downbeats, not the tempo. SPEC §19's *"no tempo jump from one
isolated syncopated event"* is precisely the gate it exists to test, and a corpus
that excluded it would have no fixture for that sentence at all. It is **not**
core for anything needing an attack on every beat; `silentBeats` marks the beats
where that applies, and a harness should exclude those from onset-based
precision while keeping them for phase and tempo.

The list previously held *scenario tag* names (`palm_mute`,
`distorted_power_chords`) while the fixtures are named `palm_mute_metal` and
`power_chords_distorted`. The two namespaces only partly overlap, so a harness
resolving membership by fixture name matched 7 of 11. Membership is now by
fixture name, and the generator **and** the C++ suite both fail loudly if the
list and the per-fixture `core` tag ever disagree again.

## The synthesis

A plucked-string model per string, summed through a physically-motivated capture
chain:

```
dry strings → amp drive → cabinet tone → body resonance (mic) → room (mic)
            → capture EQ → noise / hum → level → limiter → 16-bit PCM
```

**Karplus-Strong**: a shaped noise burst into a delay line with a lowpass
feedback loop, plus a one-pole allpass for fractional-delay pitch accuracy.
Per-fixture character comes from pick/strum burst length and brightness, string
count, open-position fret offsets, per-string velocity spread, pick hardness and
a small detune (±5 cents). **Real playing has dynamics**: every event has its own
velocity, phrases accent and decay, and amplitude is never constant.

Per-string decay and brightness, standard tuning E2…E4:

| String | Open freq | T60 | Brightness |
|---|---|---|---|
| Low E | 82.41 Hz | 4.20 s | 0.26 |
| A | 110.00 Hz | 3.90 s | 0.32 |
| D | 146.83 Hz | 3.40 s | 0.40 |
| G | 196.00 Hz | 2.90 s | 0.48 |
| B | 246.94 Hz | 2.30 s | 0.56 |
| High E | 329.63 Hz | 1.90 s | 0.64 |

### Four synthesis bugs worth recording

Each of these was found by an independent audit pass (an STFT spectral-flux onset
detector checking that every declared onset is actually findable in the rendered
audio), and each would have produced a corpus that looks fine and is useless.
They are documented here because the failure mode in every case is *silent*.

1. **Damping off by a factor of `freq`.** The write head makes one trip per
   string *period*, so the per-visit gain is `10^(−3/(t60·freq))`, not
   `10^(−3/(t60·sr))`. The second gives ~2 dB/s on the low E instead of
   ~14 dB/s: every string drones on, and each new onset is buried under the
   previous note's ring until the declared ground truth is unfindable.
2. **Single-exponential decay.** A real string loses energy fastest right after
   the pluck, because the saddle transmits a force proportional to string
   velocity back into it, and that force is largest when displacement is
   largest. With one exponential, a chord strummed every eighth note lands on a
   ring only ~7 dB down, the attacks stop articulating, and no onset detector can
   find the beats the manifest declares. The generator uses a two-stage decay
   (18 % of T60 for the first 100 ms).
3. **Reverb RT60 of 1.69 s.** The original comb feedback gave a *hall*, not a
   room. A long tail smears the gap between eighth notes until consecutive
   attacks merge into one wash. Real small-room RT60 is 0.4–0.6 s; the bank now
   measures ~0.42 s.
4. **Duplicate onsets.** Two pattern positions could jitter onto or clamp to the
   same instant, declaring an onset pair that no detector can resolve and
   inflating the expected count. Onsets are now de-duplicated to 12 ms.

After these fixes every fixture separates cleanly: the weakest declared onset
sits at **≥ 3.1×** its file's median spectral flux, while the strongest
non-onset frame reaches at most **2.3×**, so a threshold at 2.5× median finds
every declared onset and no undescribed one.

## What this corpus can and cannot tell you

**It can tell you:**

- whether a tracker can find a beat at all in guitar-spectrum material with
  little low end — the thing commercial-song benchmarks never test;
- whether it snaps to half or double time on eighths, sixteenths and arpeggios;
- whether it survives syncopation, absent downbeats, 3/4 and 6/8;
- whether it handles low input level, input clipping and low SNR independently;
- whether it follows a controlled tempo ramp, and whether recovery after a
  two-bar stop works;
- acquisition time in beats/bars, BPM relative error, phase error, half/double
  error rate and false beats in silence, per SPEC §12.3.

**It cannot tell you, and this limitation is the important one:**

- **This is synthetic, not a guitar.** There are no real strings, no real
  pickups, no real amplifier, no real room, no real fret noise, no real
  intonation error, no real pick and no real player. The plucked-string model is
  a decent *approximation* of a note's spectrum and decay, and that is the
  entirety of what it is good at. It does not model sympathetic resonance
  between strings, string-to-string coupling, pick position changing across a
  strum, real pickup nonlinearity, cable capacitance, or the dozens of small
  physical effects that make a real guitar guitar.
- **Consequently, a tracker that scores well here may still fail on a real
  instrument**, and a tracker that scores badly here deserves a second look with
  real audio before being discarded. Results from this corpus are evidence, not
  a verdict. `SPEC.md` §20 (musical-quality gate) still requires repeated
  real-guitar play tests and cannot be satisfied by this corpus at all.
- The impairment levels (noise floor, hum, clipping ratio, room decay) are
  *plausible*, not *measured from real captures*. They are chosen to be
  unambiguous enough to isolate the variable under test. There is no claim that
  −34 dBFS hiss corresponds to any particular microphone.
- Humanisation is a per-event jitter bounded at ±10 ms (4–10 ms depending on the
  pattern) plus a per-string detune of up to ±5 cents. Real playing
  has far larger and highly structured timing deviation (rushing, dragging,
  uneven across a phrase). Anything measuring *how a human plays* rather than
  *whether a tracker finds the beat* is out of scope here.
- A tracker with a large learned model may have been trained on real recordings
  and could behave differently on synthetic audio for reasons that have nothing
  to do with guitar physics. This corpus is a controlled probe, not a
  representative sample.

### The sanctioned path for adding real recordings later

`DEVPLAN.md` EVAL-001 explicitly permits this: *"If larger/private recordings
cannot be committed, manifest paths and hashes may point to local fixtures while
synthetic fixtures remain in repo."* The mechanism the manifest already
supports:

- add a manifest entry whose `file` is a **local path** rather than
  `wav/<name>.wav` — e.g. `local/real_capture_01.wav`;
- record its `sha256` and `bytes` exactly as for synthetic fixtures, so drift and
  provenance are still checkable;
- set `license` and `provenance` to state the actual ownership of the recording
  (do **not** reuse `CC0-1.0` unless that is genuinely true);
- add a `capture: real` qualifier tag, and add it to `tagVocabulary.qualifier`
  in the same commit.

Such entries are **not** committed, so the existing C++ suite would fail its
"every listed fixture file exists" check on a fresh clone. The intended handling
is to keep local-only entries in a separate manifest
(`manifest.local.json`) that `tools/rhythm-eval/` (EVAL-002) merges at run time,
leaving the committed `manifest.json` self-consistent. That split is EVAL-002's
decision to make; this README documents the field conventions so it can be made
without guessing.

## Test coverage

`tests/jam/RhythmCorpusTests.cpp`, suite `RhythmCorpus`, **validates** the
corpus rather than generating it (generation is Python). It runs in the existing
`jam-core` ctest binary and needs no new build wiring.

| Test | Verifies |
|---|---|
| `manifestParsesAndDescribesTheWholeCorpus` | manifest parses; 19 fixtures; every declared file exists; paths are relative and cannot escape the corpus directory; SHA-256 hex length; 48 kHz; unique names |
| `everySha256MatchesTheFileOnDisk` | recomputes SHA-256 over the actual bytes with a self-tested implementation (FIPS 180-4 vectors) |
| `beatsArePresentStrictlyIncreasingAndInsideTheFile` | non-empty beats, strictly increasing, ≥ one bar, inside the duration; onsets ordered and in range |
| `steadyFixturesMatchTheirDeclaredNominalBpm` | **the important one** — every beat gap on a steady fixture matches `nominalBpm` to within **0.05 %**. Catches a generator bug that would otherwise silently corrupt the benchmark |
| `rampFixturesHaveGenuinelyNonUniformBeats` | re-integrates the tempo function across each gap independently (must equal exactly 1 beat) and requires a > 1.20× spacing ratio, so a "ramp" with a constant grid fails |
| `allNineteenRequiredScenarioTagsArePresentExactlyOnce` | all 19 SPEC §12.2 cases present, each exactly once, with no unexpected primary tag |
| `tagVocabularyIsClosed` | no tag outside the declared vocabulary — a typo cannot create a category nothing consumes — **and every tag that carries membership has a declared, non-empty membership list** |
| `trueSilenceSpansAreDeclaredAndSelfConsistent` | `trueSilenceSpans` present and well-formed on **every** fixture; within `[0, durationSeconds]`; sorted and non-overlapping; **no span overlaps any onset** |
| `trueSilenceSpansMatchTheAudioOnDisk` | parses the RIFF `data` chunk and checks every declared span is actually quiet — at most 15 dB above that fixture's measured noise floor. This is the independent second opinion on the audio claim; the field itself is derived from synthesis intent |
| `coreMembershipListAgreesWithTheCoreTag` | `tagVocabulary.core` and the per-fixture `core` tag name the **same** fixtures; every listed name is a real fixture; core fixtures are `steady_tempo` with constant tempo |
| `meterLicenseAndProvenanceAreDeclaredEverywhere` | `meter`, `license` (`CC0-1.0`) and `provenance` on every entry; `beatsPerBar` consistent with the meter; ≥ 4 bars per fixture |
| `meterSpecificFixturesUseTheIntendedMeter` | 3/4 and 6/8 really have 3 and 2 beats per bar, and the 3/4 bar really spans three beat intervals |
| `coreTagMarksTheFixturesSpec19ScoresAgainst` | `core` fixtures are all `steady_tempo`, and the count is non-trivial |
| `manifestIsCanonicallySerialised` | single line, no CR, compact separators, top-level keys sorted — so the file does not churn in git diffs |

JSON is parsed by a ~150-line hand-written reader in the test file. The
alternative — a companion CSV index — would have been less code but strictly
worse, because the manifest *is* the contract and a second file duplicating its
fields is a second thing that can drift from it. No JSON dependency was added and
nothing here is on any real-time path; `SPEC.md` §7.1 only forbids JSON parsing on
the audio thread, and no audio thread exists in this test.

`trueSilenceSpansMatchTheAudioOnDisk` is the only test that reads sample data, so
it is also the only one that needs a WAV reader. It parses the RIFF chunk list
rather than assuming a 44-byte header: an earlier version read the whole file as
samples and interpreted the ASCII `RIFF`/`WAVE` header as audio, which put a
spurious 0 dBFS transient at the start of all 19 files and failed every one of
them for a reason that had nothing to do with the corpus. Real captures carry
`LIST` or `fact` chunks, so the offset is not a constant either.

The suite finds the corpus via `JAM_RHYTHM_CORPUS`, then `__FILE__`, then the
working directory, and **throws** if all miss — a relocated corpus is a red test,
never a suite that quietly validates nothing. `JAM_RHYTHM_CORPUS` is also how the
"can this fail" evidence was produced, without mutating the repository.