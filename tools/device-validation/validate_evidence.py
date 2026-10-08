#!/usr/bin/env python3
"""Fail-closed validator for Guitar Companion device-validation evidence.

Rules (device tools must retain raw observations, identity and explicit
measured/unmeasured fields; an empty template can never pass):

  * every measurement record must reference raw bytes whose sha256 matches;
  * a latency record's number is re-derived from the WAV with the recorded
    parameters, so a bare or edited number is rejected;
  * synthetic records are labelled and are excluded from every physical gate;
  * the SPEC.md 21.5 hardware matrix must be non-empty and every required cell
    must be measured and valid; Windows ASIO cells must identify the backend and
    driver and carry an actual physical measurement (callback estimates alone do
    not count);
  * the SPEC.md 20 play trials (when --gate all) must each be present.

Exit codes: 0 = requested gate passes, 1 = hard failure (schema/hash/fabrication),
2 = no hard failure but the gate is incomplete or failing.
"""
import argparse
import os
import sys

import device_lib as dl

REPORT_SCHEMA = "device-validation/report/1.0"
TARGET_MS = dl.LATENCY_CONDITIONS["asio_48k_128"]["target_ms"]


class HardError(Exception):
    pass


def add_hard(errors, record_path, rule, detail):
    errors.append({"record": record_path, "rule": rule, "detail": detail})


def _dedupe_errors(errors):
    seen = set()
    out = []
    for err in errors:
        key = (err["record"], err["rule"], err["detail"])
        if key not in seen:
            seen.add(key)
            out.append(err)
    return out


def _rel(session_dir, path):
    if not isinstance(path, str):
        return path
    rel = os.path.relpath(path, session_dir)
    return path if rel.startswith("..") else rel


def _relativize_paths(cells, errors, session_dir):
    for err in errors:
        err["record"] = _rel(session_dir, err["record"])
    for cell in cells.values():
        cell["records"] = [_rel(session_dir, p) for p in cell.get("records", [])]


def check_ref(ref, base_dir, errors, record_path, label):
    if not isinstance(ref, dict):
        add_hard(errors, record_path, "raw-ref-missing", "%s has no reference" % label)
        return False
    if not dl.is_hex64(ref.get("sha256")):
        add_hard(errors, record_path, "raw-ref-hash", "%s sha256 malformed" % label)
        return False
    resolved = dl.resolve_path(base_dir, ref.get("path"))
    if not resolved or not os.path.isfile(resolved):
        add_hard(errors, record_path, "raw-unresolved",
                 "%s not found: %s" % (label, ref.get("path")))
        return False
    actual = dl.sha256_file(resolved)
    if actual != ref["sha256"]:
        add_hard(errors, record_path, "raw-hash-mismatch",
                 "%s sha256 %s != recorded %s" % (label, actual[:16], ref["sha256"][:16]))
        return False
    size = os.path.getsize(resolved)
    if isinstance(ref.get("bytes"), int) and ref["bytes"] != size:
        add_hard(errors, record_path, "raw-size-mismatch",
                 "%s bytes %d != recorded %d" % (label, size, ref["bytes"]))
        return False
    return True


def dedupe(seq):
    seen = set()
    out = []
    for item in seq:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def expected_latency_from_bytes(record, base_dir, errors, record_path):
    """Re-run the recorded algorithm and return (reasons, latency, target_met).

    Returns ``(None, None, None, extra)`` on an unreadable WAV; caller records
    the hard error.
    """
    identity = record.get("identity") or {}
    ref = identity.get("raw_wav")
    if not check_ref(ref, base_dir, errors, record_path, "identity.raw_wav"):
        return None, None, None, None
    analysis = record.get("analysis")
    if not isinstance(analysis, dict):
        add_hard(errors, record_path, "analysis-missing", "analysis block absent")
        return None, None, None, None
    params = analysis.get("params")
    if not isinstance(params, dict):
        add_hard(errors, record_path, "analysis-params", "analysis.params absent")
        return None, None, None, None
    try:
        recomputed = dl.analyze_loopback(ref["path"], params, base_dir=base_dir)
    except dl.WavError as exc:
        add_hard(errors, record_path, "raw-unreadable", str(exc))
        return None, None, None, None
    iface = (identity.get("interface") or {})
    i_reasons = dl.interface_reasons(record.get("condition"), iface)
    reasons = dedupe(list(recomputed["reasons"]) + i_reasons)
    lat = recomputed["physical_roundtrip_latency_ms"]
    unaccepted = None
    if reasons:
        unaccepted = lat
        lat = None
    target_ms = params.get("target_ms")
    target_met = None
    if lat is not None and target_ms is not None:
        target_met = lat <= target_ms
    return reasons, lat, target_met, {
        "recomputed_valid_dsp": recomputed["valid"],
        "unaccepted": unaccepted,
    }


def validate_latency(record, base_dir, errors, record_path, recheck):
    identity = record.get("identity") or {}
    ref = identity.get("raw_wav")
    check_ref(ref, base_dir, errors, record_path, "identity.raw_wav")
    if identity.get("raw_reference_wav"):
        check_ref(identity["raw_reference_wav"], base_dir, errors, record_path,
                  "identity.raw_reference_wav")
    analysis = record.get("analysis")
    if not isinstance(analysis, dict):
        add_hard(errors, record_path, "analysis-missing", "analysis absent")
        return {"status": "fail", "latency_ms": None, "target_ms": None,
                "target_met": None, "reasons": ["analysis-missing"]}
    method = analysis.get("method")
    if method not in ("two-channel", "single-channel"):
        add_hard(errors, record_path, "analysis-method",
                 "unknown method %r" % method)
    stored_reasons = analysis.get("reasons")
    if not isinstance(stored_reasons, list):
        add_hard(errors, record_path, "analysis-reasons", "reasons not a list")
        stored_reasons = []
    stored_lat = analysis.get("physical_roundtrip_latency_ms")
    stored_valid = analysis.get("valid") is True
    if stored_lat is not None and not dl.finite(stored_lat):
        add_hard(errors, record_path, "nonfinite-latency",
                 "physical latency is not finite")

    if recheck:
        reasons, lat, target_met, extra = expected_latency_from_bytes(
            record, base_dir, errors, record_path)
        if reasons is not None:
            if list(stored_reasons) != reasons:
                add_hard(errors, record_path, "latency-recheck-reasons",
                         "stored reasons %s != recomputed %s"
                         % (stored_reasons, reasons))
            if stored_lat != lat:
                add_hard(errors, record_path, "latency-recheck-value",
                         "stored latency %r != recomputed %r" % (stored_lat, lat))
            expected_valid = lat is not None and not reasons
            if stored_valid != expected_valid:
                add_hard(errors, record_path, "latency-recheck-valid",
                         "stored valid %s != recomputed %s"
                         % (stored_valid, expected_valid))
            stored_target = analysis.get("target_met")
            if stored_target != target_met:
                add_hard(errors, record_path, "latency-recheck-target",
                         "stored target_met %r != recomputed %r"
                         % (stored_target, target_met))
            effective_lat = lat
            effective_reasons = reasons
            effective_target_met = target_met
        else:
            effective_lat = stored_lat if stored_valid else None
            effective_reasons = stored_reasons
            effective_target_met = analysis.get("target_met")
    else:
        effective_lat = stored_lat if stored_valid else None
        effective_reasons = stored_reasons
        effective_target_met = analysis.get("target_met")

    target_ms = (analysis.get("params") or {}).get("target_ms")
    if target_ms is not None and not dl.finite(target_ms):
        add_hard(errors, record_path, "nonfinite-target", "target_ms not finite")
    status = "fail"
    if effective_lat is not None:
        if target_ms is not None and (effective_lat > target_ms):
            status = "fail"
        else:
            status = "pass"
        # The validator does not trust a self-declared target_met.
        recomputed_met = None if target_ms is None else effective_lat <= target_ms
        if effective_target_met is not None and recomputed_met is not None \
                and effective_target_met != recomputed_met:
            add_hard(errors, record_path, "target-met-tampered",
                     "declared target_met %r != recomputed %r"
                     % (effective_target_met, recomputed_met))
    return {"status": status, "latency_ms": effective_lat,
            "target_ms": target_ms, "target_met": effective_target_met,
            "reasons": effective_reasons}


def validate_functional(record, base_dir, errors, record_path):
    if record.get("measured") is not True:
        add_hard(errors, record_path, "functional-unmeasured",
                 "functional record is not marked measured")
    if record.get("provenance") != "instrumented-raw":
        add_hard(errors, record_path, "functional-provenance",
                 "functional provenance must be instrumented-raw")
    ok = check_ref(record.get("receipt"), base_dir, errors, record_path, "receipt")
    outcome = record.get("outcome")
    if outcome not in ("pass", "fail"):
        add_hard(errors, record_path, "functional-outcome", "bad outcome")
    if not ok:
        return {"status": "fail", "outcome": outcome}
    return {"status": "pass" if outcome == "pass" else "fail", "outcome": outcome}


PLAY_MEASURED_GROUPS = {
    "timing": ("start_requested_s", "join_heard_s", "stop_s"),
    "callback": ("p50_ms", "p99_ms", "deadline_misses", "analysis_overruns"),
}


def _check_measured_field(group, key, receipt, base_dir, errors, record_path):
    field = group.get(key)
    if not isinstance(field, dict):
        add_hard(errors, record_path, "field-missing", "%s missing" % key)
        return False
    measured = field.get("measured") is True
    value = field.get("value")
    if measured and value is not None and not dl.finite(value):
        add_hard(errors, record_path, "nonfinite-field", "%s not finite" % key)
    if measured:
        if field.get("provenance") not in dl.MEASURED_PROVENANCE:
            add_hard(errors, record_path, "field-provenance",
                     "%s measured without raw provenance" % key)
        if not check_ref(field.get("receipt"), base_dir, errors, record_path,
                         "%s.receipt" % key):
            return False
    else:
        if value is not None:
            add_hard(errors, record_path, "unmeasured-value",
                     "%s carries a value but is marked unmeasured" % key)
    return True


def validate_play(record, base_dir, errors, record_path):
    for path in record.get("raw_files") or []:
        check_ref(path, base_dir, errors, record_path, "raw_files")
    settings = record.get("settings") or {}
    for key in ("intensity", "complexity", "fill_amount", "follow_tightness",
                "tempo_bpm"):
        if not dl.finite(settings.get(key)):
            add_hard(errors, record_path, "settings-missing",
                     "setting %s missing/non-finite" % key)
    timing = record.get("timing") or {}
    for key in PLAY_MEASURED_GROUPS["timing"]:
        _check_measured_field(timing, key, timing.get("receipt"), base_dir,
                              errors, record_path)
    callback = record.get("callback") or {}
    for key in PLAY_MEASURED_GROUPS["callback"]:
        _check_measured_field(callback, key, callback.get("receipt"), base_dir,
                              errors, record_path)
    drop = record.get("dropouts") or {}
    _check_measured_field(drop, "count", drop.get("receipt"), base_dir, errors,
                          record_path)
    lock = record.get("useful_lock") or {}
    _check_measured_field(lock, "window_bars", lock.get("receipt"), base_dir,
                          errors, record_path)
    _check_measured_field(lock, "time_to_lock_s", lock.get("receipt"), base_dir,
                          errors, record_path)
    verdict = lock.get("verdict")
    if verdict not in ("yes", "no", "unknown"):
        add_hard(errors, record_path, "play-verdict", "bad useful_lock verdict")
    if verdict == "yes":
        if not (lock.get("window_bars") or {}).get("measured"):
            add_hard(errors, record_path, "play-lock-window",
                     "useful-lock=yes without a measured two-bar window")
        if not (lock.get("time_to_lock_s") or {}).get("measured"):
            add_hard(errors, record_path, "play-lock-time",
                     "useful-lock=yes without a measured time-to-lock")
    if verdict == "yes" and (lock.get("window_bars") or {}).get("measured") \
            and (lock.get("time_to_lock_s") or {}).get("measured"):
        status = "pass"
    elif verdict == "no":
        status = "fail"
    else:
        status = "unmeasured"
    return {"status": status, "verdict": verdict}


def cross_check_identity(record, interfaces_by_id, errors, record_path):
    """The record's stored interface identity must match the session manifest."""
    iid = record.get("interface_id")
    iface = interfaces_by_id.get(iid)
    if iface is None:
        add_hard(errors, record_path, "interface-unknown",
                 "interface_id %r not in session" % iid)
        return
    identity = (record.get("identity") or {}).get("interface") or {}
    for key in ("os", "backend", "driver", "sample_rate", "block_frames"):
        if identity.get(key) != iface.get(key):
            add_hard(errors, record_path, "interface-identity-mismatch",
                     "%s %r != session %r" % (key, identity.get(key), iface.get(key)))


def validate_record(record, base_dir, record_path, errors, recheck,
                    interfaces_by_id=None):
    if not isinstance(record, dict):
        add_hard(errors, record_path, "not-object", "record is not an object")
        return None
    schema = record.get("schema")
    condition = record.get("condition")
    if schema not in dl.SCHEMAS:
        add_hard(errors, record_path, "schema", "unknown schema %r" % schema)
        return None
    if not isinstance(record.get("synthetic"), bool):
        add_hard(errors, record_path, "synthetic-flag",
                 "synthetic must be an explicit boolean")
    synthetic = record.get("synthetic") is True
    if interfaces_by_id is not None:
        cross_check_identity(record, interfaces_by_id, errors, record_path)
    if schema == dl.LATENCY_SCHEMA:
        if condition not in dl.LATENCY_CONDITIONS:
            add_hard(errors, record_path, "condition", "unknown latency condition")
        cell = validate_latency(record, base_dir, errors, record_path, recheck)
        kind = "latency"
    elif schema == dl.FUNCTIONAL_SCHEMA:
        if condition not in dl.FUNCTIONAL_CONDITIONS:
            add_hard(errors, record_path, "condition", "unknown functional condition")
        cell = validate_functional(record, base_dir, errors, record_path)
        kind = "functional"
    elif schema == dl.PLAY_TRIAL_SCHEMA:
        if condition not in dl.PLAY_CONDITIONS:
            add_hard(errors, record_path, "condition", "unknown play condition")
        cell = validate_play(record, base_dir, errors, record_path)
        kind = "play"
    else:  # session schema is never a measurement record
        add_hard(errors, record_path, "schema", "session file in measurements")
        return None
    return {"path": record_path, "schema": schema, "condition": condition,
            "kind": kind, "synthetic": synthetic, "cell": cell}


def discover_measurements(session_dir, explicit):
    found = []
    for path in explicit:
        found.append(os.path.abspath(path))
    default_dir = os.path.join(session_dir, "measurements")
    if not explicit and os.path.isdir(default_dir):
        for name in sorted(os.listdir(default_dir)):
            full = os.path.join(default_dir, name)
            if name.endswith(".json") and os.path.isfile(full):
                found.append(os.path.abspath(full))
    return found


def build_matrix(records, errors):
    cells = {}
    for cond in dl.REQUIRED_HARDWARE_CONDITIONS:
        cells[cond] = {"status": "missing", "kind": dl.condition_kind(cond),
                       "records": [], "latency_ms": None, "target_ms": None,
                       "target_met": None, "reasons": []}
    for cond in dl.PLAY_CONDITIONS:
        cells[cond] = {"status": "missing", "kind": "play", "records": [],
                       "verdict": None}
    for rec in records:
        if rec["synthetic"]:
            continue  # synthetic evidence never backs a physical cell
        cond = rec["condition"]
        if cond not in cells:
            continue
        cell = cells[cond]
        cell.setdefault("records", []).append(rec["path"])
        status = rec["cell"]["status"]
        if cond in dl.LATENCY_CONDITIONS and rec["cell"].get("latency_ms") is not None:
            cell["latency_ms"] = rec["cell"]["latency_ms"]
            cell["target_ms"] = rec["cell"]["target_ms"]
            cell["target_met"] = rec["cell"]["target_met"]
            cell["reasons"] = rec["cell"].get("reasons", [])
        if cond in dl.PLAY_CONDITIONS:
            cell["verdict"] = rec["cell"].get("verdict")
        # Prefer a pass, then fail, then unmeasured, for repeated observations.
        rank = {"missing": 0, "unmeasured": 1, "fail": 2, "pass": 3}
        if rank.get(status, 0) > rank.get(cell["status"], 0):
            cell["status"] = status
    return cells


def build_parser():
    p = argparse.ArgumentParser(
        description="Validate device-validation evidence and physical gates.")
    p.add_argument("--session", required=True)
    p.add_argument("--measurement", action="append", default=[],
                   help="explicit record file (repeatable); default: session measurements/")
    p.add_argument("--out", default=None, help="report JSON path")
    p.add_argument("--summary-md", default=None)
    p.add_argument("--gate", default="all", choices=("hardware", "play", "all"))
    p.add_argument("--allow-synthetic-selftest", action="store_true",
                   help="load synthetic records for tooling self-test only")
    p.add_argument("--no-recheck-latency", action="store_true",
                   help="skip re-deriving latency from the raw WAV")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        session_path = args.session
        if os.path.isdir(session_path):
            session_path = os.path.join(session_path, "session.json")
        session = dl.load_json_strict(session_path)
        if session.get("schema") != dl.SESSION_SCHEMA:
            raise dl.DeviceValidationError("not a session file: %s" % session_path)
    except (dl.DeviceValidationError, OSError, ValueError) as exc:
        sys.stderr.write("error: %s\n" % exc)
        return 1
    session_dir = os.path.dirname(os.path.abspath(session_path))

    errors = []
    records = []
    synthetic_records = []
    interfaces_by_id = {i.get("id"): i for i in (session.get("interfaces") or [])}
    paths = discover_measurements(session_dir, args.measurement)
    for path in paths:
        try:
            rec = dl.load_json_strict(path)
        except (ValueError, OSError) as exc:
            add_hard(errors, path, "json", "unreadable/non-finite JSON: %s" % exc)
            continue
        validated = validate_record(rec, session_dir, path, errors,
                                    not args.no_recheck_latency,
                                    interfaces_by_id)
        if validated is None:
            continue
        if validated["synthetic"]:
            if not args.allow_synthetic_selftest:
                add_hard(errors, path, "synthetic",
                         "synthetic record present without --allow-synthetic-selftest")
                continue
            synthetic_records.append(validated)
        else:
            records.append(validated)

    cells = build_matrix(records, errors)
    errors[:] = _dedupe_errors(errors)
    _relativize_paths(cells, errors, session_dir)
    hardware_statuses = [cells[c]["status"] for c in dl.REQUIRED_HARDWARE_CONDITIONS]
    play_statuses = [cells[c]["status"] for c in dl.PLAY_CONDITIONS]
    matrix_empty = not records
    hardware_matrix_pass = (not errors) and matrix_empty is False \
        and all(s == "pass" for s in hardware_statuses)
    asio_ref = cells.get("asio_48k_128", {})
    monitoring_latency_pass = (not errors) and asio_ref.get("status") == "pass" \
        and asio_ref.get("latency_ms") is not None \
        and asio_ref["latency_ms"] <= TARGET_MS
    play_trials_pass = (not errors) and bool(records) \
        and all(s == "pass" for s in play_statuses)
    selftest_pass = any(r["kind"] == "latency" and r["cell"]["status"] == "pass"
                        for r in synthetic_records)

    gates = {
        "hardware_matrix": {
            "pass": hardware_matrix_pass,
            "required": list(dl.REQUIRED_HARDWARE_CONDITIONS),
            "statuses": {c: cells[c]["status"] for c in dl.REQUIRED_HARDWARE_CONDITIONS},
            "empty": matrix_empty,
        },
        "monitoring_latency": {
            "pass": monitoring_latency_pass,
            "condition": "asio_48k_128",
            "target_ms": TARGET_MS,
            "measured_ms": asio_ref.get("latency_ms"),
            "target_met": asio_ref.get("target_met"),
        },
        "play_trials": {
            "pass": play_trials_pass,
            "required": list(dl.PLAY_CONDITIONS),
            "statuses": {c: cells[c]["status"] for c in dl.PLAY_CONDITIONS},
        },
        "synthetic_selftest": {
            "pass": selftest_pass,
            "loaded": len(synthetic_records),
            "note": "proves tooling only; never counts toward a physical gate",
        },
    }
    if args.gate == "hardware":
        overall = hardware_matrix_pass
    elif args.gate == "play":
        overall = play_trials_pass
    else:
        overall = hardware_matrix_pass and monitoring_latency_pass and play_trials_pass

    report = {
        "schema": REPORT_SCHEMA,
        "generated_utc": dl.utc_now(),
        "session_id": session.get("session_id"),
        "session_source_sha": (session.get("source") or {}).get("git_sha"),
        "gate_requested": args.gate,
        "allow_synthetic_selftest": bool(args.allow_synthetic_selftest),
        "recheck_latency": not args.no_recheck_latency,
        "matrix_empty": matrix_empty,
        "hard_errors": errors,
        "records": [{"path": _rel(session_dir, r["path"]), "schema": r["schema"],
                     "condition": r["condition"], "kind": r["kind"],
                     "synthetic": r["synthetic"],
                     "status": r["cell"]["status"]} for r in records],
        "synthetic_records": [{"path": _rel(session_dir, r["path"]),
                               "condition": r["condition"], "kind": r["kind"],
                               "status": r["cell"]["status"]}
                              for r in synthetic_records],
        "cells": cells,
        "gates": gates,
        "overall_pass": bool(overall and not errors),
        "awaiting_physical_evidence": (not errors) and not records,
    }
    out = args.out or os.path.join(session_dir, "report.json")
    dl.write_json(out, report)
    summary = render_summary(report)
    if args.summary_md:
        with open(args.summary_md, "w", encoding="utf-8") as fh:
            fh.write(summary)
    sys.stdout.write(summary)
    if errors:
        return 1
    return 0 if report["overall_pass"] else 2


def render_summary(report):
    lines = []
    lines.append("# Device-validation report")
    lines.append("")
    lines.append("- session: `%s`" % report["session_id"])
    lines.append("- gate requested: **%s**" % report["gate_requested"])
    lines.append("- overall pass: **%s**" % report["overall_pass"])
    lines.append("- hard errors: **%d**" % len(report["hard_errors"]))
    lines.append("- matrix empty: **%s**" % report["matrix_empty"])
    lines.append("")
    lines.append("## Hardware matrix")
    lines.append("")
    lines.append("| condition | status | latency ms | target ms |")
    lines.append("|---|---|---|---|")
    for cond in dl.REQUIRED_HARDWARE_CONDITIONS:
        cell = report["cells"][cond]
        lines.append("| %s | %s | %s | %s |"
                     % (cond, cell["status"], cell.get("latency_ms"),
                        cell.get("target_ms")))
    lines.append("")
    lines.append("## Play trials")
    lines.append("")
    lines.append("| condition | status | useful-lock |")
    lines.append("|---|---|---|")
    for cond in dl.PLAY_CONDITIONS:
        cell = report["cells"][cond]
        lines.append("| %s | %s | %s |"
                     % (cond, cell["status"], cell.get("verdict")))
    lines.append("")
    lines.append("## Gates")
    lines.append("")
    for name, gate in report["gates"].items():
        lines.append("- **%s**: %s" % (name, "PASS" if gate["pass"] else "FAIL"))
    if report["hard_errors"]:
        lines.append("")
        lines.append("## Hard errors")
        lines.append("")
        for err in report["hard_errors"]:
            lines.append("- `%s`: %s (%s)"
                         % (os.path.basename(err["record"]), err["detail"],
                            err["rule"]))
    lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    sys.exit(main())
