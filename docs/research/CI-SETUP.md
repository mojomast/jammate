# CI-SETUP — Gitea Actions configuration, assumptions, and what is not proven

**Task:** CI-001 — CI baseline
**Branch:** `wp/CI-001` · **Base commit:** `a8da4f2`
**Written:** 2026-10-07

This repository is hosted on **Gitea**. The workflows are in
`.gitea/workflows/`, not `.github/workflows/`, per ADR-0003 decision 6 and
EXECUTION-LEDGER deviation **D3**. Nothing under `.github/` exists and nothing
should create it: this forge does not read it, so a `.github/workflows/` file
would be a pipeline that looks real and never runs.

Read **§3** before the first run. It is the part that can waste an afternoon.

---

## 1. What is in `.gitea/workflows/`

| File | Lane | Provable from the dev machine? |
|---|---|---|
| `jam-core-linux.yml` | **core** — `jam-core`, CMake + Ninja, ctest | **Yes, fully** (see `task-notes/CI-001.md`) |
| `jam-core-btrack-linux.yml` | **core** + the vendored GPL BTrack backend | **Partly** — see §2 |
| `plugin-windows.yml` | **plugin** — JUCE `GuitarCompanion` (Standalone + VST3) + `GuitarCompanionTests` | **No. Never compiled anywhere, ever.** See §4 |

The lanes are ADR-0003's. They are not interchangeable: `jam-core` is
platform-neutral (no JUCE, no audio device, no audio headers); the plugin
target needs Windows and has never been built.

### 1.1 Why none of these files uses `uses:`

**The three workflows reference zero actions.** Checkout is a plain
`git init` + depth-1 `git fetch` + `git checkout --detach` in a `run:` block.

This is the single largest source of avoidable risk on a server whose
configuration is invisible from here. If `actions/checkout@v4` does not resolve
— Gitea does not fetch marketplace actions from `github.com`; it needs either a
server-side mirror or a `gitea.com/actions/...` reference — the job fails
during job preparation, **before the first step runs**, and `continue-on-error`
on a later step does not save it. There is no version of "swap in
`actions/checkout` later" that is cheaper than "just use git".

The same reasoning removes `actions/cache` and `actions/upload-artifact`. See
§6 for exactly where to put them back.

Consequence, stated plainly: these files are longer than they would be with
`uses:`, and they are longer on purpose.

---

## 2. The BTrack job does **not** build BTrack, and says so on every run

The contract for `jam-core-btrack-linux.yml` was "the same, plus
`-DJAM_ENABLE_BTRACK=ON`, which compiles the vendored GPL BTrack backend and
runs the `jam.BTrackBackend` suite". **That is not currently possible.** Measured
on this branch's base commit:

```console
$ cmake -S jam-core -B build-btrack -G Ninja -DJAM_ENABLE_BTRACK=ON
...
CMake Error at third_party/BTrack/CMakeLists.txt:74 (message):
  JAM_ENABLE_BTRACK is ON but no sources exist in src/btrack/.  The adapter
  has not been written yet.

-- Configuring incomplete, errors occurred!
$ echo $?
1
```

`src/btrack/` does not exist, so the TRACK-001 adapter is unwritten; and
`tests/jam/BTrackBackendTests.cpp` does not exist either, so even with the
adapter in place, `jam-core/CMakeLists.txt` lines 74–85 would take the
`message(STATUS "…no BTrackBackendTests.cpp yet")` branch and register **no**
`jam.BTrackBackend` suite.

A workflow that ran the BTrack build and went green would be lying. One that
ran it unconditionally would be permanently red for a reason unrelated to any
regression. So the job does the only honest thing available, and each of its
three assertions can fail:

1. **Build and run the whole core suite for real**, `JAM_ENABLE_BTRACK=OFF`.
2. **Licence boundary.** The default build must contain no BTrack or `kiss_fft`
   objects — checked twice, once over the Ninja build graph and once with `nm`
   over `libjam-core.a` and `jamTests`. "src/jam/ is free of third-party code"
   is a review-time rule in `jam-core/CMakeLists.txt`; this makes it a
   machine-checked one.
3. **The BTrack lane's current state.** `-DJAM_ENABLE_BTRACK=ON` must still
   fail, with the specific `JAM_ENABLE_BTRACK is ON but …` message from
   `third_party/BTrack/CMakeLists.txt` (rather than failing for some unrelated
   reason), and `src/btrack/` and `tests/jam/BTrackBackendTests.cpp` must still
   be absent.

**Step 3 is an assertion, not a suppression, and it is designed to go red.**
The day TRACK-001 lands and the BTrack build works, this step fails and its
error message prints the replacement commands:

```sh
cmake -S jam-core -B build-btrack -G Ninja -DJAM_ENABLE_BTRACK=ON
cmake --build build-btrack --parallel
ctest --test-dir build-btrack --output-on-failure
```

plus the instruction to assert `jam.BTrackBackend` is registered. Until someone
does that work, the GPL backend is **not** covered by CI, and the job's green
tick means "still not built", not "BTrack verified".

---

## 3. Assumed, not verified — check all of this first

Everything below is an **assumption**. I could not see this Gitea server's
configuration, and nothing in the repository records it. Each row says what to
assume and where to look. Confirm every row before treating any workflow as
working, and record the answers here.

| # | Assumed | Why I had to assume it | Where to check |
|---|---|---|---|
| A1 | **`ubuntu-latest` and `windows-latest` are published runner labels** | runner labels are configured server-side per runner, not in the repo | The runner's own config: `act_runner`'s `config.yml` under `[labels]`, and/or the labels shown next to the runner in the Gitea admin UI (`Site Administration → Actions → Runners`). If they differ, change only the three `runs-on:` lines. |
| A2 | **`ubuntu-latest` can install packages** — the runner is root, or has passwordless sudo | the workflows `apt-get install cmake ninja-build binutils` | Run `id -u` and `sudo -n true` on the runner. The toolchain step already tests `sudo -n true` (not `command -v sudo`, which passes when a password is required) and fails with an explicit message naming the three packages. |
| A3 | **`windows-latest` has `shell: bash`** (Git for Windows) | used for every `run:` block in the plugin job | You cannot use submodules on Windows without Git for Windows, so a runner that can run step 2 has a bash. If not, change `defaults.run.shell` to `pwsh` and rewrite the `if`/`||` guards. |
| A4 | **`windows-latest` has Visual Studio 2022 ("Visual Studio 17 2022" generator) with "Desktop development with C++"** | the root `CMakeLists.txt` needs a compiler, and `README.md`:241 / `CONTRIBUTING.md`:7 specify VS 2022 | The toolchain step asserts the generator is present and fails with an explicit message if not. Otherwise install it on the runner image. |
| A5 | **`github.server_url`, `github.repository`, `github.sha` are populated** | the checkout step clones `${CI_REPO_URL}` at `${CI_COMMIT_SHA}` | Read `env:` from the first workflow run's log. The checkout hard-fails if it cannot land on the expected SHA, so a wrong value surfaces as a red job, never as a wrong tree. If `github.sha` is an unreachable merge ref on PR events, change `CI_COMMIT_SHA` to `${{ github.event.pull_request.head.sha }}` — the commented fallback in each checkout step already fetches ref tips. |
| A6 | **Fetching a commit by SHA is allowed** | Gitea configures `uploadpack.allowAnySHA1InWant` / `allowReachableSHA1InWant` | The checkout step already handles refusal by falling back to fetching `refs/heads/*` and `refs/pull/*/head`; it fails loudly only if neither lands on the SHA. |
| A7 | **`concurrency` groups and `cancel-in-progress` are honoured** | Gitea's act_runner implements the GitHub Actions schema for these | Push twice to a branch quickly; the older run should be cancelled. Pure optimisation — no correctness depends on it. |
| A8 | **`permissions: contents: read` is accepted** | Gitea maps it to token scopes; unknown server config | Same place as A1. If the server rejects the key outright, the job will not start — remove the block and re-check. |
| A9 | **`$GITHUB_ENV`, `$GITHUB_WORKSPACE`, `${{ github.ref }}`, `if: failure()` work** | standard runner environment, implemented by act_runner | First run's log. Each failure mode is a red job at a named step, not a silent pass. |
| A10 | **Triggers `push`, `pull_request`, `workflow_dispatch` fire** | Gitea supports these | Repo → **Actions**. No branch filter is used on `push:`, so no glob support is assumed. |
| A11 | **The repository is clonable by anonymous HTTPS from the runner** | the workflows clone with **no credentials, no secrets** (a hard requirement of this task) | If the repo is **private**, plain-HTTPS clone fails. The fix needs a server-side secret and a credential-bearing clone — out of scope here, and deliberately so. Do not paste a token into a workflow file. |
| A12 | **`windows-latest` has CMake ≥ 3.22** | `CMakeLists.txt`:1 | The toolchain step asserts the version and names the requirement in its failure message. |

**Nothing above is pinned to a moving major version, because nothing is
referenced by version.** There are no `uses:` lines. When you add actions
(§6), pin them to a tag and prefer a commit SHA.

---

## 4. What is **not** proven

**`plugin-windows.yml` has never run. The `GuitarCompanion` target has never
been compiled, anywhere, in this project's entire history.**

- Not on the development machine: ADR-0003 measured the ALSA and freetype
  headers absent and `sudo` unavailable, so JUCE cannot configure there.
- Not on Windows: no Windows machine or runner has ever built this repository.
- `GuitarCompanionTests` is equally unbuilt — `tests/CMakeLists.txt` links
  `juce::juce_audio_formats` and `juce::juce_audio_processors`, blocked for the
  same reason (ADR-0003).

**Do not present that job as ready. Expect the first run to fail.** The
expected causes, in order:

1. `windows-latest` is not a label the server publishes (A1).
2. No bash on the Windows runner image (A3).
3. The submodule recipe is incomplete for NAM's nested submodules (§5) — it was
   derived from the pinned SHAs' `.gitmodules`, never executed.
4. The JUCE plugin genuinely does not compile.

Cause 4 is the one that matters. It is the measurement FND-002 and the G0
plugin-lane items have been waiting for, and it must be recorded as a finding,
not worked around. **No `--warn-as-error`, no `|| exit 0`, no dropped target,
no `continue-on-error` on the build.** A CI job that cannot fail is worse than
no CI job, because it manufactures confidence that does not exist.

**First-run expectation, explicitly:** the run's outcome is information about
the plugin lane, whatever it is. Green means the target compiled and 4 `drums.*`
suites passed on Windows for the first time in this project's history. Red
means the same thing with a log attached.

---

## 5. Submodules — what each lane needs, and one correction to the brief

The brief said to initialise only `third_party/JUCE` and
`third_party/NeuralAmpModelerCore`, explicitly not `--recursive`. **The
non-recursive form is not enough for NAM.** Verified over the network against
the pinned SHAs:

```console
$ curl -sS https://raw.githubusercontent.com/sdatkinson/NeuralAmpModelerCore/1f42f88535884450104b8711d7595019afa0495b/.gitmodules
[submodule "Dependencies/eigen"]
	path = Dependencies/eigen
	url = https://gitlab.com/libeigen/eigen
[submodule "Dependencies/AudioDSPTools"]
	path = Dependencies/AudioDSPTools
	url = https://github.com/sdatkinson/AudioDSPTools.git

$ curl -sS https://api.github.com/repos/juce-framework/JUCE/git/trees/91ad83ae34a81e0833b1a2b0866f54846370ae53 \
    | python3 -c "import json,sys; print(any(e['path']=='.gitmodules' for e in json.load(sys.stdin)['tree']))"
False

$ curl -sS https://api.github.com/repos/sdatkinson/NeuralAmpModelerCore/git/trees/daf9edcdb7ce \
    | python3 -c "import json,sys; [print(e['type'], e['path']) for e in json.load(sys.stdin)['tree']]"
commit   AudioDSPTools
commit   eigen
blob     info.txt
tree     nlohmann
```

So: JUCE has **no** nested submodules; NAM has **two**, and root
`CMakeLists.txt`:36–40 puts `Dependencies/eigen` on the include path.
`Dependencies/nlohmann` is vendored as plain files, so it needs nothing.

The workflow therefore uses a **scoped** recursive init — the brief's intent
(hugely: never fetch `references/*`) with the missing depth added:

```sh
git submodule update --init third_party/JUCE                    # no nested submodules
git submodule update --init --recursive third_party/NeuralAmpModelerCore  # eigen + AudioDSPTools
```

It then **asserts** that `third_party/JUCE/CMakeLists.txt`,
`third_party/NeuralAmpModelerCore/NAM/dsp.cpp` and
`third_party/NeuralAmpModelerCore/Dependencies/eigen/Eigen/Core` all exist, and
that `references/airwindows` does not. Trusting the command's exit code alone
is how a partial submodule tree produces a mysterious compile error 200 lines
later.

The two core-lane workflows initialise **nothing**, and assert that
`third_party/JUCE/CMakeLists.txt` was *not* materialised, so a later
"harmless" `--recursive` is caught by CI.

---

## 6. Caching and log upload — intentionally omitted

Both need an action whose availability on this server is unverified, and
`continue-on-error` cannot rescue an unresolvable `uses:` (see §1.1).

**Log upload.** Each job's last step runs `if: failure()` and prints the
`ctest.log` / `configure.log` / `build.log` into the job log, capped at 2 MiB
per file with the tail kept (ctest prints the failure last). That satisfies
"only on failure, and only if it is small" without any action. To add a real
artifact once you have confirmed an action exists, insert this **before** the
final step and drop the `cat`:

```yaml
      - name: Upload logs (failure only, <= 2 MiB)
        if: failure()
        uses: <CONFIRMED-CHECKOUT-MIRROR>/upload-artifact@v4   # e.g. gitea.com/actions/upload-artifact
        with:
          name: ci-logs-${{ github.run_id }}
          path: ctest.log
          if-no-files-found: ignore
```

The 2 MiB cap is already enforced by the step above it, so the artifact cannot
be unbounded.

**Cache.** DEVPLAN §9 step 6 allows it and forbids correctness depending on
it. Nothing here depends on a cache: each job configures and builds from a cold
directory every run. To add one, once you know what exists:

```yaml
      - uses: <CONFIRMED-CHECKOUT-MIRROR>/cache@v4
        with:
          path: build                       # Windows plugin lane only
          key: plugin-${{ runner.os }}-${{ hashFiles('CMakeLists.txt', 'src/**', 'tests/**') }}
```

Key on the source hash, never on the branch, or a green run can restore a stale
build directory. Do **not** cache the Linux core lane — it is 12 translation
units and finished in ~2 s locally; a cache would be slower than the build.

**A note on `actions/checkout` specifically.** If the server turns out to
mirror the standard actions, replacing the `git` checkout with
`uses: actions/checkout@v4` plus `submodules: false` (or `recursive: false`) is
a net simplification. Do it only after confirming it resolves, and pin it.

---

## 7. Reproducing these jobs locally

### 7.1 `jam-core-linux.yml` — fully reproducible today

```sh
cmake -S jam-core -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure; echo "exit=$?"
```

Expected on the base commit: `100% tests passed out of 5` — `jam.AnalysisAudioRing`,
`jam.DrumTransportAdapter`, `jam.MusicalClock`, `jam.RhythmCorpus`, `jam.RtSignal`.
Use a directory outside the worktree if you prefer; the workflow uses
`$GITHUB_WORKSPACE/guitar-companion-ci/build`, which is equivalent.

Licence-boundary check, as the workflow runs it:

```sh
grep -q 'third_party/BTrack' build/build.ninja && echo "LEAKED"
nm -C build/libjam-core.a build/jamTests | grep -c 'BTrack\|btrack::\|kiss_fft'   # 0
```

### 7.2 `jam-core-btrack-linux.yml`

Same three commands for the default configuration, then the BTrack state
assertion:

```sh
cmake -S jam-core -B build-btrack -G Ninja -DJAM_ENABLE_BTRACK=ON; echo "exit=$?"  # 1, today
```

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

**Why those three options, and why they are not a workaround:**

- `-DGUITAR_COMPANION_BUILD_TESTS=ON` adds `tests/CMakeLists.txt`
  (`CMakeLists.txt`:284–289), creating `GuitarCompanionTests` and the four
  `drums.*` ctest suites.
- `-DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF` skips the WebView2 NuGet download
  entirely (`CMakeLists.txt`:71–108) — no network fetch, no ~100 MB per cold
  configure. This is exactly what `CONTRIBUTING.md`:19–21 prescribes for an
  offline build. Being precise: because that whole block is skipped, the
  **WebView2 warning is never printed at all**; it lives inside the skipped
  block. Only the ASIO warning appears. The embedded TONE3000 picker is not
  built and the store falls back to the system browser.
- `-DASIOSDK_DIR=` (empty) makes the ASIO branch at `CMakeLists.txt`:228
  unreachable, so `message(WARNING)` at line 233 is printed and the build
  proceeds with WASAPI/DirectSound. **That warning is expected and is not a
  failure.** It is set explicitly-empty rather than unset so a stale cache value
  cannot switch ASIO on by accident, and the plugin job *asserts the warning is
  present*, so "built without ASIO" is proven rather than hoped for.
- No `--warn-as-error` anywhere. CMake 4's `--warn-unerror` would turn that
  expected warning into a configure failure.
- No TONE3000 key, no store account, no credentials of any kind.

The job builds `ALL_BUILD` rather than naming targets. The JUCE target names
(`GuitarCompanion_VST3`, `GuitarCompanion_Standalone`) are the conventional JUCE
8 names but **could not be verified** — the JUCE submodule is not checked out —
and naming a nonexistent target is a hard failure. `ALL_BUILD` also compiles
`jam-core`/`jamTests`, so the core lane's suites run on Windows too. Once a
Windows run succeeds, its log will show the real target names and the build can
be narrowed.

---

## 8. Badge and required status check

Do this **after** the server answers §3. A badge pointing at a workflow that has
never run is worse than no badge.

**Badge.** Gitea serves workflow status at
`{server}/{owner}/{repo}/actions/workflows/{file}/badge.svg`. In `README.md`,
after confirming the first run exists:

```markdown
![jam-core](https://<gitea-host>/<owner>/<repo>/actions/workflows/jam-core-linux.yml/badge.svg)
![plugin (windows, unverified)](https://<gitea-host>/<owner>/<repo>/actions/workflows/plugin-windows.yml/badge.svg)
```

Link the badge to the Actions page. Only add it once the run you are advertising
has actually happened.

**Required check.** Repository → **Settings → Branches → Branch protection** →
add the job's check name under *Status checks*. The names are exactly:

| Workflow | Required-check name |
|---|---|
| `jam-core-linux.yml` | `jam-core / cmake+ninja / ctest` |
| `jam-core-btrack-linux.yml` | `jam-core / +BTrack / ctest` |
| `plugin-windows.yml` | `GuitarCompanion Standalone+VST3 / Release / ctest` |

These come from the `name:` fields, which is why they are explicit.

**Before making `plugin-windows` required**, read §4 again. It has never run.
Making an unverified job a merge gate risks blocking every merge on a runner
label that may not exist. Recommended order: run all three, fix whatever the
first runs surface, *then* require `jam-core-linux` only, then decide about the
plugin lane once it has a real green run behind it.

---

## 9. Two safety properties worth keeping

Both are deliberate, and both are easy to "fix" away by accident.

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
running anything. The core jobs additionally assert each of the five `jam.*`
suite names is registered, so a suite deleted from a source file turns the job
red instead of quietly reducing coverage. Do not add `--no-tests=error` as a
substitute — the count assertion is what catches a broken configure, which
happens before ctest exists.

**The test runner's non-zero-on-empty-filter is load-bearing.** Both
`tests/jam/JamTestMain.cpp` and `tests/TestMain.cpp` exit non-zero when a filter
matches zero tests, which is why a renamed case can never pass silently. No job
may wrap a `ctest` call in `|| true`, and no `continue-on-error` may appear on a
build or test step. The suite-count assertions above are added *on top of* that
behaviour, not as a replacement for it.