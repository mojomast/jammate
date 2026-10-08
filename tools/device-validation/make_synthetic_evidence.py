#!/usr/bin/env python3
"""Generate a LABELLED synthetic device-validation session for tooling tests.

The generated session is ``synthetic: true`` throughout. It exercises every
ingest path and every validator rule but can never pass a physical gate: the
validator excludes synthetic records from the hardware matrix and play trials,
and reports them only under ``synthetic_selftest``.

Recordings are deterministic and were never captured from hardware. The clean
two-channel case has a known 96-sample delay so the recovered latency can be
asserted exactly.
"""
import argparse
import os
import shutil
import sys

import device_lib as dl
import ingest_latency
import ingest_play_trial

RATE = 48000
DELAY_SAMPLES = 96  # 2.0 ms at 48 kHz


def _clean_pair():
    n = int(RATE * 0.12)
    ref = dl.make_click(RATE, offset_ms=5.0, total_ms=120.0)
    lp = [0.0] * n
    for i in range(len(ref)):
        j = i + DELAY_SAMPLES
        if j < n:
            lp[j] = ref[i] * 0.7
    # A small deterministic dither keeps the silent floor away from exact zero.
    for i in range(n):
        lp[i] += 0.0005 * ((i * 2654435761) % 1000 / 1000.0 - 0.5)
    return ref, lp


def _session(out):
    return {
        "schema": dl.SESSION_SCHEMA,
        "session_id": "synthetic-selftest",
        "generated_utc": dl.utc_now(),
        "synthetic": True,
        "product": dict(dl.PRODUCT_DEFAULTS),
        "source": {"git_sha": "0" * 40, "build_kind": "synthetic"},
        "host": {"os": "windows", "machine": "synthetic", "python": sys.version.split()[0]},
        "interfaces": [{
            "id": "iface-asio",
            "os": "windows",
            "backend": "asio",
            "driver": "Synthetic ASIO Driver",
            "input_device": "Synthetic In",
            "output_device": "Synthetic Out",
            "sample_rate": RATE,
            "block_frames": 128,
            "input_channels": 2,
            "output_channels": 2,
            "monitoring": "software-app",
            "reported_input_latency_ms": None,
            "reported_output_latency_ms": None,
        }],
        "target_conditions": list(dl.REQUIRED_HARDWARE_CONDITIONS),
        "raw_files": [],
        "measurements": [],
        "notes": ["SYNTHETIC tooling fixture; never physical evidence"],
    }


def _params(**over):
    params = {
        "method": "two-channel",
        "loopback_channel": 0,
        "reference_channel": 1,
        "reference_wav": None,
        "reference_onset_ms": 0.0,
        "search_start_ms": -10.0,
        "search_end_ms": 250.0,
        "onset_threshold_dbfs": -40.0,
        "silence_dbfs": -60.0,
        "min_correlation": 0.5,
        "min_dominance": 1.05,
        "template_ms": 20.0,
        "ambiguity_sep_ms": 2.0,
    }
    params.update(over)
    return params


def _receipt(out, session, iface, kind, metrics, name):
    doc = {
        "schema": dl.RECEIPT_SCHEMA,
        "kind": kind,
        "session_id": session.get("session_id"),
        "interface_id": iface.get("id"),
        "generated_utc": dl.utc_now(),
        "interface": {k: iface.get(k) for k in
                      ("os", "backend", "driver", "input_device",
                       "output_device", "sample_rate", "block_frames",
                       "monitoring")},
        "metrics": metrics,
    }
    rel = os.path.join("raw", name)
    dl.write_json(os.path.join(out, rel), doc)
    return dl.raw_ref(out, rel, role=kind), doc


def generate(out):
    if os.path.exists(out) and os.listdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "raw"), exist_ok=True)
    os.makedirs(os.path.join(out, "measurements"), exist_ok=True)
    session = _session(out)
    dl.write_json(os.path.join(out, "session.json"), session)
    iface = session["interfaces"][0]
    raw = os.path.join(out, "raw")
    meas = os.path.join(out, "measurements")

    # --- clean two-channel: valid, known delay -----------------------------
    ref, lp = _clean_pair()
    clean = os.path.join(raw, "clean-two-channel.wav")
    dl.write_wav(clean, [lp, ref], RATE, "pcm16")
    rec = ingest_latency.make_record(
        session, out, "asio_48k_128", iface, os.path.relpath(clean, out),
        _params(), synthetic=True, notes="synthetic known 96-sample delay",
        out_path=os.path.join(meas, "latency-asio_48k_128.json"))

    # --- ambiguous: two equally strong returns -----------------------------
    n = int(RATE * 0.12)
    ref2 = dl.make_click(RATE, offset_ms=5.0, total_ms=120.0)
    amb = [0.0] * n
    for d in (96, 96 + 1000):
        for i, v in enumerate(ref2):
            j = i + d
            if j < n:
                amb[j] += v * 0.7
    amb_path = os.path.join(raw, "ambiguous.wav")
    dl.write_wav(amb_path, [amb, ref2], RATE, "pcm16")
    ingest_latency.make_record(
        session, out, "asio_48k_64", iface, os.path.relpath(amb_path, out),
        _params(), synthetic=True,
        notes="synthetic ambiguous pair",
        out_path=os.path.join(meas, "latency-asio_48k_64.json"))

    # --- silent ------------------------------------------------------------
    silence = os.path.join(raw, "silent.wav")
    dl.write_wav(silence, [[0.0] * n, ref2], RATE, "pcm16")
    ingest_latency.make_record(
        session, out, "asio_48k_256", iface, os.path.relpath(silence, out),
        _params(), synthetic=True, notes="synthetic silence",
        out_path=os.path.join(meas, "latency-asio_48k_256.json"))

    # --- clipped -----------------------------------------------------------
    clipped = [max(-1.0, min(1.0, v * 12.0)) for v in lp]
    clip_path = os.path.join(raw, "clipped.wav")
    dl.write_wav(clip_path, [clipped, ref], RATE, "pcm16")
    ingest_latency.make_record(
        session, out, "wasapi_low_latency", iface, os.path.relpath(clip_path, out),
        _params(), synthetic=True, notes="synthetic clipped",
        out_path=os.path.join(meas, "latency-wasapi_low_latency.json"))

    # --- non-finite float --------------------------------------------------
    bad = list(lp)
    bad[1000] = float("nan")
    nf_path = os.path.join(raw, "nonfinite.wav")
    dl.write_wav(nf_path, [bad, ref], RATE, "float32")
    # I/O error is expected if the operator's environment cannot even read it;
    # the analysis, not the writer, must catch the non-finite sample.
    ingest_latency.make_record(
        session, out, "asio_48k_128", iface, os.path.relpath(nf_path, out),
        _params(), synthetic=True, notes="synthetic non-finite float",
        out_path=os.path.join(meas, "latency-nonfinite.json"))

    # --- declared/recorded rate mismatch -----------------------------------
    mm_path = os.path.join(raw, "rate-mismatch.wav")
    dl.write_wav(mm_path, [lp, ref], 44100, "pcm16")
    ingest_latency.make_record(
        session, out, "asio_48k_128", iface, os.path.relpath(mm_path, out),
        _params(), synthetic=True, notes="synthetic rate mismatch",
        out_path=os.path.join(meas, "latency-ratemismatch.json"))

    # --- functional + play records (all synthetic, parseable receipts) -----
    for cond in dl.FUNCTIONAL_CONDITIONS:
        ref, _doc = _receipt(out, session, iface, "functional",
                             {"functional": {"outcome": "pass"}},
                             "functional-%s-receipt.json" % cond)
        from ingest_functional import main as functional_main  # noqa: E402
        functional_main(["--session", out, "--condition", cond, "--outcome",
                         "pass", "--receipt", ref["path"],
                         "--synthetic-selftest"])

    play_metrics = {
        "timing": {"start_requested_s": 1.0, "join_heard_s": 3.5,
                   "stop_s": 12.0},
        "useful_lock": {"window_bars": 2, "time_to_lock_s": 1.8},
        "dropouts": {"count": 0},
        "callback": {"p50_ms": 0.8, "p99_ms": 1.2, "deadline_misses": 0,
                     "analysis_overruns": 0},
    }
    ref, doc = _receipt(out, session, iface, "play-trial", play_metrics,
                        "play-trace-receipt.json")
    refs = {"timing": ref, "lock": ref, "dropout": ref, "callback": ref}
    docs = {"timing": doc, "lock": doc, "dropout": doc, "callback": doc}
    values = {
        "style": "Rock", "jam_mode": "follow", "intensity": 0.5,
        "complexity": 0.4, "fill_amount": 0.3, "follow_tightness": 0.5,
        "meter": "4/4", "tempo_bpm": 120.0,
        "start_requested_s": 1.0, "join_heard_s": 3.5, "stop_s": 12.0,
        "useful_lock": "yes", "lock_window_bars": 2, "time_to_lock_s": 1.8,
        "dropouts": 0, "callback_p50_ms": 0.8, "callback_p99_ms": 1.2,
        "callback_deadline_misses": 0, "analysis_overruns": 0,
        "join_sensible": "yes", "stayed_stable": "yes", "overreacted": "no",
        "fills_musical": "yes", "push_pull": "yes",
        "recovered_tap_resync": "yes", "operator": "synthetic",
        "notes": "synthetic play trial",
    }
    play = ingest_play_trial.make_record(session, out, "clean_strumming", iface,
                                         values, refs, docs, [], True)
    dl.write_json(os.path.join(meas, "play-clean_strumming.json"), play)

    # Register the raw files (hash preserved).
    raw_files = []
    for name in sorted(os.listdir(raw)):
        raw_files.append(dl.raw_ref(out, os.path.join("raw", name), role="raw"))
    session["raw_files"] = raw_files
    session["measurements"] = sorted(os.listdir(meas))
    dl.write_json(os.path.join(out, "session.json"), session)
    return {
        "clean_latency_ms": rec["analysis"]["physical_roundtrip_latency_ms"],
        "expected_latency_ms": 1000.0 * DELAY_SAMPLES / RATE,
        "records": sorted(os.listdir(meas)),
    }


def main(argv=None):
    p = argparse.ArgumentParser(description="Generate labelled synthetic device-validation evidence.")
    p.add_argument("--out", required=True)
    args = p.parse_args(argv)
    info = generate(os.path.abspath(args.out))
    print("synthetic session at %s" % os.path.abspath(args.out))
    print("  clean latency: recovered=%s expected=%s"
          % (info["clean_latency_ms"], info["expected_latency_ms"]))
    print("  records: %s" % ", ".join(info["records"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
