# guitar-lock-eval evidence (EVAL-GUITAR-009)

This directory holds the frozen protocol, the bounded evidence tree and the
diagnostic baseline for the useful-lock evaluator. **G3 remains OPEN.** No real
representative guitar recordings were available, so the >=95% gate **cannot
pass** here and is reported as a fail-closed FAIL. Everything below the gate is
a **diagnostic** over the committed synthetic corpus and never counts toward the
gate.

- Base commit: `984ad1d32c44157ad2d4128c47b86dc932535918` (see `provenance.json`
  for the generated-at HEAD).
- Frozen protocol: `../../tools/guitar-lock-eval/protocol/useful-lock-protocol.json`
  (sha256 recorded in `provenance.json`; hashed before and after the run).
- Tool: `tools/guitar-lock-eval` (Python standard library; no new dependency).
- Replay adapter: the pre-existing `tracker-diagnostics` binary, built into
  `/home/mojo/projects/build-EVAL-GUITAR-009/diag`, loading the pre-existing
  `librhythm-eval-{btrack,aubio}.so` plugins. Traces are generated from the
  actual audio; no result is canned.

## The protocol is frozen before measurement

`protocol/useful-lock-protocol.json` fixes, before any trace is generated:

- BPM agreement 2 % (SPEC 19), beat tolerance 70 ms (EVAL-002);
- lock run = one bar of the annotated meter; acquisition window = two bars;
- useful lock = acquired within two bars **and** correct BPM **and** usable phase
  (mean |phase| <= 70 ms, p95 <= 150 ms);
- half/double band ±10 %; holdover, half, double, false and no-lock are separate
  labels;
- the 95 % gate is evaluated only over representative, human-annotated,
  licensed, steady 4/4 real recordings; missing/empty/corrupt/NaN/unrepresentative
  evidence fails closed.

`evaluate` wrote the trace set, then re-ran the whole comparison from the
committed traces alone (`--traces-dir traces`) and reproduced the numbers
exactly. `results.json` records the protocol hash before and after.

## Baseline results (diagnostic, synthetic corpus, block 128)

| backend | population | n | useful lock | fraction | half | double | false lock | no lock |
|---|---|---|---|---|---|---|---|---|
| btrack | diagnostic | 19 | 5 | 26.3% | 0 | 0 | 9 | 1 |
| aubio | diagnostic | 19 | 12 | 63.2% | 0 | 0 | 4 | 3 |
| diagnostic-beat-interval-bpm (derived) | diagnostic | 19 | 12 | 63.2% | 1 | 0 | 5 | 0 |

The **gate** population is empty for both real backends (no representative real
recording), so the gate is FAIL with the fail-closed reason recorded in
`summary.md` / `results.json`.

These are diagnostic numbers on synthetic material and are **not** comparable to
the committed real-backend SPEC 19 gate (EVAL-002/EVAL-004/TRACK-004): the
population here is all 19 fixtures (not the 11 core), and a useful lock adds the
explicit BPM-correct **and** phase-usable conditions. As a cross-check of the
scorer against the unmodified C++ evidence, BTrack's `clean_eighths` reported
`123.046875` BPM (2.34 % high) and is therefore a **false lock** (phase correct,
tempo wrong) — the same report artefact TRACK-004 documents.

The derived candidate is a causal beat-interval BPM (60 / median of the last ≤4
inter-beat intervals) computed entirely inside the new tool. It is shown only to
demonstrate the comparison path: it recovers useful lock on
`clean_sixteenths`/`palm_mute_metal` where aubio did not, loses it on some
fixtures aubio held, and labels `sustained_chords` a **half-time** lock. It is a
diagnostic candidate, not a production promotion, and no `src/` file changed.

## Artifact tree

```
baseline-import.json   synthetic import manifest (identity: path + sha256 + rate + duration)
protocol (frozen)      ../../tools/guitar-lock-eval/protocol/useful-lock-protocol.json
traces/<backend>/<id>.json          reduced traces (beats + compacted tempo + receipt)
traces/diagnostic-beat-interval-bpm/<id>.json   derived candidate traces
traces/raw-beats/<backend>/<id>.beats.csv       raw per-beat CSV from the adapter
results.json           per-backend gate + diagnostic populations, per-fixture scores
per-fixture.csv        the same scores as a flat table
summary.md             human-readable gate/diagnostic summary
provenance.json        protocol/manifest/adapter/trace hashes, audio identities, G3=OPEN
```

Everything is bounded (≈0.9 MiB) and deterministic. Private real WAVs are never
copied into the repository: a private recording is imported by path + sha256 and
scored in place.

## What would make the gate pass

A representative, licensed, human-annotated set of steady 4/4 real guitar
recordings imported with `guitar-lock-eval import --classification real`, plus a
trace per required backend. Until then the real-guitar G3 gate stays OPEN and
fails closed.
