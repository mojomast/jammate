#!/usr/bin/env python3
"""Fail-closed validator for Guitar Companion device-validation evidence.

Correction round 1 (see docs/research/device-validation/CORRECTION-PROTOCOL.md)
closes the false-pass holes in the first review:

  * F1 canonical thresholds: expected rate / channels and the target come from
    the immutable condition spec and session interface, never from the record's
    own ``analysis.params``; a record whose params contradict them is a hard
    error, and ``asio_48k_128`` is always the 12 ms target;
  * F2 measured fields need a finite, correctly-typed value;
  * F3 latency cells require ``monitoring == software-app`` and identity is
    cross-checked including input/output device and monitoring;
  * F4 numeric "measured" values need a parseable receipt whose metric value and
    interface identity the validator re-checks; an unparseable receipt is only
    ``receipt-attested`` and never gates;
  * F5 malformed params are reported as hard errors, not crashes;
  * F6 aggregation is worst-case across ALL observations, so a regression is
    never masked by another passing record;
  * F7 the estimator is polarity-robust and separates nearby distinct paths.

Exit codes: 0 gate passes, 1 hard failure (schema/hash/fabrication/crash),
2 no hard failure but the gate is incomplete or failing.
"""
import argparse
import os
import sys

import device_lib as dl

REPORT_SCHEMA = "device-validation/report/1.0"
TARGET_MS = dl.LATENCY_CONDITIONS["asio_48k_128"]["target_ms"]
DEADLINE_FRACTION = 0.70

CANONICAL_IFACE_KEYS = ("os", "backend", "driver", "input_device",
                        "output_device", "sample_rate", "block_frames",
                        "monitoring")

REQUIRED_KEYS = {
    dl.LATENCY_SCHEMA: ("schema", "synthetic", "condition", "interface_id",
                        "generated_utc", "provenance", "identity", "analysis"),
    dl.FUNCTIONAL_SCHEMA: ("schema", "synthetic", "condition", "interface_id",
                           "generated_utc", "provenance", "measured", "outcome",
                           "identity", "receipt"),
    dl.PLAY_TRIAL_SCHEMA: ("schema", "synthetic", "condition", "interface_id",
                           "generated_utc", "identity", "settings", "timing",
                           "useful_lock", "dropouts", "callback", "judgements",
                           "raw_files"),
}

PLAY_FIELD_SPEC = {
    "timing": {"start_requested_s": "number", "join_heard_s": "number",
               "stop_s": "number"},
    "useful_lock": {"window_bars": "nonneg_int", "time_to_lock_s": "number"},
    "dropouts": {"count": "nonneg_int"},
    "callback": {"p50_ms": "number", "p99_ms": "number",
                 "deadline_misses": "nonneg_int",
                 "analysis_overruns": "nonneg_int"},
}

# A play trial only passes the release gate when these metrics gate.
PLAY_REQUIRED_GATING = (
    ("useful_lock", "window_bars"), ("useful_lock", "time_to_lock_s"),
    ("timing", "start_requested_s"), ("timing", "join_heard_s"),
    ("timing", "stop_s"), ("dropouts", "count"),
    ("callback", "p99_ms"), ("callback", "deadline_misses"),
)


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
        for obs in cell.get("observations", []):
            obs["path"] = _rel(session_dir, obs["path"])


def _typed(value, kind):
    if kind == "nonneg_int":
        return dl.nonneg_int(value)
    return dl.finite(value)


def _values_equal(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if dl.is_number(a) and dl.is_number(b):
        return abs(a - b) <= 1e-9
    return a == b


def check_ref(ref, base_dir, errors, record_path, label):
    if not isinstance(ref, dict):
        add_hard(errors, record_path, "raw-ref-missing", "%s has no reference" % label)
        return False
    if not dl.is_hex64(ref.get("sha256")):
        add_hard(errors, record_path, "raw-ref-hash", "%s sha256 malformed" % label)
        return False
    path = ref.get("path")
    if not path or os.path.isabs(path) or not dl.is_contained(base_dir, path):
        add_hard(errors, record_path, "raw-path-not-contained",
                 "%s path must be session-contained and relative: %r" % (label, path))
        return False
    resolved = dl.resolve_path(base_dir, path)
    if not resolved or not os.path.isfile(resolved):
        add_hard(errors, record_path, "raw-unresolved",
                 "%s not found: %s" % (label, path))
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


def _receipt_cache_get(cache, base_dir, ref):
    key = ref.get("sha256") if isinstance(ref, dict) else None
    if key not in cache:
        cache[key] = dl.load_receipt(base_dir, ref)[0]
    return cache[key]


def cross_check_receipt(receipt, session, iface, record, errors, record_path,
                        label):
    """Receipt session/interface identity must match the session exactly."""
    if receipt.get("session_id") is not None \
            and receipt.get("session_id") != session.get("session_id"):
        add_hard(errors, record_path, "receipt-session-mismatch",
                 "%s session_id %r != %r" % (label, receipt.get("session_id"),
                                             session.get("session_id")))
    if receipt.get("interface_id") is not None \
            and receipt.get("interface_id") != record.get("interface_id"):
        add_hard(errors, record_path, "receipt-interface-mismatch",
                 "%s interface_id %r != record %r"
                 % (label, receipt.get("interface_id"), record.get("interface_id")))
    r_iface = receipt.get("interface")
    if not isinstance(r_iface, dict):
        add_hard(errors, record_path, "receipt-interface-missing",
                 "%s has no interface identity" % label)
        return
    for key in CANONICAL_IFACE_KEYS:
        if r_iface.get(key) != iface.get(key):
            add_hard(errors, record_path, "receipt-identity-mismatch",
                     "%s interface.%s=%r != session %r"
                     % (label, key, r_iface.get(key), iface.get(key)))


def _check_measured_field(group_name, key, kind, group, base_dir, errors,
                          record_path, session, iface, record, cache):
    field = group.get(key)
    if not isinstance(field, dict):
        add_hard(errors, record_path, "field-missing", "%s missing" % key)
        return False, False
    measured = field.get("measured") is True
    value = field.get("value")
    if not measured:
        if value is not None:
            add_hard(errors, record_path, "unmeasured-value",
                     "%s carries a value but is marked unmeasured" % key)
        return False, False
    if not _typed(value, kind):
        add_hard(errors, record_path, "measured-value-type",
                 "%s measured value %r is not a finite %s" % (key, value, kind))
        return False, False
    prov = field.get("provenance")
    if prov not in dl.MEASURED_PROVENANCE:
        add_hard(errors, record_path, "field-provenance",
                 "%s measured without a raw provenance (%r)" % (key, prov))
        return False, False
    if not check_ref(field.get("receipt"), base_dir, errors, record_path,
                     "%s.receipt" % key):
        return False, False
    if prov != "instrumented-raw":
        # receipt-attested: recorded, but it can never gate a release.
        return True, False
    receipt = _receipt_cache_get(cache, base_dir, field.get("receipt"))
    if receipt is None:
        add_hard(errors, record_path, "receipt-not-parseable",
                 "%s receipt is not a parseable %s document" % (key, dl.RECEIPT_SCHEMA))
        return False, False
    cross_check_receipt(receipt, session, iface, record, errors, record_path,
                        "%s.receipt" % key)
    found, rv = dl.receipt_value(receipt, group_name, key)
    if not found:
        add_hard(errors, record_path, "receipt-metric-missing",
                 "receipt lacks metrics.%s.%s" % (group_name, key))
        return False, False
    if not _values_equal(rv, value):
        add_hard(errors, record_path, "receipt-metric-mismatch",
                 "receipt metrics.%s.%s=%r != record %r"
                 % (group_name, key, rv, value))
        return False, False
    return True, True


def validate_latency(record, base_dir, session, iface, errors, record_path,
                     recheck, cache):
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
    stored_reasons = analysis.get("reasons")
    if not isinstance(stored_reasons, list):
        add_hard(errors, record_path, "analysis-reasons", "reasons not a list")
        stored_reasons = []
    stored_lat = analysis.get("physical_roundtrip_latency_ms")
    stored_valid = analysis.get("valid") is True
    if stored_lat is not None and not dl.finite(stored_lat):
        add_hard(errors, record_path, "nonfinite-latency",
                 "physical latency is not finite")

    canonical = dl.canonical_latency_expectations(record.get("condition"), iface)
    params = analysis.get("params")
    problems = dl.validate_latency_params(params)
    for problem in problems:
        add_hard(errors, record_path, "analysis-params-invalid", problem)
    if problems:
        return {"status": "fail", "latency_ms": None,
                "target_ms": canonical["target_ms"], "target_met": None,
                "reasons": ["analysis-params-invalid"]}

    # F1: the record may not move its own threshold, expected rate or channels.
    if params.get("expected_rate") != canonical["expected_rate"]:
        add_hard(errors, record_path, "params-expected-rate-tampered",
                 "params.expected_rate %r != canonical %r"
                 % (params.get("expected_rate"), canonical["expected_rate"]))
    if params.get("target_ms") != canonical["target_ms"]:
        add_hard(errors, record_path, "params-target-ms-tampered",
                 "params.target_ms %r != canonical %r"
                 % (params.get("target_ms"), canonical["target_ms"]))
    canonical_channels = dl.required_channels(params)
    if params.get("expected_channels") != canonical_channels:
        add_hard(errors, record_path, "params-expected-channels-tampered",
                 "params.expected_channels %r != canonical %r"
                 % (params.get("expected_channels"), canonical_channels))

    canonical_params = dict(params)
    canonical_params["expected_rate"] = canonical["expected_rate"]
    canonical_params["target_ms"] = canonical["target_ms"]
    canonical_params["expected_channels"] = canonical_channels

    if recheck:
        try:
            recomputed = dl.analyze_loopback(ref.get("path"), canonical_params,
                                             base_dir=base_dir)
        except (dl.DeviceValidationError, KeyError, TypeError, ValueError,
                ZeroDivisionError, OSError) as exc:
            add_hard(errors, record_path, "analysis-error", str(exc))
            return {"status": "fail", "latency_ms": None,
                    "target_ms": canonical["target_ms"], "target_met": None,
                    "reasons": ["analysis-error"]}
        i_reasons = dl.interface_reasons(record.get("condition"), iface)
        expected_reasons = _dedupe(recomputed["reasons"] + i_reasons)
        expected_lat = recomputed["physical_roundtrip_latency_ms"]
        if expected_reasons:
            expected_lat = None
        target_ms = canonical["target_ms"]
        expected_target = None if (expected_lat is None or target_ms is None) \
            else expected_lat <= target_ms
        if list(stored_reasons) != expected_reasons:
            add_hard(errors, record_path, "latency-recheck-reasons",
                     "stored reasons %s != recomputed %s"
                     % (stored_reasons, expected_reasons))
        if stored_lat != expected_lat:
            add_hard(errors, record_path, "latency-recheck-value",
                     "stored latency %r != recomputed %r" % (stored_lat, expected_lat))
        expected_valid = expected_lat is not None
        if stored_valid != expected_valid:
            add_hard(errors, record_path, "latency-recheck-valid",
                     "stored valid %s != recomputed %s"
                     % (stored_valid, expected_valid))
        if analysis.get("target_met") != expected_target:
            add_hard(errors, record_path, "latency-recheck-target",
                     "stored target_met %r != recomputed %r"
                     % (analysis.get("target_met"), expected_target))
        effective_lat = expected_lat
        effective_reasons = expected_reasons
        effective_target = expected_target
    else:
        effective_lat = stored_lat if stored_valid else None
        effective_reasons = stored_reasons
        effective_target = analysis.get("target_met")

    if effective_lat is not None and canonical["target_ms"] is not None \
            and effective_lat > canonical["target_ms"]:
        status = "fail"
    elif effective_lat is not None:
        status = "pass"
    else:
        status = "fail"
    return {"status": status, "latency_ms": effective_lat,
            "target_ms": canonical["target_ms"], "target_met": effective_target,
            "reasons": effective_reasons}


def _dedupe(seq):
    seen = set()
    out = []
    for item in seq:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def validate_functional(record, base_dir, session, iface, errors, record_path,
                        cache):
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
    receipt = _receipt_cache_get(cache, base_dir, record.get("receipt"))
    if receipt is None:
        add_hard(errors, record_path, "receipt-not-parseable",
                 "functional receipt is not a parseable %s document" % dl.RECEIPT_SCHEMA)
        return {"status": "fail", "outcome": outcome}
    cross_check_receipt(receipt, session, iface, record, errors, record_path,
                        "receipt")
    found, value = dl.receipt_value(receipt, "functional", "outcome")
    if not found:
        add_hard(errors, record_path, "receipt-metric-missing",
                 "receipt lacks metrics.functional.outcome")
        return {"status": "fail", "outcome": outcome}
    if value != outcome:
        add_hard(errors, record_path, "receipt-metric-mismatch",
                 "receipt outcome %r != record %r" % (value, outcome))
        return {"status": "fail", "outcome": outcome}
    return {"status": "pass" if outcome == "pass" else "fail", "outcome": outcome}


def validate_play(record, base_dir, session, iface, errors, record_path, cache):
    for path in record.get("raw_files") or []:
        check_ref(path, base_dir, errors, record_path, "raw_files")
    settings = record.get("settings") or {}
    for key in ("intensity", "complexity", "fill_amount", "follow_tightness",
                "tempo_bpm"):
        if not dl.finite(settings.get(key)):
            add_hard(errors, record_path, "settings-missing",
                     "setting %s missing/non-finite" % key)
    judgements = record.get("judgements") or {}
    if judgements.get("provenance") != "operator-report":
        add_hard(errors, record_path, "judgements-provenance",
                 "judgements must be operator-report")

    gating = {}
    timing = record.get("timing") or {}
    lock = record.get("useful_lock") or {}
    drop = record.get("dropouts") or {}
    callback = record.get("callback") or {}
    groups = {"timing": timing, "useful_lock": lock, "dropouts": drop,
              "callback": callback}
    for group_name, fields in PLAY_FIELD_SPEC.items():
        for key, kind in fields.items():
            m, g = _check_measured_field(group_name, key, kind, groups[group_name],
                                         base_dir, errors, record_path, session,
                                         iface, record, cache)
            gating[(group_name, key)] = g

    verdict = lock.get("verdict")
    if verdict not in ("yes", "no", "unknown"):
        add_hard(errors, record_path, "play-verdict", "bad useful_lock verdict")
    # A verdict of yes without gated lock metrics simply cannot pass (it is
    # reported as unmeasured below); it is not a fabrication hard error.

    # Timing must be ordered when all three are gated measurements.
    if all(gating.get(("timing", k)) for k in
           ("start_requested_s", "join_heard_s", "stop_s")):
        t = [timing[k]["value"] for k in
             ("start_requested_s", "join_heard_s", "stop_s")]
        if not (t[0] <= t[1] <= t[2]):
            add_hard(errors, record_path, "play-timing-order",
                     "timing not ordered: %s" % t)

    # F3/18.2: callback p99 must be a gated measurement within 70% of a block.
    # A measured over-deadline or missing payment is a FAILED GATE (like an
    # over-target latency), not a fabrication hard error.
    block_ms = None
    if dl.nonneg_int(iface.get("block_frames")) and iface.get("sample_rate"):
        block_ms = 1000.0 * iface["block_frames"] / iface["sample_rate"]
    cb_state = None  # None = no gated measurement, True = within, False = over
    p99 = callback.get("p99_ms") or {}
    if p99.get("gating") and dl.finite(p99.get("value")) and block_ms:
        cb_state = p99["value"] <= DEADLINE_FRACTION * block_ms
    dm = callback.get("deadline_misses") or {}
    dm_fail = (dm.get("gating") and dl.nonneg_int(dm.get("value"))
               and dm["value"] != 0)

    required_ok = all(gating.get(k) for k in PLAY_REQUIRED_GATING)
    if verdict == "no":
        status = "fail"
    elif verdict == "yes" and required_ok:
        if dm_fail or cb_state is False:
            status = "fail"
        elif cb_state is True:
            status = "pass"
        else:
            status = "unmeasured"
    else:
        status = "unmeasured"
    return {"status": status, "verdict": verdict, "gating": gating,
            "callback_gate": (cb_state is True and not dm_fail),
            "block_ms": block_ms}


def cross_check_identity(record, interfaces_by_id, errors, record_path):
    iid = record.get("interface_id")
    iface = interfaces_by_id.get(iid)
    if iface is None:
        add_hard(errors, record_path, "interface-unknown",
                 "interface_id %r not in session" % iid)
        return None
    identity = (record.get("identity") or {}).get("interface") or {}
    for key in CANONICAL_IFACE_KEYS:
        if identity.get(key) != iface.get(key):
            add_hard(errors, record_path, "interface-identity-mismatch",
                     "%s %r != session %r" % (key, identity.get(key), iface.get(key)))
    return iface


def _require_keys(record, schema, errors, record_path):
    for key in REQUIRED_KEYS.get(schema, ()):
        if key not in record:
            add_hard(errors, record_path, "missing-key", "required key %r absent" % key)


def validate_record(record, base_dir, record_path, errors, recheck,
                    interfaces_by_id, session, cache):
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
    _require_keys(record, schema, errors, record_path)

    iface = cross_check_identity(record, interfaces_by_id, errors, record_path)
    if iface is None:
        iface = {"id": record.get("interface_id"), "os": None, "backend": None,
                 "driver": None, "input_device": None, "output_device": None,
                 "sample_rate": None, "block_frames": None, "monitoring": None}

    session_synthetic = session.get("synthetic") is True
    effective_synthetic = record.get("synthetic") is True or session_synthetic

    # F-source: physical identity must carry a 40-hex source SHA.
    source_sha = (record.get("identity") or {}).get("source_sha")
    if not effective_synthetic and not dl.is_git_sha(source_sha):
        add_hard(errors, record_path, "source-sha",
                 "physical record needs a 40-hex source_sha, got %r" % source_sha)

    if schema == dl.LATENCY_SCHEMA:
        if condition not in dl.LATENCY_CONDITIONS:
            add_hard(errors, record_path, "condition", "unknown latency condition")
        cell = validate_latency(record, base_dir, session, iface, errors,
                                record_path, recheck, cache)
        kind = "latency"
    elif schema == dl.FUNCTIONAL_SCHEMA:
        if condition not in dl.FUNCTIONAL_CONDITIONS:
            add_hard(errors, record_path, "condition", "unknown functional condition")
        cell = validate_functional(record, base_dir, session, iface, errors,
                                   record_path, cache)
        kind = "functional"
    elif schema == dl.PLAY_TRIAL_SCHEMA:
        if condition not in dl.PLAY_CONDITIONS:
            add_hard(errors, record_path, "condition", "unknown play condition")
        cell = validate_play(record, base_dir, session, iface, errors,
                             record_path, cache)
        kind = "play"
    else:
        add_hard(errors, record_path, "schema", "session file in measurements")
        return None
    return {"path": record_path, "schema": schema, "condition": condition,
            "kind": kind, "synthetic": effective_synthetic, "cell": cell}


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


def build_matrix(records):
    """Worst-case aggregation across ALL observations of a condition (F6)."""
    cells = {}
    for cond in dl.REQUIRED_HARDWARE_CONDITIONS:
        cells[cond] = {"status": "missing", "kind": dl.condition_kind(cond),
                       "records": [], "observations": [], "conflicts": [],
                       "latency_ms": None, "target_ms": None,
                       "target_met": None, "reasons": []}
    for cond in dl.PLAY_CONDITIONS:
        cells[cond] = {"status": "missing", "kind": "play", "records": [],
                       "observations": [], "conflicts": [], "verdict": None,
                       "callback_gate": None}
    for rec in records:
        if rec["synthetic"]:
            continue
        cond = rec["condition"]
        if cond not in cells:
            continue
        cell = cells[cond]
        status = rec["cell"]["status"]
        cell["records"].append(rec["path"])
        obs = {"path": rec["path"], "status": status}
        if cond in dl.LATENCY_CONDITIONS:
            obs["latency_ms"] = rec["cell"].get("latency_ms")
            obs["target_ms"] = rec["cell"].get("target_ms")
            obs["target_met"] = rec["cell"].get("target_met")
            cell["latency_ms"] = rec["cell"].get("latency_ms")
            cell["target_ms"] = rec["cell"].get("target_ms")
            cell["target_met"] = rec["cell"].get("target_met")
            cell["reasons"] = rec["cell"].get("reasons", [])
        if cond in dl.PLAY_CONDITIONS:
            obs["verdict"] = rec["cell"].get("verdict")
            obs["callback_gate"] = rec["cell"].get("callback_gate")
            cell["verdict"] = rec["cell"].get("verdict")
            cell["callback_gate"] = rec["cell"].get("callback_gate")
        cell["observations"].append(obs)
        if status != "pass":
            cell["conflicts"].append(obs)
        if status == "fail":
            cell["status"] = "fail"
        elif status == "unmeasured" and cell["status"] != "fail":
            cell["status"] = "unmeasured"
        elif status == "pass" and cell["status"] == "missing":
            cell["status"] = "pass"
    return cells


def build_parser():
    p = argparse.ArgumentParser(
        description="Validate device-validation evidence and physical gates.")
    p.add_argument("--session", required=True)
    p.add_argument("--measurement", action="append", default=[])
    p.add_argument("--out", default=None)
    p.add_argument("--summary-md", default=None)
    p.add_argument("--gate", default="all", choices=("hardware", "play", "all"))
    p.add_argument("--allow-synthetic-selftest", action="store_true")
    p.add_argument("--no-recheck-latency", action="store_true")
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
    cache = {}
    interfaces_by_id = {i.get("id"): i for i in (session.get("interfaces") or [])}
    session_synthetic = session.get("synthetic") is True
    if not session_synthetic and not dl.is_git_sha((session.get("source") or {}).get("git_sha")):
        add_hard(errors, session_path, "session-source-sha",
                 "non-synthetic session needs a 40-hex source.git_sha")

    paths = discover_measurements(session_dir, args.measurement)
    for path in paths:
        try:
            rec = dl.load_json_strict(path)
        except (ValueError, OSError) as exc:
            add_hard(errors, path, "json", "unreadable/non-finite JSON: %s" % exc)
            continue
        try:
            validated = validate_record(rec, session_dir, path, errors,
                                        not args.no_recheck_latency,
                                        interfaces_by_id, session, cache)
        except Exception as exc:  # never crash on a hostile record
            add_hard(errors, path, "validator-crash", "%s: %s"
                     % (type(exc).__name__, exc))
            continue
        if validated is None:
            continue
        if validated["synthetic"]:
            if not args.allow_synthetic_selftest:
                add_hard(errors, path, "synthetic",
                         "synthetic record/session present without "
                         "--allow-synthetic-selftest")
                continue
            synthetic_records.append(validated)
        else:
            records.append(validated)

    cells = build_matrix(records)
    errors[:] = _dedupe_errors(errors)
    _relativize_paths(cells, errors, session_dir)

    hardware_statuses = [cells[c]["status"] for c in dl.REQUIRED_HARDWARE_CONDITIONS]
    play_statuses = [cells[c]["status"] for c in dl.PLAY_CONDITIONS]
    matrix_empty = not records
    hardware_matrix_pass = (not errors) and not matrix_empty \
        and all(s == "pass" for s in hardware_statuses)
    asio_ref = cells.get("asio_48k_128", {})
    monitoring_latency_pass = (not errors) and asio_ref.get("status") == "pass" \
        and asio_ref.get("latency_ms") is not None \
        and asio_ref["latency_ms"] <= TARGET_MS
    play_trials_pass = (not errors) and not matrix_empty \
        and all(s == "pass" for s in play_statuses)
    play_records = [r for r in records if r["kind"] == "play"]
    callback_deadline_pass = (not errors) and bool(play_records) and all(
        r["cell"].get("callback_gate") is True for r in play_records)
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
        "callback_deadline": {
            "pass": callback_deadline_pass,
            "fraction_of_block": DEADLINE_FRACTION,
            "play_records": len(play_records),
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
        overall = play_trials_pass and callback_deadline_pass
    else:
        overall = (hardware_matrix_pass and monitoring_latency_pass
                   and play_trials_pass and callback_deadline_pass)

    report = {
        "schema": REPORT_SCHEMA,
        "generated_utc": dl.utc_now(),
        "session_id": session.get("session_id"),
        "session_source_sha": (session.get("source") or {}).get("git_sha"),
        "session_synthetic": session_synthetic,
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
        "awaiting_physical_evidence": (not errors) and matrix_empty,
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
    lines.append("| condition | status | latency ms | target ms | observations |")
    lines.append("|---|---|---|---|---|")
    for cond in dl.REQUIRED_HARDWARE_CONDITIONS:
        cell = report["cells"][cond]
        lines.append("| %s | %s | %s | %s | %d |"
                     % (cond, cell["status"], cell.get("latency_ms"),
                        cell.get("target_ms"), len(cell.get("observations", []))))
    lines.append("")
    lines.append("## Play trials")
    lines.append("")
    lines.append("| condition | status | useful-lock | observations |")
    lines.append("|---|---|---|---|")
    for cond in dl.PLAY_CONDITIONS:
        cell = report["cells"][cond]
        lines.append("| %s | %s | %s | %d |"
                     % (cond, cell["status"], cell.get("verdict"),
                        len(cell.get("observations", []))))
    lines.append("")
    lines.append("## Gates")
    lines.append("")
    for name, gate in report["gates"].items():
        lines.append("- **%s**: %s" % (name, "PASS" if gate["pass"] else "FAIL"))
    conflicts = {c: report["cells"][c]["conflicts"]
                 for c in report["cells"] if report["cells"][c]["conflicts"]}
    if conflicts:
        lines.append("")
        lines.append("## Conflicting observations (worst status wins)")
        lines.append("")
        for cond, obs in conflicts.items():
            for o in obs:
                lines.append("- `%s` -> %s (%s)" % (cond, o["status"],
                                                   os.path.basename(o["path"])))
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
