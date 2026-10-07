# CODE-MAP — `src/` file-by-file reachability map

**Task:** FND-003
**Base commit:** `48f301c8fd046429fe55519ec3a2d20a2fccaa44`
**Companion document:** `docs/research/RT-REACHABILITY.md` (full findings and evidence).

Legend for **Callback-reachable?**
- `yes (callback)` — this file *is* the audio callback path.
- `yes (write)` — the callback enters it and writes state; reads happen elsewhere.
- `conditional (…)` — reachable only under the stated runtime condition.
- `no` — not called from `processBlock` / `DrumEngine::process`; runs on the message thread, `loaderPool`, `recThread`, or a network/UI thread.

"Thread(s)" lists every thread that touches the file's code: **audio** = the JUCE
`processBlock` callback; **msg** = message/UI thread; **loader** = `juce::ThreadPool
loaderPool`; **rec** = `juce::TimeSliceThread recThread`; **worker** = rhythm-analysis /
clock worker (not implemented here yet); **net** = network/async.

| File | Role | Thread(s) | Callback-reachable? |
|---|---|---|---|
| `src/PluginProcessor.h` | Declares `GuitarCompanionProcessor`: APVTS parameters, DSP member state, model/IR/VST pending-retired atomics, scenes, recording, drum engine, all callback helpers. | audio + msg + loader | `yes (callback)` — declares the callback and everything it touches |
| `src/PluginProcessor.cpp` | Implementation: `processBlock` (the callback, `:1099`), the guitar chain modules, model/IR/capture swap protocol, scenes, recording, drum bus. | audio + msg + loader + rec | `yes (callback)`; the file also contains message/loader-only functions |
| `src/DrumEngine.h` | Declares the internal drum sampler + timeline; atomics for UI↔audio pattern/BPM/meters; `process`, `fireHit`, `trigger`. | audio + msg | `yes (callback)` — `DrumEngine::process` is called by `processDrums` |
| `src/DrumEngine.cpp` | Drum sequencer/sampler: block scheduling, MIDI to a hosted drum VST, embedded-kit playback, meter publishing. | audio + msg | `yes (callback)` — `process` `:265`, MIDI add `:233/:383` |
| `src/DrumLibrary.cpp` | Factory groove/fill library (`drum::library()` + specs) and `library()` iteration. | msg | `no` — only `DrumOverlay.cpp:1074,2546,…` (UI) calls it |
| `src/DrumGenerator.h` | Declares procedural groove generation from genre/style/role/params. | msg | `no` |
| `src/DrumGenerator.cpp` | Implements `drum::generate*` pattern generation (ported MIT logic). | msg | `no` — invoked from `DrumOverlay`/editor |
| `src/DrumOverlay.h` | Thin drum strip UI (transport + mini-staff) shown on the guitar screen. | msg | `no` |
| `src/DrumOverlay.cpp` | Full Drums module UI: staff, kit mixer, library browser, generator. | msg | `no` |
| `src/SongOverlay.h` | Song/Scenes screen UI (setlist, scene cards, inspector, transport). | msg | `no` |
| `src/SongOverlay.cpp` | Song/Scenes UI; calls `applySceneForSection`, pattern loaders, `DrumGenerator`. | msg | `no` |
| `src/StoreOverlay.h` | Tone Store UI (browse/downloads/library, tone cards). | msg + net | `no` |
| `src/StoreOverlay.cpp` | Tone Store implementation; downloads/installs captures and plugins. | msg + net | `no` |
| `src/AudioOverlay.h` | Audio/MIDI settings overlay UI (staged device config). | msg | `no` |
| `src/AudioOverlay.cpp` | Implements device enumeration/selection UI. | msg | `no` |
| `src/PluginEditor.h` | Declares the main editor, chain view, knobs, tuner/analyzer panels. | msg | `no` |
| `src/PluginEditor.cpp` | Main editor implementation; polls processor atomics/rings, drives drum UI, tuner, analyzer. | msg | `no` — includes `readAnalyzerBlock`/`readTunerBlock` reads |
| `src/PluginCatalog.h` | Declares the embedded recommended-VST3 catalog surface. | msg | `no` |
| `src/PluginCatalog.cpp` | Plugin catalog: detect/download/install VST3s. | msg + net + file | `no` |
| `src/Tone3000Client.h` | Declares the TONE3000 HTTP/OAuth client. | net | `no` |
| `src/Tone3000Client.cpp` | TONE3000 API: OAuth PKCE, downloads, images. | net | `no` |
| `src/ToneWebView.h` | Declares the embedded TONE3000 WebView2 picker. | msg + net | `no` |
| `src/ToneWebView.cpp` | WebView component; catches the OAuth redirect. | msg + net | `no` |
| `src/LookAndFeel.h` | Theme palette and custom knob/button/chip drawing. | msg | `no` |
| `src/jam/AnalysisAudioRing.h` | Fixed-capacity SPSC analysis audio ring (SPEC §8.1). Allocation-free, bounded. | audio (producer, *future*) + worker (consumer) | `no` **today** — not included by `PluginProcessor.cpp`; becomes `yes (write)` when INT-ANALYSIS-001 wires the tap (re-audit then) |
| `src/jam/RhythmTypes.h` | Frozen POD data types (`AnalysisFrame`, `RhythmObservation`, …); dependency-free. | worker + (future) audio | `no` — types only, no caller in the callback |
| `src/jam/IRhythmTracker.h` | Tracker backend interface; `process` runs on the analysis worker. | worker | `no` |
| `src/jam/IDrumTransport.h` | Drum transport abstraction between Jam Director and DrumEngine. | worker (methods marked `[worker]`) | `no` |
| `src/jam/JamConfig.h` | Central `ClockConfig` / `DirectorConfig` thresholds (SPEC §10.2). | worker/msg | `no` |
| `src/jam/JamCore.h` | jam-core version + persistence schema constants. | any (header-only, constexpr) | `no` |
| `src/jam/JamCore.cpp` | `jam::jamCoreVersion()` definition (keeps `jam-core` a buildable static lib). | any | `no` |

## Notes

* Only three files in `src/` participate in the audio callback: the two `PluginProcessor`
  files and the two `DrumEngine` files. Everything else is UI, network, loader, recorder,
  or the not-yet-wired `jam/` core.
* `src/jam/` is compiled into the `jam-core` static library
  (`jam-core/CMakeLists.txt:44-59`) and is deliberately free of `juce::` (ADR-0003 rule 2).
  It is **not** linked into the callback at this commit.
* The two `triggerAsyncUpdate()` code sites both live in `src/PluginProcessor.cpp`
  (`:1070` in `prepareToPlay`, `:1227` in `processBlock`). See `RT-REACHABILITY.md` §1.
