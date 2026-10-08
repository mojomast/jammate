# NAM-ARCHITECTURE-PROBE — bounded processor coverage beyond lstm.nam

**Task:** RT-004 (bounded NAM architecture processor coverage)
**Worktree:** `/home/mojo/projects/worktrees/RT-004-nam-architectures`, branch `wp/RT-004-nam-architectures`, base `bcf540a`.
**Task note:** `task-notes/RT-004.md`.
**Tools (new):** `tools/nam-architecture-probe/` (runner, validator/summariser, predeclaration, tests).
**Artifacts (new):** `docs/research/nam-architecture-probe/` (per-run logs/CSV/findings, summary, model identity, source/archive pins, manifest).

This document reports **executed** measurements of the real
`GuitarCompanionProcessor::processBlock` for five predeclared NAM model types.
It reuses the existing, read-only probe binaries. No processor, editor,
DrumEngine, NAM or probe source was changed, and no architecture was repaired.

## Scope and contract

- Predeclared set: five distinct architecture/config types (see below). The
  `lstm.nam` control is the RT-002/RT-003 example.
- Matrix per model (per variant): one clean process; 18 dry built-in cases
  (existing probe, unchanged) plus 8 NAM cases = 2 rates (48000, 96000) x
  2 blocks (128, 512) x {nam, nam+drums}, each with cold and warm (128 warm
  blocks) measurement.
- Two variants: **repaired** (current processor: `libnam_core.a` with the
  RT-003 patch, dbd11fb2...) is the subject. **original_control** (pinned
  upstream `libnam_core_original.a`, af023ffc...) is a labelled control only.
- Out of scope, not claimed: ASIO/device/Windows, real-time deadlines, hosted
  VST3, whole-program allocation safety, per-sample output finiteness (the
  probe only reports mean warm `out_rms`).

## Predeclared model set

Identities were read from the pinned NAM checkout (`1f42f885...`). All files
were read in place; weights were not copied or shipped. The `models/*.json`
files hold architecture/config summaries only.

| id | file | sha256 (prefix) | architecture / version | role |
|---|---|---|---|---|
| lstm_control | lstm.nam | df9f78c4 | LSTM / 0.5.4 | current control |
| a1_wavenet_standard | wavenet_a1_standard.nam | ceb53469 | WaveNet / 0.5.0 | A1 WaveNet, 10 dilations, 16/8 channels |
| a2_wavenet_max | wavenet_a2_max.nam | 12384c66 | WaveNet / 0.6.0 | A2-style WaveNet with condition_dsp, bottleneck, Softsign |
| slimmable_wavenet | slimmable_wavenet.nam | 735c1a86 | WaveNet / 0.7.0 | WaveNet with slimmable allowed_channels 1/2/3 |
| a2_slimmable_container | A2.nam | 2d2d7445 | SlimmableContainer / 0.7.0 | container of two WaveNet submodels |

Not in the predeclared set (5-type cap, not measured):
`slimmable_container.nam` (LSTM + WaveNet container), `wavenet_condition_dsp.nam`,
`wavenet.nam`. `my_model.nam` is byte-identical to `wavenet_a1_standard.nam`
(same sha256) and is not a distinct type.

Pinned NAM core accepts versions 0.5.0 through 0.7.0 (`NAM/get_dsp.h`), so all
five are in range. The processor never calls `SetSlimmableSize`. The
`SlimmableContainer` therefore runs its **default** submodel, the last one
(index 1: WaveNet, 8 channels, 23 dilations, max_value 1.0). The other
submodel (max_value 0.5) was not measured.

## Method

- `tools/nam-architecture-probe/run_nam_arch.sh` runs the existing probe binary
  with `--warm-blocks 128 --csv --json --nam-model <file>`, under `env -i`,
  `timeout 600`, one independent process per (variant, model). The probe binary
  is not rebuilt; the repaired binary is
  `/home/mojo/projects/build-RT-003-integration/product-probe/build/processor_probe`
  (sha f950d6d9...). Its source pin (`source-pin.txt`) matches the current
  processor sources, which are byte-identical to the pinned `677ce9f`.
- Control binary: `/home/mojo/projects/build-RT-003-integration/probe-baseline/build/processor_probe`
  (sha de089230...), linked against `libnam_core_original.a`.
- `tools/nam-architecture-probe/summarize_nam_arch.py` validates every run
  fail-closed and writes `summary.json`, `summary.md`, `models/*.json`,
  `source-pin.txt` and `manifest.sha256`.

Validation checks per run (any failure is `failed`):

- Exit 0; exit-status binary, variant and model sha match the predeclaration.
- Model file present with the predeclared sha; `[nam] requesting async load:`
  equals the expected path exactly, so the model identity is witnessed in the log.
- Self-check PASS line present; `findings.json` self-check/args/scene flags
  true; `dry_cases` = 18.
- Load witness: the post-dry line `loaded=1 ... error=""` plus the in-dry
  activation line. `loaded=1` with an error, or with no activation witness,
  fails. `loaded=0` requires the explicit `UNMEASURED` marker and zero NAM rows.
- CSV header equals the probe contract (45 columns). Exact dry (18) and NAM (8)
  case matrices, no duplicates, `warm_blocks` = 128, `drums` flag consistent
  with the tag, duration consistent with warm x block / rate, all counts
  non-negative integers, all floats finite.
- `findings.json` `nam_cases_with_alloc` equals the CSV count.

Status (never relabelled):

- **measured-clean**: loaded, activation witnessed, and every dry and NAM
  heap alloc/free/lock/trylock/cond/unlock count is zero. No-op `free(NULL)` is
  reported separately.
- **measured-findings**: as above, with at least one positive count. The count
  is kept as a finding; it does not fail the run or change the matrix.
- **unmeasured**: the model did not load or activate. Only dry rows, not clean.
- **failed**: any validation error.

## Results

Summary: 10 runs, `coverage_complete=true`, **7 measured-clean, 3
measured-findings, 0 unmeasured, 0 failed**. Every model loaded and activated
within the bounded dry run (`resampling` is recorded per run). All dry rows were
zero in every run.

| variant | architecture | model | status | NAM rows | warm alloc total | warm alloc/host-sample (min-max) | warm free | lock ops | noop frees | out RMS (min-max) |
|---|---|---|---|---|---|---|---|---|---|---|
| repaired | lstm_control | lstm.nam | measured-clean | 8 | 0 | 0-0 | 0 | 0 | 1677 | 0.0085-0.1892 |
| repaired | a1_wavenet_standard | wavenet_a1_standard.nam | measured-clean | 8 | 0 | 0-0 | 0 | 0 | 1677 | 0.1909-0.2866 |
| repaired | a2_wavenet_max | wavenet_a2_max.nam | **measured-findings** | 8 | 491520 | 1.0-2.0 | 491520 | 0 | 1677 | 9.1751-9.1813 |
| repaired | slimmable_wavenet | slimmable_wavenet.nam | measured-clean | 8 | 0 | 0-0 | 0 | 0 | 1677 | 8.6990-9.0228 |
| repaired | a2_slimmable_container | A2.nam | measured-clean | 8 | 0 | 0-0 | 0 | 0 | 1677 | 0.0207-0.1915 |
| original_control | lstm_control | lstm.nam | **measured-findings** | 8 | 491520 | 1.0-2.0 | 491520 | 0 | 1677 | 0.0085-0.1892 |
| original_control | a1_wavenet_standard | wavenet_a1_standard.nam | measured-clean | 8 | 0 | 0-0 | 0 | 0 | 1677 | 0.1909-0.2866 |
| original_control | a2_wavenet_max | wavenet_a2_max.nam | **measured-findings** | 8 | 491520 | 1.0-2.0 | 491520 | 0 | 1677 | 9.1751-9.1813 |
| original_control | slimmable_wavenet | slimmable_wavenet.nam | measured-clean | 8 | 0 | 0-0 | 0 | 0 | 1677 | 8.6990-9.0228 |
| original_control | a2_slimmable_container | A2.nam | measured-clean | 8 | 0 | 0-0 | 0 | 0 | 1677 | 0.0207-0.1915 |

Reading the table:

1. **Repaired LSTM is zero** (`lstm.nam`), matching the RT-003 repair. The
   original control reproduces RT-002's 2 allocations per host sample at 48 kHz.
2. **The repair is LSTM-specific.** `a2_wavenet_max` is positive in both the
   repaired and original archives, with identical counts (491520 warm
   allocs = 2.0 per sample at 48 kHz, 1.0 at resampled 96 kHz). The RT-003
   repair does not cover this architecture.
3. **Call sites** (repaired, `addr2line` resolves function names only; the
   binary has no debug line info, so locations are `??`):
   `nam::activations::ActivationPReLU::apply(Eigen::Matrix<float,...>&)` and
   `void nam::gating_activations::BlendingActivation::apply<...>`. These are
   upstream NAM activation code paths, not the LSTM sites. Not repaired here.
4. **A1 WaveNet standard, slimmable WaveNet and the A2 container are zero** in
   the warm and NAM-measured cases. The A2 container result covers only the
   default submodel (see above).
5. **Cold path.** The repaired `a2_wavenet_max` cold allocation total is 3840
   (non-zero). The original control shows the same 3840 for the LSTM and the
   WaveNet-max cases. Cold clean status covers only the CSV-visible categories
   (`new`, `new[]`, `malloc`, `calloc`, `realloc`, and their frees). Nothrow and
   aligned `new` are not separate CSV columns, so cold allocations in those
   categories would not be visible. Warm totals include them.
6. **noop frees** (`free(NULL)`, counted separately from heap frees) are 1677
   in every run, the same drum-path no-op RT-002 described. They are not heap
   operations and do not affect status.
7. **Output RMS** for `slimmable_wavenet` and `a2_wavenet_max` is about 9, far
   above the other models (0.01–0.29). It is finite, which is all the probe
   checks, and is recorded only as measured. It is not a correctness or gain
   judgement. Per-sample output finiteness is **not** checked (`per_sample_output_checked=false`).
8. Drum rendering was active in every NAM+drums case (`drum_active_blocks` =
   128 per such case, voice events 2–10), so the drums rows are not idle.

## Limitations

- **Activation is not isolated.** Every model became active during the dry run
  (`activation_witnessed_in_dry_run=true`), so NAM cold rows are the first
  `processBlock` after `prepareToPlay`, not the activation swap. The probe is not
  edited here (`tools/processor-probe/**` is read-only).
- **Five types, not the full NAM set.** Four example models were excluded by
  the predeclared 5-type cap. Only the example files were measured, not
  production captures.
- **Warm budget is 128 blocks** (not 256). This keeps the bounded run cheap.
  RT-002 used 256, so the two are not directly comparable in wall time.
- **Non-device, single-threaded** `processBlock` calls on one probe thread. Wall
  times are instrumented and are not a latency or real-time deadline gate.
- **Symbol resolution** is by function name only (no debug line info).
- **Probe binaries are reused, not rebuilt**, so probe-build behaviour is taken
  as recorded in the RT-003 integration pins.
- No Windows, ASIO, device, hosted-plugin or whole-program safety claim is made.

## Reproduce

```sh
cd /home/mojo/projects/worktrees/RT-004-nam-architectures
export PATH=/tmp/opencode/venv/bin:$PATH
export TMPDIR=/home/mojo/projects/guitars-build-resume/tmp
tools/nam-architecture-probe/run_nam_arch.sh          # 10 clean processes (~9 s)
python3 tools/nam-architecture-probe/summarize_nam_arch.py   # validate + write summary
python3 -m unittest -v tools/nam-architecture-probe/test_summarize_nam_arch.py
```

Exit status from the summariser: 0 when every run is measured (clean or
findings); 3 when any run is unmeasured; 4 when any run failed.

## Artifacts

| path | content |
|---|---|
| `docs/research/nam-architecture-probe/runs/<variant>/<id>/` | `probe.log`, `cases.csv`, `findings.json`, `exit-status.txt` (binary/model sha, exit) |
| `docs/research/nam-architecture-probe/summary.json` | machine-readable per-run status, totals, per-row counts, call sites |
| `docs/research/nam-architecture-probe/summary.md` | results table above |
| `docs/research/nam-architecture-probe/models/<id>.json` | weight-free model identity (architecture, version, config summary) |
| `docs/research/nam-architecture-probe/source-pin.txt` | processor source hashes and archive identities |
| `docs/research/nam-architecture-probe/manifest.sha256` | sha256 of every artifact above |

Tests: `test_summarize_nam_arch.py` runs 36 cases (10 evidence and 26
fail-closed fixture checks). They re-read the raw CSVs, verify model files and
manifest hashes, and check exact expected counts.
