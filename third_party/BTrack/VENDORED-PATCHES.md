# VENDORED-PATCHES.md — BTrack 1.0.7

**Upstream:** https://github.com/adamstark/BTrack at
`9d6127618a5679e9caa74c594b88f1d74f0e035f` (tagged `v1.0.7` by upstream's
`CMakeLists.txt` `project(BTrack VERSION 1.0.7)`).

**Licence:** GNU GPL v3 (`LICENSE.txt`, © 2008-2014 Queen Mary University of
London). Compatible with this project's AGPLv3 only inside the already-open path
(SPEC.md §25.6). `libs/kiss_fft130` is BSD; its `COPYING` ships here.

This directory is a **verbatim copy** of upstream except for the changes listed
below. Every deviation is deliberate, minimal, and load-bearing; none of them
alters the beat-tracking algorithm itself.

---

## Patch 1 — make libsamplerate optional (`src/BTrack.cpp`)

**Problem.** `src/BTrack.cpp` includes `"samplerate.h"` unconditionally and uses
`SRC_DATA` / `src_simple` in `resampleOnsetDetectionFunction()`. libsamplerate is
not vendored, is not a dependency of this project, and cannot be installed here.

**Why it is safe to remove.** `resampleOnsetDetectionFunction()` is used only by
the *non-causal, offline* beat-time helpers (`getBeatTimes`, and friends). This
product is a causal real-time tracker: it consumes audio as it arrives and never
looks ahead. Nothing in the causal path calls it.

**Change.** The include and the whole function body are wrapped in
`#ifdef BTRACK_WITH_LIBSAMPLERATE`. With the macro undefined the function still
exists as a documented no-op so the declaration in `BTrack.h` still links. If
anyone later needs the offline helpers, define the macro and provide
libsamplerate — that is the correct way to reintroduce the dependency, not to
silently restore the include.

**Audit:** `grep -rn resampleOnsetDetectionFunction src/` — callers are confined
to the offline helpers.

## Patch 2 — select the kiss_fft backend (`CMakeLists.txt`, ours)

Upstream's own `src/CMakeLists.txt` selects a backend with `USE_KISS_FFT` or
`USE_FFTW`. `OnsetDetectionFunction.h` includes `kiss_fft.h` only under
`#ifdef USE_KISS_FFT`, so building it without that define fails with
`complexOut was not declared`. We compile kiss_fft (already vendored under
`libs/`) and define `USE_KISS_FFT`.

## Patch 3 — build configuration only

Upstream `CMakeLists.txt` is replaced by ours. Upstream's is written for its own
tests and plugins and would drag in a test binary this project does not want.
Sources, include paths and the kiss_fft dependency are unchanged.

---

## NOT patched, but load-bearing for whoever writes the adapter

`BTrack::calculateTempo()` contains a **hard-coded `44100.0`**:

```cpp
double tempoToLagFactor = 60. * 44100. / 512.;
```

This is combined with the default hop size of 512. The upstream defaults
(`hopSize = 512`, `frameSize = 1024`) therefore assume **44.1 kHz**. This product
runs at 48 kHz (SPEC.md §17 reference configuration).

Consequences the adapter author must handle, not silently ignore:

- Feeding BTrack 48 kHz audio with a 512-sample hop yields a tempo estimate in
  "48 kHz beats per second", i.e. **~8.8 % fast** relative to true BPM, unless the
  hop is scaled (`hop = 512 * 48000/44100 ≈ 557`) or the onset detection function
  is resampled to 44.1 kHz first.
- Both are legitimate; they are not equivalent in cost or latency. **Whichever is
  chosen must be documented, and the corpus ground truth is at real seconds**, so
  a silent 8.8 % bias would be scored as a real BPM error.
- Resampling happens on the analysis worker thread, never on the audio callback.

This is the single most likely source of a wrong tracker-selection decision, and
it is why `RhythmObservation::phaseValid` exists: BTrack exposes
`beatDueInCurrentFrame()` but **no beat phase at all**, so an adapter must derive
phase from the timing of beat events relative to the beat period.