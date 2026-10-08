#!/usr/bin/env python3
"""Ingest one SPEC.md 21.5 functional hardware condition.

Covers the non-latency matrix cells: device disconnect/reconnect, input channel
change, silent input and clipped input. An outcome is only recorded with a raw
receipt (device log, diagnostics trace or recording) hashed from actual bytes.
"""
import argparse
import os
import sys

import device_lib as dl


def build_parser():
    p = argparse.ArgumentParser(
        description="Ingest a functional device-condition observation.")
    p.add_argument("--session", required=True)
    p.add_argument("--condition", required=True, choices=sorted(dl.FUNCTIONAL_CONDITIONS))
    p.add_argument("--outcome", required=True, choices=("pass", "fail"))
    p.add_argument("--receipt", required=True,
                   help="raw log/trace/WAV that shows the observation")
    p.add_argument("--interface-id", default=None)
    p.add_argument("--detail", default=None)
    p.add_argument("--out", default=None)
    p.add_argument("--notes", default=None)
    p.add_argument("--synthetic-selftest", action="store_true")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        path = args.session
        if os.path.isdir(path):
            path = os.path.join(path, "session.json")
        session = dl.load_json_strict(path)
        if session.get("schema") != dl.SESSION_SCHEMA:
            raise dl.DeviceValidationError("not a session file: %s" % path)
        base_dir = os.path.dirname(os.path.abspath(path))
        interfaces = session.get("interfaces") or []
        if not interfaces:
            raise dl.DeviceValidationError("session has no interfaces")
        iface = interfaces[0]
        if args.interface_id:
            matches = [i for i in interfaces if i.get("id") == args.interface_id]
            if not matches:
                raise dl.DeviceValidationError("no interface %r" % args.interface_id)
            iface = matches[0]
        receipt = dl.raw_ref(base_dir, args.receipt, role="functional")
        record = {
            "schema": dl.FUNCTIONAL_SCHEMA,
            "synthetic": bool(args.synthetic_selftest),
            "condition": args.condition,
            "session_id": session.get("session_id"),
            "interface_id": iface.get("id"),
            "generated_utc": dl.utc_now(),
            "provenance": "instrumented-raw",
            "measured": True,
            "outcome": args.outcome,
            "detail": args.detail,
            "identity": {
                "source_sha": (session.get("source") or {}).get("git_sha"),
                "interface": {
                    "os": iface.get("os"),
                    "backend": iface.get("backend"),
                    "driver": iface.get("driver"),
                    "sample_rate": iface.get("sample_rate"),
                    "block_frames": iface.get("block_frames"),
                },
            },
            "receipt": receipt,
            "notes": [args.notes] if args.notes else [],
        }
        out = args.out or os.path.join(
            base_dir, "measurements", "functional-%s.json" % args.condition)
        dl.write_json(out, record)
    except dl.DeviceValidationError as exc:
        sys.stderr.write("error: %s\n" % exc)
        return 1
    print("wrote %s (condition=%s outcome=%s)"
          % (out, args.condition, args.outcome))
    return 0 if args.outcome == "pass" else 3


if __name__ == "__main__":
    sys.exit(main())
