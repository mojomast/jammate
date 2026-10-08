# CI-SETUP — Gitea Actions configuration, assumptions, and what is not proven

**Task:** CI-002 — replace the obsolete BTrack expected-failure tripwire with real tracker CI
**Branch:** `wp/CI-002-trackers` · **Base commit:** `eac59ba`
**Written:** 2026-10-07 · first created by **CI-001** (branch `wp/CI-001`, base `a8da4f2`)

This repository's CI is written for **Gitea** and stored in
`.gitea/workflows/`, not `.github/workflows/`, per ADR-0003 decision 6 and
EXECUTION-LEDGER deviation **D3**. **The actual hosting and runner configuration
remain unverified** — the ledger records exactly that (`Forge | Gitea workflow
definitions; actual hosting and runners remain unverified`). "Targets Gitea" is
the intended forge, not an observation. Nothing under `.github/` exists and
nothing should create it: if the forge really is Gitea then GitHub would not
read a `.github/workflows/` file, and if it turned out to be GitHub then this
whole directory would be a pipeline that looks real and never runs. Confirm the
forge and runners before the first run (§3).

Read **§3** before the first run. It is the part that can waste an afternoon.

> **No remote runner has run any of these workflows.** Nothing here is evidence
> that the server-side CI is green. Every measurement below was taken locally
> from the branch worktree, exactly as §7 says. See **§4**.

---

## 1. What is in `.gitea/workflows/`

| File | Lane | Provable from the dev machine? |
|---|---|---|
| `jam-core-linux.yml` | **core** — dependency-free `jam-core`, CMake + Ninja, ctest | **Yes, fully** (§7.1) |
| `jam-core-trackers-linux.yml` | **core + GPL lanes** — all four tracker configurations: dep-free, BTrack, aubio, both | **Yes, fully** (§7.2) |
| `plugin-windows.yml` | **plugin** — JUCE `GuitarCompanion` (Standalone + VST3) + `GuitarCompanionTests` | **No. Never compiled ON WINDOWS.** Linux Standalone+VST3 have built (§4) |

The lanes are ADR-0003's. They are not interchangeable: `jam-core` is
platform-neutral (no JUCE, no audio device, no audio headers); the plugin
target needs Windows, where it has never been built. Be precise: on **Linux**
the Standalone and VST3 formats have been configured and built without root
using extracted Debian dev headers (`docs/research/LOCAL-LINUX-BUILD.md`). The
unmeasured lane is Windows/MSVC/VST3/ASIO, not "the plugin" as such.

`jam-core-btrack-linux.yml` (CI-001) was **renamed** to
`jam-core-trackers-linux.yml` by CI-002. The old name described a job that only
existed because BTrack was unimplemented; that job no longer exists. See §2.

### 1.1 Why none of these files uses `uses:`

**The workflows reference zero actions.** Checkout is a plain `git init` +
depth-1 `git fetch` + `git checkout --detach` in a `run:` block.

This is the single largest source of avoidable risk on a server whose
configuration is invisible from here. If `actions/checkout@v4` does not resolve
— Gitea does not fetch marketplace actions from `github.com`; it needs either a
server-side mirror or a `gitea.com/actions/...` reference — the job fails
during job preparation, **before the first step runs**, and `continue-on-error`
on a later step does not save it. There is no version of "swap in
`actions/checkout` later" that is cheaper than "just use git".

The same reasoning removes `actions/cache` and `actions/upload-artifact`, and is
why the tracker job uses a `for` loop instead of `strategy.matrix` (§2.3). See
§6 for exactly where to put the actions back.

Consequence, stated plainly: these files are longer than they would be with
`uses:`, and they are longer on purpose.

---

## 2. The tracker job now really builds and tests both trackers

### 2.1 What CI-001 did, and why it had to change

CI-001 shipped `jam-core-btrack-linux.yml` as a **tripwire**. BTrack had no
adapter, so `-DJAM_ENABLE_BTRACK=ON` was asserted to fail with

```
JAM_ENABLE_BTRACK is ON but no sources exist in src/btrack/.  The adapter
has not been written yet.
```

and the job was deliberately designed to go **red** the day TRACK-001 landed, so
the tripwire could not be forgotten. That day has arrived:

| Deliverable | Commit family | What it added |
|---|---|---|
| TRACK-001 | `17fe481`, `c3dad10` | `src/btrack/BTrackBackend.*`, `tests/jam/BTrackBackendTests.cpp` (`jam.BTrackBackend`) |
| TRACK-002 | `fef77ec`, `2b2b095` | `src/aubio/AubioBackend.*`, `tests/jam/AubioBackendTests.cpp` (`jam.AubioBackend`) |
| EVAL-004 | `8ae3249`, `64b39ee` | `tests/jam/BackendRunnerTests.cpp` (`jam.BackendRunner`) and the `RhythmDerived*` suites |

`jam-core/CMakeLists.txt` wires both backends behind independent OFF-by-default
options. Keeping the expected-failure step would now make the lane permanently
red for the reason opposite to the one it was written for. It is **deleted, not
suppressed**, and replaced with a genuine four-configuration run.

### 2.2 The four configurations

`jam-core-trackers-linux.yml`, step 3, configures, builds and runs ctest for:

| Config | `JAM_ENABLE_BTRACK` | `JAM_ENABLE_AUBIO` | Suites measured at base `eac59ba` |
|---|---|---|---|
| `depfree` | OFF | OFF | 6 core |
| `btrack` | ON | OFF | 6 core + `jam.BTrackBackend` |
| `aubio` | OFF | ON | 6 core + `jam.AubioBackend` |
| `both` | ON | ON | 6 core + both |

All four run **in one job, sequentially**, and each is allowed to fail without
hiding the others: the job measures every configuration and is red if any of
them is. The per-configuration `*-configure.log` / `*-build.log` /
`*-ctest.log` are printed on failure (§6).

### 2.3 Why one job with a `for` loop, not `strategy.matrix`

A matrix would give per-configuration check names. It is not used because
**Gitea's support for `strategy.matrix` on this server is unverified**, and an
unsupported workflow key kills the job during preparation, before the first
step runs — the same failure mode as an unresolvable `uses:`. A `for` loop needs
nothing the runner can lack. This is the same conservatism as §1.1, applied to
the other unverified Gitea feature. If a first run confirms matrices work,
converting is a mechanical change.

### 2.4 The OFF boundary is proven, not assumed

Two claims are load-bearing and both are machine-checked **in every
configuration**, so they cannot rot:

1. **`src/jam/` stays third-party-free.** `libjam-core.a` and `jamTests` must
   contain **zero** BTrack/aubio/`kiss_fft` symbols — with both trackers ON as
   well as OFF. This is what `jam-core/CMakeLists.txt` and
   `third_party/{BTrack,aubio}/CMakeLists.txt` promise at review time, turned
   into a machine check. It is the `nm` test in step 3, matched case-insensitively
   after nm's header lines are stripped, so a capitalised `BTrack`/`AubioBackend`
   leak cannot slip past a lowercase-only pattern.
2. **The options genuinely gate the code.** In the dependency-free
   configuration the build graph must not reference `third_party/BTrack` or
   `third_party/aubio` at all; and a tracker configured OFF must not register
   its ctest suite. The enabled configurations additionally prove the opposite
   direction with **explicit symbol assertions**, not just suite registration:
   `jamBTrackTests` must carry `BTrack::` and `kiss_fft` symbols and
   `btrack/libjam-btrack.a` must carry `jam::BTrackBackend`; `jamAubioTests`
   must carry `aubio_` symbols and `aubio/libjam-aubio.a` must carry
   `jam::AubioBackend`. Real counts at the base commit: `BTrack::`=33,
   `kiss_fft`=5, `aubio_`=136. A stub that registered a suite and linked
   nothing fails these, so "ON" cannot be a no-op.

**`nm` is run to a file; both its exit status and a non-empty symbol output are
required before counting.** Measured failure modes: `nm` exits **0 with no
output** on an empty archive and exits **0 with "no symbols"** on a stripped
binary such as `/bin/true`; only a corrupt archive or a missing file exits
non-zero. An exit-code-only pipeline therefore false-greens a corrupt or stub
object. The job captures nm output, requires a symbol line, and only then greps
it. The same pattern is used in `jam-core-linux.yml`.

**A measured false positive worth knowing about.** The obvious `nm -C … | grep
'btrack\|aubio'` matches `nm`'s own `path:` header lines, because the build
directory is named `build-btrack` / `build-aubio`:

```console
$ nm -C build-aubio/libjam-core.a build-aubio/jamTests | grep -c '…aubio…'
2                                # <-- both are header lines, not symbols
build-aubio/libjam-core.a:
build-aubio/jamTests:
```

The job therefore strips header lines first (`grep -Ev '^[^:]+:$'`) and the
correct count is **0**. Without this, the OFF boundary would appear to be broken
in exactly the configurations that are correct.

### 2.5 `JAM_CORE_BUILD_TESTS`

After this task's base commit, `jam-core` grew `option(JAM_CORE_BUILD_TESTS …)`
and now honours it for all three test targets. The tracker job passes
`-DJAM_CORE_BUILD_TESTS=ON` explicitly so its meaning does not depend on a
default. It does **not** assert that `-DJAM_CORE_BUILD_TESTS=OFF` produces no
suites: that gating did not exist at this branch's base `eac59ba`, so such an
assertion would make the job red on the commit it is committed to. That check
belongs in a follow-up once the branch is rebased on `main`.

### 2.6 Python 3 and the derived suites — a silent-disappearance guard

Current `main`'s `jam-core/CMakeLists.txt` registers
`jam.RhythmDerivedGenerator` **only** when
`find_package(Python3 COMPONENTS Interpreter QUIET)` succeeds; otherwise it
prints a `STATUS` line and registers nothing. A runner without an interpreter
would therefore lose the generator check with a green tick. Two changes remove
that hole:

- Both Linux workflows install `python3` in the toolchain step (and print its
  version), so `find_package(Python3)` succeeds on the runner.
- Step 3 requires `jam.RhythmDerivedGenerator` whenever
  `tools/rhythm-eval/tools/test_make_derived.py` exists **and** the CMake
  registration string is present, and requires `jam.RhythmDerived` whenever
  `tests/jam/RhythmDerivedTests.cpp` exists. A missing interpreter now turns the
  job red instead of dropping the suite.

Both conditionals are base-safe: at `eac59ba` neither source nor registration
exists, so neither suite is required there. Measured: forcing
`-DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON` drops `jam.RhythmDerivedGenerator`
from `ctest -N` on `main`, and the step-3 requirement then fires.

---

## 3. Assumed, not verified — check all of this first

Everything below is an **assumption**. I could not see this Gitea server's
configuration, and nothing in the repository records it. Each row says what to
assume and where to look. Confirm every row before treating any workflow as
working, and record the answers here.

| # | Assumed | Why I had to assume it | Where to check |
|---|---|---|---|
| A1 | **`ubuntu-latest` and `windows-latest` are published runner labels** | runner labels are configured server-side per runner, not in the repo | The runner's own config: `act_runner`'s `config.yml` under `[labels]`, and/or the labels shown next to the runner in the Gitea admin UI (`Site Administration → Actions → Runners`). If they differ, change only the three `runs-on:` lines. |
| A2 | **`ubuntu-latest` can install packages** — the runner is root, or has passwordless sudo | the workflows `apt-get install cmake ninja-build binutils python3` | Run `id -u` and `sudo -n true` on the runner. The toolchain step already tests `sudo -n true` (not `command -v sudo`, which passes when a password is required) and fails with an explicit message naming the four packages. `python3` is required, not incidental: without it main silently drops `jam.RhythmDerivedGenerator` (§2.6). |
| A3 | **`windows-latest` has `shell: bash`** (Git for Windows) | used for every `run:` block in the plugin job | You cannot use submodules on Windows without Git for Windows, so a runner that can run step 2 has a bash. If not, change `defaults.run.shell` to `pwsh` and rewrite the `if`/`||` guards. |
| A4 | **`windows-latest` has Visual Studio 2022 ("Visual Studio 17 2022" generator) with "Desktop development with C++"** | the root `CMakeLists.txt` needs a compiler, and `README.md`:241 / `CONTRIBUTING.md`:7 specify VS 2022 | The toolchain step asserts the generator is present and fails with an explicit message if not. Otherwise install it on the runner image. |
| A5 | **`github.server_url`, `github.repository`, `github.sha` are populated** | the checkout step clones `${CI_REPO_URL}` at `${CI_COMMIT_SHA}` | Read `env:` from the first workflow run's log. See §3.1 for the Gitea-specific review. The checkout hard-fails if it cannot land on the expected SHA, so a wrong value surfaces as a red job, never as a wrong tree. |
| A6 | **Fetching a commit by SHA is allowed** | Gitea configures `uploadpack.allowAnySHA1InWant` / `allowReachableSHA1InWant` | The checkout step already handles refusal by falling back to fetching ref tips; it fails loudly only if neither lands on the SHA. |
| A7 | **`concurrency` groups and `cancel-in-progress` are honoured** | Gitea's act_runner implements the GitHub Actions schema for these | Push twice to a branch quickly; the older run should be cancelled. Pure optimisation — no correctness depends on it. |
| A8 | **`permissions: contents: read` is accepted** | Gitea maps it to token scopes; unknown server config | Same place as A1. If the server rejects the key outright, the job will not start — remove the block and re-check. |
| A9 | **`$GITHUB_ENV`, `$GITHUB_WORKSPACE`, `${{ github.ref }}`, `if: failure()` work** | standard runner environment, implemented by act_runner | First run's log. Each failure mode is a red job at a named step, not a silent pass. |
| A10 | **Triggers `push`, `pull_request`, `workflow_dispatch` fire** | Gitea supports these | Repo → **Actions**. No branch filter is used on `push:`, so no glob support is assumed. |
| A11 | **The repository is clonable by anonymous HTTPS from the runner** | the workflows clone with **no credentials, no secrets** (a hard requirement of this task) | If the repo is **private**, plain-HTTPS clone fails. The fix needs a server-side secret and a credential-bearing clone — out of scope here, and deliberately so. Do not paste a token into a workflow file. |
| A12 | **`windows-latest` has CMake ≥ 3.22** | `CMakeLists.txt`:1 | The toolchain step asserts the version and names the requirement in its failure message. |

**Nothing above is pinned to a moving major version, because nothing is
referenced by version.** There are no `uses:` lines. When you add actions
(§6), pin them to a tag and prefer a commit SHA.

### 3.1 Checkout on Gitea vs GitHub — reviewed for CI-002

All three workflows build the repo URL the same way:

```yaml
env:
  CI_REPO_URL: ${{ github.server_url }}/${{ github.repository }}.git
  CI_COMMIT_SHA: ${{ github.sha }}
```

This is the GitHub-Actions idiom and it is what Gitea's `act_runner` exposes
(A5). Two Gitea-specific caveats, both already handled by the `run:` block:

- **Pull-request SHAs are the classic difference.** GitHub sets the PR check
  out at a synthetic merge commit; Gitea may set `github.sha` to a SHA that is
  not on any fetched ref tip. The step first tries
  `git fetch --depth 1 origin "${CI_COMMIT_SHA}"`; on refusal it falls back to
  fetching **branch tips and both pull-request refs**:
  `refs/heads/*`, `refs/pull/*/head` (Gitea's PR head) and
  `refs/pull/*/merge` (GitHub's, and Gitea's when it exposes one). It then
  `git checkout --detach "${CI_COMMIT_SHA}"` and asserts `git rev-parse HEAD`
  equals it. A wrong or unreachable SHA is therefore a red job, never a silently
  wrong tree.
  If a run still cannot reach a merge SHA (a server that exposes no
  `refs/pull/*/merge`), the correct fix is to read
  `${{ github.event.pull_request.head.sha }}` instead; that is recorded here
  rather than guessed at.
- **Glob refspecs that match nothing are tolerated. MEASURED:** a fetch with a
  wildcard refspec that matches no ref exits 0, while a *literal* missing ref
  exits 128. Adding `refs/pull/*/merge` is therefore safe on a server that does
  not publish it, and the same fallback works on `push` and `pull_request`
  events alike.
- **`github.server_url` may carry a sub-path** (e.g. a Gitea under
  `https://host/gitea`). It is used verbatim, so the clone URL stays correct.

None of this is verified against a live runner; the glob tolerance above and
the direct-SHA path in §7.4 are measured locally. The fallback's ability to
reach a specific merge SHA is not: that depends on what the server exposes, and
the exact-SHA hard-fail guarantees a red job rather than a wrong tree if it
cannot.

---

## 4. What is **not** proven

**No remote runner has run anything.** Neither this branch's workflows nor the
plugin lane has produced a server-side result. A green local run (§7) is
evidence about the *commands*, not about the *server*. Do not write "CI is
green" anywhere until a run exists in the Gitea Actions tab.

**`plugin-windows.yml` has never run, and the plugin has never been compiled ON
WINDOWS.** That is the specific gap. It is **not** true that the plugin has
never been built anywhere: on **Linux**, the Standalone and VST3 formats were
configured, built and tested without root using extracted Debian dev headers
(`docs/research/LOCAL-LINUX-BUILD.md`, and the main-branch commit that records
the app build). So:

- **Linux, measured:** `GuitarCompanion_Standalone` and `GuitarCompanion_VST3`
  were built with GCC; `GuitarCompanionTests` and the `drums.*` suites ran.
  This is not evidence about Windows.
- **Windows, unmeasured:** no Windows machine or runner has ever built this
  repository. MSVC, the Visual Studio generator, the VST3 bundle there, WASAPI/
  DirectSound and the ASIO path are all unverified on Windows.
- **Windows VST3, unmeasured:** the Linux VST3 artefact says nothing about the
  Windows VST3 build or its packaging.

**Do not present that job as ready. Expect the first Windows run to fail.**
The expected causes, in order:

1. `windows-latest` is not a label the server publishes (A1).
2. No bash on the Windows runner image (A3).
3. The submodule recipe is wrong for NAM's nested submodules (§5) — the
   topology was verified from the pinned SHAs, but the `git submodule update`
   commands have never executed on Windows.
4. The JUCE plugin genuinely does not compile under MSVC on Windows.

Cause 4 is the one that matters. It is the measurement FND-002 and the G0
plugin-lane items have been waiting for, and it must be recorded as a finding,
not worked around. **No `--warn-as-error`, no `|| exit 0`, no dropped target,
no `continue-on-error` on the build.** A CI job that cannot fail is worse than
no CI job, because it manufactures confidence that does not exist.

---

## 5. Submodules — what each lane needs, and the nesting depth

The two core lanes initialise **nothing** and assert that
`third_party/JUCE/CMakeLists.txt` was **not** materialised, so a later
"harmless" `--recursive` is caught by CI. Both trackers are vendored in-tree
(`third_party/BTrack`, `third_party/aubio`), so the tracker lane needs no
submodule either.

The **plugin lane** initialises exactly `third_party/JUCE` and
`third_party/NeuralAmpModelerCore`, never the `references/*` repositories. The
nesting was re-verified for CI-002 against the pinned SHAs:

```console
$ git ls-tree HEAD third_party/JUCE third_party/NeuralAmpModelerCore
160000 commit 91ad83ae34a81e0833b1a2b0866f54846370ae53  third_party/JUCE
160000 commit 1f42f88535884450104b8711d7595019afa0495b  third_party/NeuralAmpModelerCore

$ curl -sS https://raw.githubusercontent.com/sdatkinson/NeuralAmpModelerCore/1f42f885…/.gitmodules
[submodule "Dependencies/eigen"]
	path = Dependencies/eigen
	url = https://gitlab.com/libeigen/eigen
[submodule "Dependencies/AudioDSPTools"]
	path = Dependencies/AudioDSPTools
	url = https://github.com/sdatkinson/AudioDSPTools.git

$ curl -sS https://raw.githubusercontent.com/sdatkinson/AudioDSPTools/0827c6c2…/.gitmodules
[submodule "Dependencies/eigen"]
	path = Dependencies/eigen
	url = https://gitlab.com/libeigen/eigen

# JUCE 91ad83ae… has no .gitmodules at all (checked via the GitHub trees API).
```

So the tree is **two levels deep**:

| Path | Pinned SHA | Notes |
|---|---|---|
| `third_party/JUCE` | `91ad83ae…` | no nested submodules |
| `third_party/NeuralAmpModelerCore` | `1f42f885…` | has `.gitmodules` |
| `…/Dependencies/eigen` | `bc3b3987…` | header-only; on the include path (`CMakeLists.txt`:36-40) |
| `…/Dependencies/AudioDSPTools` | `0827c6c2…` | MIT (verified from its `LICENSE` at that SHA); itself has `.gitmodules` |
| `…/Dependencies/AudioDSPTools/Dependencies/eigen` | `6d829e76…` | fetched only by `--recursive` |
| `…/Dependencies/nlohmann` | — | vendored in-tree, not a gitlink |

A **non-recursive** init is not enough for NAM: it leaves `AudioDSPTools` empty,
and `PluginProcessor.cpp` includes `LanczosResampler.h` from there, so the
plugin cannot compile. The workflow therefore uses the scoped recursive form —
the brief's intent (never fetch the huge `references/*`) with the missing depth
added:

```sh
git submodule update --init third_party/JUCE                        # no nested submodules
git submodule update --init --recursive third_party/NeuralAmpModelerCore  # eigen + AudioDSPTools + its eigen
```

It then **asserts** that `JUCE/CMakeLists.txt`, `NAM/NAM/dsp.cpp`,
`NAM/Dependencies/eigen/Eigen/Core`,
`NAM/Dependencies/AudioDSPTools/CMakeLists.txt` **and**
`NAM/Dependencies/AudioDSPTools/Dependencies/eigen/Eigen/Core` all exist, and
that `references/airwindows` is **not materialised**. Trusting the command's
exit code alone is how a partial submodule tree produces a mysterious compile
error 200 lines later.

**Materialisation, not directory presence — measured.** A plain checkout creates
an **empty directory for every gitlink it carries**, before any submodule init:
a fresh `git clone` of this repository has empty `third_party/JUCE/` and
`third_party/NeuralAmpModelerCore/` directories with no `.git` and no contents.
At this base, `references/*` have **no** gitlink in the tree (only
`references/README.md`), so `references/airwindows` does not appear at all — but
`[ -d references/airwindows ]` would flag a placeholder if a gitlink were ever
added. The workflow therefore tests materialisation: `-e "$ref/.git"` or a
non-empty `ls -A`, which is false for a placeholder and true for a real
submodule checkout.

(The `git submodule update` commands themselves were **not** executed: the
initialised trees are large and network-bound. The topology is verified from the
pinned `.gitmodules` above; the recipe is not. This is recorded as unproven.)

---

## 6. Caching and log upload — intentionally omitted

Both need an action whose availability on this server is unverified, and
`continue-on-error` cannot rescue an unresolvable `uses:` (see §1.1).

**Log upload.** Each job's last step runs `if: failure()` and prints every
`*-configure.log`, `*-build.log` and `*-ctest.log` (the tracker job has four of
each) into the job log, capped at 2 MiB per file with the tail kept (ctest
prints the failure last). That is the failure evidence, retained in the job log
without any action. To add a real artifact once you have confirmed an action
exists, insert this **before** the final step and drop the `cat`:

```yaml
      - name: Upload logs (failure only, <= 2 MiB)
        if: failure()
        uses: <CONFIRMED-CHECKOUT-MIRROR>/upload-artifact@v4   # e.g. gitea.com/actions/upload-artifact
        with:
          name: ci-logs-${{ github.run_id }}
          path: |
            *-configure.log
            *-build.log
            *-ctest.log
          if-no-files-found: ignore
```

The 2 MiB cap is already enforced by the step above it, so the artifact cannot
be unbounded.

**Cache.** DEVPLAN §9 step 6 allows it and forbids correctness depending on it.
Nothing here depends on a cache: each job configures and builds from a cold
directory every run. To add one, once you know what exists:

```yaml
      - uses: <CONFIRMED-CHECKOUT-MIRROR>/cache@v4
        with:
          path: build                       # Windows plugin lane only
          key: plugin-${{ runner.os }}-${{ hashFiles('CMakeLists.txt', 'src/**', 'tests/**') }}
```

Key on the source hash, never on the branch, or a green run can restore a stale
build directory. Do **not** cache the Linux core lanes — the tracker lane is
four cold builds of a few dozen translation units; a cache would save little and
risk more.

**A note on `actions/checkout` specifically.** If the server turns out to
mirror the standard actions, replacing the `git` checkout with
`uses: actions/checkout@v4` plus `submodules: false` (or `recursive: false`) is
a net simplification. Do it only after confirming it resolves, and pin it.

---

## 7. Reproducing these jobs locally

Everything in this section was run on the development machine from the branch
worktree. `/tmp` is a full 7.9 GB tmpfs, so build directories live under
`/home/mojo/projects/build-CI-002/` (disk-backed) with `TMPDIR` pointed there.
`CMAKE_BUILD_PARALLEL_LEVEL=2` keeps disk and CPU use bounded.

### 7.1 `jam-core-linux.yml` — fully reproducible today

```sh
cmake -S jam-core -B build -G Ninja \
      -DJAM_CORE_BUILD_TESTS=ON -DJAM_ENABLE_BTRACK=OFF -DJAM_ENABLE_AUBIO=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure; echo "exit=$?"
```

At base `eac59ba` this is **6 suites**: `jam.AnalysisAudioRing`,
`jam.DrumTransportAdapter`, `jam.MusicalClock`, `jam.RhythmCorpus`,
`jam.RhythmEvalMetrics`, `jam.RtSignal`. On a current `main` snapshot it is
**9**: the same six plus `jam.BackendRunner`, `jam.RhythmDerived` and
`jam.RhythmDerivedGenerator` (the last is the Python generator check). The job
requires a fixed subset and adds each newer suite only when its source/registration
is present, so it is green on the base it was written against and stricter on
`main` (§2.6).

Licence-boundary check, as the job runs it (explicit nm status + non-empty symbol
output, then header-stripped case-insensitive match):

```sh
grep -q 'third_party/BTrack\|third_party/aubio' build/build.ninja && echo "LEAKED"
nm -C build/libjam-core.a build/jamTests > nm-core.log 2>&1   # must exit 0 AND
grep -Eq '^[0-9a-fA-F]+ [A-Za-z]|^ +U ' nm-core.log           # produce symbol lines
grep -Ev '^[^:]+:$' nm-core.log | grep -Eci 'btrack|aubio|kiss_fft'   # 0
```

### 7.2 `jam-core-trackers-linux.yml` — fully reproducible today

Step 3's body is self-contained; point `CI_WORKDIR` at a checkout and run it
with the runner's shell:

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
export CI_WORKDIR=<a real checkout> TMPDIR=/home/mojo/projects/build-CI-002/tmp
bash --noprofile --norc -eo pipefail extracted-step3.sh
```

Measured on the **branch base `eac59ba`** (all four configurations green):

| Config | configure | build | ctest | suites |
|---|---|---|---|---|
| `depfree` | 0 | 0 | 0 | 6 |
| `btrack` | 0 | 0 | 0 | 7 |
| `aubio` | 0 | 0 | 0 | 7 |
| `both` | 0 | 0 | 0 | 8 |

Gate evidence (base, `build.ninja` reference counts):

| Config | `third_party/BTrack` refs | `third_party/aubio` refs | `jam.BTrackBackend` | `jam.AubioBackend` |
|---|---|---|---|---|
| `depfree` | 0 | 0 | absent | absent |
| `btrack` | 11 | 0 | registered | absent |
| `aubio` | 0 | 57 | absent | registered |
| `both` | 11 | 57 | registered | registered |

In every configuration `nm -C libjam-core.a jamTests` (header lines stripped,
case-insensitive) reports **0** GPL tracker symbols. The enabled configurations
assert real linkage explicitly: `jamBTrackTests` carries `BTrack::`=33 and
`kiss_fft`=5 symbols; `jamAubioTests` carries `aubio_`=136; the adapter archives
carry `jam::BTrackBackend` / `jam::AubioBackend`. That is what makes "ON" a
measurement rather than a claim.

The same step was also run against a snapshot of `main`, which adds
`jam.BackendRunner`, `jam.RhythmDerived` and `jam.RhythmDerivedGenerator` and
honours `JAM_CORE_BUILD_TESTS`: all four configurations configured, built and
ran ctest with the larger suite set (9/10/10/11). That is what the conditional
suite requirements in step 3 are for.

### 7.3 `plugin-windows.yml` — Windows, VS 2022, no admin needed

```powershell
git clone <repo-url> GuitarCompanion
cd GuitarCompanion
git submodule update --init third_party/JUCE
git submodule update --init --recursive third_party/NeuralAmpModelerCore

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_BUILD_TYPE=Release `
      -DGUITAR_COMPANION_BUILD_TESTS=ON `
      -DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF `
      -DASIOSDK_DIR=
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The three options are documented and deliberate, not a workaround:

- `-DGUITAR_COMPANION_BUILD_TESTS=ON` adds `tests/CMakeLists.txt`
  (`CMakeLists.txt`:284–289), creating `GuitarCompanionTests` and the four
  `drums.*` ctest suites.
- `-DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF` skips the WebView2 NuGet download
  entirely (`CMakeLists.txt`:71–108): no network fetch, no ~100 MB per cold
  configure. Being precise: because that whole block is skipped, the **WebView2
  warning is never printed at all**; only the ASIO warning appears.
- `-DASIOSDK_DIR=` (empty) makes the ASIO branch unreachable, so
  `message(WARNING)` at line 233 is printed and the build proceeds with
  WASAPI/DirectSound. **That warning is expected and is not a failure**; the job
  asserts it is present, so "built without ASIO" is proven rather than hoped
  for.
- No `--warn-as-error` anywhere. CMake 4's `--warn-unerror` would turn that
  expected warning into a configure failure.

The job builds `ALL_BUILD` rather than naming `GuitarCompanion_VST3` /
`GuitarCompanion_Standalone`, because those JUCE target names are unverified
here and naming a nonexistent target is a hard failure.

**This is the unmeasured lane.** For the **Linux** equivalent, which was built
and is measured, see `docs/research/LOCAL-LINUX-BUILD.md`: Standalone + VST3
built with GCC against extracted Debian dev headers, no root. That says nothing
about MSVC, the Visual Studio generator, Windows VST3 packaging or ASIO.

### 7.4 Checkout, shell bodies and YAML

- **YAML:** `task-notes/ci-validate.py` runs PyYAML `safe_load` plus a
  structural check over every `.gitea/workflows/*.yml` (see below).
- **Shell bodies:** every `run:` body is extracted and `bash -n`-checked; the
  tracker job's step 3 is additionally executed end-to-end (§7.2).
- **Checkout:** the checkout step runs against a local `file://` remote with
  `CI_COMMIT_SHA` set to the real branch base and must end with
  `git rev-parse HEAD == CI_COMMIT_SHA`. This exercises the plain-git recipe's
  direct-SHA path (the path a normal push event takes). The exact-SHA hard-fail
  is the guarantee; the **fallback** is not locally reachable (a local remote
  allows direct SHA fetch), but its glob-refspec tolerance **is** measured: a
  wildcard refspec that matches no ref exits 0, while a literal missing ref
  exits 128 (§3.1). A Gitea run will be the first measurement of the server's
  SHA-fetch policy (A6) and of whether `refs/pull/*/merge` is exposed.
- **Failure injection:** the `nm` helper was driven with an empty archive, a
  missing file, a corrupt archive and a `/bin/true` stub, and the ON-linkage
  count with a real binary that has symbols but no tracker code; all five fire
  the guard (§7.2 and CI-002.md). Forcing `CMAKE_DISABLE_FIND_PACKAGE_Python3=ON`
  drops `jam.RhythmDerivedGenerator`, and the step-3 suite requirement fires.

### 7.5 YAML validation result

PyYAML 6.0.3, `yaml.safe_load` — note it implements YAML 1.1, in which the bare
key `on:` resolves to the boolean `True`; GitHub Actions and Gitea's act_runner
(`gopkg.in/yaml.v3`, YAML 1.2 core schema) read it as the string `"on"`. The
validator accepts either. Unquoted `on:` is what every GitHub Actions workflow
uses, so it was left alone.

`task-notes/ci-validate.py` checks, per file: parse; `permissions` is exactly
`{contents: read}`; `concurrency.cancel-in-progress is true` and the group keys
on `github.ref`; triggers present; every step has `run:` or `uses:` but not both;
**no step sets `continue-on-error`**; every `run:` is a non-empty string. All
three files pass and all **19** extracted `run:` bodies pass `bash -n`.

---

## 8. Badge and required status check

Do this **after** the server answers §3. A badge pointing at a workflow that has
never run is worse than no badge.

**Badge.** Gitea serves workflow status at
`{server}/{owner}/{repo}/actions/workflows/{file}/badge.svg`. In `README.md`,
after confirming the first run exists:

```markdown
![jam-core](https://<gitea-host>/<owner>/<repo>/actions/workflows/jam-core-linux.yml/badge.svg)
![jam-core + trackers](https://<gitea-host>/<owner>/<repo>/actions/workflows/jam-core-trackers-linux.yml/badge.svg)
![plugin (windows, unverified)](https://<gitea-host>/<owner>/<repo>/actions/workflows/plugin-windows.yml/badge.svg)
```

**Required check.** Repository → **Settings → Branches → Branch protection** →
add the job's check name under *Status checks*. The names are exactly:

| Workflow | Required-check name |
|---|---|
| `jam-core-linux.yml` | `jam-core / cmake+ninja / ctest` |
| `jam-core-trackers-linux.yml` | `jam-core / trackers / ctest` |
| `plugin-windows.yml` | `GuitarCompanion Standalone+VST3 / Release / ctest` |

These come from the `name:` fields, which is why they are explicit.

**Before making `plugin-windows` required**, read §4 again. It has never run.
Making an unverified job a merge gate risks blocking every merge on a runner
label that may not exist. Recommended order: run all three, fix whatever the
first runs surface, *then* require `jam-core-linux` and
`jam-core-trackers-linux`, then decide about the plugin lane once it has a real
green run behind it.

---

## 9. Three safety properties worth keeping

All are deliberate, and all are easy to "fix" away by accident.

**`nm` exits 0 with no symbols on an empty archive or a stripped binary.**
Measured: `nm` on an empty `ar` archive exits 0 with empty output; `nm -C
/bin/true` exits 0 and prints `no symbols`; only a corrupt archive or a missing
file exits non-zero. An `nm … | grep … || true` pipeline therefore false-greens
a corrupt or stub object. Both jobs run `nm` to a file, require a zero exit
**and** at least one symbol line, and only then grep — on the core archive/binary
and on every enabled tracker binary/archive (§2.4).

**`ctest` exits 0 when no tests are found.** Measured:

```console
$ ctest --test-dir /tmp/opencode/ci-empty --output-on-failure
Test project /tmp/opencode/ci-empty
No tests were found!!!
$ echo $?
0
```

A silently empty run is a silent **pass**. Every job therefore parses
`ctest -N` for `Total Tests: N` and hard-fails if it is absent or `< 1`, before
running anything. The tracker job additionally asserts the parsed suite names
equal the count (a parser that silently drops a line must not look green), and
asserts a fixed set of `jam.*` suite names is registered, so a suite deleted
from a source file turns the job red instead of quietly reducing coverage. Do
not add `--no-tests=error` as a substitute — the count assertion is what catches
a broken configure, which happens before ctest exists.

**The test runner's non-zero-on-empty-filter is load-bearing.** Both
`tests/jam/JamTestMain.cpp` and `tests/TestMain.cpp` exit non-zero when a filter
matches zero tests, which is why a renamed case can never pass silently. No job
may wrap a `ctest` call in `|| true`, and no `continue-on-error` may appear on a
build or test step. The suite-count assertions above are added *on top of* that
behaviour, not as a replacement for it.

**The tracker job is honest only if it can fail.** It can: configure, build,
suite registration, the core licence boundary, the `nm`-status/non-empty check,
the enabled-tracker linkage assertions, the disabled-tracker-absent check, the
dependency-free build-graph check and ctest are all independent failure points,
each printing a named `::error::` line. There is no `continue-on-error`, no
`|| true` around a build or test, and no dropped configuration. The failure
injections in §7.4 exercise each guard.
