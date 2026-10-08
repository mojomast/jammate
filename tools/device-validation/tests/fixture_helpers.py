"""Shared fixtures for the device-validation unit tests (stdlib only).

All fixtures here are TEST-ONLY: they are generated in temp directories (or a
committed *labelled synthetic* example) and are never presented as a real
hardware run.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
if TOOL not in sys.path:
    sys.path.insert(0, TOOL)

import device_lib as dl  # noqa: E402
import ingest_latency  # noqa: E402
import ingest_play_trial  # noqa: E402


def asio_interface(iface_id, block, monitoring="software-app"):
    return {
        "id": iface_id, "os": "windows", "backend": "asio",
        "driver": "Test ASIO Driver", "input_device": "In", "output_device": "Out",
        "sample_rate": 48000, "block_frames": block,
        "input_channels": 2, "output_channels": 2,
        "monitoring": monitoring,
        "reported_input_latency_ms": None, "reported_output_latency_ms": None,
    }


def wasapi_interface(iface_id="iface-wasapi"):
    return {
        "id": iface_id, "os": "windows", "backend": "wasapi",
        "driver": "WASAPI", "input_device": "In", "output_device": "Out",
        "sample_rate": 48000, "block_frames": 240,
        "input_channels": 2, "output_channels": 2,
        "monitoring": "software-app",
        "reported_input_latency_ms": None, "reported_output_latency_ms": None,
    }


def make_session(session_dir, interfaces, synthetic=False, source_sha="a" * 40):
    session = {
        "schema": dl.SESSION_SCHEMA,
        "session_id": "unit-session",
        "generated_utc": dl.utc_now(),
        "synthetic": synthetic,
        "product": dict(dl.PRODUCT_DEFAULTS),
        "source": {"git_sha": source_sha, "build_kind": "test"},
        "host": {"os": "windows", "machine": "unit", "python": sys.version.split()[0]},
        "interfaces": interfaces,
        "target_conditions": list(dl.REQUIRED_HARDWARE_CONDITIONS),
        "raw_files": [],
        "measurements": [],
        "notes": [],
    }
    os.makedirs(os.path.join(session_dir, "raw"), exist_ok=True)
    os.makedirs(os.path.join(session_dir, "measurements"), exist_ok=True)
    dl.write_json(os.path.join(session_dir, "session.json"), session)
    return session


def clean_two_channel(path, rate=48000, delay=96, seconds=0.12, amplitude=0.7,
                      invert=False):
    n = int(rate * seconds)
    ref = dl.make_click(rate, offset_ms=5.0, total_ms=seconds * 1000.0)
    lp = [0.0] * n
    sign = -1.0 if invert else 1.0
    for i, v in enumerate(ref):
        j = i + delay
        if j < n:
            lp[j] = v * amplitude * sign
    for i in range(n):
        lp[i] += 0.0005 * ((i * 2654435761) % 1000 / 1000.0 - 0.5)
    dl.write_wav(path, [lp, ref], rate, "pcm16")
    return delay


def default_params(**over):
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


def receipt_json(session_dir, session, interface, kind, metrics, name):
    """Write a parseable receipt and return (raw_ref, doc)."""
    doc = {
        "schema": dl.RECEIPT_SCHEMA,
        "kind": kind,
        "session_id": session.get("session_id"),
        "interface_id": interface.get("id"),
        "generated_utc": dl.utc_now(),
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
        "metrics": metrics,
    }
    rel = os.path.join("raw", name)
    path = os.path.join(session_dir, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    dl.write_json(path, doc)
    return dl.raw_ref(session_dir, rel, role=kind), doc


def latency_record(session, session_dir, condition, interface, wav_rel, params=None,
                   synthetic=False):
    return ingest_latency.make_record(
        session, session_dir, condition, interface, wav_rel,
        params or default_params(), synthetic=synthetic)


def text_receipt(session_dir, name, text="fixture receipt\n"):
    """A hashed but deliberately unparseable receipt (for receipt-attested tests)."""
    path = os.path.join(session_dir, "raw", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return os.path.relpath(path, session_dir)


def play_record(session, session_dir, condition, interface, receipt_rel=None,
                useful_lock="yes", synthetic=False, as_receipt=True, **over):
    values = {
        "style": "Rock", "jam_mode": "follow", "intensity": 0.5,
        "complexity": 0.4, "fill_amount": 0.3, "follow_tightness": 0.5,
        "meter": "4/4", "tempo_bpm": 120.0,
        "start_requested_s": 1.0, "join_heard_s": 3.0, "stop_s": 10.0,
        "useful_lock": useful_lock, "lock_window_bars": 2, "time_to_lock_s": 1.5,
        "dropouts": 0, "callback_p50_ms": 1.0, "callback_p99_ms": 1.5,
        "callback_deadline_misses": 0, "analysis_overruns": 0,
        "join_sensible": "yes", "stayed_stable": "yes", "overreacted": "no",
        "fills_musical": "yes", "push_pull": "yes",
        "recovered_tap_resync": "yes", "operator": "unit", "notes": None,
    }
    values.update(over)
    metrics = {
        "timing": {k: values[k] for k in
                   ("start_requested_s", "join_heard_s", "stop_s")
                   if values.get(k) is not None},
        "useful_lock": {},
        "dropouts": ({"count": values["dropouts"]}
                     if values.get("dropouts") is not None else {}),
        "callback": {out_key: values[src_key] for out_key, src_key in
                     (("p50_ms", "callback_p50_ms"), ("p99_ms", "callback_p99_ms"),
                      ("deadline_misses", "callback_deadline_misses"),
                      ("analysis_overruns", "analysis_overruns"))
                     if values.get(src_key) is not None},
    }
    if values.get("lock_window_bars") is not None:
        metrics["useful_lock"]["window_bars"] = values["lock_window_bars"]
    if values.get("time_to_lock_s") is not None:
        metrics["useful_lock"]["time_to_lock_s"] = values["time_to_lock_s"]

    if as_receipt:
        ref, doc = receipt_json(session_dir, session, interface, "play-trial",
                                metrics, "play-%s-receipt.json" % condition)
        refs = {"timing": ref, "lock": ref, "dropout": ref, "callback": ref}
        docs = {"timing": doc, "lock": doc, "dropout": doc, "callback": doc}
    else:
        rel = receipt_rel or text_receipt(session_dir, "play-%s.txt" % condition)
        ref = dl.raw_ref(session_dir, rel, role="play")
        refs = {"timing": ref, "lock": ref, "dropout": ref, "callback": ref}
        docs = {"timing": None, "lock": None, "dropout": None, "callback": None}
    return ingest_play_trial.make_record(session, session_dir, condition,
                                         interface, values, refs, docs, [],
                                         synthetic)


def write_record(session_dir, name, record):
    path = os.path.join(session_dir, "measurements", name)
    dl.write_json(path, record)
    return path


def complete_matrix(session_dir, synthetic=False, delay=96, source_sha="a" * 40):
    """TEST-ONLY: build a structurally complete matrix in a temp directory."""
    interfaces = [asio_interface("iface-asio-64", 64),
                  asio_interface("iface-asio-128", 128),
                  asio_interface("iface-asio-256", 256),
                  wasapi_interface()]
    session = make_session(session_dir, interfaces, synthetic=synthetic,
                           source_sha=source_sha)
    wav = os.path.join(session_dir, "raw", "clean.wav")
    clean_two_channel(wav, delay=delay)
    by_id = {i["id"]: i for i in interfaces}
    for cond, iid in (("asio_48k_64", "iface-asio-64"),
                      ("asio_48k_128", "iface-asio-128"),
                      ("asio_48k_256", "iface-asio-256"),
                      ("wasapi_low_latency", "iface-wasapi")):
        rec = latency_record(session, session_dir, cond, by_id[iid],
                             "raw/clean.wav", synthetic=synthetic)
        write_record(session_dir, "latency-%s.json" % cond, rec)
    iface = by_id["iface-asio-128"]
    for cond in dl.FUNCTIONAL_CONDITIONS:
        rec = functional_record(session, session_dir, cond, iface,
                                synthetic=synthetic)
        write_record(session_dir, "functional-%s.json" % cond, rec)
    for cond in dl.PLAY_CONDITIONS:
        rec = play_record(session, session_dir, cond, iface, synthetic=synthetic)
        write_record(session_dir, "play-%s.json" % cond, rec)
    return session


def functional_record(session, session_dir, condition, interface, receipt_rel=None,
                      outcome="pass", synthetic=False, as_receipt=True):
    if as_receipt:
        ref, doc = receipt_json(session_dir, session, interface, "functional",
                                {"functional": {"outcome": outcome}},
                                "functional-%s-receipt.json" % condition)
    else:
        rel = receipt_rel or text_receipt(session_dir, "functional-%s.txt" % condition)
        ref = dl.raw_ref(session_dir, rel, role="functional")
    return {
        "schema": dl.FUNCTIONAL_SCHEMA,
        "synthetic": synthetic,
        "condition": condition,
        "session_id": session.get("session_id"),
        "interface_id": interface.get("id"),
        "generated_utc": dl.utc_now(),
        "provenance": "instrumented-raw",
        "measured": True,
        "outcome": outcome,
        "detail": "unit",
        "identity": {
            "source_sha": (session.get("source") or {}).get("git_sha"),
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
        "receipt": ref,
        "notes": [],
    }
