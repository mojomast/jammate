"""Command-line interface for guitar-lock-eval (EVAL-GUITAR-009).

Subcommands
-----------
  import          add/update one recording in an import manifest
  import-corpus   convert a rhythm corpus manifest into a synthetic import manifest
  validate        validate an import manifest (and optional traces) against the bytes
  evaluate        one-command compare of BTrack/aubio/diagnostic candidate

`evaluate` freezes the protocol first, then generates traces from the actual
audio through the existing tracker-diagnostics binary (or reads pre-generated
traces), scores useful lock, and writes bounded evidence. It never edits src/,
CMake, the ledger or any existing evidence.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

from . import __version__
from .adapters import (DEFAULT_TRACKER_DIAGNOSTICS, build_rhythm_manifest,
                       copy_raw_beats, derive_beat_interval_trace, reduce_trace,
                       resolve_backend_lib, run_tracker_diagnostics)
from .gate import evaluate_diagnostic, evaluate_gate
from .integrity import (IntegrityError, dump_json_strict, is_hex64,
                        load_json_strict, safe_resolve, sha256_file, utc_now_iso)
from .manifest import (IMPORT_SCHEMA, ImportManifest, load_import_manifest,
                       validate_manifest)
from .report import fixture_csv_rows, markdown_summary
from .scoring import Criteria, score_useful_lock
from .trace import load_trace, parse_trace, validate_trace

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PROTOCOL = os.environ.get(
    "GLE_PROTOCOL", os.path.join(HERE, "protocol", "useful-lock-protocol.json"))
DEFAULT_WORKDIR = os.environ.get(
    "GLE_WORKDIR", "/home/mojo/projects/build-EVAL-GUITAR-009/guitar-lock-eval")


def _git_head() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except Exception:  # noqa: BLE001
        return ""


# ---------------------------------------------------------------------------
# import
# ---------------------------------------------------------------------------

def cmd_import(args) -> int:
    manifest_path = os.path.abspath(args.manifest)
    if os.path.isfile(manifest_path):
        data = load_json_strict(manifest_path)
        if data.get("schema") != IMPORT_SCHEMA:
            print(f"import: {manifest_path} has unexpected schema", file=sys.stderr)
            return 2
    else:
        data = {"schema": IMPORT_SCHEMA, "task": "EVAL-GUITAR-009",
                "created_utc": utc_now_iso(), "recordings": []}

    audio = os.path.abspath(args.audio)
    from .wav import read_wav_info
    info = read_wav_info(audio)
    if not info.ok:
        print(f"import: audio not scorable: {info.error}", file=sys.stderr)
        return 2

    ann_spec = None
    if args.annotation:
        ann_path = os.path.abspath(args.annotation)
        if not os.path.isfile(ann_path):
            print(f"import: annotation not found: {ann_path}", file=sys.stderr)
            return 2
        # Parse to validate it before recording it.
        from .manifest import _annotation_from_obj
        _annotation_from_obj(load_json_strict(ann_path), ann_path)
        try:
            rel = os.path.relpath(ann_path, os.path.dirname(manifest_path))
            if not rel.startswith(".."):
                ann_path_stored = rel
            else:
                ann_path_stored = ann_path
        except ValueError:
            ann_path_stored = ann_path
        ann_spec = {"kind": "file", "path": ann_path_stored,
                    "sha256": sha256_file(ann_path)}

    try:
        audio_rel = os.path.relpath(audio, os.path.dirname(manifest_path))
        if audio_rel.startswith(".."):
            audio_stored = audio
        else:
            audio_stored = audio_rel
    except ValueError:
        audio_stored = audio

    entry = {
        "id": args.id,
        "audio_path": audio_stored,
        "audio_sha256": info.sha256,
        "sample_rate": info.sample_rate,
        "channels": info.channels,
        "sample_width_bytes": info.sample_width_bytes,
        "frames": info.frames,
        "duration_seconds": info.duration_seconds,
        "classification": args.classification,
        "license": args.license or "",
        "ownership": args.ownership or "",
        "provenance": args.provenance or "",
        "representative": not args.unrepresentative,
        "parent_id": args.parent_id,
        "tags": args.tags.split(",") if args.tags else [],
        "annotation": ann_spec,
    }
    recs = [r for r in data["recordings"] if r.get("id") != args.id]
    recs.append(entry)
    data["recordings"] = sorted(recs, key=lambda r: r["id"])
    os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
    dump_json_strict(data, manifest_path)
    print(f"import: wrote {manifest_path} with {len(data['recordings'])} recordings")
    return 0


def cmd_import_corpus(args) -> int:
    corpus = os.path.abspath(args.rhythm_manifest)
    data = load_json_strict(corpus)
    corpus_dir = os.path.dirname(corpus)
    audio_root = args.audio_root or corpus_dir
    base_root = os.path.abspath(args.base_root) if args.base_root else os.path.abspath(os.getcwd())
    fixtures = data.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        print("import-corpus: corpus has no fixtures", file=sys.stderr)
        return 2
    from .wav import read_wav_info
    corpus_rel = os.path.relpath(corpus, base_root)
    if corpus_rel.startswith(".."):
        corpus_rel = corpus
    entries = []
    for fx in fixtures:
        name = fx["name"]
        audio = os.path.join(audio_root, fx["file"])
        info = read_wav_info(audio)
        if not info.ok:
            print(f"import-corpus: {name}: {info.error}", file=sys.stderr)
            return 2
        audio_rel = os.path.relpath(os.path.abspath(audio), base_root)
        if audio_rel.startswith(".."):
            audio_rel = os.path.abspath(audio)
        entries.append({
            "id": name,
            "audio_path": audio_rel,
            "audio_sha256": info.sha256,
            "sample_rate": info.sample_rate,
            "channels": info.channels,
            "sample_width_bytes": info.sample_width_bytes,
            "frames": info.frames,
            "duration_seconds": info.duration_seconds,
            "classification": args.classification,
            "license": fx.get("license", "") or "CC0-1.0",
            "ownership": "project-generated (testdata/rhythm)",
            "provenance": fx.get("provenance", ""),
            "representative": False,
            "parent_id": None,
            "tags": list(fx.get("scenarioTags", [])),
            "annotation": {
                "kind": "rhythm-manifest-fixture",
                "corpus_manifest": corpus_rel,
                "fixture": name,
            },
        })
    out = {
        "schema": IMPORT_SCHEMA,
        "task": "EVAL-GUITAR-009",
        "created_utc": utc_now_iso(),
        "note": ("Synthetic diagnostic baseline generated from the committed "
                 "testdata/rhythm corpus. Not a real-guitar G3 gate population."),
        "recordings": sorted(entries, key=lambda r: r["id"]),
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    dump_json_strict(out, args.out)
    print(f"import-corpus: wrote {args.out} with {len(entries)} synthetic recordings")
    return 0


# ---------------------------------------------------------------------------
# validate
# ---------------------------------------------------------------------------

def cmd_validate(args) -> int:
    try:
        manifest = load_import_manifest(args.manifest, args.audio_root,
                                        args.annotation_root, verify_files=True)
    except IntegrityError as exc:
        print(f"validate: FAIL: {exc}", file=sys.stderr)
        return 2
    findings = validate_manifest(manifest)
    hard = [f for f in findings if f.severity == "hard"]
    print(f"validate: {len(manifest.recordings)} recordings, "
          f"{len(hard)} hard findings")
    for f in findings:
        print(f"  [{f.severity}] {f.code}: {f.detail}")
    return 1 if hard else 0


# ---------------------------------------------------------------------------
# evaluate
# ---------------------------------------------------------------------------

def _load_traces_from_dir(traces_dir: str, backend: str, recordings):
    """Load per-recording traces from <traces_dir>/<backend>/<id>.json."""
    out = {}
    missing = []
    for rec in recordings:
        path = os.path.join(traces_dir, backend, rec.id + ".json")
        if not os.path.isfile(path):
            missing.append(rec.id)
            continue
        out[rec.id] = load_trace(path)
    return out, missing


def cmd_evaluate(args) -> int:
    workdir = os.path.abspath(args.workdir)
    os.makedirs(workdir, exist_ok=True)
    protocol_sha_before = sha256_file(args.protocol)
    protocol = load_json_strict(args.protocol)
    criteria = Criteria.from_protocol(protocol)

    try:
        manifest = load_import_manifest(args.manifest, args.audio_root,
                                        args.annotation_root, verify_files=True)
    except IntegrityError as exc:
        print(f"evaluate: FAIL: {exc}", file=sys.stderr)
        return 2

    manifest_findings = [f for f in validate_manifest(manifest) if f.severity == "hard"]
    if manifest_findings:
        print("evaluate: FAIL: manifest integrity", file=sys.stderr)
        for f in manifest_findings:
            print(f"  {f.code}: {f.detail}", file=sys.stderr)
        return 2

    recordings = manifest.recordings
    annotated = [r for r in recordings if r.annotation is not None]
    integrity_errors: list[str] = []
    missing_annotations = [r.id for r in recordings if r.annotation is None]
    for rec_id in missing_annotations:
        integrity_errors.append(f"{rec_id}: no annotation; cannot be scored (fail closed)")

    backends = [b.strip() for b in args.backends.split(",") if b.strip()]
    if not backends:
        print("evaluate: no backends requested", file=sys.stderr)
        return 2

    traces: dict[str, dict] = {}
    adapters_meta = {}
    traces_out = os.path.abspath(args.traces_out) if getattr(args, "traces_out", None) else None
    if args.traces_dir:
        for backend in backends:
            loaded, missing = _load_traces_from_dir(args.traces_dir, backend, annotated)
            traces[backend] = loaded
            for rec_id in missing:
                integrity_errors.append(f"{backend}: missing pre-generated trace for {rec_id}")
    else:
        staging = os.path.join(workdir, "staging")
        corpus_dir = build_rhythm_manifest(annotated, staging)
        for backend in backends:
            out_dir = os.path.join(workdir, "runs", backend)
            lib = resolve_backend_lib(backend, getattr(args, backend + "_lib", None))
            adapter = run_tracker_diagnostics(args.tracker_diagnostics, backend, lib,
                                              corpus_dir, out_dir, args.block)
            adapters_meta[backend] = adapter
            if not adapter.ok():
                integrity_errors.append(
                    f"{backend}: adapter exited {adapter.returncode}: {adapter.stderr.strip()[:300]}")
                traces[backend] = {}
                continue
            traces[backend] = {}
            for rec in annotated:
                try:
                    tdict = reduce_trace(rec, backend, "real", out_dir, adapter, args.block)
                except IntegrityError as exc:
                    integrity_errors.append(f"{backend}/{rec.id}: {exc}")
                    continue
                if traces_out:
                    dest_dir = os.path.join(traces_out, backend)
                    os.makedirs(dest_dir, exist_ok=True)
                    dump_json_strict(tdict, os.path.join(dest_dir, rec.id + ".json"))
                    try:
                        copy_raw_beats(out_dir, rec.id,
                                       os.path.join(traces_out, "raw-beats", backend))
                    except OSError:
                        pass
                t = parse_trace(tdict, f"{backend}/{rec.id}", sha="")
                traces[backend][rec.id] = t

    # Close the trace objects (compute hashes) and validate against recordings.
    scores: dict[str, dict] = {}
    for backend in backends:
        scores[backend] = {}
        for rec in annotated:
            t = traces[backend].get(rec.id)
            if t is None:
                continue
            t.sha256 = _trace_sha(t)
            findings = validate_trace(t, rec, expected_backend=backend,
                                      expected_kind="real")
            hard = [f for f in findings if f.severity == "hard"]
            if hard:
                for f in hard:
                    integrity_errors.append(f"{backend}/{rec.id}: {f.code}: {f.detail}")
                continue
            scores[backend][rec.id] = score_useful_lock(rec, t, criteria)

    # Diagnostic candidate derived from the btrack trace (trace-level only).
    candidate_id = "diagnostic-beat-interval-bpm"
    candidate_scores: dict[str, "FixtureScore"] = {}
    candidate_traces = {}
    if args.diagnostic_candidate and "btrack" in traces:
        for rec in annotated:
            parent = traces["btrack"].get(rec.id)
            if parent is None:
                continue
            cdict = derive_beat_interval_trace(parent, rec, candidate_id=candidate_id)
            ct = parse_trace(cdict, f"{candidate_id}/{rec.id}", sha="")
            if traces_out:
                dest_dir = os.path.join(traces_out, candidate_id)
                os.makedirs(dest_dir, exist_ok=True)
                dump_json_strict(cdict, os.path.join(dest_dir, rec.id + ".json"))
            ct.sha256 = _trace_sha(ct)
            findings = validate_trace(ct, rec, expected_backend=candidate_id,
                                      expected_kind="derived", expected_parent="btrack")
            if any(f.severity == "hard" for f in findings):
                continue
            candidate_traces[rec.id] = ct
            candidate_scores[rec.id] = score_useful_lock(rec, ct, criteria)

    # Populations.
    populations = []
    for backend in backends:
        populations.append(evaluate_gate(backend, recordings, scores[backend], criteria))
    for backend in backends:
        populations.append(evaluate_diagnostic(backend, recordings, scores[backend]))
    all_backends = list(backends)
    if candidate_scores:
        all_backends.append(candidate_id)
        populations.append(evaluate_diagnostic(candidate_id, recordings, candidate_scores))

    protocol_sha_after = sha256_file(args.protocol)
    protocol_unchanged = protocol_sha_before == protocol_sha_after
    if not protocol_unchanged:
        integrity_errors.append("protocol file changed during measurement (freeze violated)")

    meta = {
        "schema": "guitar-lock-eval/results/1.0",
        "tool": "guitar-lock-eval",
        "version": __version__,
        "generated_utc": utc_now_iso(),
        "base_commit": protocol.get("base_commit", "") or _git_head(),
        "head": _git_head(),
        "protocol_path": os.path.abspath(args.protocol),
        "protocol_sha256": protocol_sha_before,
        "protocol_unchanged": protocol_unchanged,
        "manifest_path": manifest.path,
        "manifest_sha256": manifest.sha256,
        "block_frames": args.block,
        "evidence_class": ("diagnostic baseline (synthetic corpus); real-guitar G3 gate "
                           "cannot pass without representative real recordings"),
        "integrity_errors": integrity_errors,
        "missing_annotations": missing_annotations,
        "adapters": {b: {
            "binary": a.binary, "binary_sha256": a.binary_sha256,
            "backend_lib": a.backend_lib, "backend_lib_sha256": a.backend_lib_sha256,
            "returncode": a.returncode,
        } for b, a in adapters_meta.items()},
        "backends": all_backends,
    }

    # Gate verdict (across real backends; fail closed on any integrity error).
    gate_pops = [p for p in populations if p.label == "gate"]
    gate_pass = (not integrity_errors) and all(p.gate_pass for p in gate_pops)
    gate_reasons = []
    if integrity_errors:
        gate_reasons.extend(integrity_errors)
    for p in gate_pops:
        gate_reasons.extend(p.fail_closed)
    meta["gate_pass"] = gate_pass
    meta["gate_reasons"] = gate_reasons
    meta["populations"] = [p.as_dict() for p in populations]

    # Write outputs.
    if args.json_out:
        dump_json_strict(meta, args.json_out)
    if args.provenance_out:
        prov_root = os.path.dirname(os.path.abspath(args.provenance_out))
        trace_files = []
        search_root = traces_out or (os.path.abspath(args.traces_dir) if args.traces_dir else None)
        if search_root and os.path.isdir(search_root):
            for base, _dirs, files in os.walk(search_root):
                for fn in sorted(files):
                    if fn.endswith(".json"):
                        p = os.path.join(base, fn)
                        trace_files.append({
                            "path": os.path.relpath(p, prov_root),
                            "sha256": sha256_file(p),
                        })
        provenance = {
            "schema": "guitar-lock-eval/provenance/1.0",
            "task": "EVAL-GUITAR-009",
            "tool": "guitar-lock-eval",
            "version": __version__,
            "generated_utc": meta["generated_utc"],
            "base_commit": meta["base_commit"],
            "head": meta["head"],
            "protocol_path": meta["protocol_path"],
            "protocol_sha256": meta["protocol_sha256"],
            "protocol_unchanged": protocol_unchanged,
            "manifest_path": manifest.path,
            "manifest_sha256": manifest.sha256,
            "block_frames": args.block,
            "evidence_class": meta["evidence_class"],
            "g3": "OPEN",
            "gate_pass": gate_pass,
            "gate_reasons": gate_reasons,
            "adapters": meta["adapters"],
            "audio_identities": [{
                "id": r.id,
                "classification": r.classification,
                "audio_path": r.audio_path,
                "audio_sha256": r.declared.get("audio_sha256"),
                "sample_rate": r.declared.get("sample_rate"),
                "duration_seconds": r.declared.get("duration_seconds"),
            } for r in recordings],
            "trace_files": trace_files,
        }
        dump_json_strict(provenance, args.provenance_out)
    if args.summary_md:
        with open(args.summary_md, "w", encoding="utf-8") as fh:
            fh.write(markdown_summary(meta, populations))
    if args.per_fixture_csv:
        cols, rows = fixture_csv_rows(populations)
        with open(args.per_fixture_csv, "w", encoding="utf-8", newline="") as fh:
            import csv as _csv
            w = _csv.writer(fh)
            w.writerow(cols)
            w.writerows(rows)

    # One-command comparison to stdout.
    print(f"guitar-lock-eval: {manifest.path} block {args.block}")
    for p in populations:
        frac = "n/a" if p.fraction is None else f"{p.fraction*100:.1f}%"
        verdict = ("PASS" if p.gate_pass else ("FAIL" if p.label == "gate" else "diagnostic"))
        print(f"  {p.backend:32s} {p.label:11s} n={len(p.population):2d} "
              f"useful={p.useful_lock_count:2d} ({frac}) half={p.label_counts.get('half_time_lock',0)} "
              f"double={p.label_counts.get('double_time_lock',0)} "
              f"false={p.label_counts.get('false_lock',0)} -> {verdict}")
    if integrity_errors:
        print("integrity errors:")
        for e in integrity_errors:
            print(f"  - {e}")
    print(f"gate: {'PASS' if gate_pass else 'FAIL'}"
          + ("" if gate_pass else " (" + "; ".join(gate_reasons[:4]) + ")"))

    if integrity_errors or not protocol_unchanged:
        return 2
    if gate_pass or args.diagnostic_ok:
        return 0
    return 1


def _trace_sha(t) -> str:
    """Deterministic sha256 of the trace's own content (not the file)."""
    import hashlib
    import json
    payload = json.dumps(t.raw, sort_keys=True, allow_nan=False).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


# ---------------------------------------------------------------------------
# protocol-hash
# ---------------------------------------------------------------------------

def cmd_protocol_hash(args) -> int:
    print(sha256_file(args.protocol))
    return 0


# ---------------------------------------------------------------------------
# argument parsing
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="guitar-lock-eval", description=__doc__)
    p.add_argument("--version", action="version", version=__version__)
    sub = p.add_subparsers(dest="cmd", required=True)

    pi = sub.add_parser("import", help="add/update one recording in an import manifest")
    pi.add_argument("--manifest", required=True)
    pi.add_argument("--id", required=True)
    pi.add_argument("--audio", required=True)
    pi.add_argument("--classification", required=True,
                    choices=["real", "synthetic", "derived"])
    pi.add_argument("--annotation", help="annotation JSON file")
    pi.add_argument("--license", default="")
    pi.add_argument("--ownership", default="")
    pi.add_argument("--provenance", default="")
    pi.add_argument("--parent-id", default=None)
    pi.add_argument("--tags", default="")
    pi.add_argument("--unrepresentative", action="store_true")
    pi.set_defaults(func=cmd_import)

    pc = sub.add_parser("import-corpus",
                        help="convert a rhythm corpus manifest into an import manifest")
    pc.add_argument("--rhythm-manifest", required=True)
    pc.add_argument("--audio-root")
    pc.add_argument("--base-root", help="store audio/annotation paths relative to this root")
    pc.add_argument("--classification", default="synthetic",
                    choices=["real", "synthetic", "derived"])
    pc.add_argument("--out", required=True)
    pc.set_defaults(func=cmd_import_corpus)

    pv = sub.add_parser("validate", help="validate an import manifest against the bytes")
    pv.add_argument("--manifest", required=True)
    pv.add_argument("--audio-root")
    pv.add_argument("--annotation-root")
    pv.set_defaults(func=cmd_validate)

    pe = sub.add_parser("evaluate", help="one-command backend comparison")
    pe.add_argument("--manifest", required=True)
    pe.add_argument("--protocol", default=DEFAULT_PROTOCOL)
    pe.add_argument("--audio-root")
    pe.add_argument("--annotation-root")
    pe.add_argument("--block", type=int, default=128)
    pe.add_argument("--backends", default="btrack,aubio")
    pe.add_argument("--tracker-diagnostics", default=DEFAULT_TRACKER_DIAGNOSTICS)
    pe.add_argument("--btrack-lib")
    pe.add_argument("--aubio-lib")
    pe.add_argument("--workdir", default=DEFAULT_WORKDIR)
    pe.add_argument("--traces-dir")
    pe.add_argument("--traces-out", help="write generated traces here (bounded evidence)")
    pe.add_argument("--diagnostic-candidate", action="store_true", default=True)
    pe.add_argument("--no-diagnostic-candidate", dest="diagnostic_candidate",
                    action="store_false")
    pe.add_argument("--diagnostic-ok", action="store_true",
                    help="exit 0 when a diagnostic run has no integrity errors but the gate cannot pass")
    pe.add_argument("--json-out")
    pe.add_argument("--summary-md")
    pe.add_argument("--per-fixture-csv")
    pe.add_argument("--provenance-out")
    pe.set_defaults(func=cmd_evaluate)

    ph = sub.add_parser("protocol-hash", help="print the frozen protocol sha256")
    ph.add_argument("--protocol", default=DEFAULT_PROTOCOL)
    ph.set_defaults(func=cmd_protocol_hash)
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except IntegrityError as exc:
        print(f"guitar-lock-eval: FAIL: {exc}", file=sys.stderr)
        return 2
    except OSError as exc:
        print(f"guitar-lock-eval: FAIL: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
