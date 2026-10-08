#!/usr/bin/env python3
"""EVAL-LIVE-001 synthetic evidence generator.

Builds a *clearly labelled synthetic* measured evidence tree that is internally
consistent with the frozen predeclared matrix. It exists to:

  * exercise the validator's positive path deterministically in unit tests;
  * provide real fixture WAVs and a manifest whose hashes and sample-exact
    checksums can be cross-checked, without committing waveform binaries.

This is NOT a substitute for the actual-processor replay. The synthetic record
carries ``"synthetic": true`` and is only acceptable to the validator as a
fixture/self-test, never as a shipped actual-processor measurement.
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
    """Deterministic int16 frames. strum/click/noise/silence are distinct."""
    out = []
    for i in range(frames):
        if fixture_id == "silence":
            v = 0.0
        elif fixture_id == "noise":
            x = (i * 1103515245 + 12345) & 0x7FFFFFFF
            v = ((x / 0x7FFFFFFF) * 2.0 - 1.0) * 0.1
        elif fixture_id == "click_120":
            period = int(rate * 60.0 / 120.0 / 2.0)  # eighth note at 120 BPM
            phase = i % period
            v = math.exp(-20.0 * phase / period) * (0.7 if phase < 2 else 0.0)
        else:  # strum_120
            period = int(rate * 60.0 / 120.0 / 4.0)  # sixteenth note at 120 BPM
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
    return {
        "id": fixture_id,
        "path": os.path.basename(path),
        "sha256": hashlib.sha256(open(path, "rb").read()).hexdigest(),
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
    return {
        "manifest_sha256": hashlib.sha256(open(manifest_path, "rb").read()).hexdigest(),
        "entries": entries,
    }


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


def make_cells(predeclared, realtime=True, target_seconds=4.0):
    matrix = predeclared["matrix"]
    cells = []
    for rate in matrix["rates_hz"]:
        for block in matrix["block_sizes"]:
            warm = max(16, int(round(target_seconds * rate / block)))
            for pipe in matrix["pipelines"]:
                for inp in matrix["inputs"]:
                    cid = ve.cell_id(rate, block, pipe, inp)
                    enabled = pipe != "disabled"
                    silence = inp == "silence"
                    paced = (pipe == "enabled") and realtime
                    measured = True
                    reason = None
                    if enabled and silence:
                        bpm = 100.0   # explicit configured fallback, not a belief
                        locked = False
                        playing = False
                    elif enabled:
                        bpm = 120.0
                        locked = True
                        playing = (pipe == "enabled")
                    else:
                        bpm = 100.0
                        locked = False
                        playing = False
                    if pipe == "enabled_pressure":
                        # fast loop intentionally starves the worker: no receipt
                        # is claimed, and queue pressure is visible in drops.
                        receipts_measured = 0
                        analysis_drops = 7
                        candidate = 0
                    elif enabled:
                        receipts_measured = warm
                        analysis_drops = 0
                        candidate = warm if not silence else 0
                    else:
                        receipts_measured = 0
                        analysis_drops = 0
                        candidate = 0
                    cells.append({
                        "id": cid, "rate": rate, "block": block, "pipeline": pipe,
                        "input": inp, "warm_blocks": warm,
                        "realtime_paced": paced, "measured": measured,
                        "unmeasured_reason": reason,
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
                                            receipt_measured=(receipts_measured > 0)),
                        "progression": {
                            "audio_sample_start": 0,
                            "audio_sample_end": (warm + 1) * block,
                            "audio_sample_monotonic": True,
                            "audio_sample_delta_ok": True,
                            "audio_sample_delta_mismatches": 0,
                            "prepared_seen": True,
                            "requested_running_seen": enabled,
                            "join_pending_seen": enabled and not silence,
                            "drums_playing_seen": playing,
                            "generation_changes": 2 if enabled else 0,
                            "candidate_bpm_nonzero": candidate,
                            "receipt_count": warm,
                            "receipt_measured_count": receipts_measured,
                            "receipt_before_horizon": 0,
                            "event_after_horizon": 0,
                            "max_receipt_lag_samples": block if receipts_measured else 0,
                            "last_receipt_sample": (warm + 1) * block if receipts_measured else 0,
                            "analysis_drops": analysis_drops,
                        },
                        "commands": (["Start@0:accepted"] if enabled else []),
                    })
    return cells


def make_evidence(predeclared_path, fixtures, source_hash="a" * 64,
                  product_hash="b" * 64, synthetic=True):
    with open(predeclared_path, "rb") as f:
        predeclared_sha = hashlib.sha256(f.read()).hexdigest()
    predeclared = ve.load_json_strict(predeclared_path)
    return {
        "schema": ve.SCHEMA_MEASURED,
        "task": "EVAL-LIVE-001",
        "status": ve.STATUS_MEASURED,
        "synthetic": synthetic,
        "identity": {
            "generated_utc": "synthetic",
            "runner_git_head": "0" * 40,
            "source": {"path": "synthetic/source", "head": "0" * 40,
                       "src_tree_hash": source_hash, "dirty": False,
                       "plugin_processor_h_sha256": source_hash,
                       "plugin_processor_cpp_sha256": source_hash,
                       "jam_live_interface_h_sha256": source_hash,
                       "facade_definitions_present": True},
            "product": {"path": "synthetic/product", "shared_archive": "synthetic.a",
                        "shared_archive_sha256": product_hash,
                        "nam_archive_sha256": product_hash,
                        "assets_archive_sha256": product_hash,
                        "build_ninja_sha256": product_hash,
                        "product_source_dir": "synthetic/source",
                        "source_matches_product": True},
            "facade_symbols": {"submitJamCommand": True, "readJamLiveState": True},
            "backend": {"required": "experimentalBTrack", "symbol_present": True,
                        "flag_defined": True, "usable": True},
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
        "cells": make_cells(predeclared),
        "semantics": {
            "ran": True, "startAccepted": True, "stopAtNextBarWasDeferred": True,
            "stopWasImmediate": True, "blocksToStopAtNextBar": 8,
            "blocksToImmediateStop": 1, "generationBeforeReprepare": 1,
            "generationAfterReprepare": 2, "generationChangedOnReprepare": True,
            "audioSampleBeforeRelease": 100000,
            "audioSampleAfterReprepareFirstBlock": 4096,
            "silentStartNoLock": True,
        },
        "counts": {"cells": len(ve.expected_cell_ids(predeclared["matrix"])),
                   "measured": len(ve.expected_cell_ids(predeclared["matrix"])),
                   "enabled_rejected": 0},
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True)
    p.add_argument("--predeclared", default=os.path.join(os.path.dirname(__file__), "predeclared.json"))
    p.add_argument("--fixtures-dir")
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)
    fixtures_dir = args.fixtures_dir or os.path.join(args.out, "fixtures")
    fixtures = make_fixtures(fixtures_dir)
    ev = make_evidence(args.predeclared, fixtures)
    evidence_path = os.path.join(args.out, "evidence.json")
    with open(evidence_path, "w", encoding="utf-8") as f:
        json.dump(ev, f, indent=2)
        f.write("\n")
    print(evidence_path)
    print(f"fixtures: {fixtures_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
