# Third-party credits — Guitar Companion

This program includes third-party material. Each item is listed below with its
license and the required attribution. (Guitar Companion's own code is AGPLv3;
these materials keep their own licenses.)

## Libraries compiled into the program

Everything below is **inside the binary you run**, not a tool used to build it.
This section was missing entirely until an audit went looking for it: the file
credited samples, fonts and adapted DSP, but not the libraries the amp engine
is actually made of. Two of them are MIT, whose notice is required to travel
with binary distributions — so this was an unmet obligation, not just a
courtesy overlooked.

### Neural Amp Modeler Core — MIT

The neural amp engine: it loads `.nam` captures and runs them. Pinned at v0.5.4
(submodule `third_party/NeuralAmpModelerCore`).

- **Source:** <https://github.com/sdatkinson/NeuralAmpModelerCore>
- **License:** MIT · `Copyright (c) 2023 Steven Atkinson`

### AudioDSPTools — MIT

The **Lanczos resampler**, which is what lets a capture recorded at one sample
rate run at your interface's. The README lists that as a feature of this
program; the code is Steven Atkinson's, included directly by
`src/PluginProcessor.cpp` from NAM Core's `Dependencies/`.

- **Source:** <https://github.com/sdatkinson/AudioDSPTools>
- **License:** MIT · `Copyright (c) 2023 Steven Atkinson`

### Eigen — MPL2

The linear algebra behind every layer of the neural network — the matrix maths
in the hottest loop of the whole program. Header-only, compiled in through NAM
Core's `Dependencies/eigen`.

- **Source:** <https://eigen.tuxfamily.org>
- **License:** primarily **MPL2**; some files carry BSD, Apache or Minpack
  terms, all MPL2-compatible. See `COPYING.MPL2`, `COPYING.BSD`,
  `COPYING.APACHE` and `COPYING.MINPACK` in that directory.

### nlohmann/json — MIT

Reads the `.nam` files themselves: architecture, weights, metadata. Version
3.12.0, header-only, compiled in through NAM Core's `Dependencies/nlohmann`.

- **Source:** <https://github.com/nlohmann/json>
- **License:** MIT · `SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann`

### JUCE — dual licence

The audio framework, the plugin formats and the UI toolkit. Pinned at 8.0.15
(submodule `third_party/JUCE`). Used here under the **AGPLv3** option, which is
why this project is AGPLv3 too.

- **Source:** <https://juce.com> · `Copyright (c) Raw Material Software Limited`
- JUCE bundles Steinberg's **VST3 SDK** to build the VST3 target
  (`modules/juce_audio_processors*/format_types/VST3_SDK`). It is Steinberg's,
  dual-licensed GPLv3 / proprietary; this project uses the GPLv3 option.

### Experimental live rhythm backend — BTrack, kiss_fft and libsamplerate

Product builds with `GUITAR_COMPANION_LIVE_JAM=ON` and `JAM_ENABLE_BTRACK=ON`
link the existing BTrack adapter for the experimental live Jam slice. This does
not select the production tracker; the portable core remains tracker-free.

- **BTrack 1.0.7:** <https://github.com/adamstark/BTrack>, pinned at
  `9d6127618a5679e9caa74c594b88f1d74f0e035f`. GNU GPLv3; Copyright
  2008–2014 Queen Mary University of London / Adam Stark. Licence and original
  authorship: `third_party/BTrack/LICENSE.txt` and source headers. The complete
  vendored source is included in this repository's corresponding source.
- **kiss_fft130:** bundled in that BTrack pin; Copyright (c) 2003-2010 Mark
  Borgerding. BSD-style notice reproduced below from
  `third_party/BTrack/libs/kiss_fft130/COPYING`.
- **libsamplerate 0.2.2:** <https://github.com/libsndfile/libsamplerate>, pinned
  at `0844c208f683527c08ea8a80acc13b398aa9c8bf`. BSD-2-Clause notice reproduced
  below from `third_party/libsamplerate/COPYING`. This is BTrack's causal onset-
  function resampler, separate from the guitar model's Lanczos resampler.

#### kiss_fft notice

```
Copyright (c) 2003-2010 Mark Borgerding

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the author nor the names of any contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

#### libsamplerate notice

```
Copyright (c) 2012-2016, Erik de Castro Lopo <erikd@mega-nerd.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### The MIT notices, in full

MIT requires this text to accompany the software. It covers NAM Core and
AudioDSPTools (same holder) and, separately, nlohmann/json.

```
MIT License

Copyright (c) 2023 Steven Atkinson              [NAM Core, AudioDSPTools]
Copyright (c) 2013 - 2025 Niels Lohmann         [nlohmann/json]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Drum grooves and fills — Groove MIDI Dataset

Part of the grooves-and-fills library of the **Drums module** was derived from
the **Groove MIDI Dataset**, quantized and adapted to the internal 1-bar format.

- **Source:** Groove MIDI Dataset — Google Magenta
  <https://magenta.tensorflow.org/datasets/groove>
- **License:** Creative Commons Attribution 4.0 International (**CC BY 4.0**)
  <https://creativecommons.org/licenses/by/4.0/>
- **Attribution:** "Groove MIDI Dataset" by Google LLC (Magenta), used under
  CC BY 4.0. The patterns were quantized to a sixteenth-note grid and remapped
  to 9 voices; they are derivative works.

## Metal grooves and fills — midi-drums

Part of the **metal** grooves and fills was ported (positions and velocities)
from the pattern definitions of the **midi-drums** project.

- **Source:** midi-drums — fsecada01
  <https://github.com/fsecada01/midi-drums>
- **License:** MIT (declared in the project's `pyproject.toml`:
  `license = { text = "MIT" }`)
- **Attribution / MIT notice:**

  ```
  MIT License

  Copyright (c) fsecada01 (midi-drums)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  ```

## Internal drum kit samples — GMRockKit

The internal drum sampler uses the samples of **GMRockKit** (see also
`assets/drums/ORIGEM.txt`).

- **Kit:** GMRockKit — "A Sampled 5pc Pearl DX Series Drumkit"
- **Authors:** Glen MacArthur / Sebastian Moors
- **License:** GPL (compatible with this project's AGPLv3)
- **Source:** the Hydrogen repository
  <https://github.com/hydrogen-music/hydrogen/tree/main/data/drumkits/GMRockKit>

## Typography

All four faces below are embedded in the binary and all four are OFL 1.1; the
licence text for each is in `assets/fonts/*-OFL.txt`.

- **Archivo** — the interface font, the one most of the UI is set in.
  `Copyright 2020 The Archivo Project Authors`
  <https://github.com/Omnibus-Type/Archivo>
- **Space Grotesk** and **JetBrains Mono** — display and monospace.
- **Leland** — MuseScore's SMuFL-compliant music font, used by the Drums
  notation renderer. Copyright (c) 2025 MuseScore Limited; distributed under
  the SIL Open Font License 1.1 with Reserved Font Name "Leland". The copy is
  pinned to MuseScore commit `73d6c2594fb2a90497d4abdc40b825849cb34d43`.
  See `assets/fonts/Leland-OFL.txt` and
  <https://github.com/musescore/MuseScore/tree/main/fonts/leland>.

## Drum notation — MuseScore

The Drums module's score owes MuseScore more than the font, and the font entry
above understated it.

Everything that is a *symbol* on that staff is a **SMuFL glyph from Leland**:
the percussion clef (U+E069), the black and X note heads (U+E0A4, U+E0A9), the
parentheses that mark ghost notes (U+E0F5/U+E0F6), the rests and the time
signature digits. And the engraving those glyphs sit in — a percussion staff
read as pitch-mapped voices, a time signature written only where it changes,
beams grouped by the metre, ghosts in parentheses and accents above the head —
is standard music engraving as **MuseScore renders it**, which is where those
conventions were read from.

To be precise about what that is and is not: **no MuseScore code was copied or
ported**, and MuseScore is not a dependency of this program. The stems, beams
and staff lines are drawn by this project's own vector code (`DrumOverlay.cpp`).
What was taken is the font — under its OFL licence, as above — and the
conventions, which are the shared vocabulary of written music and belong to
nobody. The credit is here because "we used their font" would not describe what
the screen actually shows.

- **Source:** MuseScore — <https://github.com/musescore/MuseScore>
- **SMuFL** (Standard Music Font Layout), the specification Leland implements,
  is maintained by the W3C Music Notation Community Group —
  <https://w3c.github.io/smufl/>.

## Adapted DSP code — Airwindows (MIT)

Three effects are **ports of Airwindows code**, not merely inspired by it:
**Tape** (from ToTape/IronOxide), **Console**, and the **Valve** variation of
Drive (from Tube). Airwindows is by Chris Johnson and is released under MIT,
which requires this notice to travel with the software.

- **Source:** Airwindows — Chris Johnson <https://github.com/airwindows/airwindows>
- **License:** MIT
- **Notice:**

  ```
  MIT License

  Copyright (c) Chris Johnson / Airwindows

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  ```

> The copyright line above names the author; when in doubt, the authoritative
> text is the `LICENSE` file in the upstream repository at the pinned commit.

## Study references — where the ideas came from

`docs/EFEITOS.md` is the honest, per-effect map: for **every** effect it records
the classic gear the variation chases, the open-source project read to study
topology and parameter ranges, and what actually runs in the code. Anything that
is a **port** of someone else's code is labelled as such there — everything else
is DSP written for this project (`juce::dsp`, RBJ biquads) after reading the
references.

The projects studied are pinned as submodules under `references/` and are **not
compiled into, linked against, or shipped with** this program:

| Project | License | Read for |
|---|---|---|
| [Airwindows](https://github.com/airwindows/airwindows) | MIT | Ring Mod, Bitcrusher, Exciter, modulation — **and the three ports above** |
| [BYOD](https://github.com/Chowdhury-DSP/BYOD) | GPLv3 | drive stages |
| [Dragonfly Reverb](https://github.com/michaelwillis/dragonfly-reverb) | GPLv3 | reverb |
| [Guitarix](https://github.com/brummer10/guitarix) | GPLv2-or-later | slow gear, wah, drive, modulation |
| [GxPlugins.lv2](https://github.com/brummer10/GxPlugins.lv2) | GPL | drive, modulation |
| [LSP Plugins](https://github.com/lsp-plugins/lsp-plugins) | GPLv3 | compressor, de-esser |
| [rkrlv2](https://github.com/ssj71/rkrlv2) (Rakarrack port) | GPLv2 | pitch shifter, harmonizer |
| [ToobAmp](https://github.com/rerdavies/ToobAmp) | GPLv3 | gate, pre-EQ, modulation |

Reading source to learn a technique is not copying it: algorithms and topologies
are not covered by copyright, only the concrete expression is. That distinction
is the reason `docs/EFEITOS.md` separates "study" from "port" — so the line is
auditable rather than a claim. If you believe something crossed that line, open
an issue and it will be corrected or removed.

## Built-in VST3 plugin catalog

The in-app installer (Tone Store → Plugins tab) **downloads from each project's
official release** and extracts the `.vst3`; each plugin keeps its own license.
See `plugins/README.md` for the list, versions and official sources.

**This repository redistributes no plugin binary at all.** It used to ship three
release zips under `plugins/offline/` as an offline fallback; two of them were
GPL (Dragonfly Reverb, Zam Plugins), and shipping GPL **binaries** carries the
obligation to provide the corresponding source to whoever receives them. Since
the installer already fetches from each project's own release, that fallback was
removed rather than carrying an obligation for no real benefit.

## TONE3000

The Tone Store talks to the public TONE3000 API under its **free,
non-commercial tier**, using the OAuth prompt flows and bounded list endpoints
that tier allows. Each user supplies their own publishable key — **no credential
ships in this repository**.

**Guitar Companion is not affiliated with, sponsored by, or endorsed by
TONE3000.** The TONE3000 name and logos belong to them and are used only to
identify the service, following their published design guidance.
See <https://www.tone3000.com/api>.

Because that tier browses through TONE3000's own web pages, the Tone Store
hosts those pages in an embedded browser (see below). The pages shown in it are
served by tone3000.com and are theirs; Guitar Companion only opens the authorisation
URL and reads the redirect that comes back.

## Microsoft Edge WebView2 SDK

The embedded TONE3000 browser uses **WebView2**, through JUCE's
`WebBrowserComponent`. The build links `WebView2LoaderStatic.lib` from the
`Microsoft.Web.WebView2` NuGet package (pinned in `CMakeLists.txt`, downloaded
into the gitignored `third_party/webview2/` at configure time — **no Microsoft
binary is committed here**). The rendering engine itself is the WebView2
Runtime already installed on the machine; nothing of it is redistributed.

- **Source:** <https://www.nuget.org/packages/Microsoft.Web.WebView2>
- **License:** BSD-3-Clause style (the package's `LICENSE.txt`)
- **Attribution / notice:**

  ```
  Copyright (C) Microsoft Corporation. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the following disclaimer
  in the documentation and/or other materials provided with the
  distribution.
     * The name of Microsoft Corporation, or the names of its contributors
  may not be used to endorse or promote products derived from this
  software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  ```

## Interface ideas

Some interface concepts of the Drums module (browser, humanization) were
inspired by **DrumGroovePro** (InToEtherion, GPLv3) —
<https://github.com/InToEtherion/DrumGroovePro>. No code was copied; only ideas,
which are not covered by copyright.
