# Contributing to JamMate

Thanks for your interest! This guide covers the essentials to build, change and submit improvements.
Start with the [README](README.md) for current feature status and build options,
and [DEVPLAN.md](DEVPLAN.md) for task contracts. Application identifiers and UI
developer flags currently retain the Guitar Companion name.

## 🔨 Environment and build

- **Windows 10/11 x64** · Visual Studio 2022 (Build Tools or Community) with "Desktop development with C++" · CMake ≥ 3.24 · Git
- Clone and build:

```powershell
git clone https://github.com/mojomast/jammate.git
cd jammate
git submodule update --init third_party/JUCE
git -c core.autocrlf=false submodule update --init --recursive third_party/NeuralAmpModelerCore
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

- ASIO is optional (see README). The Standalone runs on WASAPI with nothing extra.
- The embedded TONE3000 browser needs the WebView2 SDK, which CMake downloads on
  the first configure. Build offline with `-DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF`
  (the store then uses the system browser, as it always did).
- Run the Standalone: `build\GuitarCompanion_artefacts\Release\Standalone\Guitar Companion.exe`

For a JUCE-free contribution, build/test `jam-core` first; it needs no submodule
initialization or audio hardware. The README includes both default and optional
tracker test recipes. Linux application build instructions are there too.

### 🧪 Dev flags

Environment variables that open a screen straight away. They exist so a screen
can be checked **deterministically** instead of by clicking at coordinates —
which is fragile, and which has corrupted state here before (a synthetic drag
landed on a knob mid-reflow). Prefer adding a flag over scripting clicks.

| Variable | Values | Opens |
|---|---|---|
| `GUITAR_COMPANION_OPEN_STORE` | `explore` · `library` · `plugins` · `browse` · `rig1..rig3` · `cab1..cab3` | Tone Store. `browse` goes into the embedded TONE3000 picker; `rigN`/`cabN` aim the store at that rig (loads land in that amp/cab, and the card labels say so), and `cabN` also starts the picker on IRs |
| `GUITAR_COMPANION_LOAD_MENU` | `1..3` | The amp card's LOAD/CHANGE CAPTURE menu for that lane (popup menus are their own window, so they cannot be captured any other way) |
| `GUITAR_COMPANION_OPEN_DRUMS` | `1` · `play` · `grid` · `gen` · `genfill` · `meter` · `rig1..3` | Drums module |
| `GUITAR_COMPANION_OPEN_SONG` | `1` | Song / Scenes |
| `GUITAR_COMPANION_OPEN_AUDIO` | `1` | Audio & MIDI |
| `GUITAR_COMPANION_STAGE` | `1` | Stage mode |
| `GUITAR_COMPANION_TUNER` | `1` | Tuner on at start |
| `GUITAR_COMPANION_EXT_PLUGIN` | `<path>` | Loads that VST3 into the external slot at start |
| `GUITAR_COMPANION_DEBUGLOG` | `<file>` | Logs chain reordering (`setChainOrder`) |

When a screenshot must include a popup menu, capture the **whole screen** and
crop: do not call `SetForegroundWindow` after the menu is up, because JUCE
closes it on focus loss.

## 🗂️ Code map

| File | Responsibility |
|---|---|
| `src/PluginProcessor.*` | Audio: DSP chain, parameters (APVTS), loading NAM models/IRs, presets, state |
| `src/PluginEditor.*` | UI: top bar, chain (`ChainView`, drag-and-drop), tuner, knobs |
| `src/LookAndFeel.h` | Theme (`ui::` palette), drawing of knobs/buttons/chips |
| `src/StoreOverlay.*` | Tone Store (browse/downloads/library UI) |
| `src/ToneWebView.*` | Embedded TONE3000 picker (WebView2): catches the OAuth redirect in `pageAboutToLoad` |
| `src/Tone3000Client.*` | TONE3000 API: OAuth PKCE, prompt flows, downloads, images |
| `src/DrumEngine.* · DrumOverlay.* · DrumGenerator.* · DrumLibrary.cpp` | Drums module: sequencer, staff/library UI, groove generator, factory library |
| `src/jam/` | Portable analysis worker, Musical Clock, rhythm types and transport seams |
| `src/rt/` | Bounded signalling and queues |
| `tools/` | Offline evaluations, diagnostic traces and runtime probes |

## ⚡ Golden rules

1. **Real-time safety is non-negotiable**: inside `processBlock` (and any function it calls) it is **forbidden** to allocate memory, use locks, do I/O, log or use the network. Exchanging data with other threads = atomics or RT-safe mechanisms (see the pending/retired model-swap protocol).
2. **Accented strings**: `juce::String("text")` interprets `char*` as **Latin-1**. Any literal with an accent/symbol must use `juce::String (juce::CharPointer_UTF8 ("..."))` or `juce::String::fromUTF8`. The target compiles with `/utf-8`. (Watch the `\x` hex-escape trap: `\x` consumes *all* following hex digits, so split literals like `"...\xc3\xba" "dio"` when the next char is a hex letter.)
3. **MSVC + lambdas**: `this` in the init-capture of a nested lambda resolves wrong on MSVC — use `auto* self = this;` first. `Component::SafePointer` needs the explicit template argument.
4. **Secrets**: `tone3000.json` (the user's key/token) lives in `Documents\Guitar Companion\` and **never** enters the repository. Never commit keys, tokens or passwords.
5. **Style**: follow the surrounding code (JUCE style: 4 spaces, Allman braces, `camelCase`). Comments in English explaining the *why*, not the *what*.

## ✅ Before opening a Pull Request

- [ ] Builds in Release with no new errors (`cmake --build build --config Release`)
- [ ] The Standalone opens and audio passes (test with a capture from `third_party/NeuralAmpModelerCore/example_models/`)
- [ ] Changed DSP? Describe how you tested the sound (ideally: before/after)
- [ ] Changed UI? Attach a screenshot in the PR
- [ ] Old presets still load (state compatibility)
- [ ] Small commits with descriptive messages

## 🧭 Where to start

See [DEVPLAN.md](DEVPLAN.md) for tasks and acceptance criteria, and
[EXECUTION-LEDGER.md](EXECUTION-LEDGER.md) for verified progress and open gates.
New ideas: open an issue first to align the scope. The projects in `references/`
serve as algorithm references; retain their attribution and check the dependency
inventory before introducing or porting third-party code.

## 📜 License

By contributing, you agree that your contribution will be licensed under **AGPLv3** (the same license as the project, required by the use of JUCE in the open-source tier).
