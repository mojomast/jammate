#!/usr/bin/env python3
"""Ingest explicit Guitar Companion play-trial evidence.

A play trial is a human Jam session: settings, timing, useful-lock window,
dropouts, callback load and the SPEC.md section 20 judgements. Nothing is
invented.

Measured-number policy (fail closed):

* a numeric timing/dropout/callback/lock value is accepted as ``measured`` only
  when a raw receipt backs it; with no receipt the value must be omitted;
* a *parseable* receipt (``device-validation/receipt/1.0``) that actually carries
  the same metric value is ``instrumented-raw`` and *gates*;
* a hashed but unparseable receipt is recorded as ``receipt-attested`` and never
  gates a release;
* the six qualitative judgements are always ``operator-report`` and are never
  presented as measured receipts.
"""
import argparse
import os
import sys

import device_lib as dl

# group -> {key: kind}
PLAY_METRICS = {
    "timing": {"start_requested_s": "number", "join_heard_s": "number",
               "stop_s": "number"},
    "useful_lock": {"window_bars": "nonneg_int", "time_to_lock_s": "number"},
    "dropouts": {"count": "nonneg_int"},
    "callback": {"p50_ms": "number", "p99_ms": "number",
                 "deadline_misses": "nonneg_int",
                 "analysis_overruns": "nonneg_int"},
}


def build_parser():
    p = argparse.ArgumentParser(
        description="Ingest explicit play-trial evidence into a record.")
    p.add_argument("--session", required=True)
    p.add_argument("--condition", required=True, choices=sorted(dl.PLAY_CONDITIONS))
    p.add_argument("--interface-id", default=None)
    p.add_argument("--out", default=None)
    p.add_argument("--style", default=None)
    p.add_argument("--jam-mode", default=None)
    p.add_argument("--intensity", type=float, default=None)
    p.add_argument("--complexity", type=float, default=None)
    p.add_argument("--fill-amount", type=float, default=None)
    p.add_argument("--follow-tightness", type=float, default=None)
    p.add_argument("--meter", default=None)
    p.add_argument("--tempo-bpm", type=float, default=None)
    p.add_argument("--timing-receipt", default=None)
    p.add_argument("--lock-receipt", default=None)
    p.add_argument("--dropout-receipt", default=None)
    p.add_argument("--callback-receipt", default=None)
    p.add_argument("--trace", action="append", default=[])
    p.add_argument("--start-requested-s", type=float, default=None)
    p.add_argument("--join-heard-s", type=float, default=None)
    p.add_argument("--stop-s", type=float, default=None)
    p.add_argument("--useful-lock", choices=("yes", "no", "unknown"),
                   default="unknown")
    p.add_argument("--lock-window-bars", type=int, default=None)
    p.add_argument("--time-to-lock-s", type=float, default=None)
    p.add_argument("--dropouts", type=int, default=None)
    p.add_argument("--callback-p50-ms", type=float, default=None)
    p.add_argument("--callback-p99-ms", type=float, default=None)
    p.add_argument("--callback-deadline-misses", type=int, default=None)
    p.add_argument("--analysis-overruns", type=int, default=None)
    for q in ("join-sensible", "stayed-stable", "overreacted",
              "fills-musical", "push-pull", "recovered-tap-resync"):
        p.add_argument("--%s" % q, choices=("yes", "no", "unclear"), default=None)
    p.add_argument("--operator", default=None)
    p.add_argument("--notes", default=None)
    p.add_argument("--synthetic-selftest", action="store_true")
    return p


def load_session(path):
    if os.path.isdir(path):
        path = os.path.join(path, "session.json")
    session = dl.load_json_strict(path)
    if session.get("schema") != dl.SESSION_SCHEMA:
        raise dl.DeviceValidationError("not a %s file: %s"
                                       % (dl.SESSION_SCHEMA, path))
    return path, session


def find_interface(session, interface_id):
    interfaces = session.get("interfaces") or []
    if not interfaces:
        raise dl.DeviceValidationError("session has no interfaces")
    if interface_id is None:
        return interfaces[0]
    for iface in interfaces:
        if iface.get("id") == interface_id:
            return iface
    raise dl.DeviceValidationError("no interface %r in session" % interface_id)


def _values_equal(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if dl.is_number(a) and dl.is_number(b):
        return abs(a - b) <= 1e-9
    return a == b


def _validate_value(value, kind, label):
    if kind == "nonneg_int":
        if not dl.nonneg_int(value):
            raise dl.DeviceValidationError("%s must be a non-negative integer" % label)
    else:
        if not dl.finite(value):
            raise dl.DeviceValidationError("%s must be a finite number" % label)


def measured(value, receipt_ref, receipt_doc, group, key, kind):
    """Wrap a numeric with its gating provenance (see module docstring)."""
    if value is None:
        return {"value": None, "measured": False, "gating": False,
                "reason": "not-supplied"}
    _validate_value(value, kind, key)
    if receipt_ref is None:
        raise dl.DeviceValidationError(
            "%s was supplied without a raw receipt; omit it (unmeasured) or "
            "pass the matching --*-receipt" % key)
    out = {"value": value, "measured": True, "gating": False,
           "receipt": receipt_ref}
    if receipt_doc is None:
        out["provenance"] = "receipt-attested"
        out["reason"] = "receipt-unparseable"
        return out
    found, rv = dl.receipt_value(receipt_doc, group, key)
    if not found:
        raise dl.DeviceValidationError(
            "receipt for %s lacks metrics.%s.%s" % (key, group, key))
    if not _values_equal(rv, value):
        raise dl.DeviceValidationError(
            "receipt metrics.%s.%s=%r does not match supplied %r"
            % (group, key, rv, value))
    out["provenance"] = "instrumented-raw"
    out["gating"] = True
    return out


def make_record(session, base_dir, condition, interface, values, receipt_refs,
                receipt_docs, warnings=None, synthetic=False):
    timing_ref = receipt_refs.get("timing")
    lock_ref = receipt_refs.get("lock") or timing_ref
    lock_doc = receipt_docs.get("lock") or receipt_docs.get("timing")
    drop_ref = receipt_refs.get("dropout")
    drop_doc = receipt_docs.get("dropout")
    cb_ref = receipt_refs.get("callback")
    cb_doc = receipt_docs.get("callback")

    timing = {
        "start_requested_s": measured(values.get("start_requested_s"),
                                      timing_ref, receipt_docs.get("timing"),
                                      "timing", "start_requested_s", "number"),
        "join_heard_s": measured(values.get("join_heard_s"), timing_ref,
                                 receipt_docs.get("timing"), "timing",
                                 "join_heard_s", "number"),
        "stop_s": measured(values.get("stop_s"), timing_ref,
                           receipt_docs.get("timing"), "timing", "stop_s",
                           "number"),
        "receipt": timing_ref,
    }
    useful_lock = {
        "verdict": values.get("useful_lock"),
        "provenance": "operator-report",
        "window_bars": measured(values.get("lock_window_bars"), lock_ref,
                                lock_doc, "useful_lock", "window_bars",
                                "nonneg_int"),
        "time_to_lock_s": measured(values.get("time_to_lock_s"), lock_ref,
                                   lock_doc, "useful_lock", "time_to_lock_s",
                                   "number"),
        "receipt": lock_ref,
    }
    if values.get("useful_lock") == "yes" and lock_ref is None:
        raise dl.DeviceValidationError(
            "useful-lock=yes requires a raw lock/timing receipt proving the "
            "two-bar window; a bare assertion is not evidence")

    callback = {
        "p50_ms": measured(values.get("callback_p50_ms"), cb_ref, cb_doc,
                           "callback", "p50_ms", "number"),
        "p99_ms": measured(values.get("callback_p99_ms"), cb_ref, cb_doc,
                           "callback", "p99_ms", "number"),
        "deadline_misses": measured(values.get("callback_deadline_misses"),
                                    cb_ref, cb_doc, "callback",
                                    "deadline_misses", "nonneg_int"),
        "analysis_overruns": measured(values.get("analysis_overruns"),
                                      cb_ref, cb_doc, "callback",
                                      "analysis_overruns", "nonneg_int"),
        "receipt": cb_ref,
    }
    judgements = {
        "join_sensible": values.get("join_sensible"),
        "stayed_stable": values.get("stayed_stable"),
        "overreacted": values.get("overreacted"),
        "fills_musical_enough": values.get("fills_musical"),
        "push_pull_possible": values.get("push_pull"),
        "recovered_tap_resync": values.get("recovered_tap_resync"),
        "provenance": "operator-report",
    }

    raw_files = [r for r in receipt_refs.values() if r]
    record = {
        "schema": dl.PLAY_TRIAL_SCHEMA,
        "synthetic": bool(synthetic),
        "condition": condition,
        "session_id": session.get("session_id"),
        "interface_id": interface.get("id"),
        "generated_utc": dl.utc_now(),
        "identity": {
            "source_sha": (session.get("source") or {}).get("git_sha"),
            "build_kind": (session.get("source") or {}).get("build_kind"),
            "interface": {
                "os": interface.get("os"),
                "backend": interface.get("backend"),
                "driver": interface.get("driver"),
                "input_device": interface.get("input_device"),
                "output_device": interface.get("output_device"),
                "sample_rate": interface.get("sample_rate"),
                "block_frames": interface.get("block_frames"),
                "monitoring": interface.get("monitoring"),
            },
        },
        "settings": {
            "style": values.get("style"),
            "jam_mode": values.get("jam_mode"),
            "intensity": values.get("intensity"),
            "complexity": values.get("complexity"),
            "fill_amount": values.get("fill_amount"),
            "follow_tightness": values.get("follow_tightness"),
            "meter": values.get("meter"),
            "tempo_bpm": values.get("tempo_bpm"),
        },
        "timing": timing,
        "useful_lock": useful_lock,
        "dropouts": {
            "count": measured(values.get("dropouts"), drop_ref, drop_doc,
                              "dropouts", "count", "nonneg_int"),
            "receipt": drop_ref,
        },
        "callback": callback,
        "judgements": judgements,
        "raw_files": raw_files,
        "warnings": warnings or [],
        "operator": values.get("operator"),
        "notes": [values["notes"]] if values.get("notes") else [],
    }
    return record


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        session_path, session = load_session(args.session)
        base_dir = os.path.dirname(os.path.abspath(session_path))
        interface = find_interface(session, args.interface_id)
        receipt_refs = {
            "timing": dl.raw_ref(base_dir, args.timing_receipt, role="timing"),
            "lock": dl.raw_ref(base_dir, args.lock_receipt, role="lock"),
            "dropout": dl.raw_ref(base_dir, args.dropout_receipt, role="dropout"),
            "callback": dl.raw_ref(base_dir, args.callback_receipt, role="callback"),
        }
        for extra in args.trace:
            receipt_refs.setdefault("trace", dl.raw_ref(base_dir, extra, role="trace"))
        receipt_docs = {group: dl.load_receipt(base_dir, ref)[0]
                        for group, ref in receipt_refs.items()}
        warnings = []
        values = vars(args)
        record = make_record(session, base_dir, args.condition, interface,
                             values, receipt_refs, receipt_docs, warnings,
                             args.synthetic_selftest)
        out = args.out or os.path.join(
            base_dir, "measurements", "play-%s.json" % args.condition)
        dl.write_json(out, record)
    except dl.DeviceValidationError as exc:
        sys.stderr.write("error: %s\n" % exc)
        return 1
    print("wrote %s" % out)
    print("  condition=%s useful_lock=%s" % (record["condition"],
                                             record["useful_lock"]["verdict"]))
    print("  timing measured=%s callback measured=%s dropouts measured=%s"
          % (event_measured(record["timing"], ("start_requested_s", "join_heard_s", "stop_s")),
             event_measured(record["callback"], ("p50_ms", "p99_ms", "deadline_misses", "analysis_overruns")),
             record["dropouts"]["count"]["measured"]))
    return 0


def event_measured(group, keys):
    return any(group[k]["measured"] for k in keys)


if __name__ == "__main__":
    sys.exit(main())
