# VENDORED-PATCHES.md — BTrack 1.0.7

**Upstream:** https://github.com/adamstark/BTrack at
`9d6127618a5679e9caa74c594b88f1d74f0e035f` (upstream's own `CMakeLists.txt`
declares `project(BTrack VERSION 1.0.7)`).

**Licence:** GNU GPL v3 (`LICENSE.txt`, © 2008-2014 Queen Mary University of
London). Compatible with this project's AGPLv3 only inside the already-open path
(SPEC.md §25.6). `libs/kiss_fft130` is BSD; its `COPYING` ships here.

**Current state: every file under `src/` and `libs/` is byte-identical to
upstream.** MSVC now compiles a generated portability copy described below;
GNU/Linux still compiles the original. The vendored files are never rewritten.

---

## RETRACTED — a patch that broke the tracker

**An earlier revision of this directory contained a Patch 1 that guarded out
libsamplerate. It was wrong, and it silently disabled the beat tracker.**

The reasoning was: `resampleOnsetDetectionFunction()` uses libsamplerate's
`src_simple()`, libsamplerate is not vendored, so guard the function out as
"unreachable". The supporting claim written into that revision was that the
function is *"used only by the NON-CAUSAL offline beat-time helpers"* and that
*"`grep -rn resampleOnsetDetectionFunction src/` — callers are confined to the
offline helpers."*

**Both halves of that claim were false.** There is exactly one caller, and it is
in the causal beat-detection loop:

```cpp
// src/BTrack.cpp:255-261
// if we are at a beat
if (timeToNextBeat == 0)
{
    beatDueInFrame = true;         // indicate a beat should be output
    // recalculate the tempo
    resampleOnsetDetectionFunction();   // <-- EVERY BEAT
    calculateTempo();
}
```

The function writes `resampledOnsetDF`, and `calculateTempo()` immediately
consumes it for both `adaptiveThreshold()` and `calculateBalancedACF()`. With the
function stubbed out, `resampledOnsetDF` stayed all zeros, the autocorrelation ran
on silence, and **BTrack reported a fixed 79.5 BPM for every input, at every
sample rate, at every hop size** — a value that is superficially plausible and
therefore the most dangerous kind of wrong.

Two lessons, recorded because both are generalisable:

1. **An audit claim written into a document gets trusted.** This file asserted a
   reachability result with a `grep` quoted as evidence, and a downstream worker
   (TRACK-001) correctly relied on it. My verification was `grep` for the *call
   sites*; I never traced whether the *containing function* was on the causal
   path. The grep was real and the conclusion was wrong.
2. **A guard that silently produces a plausible wrong answer is worse than a
   build error.** Had the stub also asserted at runtime, this would have been
   caught in minutes. TRACK-001 caught it by testing tempo against a known
   synthetic signal — which is the argument for the "prove it unbiased by test"
   requirement in its brief, vindicated.

**The fix was to stop patching.** libsamplerate is vendored (BSD-2-Clause, pinned
`0844c208f683527c08ea8a80acc13b398aa9c8bf` at `third_party/libsamplerate/`) and
BTrack's sources are now byte-identical to upstream. The reimplementation
alternative — inlining the resample — was rejected because at `hop == 512` the
ratio is exactly 1.0 but a SINC converter still filters, so an inline version
would *not* be bit-identical. For an instrument that decides the production
tracker, exact fidelity is worth 9.6 MB.

Verify the claim above rather than trusting it:

```bash
diff -r third_party/BTrack/src <(git -C <upstream-clone> show 9d61276:src)
```

## Build configuration (not source patches)

1. **`USE_KISS_FFT=1`** is defined. `OnsetDetectionFunction.h` includes
   `kiss_fft.h` only under that macro; upstream's own `src/CMakeLists.txt` sets
   it. Without it the build fails with `complexOut was not declared`.
2. **`CMakeLists.txt` is ours**, because upstream's is written for its own tests
   and plugins. Sources, include paths and dependencies are unchanged.
3. **libsamplerate and kiss_fft are built as separate static libraries.** The
   9.2 MB `high_qual_coeffs.h` is the `SRC_SINC_BEST_QUALITY` filter bank, which
   BTrack selects explicitly. There is no table-generation build step in this
    version, so vendoring is a plain compile.
4. **libsamplerate's generated `config.h` and `HAVE_CONFIG_H` are private.**
   First live-product integration exposed the earlier public usage requirement:
   JUCE's bundled PNG code found libsamplerate's unrelated configuration header,
   inherited C-only macros and failed to compile. The flag and generated-header
   search directory now apply only to the samplerate target. Its public API
   header directory and static link dependency remain exported. No BTrack,
    libsamplerate or kiss_fft source byte changes are involved.

## MSVC-only generated portability overlay

GitHub run37746525272 first compiled the experimental BTrack product backend on
Windows and exposed upstream GNU variable-length arrays and an unavailable
`M_PI`. `MsvcOverlay.cmake` verifies the LF-normalised source SHA256
`e787d20139c1628b2710330c3eaa3ec0a1ad1d5a9f02a789c658c48dffc829b0` and writes a
build-directory copy only when `MSVC` is true. It replaces six VLA declarations
with uninitialised RAII-owned arrays and raw pointer aliases. All extents,
indexing, arithmetic and call sites remain unchanged. `_USE_MATH_DEFINES=1` is
private to the BTrack target. The original GPL notice stays in the generated
file and the generator ships in the corresponding source.

These arrays allocate on the analysis worker, where allocation is permitted;
they are not callback-thread scratch or a claim of worker-wide allocation
freedom. The overlay fails closed on a changed source pin. Native strict C++17
compilation succeeds, the original VLA source fails the strict compiler control,
and a changed-source negative control fails the generator. Original versus
overlay beat flags, tempo and cumulative scores are bit-identical across36,000
rows (three hop sizes × four steady/noise/gap cases). Native comparison is not
a Windows runtime/deadline claim; Windows compilation is checked by CI.

---

## NOT patched, but load-bearing for whoever writes the adapter

`BTrack::calculateTempo()` contains a **hard-coded `44100.0`**:

```cpp
double tempoToLagFactor = 60. * 44100. / 512.;
```

Upstream's defaults (`hopSize = 512`, `frameSize = 1024`) therefore assume
**44.1 kHz**. This product runs at 48 kHz (SPEC.md §17).

**Correction to an earlier claim in this file:** it previously suggested that
scaling the hop (`hop = round(512 * rate / 44100)`) would fix the bias. TRACK-001
measured that it does not, and is right not to — upstream normalises the onset
detection function to a fixed 512 points whose spacing is `512 / sampleRate`,
**independent of the hop**. Measured on `clean_eighths` (126 BPM):

| configuration | tempo error |
|---|---|
| 44.1 kHz, hop 512 | −2.34 % |
| 48 kHz, hop 512 | −8.85 % |
| 48 kHz, hop 557 | **−10.23 %** (worse) |
| 48 kHz, hop 559 | −10.55 % |

Resampling the audio to 44.1 kHz before handing it to BTrack is the approach that
works, because it changes `sampleRate` — the only term the bug actually depends
on.

BTrack also exposes `beatDueInCurrentFrame()` and **no beat phase at all**, which
is why `RhythmObservation::phaseValid` exists. An adapter must derive phase or
report `phaseValid = false` honestly; fabricating one would corrupt both the
scoring and the Musical Clock's lock behaviour.
