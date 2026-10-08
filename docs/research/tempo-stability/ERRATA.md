# TRACK-008 errata and correction contract

Committed **before** the corrected derived evidence is recomputed. This file and
`docs/research/tempo-stability/evidence-corrected/` are **additive**. The
following are **immutable** and are not edited, re-rendered or overwritten:

- `docs/research/tempo-stability/PROTOCOL.md` (preregistered protocol, `0c1f972`);
- `docs/research/tempo-stability/FREEZE.json` and the behaviour freeze `8a0c637`;
- the entire historical `docs/research/tempo-stability/evidence/**` tree (raw
  scorer JSON, diagnostic beat CSVs, the four gzipped raw method logs, the
  derived JSONs and `artifact-hashes.txt`);
- both corpora and the frozen generators.

No scorer or evaluator binary is re-run. No scored metric, outcome, comparison,
control pair, interval, coverage or beat-equality field changes.

## Errata

### E1 — `FREEZE.json` `evaluation` block is a historical pre-run snapshot

`FREEZE.json.evaluation` / `evaluationCombinedSha256` hash the **evaluation
tooling as it stood when the behaviour freeze was committed** (`8a0c637`),
*before* the three tooling-only fixes in E3. They are **not** an authentication
of the current retained tooling. The **behaviour freeze** — `sources`,
`combinedSha256`
`8034e2d1fd8b6dc268084e24a75878fdfeda7888809e4386fbd18174660cbed3`,
`binary` `791d36ecb4f2e9a7c8a4f045d13763d177d71c32257c174fb8f06fddffcff183`
and the embedded EVAL-005 archives — is unchanged and is what `run_stability.py`
and the evidence tests enforce. `FREEZE.json` is intentionally **not** rewritten.

The **current** evaluation tooling (`sources` and commands) is recorded
separately in `docs/research/tempo-stability/tooling-hashes.json` and
authenticated by `tools/tempo-stability/tests/test_stability_evidence.py`.

### E2 — method-summary nominal-missing semantics (corrected)

In the historical `evidence/method-summary.json`, `confirmedBeatsOutOfBand` and
`longestOutOfBandConfirmedRun` (candidate) and `readyBeatsOutOfBand` and
`longestOutOfBandReadyRun` (old variant) were written as **0** when a fixture
declares **no `nominalBpm`**. That is *unmeasured*, not zero. Only the two short
`tempo_step` fixtures (`clean_eighths__tempo_step_1.25`,
`clean_eighths__tempo_step_0.85`) lack a nominal BPM.

Corrected, in a separate `evidence-corrected/method-summary.json` recomputed from
the preserved gzipped raw method logs and the manifests: those two fields are
`null` and a `...Measured` flag is `false`; for every fixture with a nominal BPM
the measured counts are unchanged. This is a reporting semantic fix only; the
historical file is preserved unchanged.

### E3 — process deviations disclosed (three tooling-only fixes)

Two evaluation-tooling-only fixes were already disclosed in the task note; a
third is added here. All were applied after the behaviour freeze and before the
retained run, and none changes the candidate, bands or matrix:

1. `run_stability.py`: a guard for the two short `tempo_step` fixtures that
   declare no `nominalBpm` (interval diagnostic only), so `interval_summary`
   returns null expected/outlier fields instead of dividing by zero.
2. `run_stability.py`: a beat-equality bookkeeping bug where the aubio digest was
   added to the same dict object stored as the BTrack-family hash (the family
   comparison itself was already correct).
3. `tests/test_stability_evidence.py`: a **test-only** fix that validated the
   concatenated method log with a global block index instead of per-fixture
   windows.

### E4 — "pure fallback on short" wording

The report and task note said the candidate was "pure fallback" on 5 s clips and
"cannot deliver derived-BPM gains there". That is too absolute. The confirmation
gate **does** confirm on short clips — seven short fixtures carry confirmed beats
(six of them with a nominal BPM), including `clean_eighths__level_-40db`, whose
BPM relative error improves 2.344 % → 0.036 % (the single short BPM gain). The
corrected wording states *usually* fallback, not *pure*.

### E5 — corpus availability / rendering contingency

`run_stability.py` **fails closed** if any expected WAV is missing or its
hash/size/framing differs from the committed manifest. Rendering the long WAVs
from scratch is an **explicit operator step** (the frozen TRACK-007 generator run
outside git); it is **not** implemented automatically by this tool and no
generator is changed. The retained run reused the already-rendered read-only long
corpus and the tracked short corpus.

## What the corrected derived evidence adds

`tools/tempo-stability/recompute_method_summary.py` reads **only** the four
preserved gzipped raw method logs and the two committed manifests, and writes:

- `evidence-corrected/method-summary.json` — the E2-corrected candidate and old
  variant summaries (`null` + `...Measured` when nominal is missing);
- `evidence-corrected/derived-hashes.txt` — hashes of the corrected files and the
  raw inputs they were derived from.

No WAVs, no duplicated raw logs, no re-scoring. The historical `evidence/**` and
its `artifact-hashes.txt` stay byte-identical.
