#!/usr/bin/env python3
"""Ingest explicit Guitar Companion play-trial evidence.

A play trial is a human Jam session: settings, timing, useful-lock window,
dropouts, callback load and the SPEC.md section 20 judgements. Nothing is
invented. A numeric timing/dropout/callback/lock value is only accepted as
MEASURED when a raw receipt backs it; otherwise the value must be omitted (it
is recorded as unmeasured). Qualitative judgements are always labelled
``operator-report`` and are never presented as measured receipts.
"""
import argparse
import os
import sys

import device_lib as dl


def build_parser():
    p = argparse.ArgumentParser(
        description="Ingest explicit play-trial evidence into a record.")
    p.add_argument("--session", required=True)
    p.add_argument("--condition", required=True, choices=sorted(dl.PLAY_CONDITIONS))
    p.add_argument("--interface-id", default=None)
    p.add_argument("--out", default=None)
    # Configured settings (declared, not measured).
    p.add_argument("--style", default=None)
    p.add_argument("--jam-mode", default=None)
    p.add_argument("--intensity", type=float, default=None)
    p.add_argument("--complexity", type=float, default=None)
    p.add_argument("--fill-amount", type=float, default=None)
    p.add_argument("--follow-tightness", type=float, default=None)
    p.add_argument("--meter", default=None)
    p.add_argument("--tempo-bpm", type=float, default=None)
    # Receipts (raw files that back measured numbers).
    p.add_argument("--timing-receipt", default=None)
    p.add_argument("--lock-receipt", default=None)
    p.add_argument("--dropout-receipt", default=None)
    p.add_argument("--callback-receipt", default=None)
    p.add_argument("--trace", action="append", default=[],
                   help="additional raw diagnostics trace(s) to hash (repeatable)")
    # Measured timing (seconds).
    p.add_argument("--start-requested-s", type=float, default=None)
    p.add_argument("--join-heard-s", type=float, default=None)
    p.add_argument("--stop-s", type=float, default=None)
    # Useful lock.
    p.add_argument("--useful-lock", choices=("yes", "no", "unknown"),
                   default="unknown")
    p.add_argument("--lock-window-bars", type=int, default=None)
    p.add_argument("--time-to-lock-s", type=float, default=None)
    # Dropouts.
    p.add_argument("--dropouts", type=int, default=None)
    # Callback evidence.
    p.add_argument("--callback-p50-ms", type=float, default=None)
    p.add_argument("--callback-p99-ms", type=float, default=None)
    p.add_argument("--callback-deadline-misses", type=int, default=None)
    p.add_argument("--analysis-overruns", type=int, default=None)
    # Operator judgements (SPEC 20). Always operator-report.
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


def measured(value, receipt, label):
    """Wrap a numeric as measured only when a receipt is present."""
    if value is None:
        return {"value": None, "measured": False, "reason": "not-supplied"}
    if receipt is None:
        raise dl.DeviceValidationError(
            "%s was supplied without a raw receipt; omit it (unmeasured) or "
            "pass the matching --*-receipt" % label)
    return {"value": value, "measured": True,
            "provenance": "instrumented-raw", "receipt": receipt}


def make_record(session, base_dir, condition, interface, values, receipts,
                warnings=None, synthetic=False):
    timing_receipt = receipts.get("timing")
    lock_receipt = receipts.get("lock") or timing_receipt
    drop_receipt = receipts.get("dropout")
    cb_receipt = receipts.get("callback")

    timing = {
        "start_requested_s": measured(values.get("start_requested_s"),
                                      timing_receipt, "start-requested-s"),
        "join_heard_s": measured(values.get("join_heard_s"), timing_receipt,
                                 "join-heard-s"),
        "stop_s": measured(values.get("stop_s"), timing_receipt, "stop-s"),
        "receipt": timing_receipt,
    }
    useful_lock = {
        "verdict": values.get("useful_lock"),
        "provenance": "operator-report",
        "window_bars": measured(values.get("lock_window_bars"), lock_receipt,
                                "lock-window-bars"),
        "time_to_lock_s": measured(values.get("time_to_lock_s"), lock_receipt,
                                   "time-to-lock-s"),
        "receipt": lock_receipt,
    }
    if values.get("useful_lock") == "yes" and lock_receipt is None:
        raise dl.DeviceValidationError(
            "useful-lock=yes requires a raw lock/timing receipt proving the "
            "two-bar window; a bare assertion is not evidence")

    callback = {
        "p50_ms": measured(values.get("callback_p50_ms"), cb_receipt,
                           "callback-p50-ms"),
        "p99_ms": measured(values.get("callback_p99_ms"), cb_receipt,
                           "callback-p99-ms"),
        "deadline_misses": measured(values.get("callback_deadline_misses"),
                                    cb_receipt, "callback-deadline-misses"),
        "analysis_overruns": measured(values.get("analysis_overruns"),
                                      cb_receipt, "analysis-overruns"),
        "receipt": cb_receipt,
    }
    judgements = {
        "join_sensible": values.get("join_sensible"),
        "stayed_stable": values.get("stayed_stable"),
        "overreacted": values.get("overreacted"),
        "fills_musical_enough": values.get("fills_musical"),
        "push_pull_possible": values.get("push_pull"),
        "recovered_tap_resync": values.get("recovered_tap_resync"),
    }

    raw_files = [r for r in receipts.values() if r]
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
            "count": measured(values.get("dropouts"), drop_receipt, "dropouts"),
            "receipt": drop_receipt,
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
        receipts = {
            "timing": dl.raw_ref(base_dir, args.timing_receipt, role="timing"),
            "lock": dl.raw_ref(base_dir, args.lock_receipt, role="lock"),
            "dropout": dl.raw_ref(base_dir, args.dropout_receipt, role="dropout"),
            "callback": dl.raw_ref(base_dir, args.callback_receipt, role="callback"),
        }
        for extra in args.trace:
            receipts.setdefault("trace", dl.raw_ref(base_dir, extra, role="trace"))
        warnings = []
        values = vars(args)
        record = make_record(session, base_dir, args.condition, interface,
                             values, receipts, warnings, args.synthetic_selftest)
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
