#!/usr/bin/env python3
"""One-command initialise of a device-validation session / evidence manifest.

Creates a self-describing session directory::

    <out>/session.json          session + interface/config identity (no measurements)
    <out>/raw/                  drop recorded WAVs / receipts here
    <out>/measurements/         ingest tools write validated records here
    <out>/README.txt            how to attach evidence

The manifest records product/build/interface/driver/rate/block identity and an
EMPTY measurement list. It never marks anything measured. Re-running refuses a
non-empty directory unless --force is given.
"""
import argparse
import os
import shutil
import sys

import device_lib as dl


def build_parser():
    p = argparse.ArgumentParser(
        description="Initialise a device-validation session directory.")
    p.add_argument("--out", required=True,
                   help="session directory to create (must be empty unless --force)")
    p.add_argument("--session-id", default=None,
                   help="stable session id (default: directory name)")
    p.add_argument("--source-sha", default=None,
                   help="Guitar Companion git SHA the build was made from")
    p.add_argument("--build-kind", default="release",
                   choices=("release", "debug", "relwithdebinfo", "unknown"))
    p.add_argument("--machine", default=None,
                   help="free-form machine label (hostname by default)")
    p.add_argument("--os", default=None, choices=dl.KNOWN_OS,
                   help="operating system of the host running Guitar Companion")
    p.add_argument("--interface-id", default="iface-0")
    p.add_argument("--backend", default="unknown", choices=dl.KNOWN_BACKENDS)
    p.add_argument("--driver", default=None,
                   help="actual driver name, e.g. 'Focusrite USB ASIO'")
    p.add_argument("--input", default=None, dest="input_device")
    p.add_argument("--output", default=None, dest="output_device")
    p.add_argument("--sample-rate", type=int, default=48000)
    p.add_argument("--block", type=int, default=128, dest="block_frames")
    p.add_argument("--input-channels", type=int, default=2)
    p.add_argument("--output-channels", type=int, default=2)
    p.add_argument("--monitoring", default="software-app",
                   choices=("software-app", "direct-hardware", "unknown"),
                   help="how the guitar is monitored (affects what a loopback measures)")
    p.add_argument("--reported-input-latency-ms", type=float, default=None)
    p.add_argument("--reported-output-latency-ms", type=float, default=None)
    p.add_argument("--condition", action="append", default=[],
                   choices=dl.REQUIRED_HARDWARE_CONDITIONS + dl.PLAY_CONDITIONS,
                   help="declare a target condition (repeatable); informational")
    p.add_argument("--notes", default=None)
    p.add_argument("--force", action="store_true",
                   help="overwrite a non-empty target directory")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    out = os.path.abspath(args.out)
    if os.path.exists(out) and os.listdir(out) and not args.force:
        sys.stderr.write("refusing non-empty --out %s (use --force)\n" % out)
        return 2
    if args.force and os.path.isdir(out):
        for name in os.listdir(out):
            target = os.path.join(out, name)
            if os.path.isdir(target):
                shutil.rmtree(target)
            else:
                os.remove(target)
    os.makedirs(os.path.join(out, "raw"), exist_ok=True)
    os.makedirs(os.path.join(out, "measurements"), exist_ok=True)

    session_id = args.session_id or os.path.basename(out)
    os_name = args.os or ("windows" if os.name == "nt" else "linux")
    host = {
        "os": os_name,
        "machine": args.machine or _hostname(),
        "python": sys.version.split()[0],
    }
    interface = {
        "id": args.interface_id,
        "os": os_name,
        "backend": args.backend,
        "driver": args.driver,
        "input_device": args.input_device,
        "output_device": args.output_device,
        "sample_rate": args.sample_rate,
        "block_frames": args.block_frames,
        "input_channels": args.input_channels,
        "output_channels": args.output_channels,
        "monitoring": args.monitoring,
        "reported_input_latency_ms": args.reported_input_latency_ms,
        "reported_output_latency_ms": args.reported_output_latency_ms,
    }
    session = {
        "schema": dl.SESSION_SCHEMA,
        "session_id": session_id,
        "generated_utc": dl.utc_now(),
        "synthetic": False,
        "product": dict(dl.PRODUCT_DEFAULTS),
        "source": {"git_sha": args.source_sha, "build_kind": args.build_kind},
        "host": host,
        "interfaces": [interface],
        "target_conditions": sorted(set(args.condition)),
        "raw_files": [],
        "measurements": [],
        "notes": [args.notes] if args.notes else [],
    }
    dl.write_json(os.path.join(out, "session.json"), session)

    readme = _readme(session_id)
    with open(os.path.join(out, "README.txt"), "w", encoding="utf-8") as fh:
        fh.write(readme)

    print("initialised session %s at %s" % (session_id, out))
    print("  session.json, raw/, measurements/")
    print("next: record loop-back WAVs into raw/, then run ingest_latency.py,")
    print("      ingest_play_trial.py and ingest_functional.py, then")
    print("      validate_evidence.py --session %s" % out)
    return 0


def _hostname():
    try:
        import socket
        return socket.gethostname()
    except Exception:  # pragma: no cover - defensive
        return "unknown"


def _readme(session_id):
    return (
        "Device-validation session %s\n"
        "============================\n\n"
        "This directory is a session/evidence manifest. It contains NO measured\n"
        "values until ingest tools write them from actual recordings/receipts.\n\n"
        "Layout\n"
        "  session.json    product/build/interface/driver/rate/block identity\n"
        "  raw/            put recorded loop-back WAVs and diagnostic receipts here\n"
        "  measurements/   ingest tools write records here; validator discovers them\n\n"
        "Procedure (see tools/device-validation/README.md)\n"
        "  1. identify the real Guitar Companion build and audio interface;\n"
        "  2. record the loop-back WAV(s) into raw/;\n"
        "  3. python3 tools/device-validation/ingest_latency.py --session . ...\n"
        "     python3 tools/device-validation/ingest_play_trial.py --session . ...\n"
        "     python3 tools/device-validation/ingest_functional.py --session . ...\n"
        "  4. python3 tools/device-validation/validate_evidence.py --session .\n\n"
        "A synthetic self-test session cannot satisfy a physical hardware gate.\n"
        % session_id)


if __name__ == "__main__":
    sys.exit(main())
