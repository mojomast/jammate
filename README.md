# 🎸 PedalForge NAM

**Personal guitar amp sim** — Standalone + VST3, built on [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) (neural captures of real amps) and JUCE 8, with a store integrated with [TONE3000](https://www.tone3000.com).

![Windows](https://img.shields.io/badge/Windows-10%2F11%20x64-0078d4) ![JUCE](https://img.shields.io/badge/JUCE-8.0.15-8bc34a) ![NAM](https://img.shields.io/badge/NAM%20Core-v0.5.4%20(A2)-33c9d6) ![Status](https://img.shields.io/badge/status-functional%20%C2%B7%20evolving-33c9d6)

![Main screen](docs/screenshots/rig.png)

---

## 📊 Project status

| Phase | Deliverable | Status |
|-------|-------------|--------|
| 0 | JUCE shell (Standalone + VST3), passthrough, ASIO | ✅ |
| 1 | NAM Core integrated into the build (WHOLE_ARCHIVE, C++20) | ✅ |
| 2 | Loading `.nam` captures + real DSP, RT-safe swap | ✅ |
| 3 | Automatic resampler (Lanczos), Noise Gate, Cab IR, presets | ✅ |
| 4 | Tone Store: OAuth PKCE + search + downloads from TONE3000 | ✅ |
| 5 | Design v2, full pedalboard chain, tuner, photos, UX | ✅ |

**Validation**: each phase was tested with a real guitar (Focusrite ASIO, 48 kHz, 128 samples) and headless DSP tests (loading the A2/WaveNet/LSTM architectures, resampling 48→44.1 kHz).

## ⚡ Features

- **Pedalboard-style signal chain**: shows only the effects in use; 23 effects available in the **`+ EFFECT`** drawer (by category), all reorderable via drag-and-drop
- **Neural-capture amp**: any `.nam` file (A1/A2 architectures), with a GAIN that saturates the model like the real amp, a B/M/T/Presence tone stack and Master
- **Automatic resampler**: captures run at the sample rate they expect, at any interface sample rate (~0.6 ms latency, reported to the host)
- **Parallel rigs**: up to 3 complete **AMP+CAB** pairs (capture + own knobs + per-lane IR), always in pairs, summed in the **Mixer** card (per-rig blend + global AIR)
- **Cab IR** by convolution (wav/aiff/flac, glitch-free swap), low/high cut and phase per lane
- **Tone Store (TONE3000)**: OAuth login, search with photos, filters by type/tags/A2 architecture, variation picker (mics/channels), downloads with progress, offline library, no re-downloads
- **Real tuner** (NSDF pitch detection) with on/off
- **Presets**: 1-click save, "Save as", modified indicator (•), factory presets, ◂ ▸ navigation
- **Photos** of the loaded amp/cabinet on the rig cards
- **UX**: knobs with a default detent, double-click resets, wheel adjusts, Ctrl = fine, type-in values; tooltips everywhere; shortcuts (space, T, ←/→, Esc); IN/OUT + real CPU meters
- **Pitch/octaver, Looper (60 s, overdub, WAV export) and Limiter** with a clip indicator
- **Per-effect variations** (Drive ×8, Comp ×4, Delay ×5, Reverb ×5, Mod ×6, Pitch ×5) with classic inspirations and study sources documented in **[docs/EFEITOS.md](docs/EFEITOS.md)**
- **External VST3 plugin slot**: host any third-party effect in the chain, with its own panel, MIX and state saved in presets
- **Real-time safety**: zero allocation/locks/IO in the audio path (a non-negotiable project rule)

## 🖼️ Screens

| Tone Store | Offline library |
|---|---|
| ![Tone Store](docs/screenshots/tone-store.png) | ![Library](docs/screenshots/biblioteca.png) |

## 🔧 Build (Windows)

Requirements: VS 2022 (Build Tools or Community) with C++, CMake ≥ 3.22, Git.

```powershell
git clone <repo-url> GuitarRigNAM
cd GuitarRigNAM
# only what the build needs (references/ is optional and heavy):
git submodule update --init --recursive third_party

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artifacts in `build\GuitarRigNAM_artefacts\Release\` (`Standalone\PedalForge NAM.exe` and `VST3\PedalForge NAM.vst3`).

### ASIO (recommended)

The Steinberg SDK cannot be redistributed. Download it from <https://www.steinberg.net/asiosdk>, extract it to `third_party/asiosdk/` (it must contain `common/iasiodrv.h`) and reconfigure with:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DASIOSDK_DIR="$PWD\third_party\asiosdk"
```

`third_party/asiosdk/` is in `.gitignore` and **never** goes into Git.

## 🔑 TONE3000 setup

The API requires your own key (free):

1. Create an account at [tone3000.com](https://www.tone3000.com) → Settings → API Keys
2. Register the redirect `http://localhost:53682/callback`
3. Paste the key (`t3k_pub_…`) into `Documents\PedalForge NAM\tone3000.json` (the app creates the template)
4. In the app: Tone Store → **Connect TONE3000**

> ⚠️ **Security**: `tone3000.json` holds your key and your account's refresh token. It lives in `Documents\PedalForge NAM\` — **outside this repository** — and must never be committed anywhere.

## 📁 Structure

```
src/                  plugin code (processor, editor, store, TONE3000 client)
docs/EFEITOS.md       sources/references for each effect and variation
assets/fonts/         Space Grotesk + JetBrains Mono (OFL, embedded in the binary)
docs/screenshots/     project screenshots
references/           OPTIONAL submodules: reference projects for effects (see references/README.md)
third_party/JUCE            submodule pinned at 8.0.15 (required to build)
third_party/NeuralAmpModelerCore  submodule pinned at v0.5.4, A2 support (required to build)
```

User data (outside the repo): `Documents\PedalForge NAM\` — `Captures/`, `IRs/`, `Presets/`, `tone3000.json`.

## 🗺️ Roadmap

**Phase 6 — done:**
- [x] Parallel cabs: 1–3 IR slots with blend, low/high cut and phase per cab + global AIR *(evolved into AMP+CAB rigs in phase 8)*
- [x] Chain reorderable via **drag-and-drop** (drag the effect cards; amp+cabs are a fixed anchor)
- [x] **ECO** mode (a light capture downloaded alongside) with **auto-ECO** at CPU > 90% + ⚠ warning on the meter
- [x] Architecture **V1/V2** badges on amps and IRs
- [x] P1 effects: pedal compressor (Clean/Country/Lead presets), gate with hold + 6 dB hysteresis, pre-EQ before the NAM

**Phase 7 — P2 effects (done):**
- [x] Per-effect variations on the card: Drive ×6 (Boost/Screamer/Blues/Distortion/Fuzz/Heavy), Comp ×3, Delay ×4, Reverb ×4
- [x] Delay: **TAP tempo** with subdivisions (1/4, 1/8, 1/8., 1/16), stereo **Ping-Pong** and **trails**
- [x] Hall/Room/Plate/**Spring** reverbs, true stereo and trails
- [x] **Modulation** card: Chorus, Phaser, Flanger and harmonic Tremolo

**Phase 8 — parallel AMP+CAB rigs (done):**
- [x] Fixed architecture: each parallel lane is a complete **AMP+CAB** pair (NAM capture with its own knobs + IR), not just parallel IRs
- [x] Dedicated **Mixer** card: sums the lanes with per-rig blend, global AIR and +/− buttons that add/remove the whole pair (min. 1, max. 3)
- [x] Truly parallel visuals: **stacked** lanes with a split bus at the input and a sum bus at the Mixer (with 1 rig, keeps the classic big card)

![Parallel rigs](docs/screenshots/rigs-paralelos.png)

**Phase 9 — P3 effects (done):**
- [x] **Pitch** card: 2-head granular octaver (Octave ↓/↑, Fifth, Detune) with MIX/LEVEL — validated headless (440 Hz → 220/660/880 Hz)
- [x] **Looper** card: up to 60 s, REC → closes and plays → overdub, PLAY/STOP, CLEAR and **WAV export** (`Documents\PedalForge NAM\Loops`)
- [x] **Limiter** card: brickwall at the end of the chain with a gain-reduction bar + **CLIP** warning on the OUT meter

![P3 effects](docs/screenshots/efeitos-p3.png)

**Phase 10 — extra variations + documented references (done):**
- [x] **[docs/EFEITOS.md](docs/EFEITOS.md)**: each effect and variation with its classic inspiration, the reference project studied (`references/`) and the implementation basis; selector tooltips cite the sources
- [x] New variations from the reference list: Drive **Valve** (Airwindows Tube, MIT) and **Metal** (Guitarix) · Comp **Squeezer** · Delay **Ducking** · Reverb **Shimmer** (octave up in the wet) · Mod **Vibrato** and **Rotary** (Leslie) · Pitch **Fourth**
- [x] Fix: Drive's Boost and Heavy Fuzz had a menu but fell back to the Screamer voicing — now they have their own voicing and clipping

**Phase 11 — external VST3 plugin slot (done):**
- [x] **VST3 Plugin** card in the chain: hosts any VST3 effect from disk (Dragonfly, LSP, Airwindows, BIAS FX…) via JUCE hosting
- [x] LOAD/CHANGE buttons, **PANEL** (the plugin's UI in its own window) and REMOVE + MIX knob (dry/wet) + bypass LED
- [x] RT-safe instance swap (same pending/retired protocol as the NAM models); mono → stereo for the guest with stereo return via `stereoExtra`
- [x] The plugin's **path and internal state** saved in presets (base64), with automatic restore
- [x] Tested with BIAS FX 2 (processing + panel)

**Phase 12 — P4 cards: 10 new effects, one card per effect (done):**
- [x] **Wah** (Auto/Manual/LFO) · **Slow Gear** (swell) · analog **Octaver** · **Ring Mod** · **Bitcrusher** — pre-amp side
- [x] **Diatonic Harmonizer**: detects the played note (autocorrelation) and sings the 3rd/5th/6th/octave WITHIN the chosen key/scale
- [x] **Exciter** · **De-esser** · **Tape** (Airwindows ToTape, MIT) · **Console** glue (Airwindows Console, MIT) — post-amp side
- [x] Each effect has its own card with dedicated controls (total: 23 cards in the chain, all reorderable)
- [x] VST3 slot: the LOAD button became a menu with the plugins installed on the system + a table of recommended free ones in docs/EFEITOS.md

**Phase 13 — UX: effects drawer + navigation (done):**
- [x] The chain shows **only the effects in use**; a **`+` on each connector** adds an effect at that exact position, and the dashed **`+ EFFECT`** button at the end inserts at the musically correct position — both open the drawer by category (Dynamics · Drive & Filter · Pitch · Modulation & Color · Ambience · Extras)
- [x] **✕** on each card returns the effect to the drawer (settings preserved); presets save the assembled pedalboard
- [x] **Disabled cards are dimmed** — your eye finds instantly what is sounding
- [x] **Mouse wheel scrolls the chain** and **dragging the background pans** (hand cursor), like in a DAW

![Effects drawer](docs/screenshots/gaveta-efeitos.png)

**Phase 14 — stage mode (done):**
- [x] **STAGE** chip (or key **F**): hides the chain and shows the essentials huge — preset name (with modified indicator), loaded capture, a **big tuner** (note + cents ruler, green when in tune) and shortcut hints
- [x] On stage the tuner works even with the TUNER chip off; clicking the sides navigates presets, the center opens the menu; **Esc/F** returns to editing

![Stage mode](docs/screenshots/modo-palco.png)

**Phase 15 — roadmap closed (done):**
- [x] **Spectrum analyzer**: card with live 2048 FFT (24 log bands, 40 Hz–16 kHz)
- [x] **Meters with peak-hold** + micro-interactions (hover on `+`/`✕`, hand cursor)
- [x] **File drag-and-drop**: drop `.nam` on the amp, an IR on the cab and a `.vst3` on the external slot (with target highlight)
- [x] **Tuner with MUTE** (silences the output while tuning) · stereo delay/reverb (since phase 7)
- [x] **★ favorites on TONE3000** (persisted + "★ only" filter) · **rig A/B** (compares two complete setups) · **quick recorder** (24-bit WAV of the output in `Documents\PedalForge NAM\Recordings`)
- [x] UX fixes: immediate relayout when removing/adding cards (no stale targets under the mouse), deferred relayout while dragging a knob

**Phases 16–17 — external VST3 plugins (done):**
- [x] **Up to 8 slots** of VST3 plugins in the chain (in practice the limit is CPU); LOAD menu by category
- [x] **Built-in catalog** of 8 open-source plugins (Dragonfly, Airwindows, Zam, AIDA-X, Fire, Wolf Shaper, PeakEater, Surge XT Effects): a **Plugins** tab in the Tone Store with an INSTALL ⇄ UNINSTALL toggle, progress and pinned versions — **direct download only**: installs by extracting the .vst3 into the user folder (no admin) and uninstalls by deleting the file, no installer
- [x] `plugins/` in the repo: an alternative script + offline copy (32 MB) with licenses
- [x] **Renamed to PedalForge NAM** (avoids confusion with NI's Guitar Rig); old data migrates automatically

**Phase 18 — Drums module (in progress):**
- [x] Engine: sample-accurate sequencer in processBlock (2 bars × 16 steps, 9 voices, accent/ghost), swing, click, count-in; its own bus summed into the master (does not go through the guitar chain)
- [x] Sound sources: **internal synthesized sampler** (works out of the box) and a **hosted drum VST3** (GM MIDI channel 10, pending/retired protocol, panel in its own window)
- [x] UI v4 "the staff is the track" (**Drums** button in the top bar): the central area shows the **whole section (4 bars) on a continuous staff**; **1-bar** grooves are **dragged from the library straight onto the bar on the staff**; clicking the staff edits (empty→hit→accent→ghost); sections as tabs (**+ SECTION** = +4 bars); **FOLLOW** turns the page on play; the **GRID** chip opens the 16-step grid of the selected bar
- [x] **Massive reorganized library**: ~460 factory grooves+fills; **each genre gathers its grooves AND its fills** (ALL/GROOVES/FILLS sub-filter; fills with an orange border); 16 genres incl. **SOUL/GOSPEL** and **GENERAL** (generic fills). Part comes from the **Groove MIDI Dataset** (Google Magenta, CC BY 4.0 — see `THIRD_PARTY.md`), quantized; the dataset's jazz/blues are left out (swing doesn't fit the straight grid). Each card shows a **notation thumbnail**; + **My bars** (`Documents\PedalForge NAM\compassos`)
- [x] Timeline/BPM/swing/source saved in the preset (A/B included; old formats migrate)
- [x] **Column browser** (DrumGroovePro style, GPLv3): Genre (with counts) | Grooves/Fills | **Preview** with the big notation + "apply to bar" + drag; **humanize** (velocity/micro-timing/round-robin) of the internal kit; **EDIT button** (edit notes ⇄ assemble: drag the whole bar to reposition/copy)
- [x] **Per-bar time signature**: each bar can have its own meter (4/4, 3/4, 2/4, 6/8, 12/8 + Custom); the signature is written only when it changes (notation convention), the width adjusts to the number of steps (engine with variable steps per bar, cap 32) and the beams group by meter (compound in threes). Clicking the signature on the bar header opens the menu; the grid and playhead follow. Saved in the preset.
- [x] **Groove generator** (ported from midi-drums, MIT): genre/style/drummer + parameters generate a bar honoring its time signature; per-bar role (Verse/Chorus/Bridge/Fill) drives the generation
- [x] **Ribbons + morph**: each screen carries a live ribbon of the other at the top (drums ribbon on guitar with playhead; guitar ribbon on drums with amp + active pedals), and clicking morphs into the full screen
- [ ] Pending: external MIDI output, per-piece mini-mixer, copy bar→bar by dragging
- Approved design: `docs/design/`; dev flags `GUITARRIG_OPEN_DRUMS=1|play|meter|gen|genfill`

**Next:**
- [ ] Future ideas: chain minimap, MIDI learn, per-song scene snapshot
- [ ] Full visual redesign (see `docs/design/redesign-brief.md`)

## 🤝 Contributing

Contributions are welcome! Read **[CONTRIBUTING.md](CONTRIBUTING.md)** (build, code map, real-time safety rules and known MSVC/JUCE pitfalls) and use the issue/PR templates. Unchecked roadmap items are a great starting point.

## 📜 Licenses

This project is licensed under **[AGPLv3](LICENSE)** — required by using JUCE 8 in the open-source tier. Dependencies:

- **JUCE 8** — AGPLv3 (personal/open-source use) · **NAM Core** — MIT · **AudioDSPTools** — Apache-2.0/MIT (see repository)
- **Fonts** — SIL Open Font License (text in `assets/fonts/`)
- **ASIO SDK** — Steinberg license (manual download, not redistributed)
- Captures/IRs downloaded from TONE3000 have their own per-tone licenses (CC/T3K) — respect them when redistributing tones.
