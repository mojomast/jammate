# 🎸 Guitar Companion

**A companion for the guitarist** — Standalone + VST3, built on [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) (neural captures of real amps) and JUCE 8, with a store integrated with [TONE3000](https://www.tone3000.com).

![Windows](https://img.shields.io/badge/Windows-10%2F11%20x64-0078d4) ![JUCE](https://img.shields.io/badge/JUCE-8.0.15-8bc34a) ![NAM](https://img.shields.io/badge/NAM%20Core-v0.5.4%20(A2)-33c9d6) ![Version](https://img.shields.io/badge/version-v0.1-33c9d6) ![Status](https://img.shields.io/badge/status-beta-e0a35c)

![The Rig screen](docs/screenshots/01-rig.jpg)

## 🎯 The idea

This started from a simple wish: **a tool that helps guitarists keep practising,
at home or on the road, and catch an idea on the fly**.

Not just another amp sim. Picking up the guitar alone tends to end the same way
— the same riff, the same key, the same four bars, and then you put it down.
What was missing was on the other side of the sound: something **to play with**,
something that answers back, something that makes the next idea show up.

So it grew in that direction. The amp is the foundation, but around it there is a
drummer you can write a groove for, an arrangement that follows you through the
sections of a song, a rig that changes on its own when the chorus arrives, and a
recorder that keeps what you played before you forget it. **The aim is to help on
the creative side** — to shorten the distance between an idea and hearing it out
loud.

Whether it manages that is for you to judge. It is what everything here was built
towards.

> ### ⚠️ This is a beta
>
> Version **v0.1**. It works, it is used daily on a real rig, and it can still
> have bugs — screens with rough edges, corner cases nobody has hit yet, things
> that break in a setup different from the one it was developed on.
>
> **Keep backups of anything that matters** (your presets live in
> `Documents\Guitar Companion\Presets`) and please **[open an issue](../../issues)**
> when something goes wrong: what you did, what you expected, what happened. A
> report with those three lines is worth more here than any feature request.

> **New here? Read [How it works](#-how-it-works) first.** The window looks busy on purpose — it is a pedalboard, and everything on it is reachable in one click. That section walks through it in the order you would actually use it.

---

## 🌱 Built on open source, and open in return

This program exists because other people published their work. The neural engine, the framework, the DSP it learned from, the drum samples, the music font — none of it was written here first. It would have been impossible to build alone, and it would be dishonest to pretend otherwise.

So the deal is symmetric: **everything this project uses is open source, and this project is open source too** — [AGPLv3](LICENSE), source and history public, no closed core, nothing held back for a paid version. If it helps someone else build their own amp sim, that is the point.

Three rules follow from that, and they are enforced in the repository rather than just stated here:

- **Credit is specific, never vague.** Every effect records *which* project was read for it, and whether the result was **studied** or **ported**. That map is [`docs/EFEITOS.md`](docs/EFEITOS.md), one row per effect. "Inspired by the open-source community" is not a credit; a name and a link is.
- **Licenses are respected in full**, including the boring parts — MIT notices travel with the code, GPL samples keep their terms, fonts keep their OFL. See [`THIRD_PARTY.md`](THIRD_PARTY.md). This repository redistributes **no** third-party binary: plugins are downloaded from each project's own release.
- **If a line was crossed, it gets fixed.** Reading source to learn a technique is not copying it — algorithms are not covered by copyright, only their concrete expression. That is exactly why "studied" and "ported" are separated per effect instead of asserted in bulk: the claim is auditable. **If you think something crossed the line, open an issue** and it will be corrected or removed.

### Where the inspirations came from

The projects this was built on top of, or learned from:

| Project | Credited as | What it gave this program |
|---|---|---|
| [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) | Steven Atkinson | The neural amp engine itself — the core of the whole program |
| [JUCE](https://juce.com) | Raw Material Software Limited | Audio framework, plugin format, UI toolkit |
| [Airwindows](https://github.com/airwindows/airwindows) | Chris Johnson | **Ported** (MIT): Tape, Console and the Valve drive. Also studied for Ring Mod, Bitcrusher, Exciter and modulation |
| [Guitarix](https://github.com/brummer10/guitarix) · [GxPlugins.lv2](https://github.com/brummer10/GxPlugins.lv2) | the Guitarix contributors | Studied: slow gear, wah, drive voicing, modulation |
| [LSP Plugins](https://github.com/lsp-plugins/lsp-plugins) | the LSP Plugins contributors | Studied: compressor and de-esser behaviour |
| [ToobAmp](https://github.com/rerdavies/ToobAmp) | the ToobAmp contributors | Studied: noise gate, pre-EQ, modulation |
| [BYOD](https://github.com/Chowdhury-DSP/BYOD) | Chowdhury DSP | Studied: drive stages |
| [Dragonfly Reverb](https://github.com/michaelwillis/dragonfly-reverb) | the Dragonfly Reverb contributors | Studied: reverb |
| [rkrlv2](https://github.com/ssj71/rkrlv2) | the rkrlv2 contributors, porting Rakarrack | Studied: pitch shifter and harmonizer |
| [GMRockKit](https://github.com/hydrogen-music/hydrogen) | Glen MacArthur · Sebastian Moors, via the Hydrogen project | The internal drum kit's actual samples |
| [MuseScore](https://github.com/musescore/MuseScore) | MuseScore Limited | The drum notation: its **Leland** font supplies the glyphs, and its engraving of a percussion staff is the language the score follows |
| [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove) | Google LLC (Magenta) | Part of the groove and fill library |
| [midi-drums](https://github.com/fsecada01/midi-drums) | fsecada01 | Part of the metal grooves and fills |
| [DrumGroovePro](https://github.com/InToEtherion/DrumGroovePro) | InToEtherion | Interface ideas for the drum browser and humanisation |
| [TONE3000](https://www.tone3000.com) | TONE3000 | The capture library the Tone Store reads (public API, free tier — not affiliated) |

**About that middle column.** Every name in it was taken from the project's own licence or notice file, not from memory. Where a project **declares a named copyright holder**, that name is used verbatim:

- Airwindows — `Copyright (c) 2018 Chris Johnson`
- Neural Amp Modeler — `Copyright (c) 2023 Steven Atkinson`
- JUCE — `Copyright (c) Raw Material Software Limited`, read from the source headers of the exact commit this repository pins

The first two are also the ones whose licences (MIT) legally require the name to travel with the code.

Most of the GPL projects here declare **no single holder** — their `LICENSE` is the plain GPL text and their README carries no copyright statement. For those, "the *X* contributors" is not a hedge, it is the accurate answer: they are years of work by more than one person, and inventing a single author for them would misrepresent that. If you maintain one of these and would rather be credited by name, open an issue and it will be changed.

The projects marked *studied* are pinned as submodules under [`references/`](references/README.md) so any claim above can be checked against the real source. They are **not** compiled into, linked against, or shipped with this program.

Thank you to all of them. If you maintain one of these projects and something here is wrong or uncomfortable, please open an issue — it will be treated as a priority.

---

## 📊 Project status

| Area | What is there | Status |
|---|---|---|
| Amp + cabinet | NAM captures (A1/A2), up to 3 parallel AMP+CAB rigs, IR convolution, automatic resampler | ✅ |
| Effects | 23 effects, reorderable, with variations — plus a slot for any third-party VST3 | ✅ |
| Tone Store | TONE3000 sign-in and browsing inside the app, downloads, offline library, VST3 catalogue | ✅ |
| Drums | Sequencer with real samples or a hosted drum VST3, editable notation, ~157 grooves | ✅ |
| Song / Scenes | Up to 8 sections, one rig snapshot per section, applied on the bar line | ✅ |
| Stage | Big-target live screen, next-scene tile | ✅ |
| Recording | Take as a mix plus separate guitar and drum stems | ✅ |
| Rough edges | Beta: see the warning above, and the [issues](../../issues) | ⚠️ |

**Validation**: everything is tested with a real guitar (Focusrite ASIO, 48 kHz,
128 samples) and headless DSP tests (A2/WaveNet/LSTM architectures, resampling
48→44.1 kHz). There are unit tests for the drums module — see
[`tests/README.md`](tests/README.md).

## ⚡ Features

- **Pedalboard-style signal chain**: shows only the effects in use; 23 effects available in the **`+ EFFECT`** drawer (by category), all reorderable via drag-and-drop
- **Neural-capture amp**: any `.nam` file (A1/A2 architectures), with a GAIN that saturates the model like the real amp, a B/M/T/Presence tone stack and Master
- **Automatic resampler**: captures run at the sample rate they expect, at any interface sample rate (~0.6 ms latency, reported to the host)
- **Parallel rigs**: up to 3 complete **AMP+CAB** pairs (capture + own knobs + per-lane IR), always in pairs, summed in the **Mixer** card (per-rig blend + global AIR)
- **Cab IR** by convolution (wav/aiff/flac, glitch-free swap), low/high cut and phase per lane
- **Tone Store (TONE3000)**: sign-in and the whole catalogue **inside the app** (their own picker runs in an embedded browser), variation picker (mics/channels), downloads with progress, offline library, no re-downloads
- **Drums to play with**: sequencer with real samples or a hosted drum VST3, editable notation, ~157 grooves and ~53 fills across 14 genres, groove generator
- **Song mode**: up to 8 sections, each with its own bars and its own **rig snapshot**, switched on the bar line while you play
- **Real tuner** (NSDF pitch detection) with on/off
- **Presets**: 1-click save, "Save as", modified indicator (•), factory presets, ◂ ▸ navigation
- **Photos** of the loaded amp/cabinet on the rig cards
- **UX**: knobs with a default detent, double-click resets, wheel adjusts, Ctrl = fine, type-in values; tooltips everywhere; shortcuts (space, T, ←/→, Esc); IN/OUT + real CPU meters
- **Pitch/octaver, Looper (60 s, overdub, WAV export) and Limiter** with a clip indicator
- **Per-effect variations** (Drive ×8, Comp ×4, Delay ×5, Reverb ×5, Mod ×6, Pitch ×5) with classic inspirations and study sources documented in **[docs/EFEITOS.md](docs/EFEITOS.md)**
- **External VST3 plugin slot**: host any third-party effect in the chain, with its own panel, MIX and state saved in presets
- **Real-time safety**: zero allocation/locks/IO in the audio path (a non-negotiable project rule)

## 🧭 How it works

There is **one signal path**, and it runs left to right:

```
guitar ──► effects you dragged in ──► AMP + CAB (the neural capture) ──► out
                                      └─ up to 3 of these in parallel
```

Everything else is a **screen that edits one part of that path**. There are six, and you reach all of them from the top bar. You only ever need two of them to make sound.

---

### 1 · Rig — where you build the sound

![Rig](docs/screenshots/01-rig.jpg)

Three fixed columns: **INPUT** on the left, the **chain** in the middle, **OUTPUT/MIXER** on the right. The chain shows *only the effects you are actually using* — an empty chain is normal. Add one with **`+ EFFECT`**, drag cards to reorder, click the **✕** on a card to send it back to the drawer (its settings are kept).

The big card in the middle is the **AMP HEAD**: it loads a `.nam` capture of a real amplifier. **CHANGE** picks a different capture, **VARIANTS** switches between the mics and channels of the same amp. Next to it, **Cab IR** is the speaker cabinet.

**`+ ADD AMP+CAB`** (bottom right) adds a second or third amp+cabinet **in parallel** — they are summed in the MIXER card, with a blend per lane.

> If you load nothing at all, you still get sound: the chain just passes your guitar through.

### 2 · Tone Store — where you get amps

![Tone Store](docs/screenshots/04-store.jpg)

Amps and cabinets come from **[TONE3000](https://www.tone3000.com)**, a community library of neural captures. The grid shows **trending and latest**; **BROWSE ON TONE3000** opens their full catalogue in your browser and brings the tone you pick back into the app.

Each card loads straight into **AMP 1**; the small **▾** next to it targets a specific lane (`Load in AMP 2`) or an IR slot. **My library** is everything you already downloaded, and works offline.

You need a free TONE3000 account and your own API key — see [TONE3000 setup](#-tone3000-setup). It takes about a minute.

### 3 · Drums — to play with

![Drums](docs/screenshots/02-drums.jpg)

A drummer, not a metronome. The centre is **real music notation** — engraved with [MuseScore](https://github.com/musescore/MuseScore)'s Leland font, following how MuseScore draws a percussion staff. Click a note to edit it, drag a groove from the library onto a bar to replace it. A song is up to **8 sections of 4 bars**, each bar with its own time signature — odd meters are first-class here.

The strip at the top mirrors your guitar chain, so you can tweak the amp without leaving the screen. **GENERATE** writes a groove for you from genre, style and complexity.

### 4 · Song / Scenes — one rig per section

![Song](docs/screenshots/03-song.jpg)

A **scene** is a snapshot of your whole guitar rig attached to a drum section. Capture a clean rig on the verse and a lead rig on the chorus, turn on **AUTO-SWITCH**, and the rig changes by itself at the bar line — muted while the new capture loads, so the change is silent.

### 5 · Stage — for playing live

![Stage](docs/screenshots/05-stage.jpg)

The same rig with everything small removed: preset name, tuner, big on/off tiles and a footswitch row. Press **F** to enter, **Esc** to leave.

### 6 · Audio & MIDI — your interface

![Audio](docs/screenshots/06-audio.jpg)

Driver, device, sample rate and buffer. Changes are **staged**: nothing happens until you press **APPLY CHANGES**, and if the device fails to open the previous one is restored instead of leaving you silent.

---

### The first five minutes

1. **Audio & MIDI** → pick your interface, **APPLY CHANGES**. Confirm INPUT moves when you play.
2. **Tone Store** → paste your TONE3000 key, **Connect**, then load any amp into AMP 1.
3. Back on **Rig** → set GAIN and MASTER on the amp card. That is already a full sound.
4. Add effects with **`+ EFFECT`** only once you want them.

## 📥 Install (Windows)

Grab **`Guitar-Companion-0.1-win64-setup.exe`** from the
[releases page](../../releases) and run it. It installs the standalone
application and, if you keep the box ticked, the **VST3** into
`C:\Program Files\Common Files\VST3` so your DAW finds it. There is a proper
uninstaller in Add/Remove Programs.

Requirements: **Windows 10 or 11, 64-bit**.

> **The embedded browser needs Windows 11 — or the WebView2 Runtime.**
> The Tone Store opens TONE3000's own pages *inside* the app, and that uses
> Microsoft's WebView2. **Windows 11 has it out of the box.** On Windows 10 it
> often arrives with Edge, but not always: if it is missing, the installer says
> so, and the store falls back to opening TONE3000 in your normal browser —
> everything still works, it just leaves the app to do it. To get the embedded
> version on Windows 10, install the
> [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)
> (free, from Microsoft).

Prefer not to install? The release also has the plain build if you would rather
copy the files yourself.

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

Artifacts in `build\GuitarCompanion_artefacts\Release\` (`Standalone\Guitar Companion.exe` and `VST3\Guitar Companion.vst3`).

### ASIO (recommended)

The Steinberg SDK cannot be redistributed. Download it from <https://www.steinberg.net/asiosdk>, extract it to `third_party/asiosdk/` (it must contain `common/iasiodrv.h`) and reconfigure with:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DASIOSDK_DIR="$PWD\third_party\asiosdk"
```

`third_party/asiosdk/` is in `.gitignore` and **never** goes into Git.

### Embedded browser (WebView2)

The Tone Store opens TONE3000's picker inside the app, which needs the WebView2
SDK. CMake downloads the pinned NuGet package into `third_party/webview2/` on
the first configure (gitignored — no Microsoft binary is committed) and links
the loader statically; the rendering engine is the WebView2 Runtime already on
the machine. Nothing to install by hand.

Building offline, or without it, is fine:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF
```

The store then falls back to the system browser plus the `localhost:53682`
listener — the path that always existed. The same happens at runtime on a
machine with no WebView2 Runtime.

### Packaging the installer

The Windows installer is an [Inno Setup](https://jrsoftware.org/isinfo.php)
script. Build the app in Release first, then:

```powershell
iscc packaging\guitar-companion.iss
```

The `.exe` lands in `packaging\output\` (gitignored).

## 🔑 TONE3000 setup

The API requires your own key (free):

1. Create an account at [tone3000.com](https://www.tone3000.com) → Settings → API Keys
2. Register the redirect `http://localhost:53682/callback`
3. In the app: **Tone Store → paste the key → Save key**, then **Connect TONE3000**

Sign-in and browsing happen **inside the app**: TONE3000's own pages open in an
embedded browser panel, and the tone you pick comes straight back into the rig.
The sign-in is remembered (its profile lives in `Documents\Guitar Companion\webview\`),
so you only do it once. The redirect above is still what your key registers —
the panel simply catches it instead of a local web server. To sign in as a
different account, use TONE3000's own account menu on the page inside the panel.

The panel header has two switches, because TONE3000 filters by **one value at a
time** on each and Guitar Companion reads more than one:

- **FORMAT — NAM / IR.** They also publish in AIDA-X, Proteus and Amped Roots,
  which belong to other ecosystems; scoping to one of ours keeps those out.
  Opening the picker from a cab's **CHANGE** starts on IRs, from an amp's
  **LOAD/CHANGE CAPTURE** on NAM.
- **ARCHITECTURE — A1 + CUSTOM / A2** (captures only; an IR has none). Asking
  for no architecture is not neutral: it means "A1 + Custom", which hides every
  A2-only tone behind a "Not supported" notice. Guitar Companion loads both, so flip
  the switch when a tone you want says that.

The store's own grid is a curated list from TONE3000 and is not format-scoped,
so a foreign-format tone can still appear there. Those show their format on the
badge and say so if you pick one, instead of downloading a file that would never
load. (AIDA-X itself is in the Plugins tab, if you want it.)

Everyone uses their **own** key, so your downloads and your rate limit are yours and no credential ships in this repository.

> ⚠️ **Security**: `tone3000.json` holds your key and your account's refresh token. It lives in `Documents\Guitar Companion\` — **outside this repository** — and must never be committed anywhere.

Guitar Companion uses the public TONE3000 API under its **free, non-commercial tier**: the OAuth prompt flows and the bounded list endpoints that tier allows. **This project is not affiliated with, sponsored by, or endorsed by TONE3000.**

## 📁 Structure

```
src/                  plugin code (processor, editor, store, TONE3000 client)
docs/EFEITOS.md       sources/references for each effect and variation
assets/fonts/         Space Grotesk + JetBrains Mono (OFL, embedded in the binary)
assets/brand/         the app icon: two source SVGs + the files derived from them
packaging/            Inno Setup script + make-icon.ps1 (regenerates the icon)
docs/screenshots/     project screenshots
references/           OPTIONAL submodules: reference projects for effects (see references/README.md)
third_party/JUCE            submodule pinned at 8.0.15 (required to build)
third_party/NeuralAmpModelerCore  submodule pinned at v0.5.4, A2 support (required to build)
third_party/webview2/       WebView2 SDK fetched by CMake (gitignored, not committed)
```

User data (outside the repo): `Documents\Guitar Companion\` — `Captures/`, `IRs/`, `Presets/`, `tone3000.json`, `webview/` (the embedded browser's profile: cookies of your TONE3000 sign-in).

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
- [x] **Looper** card: up to 60 s, REC → closes and plays → overdub, PLAY/STOP, CLEAR and **WAV export** (`Documents\Guitar Companion\Loops`)
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
- [x] **★ favorites on TONE3000** (persisted + "★ only" filter) · **rig A/B** (compares two complete setups) · **quick recorder** (24-bit WAV of the output in `Documents\Guitar Companion\Recordings`)
- [x] UX fixes: immediate relayout when removing/adding cards (no stale targets under the mouse), deferred relayout while dragging a knob

**Phases 16–17 — external VST3 plugins (done):**
- [x] **Up to 8 slots** of VST3 plugins in the chain (in practice the limit is CPU); LOAD menu by category
- [x] **Built-in catalog** of 8 open-source plugins (Dragonfly, Airwindows, Zam, AIDA-X, Fire, Wolf Shaper, PeakEater, Surge XT Effects): a **Plugins** tab in the Tone Store with an INSTALL ⇄ UNINSTALL toggle, progress and pinned versions — **direct download only**: installs by extracting the .vst3 into the user folder (no admin) and uninstalls by deleting the file, no installer
- [x] `plugins/` in the repo: an alternative script + offline copy (32 MB) with licenses
- [x] **Renamed to Guitar Companion** (avoids confusion with NI's Guitar Rig); old data migrates automatically

**Phase 18 — Drums module (in progress):**
- [x] Engine: sample-accurate sequencer in processBlock (2 bars × 16 steps, 9 voices, accent/ghost), swing, click, count-in; its own bus summed into the master (does not go through the guitar chain)
- [x] Sound sources: **internal synthesized sampler** (works out of the box) and a **hosted drum VST3** (GM MIDI channel 10, pending/retired protocol, panel in its own window)
- [x] UI v4 "the staff is the track" (**Drums** button in the top bar): the central area shows the **whole section (4 bars) on a continuous staff**; **1-bar** grooves are **dragged from the library straight onto the bar on the staff**; clicking the staff edits (empty→hit→accent→ghost); sections as tabs (**+ SECTION** = +4 bars); **FOLLOW** turns the page on play; the **GRID** chip opens the 16-step grid of the selected bar
- [x] **Massive reorganized library**: ~460 factory grooves+fills; **each genre gathers its grooves AND its fills** (ALL/GROOVES/FILLS sub-filter; fills with an orange border); 16 genres incl. **SOUL/GOSPEL** and **GENERAL** (generic fills). Part comes from the **Groove MIDI Dataset** (Google Magenta, CC BY 4.0 — see `THIRD_PARTY.md`), quantized; the dataset's jazz/blues are left out (swing doesn't fit the straight grid). Each card shows a **notation thumbnail**; + **My bars** (`Documents\Guitar Companion\compassos`)
- [x] Timeline/BPM/swing/source saved in the preset (A/B included; old formats migrate)
- [x] **Column browser** (DrumGroovePro style, GPLv3): Genre (with counts) | Grooves/Fills | **Preview** with the big notation + "apply to bar" + drag; **humanize** (velocity/micro-timing/round-robin) of the internal kit; **EDIT button** (edit notes ⇄ assemble: drag the whole bar to reposition/copy)
- [x] **Per-bar time signature**: each bar can have its own meter (4/4, 3/4, 2/4, 6/8, 12/8 + Custom); the signature is written only when it changes (notation convention), the width adjusts to the number of steps (engine with variable steps per bar, cap 32) and the beams group by meter (compound in threes). Clicking the signature on the bar header opens the menu; the grid and playhead follow. Saved in the preset.
- [x] **Groove generator** (ported from midi-drums, MIT): genre/style/drummer + parameters generate a bar honoring its time signature; per-bar role (Verse/Chorus/Bridge/Fill) drives the generation
- [x] **Ribbons + morph**: each screen carries a live ribbon of the other at the top (drums ribbon on guitar with playhead; guitar ribbon on drums with amp + active pedals), and clicking morphs into the full screen
- [ ] Pending: external MIDI output, per-piece mini-mixer, copy bar→bar by dragging
- Approved design: `docs/design/`; dev flags `GUITAR_COMPANION_OPEN_DRUMS=1|play|meter|gen|genfill`

**Next:**
- [ ] Future ideas: chain minimap, MIDI learn, per-song scene snapshot
- [ ] Full visual redesign (see `docs/design/redesign-brief.md`)

## 🤝 Contributing

Contributions are welcome! Read **[CONTRIBUTING.md](CONTRIBUTING.md)** (build, code map, real-time safety rules and known MSVC/JUCE pitfalls) and use the issue/PR templates. Unchecked roadmap items are a great starting point.

## 🔍 Attribution in detail

The credits are up top in [Where the inspirations came from](#where-the-inspirations-came-from). The full paperwork lives in three files:

- **[`docs/EFEITOS.md`](docs/EFEITOS.md)** — one row per effect: the classic gear the variation chases, the project read to study it, and what actually runs in the code. Ports are labelled as ports.
- **[`THIRD_PARTY.md`](THIRD_PARTY.md)** — every third-party material with its license and required attribution text.
- **[`references/`](references/README.md)** — the studied projects, pinned as submodules so the claims can be checked.

## 📜 Licenses

This project is licensed under **[AGPLv3](LICENSE)** — required by using JUCE 8 in the open-source tier. Dependencies:

- **JUCE 8** — AGPLv3 (personal/open-source use) · **NAM Core** — MIT · **AudioDSPTools** — Apache-2.0/MIT (see repository)
- **Fonts** — SIL Open Font License (text in `assets/fonts/`)
- **ASIO SDK** — Steinberg license (manual download, not redistributed)
- Captures/IRs downloaded from TONE3000 have their own per-tone licenses (CC/T3K) — respect them when redistributing tones.
