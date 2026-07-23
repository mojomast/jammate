# Contributing to PedalForge NAM

Thanks for your interest! This guide covers the essentials to build, change and submit improvements.

## 🔨 Environment and build

- **Windows 10/11 x64** · Visual Studio 2022 (Build Tools or Community) with "Desktop development with C++" · CMake ≥ 3.22 · Git
- Clone and build:

```powershell
git clone <repo-url> GuitarRigNAM
cd GuitarRigNAM
git submodule update --init --recursive third_party   # references/ is NOT needed
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

- ASIO is optional (see README). The Standalone runs on WASAPI with nothing extra.
- Run the Standalone: `build\GuitarRigNAM_artefacts\Release\Standalone\PedalForge NAM.exe`

## 🗂️ Code map

| File | Responsibility |
|---|---|
| `src/PluginProcessor.*` | Audio: DSP chain, parameters (APVTS), loading NAM models/IRs, presets, state |
| `src/PluginEditor.*` | UI: top bar, chain (`ChainView`, drag-and-drop), tuner, knobs |
| `src/LookAndFeel.h` | Theme (`ui::` palette), drawing of knobs/buttons/chips |
| `src/StoreOverlay.*` | Tone Store (search/downloads/library UI) |
| `src/Tone3000Client.*` | TONE3000 API: OAuth PKCE, search, downloads, images |
| `src/DrumEngine.* · DrumOverlay.* · DrumGenerator.* · DrumLibrary.cpp` | Drums module: sequencer, staff/library UI, groove generator, factory library |

## ⚡ Golden rules

1. **Real-time safety is non-negotiable**: inside `processBlock` (and any function it calls) it is **forbidden** to allocate memory, use locks, do I/O, log or use the network. Exchanging data with other threads = atomics or RT-safe mechanisms (see the pending/retired model-swap protocol).
2. **Accented strings**: `juce::String("text")` interprets `char*` as **Latin-1**. Any literal with an accent/symbol must use `juce::String (juce::CharPointer_UTF8 ("..."))` or `juce::String::fromUTF8`. The target compiles with `/utf-8`. (Watch the `\x` hex-escape trap: `\x` consumes *all* following hex digits, so split literals like `"...\xc3\xba" "dio"` when the next char is a hex letter.)
3. **MSVC + lambdas**: `this` in the init-capture of a nested lambda resolves wrong on MSVC — use `auto* self = this;` first. `Component::SafePointer` needs the explicit template argument.
4. **Secrets**: `tone3000.json` (the user's key/token) lives in `Documents\PedalForge NAM\` and **never** enters the repository. Never commit keys, tokens or passwords.
5. **Style**: follow the surrounding code (JUCE style: 4 spaces, Allman braces, `camelCase`). Comments in English explaining the *why*, not the *what*.

## ✅ Before opening a Pull Request

- [ ] Builds in Release with no new errors (`cmake --build build --config Release`)
- [ ] The Standalone opens and audio passes (test with a capture from `third_party/NeuralAmpModelerCore/example_models/`)
- [ ] Changed DSP? Describe how you tested the sound (ideally: before/after)
- [ ] Changed UI? Attach a screenshot in the PR
- [ ] Old presets still load (state compatibility)
- [ ] Small commits with descriptive messages

## 🧭 Where to start

See the **Roadmap** in the README — unchecked items are welcome. New ideas: open an issue first to align the scope. The projects in `references/` serve as algorithm references (mind the licenses described in `references/README.md` — only MIT code may be ported directly).

## 📜 License

By contributing, you agree that your contribution will be licensed under **AGPLv3** (the same license as the project, required by the use of JUCE in the open-source tier).
