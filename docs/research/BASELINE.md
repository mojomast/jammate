# FND-001 — Frozen baseline of the fork

**Task:** FND-001 — Fork provenance and dependency inventory
**Written:** 2026-10-07
**Local branch:** `wp/FND-001-deps`
**Base commit for this task:** `48f301c8fd046429fe55519ec3a2d20a2fccaa44`

This document freezes *what the code actually is*. The companion document
[`DEPENDENCIES.md`](DEPENDENCIES.md) freezes *what may legally and technically
ship*. The reasoning for choosing this base at all is
[`docs/adr/0001-base-selection.md`](../adr/0001-base-selection.md).

Every claim below is either **VERIFIED** (with the command or file that proves
it) or **ASSUMED** (explicitly marked, with the reason it could not be
verified here). Nothing is inferred from memory.

---

## 1. Identity of the base

| Field | Value | Evidence |
|---|---|---|
| Upstream repository | `https://github.com/raphaelfukuda/Guitar-Companion` | `SPEC.md` line 7, §27; `README.md`; `packaging/guitar-companion.iss` `AppUrl` |
| Frozen upstream SHA | `88f7e7c805c9c5e17388154a678c2c6a3633ff23` | `SPEC.md` line 7 |
| Local commit carrying it | `88f7e7c805c9c5e17388154a678c2c6a3633ff23` | `git rev-parse 88f7e7c` |
| Upstream branch | `refs/heads/master` | `git ls-remote` (verbatim below) |
| Upstream commit author | `rapha <fukuda.rullo@gmail.com>`, 2026-07-31T20:52:53Z | GitHub `git/commits` API |
| Signature | **unsigned** (`verification.verified = false`, `reason = "unsigned"`) | GitHub `git/commits` API |
| Tree SHA (local `88f7e7c`) | `67ddc31f773020158ed0d071d531b0983653177c` | `git rev-parse 88f7e7c^{tree}` |
| Tree SHA (upstream `88f7e7c`) | `67ddc31f773020158ed0d071d531b0983653177c` | GitHub `git/trees` API |
| Product licence | **AGPLv3** — `LICENSE` lines 1–2: `GNU AFFERO GENERAL PUBLIC LICENSE` / `Version 3, 19 November 2007`, 661 lines | `LICENSE` |

### 1.1 Upstream SHA re-verification (verbatim)

Command actually run, in this worktree:

```
$ git ls-remote https://github.com/raphaelfukuda/Guitar-Companion HEAD
88f7e7c805c9c5e17388154a678c2c6a3633ff23	HEAD
```

Full listing of the same remote, run to record branch and tag state:

```
$ git ls-remote https://github.com/raphaelfukuda/Guitar-Companion
88f7e7c805c9c5e17388154a678c2c6a3633ff23	HEAD
88f7e7c805c9c5e17388154a678c2c6a3633ff23	refs/heads/master
1a20c07039a1261637a5f217bd56d87ac043b194	refs/tags/v0.1
55d1bffeb5a707b5b3b6bc50e0b5a5b40044006c	refs/tags/v0.1^{}
```

**Result: the upstream SHA has NOT drifted.** It is identical to the SHA frozen
in `SPEC.md` and it is still the tip of `master`. The newest tag is `v0.1`, whose
annotated tag object is `1a20c070…` peeling to commit `55d1bffeb5a7…` — i.e. the
frozen `88f7e7c…` is **25 commits past the v0.1 release tag**, on an unreleased
`master`. There is no upstream commit after `88f7e7c…`.

### 1.2 Content identity: VERIFIED, not assumed

The strongest available statement is a tree-hash comparison, and it is an
exact match:

```
$ git rev-parse 88f7e7c^{tree}
67ddc31f773020158ed0d071d531b0983653177c
```

GitHub, for the same commit SHA:

```json
{"sha":"88f7e7c805c9c5e17388154a678c2c6a3633ff23",
 "tree":"67ddc31f773020158ed0d071d531b0983653177c", ...}
```

A git tree SHA is a content hash over every path, mode and blob in the tree,
including the two submodule gitlinks. Equality therefore proves the local
`88f7e7c` checkout is byte-identical to the upstream `master` snapshot — not
merely "similar". The upstream top-level listing was additionally compared
entry by entry (`.editorconfig`, `.gitea/…`, `.gitignore`, `.gitmodules`,
`CMakeLists.txt`, `CONTRIBUTING.md`, `LICENSE`, `README.md`, `THIRD_PARTY.md`,
`assets/`, `docs/`, `packaging/`, `plugins/`, `references/`, `src/`, `tests/`,
`third_party/`) and every blob SHA matches.

---

## 2. History shape of the fork — and a real provenance defect

### 2.1 Local history

```
$ git log --format='%H%x09%an%x09%ae%x09%ad%x09%s' --date=short
48f301c8fd046429fe55519ec3a2d20a2fccaa44	Orchestrator	orchestrator@local	2026-10-07	feat(jam-core): frozen jam seams, working platform-neutral build, and test harness
ad3ce6c18dc3f5577c77c91632f203be28d13460	Jam Orchestrator	orchestrator@local	2026-10-07	docs: add SPEC.md and DEVPLAN.md on top of upstream baseline 88f7e7c
88f7e7c805c9c5e17388154a678c2c6a3633ff23	rapha	fukuda.rullo@gmail.com	2026-07-31	align: mirar -30 dB e nunca subir lane - -18 estourava a saida

$ git rev-list --count HEAD
3
```

`git show --stat 88f7e7c` reports **123 files changed, 34 940 insertions** — a
single commit that introduces the entire project.

The two commits on top are this project's own work:

| Commit | Touches | Note |
|---|---|---|
| `ad3ce6c` | `SPEC.md`, `DEVPLAN.md` | adds 2 654 lines; no product code touched |
| `48f301c` | `CMakeLists.txt` (+15), `jam-core/CMakeLists.txt`, `src/jam/**`, `tests/jam/**`, `docs/adr/0003-…`, `HANDOFF.md`, `EXECUTION-LEDGER.md` | 14 files, +1 550 lines; the only edit to the upstream top-level `CMakeLists.txt` so far (`jam-core` subdir + tests hook) |

Because `CMakeLists.txt` blob at `48f301c` is `8479a9fc…` and at `88f7e7c` is
`181d8439…`, the upstream file is still recoverable verbatim with
`git show 88f7e7c:CMakeLists.txt`.

### 2.2 ⚠ The upstream ancestry is missing locally

**This is the single most important provenance finding in this task.**

```
$ git log --format='%H %P' 88f7e7c
88f7e7c805c9c5e17388154a678c2c6a3633ff23
```

Locally, `88f7e7c` is a **root commit with no parents**. Upstream, the same SHA
has a parent:

```json
"parents": [{"sha": "1d307b6ec61a5eea43c52c67e6b35f71d4dbe14e", ...}]
```

Walking the upstream chain via the GitHub API reached **59 ancestors**, all
single-parent, back to at least `145a69c…` / 2026-07-24 (the walk stopped there
because the unauthenticated GitHub API rate limit was exhausted —
`rate_limit.core.remaining = 0` — so the **total upstream commit count could not
be determined**; the true depth is ≥ 60).

Representative tip messages of the upstream ancestry (newest first), so a later
reader can see what kind of project this is:

```
88f7e7c  2026-07-31  align: mirar -30 dB e nunca subir lane - -18 estourava a saida
1d307b6  2026-07-31  preset: slot de IR nomeado e vazio agora ESVAZIA, em vez de manter o anterior
b9ce930  2026-07-31  rig: detecta cab embutido na captura, alinha niveis e muta lane
9194435  2026-07-30  plugin: fabricante vira "Raphael Fukuda", nao "Rapha"
0bd4e65  2026-07-30  docs: diz QUAL arquivo baixar para instalar so o plugin
bc8102d  2026-07-28  docs: credita as bibliotecas que estao DENTRO do binario
e168097  2026-07-28  docs: credita a MuseScore pela partitura, nao so pela fonte
55d1bff  2026-07-28  release v0.1 marcada como pre-release; links vao para /releases
2d434a2  2026-07-28  rename: PedalForge NAM -> Guitar Companion, sem sobras
9b892a9  2026-07-27  plugins: remove plugins/offline do repositorio
85fc634  2026-07-27  docs: transparencia de atribuicao antes de abrir o codigo
d53e309  2026-07-27  TONE3000: prompt flows do free tier + chave por usuario na propria UI
f5e7732  2026-07-24  vNext (F6): Song/Scenes por secao + gravacao de stems guitarra/bateria
a6bad3b  2026-07-24  vNext (F6b): tela SONG/SCENES dedicada conforme o mockup
e0d7ae1  2026-07-25  vNext: redesign completo conforme docs/design/pedalforge-vnext-complete.html
```

(Commit subjects are unaccented here only because this record is written in
English; they are Portuguese in the repository.)

**Consequence, stated plainly:** the tree content of the fork is exactly
upstream's `master`, but the *history* was replaced by a single snapshot
commit. `README.md` line 48 and `CONTRIBUTING.md` line 82 promise that the
project's own history is preserved — that promise is true upstream and
**currently not true in this fork**. `SPEC.md` §25.2 requires preserving
"Guitar-Companion notices and history required by its license".

**Action for the orchestrator (not performed by this task, which is
documentation-only and does not touch git history):** decide explicitly whether
to (a) keep the snapshot and record the upstream SHA + URL + this file as the
authoritative provenance pointer, or (b) re-import the upstream ancestry
(`git fetch upstream && git replace`/`filter-branch`-style graft) before any
release. Option (a) preserves the frozen-SHA guarantee that every other document
in this repository relies on; option (b) is better for AGPL §13 notice
purposes. This is a gate-G0 item, not an FND-001 code change.

---

## 3. Submodules

### 3.1 `.gitmodules` declares ten; the tree pins two

```
$ git submodule status
-91ad83ae34a81e0833b1a2b0866f54846370ae53 third_party/JUCE
-1f42f88535884450104b8711d7595019afa0495b third_party/NeuralAmpModelerCore

$ git ls-tree HEAD:third_party/
160000 commit 91ad83ae34a81e0833b1a2b0866f54846370ae53	JUCE
160000 commit 1f42f88535884450104b8711d7595019afa0495b	NeuralAmpModelerCore

$ git ls-tree HEAD:references/
100644 blob 608b80bd2b8fde0323db9e229b11f4fde62e2a8f	README.md
```

The leading `-` in `git submodule status` means **not initialised**; both
`third_party/` submodule directories are empty on disk. This is deliberate —
this task was forbidden from cloning, and ADR-0003 records that disk is at 96 %.
`third_party/JUCE/CMakeLists.txt` and the NAM sources therefore do not exist on
disk here, and no licence text inside the submodules could be read directly.

The GitHub API listing of the upstream tree at `88f7e7c` shows the same two
gitlinks and the same SHAs, and `references/` containing only `README.md`.

### 3.2 ⚠ `.gitmodules` declares eight `references/*` submodules that are NOT pinned

`references/README.md` states the reference projects are "Submódulos **pinados**"
(pinned submodules). They are declared:

```
references/airwindows        https://github.com/airwindows/airwindows.git
references/BYOD              https://github.com/Chowdhury-DSP/BYOD.git
references/dragonfly-reverb  https://github.com/michaelwillis/dragonfly-reverb.git
references/guitarix          https://github.com/brummer10/guitarix.git
references/GxPlugins.lv2     https://github.com/brummer10/GxPlugins.lv2.git
references/lsp-plugins       https://github.com/lsp-plugins/lsp-plugins.git
references/rkrlv2            https://github.com/ssj71/rkrlv2.git
references/ToobAmp           https://github.com/rerdavies/ToobAmp.git
```

…all with `shallow = true`, but **there is no `160000 commit …` gitlink for any
of them in the tree**, locally *or* upstream. Therefore:

- `git submodule update --init references/airwindows` (the command printed in
  `references/README.md`) does **not** work on this repository;
- there is **no pinned SHA** for any reference project — `DEPENDENCIES.md`
  records `NO GITLINK IN TREE` for those rows rather than inventing one;
- the *stated intent* ("pinned so any claim can be checked against the real
  source", `README.md` line 91) is **not currently satisfied**. Provenance for the
  studied references rests on `docs/EFEITOS.md` and `references/README.md`, both
  of which are upstream documentation, not pinned source.

This is an upstream defect, inherited unchanged by the fork (same blob SHAs for
both `.gitmodules` and `references/README.md`). It is recorded here because
SPEC.md §25.4 asks for a version/SHA per recorded dependency, and for these eight
the honest answer is "none pinned".

### 3.3 Not submodules, but gitignored

`.gitignore` lines 7–8 exclude `third_party/asiosdk/` and
`third_party/webview2/`. Both are downloaded or manually extracted at
configure time; neither is in the tree, and neither may be redistributed from
this repository. See `DEPENDENCIES.md` §4 and §5.

---

## 4. Build baseline situation

### 4.1 Verbatim from `CMakeLists.txt`

The plugin target, `CMakeLists.txt` lines 110–141 (abridged to the
load-bearing options; `FORMATS` is verbatim and complete):

```cmake
juce_add_plugin(GuitarCompanion
    COMPANY_NAME "Raphael Fukuda"
    BUNDLE_ID "com.raphaelfukuda.GuitarCompanion"
    PRODUCT_NAME "Guitar Companion"
    PLUGIN_MANUFACTURER_CODE Raph
    PLUGIN_CODE Gtco
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    NEEDS_WEBVIEW2 ${GUITAR_COMPANION_HAS_EMBEDDED_BROWSER}
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/icon-256.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/icon-small-16.png"
    COPY_PLUGIN_AFTER_BUILD FALSE)
```

The opt-in test target, `CMakeLists.txt` lines 284–289, verbatim:

```cmake
option(GUITAR_COMPANION_BUILD_TESTS "Build the unit tests" OFF)

if(GUITAR_COMPANION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

Other recorded build facts:

| Fact | Value | Line |
|---|---|---|
| Minimum CMake | `cmake_minimum_required(VERSION 3.22)` | 1 |
| Project / version | `project(GuitarCompanion VERSION 0.1.0 LANGUAGES C CXX)` | 3 |
| C++ standard (plugin) | `set(CMAKE_CXX_STANDARD 17)` | 5–6 |
| JUCE wired in | `add_subdirectory(third_party/JUCE)` | 8 |
| NAM Core target | `add_library(nam_core STATIC …)`, `target_compile_features(nam_core PRIVATE cxx_std_20)` | 16–32 |
| Embedded browser option | `option(GUITAR_COMPANION_EMBEDDED_BROWSER … ON)` | 65 |
| WebView2 version pin | `GUITAR_COMPANION_WEBVIEW2_VERSION "1.0.3485.44"` | 68 |
| ASIO option | `set(ASIOSDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK …")` | 226 |
| Binary data target | `juce_add_binary_data(GuitarCompanionAssets SOURCES …)` — 7 font files + 2 TONE3000 images + 27 `.wav` | 145–182 |
| `jam-core` always built | `enable_testing()` / `add_subdirectory(jam-core)`, not behind any option | 273–274 |
| Plugin host flag | `JUCE_PLUGINHOST_VST3=1` | 222 |
| curl | `JUCE_USE_CURL=0` | 219 |
| Windows backends | `JUCE_WASAPI=1`, `JUCE_DIRECTSOUND=1` | 209–210 |

`README.md` line 241 states the documented build host requirements: *VS 2022
(Build Tools or Community) with C++, CMake ≥ 3.22, Git*; artefacts land in
`build\GuitarCompanion_artefacts\Release\`
(`Standalone\Guitar Companion.exe` and `VST3\Guitar Companion.vst3`).

### 4.2 ⚠ The JUCE target is not buildable in this environment

**No build was attempted by this task** (it is documentation-only, and building
was explicitly forbidden). The blocker is already measured and recorded in
[`docs/adr/0003-build-environment-constraint.md`](../adr/0003-build-environment-constraint.md),
which I quote rather than re-derive:

- `juce::juce_audio_devices` and `juce::juce_gui_basics` need the **ALSA** and
  **freetype** development headers;
- ADR-0003 measured them **absent** (`/usr/include/alsa` does not exist), with
  `sudo` **unavailable** (`sudo -n true` fails), so they cannot be installed;
- `tests/CMakeLists.txt` links `juce::juce_audio_formats` and
  `juce::juce_audio_processors`, so the opt-in `GuitarCompanionTests` target is
  blocked for the same reason;
- ADR-0003: *"no gate that requires the JUCE target to compile can be closed on
  this machine"*, which includes FND-002, the SPEC §18.1 zero-allocation proof on
  the real callback, and every SPEC §21.5 hardware item.

Consequence for **this** document: everything above about what is *compiled* is
read statically out of `CMakeLists.txt`, `tests/CMakeLists.txt` and
`jam-core/CMakeLists.txt`. **No statement in `DEPENDENCIES.md` is backed by an
observed successful link or a binary produced here.** The "ships in release
artifact" column is derived from `CMakeLists.txt`'s
`juce_add_binary_data`/`target_link_libraries` graph plus
`packaging/guitar-companion.iss`, and is explicitly an inference, not a
measurement. ADR-0003 also forbids filling the gap: a JUCE build here would
"produce a binary that proves nothing about the Windows/ASIO target that
actually ships".

The only lane that *is* buildable and testable on this machine is `jam-core`
(`cmake -S jam-core -B build -G Ninja`, ADR-0003 lane "core"), which has **no
`juce::` dependency at all** and therefore no third-party dependency of its own.

---

## 5. Verified vs. assumed

### 5.1 VERIFIED from this repository or from the upstream remote

| # | Claim | How |
|---|---|---|
| V1 | Upstream `master` tip is `88f7e7c805c9c5e17388154a678c2c6a3633ff23` | `git ls-remote`, verbatim in §1.1 |
| V2 | Local `88f7e7c` tree is byte-identical to upstream's | tree SHA equality, §1.2 |
| V3 | The repo has 3 commits; `88f7e7c` is a local root commit | `git rev-list --count HEAD`, `git log --format='%H %P'` |
| V4 | Upstream `88f7e7c` has ≥ 60 ancestors; 59 walked | GitHub `git/commits` API |
| V5 | Project licence is AGPLv3 | `LICENSE` lines 1–2 |
| V6 | JUCE pinned at `91ad83ae34a81e0833b1a2b0866f54846370ae53` | `git ls-tree HEAD:third_party/` |
| V7 | NAM Core pinned at `1f42f88535884450104b8711d7595019afa0495b` | same |
| V8 | Neither submodule is initialised | `git submodule status` leading `-`; empty dirs |
| V9 | Eight `references/*` submodules are declared but have no gitlink, upstream too | `.gitmodules` vs `git ls-tree` vs GitHub tree API |
| V10 | Plugin formats are Standalone + VST3; tests are opt-in and OFF by default | `CMakeLists.txt` lines 110–141, 284–289 |
| V11 | 27 drum `.wav` + 7 font files + 2 TONE3000 images are compiled into the binary | `CMakeLists.txt` lines 145–182 |
| V12 | Drum samples are GMRockKit, GPL | `assets/drums/ORIGEM.txt` lines 4–8, quoted in `DEPENDENCIES.md` |
| V13 | All four font families are OFL 1.1 with named copyright holders | the four `assets/fonts/*-OFL.txt` headers, quoted in `DEPENDENCIES.md` |
| V14 | ASIO and WebView2 SDK are gitignored and fetched/extracted at configure time | `.gitignore` lines 7–8, `CMakeLists.txt` lines 71–108 and 226–239 |
| V15 | No third-party VST3 binary is redistributed by this repo | `THIRD_PARTY.md` §"Built-in VST3 plugin catalog", `plugins/README.md`, `.gitignore` line 10 (`plugins/offline/`) |
| V16 | The installer ships only the two artefacts, the icon, LICENSE, THIRD_PARTY.md, README.md | `packaging/guitar-companion.iss` `[Files]` |
| V17 | The JUCE target cannot be built here | ADR-0003 measured table |
| V18 | `jam-core` has no `juce::` dependency | `jam-core/CMakeLists.txt` rules 1–2 |
| V19 | The frozen SHA is 25 commits past the upstream `v0.1` tag | `git ls-remote` tags vs master |
| V20 | The commit is unsigned | GitHub API `verification.reason = "unsigned"` |

### 5.2 ASSUMED / NOT VERIFIABLE HERE

| # | Item | Why not verifiable in this environment | Where a human must look |
|---|---|---|---|
| A1 | JUCE is version **8.0.15** | The submodule is not checked out, so no `JUCE/CMakeLists.txt` / `juce_5.x` version constant is readable. Claimed by `README.md` line 342 and `THIRD_PARTY.md` line 55, neither of which is the version file | after `git submodule update --init third_party/JUCE`: `third_party/JUCE/CMakeLists.txt` `VERSION` / `JUCE_VERSION`, or `git -C third_party/JUCE describe --tags` at SHA `91ad83ae…` |
| A2 | NAM Core is version **v0.5.4** | Submodule not checked out. Claimed by the `CMakeLists.txt` line 13 comment ("Lista de fontes pinada ao v0.5.4"), `README.md` line 343 and `THIRD_PARTY.md` line 18 | after init: `git -C third_party/NeuralAmpModelerCore describe --tags` at SHA `1f42f885…`, and the submodule's own `LICENSE` |
| A3 | Eigen version | No version string in the tree; `Dependencies/eigen` does not exist locally | `third_party/NeuralAmpModelerCore/Dependencies/eigen/` after init — see `CMakeLists.txt` for the submodule's own pinning (`git submodule status` *inside* the NAM submodule) |
| A4 | AudioDSPTools licence — **the tree disagrees with itself** | `THIRD_PARTY.md` line 24–26 says **MIT**; `README.md` line 458 says **Apache-2.0/MIT**. Both are quoted verbatim in `DEPENDENCIES.md`. The library is inside the NAM submodule, so it is unreadable here | `third_party/NeuralAmpModelerCore/Dependencies/AudioDSPTools/LICENSE` (or `COPYING`) at SHA `1f42f885…`. **Resolve before any release.** |
| A5 | Steinberg **VST3 SDK** version and licence terms | It ships *inside* the JUCE submodule; nothing in this tree names a version. `THIRD_PARTY.md` line 61 says it is "dual-licensed GPLv3 / proprietary; this project uses the GPLv3 option" | `third_party/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/` after init — read its own `LICENSE` and version header |
| A6 | Exact licence **version** strings of most GPL reference projects | Submodules are not cloned and are not pinned in the tree. `references/README.md` and `THIRD_PARTY.md` disagree with each other in places (see the conflict table in `DEPENDENCIES.md` §6) | upstream `LICENSE` of each project at a recorded SHA — which requires first *pinning* them (V9 defect) |
| A7 | Airwindows copyright year | `THIRD_PARTY.md` line 210 says `Copyright (c) Chris Johnson / Airwindows` while `README.md` line 83 says `Copyright (c) 2018 Chris Johnson` | the `LICENSE` file of `airwindows/airwindows` at a pinned SHA |
| A8 | Total upstream commit count | GitHub API rate limit exhausted mid-walk (`remaining = 0`) | `git clone --filter=blob:none https://github.com/raphaelfukuda/Guitar-Companion` then `git rev-list --count 88f7e7c` |
| A9 | Whether the shipped binary actually contains what `CMakeLists.txt` says | No build was performed (forbidden) and the JUCE target cannot be built here anyway (ADR-0003) | FND-002 on a Windows/ASIO machine |
| A10 | BeatNet / BTrack / aubio / JJazzLab / Basic Pitch licences | Not vendored, not pinned, not in `.gitmodules`; nothing in the tree states them. `DEPENDENCIES.md` therefore records them as **candidate / deferred with an unverified licence** rather than asserting one | each project's own `LICENSE` at a pinned SHA, at the time it is actually adopted |
| A11 | TONE3000 logo/asset usage rights | `THIRD_PARTY.md` line 284 says they are used "following their published design guidance"; no licence file exists in the tree | `https://www.tone3000.com/api` published brand/terms, plus TONE3000's written permission |

### 5.3 Conflicts found inside the tree

These are upstream inconsistencies between two files that both claim to be the
record of truth. None were corrected — this task is documentation-only.

| Conflict | `THIRD_PARTY.md` says | Another file says |
|---|---|---|
| AudioDSPTools licence | **MIT** (line 24) | `README.md` line 458: **"Apache-2.0/MIT (see repository)"** |
| LSP Plugins licence | **GPLv3** (line 253) | `references/README.md` line 16: **LGPLv3** |
| Dragonfly Reverb licence | **GPLv3** (line 250) | `references/README.md` line 13: GPLv3 — agrees |
| Guitarix | **GPLv2-or-later** (line 251) | `references/README.md` line 14: GPLv2 — wording differs, same family |
| GxPlugins.lv2 | **GPL** (line 252) | `references/README.md` line 15: GPLv3 — unspecified version vs v3 |
| ToobAmp | **GPLv3** (line 255) | `references/README.md` line 18: **"MIT (verificar componentes)"** — and the same line says ToobAmp is LV2/Linux only |
| References are "pinned submodules" | `references/README.md` line 3; `README.md` line 91 | **no gitlink exists for any of them** (V9) |
| Airwindows copyright | `Copyright (c) Chris Johnson / Airwindows` (line 210) | `README.md` line 83: `Copyright (c) 2018 Chris Johnson` |
| Groove count | `README.md` line 429: "~460 factory grooves+fills … 16 genres"; line 123: "~157 grooves and ~53 fills across 14 genres" | internally inconsistent within the same file |

None of these affect whether anything ships illegally *today* (the products are
already AGPL, and the reference projects are not distributed at all), but they
are the kind of thing that fails an audit, so they are listed rather than
smoothed over. Resolution owners are the orchestrator and the reviewer; the
licence-bearing ones (A4, LSP/ToobAmp) must be settled before G7.

---

## 6. Files that carry a licence or attribution (complete list in the tree)

| File | What it establishes |
|---|---|
| `LICENSE` | AGPLv3 for the product |
| `THIRD_PARTY.md` | the upstream credits and obligations (341 lines) |
| `README.md` §"Where the inspirations came from" | per-project credit table with the "studied vs ported" distinction |
| `docs/EFEITOS.md` | per-effect map: gear inspiration / project studied / what actually runs, with ports labelled |
| `references/README.md` | per-reference-project licence table and the MIT-vs-GPL rule |
| `assets/drums/ORIGEM.txt` | drum sample provenance and licence |
| `assets/fonts/Archivo-OFL.txt`, `-JetBrainsMono-OFL.txt`, `-Leland-OFL.txt`, `-SpaceGrotesk-OFL.txt` | OFL 1.1 texts + named copyright holders |
| `plugins/README.md` | hosted-plugin catalogue with versions/licences + the no-redistribution rule |
| `CONTRIBUTING.md` §"License" | contributor agreement: contributions are AGPLv3 |
| `packaging/guitar-companion.iss` | which files actually reach the installer |
| `.gitmodules` | submodule URLs and the `shallow = true` flags |
| `.gitignore` | what is deliberately *not* in the tree (ASIO SDK, WebView2 SDK, offline plugin zips) |

`assets/brand/` has **no** licence or attribution file. `tone3000-logo.png` and
`t3k-mark.png` are compiled into the binary and are TONE3000's marks; the only
statement about them in the tree is `THIRD_PARTY.md` lines 283–286. Recorded as
`UNKNOWN — verify at <https://www.tone3000.com/api> brand terms>` in
`DEPENDENCIES.md`.

---

## 7. Bottom line for the gates

- SPEC §25.1 (AGPLv3 acceptable for the intended distribution) — **accepted as
  an assumption by the product owner**, recorded in `SPEC.md` line 8 and
  restated as a hard consequence in ADR-0001. Closed-source distribution is
  **incompatible**, not merely discouraged.
- SPEC §25.2 (notices **and history**) — notices are present and good; **history
  is not preserved locally** (§2.2). Gate G0 item.
- SPEC §25.4 (version/SHA/license/URL/usage class per dependency) — delivered by
  `DEPENDENCIES.md`. Four items could not be resolved from the tree and are
  marked `UNKNOWN — verify at …` rather than guessed (§5.2).
- SPEC §25.3 (sample/font notices) — **satisfied**; the font OFL texts and the
  drum-sample notice are in the tree and quoted in `DEPENDENCIES.md`.
- DEVPLAN G0 "clean baseline build documented" — **NOT closed here**; that is
  FND-002, blocked by ADR-0003 in this environment.