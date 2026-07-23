# Third-party credits — PedalForge NAM

This program includes third-party material. Each item is listed below with its
license and the required attribution. (PedalForge NAM's own code is AGPLv3;
these materials keep their own licenses.)

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

- **Space Grotesk** and **JetBrains Mono** — SIL Open Font License 1.1 (OFL).
  See `assets/fonts/*-OFL.txt`.

## Built-in VST3 plugin catalog

The catalog (Tone Store → Plugins tab) only **downloads from the official
releases** and extracts the `.vst3`; each plugin keeps its own license (GPLv3,
MIT, GPLv2+). See `plugins/README.md` for the list, versions and official sources.

## Interface ideas

Some interface concepts of the Drums module (browser, humanization) were
inspired by **DrumGroovePro** (InToEtherion, GPLv3) —
<https://github.com/InToEtherion/DrumGroovePro>. No code was copied; only ideas,
which are not covered by copyright.
