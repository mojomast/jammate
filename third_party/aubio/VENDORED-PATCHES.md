# VENDORED-PATCHES.md — aubio 0.4.9

**Upstream:** https://github.com/aubio/aubio at
`90bd27a23123fcc524c31787c9c8fc0ae4c79378` (annotated tag `0.4.9`, tag object
`319186517bfc0dd5c015241c091b849f209e0483`; `VERSION` declares
`AUBIO_MAJOR_VERSION=0 AUBIO_MINOR_VERSION=4 AUBIO_PATCH_VERSION=9`).

**Licence:** GNU General Public License v3 **or later** (`COPYING`, © 2003-2019
Paul Brossier and contributors). Compatible with this project's AGPLv3 only
inside the already-open path (SPEC.md §25.6). `src/` is aubio's own C code and
carries no bundled third-party copyright; aubio's optional FFTW3 / Intel IPP /
Accelerate / libsndfile / libavcodec backends are **not** vendored and are
**not** enabled (see build configuration).

**Current state: every file under `src/` is byte-identical to upstream.**
There are no source patches. Verify rather than trust:

```bash
diff -r third_party/aubio/src <(git -C <upstream-clone> show 90bd27a:src)
```

## What is vendored, and what is deliberately left out

Vendored:

- the complete upstream `src/` tree (all `.c`/`.h`, 860 KB). Keeping it whole
  makes the audit above a one-line `diff -r`; pruning to the compiled subset
  would save ~500 KB and cost the ability to verify byte-identity at a glance.
  For comparison, this is smaller than the 9.6 MB libsamplerate filter table
  already vendored for BTrack.
- `COPYING`, `AUTHORS`, `README.md`, `VERSION` — licence and provenance.
- `PINNED_SHA` — the reviewed commit.

Left out (present upstream, not needed and not copied): `python/` (≈712 KB
bindings), `doc/` (≈288 KB), `tests/` (≈228 KB), `examples/`, `scripts/`,
`wscript`, `Makefile`, `setup.py`, packaging metadata. None contain C sources
the `tempo`/`onset` path uses, and all are large. The licence is unaffected: the
whole library is GPL-3.0-or-later and `COPYING` ships here.

## Build configuration (not source patches)

1. **Built-in Ooura FFT.** aubio can be built against FFTW3, Intel IPP,
   Accelerate or its own bundled FFT (`src/spectral/ooura_fft8g.c`). The tempo
   path has **no mandatory external dependency**: with none of
   `HAVE_FFTW3`/`HAVE_FFTW3F`/`HAVE_INTEL_IPP`/`HAVE_ACCELERATE` defined,
   `fft.c` falls through to Ooura. This is what the vendored build uses, so the
   adapter adds no new library dependency.
2. **Generated `config.h`, not a committed header.** Upstream's waf generates
   `src/config.h` from host feature checks. `config.h.cmake` here is the
   template; `third_party/aubio/CMakeLists.txt` runs `configure_file()` into the
   build directory and defines `HAVE_CONFIG_H=1`. This mirrors how
   `third_party/BTrack/CMakeLists.txt` generates libsamplerate's `config.h`, and
   it keeps `src/` pristine.
3. **Curated compile list.** The CMake target compiles the 26 sources that the
   `aubio_tempo_t` dependency closure actually needs (recorded in
   `CMakeLists.txt`). `src/io/`, `src/pitch/`, `src/notes/` and `src/synth/` are
   vendored but not compiled: they are not referenced by the tempo/onset path
   and several of them require libsndfile/libav (which is why `src/io` is
   excluded).

---

## NOT patched, but load-bearing for whoever writes the adapter

### aubio is sample-rate correct, unlike BTrack

There is **no hard-coded 44100 in aubio's tempo mathematics** (the only `44100`
in the tempo sources is inside a dead `AUBIO_BEAT_WARNINGS` debug string at
`src/tempo/beattracking.c:390`, compiled out). The estimator is built from
sample-rate-aware quantities:

- `new_aubio_beattracking()` (`beattracking.c:65`) sets the 120 BPM prior as
  `rayparam = 60 * samplerate / 120 / hop_size` — in detection-function frames,
  so it scales with the rate.
- `aubio_beattracking_get_bpm()` (`beattracking.c:424`) returns
  `60 / (hop_size * bp / samplerate)`, i.e. `60 * samplerate / (hop_size * bp)`.

The consequence is that **no resampling is required**: feeding 48 kHz audio at a
fixed hop with `samplerate = 48000` is unbiased. The adapter passes the true
device rate and the true hop; the failure mode is declaring the wrong rate (see
the adapter tests: passing `44100` for 48 kHz audio biases the estimate by
`44100/48000 - 1 = -8.1 %`, which is exactly the `-8.1 %` trap BTrack had for a
different reason).

Note a real (non-bias) property of the fixed hop: `new_aubio_tempo()`
(`tempo.c:188`) sets the autocorrelation window to
`winlen = next_power_of_two(5.8 * samplerate / hop_size)` DF frames. At
44.1 kHz / hop 512 that is `512` frames (≈6.0 s); at 48 kHz / hop 512 it is
`1024` frames (≈10.9 s), because `5.8 * 48000 / 512 = 543.75` just crosses the
512 boundary. The window length is not a rate bias (BPM scaling is exact) but it
does change acquisition/hold behaviour between rates; the adapter's tests pin
tempo, not this window, and TRACK-002's note reports the measured corpus effect.

### There is no blocking / non-causal prediction API in 0.4.9

The task brief warned that aubio's beattracker "offers a non-causal 'prediction'
mode that blocks waiting for enough audio" and can call
`aubio_beattracker_set_btstate`/`get_btstate`. **That API does not exist in
0.4.9.** There is no `btstate`, `set_btstate` or `get_btstate` symbol anywhere in
`src/` (verified by grep). The public online entry point is
`aubio_tempo_do()` (`tempo.c:57`), which consumes exactly one hop and returns;
the internal `aubio_beattracking_checkstate()` (`beattracking.c:286`) is the
context-model state machine, not a blocking wait, and it is called inline from
`aubio_beattracking_do()` (`beattracking.c:183`). The adapter uses only
`aubio_tempo_do`, `aubio_tempo_get_last`, `get_period`, `get_bpm`,
`get_confidence`, `set_silence` and the `new_`/`del_` pair. None can block. See
`tests/jam/AubioBackendTests.cpp` case `beatsAreNeverWithheldForFutureAudio`.

### Phase: aubio exposes the beat *position*, not a phase01 field

aubio exposes `aubio_tempo_get_last()` — the sample time of the most recent
detected beat, including the sub-hop offset computed in `aubio_tempo_do()`
(`tempo.c:92-99`, `last_beat = total_frames + ROUND(frac * hop_size)`) — and
`aubio_tempo_get_period()` (`hop_size * bp`, in samples). There is **no**
`aubio_tempo_get_phase()` and no phase getter in `beattracking.h`. The adapter
therefore normalises the exposed beat *position* against the exposed beat
period to produce `beatPhase01`, rather than free-running a phase like the BTrack
adapter must (`phaseValid` is false until a beat exists). This is reported as
what it is in the adapter header; it is more than BTrack gives and less than a
dedicated phase output.
