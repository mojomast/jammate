#!/usr/bin/env python3
"""EVAL-LIVE-001 synthetic evidence generator (corrected schema 1.1).

Builds a *clearly labelled synthetic* measured evidence tree consistent with the
frozen predeclared matrix, for validator self-tests only. It is accepted by the
validator only under --allow-synthetic-selftest and is never an actual
measurement.

Corrections: schema 1.1 with explicit scope, allocation_scope, worker
allocations unmeasured, callback findings, audio-owner/reported cursor metrics
and null-or-measured lag fields. File handles are closed explicitly.
"""
import argparse
import hashlib
import json
import math
import os
import struct
import wave

import validate_evidence as ve


def build_fixture_samples(fixture_id, frames=4800, rate=48000):
    out = []
    for i in range(frames):
        if fixture_id == "silence":
            v = 0.0
        elif fixture_id == "noise":
            x = (i * 1103515245 + 12345) & 0x7FFFFFFF
            v = ((x / 0x7FFFFFFF) * 2.0 - 1.0) * 0.1
        elif fixture_id == "click_120":
            period = int(rate * 60.0 / 120.0 / 2.0)
            phase = i % period
            v = math.exp(-20.0 * phase / period) * (0.7 if phase < 2 else 0.0)
        else:  # strum_120
            period = int(rate * 60.0 / 120.0 / 4.0)
            phase = i % period
            env = math.exp(-5.0 * phase / period)
            tone = math.sin(2.0 * math.pi * 196.0 * i / rate)
            v = 0.5 * env * tone
        out.append(max(-1.0, min(1.0, v)))
    return [int(round(v * 32767.0)) for v in out]


def write_fixture(path, samples, rate=48000):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))


def fixture_entry(fixture_id, path, samples, rate):
    raw = struct.pack("<%dh" % len(samples), *samples)
    with open(path, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    return {
        "id": fixture_id,
        "path": os.path.basename(path),
        "sha256": sha,
        "sample_rate": rate,
        "channels": 1,
        "frames": len(samples),
        "sample_width_bytes": 2,
        "sample_exact_checksum": hashlib.sha256(raw).hexdigest(),
        "not_guitar_recording": True,
    }


def make_fixtures(fixtures_dir):
    os.makedirs(fixtures_dir, exist_ok=True)
    entries = []
    for fid in ("strum_120", "click_120", "noise", "silence"):
        path = os.path.join(fixtures_dir, fid + ".wav")
        samples = build_fixture_samples(fid)
        write_fixture(path, samples)
        entries.append(fixture_entry(fid, path, samples, 48000))
    manifest = {"schema": "live-jam-replay/fixtures/1.0", "entries": entries}
    manifest_path = os.path.join(fixtures_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    with open(manifest_path, "rb") as f:
        msha = hashlib.sha256(f.read()).hexdigest()
    return {"manifest_sha256": msha, "entries": entries}


def _snap(alloc_cxx=0, alloc_c=0, free=0, lock=0, noop=0):
    snap = {k: 0 for k in ve.SNAPSHOT_NUMERIC}
    snap["cxx_new"] = alloc_cxx
    snap["alloc_cxx_total"] = alloc_cxx
    snap["c_malloc"] = alloc_c
    snap["alloc_c_total"] = alloc_c
    snap["c_free"] = free
    snap["free_total"] = free
    snap["lock"] = lock
    snap["noop_frees"] = noop
    return snap


def _state(rate=48000, bpm=0.0, backend="experimentalBTrack", requested=False,
           playing=False, receipt_measured=False, locked=False):
    return {
        "sessionGeneration": 1, "prepared": True, "requestedRunning": requested,
        "joinPending": False, "drumsPlaying": playing, "backend": backend,
        "failure": "none", "mode": "Follow",
        "clock": {"generation": 3, "bpm": bpm, "beatPhase01": 0.0,
                  "barPhase01": 0.0, "beatInBar": 1, "beatsPerBar": 4,
                  "beatUnit": 4, "confidence01": 0.7 if locked else 0.0,
                  "lockState": "Locked" if locked else "Acquiring",
                  "tempoFrozen": False},
        "candidateBpm": 120.0 if locked else 0.0, "inputPeak": 0.1,
        "sampleRate": float(rate), "audioSampleTime": 100000,
        "lastEventSampleTime": 99000, "lastInputHorizonSampleTime": 99500,
        "lastReceiptSampleTime": 100000 if receipt_measured else 0,
        "receiptMeasured": receipt_measured,
        "analysisDrops": 0, "observationDrops": 0, "userCommandDrops": 0,
        "drumCommandDrops": 0, "discontinuities": 0,
    }


def make_cells(predeclared, scope="full", realtime=True, target_seconds=4.0):
    matrix = predeclared["matrix"]
    cells = []
    for rate in matrix["rates_hz"]:
        for block in matrix["block_sizes"]:
            warm = max(16, int(round(target_seconds * rate / block)))
            for pipe in matrix["pipelines"]:
                for inp in matrix["inputs"]:
                    cid = ve.cell_id(rate, block, pipe, inp)
                    if scope == "smoke" and cid not in ve.SMOKE_IDS:
                        continue
                    enabled = pipe != "disabled"
                    silence = inp == "silence"
                    paced = (pipe == "enabled") and realtime
                    if enabled and silence:
                        bpm, locked, playing = 100.0, False, False
                    elif enabled:
                        bpm, locked = 120.0, True
                        playing = (pipe == "enabled")
                    else:
                        bpm, locked, playing = 100.0, False, False

                    receipt = enabled and not silence and pipe == "enabled"
                    ao_measured = True
                    if pipe == "enabled_pressure":
                        coalesced, skipped = warm // 2, warm // 2
                    else:
                        coalesced, skipped = 0, 0
                    lag = block if ao_measured else None
                    cells.append({
                        "id": cid, "rate": rate, "block": block, "pipeline": pipe,
                        "input": inp, "input_source": f"builtin:{inp}",
                        "warm_blocks": warm, "realtime_paced": paced,
                        "measured": True, "unmeasured_reason": None,
                        "cold": _snap(), "warm": _snap(),
                        "timing": {
                            "callback_count": warm + 1,
                            "callback_wall_ms_sum": (warm + 1) * (block / rate * 1000.0) * 0.4,
                            "callback_us_per_block": block / rate * 1e6 * 0.4,
                            "audio_deadline_ms": block / rate * 1000.0,
                            "elapsed_wall_s": (block / rate * (warm + 1)) if paced else 0.05,
                        },
                        "output": {"rms_mean": 0.12 if not silence else 0.0001,
                                   "rms_max": 0.3 if not silence else 0.0002,
                                   "peak": 0.8 if not silence else 0.001,
                                   "nonzero_blocks": warm if not silence else 0},
                        "state_start": _state(rate=rate),
                        "state_end": _state(rate=rate, bpm=bpm, requested=enabled,
                                            playing=playing, locked=locked,
                                            receipt_measured=receipt),
                        "progression": {
                            "audio_owner_measured": ao_measured,
                            "audio_owner_delta_ok": True,
                            "audio_owner_delta_mismatches": 0,
                            "audio_owner_backward": 0,
                            "audio_owner_start": block,
                            "audio_owner_end": (warm + 1) * block,
                            "reported_monotonic": True,
                            "reported_future": 0,
                            "coalesced_reads": coalesced,
                            "skipped_publications": skipped,
                            "reported_cursor_start": 0,
                            "reported_cursor_end": (warm + 1) * block - (block if coalesced else 0),
                            "receipt_reads": warm,
                            "receipt_measured_reads": warm if receipt else 0,
                            "new_receipts": warm if receipt else 0,
                            "repeated_receipt_reads": 0,
                            "receipt_any_measured": receipt,
                            "receipt_order_violation": False,
                            "receipt_availability_lag_last": block if receipt else None,
                            "receipt_availability_lag_max": block if receipt else None,
                            "event_delay_last": block if receipt else None,
                            "event_delay_max": block if receipt else None,
                            "worker_cursor_lag_measured": ao_measured,
                            "worker_cursor_lag_last": lag,
                            "worker_cursor_lag_max": lag,
                            "prepared_seen": True,
                            "requested_running_seen": enabled,
                            "join_pending_seen": enabled and not silence,
                            "drums_playing_seen": playing,
                            "generation_changes": 2 if enabled else 0,
                            "candidate_bpm_nonzero": (warm if locked else 0),
                            "analysis_drops": (7 if pipe == "enabled_pressure" else 0),
                            "observation_drops": 0,
                            "user_command_drops": 0,
                            "drum_command_drops": 0,
                            "discontinuities": 0,
                        },
                        "commands": (["Start@0:accepted"] if enabled else []),
                    })
    return cells


def _scenarios(scope):
    if scope != "full":
        return []
    return [
        {"id": "default_clean_long", "ran": True, "injected": False,
         "backend_kind": "experimentalBTrack", "backend_first": "experimentalBTrack",
         "backend_last": "experimentalBTrack", "backend_changed": False,
         "unmeasured_reason": None,
         "unmeasured_reason_code": None, "start_accepted": True,
         "join_observed": False, "blocks_to_join": 0, "callbacks": 1500,
         "steps_fired": 0, "output_rms": 0.1, "output_nonzero_blocks": 1500,
         "stop_at_next_bar_deferred": False, "blocks_to_stop_at_next_bar": 0,
         "stop_now_stopped": False, "blocks_to_stop_now": 0, "resync_accepted": False,
         "generation_before_reprepare": 1, "generation_after_reprepare": 1,
         "generation_changed_on_reprepare": False, "shutdown_released": True,
         "audio_owner_delta_ok": True, "audio_owner_delta_mismatches": 0,
         "audio_owner_observed_s": 16.0, "callback_alloc_cxx": 0, "callback_alloc_c": 0,
         "callback_free": 0, "callback_locks": 0},
        {"id": "injected_join_stop_resync", "ran": True, "injected": True,
         "backend_kind": "injectedTest", "backend_first": "injectedTest",
         "backend_last": "injectedTest", "backend_changed": False,
         "unmeasured_reason": None,
         "unmeasured_reason_code": None, "start_accepted": True,
         "join_observed": True, "blocks_to_join": 4, "callbacks": 800,
         "steps_fired": 64, "output_rms": 0.2, "output_nonzero_blocks": 800,
         "stop_at_next_bar_deferred": True, "blocks_to_stop_at_next_bar": 8,
         "stop_now_stopped": True, "blocks_to_stop_now": 1, "resync_accepted": True,
         "generation_before_reprepare": 1, "generation_after_reprepare": 2,
         "generation_changed_on_reprepare": True, "shutdown_released": True,
         "audio_owner_delta_ok": True, "audio_owner_delta_mismatches": 0,
         "audio_owner_observed_s": 8.0, "callback_alloc_cxx": 0, "callback_alloc_c": 0,
         "callback_free": 0, "callback_locks": 0},
    ]


def make_evidence(predeclared_path, fixtures, scope="full", source_hash="a" * 64,
                  product_hash="b" * 64, synthetic=True):
    with open(predeclared_path, "rb") as f:
        predeclared_sha = hashlib.sha256(f.read()).hexdigest()
    predeclared = ve.load_json_strict(predeclared_path)
    cells = make_cells(predeclared, scope=scope)
    ids = [c["id"] for c in cells]
    findings = []
    status = "measured-findings" if findings else "measured"
    return {
        "schema": ve.SCHEMA_MEASURED,
        "task": "EVAL-LIVE-001",
        "status": status,
        "synthetic": synthetic,
        "scope": scope,
        "scope_reason": "" if scope != "diagnostic" else "synthetic diagnostic subset",
        "allocation_scope": "callback-thread-path-only",
        "worker_allocations": {"measured": False,
                               "reason": "thread-local arming counts only the callback thread"},
        "bootstrap": {"attempts": 1, "ready": True,
                      "backend_kind": "experimentalBTrack", "backend_usable": True},
        "identity": {
            "generated_utc": "synthetic",
            "runner_git_head": "0" * 40,
            "source": {"path": "synthetic/source", "head": "0" * 40,
                       "src_tree_hash": source_hash, "dirty": False,
                       "plugin_processor_h_sha256": source_hash,
                       "plugin_processor_cpp_sha256": source_hash,
                       "jam_live_interface_h_sha256": source_hash,
                       "facade_definitions_present": True,
                       "immutable_pins_ok": True,
                       "source_pin_overrides_applied": False},
            "product": {"path": "synthetic/product", "shared_archive": "synthetic.a",
                        "shared_archive_sha256": product_hash,
                        "nam_archive_sha256": product_hash,
                        "assets_archive_sha256": product_hash,
                        "build_ninja_sha256": product_hash,
                        "product_source_dir": "synthetic/source",
                        "source_matches_product": True},
            "facade_symbols": {"submitJamCommand": True, "readJamLiveState": True},
            "backend": {"required": "experimentalBTrack", "kind": "experimentalBTrack",
                        "macro_defined": True, "symbol_present": True,
                        "flag_defined": True, "usable": True,
                        "signals": ["synthetic"]},
            "instrument": {"selfcheck_pass": True,
                           "selfcheck_binary_sha256": "c" * 64,
                           "instrumentation_source_sha256": "d" * 64},
        },
        "protocol": {"predeclared_sha256": predeclared_sha,
                     "validator_sha256": "e" * 64,
                     "harness_source_sha256": "f" * 64,
                     "predeclared_freeze_commit": "88893e24be328f131b5df673078ff934a46ed5ab"},
        "matrix": predeclared["matrix"],
        "fixtures": fixtures,
        "cells": cells,
        "expected_cell_ids": ids,
        "findings": findings,
        "scenarios": _scenarios(scope),
        "counts": {"cells": len(cells), "measured": len(cells),
                   "enabled_rejected": 0, "findings": len(findings)},
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True)
    p.add_argument("--predeclared", default=os.path.join(os.path.dirname(__file__), "predeclared.json"))
    p.add_argument("--fixtures-dir")
    p.add_argument("--scope", default="full", choices=["full", "smoke", "diagnostic"])
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)
    fixtures_dir = args.fixtures_dir or os.path.join(args.out, "fixtures")
    fixtures = make_fixtures(fixtures_dir)
    ev = make_evidence(args.predeclared, fixtures, scope=args.scope)
    evidence_path = os.path.join(args.out, "evidence.json")
    with open(evidence_path, "w", encoding="utf-8") as f:
        json.dump(ev, f, indent=2)
        f.write("\n")
    print(evidence_path)
    print(f"fixtures: {fixtures_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
