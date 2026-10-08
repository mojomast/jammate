#!/usr/bin/env python3
"""Ingest a recorded loop-back WAV into a physical latency measurement.

The tool reads actual WAV bytes, runs the bounded onset/matched-filter analysis
in ``device_lib`` and writes a fail-closed latency record. A latency number is
only ever produced when the recording is clean and the estimator is
unambiguous; every rejection is recorded with explicit reasons.

Preferred two-channel recording (one file)::

    channel <loopback-channel>  the signal returned from the interface
    channel <reference-channel> the emitted reference click

Dominant positive lag = physical round-trip delay. This cancels the
recording-start offset, which a single absolute onset cannot.

Single-channel recording requires an explicit reference onset (default 0 ms)
and is labelled a weaker method.
"""
import argparse
import os
import sys

import device_lib as dl


def build_parser():
    p = argparse.ArgumentParser(
        description="Ingest a loop-back WAV into a physical latency record.")
    p.add_argument("--session", required=True, help="session dir or session.json")
    p.add_argument("--wav", required=True, help="loop-back WAV to analyse")
    p.add_argument("--out", default=None,
                   help="output record (default: <session>/measurements/latency-<condition>.json)")
    p.add_argument("--condition", required=True,
                   choices=sorted(dl.LATENCY_CONDITIONS))
    p.add_argument("--interface-id", default=None)
    p.add_argument("--method", default="two-channel",
                   choices=("two-channel", "single-channel"))
    p.add_argument("--loopback-channel", type=int, default=0)
    p.add_argument("--reference-channel", type=int, default=1)
    p.add_argument("--reference-wav", default=None,
                   help="separate reference recording (single-channel only)")
    p.add_argument("--reference-onset-ms", type=float, default=0.0,
                   help="emitted reference onset for single-channel (default 0)")
    p.add_argument("--search-start-ms", type=float, default=-10.0)
    p.add_argument("--search-end-ms", type=float, default=250.0)
    p.add_argument("--onset-threshold-dbfs", type=float, default=-40.0)
    p.add_argument("--silence-dbfs", type=float, default=-60.0)
    p.add_argument("--min-correlation", type=float, default=0.5)
    p.add_argument("--min-dominance", type=float, default=1.05)
    p.add_argument("--template-ms", type=float, default=20.0)
    p.add_argument("--ambiguity-sep-ms", type=float, default=2.0,
                   help="minimum separation between distinct correlation peaks")
    p.add_argument("--notes", default=None)
    p.add_argument("--synthetic-selftest", action="store_true",
                   help="mark as synthetic (validator never lets it pass a physical gate)")
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


def make_record(session, base_dir, condition, interface, wav_path, params,
                synthetic=False, notes=None, out_path=None):
    spec = dl.condition_spec(condition)
    params = dict(params)
    canonical = dl.canonical_latency_expectations(condition, interface)
    params["expected_rate"] = canonical["expected_rate"]
    params["target_ms"] = canonical["target_ms"]
    problems = dl.validate_latency_params(params)
    if problems:
        raise dl.DeviceValidationError(
            "invalid latency params: %s" % ", ".join(problems))
    params["expected_channels"] = dl.required_channels(params)
    analysis = dl.analyze_loopback(wav_path, params, base_dir=base_dir)
    reasons = list(analysis["reasons"])
    warnings = []

    # Interface/condition consistency is part of the identity, not the DSP.
    reasons.extend(dl.interface_reasons(condition, interface))
    if params["method"] == "single-channel":
        warnings.append("single-channel absolute onset is sensitive to "
                        "recording-start offset; prefer a two-channel loop-back")

    ref = dl.raw_ref(base_dir, wav_path, role="loopback")
    reference_wav_ref = None
    if params["method"] == "single-channel" and params.get("reference_wav"):
        reference_wav_ref = dl.raw_ref(base_dir, params["reference_wav"],
                                       role="reference")

    seen = set()
    deduped = [r for r in reasons if not (r in seen or seen.add(r))]
    # Fail closed: any reason disqualifies the record as a measurement. The
    # detected number is kept for diagnostics only when it was computed.
    analysis["valid"] = bool(analysis["valid"] and not deduped)
    if deduped and analysis["physical_roundtrip_latency_ms"] is not None:
        analysis["physical_roundtrip_latency_ms_unaccepted"] = (
            analysis["physical_roundtrip_latency_ms"])
        analysis["physical_roundtrip_latency_ms"] = None
        analysis["target_met"] = None
    analysis["reasons"] = deduped
    analysis["warnings"] = warnings

    record = {
        "schema": dl.LATENCY_SCHEMA,
        "synthetic": bool(synthetic),
        "condition": condition,
        "session_id": session.get("session_id"),
        "interface_id": interface.get("id"),
        "generated_utc": dl.utc_now(),
        "provenance": "recorded-loopback",
        "identity": {
            "source_sha": (session.get("source") or {}).get("git_sha"),
            "build_kind": (session.get("source") or {}).get("build_kind"),
            "condition_spec": spec,
            "interface": {
                "os": interface.get("os"),
                "backend": interface.get("backend"),
                "driver": interface.get("driver"),
                "input_device": interface.get("input_device"),
                "output_device": interface.get("output_device"),
                "sample_rate": interface.get("sample_rate"),
                "block_frames": interface.get("block_frames"),
                "monitoring": interface.get("monitoring"),
                "reported_input_latency_ms": interface.get("reported_input_latency_ms"),
                "reported_output_latency_ms": interface.get("reported_output_latency_ms"),
            },
            "raw_wav": ref,
            "raw_reference_wav": reference_wav_ref,
        },
        "analysis": analysis,
        "notes": [notes] if notes else [],
    }
    if out_path:
        dl.write_json(out_path, record)
    return record


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        session_path, session = load_session(args.session)
        base_dir = os.path.dirname(os.path.abspath(session_path))
        interface = find_interface(session, args.interface_id)
        out = args.out or os.path.join(
            base_dir, "measurements", "latency-%s.json" % args.condition)
        params = {
            "method": args.method,
            "loopback_channel": args.loopback_channel,
            "reference_channel": args.reference_channel,
            "reference_wav": args.reference_wav,
            "reference_onset_ms": args.reference_onset_ms,
            "search_start_ms": args.search_start_ms,
            "search_end_ms": args.search_end_ms,
            "onset_threshold_dbfs": args.onset_threshold_dbfs,
            "silence_dbfs": args.silence_dbfs,
            "min_correlation": args.min_correlation,
            "min_dominance": args.min_dominance,
            "template_ms": args.template_ms,
            "ambiguity_sep_ms": args.ambiguity_sep_ms,
        }
        if args.method == "single-channel" and args.reference_wav:
            ref_wav = dl.read_wav(dl.resolve_path(base_dir, args.reference_wav))
            ref_params = dict(params)
            ref_params["method"] = "single-channel"
            events = dl.threshold_onsets(
                ref_wav.samples[args.loopback_channel % ref_wav.channels],
                ref_wav.sample_rate, args.onset_threshold_dbfs)
            if not events:
                raise dl.DeviceValidationError(
                    "reference WAV has no detectable onset: %s" % args.reference_wav)
            params["reference_onset_ms"] = events[0]["time_ms"]
        record = make_record(session, base_dir, args.condition, interface,
                             args.wav, params, args.synthetic_selftest, args.notes,
                             out_path=out)
    except dl.WavError as exc:
        sys.stderr.write("WAV error: %s\n" % exc)
        return 1
    except dl.DeviceValidationError as exc:
        sys.stderr.write("error: %s\n" % exc)
        return 1
    a = record["analysis"]
    print("wrote %s" % out)
    print("  valid=%s method=%s" % (a["valid"], a["method"]))
    print("  physical_roundtrip_latency_ms=%s target_ms=%s target_met=%s"
          % (a["physical_roundtrip_latency_ms"], a["target_ms"], a["target_met"]))
    if a["reasons"]:
        print("  reasons=%s" % ", ".join(a["reasons"]))
    if a["warnings"]:
        print("  warnings=%s" % "; ".join(a["warnings"]))
    return 0 if a["valid"] else 3


if __name__ == "__main__":
    sys.exit(main())
