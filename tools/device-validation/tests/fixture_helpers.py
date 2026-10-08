"""Shared fixtures for the device-validation unit tests (stdlib only)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
if TOOL not in sys.path:
    sys.path.insert(0, TOOL)

import device_lib as dl  # noqa: E402
import ingest_latency  # noqa: E402
import ingest_play_trial  # noqa: E402


def asio_interface(iface_id, block):
    return {
        "id": iface_id, "os": "windows", "backend": "asio",
        "driver": "Test ASIO Driver", "input_device": "In", "output_device": "Out",
        "sample_rate": 48000, "block_frames": block,
        "input_channels": 2, "output_channels": 2,
        "monitoring": "software-app",
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


def make_session(session_dir, interfaces, synthetic=False, raw_files=None):
    session = {
        "schema": dl.SESSION_SCHEMA,
        "session_id": "unit-session",
        "generated_utc": dl.utc_now(),
        "synthetic": synthetic,
        "product": dict(dl.PRODUCT_DEFAULTS),
        "source": {"git_sha": "a" * 40, "build_kind": "test"},
        "host": {"os": "windows", "machine": "unit", "python": sys.version.split()[0]},
        "interfaces": interfaces,
        "target_conditions": list(dl.REQUIRED_HARDWARE_CONDITIONS),
        "raw_files": raw_files or [],
        "measurements": [],
        "notes": [],
    }
    os.makedirs(os.path.join(session_dir, "raw"), exist_ok=True)
    os.makedirs(os.path.join(session_dir, "measurements"), exist_ok=True)
    dl.write_json(os.path.join(session_dir, "session.json"), session)
    return session


def clean_two_channel(path, rate=48000, delay=96, seconds=0.12, amplitude=0.7):
    n = int(rate * seconds)
    ref = dl.make_click(rate, offset_ms=5.0, total_ms=seconds * 1000.0)
    lp = [0.0] * n
    for i, v in enumerate(ref):
        j = i + delay
        if j < n:
            lp[j] = v * amplitude
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
    }
    params.update(over)
    return params


def latency_record(session, session_dir, condition, interface, wav_rel, params=None,
                   synthetic=False):
    return ingest_latency.make_record(
        session, session_dir, condition, interface, wav_rel,
        params or default_params(), synthetic=synthetic)


def text_receipt(session_dir, name, text="fixture receipt\n"):
    path = os.path.join(session_dir, "raw", name)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return os.path.relpath(path, session_dir)


def play_record(session, session_dir, condition, interface, receipt_rel,
                useful_lock="yes", synthetic=False, **over):
    values = {
        "style": "Rock", "jam_mode": "follow", "intensity": 0.5,
        "complexity": 0.4, "fill_amount": 0.3, "follow_tightness": 0.5,
        "meter": "4/4", "tempo_bpm": 120.0,
        "start_requested_s": 1.0, "join_heard_s": 3.0, "stop_s": 10.0,
        "useful_lock": useful_lock, "lock_window_bars": 2, "time_to_lock_s": 1.5,
        "dropouts": 0, "callback_p50_ms": 1.0, "callback_p99_ms": 2.0,
        "callback_deadline_misses": 0, "analysis_overruns": 0,
        "join_sensible": "yes", "stayed_stable": "yes", "overreacted": "no",
        "fills_musical": "yes", "push_pull": "yes",
        "recovered_tap_resync": "yes", "operator": "unit", "notes": None,
    }
    values.update(over)
    ref = dl.raw_ref(session_dir, receipt_rel, role="timing")
    receipts = {"timing": ref, "lock": ref, "callback": ref, "dropout": ref}
    return ingest_play_trial.make_record(session, session_dir, condition,
                                         interface, values, receipts, [], synthetic)


def write_record(session_dir, name, record):
    path = os.path.join(session_dir, "measurements", name)
    dl.write_json(path, record)
    return path


def functional_record(session, session_dir, condition, interface, receipt_rel,
                      outcome="pass", synthetic=False):
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
                "sample_rate": interface.get("sample_rate"),
                "block_frames": interface.get("block_frames"),
            },
        },
        "receipt": dl.raw_ref(session_dir, receipt_rel, role="functional"),
        "notes": [],
    }
