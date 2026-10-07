# FND-001 — Dependency and licence inventory

**Task:** FND-001 — Fork provenance and dependency inventory
**Written:** 2026-10-07
**Baseline:** fork of `raphaelfukuda/Guitar-Companion` at
`88f7e7c805c9c5e17388154a678c2c6a3633ff23` — see [`BASELINE.md`](BASELINE.md)
**Purpose:** satisfy SPEC.md §25 (licensing/provenance gate) and DEVPLAN G0
("dependency/license inventory captured", "exact upstream SHA frozen").

Every row cites the file or the git object it was read from. Where a licence or
a version **cannot be determined from this tree**, the cell reads
`UNKNOWN — verify at <exact place>` — a licence is never guessed and a
redistribution permission is never asserted beyond what a file in the tree
actually says. See [`BASELINE.md` §5.2](BASELINE.md#52-assumed--not-verifiable-here)
for the full assumed list and §5.3 for contradictions found inside the tree.

### How to read the "usage class" column

| Class | Meaning |
|---|---|
| **linked** | compiled into the shipped binary |
| **vendored** | its source/text was copied into this repository's own files |
| **build-only** | compiled, but never reaches a release artefact |
| **present, not compiled** | in the tree or fetched at configure time, but no TU includes it |
| **studied only** | read for technique; never compiled, never shipped |
| **benchmarked only** | run by an offline evaluation harness; never in the product |
| **hosted, not shipped** | third-party binary the *user* installs and the app loads at runtime |
| **candidate / deferred** | proposed in SPEC.md §2.2, not adopted |

---

## 1. Compiled into the shipped product

| component | version/SHA | source URL | license | usage class | ships in release artifact? | redistribution obligations | notes |
|---|---|---|---|---|---|---|---|
| **Guitar-Companion / Guitar Companion** (the product) | `88f7e7c805c9c5e17388154a678c2c6a3633ff23` | https://github.com/raphaelfukuda/Guitar-Companion | **AGPLv3** — `LICENSE` lines 1–2, 661 lines | linked (own code) | **Yes** | AGPL §6: ship the complete corresponding **source**, §13 appropriate legal notices, §5/§6 keep the licence intact | Upstream is AGPL because JUCE 8 is used under its AGPLv3 option (`THIRD_PARTY.md` line 57, `README.md` line 456). Frozen SHA re-verified against `master` — see `BASELINE.md` §1.1. **Closed-source distribution is incompatible**; see ADR-0001. |
| **JUCE** (audio framework, plugin formats, UI toolkit) | submodule gitlink **`91ad83ae34a81e0833b1a2b0866f54846370ae53`**; version claimed **8.0.15** — `README.md` line 342, `THIRD_PARTY.md` line 55. *Version not verifiable here (submodule not checked out).* | https://github.com/juce-framework/JUCE.git · <https://juce.com> | **AGPLv3** (used under the open-source / non-commercial tier) or proprietary — dual-licensed. `THIRD_PARTY.md` line 57; `README.md` line 456 | linked | **Yes** — the entire framework is inside the binary | AGPLv3: same as the product. `COPY_PLUGIN_AFTER_BUILD FALSE`, so JUCE ships **only** through our own Standalone/VST3 artefacts | Copyright `Copyright (c) Raw Material Software Limited`, read from the source headers of the pinned commit (`README.md` line 85). `add_subdirectory(third_party/JUCE)` — `CMakeLists.txt` line 8. **This is the licence that forces the product to AGPL.** |
| **Steinberg VST3 SDK** (bundled *inside* the JUCE submodule) | *no version recorded anywhere in this tree* → `UNKNOWN — verify at third_party/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/ after \`git submodule update --init third_party/JUCE\`` | bundled by JUCE; SDK originally from https://sdk.steinberg.net/ | **GPLv3 / proprietary, dual-licensed.** `THIRD_PARTY.md` line 61–62: *"It is Steinberg's, dual-licensed GPLv3 / proprietary; this project uses the GPLv3 option."* → GPLv3 route | linked (only to build the `VST3` format) | **Yes, inside the VST3 bundle** | GPLv3 route: AGPLv3/GPLv3-compatible source obligations as part of the whole product | Nothing in the fork names a version, so this row is `UNKNOWN` on version by design. The **VST3 SDK is a different thing from the ASIO SDK** — the ASIO SDK (§5 below) is *not* redistributed at all. |
| **Neural Amp Modeler Core** (`nam_core`) | submodule gitlink **`1f42f88535884450104b8711d7595019afa0495b`**; version claimed **v0.5.4** — `CMakeLists.txt` line 13 comment, `README.md` line 343, `THIRD_PARTY.md` line 18. *Not verifiable here.* | https://github.com/sdatkinson/NeuralAmpModelerCore.git · <https://github.com/sdatkinson/NeuralAmpModelerCore> | **MIT** · `Copyright (c) 2023 Steven Atkinson` — `THIRD_PARTY.md` line 22 | linked | **Yes** | MIT: **the copyright + permission notice must travel with all copies or substantial portions.** Full text already in `THIRD_PARTY.md` lines 69–92 and shipped to users by `packaging/guitar-companion.iss` | Built by listing 13 upstream `.cpp` files directly into `add_library(nam_core STATIC …)` (`CMakeLists.txt` lines 16–29) — upstream's own `CMakeLists.txt` is *not* consumed. Needs `cxx_std_20` (line 32). Linked `WHOLE_ARCHIVE` (line 247) because NAM registers architectures via static initialisers. Defines `NAM_SAMPLE_FLOAT` and `NAM_ENABLE_A2_FAST` (lines 46–48). |
| **AudioDSPTools** (LanczosResampler) | ✅ **RESOLVED 2026-10-07.** It *is* a submodule — a **nested** one inside NAM at `Dependencies/AudioDSPTools`, pinned `0827c6c2fc0deced568536142ea86f189e0b98a1` (recorded in NAM's own `.gitmodules`; `git submodule status` inside the NAM checkout shows it uninitialised here) | <https://github.com/sdatkinson/AudioDSPTools> | **MIT.** Verified by fetching that exact SHA and reading its `LICENSE`: *"MIT License / Copyright (c) 2023 Steven Atkinson"*. **No `NOTICE` file exists** in the repository, so there is **no Apache-2.0 NOTICE obligation**. `THIRD_PARTY.md` line 24–26 was **correct**; `README.md` line 458's *"Apache-2.0/MIT"* is **wrong** — see "Corrections" below | linked (header included directly by `PluginProcessor.cpp`) | **Yes** | MIT only: the copyright and permission notice must travel with binary distributions. **Already discharged** — `THIRD_PARTY.md` lines 69–92 carry the MIT text naming Steven Atkinson. | Resolved by measurement, not inference. The nested-submodule detail also matters for CI: a non-recursive `git submodule update --init` leaves this empty, and the plugin build cannot compile without it. |
| **Eigen** (linear algebra; header-only) | bundled in the NAM submodule at `Dependencies/eigen` (`CMakeLists.txt` line 39). Version not pinned anywhere in this tree → `UNKNOWN — verify at third_party/NeuralAmpModelerCore/Dependencies/eigen/ (and \`git submodule status\` run inside the NAM submodule, after init)` | <https://eigen.tuxfamily.org> | **primarily MPL2**, with individual files under BSD / Apache / Minpack terms, all MPL2-compatible — `THIRD_PARTY.md` lines 42–43 | linked (headers, `SYSTEM` include, compiled into the hottest loop of the program) | **Yes** | MPL2 is file-level copyleft: modified `*.h` files must stay MPL2 and their sources be offered. `COPYING.MPL2`, `COPYING.BSD`, `COPYING.APACHE`, `COPYING.MINPACK` sit in that directory and **must be carried in the release** — *currently the product ships `THIRD_PARTY.md`, which paraphrases but does not include those four files* (`packaging/guitar-companion.iss` `[Files]`). **Action for the release owner: add the four Eigen licence files to the installer.** | The version is genuinely unknowable from here, which is exactly why this row is `UNKNOWN` and not "3.4". |
| **nlohmann/json** (reads `.nam` files) | bundled in the NAM submodule at `Dependencies/nlohmann` (`CMakeLists.txt` line 40); version **3.12.0** — `THIRD_PARTY.md` line 48. *Version not verifiable here.* | <https://github.com/nlohmann/json> | **MIT** · `SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann` — `THIRD_PARTY.md` line 51 | linked (header-only) | **Yes** | MIT: copyright + permission notice must travel. Already in `THIRD_PARTY.md` lines 69–92 (second block of the combined notice) and shipped via the installer | Header-only, so nothing to link; it is a compile-time include. |
| **Airwindows** — *ported code* (Tape, Console, Valve Drive) | no SHA recorded in this tree; the reference submodule is **not pinned** (see `BASELINE.md` §3.2) → `UNKNOWN — verify at airwindows/airwindows LICENSE at a SHA that must first be pinned` | <https://github.com/airwindows/airwindows> | **MIT** — `THIRD_PARTY.md` line 202. ⚠ copyright line differs between files: `THIRD_PARTY.md` line 210 = `Copyright (c) Chris Johnson / Airwindows`; `README.md` line 83 = `Copyright (c) 2018 Chris Johnson` | **vendored (ported)** — three effects are ports of Airwindows code, not inspiration | **Yes** | MIT: notice must travel — full text already in `THIRD_PARTY.md` lines 206–229, which is shipped by the installer | `THIRD_PARTY.md` line 199 names them: **Tape** (from ToTape/IronOxide), **Console**, and **Valve** (Drive variation, from Tube). Code markers: `src/PluginProcessor.cpp` line 2256 (`Tape`), 2287 (`Console`), 1499 (`Valve`); `docs/EFEITOS.md` lines 45, 49, 33, 96, 188, 189 all label these *porte/adaptado*, not *estudo*. MIT is the only reference project whose code may be ported (`CONTRIBUTING.md` line 78). |
| **Groove MIDI Dataset** (Google Magenta) — derived grooves/fills | no version/SHA recorded → the dataset is cited but **not pinned anywhere in the tree** | <https://magenta.tensorflow.org/datasets/groove> | **CC BY 4.0** | vendored (data, quantized to a 16th grid and remapped to 9 voices) | **Yes** (compiled into `src/DrumLibrary.cpp`) | CC BY requires **attribution + an indication of changes + a link to the licence, and forbids additional restrictions.** Satisfied in `THIRD_PARTY.md` lines 100–105. The compliance point is the licence file itself — a release should ship the attribution and ideally the CC BY 4.0 text | Required attribution, verbatim from `THIRD_PARTY.md` line 104: *"Groove MIDI Dataset" by Google LLC (Magenta), used under CC BY 4.0. The patterns were quantized to a sixteenth-note grid and remapped to 9 voices; they are derivative works.* Traceable in code at `src/DrumLibrary.cpp` lines 8–9: *"Part of the grooves/fills comes from the GROOVE MIDI DATASET (Google Magenta), CC BY 4.0 license - see THIRD_PARTY.md (attribution required)."* README line 429 adds that the dataset's jazz/blues was excluded because "swing doesn't fit the straight grid". |
| **midi-drums** (fsecada01) — derived metal grooves/fills | no SHA recorded in this tree → `UNKNOWN — verify at fsecada01/midi-drums pyproject.toml at a SHA that must first be pinned` | <https://github.com/fsecada01/midi-drums> | **MIT**, declared in that project's `pyproject.toml` as `license = { text = "MIT" }` — `THIRD_PARTY.md` line 114–115 | vendored (data: positions and velocities ported) | **Yes** | MIT: notice must travel — full text already in `THIRD_PARTY.md` lines 118–140 | Traceable in code at `src/DrumLibrary.cpp` line 555: `// ===== METAL (ported from midi-drums, fsecada01, MIT - see THIRD_PARTY.md) =====`. `THIRD_PARTY.md` line 110 calls it a port of *positions and velocities*. |
| **GMRockKit** drum samples — 27 `.wav` | no upstream SHA recorded. Kit: *"GMRockKit — A Sampled 5pc Pearl DX Series Drumkit"* — `assets/drums/ORIGEM.txt` line 4 | <https://github.com/hydrogen-music/hydrogen/tree/main/data/drumkits/GMRockKit> — `ORIGEM.txt` line 8 | **GPL** — `ORIGEM.txt` line 6: `Licença: GPL (compatível com o AGPLv3 deste projeto)`; authors `Glen MacArthur / Sebastian Moors` (`ORIGEM.txt` line 5). ⚠ **stated as bare "GPL", not "GPLv3"** → the exact version is `UNKNOWN — verify at the Hydrogen repository's own licence/drumkit metadata` | linked (binary data via `juce_add_binary_data`) | **Yes** — every `.wav` is embedded in the binary and therefore in the installer | GPL samples redistributed inside an AGPL binary are covered by the AGPL's own source-offer obligation. **The drift-check attribution requirement is the live risk**: `THIRD_PARTY.md` lines 144–151 records the kit and authors but the GPL text itself is not shipped by `packaging/guitar-companion.iss` | Quoted verbatim from `assets/drums/ORIGEM.txt` (whole file, 13 lines): see the block in §2 below. `CMakeLists.txt` line 143 comment: *"Fontes do design (OFL) + samples de bateria GMRockKit (GPL, ver assets/drums/ORIGEM.txt) embutidos no binário."* Code marker `src/DrumEngine.h` lines 190–191 and 228. |
| **Archivo** Regular + ExtraBold | no version recorded; files `assets/fonts/Archivo-Regular.ttf` (183 540 B), `Archivo-ExtraBold.ttf` (191 700 B) | <https://github.com/Omnibus-Type/Archivo> | **SIL Open Font License 1.1** — `assets/fonts/Archivo-OFL.txt` lines 1–3, quoted in §2 | linked (embedded binary) | **Yes** | OFL 1.1: the licence text **must be bundled with the font software**, copyright + Reserved Font Name notices retained, fonts not sold on their own, and derivative fonts may not use the Reserved Font Name | `Archivo-OFL.txt` **is** in the tree and readable. **Gap:** `packaging/guitar-companion.iss` ships `LICENSE`, `THIRD_PARTY.md` and `README.md` but **none of the four `*-OFL.txt` files**, and they are not inside the binary either. The OFL requires the licence to accompany the font — **add the four OFL texts to the installer**. |
| **Space Grotesk** Regular + Bold | `assets/fonts/SpaceGrotesk-Regular.ttf` (114 428 B), `-Bold.ttf` (116 056 B) | <https://github.com/floriankarsten/space-grotesk> | **OFL 1.1** — `assets/fonts/SpaceGrotesk-OFL.txt` lines 1–3 | linked (embedded) | **Yes** | same OFL obligation as above | Text readable in the tree. |
| **JetBrains Mono** Regular + Bold | `assets/fonts/JetBrainsMono-Regular.ttf` (270 224 B), `-Bold.ttf` (274 096 B) | <https://github.com/JetBrains/JetBrainsMono> | **OFL 1.1** — `assets/fonts/JetBrainsMono-OFL.txt` lines 1–3 | linked (embedded) | **Yes** | same OFL obligation | Text readable in the tree. |
| **Leland** (SMuFL music font, drums notation) | `assets/fonts/Leland.otf` (89 132 B); pinned to MuseScore commit **`73d6c2594fb2a90497d4abdc40b825849cb34d43`** — `THIRD_PARTY.md` line 165 | <https://github.com/musescore/MuseScore/tree/main/fonts/leland> | **OFL 1.1 with Reserved Font Name "Leland"** · `Copyright (c) 2025, MuseScore Limited` — `assets/fonts/Leland-OFL.txt` lines 1–3 | linked (embedded) | **Yes** | OFL: licence text must accompany the font; **the Reserved Font Name "Leland" may not be used for a modified version** | This is the one font with an exact SHA recorded — the only third-party item in the whole tree besides the two submodules and the WebView2 pin. Same installer gap as the other three. |
| **MuseScore** — engraving *conventions* | n/a — nothing copied | <https://github.com/musescore/MuseScore> | not applicable; `THIRD_PARTY.md` line 183: *"**no MuseScore code was copied or ported**, and MuseScore is not a dependency of this program"* | studied only (conventions, not code) | n/a | none — the credit is voluntary and already given (`THIRD_PARTY.md` lines 169–194; README line 75) | Every staff *symbol* is a Leland SMuFL glyph; stems/beams/staff lines are drawn by this project's own vector code in `src/DrumOverlay.cpp` (`THIRD_PARTY.md` line 185). SMuFL itself is a W3C Music Notation CG specification, not code (`THIRD_PARTY.md` line 193). |
| **DrumGroovePro** — drum browser / humanisation interface ideas | n/a — nothing copied | <https://github.com/InToEtherion/DrumGroovePro> | **GPLv3** — `THIRD_PARTY.md` line 339 | studied only (ideas) | n/a | none — `THIRD_PARTY.md` line 340–341: *"No code was copied; only ideas, which are not covered by copyright."* | Recorded because the upstream credit exists; keeping it accurate matters for the audit trail. |
| **TONE3000 logo + mark** (`tone3000-logo.png`, `t3k-mark.png`) | n/a — service marks | <https://www.tone3000.com/api> | ⚠ **Not an open-source licence.** `THIRD_PARTY.md` lines 283–286: *"The TONE3000 name and logos belong to them and are used only to identify the service, following their published design guidance."* No licence file exists in the tree → `UNKNOWN — verify at TONE3000's published brand/terms page and, ideally, written permission` | linked (embedded binary, `CMakeLists.txt` lines 154–155) | **Yes** — both PNGs are inside the binary | Trademark identification use only; **no redistribution right is stated anywhere in the tree**, so none is claimed here | `THIRD_PARTY.md` line 280 also records that **no credential ships** in this repo. Non-affiliation is stated at `THIRD_PARTY.md` line 283 and `README.md` line 330. |
| **TONE3000 API** | free, non-commercial tier; **each user supplies their own publishable key** — `THIRD_PARTY.md` line 279–280 | <https://www.tone3000.com/api> | proprietary service terms — `UNKNOWN — verify at TONE3000's current terms of service` | present, not compiled (network client at runtime) | n/a (no code, no asset) | API terms are the user's contract, not ours; the app ships **no** key | `Tone3000Client.cpp` / `ToneWebView.cpp` compile into the app. Store/network features are isolated from the real-time engine (SPEC §24). Captures/IRs downloaded by the user carry their own per-tone CC/T3K licences — `README.md` line 461. |

---

## 2. Quoted attributions (verbatim from `assets/`)

### 2.1 `assets/drums/ORIGEM.txt` — the complete file, 13 lines

> ```
> Samples de bateria embutidos no PedalForge NAM (sampler interno do
> módulo Bateria).
>
> Kit: GMRockKit — "A Sampled 5pc Pearl DX Series Drumkit"
> Autores: Glen MacArthur / Sebastian Moors
> Licença: GPL (compatível com o AGPLv3 deste projeto)
> Fonte: repositório oficial do Hydrogen drum machine
>   https://github.com/hydrogen-music/hydrogen/tree/main/data/drumkits/GMRockKit
>
> Seleção: 3 camadas de velocity por voz (ghost = *-Soft, normal = *-Med,
> acento = *-Hardest; no chimbal o acento usa HatSemiOpen-Hard para soar
> aberto). Arquivos renomeados para drum_<voz>_<camada>.wav, sem
> modificação no áudio.
> ```

What that obliges us to do: the sample content is GPL-licensed and unmodified
audio is redistributed inside our binary, so the AGPL source offer covers it,
**and** the kit identity and authors must be credited — which `THIRD_PARTY.md`
lines 144–151 already do. The bare word "GPL" without a version is the gap:
`UNKNOWN — verify at the Hydrogen repository's own licence for the GMRockKit
drumkit`.

### 2.2 The four OFL headers — first three lines of each file, verbatim

| File | Lines 1–3, verbatim |
|---|---|
| `assets/fonts/Archivo-OFL.txt` | `Copyright 2020 The Archivo Project Authors (https://github.com/Omnibus-Type/Archivo)` / `This Font Software is licensed under the SIL Open Font License, Version 1.1.` / `This license is copied below, and is also available with a FAQ at:` / `http://scripts.sil.org/OFL` |
| `assets/fonts/SpaceGrotesk-OFL.txt` | `Copyright 2020 The Space Grotesk Project Authors (https://github.com/floriankarsten/space-grotesk)` / `This Font Software is licensed under the SIL Open Font License, Version 1.1.` / `This license is copied below, and is also available with a FAQ at:` / `http://scripts.sil.org/OFL` |
| `assets/fonts/JetBrainsMono-OFL.txt` | `Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono)` / `This Font Software is licensed under the SIL Open Font License, Version 1.1.` / `This license is copied below, and is also available with a FAQ at:` / `https://openfontlicense.org` |
| `assets/fonts/Leland-OFL.txt` | `Copyright (c) 2025, MuseScore Limited (http://www.musescore.org/),` / `with Reserved Font Name "Leland".` / `This Font Software is licensed under the SIL Open Font License, Version 1.1.` / `This license is copied below, and is also available with a FAQ at:` / `http://scripts.sil.org/OFL` |

**OFL attribution requirement, in our words:** the OFL 1.1 licence text must
travel *with the font software*; the copyright notice and any Reserved Font
Name must be retained; the fonts may not be sold on their own; and any
modified font must not use the Reserved Font Name. `THIRD_PARTY.md` lines
153–167 already names all four and points at these files — but as noted above,
`packaging/guitar-companion.iss` does not actually install them. **That is a
concrete, closable gap for the release owner.**

---

## 3. Compiled but NOT shipped

| component | version/SHA | source URL | license | usage class | ships in release artifact? | redistribution obligations | notes |
|---|---|---|---|---|---|---|---|
| `jam-core` static library (`src/jam/**`, 8 files) | `48f301c8fd046429fe55519ec3a2d20a2fccaa44` (introducing commit) | this repository | AGPLv3 (own code) | build-only / linked-when-plugin-links-it | as code: yes (it *is* AGPL source). As a binary: it ships **only** if the JUCE plugin links it, which it does not yet | none beyond the project's own AGPL | **No third-party dependency at all.** `jam-core/CMakeLists.txt` rule 2 makes `juce::` a review-time rejection, which is precisely why it has no JUCE/Eigen/etc. on its include path. Built in *every* configuration, not behind an option (`CMakeLists.txt` lines 268–274). |
| `GuitarCompanionTests` (`tests/`, opt-in) | upstream `tests/CMakeLists.txt` blob `e44602d10ffee1f0870203a1964d9204ef6f11ec` | this repository | AGPLv3 | build-only | **No** | none | Gated behind `-DGUITAR_COMPANION_BUILD_TESTS=ON`, `OFF` by default (`CMakeLists.txt` line 284). Links `GuitarCompanionAssets` (so the embedded WAVs must be present even in tests) plus `juce::juce_audio_formats` and `juce::juce_audio_processors` — which is why ADR-0003 rules it unbuildable here. 4 ctest entries: `drums.parseSpec`, `drums.library`, `drums.generator`, `drums.barCodec`. |
| `jamTests` (`tests/jam/**`, from `48f301c`) | `48f301c8…` | this repository | AGPLv3 | build-only | **No** | none | `JAM_CORE_BUILD_TESTS` defaults **ON** inside `jam-core`, so this lane is the one that runs on any machine including CI. Generated `add_test` entries, one per `JAM_TEST(...)` suite. |

---

## 4. Present in the tree or fetched at configure time, but never compiled

| component | version/SHA | source URL | license | usage class | ships in release artifact? | redistribution obligations | notes |
|---|---|---|---|---|---|---|---|
| **Microsoft Edge WebView2 SDK** (`Microsoft.Web.WebView2` NuGet) | **pinned `1.0.3485.44`** — `CMakeLists.txt` line 68, described there as *"the version JUCE's own FindWebView2.cmake recommends"* | <https://www.nuget.org/packages/Microsoft.Web.WebView2> · downloaded from `https://globalcdn.nuget.org/packages/microsoft.web.webview2.1.0.3485.44.nupkg` (`CMakeLists.txt` line 80) | **BSD-3-Clause-style**, per the package's `LICENSE.txt` — `THIRD_PARTY.md` line 303, with the full notice at lines 306–334 | **present, not committed** — fetched into the gitignored `third_party/webview2/` at configure time, Windows-only, `ON` by default | **No.** `THIRD_PARTY.md` lines 296–300: *"no Microsoft binary is committed here"*; the WebView2 **Runtime** is whatever is already on the machine and *"nothing of it is redistributed"* | The build links `WebView2LoaderStatic.lib` from the package; if Microsoft binaries were ever vendored the BSD notice in `THIRD_PARTY.md` would have to travel. **They must never be redistributed from this repository.** | `.gitignore` line 8. `CMakeLists.txt` lines 54–108: the `.nupkg` is a plain zip, downloaded, extracted, then `file(REMOVE …)` deletes it. If the download fails, CMake `message(WARNING …)` and the store falls back to the system browser + the localhost listener. Opt out with `-DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF`; the version string is stated as *"the version JUCE's own FindWebView2.cmake recommends"*, i.e. a recommendation, not a JUCE requirement. `packaging/guitar-companion.iss` checks the runtime registry key and only informs the user. |
| **Steinberg ASIO SDK** | *no version* — user-supplied → `UNKNOWN — verify at the Steinberg ASIO SDK download the user performs; it must never enter this repository` | <https://www.steinberg.net/asiosdk> | **Steinberg licence — proprietary, non-redistributable.** `CMakeLists.txt` line 224: *"o SDK da Steinberg não pode ser redistribuído"*; `README.md` line 263: *"The Steinberg SDK cannot be redistributed"* | **present, not compiled, not committed** — enabled only if `-DASIOSDK_DIR=` points at a local copy containing `common/iasiodrv.h` (`CMakeLists.txt` lines 226–239) | **No — and it must never be redistributed from this repository** | None, because nothing is distributed. If it were, the licence forbids it | `.gitignore` line 7 (`third_party/asiosdk/`). `README.md` line 257 instructs the user to extract the SDK into `third_party/asiosdk/` and reconfigure. Without it, `CMakeLists.txt` line 233 emits a `WARNING` and the build proceeds **WASAPI/DirectSound only**. The VST3 hosting the *product* does is unaffected — this is only about the **Standalone's own** audio backend. |
| **NAM Core `example_models/`** | referenced by `CONTRIBUTING.md` line 70 as a source of test `.nam` captures | inside the NAM submodule | inherits NAM Core's **MIT** (`THIRD_PARTY.md` lines 16–22) | present, not compiled, not distributed | **No** | none | Read by a developer manually to test the Standalone. Not read by any build rule in `CMakeLists.txt`. Model **licences are per-capture** and are separate from the engine's — an unverified point worth a look before any capture ever ships. |
| **8 hosted drum/effect VST3 plugins** (see §8) | 8 distinct versions, each pinned in `src/PluginCatalog.cpp` | 8 distinct official release URLs | GPLv3 / GPLv2+ / MIT, one per plugin | **hosted, not shipped** | **No** | none for us — each plugin keeps its own licence and each is fetched from its own release | `THIRD_PARTY.md` lines 269–274 records that an `offline/` fallback of three release zips was **removed** precisely because redistributing GPL **binaries** creates a source-offer obligation. `.gitignore` line 10 keeps `plugins/offline/` out. |

---

## 5. Never compiled — reference material only (`references/*`)

**All eight are `studied only`.** None is included by any translation unit;
none is linked; none ships. `README.md` line 91 and `THIRD_PARTY.md` lines
243–244 both state they are *"not compiled into, linked against, or shipped
with this program"*, and `CONTRIBUTING.md` line 13 says a normal clone runs
`git submodule update --init --recursive third_party # references/ is NOT needed`.

⚠ **SHAs: every row below reads `NO GITLINK IN TREE`.** `.gitmodules` declares
all eight, but `git ls-tree HEAD:references/` returns **only** `README.md` — and
so does the upstream tree at `88f7e7c`. There is no pinned revision for any of
them, which contradicts `references/README.md` line 3 ("Submódulos **pinados**")
and `README.md` line 91. See [`BASELINE.md` §3.2](BASELINE.md#32--gitmodules-declares-eight-references-submodules-that-are-not-pinned).
All eight are also `shallow = true`, so even a later `--init` would fetch a
tip, not a fixed revision.

| component | version/SHA | source URL | license | usage class | ships in release artifact? | redistribution obligations | notes |
|---|---|---|---|---|---|---|---|
| **Airwindows** | **NO GITLINK IN TREE** → `UNKNOWN — pin first (add the gitlink), then read the LICENSE at that SHA` | https://github.com/airwindows/airwindows.git | **MIT** — `references/README.md` line 11, `THIRD_PARTY.md` line 248 | **studied only** *(as a reference)* — and separately **vendored** for the 3 ports in §1 | the ports: **Yes**. The reference repo: **No** | MIT: porting requires retaining the copyright notice — already in `THIRD_PARTY.md` lines 206–229 | `references/README.md` line 11 is the only reference project marked *"Portar código diretamente"*; `CONTRIBUTING.md` line 78: *"only MIT code may be ported directly."* Studied additionally for Ring Mod, Bitcrusher, Exciter and modulation (`THIRD_PARTY.md` line 248; `docs/EFEITOS.md` lines 32, 40, 42, 44). Note the Windows-independence: Airwindows ships Windows builds as `airwin2rack`/`Airwindows Consolidated`, so the study value is not blocked by the LV2/Linux problem below. |
| **BYOD** | **NO GITLINK IN TREE** | https://github.com/Chowdhury-DSP/BYOD.git | **GPLv3** — `references/README.md` line 12, `THIRD_PARTY.md` line 249 | **studied only** | **No** | none, because nothing is distributed. `references/README.md` line 23: GPL/LGPL projects are *"leitura/estudo de algoritmo… não distribuído"* | Read for drive/waveshaping stages. Also *excluded from the plugin catalogue* for a different reason: `plugins/README.md` lists BYOD among projects whose Windows release is installer-only. |
| **Dragonfly Reverb** | **NO GITLINK IN TREE** | https://github.com/michaelwillis/dragonfly-reverb.git | **GPLv3** — `references/README.md` line 13, `THIRD_PARTY.md` line 250 | **studied only** | **No** | none. Same principle as above | Read for Plate/Room/Hall reverb. Doubles as the first entry of the **hosted** plugin catalogue (GPLv3, 3.2.10) — the two roles are independent: the *binary* is user-downloaded, the *source* is never compiled here. |
| **Guitarix** | **NO GITLINK IN TREE** | https://github.com/brummer10/guitarix.git | **GPLv2** — `references/README.md` line 14; **GPLv2-or-later** — `THIRD_PARTY.md` line 251 (wording conflict, same family) | **studied only** | **No** | none | Read for personality pedals: drives, fuzz, wah. `references/README.md` line 24: LV2/Linux — *"não compilam para Windows; por isso servem só como referência, nunca como dependência de build."* Also the source of the Metal groove influences. |
| **GxPlugins.lv2** | **NO GITLINK IN TREE** | https://github.com/brummer10/GxPlugins.lv2.git | **GPLv3** — `references/README.md` line 15; **GPL** (version unspecified) — `THIRD_PARTY.md` line 252 | **studied only** | **No** | none | Read for drive/modulation voicing. Same LV2/Linux limitation. |
| **LSP Plugins** | **NO GITLINK IN TREE** | https://github.com/lsp-plugins/lsp-plugins.git | ⚠ **CONFLICT.** `references/README.md` line 16: **LGPLv3**. `THIRD_PARTY.md` line 253: **GPLv3** | **studied only** | **No** | none | Read for compressor, de-esser, limiter, analysers. `references/README.md` line 16 calls it the "studio side". Also excluded from the catalogue: `plugins/README.md` says current releases have no Windows binary. **The LGPLv3/GPLv3 conflict must be resolved from the project's own LICENSE before this ever becomes anything more than reading.** |
| **rkrlv2** (Rakarrack port) | **NO GITLINK IN TREE** | https://github.com/ssj71/rkrlv2.git | **GPLv2** — `references/README.md` line 17, `THIRD_PARTY.md` line 254 | **studied only** | **No** | none | Read for harmonizer, pitch shifter, tremolo óptico. LV2/Linux. |
| **ToobAmp** | **NO GITLINK IN TREE** | https://github.com/rerdavies/ToobAmp.git | ⚠ **CONFLICT.** `references/README.md` line 18: **"MIT (verificar componentes)"** — the upstream author themselves flagged it. `THIRD_PARTY.md` line 255: **GPLv3** | **studied only** | **No** | none | Read for gate, IR loader, EQ and modulation. `references/README.md` line 22 mentions MIT parts of ToobAmp as portable, but **no such port exists in `docs/EFEITOS.md`**, which labels every ToobAmp-derived effect *estudo*. Treat as study-only. LV2/Linux. |
| *(the eight above)* | — | — | — | — | — | — | ⚠ `references/README.md` line 23 states the rule: *"GPL/LGPL … usados como leitura/estudo de algoritmo neste projeto pessoal, não distribuído. Se o GuitarRig NAM um dia for distribuído, revisar a compatibilidade de licenças antes."* Since **the product is already distributed as AGPLv3**, and nothing from `references/` ships, the compatibility question is currently moot — but it becomes live the moment any of these is ported. |

---

## 6. Proposed but **not adopted** (SPEC.md §2.2 and §25.5–25.7)

None of these is in the tree, in `.gitmodules`, or pinned anywhere. **The
"version/SHA" and "license" cells are therefore recorded as
`UNKNOWN — verify at <the project's own LICENSE> at the moment it is adopted`,
because nothing in this repository states them and this task does not guess.**
The "why candidate" and "packaging/redistribution risk" columns come from
SPEC.md §2.2, §12, §25 and from DEVPLAN TRACK-001/002/003, JJAZZ-001, HARM-001.

| component | version/SHA | source URL | license | usage class | ships in release artifact? | redistribution obligations | notes |
|---|---|---|---|---|---|---|---|
| **BTrack** | candidate version **1.0.7** per SPEC §2.2 table; **no pin** → `UNKNOWN — verify at adamstark/BTrack LICENSE at the pinned SHA` | https://github.com/adamstark/BTrack | `UNKNOWN — verify at the project's own LICENSE at a pinned SHA` | **candidate** → benchmarked-only until ADR-TRACKER-001 | **Not yet** | If adopted: GPL-family compatibility is *"acceptable only inside the already-AGPL/open-source path"* (SPEC §25.6) | **Why:** the only listed candidate that is C++, **causal**, real-time and *by construction* inside our hard latency budget — which is why it is first. SPEC §2.2: *"Primary algorithm baseline; must earn selection in guitar-specific benchmark."* §12.1 makes it a required candidate; §12.3 requires the numbers. DEVPLAN TRACK-001 allows a `third_party` dependency declaration + pin. **Risk:** adds a third-party call reachable from the analysis path, so SPEC §7.1's "any new third-party call not audited for bounded real-time behavior" applies; resampling must happen off the callback (TRACK-001). Selection is **ADR-TRACKER-001 (`docs/adr/0002-…`), not this document**. |
| **aubio** | no version pinned | https://github.com/aubio/aubio | `UNKNOWN — verify at the project's own LICENSE at a pinned SHA` | **candidate** → benchmarked-only until ADR-TRACKER-001 | **Not yet** | SPEC §25.6, same as BTrack | **Why:** mature C/C++ onset/tempo/beat/pitch library; SPEC §2.2 *"Benchmark/secondary backend"*, §12.1 required candidate. **Risk:** larger surface than BTrack, LGPL-licensed in the project's own declaration, and its `librosa`-style feature breadth invites scope creep into a callback-adjacent path. DEVPLAN TRACK-002: *"Do not claim superiority; provide measurements."* |
| **BeatNet** | no version or model pin | https://github.com/hashimkarim/beatnet | `UNKNOWN — verify at the project's own LICENSE **and its model weights' terms*** | **candidate — benchmarked only** | **No. Explicitly forbidden for now** | 🚫 **SPEC §25.5 verbatim: "Do not import BeatNet models or datasets into release artifacts until their exact redistribution terms are reviewed."** DEVPLAN TRACK-003: *"This is **not** a shipping integration… No BeatNet code/model is added to release packaging in this task."* | **Why:** SPEC §2.2 — neural online beat/downbeat/tempo/meter tracker, *"Research benchmark only until latency, robustness, packaging, and guitar-specific accuracy justify shipping."* **Why not yet, concretely:** it is a **Python/neural** dependency (DEVPLAN TRACK-003 measures CPU/GPU usage, startup time and package size), which collides with DEVPLAN §18's ban on a Python production sidecar and with the fully-offline requirement (SPEC §1). A hosted drum-source model assumed to hear mastered songs is a poor fit for solo guitar. **TRACK-003 may be a standalone Python tool; nothing enters the product.** The model-weight terms, not just the code licence, are the blocker. |
| **JJazzLab / JJazzLab Toolkit** | candidate version **5.2.1** per SPEC §2.2 table; **no pin** | https://github.com/jjazzboss/JJazzLab | `UNKNOWN — verify at the project's own LICENSE at a pinned SHA` | **candidate — deferred to Phase 2** | **Not yet** | SPEC §25.7 verbatim: *"JJazzLab Toolkit integration is a future, separately reviewed dependency."* | **Why:** generates bass/keys/rhythm guitar from chord+rhythm information — SPEC §2.2 *"Phase-2 full-band candidate"*, §26, DEVPLAN JJAZZ-001. **Blocked by gate discipline, not by licence:** DEVPLAN §26 says do not begin until G7. JJAZZ-001 must measure startup, generation latency, packaging and licence impact, and must never sit on the hard real-time path. |
| **Spotify Basic Pitch** | no version or model pin | https://github.com/spotify/basic-pitch | `UNKNOWN — verify at the project's own LICENSE **and the ICASSP-2022 model weights' terms*** | **candidate — deferred to Phase 2** | **Not yet** | none yet, because nothing ships. Model-weights terms must be checked before it could ever ship | **Why:** SPEC §2.2 — *"Polyphonic transcription/key/chord research. Phase-2 analysis candidate, **never on the hard real-time audio thread**"*; §26; DEVPLAN HARM-001. **MVP non-goal:** automatic chord recognition is explicitly out of scope (SPEC §4.2). |
| **Giada** | candidate **1.6.0** per SPEC §2.2 table | https://github.com/monocasual/giada | `UNKNOWN — verify at the project's own LICENSE at a pinned SHA` | **rejected-as-reference / fallback base** | **No** | n/a — not adopted | **Why considered:** the most mature open live-looper/audio-engine reference; SPEC §2.2 makes it *"Fallback if Guitar-Companion fails stabilization/licensing gate."* **Why not the base:** it is a loop-station/live-engine architecture, not a guitar-input + low-latency monitoring + drum-companion application; SPEC §2.1 picks Guitar-Companion. **Adoption status: fallback only, not adopted.** |
| **Hydrogen** | no pin | https://github.com/hydrogen-music/hydrogen | ⚠ The tree already ships **GPL** audio *from* this project (§1) but states no licence for Hydrogen itself → `UNKNOWN — verify at the Hydrogen repository's own LICENSE and the GMRockKit drumkit's licence file` | **reference / content source** | its **audio content already ships** (§1); its **code does not** | n/a for code; see the GMRockKit row | **Why:** SPEC §2.2 — *"Drum workflow, pattern, kit and humanization reference. Reference/content source where licensing permits; no runtime dependency required."* In practice the fork took its *content* (GMRockKit) and none of its *code*. **Adoption status: content source only.** |

---

## 7. Adoption decision, at a glance

| component | decision |
|---|---|
| Guitar-Companion (the base) | **ADOPTED** — AGPLv3 accepted; see ADR-0001 |
| JUCE, NAM Core, Eigen, nlohmann/json, AudioDSPTools | **ADOPTED (linked)** |
| Airwindows (3 effects) | **ADOPTED (ported/vendor, MIT)** |
| Airwindows, BYOD, Dragonfly, Guitarix, Gx, LSP, rkrlv2, ToobAmp | **REJECTED-AS-REFERENCE** — studied only, never compiled, never shipped |
| Groove MIDI Dataset, midi-drums, GMRockKit, Archivo, Space Grotesk, JetBrains Mono, Leland | **ADOPTED (embedded content)** |
| MuseScore, DrumGroovePro | **STUDIED / CREDITED** — conventions and ideas, nothing copied |
| TONE3000 API + marks | **ADOPTED (runtime service)** — user-supplied credentials, no redistribution right claimed |
| Microsoft WebView2 SDK | **BUILD-TIME DEPENDENCY** — fetched, never committed, never redistributed |
| Steinberg ASIO SDK | **BUILD-TIME OPTION** — user-supplied, must never be redistributed |
| 8 hosted VST3 plugins | **HOSTED, NOT SHIPPED** — user-installed from each project's own release |
| BTrack | **CANDIDATE** — must earn selection in ADR-TRACKER-001 |
| aubio | **CANDIDATE** — same |
| BeatNet | **DEFERRED — BENCHMARK-ONLY**; models/datasets barred from release artefacts (SPEC §25.5) |
| JJazzLab Toolkit | **DEFERRED** — Phase 2, separately reviewed (SPEC §25.7) |
| Basic Pitch | **DEFERRED** — Phase 2, never on the real-time thread |
| Giada | **FALLBACK BASE ONLY** — not adopted |
| Hydrogen | **REFERENCE / CONTENT SOURCE ONLY** — code not adopted |

---

## 8. Third-party **hosted** plugins (user-installed, never shipped)

The product can load any third-party VST3 at runtime: `JUCE_PLUGINHOST_VST3=1`
(`CMakeLists.txt` line 222), up to 8 slots in the chain (`README.md` line 420)
and a hosted drum instrument on GM MIDI channel 10 (`README.md` line 427). The
in-app catalogue (`src/PluginCatalog.cpp`, surfaced by `plugins/README.md`)
downloads each plugin's **official release zip** at install time and extracts the
`.vst3` into `%LOCALAPPDATA%\Programs\Common\VST3` — **no admin, no installer,
no copy in this repository**.

| plugin | version (pinned in `src/PluginCatalog.cpp`) | license | official source |
|---|---|---|---|
| Dragonfly Reverb | 3.2.10 (line 18) | GPLv3 | <https://github.com/michaelwillis/dragonfly-reverb/releases> |
| Airwindows Consolidated (`airwin2rack`) | 2026-07-19 (line 22) | MIT | <https://github.com/baconpaul/airwin2rack> |
| Zam Plugins | 4.5 (line 26) | GPLv2+ | <https://github.com/zamaudio/zam-plugins> |
| AIDA-X | 1.1.0 (line 32) | GPLv3 | <https://github.com/AidaDSP/AIDA-X> |
| Fire | 1.5.0 (line 35) | GPLv3 | <https://github.com/jerryuhoo/Fire> |
| Wolf Shaper | 1.0.2 (line 38) | GPLv3 | <https://github.com/wolf-plugins/wolf-shaper> |
| PeakEater | 0.8.2 (line 41) | GPLv3 | <https://github.com/vvvar/PeakEater> |
| Surge XT Effects | 1.3.4 (line 44) | GPLv3 | <https://github.com/surge-synthesizer/releases-xt> |

**Why none of these ships, and why that matters legally.** `THIRD_PARTY.md`
lines 269–274: an `offline/` folder once held three release zips; two were GPL
(Dragonfly, Zam), and *"shipping GPL **binaries** carries the obligation to
provide the corresponding source to whoever receives them"*, so the fallback was
deleted. `plugins/README.md` repeats it: *"Este repositório **não redistribui
binário de plugin nenhum**."* `.gitignore` line 10 keeps `plugins/offline/` out.
**Any future proposal to vendor a plugin binary must come back here first.**

### Security posture required for hosted plugins

SPEC.md §24 sets the rule this inventory inherits:

> - "Plugin loading/scanning remains off the audio thread."
> - "Treat third-party plugins as crash-risking untrusted code; preserve or
>   improve safe-start behavior."

What that means concretely for every row above:

1. **Never load a plugin on the audio thread.** Scanning, instantiating, panel
   creation and parameter I/O all belong to the message thread. This is also a
   hard real-time rule (SPEC §7.1: "Plugin scanning/loading" is forbidden).
2. **A hosted plugin is arbitrary native code in our address space.** A crash or
   a corrupt write inside a guest VST3 takes the Standalone down with it. Safe
   start — an empty guest slot until the user explicitly loads one — must not be
   traded away for convenience.
3. **Swaps use the existing pending/retired protocol**, not an in-place
   replacement (`README.md` line 386, "RT-safe instance swap"). A guest that
   misbehaves on `process()` must be able to be bypassed without waiting on it.
4. **The catalogue is a downloader, so it is an attack surface.** It fetches
   over the network from eight third-party hosts. Integrity depends on those
   release URLs staying correct; a compromised account there would land code on
   every user who clicks Install. This is worth a follow-up decision (pin a hash
   per catalogue entry) — **recorded here as an observation, not a change.**
5. **User-installed ≠ our licence problem.** Each plugin keeps its own licence
   and the user's copy of its source is the upstream project's business, not
   ours, precisely because we redistribute nothing.

---

## 9. Licence obligation checklist for a release

Derived from §1; each item is a *file-level* action in packaging, and none is a
code change. Items marked **GAP** are not currently satisfied by
`packaging/guitar-companion.iss`.

| # | Obligation | Source | Status |
|---|---|---|---|
| 1 | Ship the complete corresponding AGPLv3 source | `LICENSE` §6 | The repository *is* the source; ensure the release links to the exact tag. **Process step, not a code change.** |
| 2 | Keep the AGPLv3 licence intact and ship it | `LICENSE` §5–6; `packaging/guitar-companion.iss` `LicenseFile=..\LICENSE` | ✅ shipped |
| 3 | Ship `THIRD_PARTY.md` with the app | `THIRD_PARTY.md` line 4 | ✅ shipped (`.iss` `[Files]`) |
| 4 | Carry the MIT notices for NAM Core / AudioDSPTools / nlohmann-json | `THIRD_PARTY.md` lines 69–92 | ✅ present in `THIRD_PARTY.md` |
| 5 | ✅ **Resolve the AudioDSPTools licence conflict** (MIT vs Apache-2.0/MIT) | `BASELINE.md` A4 | **RESOLVED 2026-10-07.** MIT at pinned SHA `0827c6c2…`. `THIRD_PARTY.md` was right; `README.md` line 458 is wrong. No NOTICE obligation. See the table row above and "Corrections" below. |
| 6 | ⚠ **Ship Eigen's four licence files** (`COPYING.MPL2`, `COPYING.BSD`, `COPYING.APACHE`, `COPYING.MINPACK`) | `THIRD_PARTY.md` lines 42–43 | **GAP** — files exist only inside the submodule, are not in `.iss` |
| 7 | ⚠ **Ship the four OFL texts** with the fonts | OFL 1.1; `THIRD_PARTY.md` lines 153–167 | **GAP** — `*-OFL.txt` are not in `.iss` |
| 8 | ⚠ **Resolve the exact GPL version of GMRockKit** and ship that GPL text + attribution | `assets/drums/ORIGEM.txt` line 6 says only "GPL" | **GAP** — attribution present, licence version and text absent |
| 9 | ✅ Retain "Leland" Reserved Font Name; never ship a modified font under it | `assets/fonts/Leland-OFL.txt` | ✅ satisfied — the .otf is unmodified |
| 10 | ✅ CC BY 4.0 attribution + change statement for the Groove MIDI Dataset | `THIRD_PARTY.md` lines 100–105; `src/DrumLibrary.cpp` lines 8–9 | ✅ satisfied |
| 11 | ✅ MIT notice for midi-drums | `THIRD_PARTY.md` lines 118–140; `src/DrumLibrary.cpp` line 555 | ✅ satisfied |
| 12 | ⚠ **Establish a right to ship the TONE3000 marks** | `THIRD_PARTY.md` lines 283–286 (no licence file) | **GAP** — identification use is stated, permission is not evidenced |
| 13 | ✅ Never redistribute the Steinberg ASIO SDK | `CMakeLists.txt` line 224, `.gitignore` line 7 | ✅ satisfied — gitignored, manual download |
| 14 | ✅ Never redistribute Microsoft WebView2 binaries | `THIRD_PARTY.md` lines 296–300, `.gitignore` line 8 | ✅ satisfied — fetched at configure time into a gitignored dir |
| 15 | ✅ Never redistribute a third-party plugin binary | `THIRD_PARTY.md` lines 269–274, `plugins/README.md`, `.gitignore` line 10 | ✅ satisfied |
| 16 | ✅ Preserve upstream notices and attribution | `README.md` lines 60–91, `docs/EFEITOS.md`, `references/README.md` | ✅ satisfied in content — ⚠ **but the upstream commit history is not preserved locally** (`BASELINE.md` §2.2) |
| 17 | ✅ Ship no user credentials | `THIRD_PARTY.md` lines 279–280; `CONTRIBUTING.md` rule 4 | ✅ satisfied |

**Six gaps (#5, #6, #7, #8, #12, and the history half of #16) are release
blockers under SPEC §25.3 and §28 item 12, and all of them are packaging or
provenance decisions rather than code.** Item #16's content half — the upstream
notices and per-effect attribution — is satisfied; only the missing upstream
commit history (see [`BASELINE.md` §2.2](BASELINE.md#2-history-shape-of-the-fork--and-a-real-provenance-defect))
is outstanding.