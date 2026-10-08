#!/usr/bin/env python3
"""EVAL-LIVE-001 fail-closed validator for corrected live-Jam replay evidence.

Corrections under docs/research/live-jam-replay/CORRECTION-CONTRACT.md:

  * default validation REJECTS synthetic evidence; a synthetic self-test tree is
    accepted only with --allow-synthetic-selftest and is labelled as synthetic;
  * scope is explicit (smoke | full | diagnostic) and the exact expected cell set
    is enforced (smoke == the 4 preregistered IDs, full == all 54, diagnostic ==
    the declared subset with a reason);
  * allocation counters are the callback-thread path only; worker allocations
    must be explicitly unmeasured and callback findings must be listed, never
    hidden (status measured vs measured-findings is cross-checked);
  * lag metrics are receipt-horizon, receipt-event and produced-reported, and are
    null exactly when unmeasured;
  * timed-out runs are a distinct status that preserved partial evidence and
    invoked the binary.

Portable by default (recorded evidence only); optional --source/--predeclared/
--fixtures-dir add explicit local cross-checks.
"""
import argparse
import hashlib
import json
import math
import os
import sys

SCHEMA_MEASURED = "live-jam-replay/evidence/1.1"
SCHEMA_AWAITING = "live-jam-replay/awaiting-product/1.0"
STATUS_MEASURED = "measured"
STATUS_MEASURED_FINDINGS = "measured-findings"
STATUS_TIMED_OUT = "timed-out"
STATUS_AWAITING_PRODUCT = "awaiting-product"
STATUS_AWAITING_BACKEND = "awaiting-backend"
MEASURED_STATUSES = {STATUS_MEASURED, STATUS_MEASURED_FINDINGS}
ALL_STATUSES = MEASURED_STATUSES | {STATUS_TIMED_OUT, STATUS_AWAITING_PRODUCT, STATUS_AWAITING_BACKEND}

SMOKE_IDS = [
    "disabled_r48000_b128_silence",
    "disabled_r48000_b128_noise",
    "enabled_r48000_b128_clean",
    "enabled_r48000_b128_silence",
]

CXX_ALLOC = ("cxx_new", "cxx_new_array", "cxx_new_nothrow", "cxx_new_aligned")
C_ALLOC = ("c_malloc", "c_calloc", "c_realloc")
FREE = ("cxx_delete", "cxx_delete_array", "cxx_delete_sized", "cxx_delete_aligned", "c_free")
LOCK = ("lock", "trylock", "cond", "unlock")
OVERFLOW = ("alloc_overflow", "lock_overflow")

CELL_REQUIRED = ("id", "rate", "block", "pipeline", "input", "input_source",
                 "warm_blocks", "realtime_paced", "measured", "unmeasured_reason",
                 "cold", "warm", "timing", "output", "state_start", "state_end",
                 "progression", "commands")
SNAPSHOT_NUMERIC = CXX_ALLOC + C_ALLOC + FREE + LOCK + OVERFLOW + (
    "noop_frees", "blocked_lock", "locked_ns", "max_lock_ns",
    "alloc_cxx_total", "alloc_c_total", "free_total")
PROGRESSION_NUMERIC = (
    "audio_owner_delta_mismatches", "audio_owner_backward", "audio_owner_start",
    "audio_owner_end", "reported_future", "coalesced_reads",
    "skipped_publications", "reported_cursor_start", "reported_cursor_end",
    "receipt_reads", "receipt_measured_reads", "new_receipts",
    "repeated_receipt_reads", "generation_changes", "candidate_bpm_nonzero",
    "analysis_drops", "observation_drops", "user_command_drops",
    "drum_command_drops", "discontinuities")
PROGRESSION_BOOL = (
    "audio_owner_measured", "audio_owner_delta_ok", "reported_monotonic",
    "receipt_any_measured", "receipt_order_violation", "worker_cursor_lag_measured",
    "prepared_seen", "requested_running_seen", "join_pending_seen", "drums_playing_seen")
PROGRESSION_MAYBE_NUMERIC = (
    "receipt_availability_lag_last", "receipt_availability_lag_max",
    "event_delay_last", "event_delay_max",
    "worker_cursor_lag_last", "worker_cursor_lag_max")


class Check:
    def __init__(self, ident, severity, passed, detail=""):
        self.id = ident
        self.severity = severity
        self.pass_ = bool(passed)
        self.detail = detail

    def as_dict(self):
        return {"id": self.id, "severity": self.severity,
                "pass": self.pass_, "detail": self.detail}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def is_hex64(value):
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def _reject_constant(name):
    raise ValueError(f"non-finite JSON constant not allowed: {name}")


def load_json_strict(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.loads(f.read(), parse_constant=_reject_constant)


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def finite(value):
    return is_number(value) and math.isfinite(value)


def nonneg_int(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def check_counter_consistency(snap, where, errors):
    for k in SNAPSHOT_NUMERIC:
        if not nonneg_int(snap.get(k)):
            errors.append(f"{where}: {k} must be a non-negative integer, got {snap.get(k)!r}")
    try:
        if snap["alloc_cxx_total"] != sum(snap[k] for k in CXX_ALLOC):
            errors.append(f"{where}: alloc_cxx_total != sum of C++ new forms")
        if snap["alloc_c_total"] != sum(snap[k] for k in C_ALLOC):
            errors.append(f"{where}: alloc_c_total != sum of C alloc forms")
        if snap["free_total"] != sum(snap[k] for k in FREE):
            errors.append(f"{where}: free_total != sum of free forms")
    except (KeyError, TypeError):
        pass


def cell_id(rate, block, pipeline, inp):
    return f"{pipeline}_r{int(rate)}_b{int(block)}_{inp}"


def expected_full_ids(matrix):
    return [cell_id(r, b, p, i)
            for r in matrix["rates_hz"]
            for b in matrix["block_sizes"]
            for p in matrix["pipelines"]
            for i in matrix["inputs"]]


def validate_measured(ev, predeclared, local, errors, checks, allow_synthetic):
    def add(ident, severity, passed, detail=""):
        checks.append(Check(ident, severity, passed, detail))
        if not passed and severity == "hard":
            errors.append(f"[{ident}] {detail}")

    synthetic = ev.get("synthetic") is True
    if synthetic and not allow_synthetic:
        add("synthetic_rejected", "hard", False,
            "synthetic evidence is not accepted as an actual measurement; use --allow-synthetic-selftest for a labelled self-test")
    if synthetic and allow_synthetic:
        add("synthetic_labelled", "hard", ev.get("synthetic") is True,
            "synthetic self-test accepted only when explicitly labelled")

    add("schema", "hard", ev.get("schema") == SCHEMA_MEASURED,
        f"schema must be {SCHEMA_MEASURED}, got {ev.get('schema')!r}")
    status = ev.get("status")
    add("status", "hard", status in MEASURED_STATUSES,
        f"status must be measured/measured-findings, got {status!r}")

    ident = ev.get("identity", {})
    inst = ident.get("instrument", {}) if isinstance(ident, dict) else {}
    src = ident.get("source", {}) if isinstance(ident, dict) else {}
    prod = ident.get("product", {}) if isinstance(ident, dict) else {}
    syms = ident.get("facade_symbols", {}) if isinstance(ident, dict) else {}
    backend = ident.get("backend", {}) if isinstance(ident, dict) else {}

    add("instrument_selfcheck", "hard", inst.get("selfcheck_pass") is True,
        "instrument self-check must pass before any measured evidence")
    add("facade_symbol_submit", "hard", syms.get("submitJamCommand") is True,
        "product archive must define submitJamCommand")
    add("facade_symbol_read", "hard", syms.get("readJamLiveState") is True,
        "product archive must define readJamLiveState")
    add("source_facade_definition", "hard", src.get("facade_definitions_present") is True,
        "source PluginProcessor.cpp must define the frozen facade")
    add("source_matches_product", "hard", prod.get("source_matches_product") is True,
        "product archive source must equal --source")
    add("backend_usable", "hard", backend.get("usable") is True,
        "a live backend must be usable for measured evidence")
    add("backend_exact", "hard", backend.get("kind") == "experimentalBTrack",
        f"default usable backend must be exactly experimentalBTrack, got {backend.get('kind')!r}")
    add("backend_macro", "hard", backend.get("macro_defined") is True,
        "the product must define the exact JAM_LIVE_BTRACK_AVAILABLE backend macro")
    boot = ev.get("bootstrap", {})
    add("bootstrap_ready", "hard",
        isinstance(boot, dict) and boot.get("ready") is True,
        "readiness bootstrap must obtain a coherent prepared tag before the cells")
    add("bootstrap_backend", "hard",
        isinstance(boot, dict) and boot.get("backend_kind") == "experimentalBTrack",
        "readiness bootstrap must report the actual experimentalBTrack backend")

    for key in ("shared_archive_sha256", "nam_archive_sha256",
                "assets_archive_sha256", "build_ninja_sha256"):
        add(f"product_{key}", "hard", is_hex64(prod.get(key)),
            f"product.{key} must be a 64-hex sha256")
    for key in ("plugin_processor_h_sha256", "plugin_processor_cpp_sha256",
                "jam_live_interface_h_sha256", "src_tree_hash"):
        add(f"source_{key}", "hard", is_hex64(src.get(key)),
            f"source.{key} must be a 64-hex sha256")
    add("source_immutable_pins", "hard", src.get("immutable_pins_ok") is True,
        "identity.source.immutable_pins_ok must be true (frozen headers unchanged)")

    proto = ev.get("protocol", {})
    for key in ("predeclared_sha256", "validator_sha256", "harness_source_sha256"):
        add(f"protocol_{key}", "hard", is_hex64(proto.get(key)),
            f"protocol.{key} must be a 64-hex sha256")

    # ---- allocation scope (M8)
    add("allocation_scope", "hard", ev.get("allocation_scope") == "callback-thread-path-only",
        "allocation_scope must be callback-thread-path-only")
    wa = ev.get("worker_allocations", {})
    add("worker_allocations_unmeasured", "hard",
        isinstance(wa, dict) and wa.get("measured") is False
        and isinstance(wa.get("reason"), str) and len(wa["reason"]) > 0,
        "worker allocations must be explicitly unmeasured with a reason")

    # ---- scope and exact matrix (M5)
    scope = ev.get("scope")
    add("scope_known", "hard", scope in ("smoke", "full", "diagnostic"),
        f"scope must be smoke|full|diagnostic, got {scope!r}")
    cells = ev.get("cells", [])
    add("cells_is_list", "hard", isinstance(cells, list) and len(cells) > 0,
        "cells must be a non-empty list")
    expected_declared = ev.get("expected_cell_ids")
    add("expected_cell_ids_present", "hard",
        isinstance(expected_declared, list) and len(expected_declared) > 0,
        "expected_cell_ids must be a non-empty list")
    have = [c.get("id") for c in cells] if isinstance(cells, list) else []

    full_ids = expected_full_ids(predeclared["matrix"])
    if scope == "full":
        add("scope_full_ids", "hard", sorted(have) == sorted(full_ids),
            f"full scope must contain all {len(full_ids)} cells; "
            f"missing={sorted(set(full_ids) - set(have))[:6]} extra={sorted(set(have) - set(full_ids))[:6]}")
        matrix = ev.get("matrix", {})
        add("matrix_dimensions", "hard",
            matrix.get("rates_hz") == predeclared["matrix"]["rates_hz"]
            and matrix.get("block_sizes") == predeclared["matrix"]["block_sizes"]
            and matrix.get("pipelines") == predeclared["matrix"]["pipelines"]
            and matrix.get("inputs") == predeclared["matrix"]["inputs"],
            "full-scope evidence matrix must equal the frozen predeclared matrix")
    elif scope == "smoke":
        add("scope_smoke_ids", "hard", sorted(have) == sorted(SMOKE_IDS),
            f"smoke scope must contain exactly the 4 preregistered ids; got {sorted(have)}")
        add("scope_smoke_declared", "hard",
            isinstance(expected_declared, list) and sorted(expected_declared) == sorted(SMOKE_IDS),
            "smoke scope must declare its exact expected ids")
    elif scope == "diagnostic":
        add("scope_diagnostic_reason", "hard",
            isinstance(ev.get("scope_reason"), str) and len(ev["scope_reason"]) > 0,
            "diagnostic scope requires a non-empty scope_reason")
        add("scope_diagnostic_no_full_claim", "hard",
            sorted(have) != sorted(full_ids),
            "a diagnostic subset must not present itself as the full 54-cell matrix")
        add("scope_diagnostic_declared", "hard",
            isinstance(expected_declared, list) and sorted(expected_declared) == sorted(have),
            "diagnostic expected_cell_ids must equal the actual cells")

    add("cells_unique", "hard", len(have) == len(set(have)),
        "cell ids must be unique")
    add("cells_match_declared", "hard",
        isinstance(expected_declared, list) and sorted(have) == sorted(expected_declared),
        "actual cells must equal the declared expected_cell_ids")

    # ---- per-cell
    measured_enabled = 0
    disabled_cells = []
    enabled_cells = []
    expected_findings = set()
    warm_by_rate_block = {}
    for c in cells if isinstance(cells, list) else []:
        cid = c.get("id", "?")
        for k in CELL_REQUIRED:
            if k not in c:
                errors.append(f"cell {cid}: missing field {k}")
        warm = c.get("warm_blocks")
        if isinstance(warm, int):
            warm_by_rate_block.setdefault((c.get("rate"), c.get("block")), set()).add(warm)
        for snap_name in ("cold", "warm"):
            snap = c.get(snap_name)
            if isinstance(snap, dict):
                check_counter_consistency(snap, f"cell {cid}.{snap_name}", errors)
                add(f"{cid}.{snap_name}.alloc_overflow", "hard", snap.get("alloc_overflow") == 0,
                    "alloc record overflow means detail evidence was dropped")
                add(f"{cid}.{snap_name}.lock_overflow", "hard", snap.get("lock_overflow") == 0,
                    "lock record overflow means detail evidence was dropped")
                if c.get("measured") is True:
                    alloc = any(snap.get(k, 0) for k in CXX_ALLOC + C_ALLOC)
                    freed = any(snap.get(k, 0) for k in FREE)
                    locked = any(snap.get(k, 0) for k in ("lock", "trylock", "unlock", "cond"))
                    if alloc or freed or locked:
                        expected_findings.add((cid, f"{snap_name}_callback_rt_ops"))
            else:
                errors.append(f"cell {cid}: {snap_name} snapshot missing")

        measured = c.get("measured") is True
        if not measured:
            reason = c.get("unmeasured_reason")
            add(f"{cid}.unmeasured_explicit", "hard",
                isinstance(reason, str) and len(reason) > 0,
                f"unmeasured cell {cid} must carry an explicit non-empty reason")
            continue
        if c.get("pipeline") == "disabled":
            disabled_cells.append(c)
        else:
            measured_enabled += 1
            enabled_cells.append(c)

        timing = c.get("timing", {})
        for key in ("callback_count", "audio_deadline_ms", "elapsed_wall_s",
                    "callback_wall_ms_sum", "callback_us_per_block"):
            add(f"{cid}.timing.{key}", "hard", finite(timing.get(key)),
                f"timing.{key} must be finite for a measured cell")
        warm_n = c.get("warm_blocks")
        add(f"{cid}.timing.callback_count", "hard",
            isinstance(warm_n, int) and timing.get("callback_count") == warm_n + 1,
            "callback_count must be warm_blocks + 1 (one cold callback)")

        output = c.get("output", {})
        for key in ("rms_mean", "rms_max", "peak"):
            add(f"{cid}.output.{key}", "hard", finite(output.get(key)),
                f"output.{key} must be finite for a measured cell")

        prog = c.get("progression", {})
        for key in PROGRESSION_NUMERIC:
            add(f"{cid}.progression.{key}", "hard", nonneg_int(prog.get(key)),
                f"progression.{key} must be a non-negative integer")
        for key in PROGRESSION_BOOL:
            add(f"{cid}.progression.{key}", "hard", isinstance(prog.get(key), bool),
                f"progression.{key} must be boolean")
        for key in PROGRESSION_MAYBE_NUMERIC:
            v = prog.get(key)
            add(f"{cid}.progression.{key}", "hard", v is None or nonneg_int(v),
                f"progression.{key} must be a non-negative integer or explicit null")

        # C1: audio-owner vs reported cursor
        block = c.get("block")
        warm = c.get("warm_blocks")
        add(f"{cid}.audio_owner_measured", "hard",
            prog.get("audio_owner_measured") is True,
            "the audio-owner cursor must be measured for every measured cell")
        add(f"{cid}.audio_owner_delta", "hard",
            prog.get("audio_owner_delta_ok") is True
            and prog.get("audio_owner_delta_mismatches") == 0
            and prog.get("audio_owner_backward") == 0,
            "audio-owner cursor must advance by exactly one block with no backwards move")
        add(f"{cid}.audio_owner_start", "hard",
            isinstance(block, int) and prog.get("audio_owner_start") == block,
            "audio_owner_start must equal one block (the cold baseline)")
        add(f"{cid}.audio_owner_end", "hard",
            isinstance(block, int) and isinstance(warm, int)
            and prog.get("audio_owner_end") == (warm + 1) * block,
            "audio_owner_end must equal (warm_blocks+1)*block (exact engine position)")
        add(f"{cid}.audio_owner_advance", "hard",
            isinstance(prog.get("audio_owner_end"), int)
            and isinstance(prog.get("audio_owner_start"), int)
            and prog.get("audio_owner_end") > prog.get("audio_owner_start"),
            "at least one real audio-owner delta must be observed (no forged zero)")
        add(f"{cid}.reported_monotonic", "hard", prog.get("reported_monotonic") is True,
            "facade cursor must be non-decreasing (coalescing tolerant)")
        add(f"{cid}.reported_future", "hard", prog.get("reported_future") == 0,
            "facade cursor must never exceed the produced audio-owner cursor")

        # M9: receipt and lag null policy
        if prog.get("receipt_any_measured") is True:
            add(f"{cid}.receipt_order", "hard", prog.get("receipt_order_violation") is False,
                "receipt must be >= horizon and >= event")
            for key in ("receipt_availability_lag_last", "receipt_availability_lag_max",
                        "event_delay_last", "event_delay_max"):
                add(f"{cid}.{key}.measured", "hard", nonneg_int(prog.get(key)),
                    f"{key} must be a measured integer when a receipt exists")
            add(f"{cid}.new_receipts", "hard", prog.get("new_receipts", 0) >= 1,
                "a measured receipt must yield at least one new receipt")
        else:
            for key in ("receipt_availability_lag_last", "receipt_availability_lag_max",
                        "event_delay_last", "event_delay_max"):
                add(f"{cid}.{key}.null", "hard", prog.get(key) is None,
                    f"{key} must be explicit null when no receipt is measured")

        if prog.get("audio_owner_measured") is True:
            add(f"{cid}.worker_cursor_lag_measured", "hard",
                prog.get("worker_cursor_lag_measured") is True,
                "worker cursor lag must be measured when the audio-owner cursor is")
            add(f"{cid}.worker_cursor_lag_values", "hard",
                nonneg_int(prog.get("worker_cursor_lag_last"))
                and nonneg_int(prog.get("worker_cursor_lag_max")),
                "worker cursor lag must be integers when measured")
        else:
            add(f"{cid}.worker_cursor_lag_null", "hard",
                prog.get("worker_cursor_lag_measured") is False
                and prog.get("worker_cursor_lag_last") is None
                and prog.get("worker_cursor_lag_max") is None,
                "worker cursor lag must be unmeasured/null when the audio-owner cursor is unmeasured")

        end = c.get("state_end", {})
        clock = end.get("clock", {}) if isinstance(end, dict) else {}
        if isinstance(end, dict):
            add(f"{cid}.state_end.prepared", "hard", end.get("prepared") is True,
                "state_end.prepared must be true after a measured replay")
            add(f"{cid}.state_end.sample_rate", "hard",
                finite(end.get("sampleRate")) and end.get("sampleRate") == c.get("rate"),
                "state_end.sampleRate must equal the cell rate")
            if end.get("receiptMeasured") is True:
                lr, lh, le = (end.get("lastReceiptSampleTime"), end.get("lastInputHorizonSampleTime"),
                              end.get("lastEventSampleTime"))
                add(f"{cid}.state_end.receipt_order", "hard",
                    all(isinstance(x, int) for x in (lr, lh, le)) and lr >= lh >= le,
                    "when receiptMeasured, lastReceiptSampleTime >= lastInputHorizonSampleTime >= lastEventSampleTime")
        if c.get("input") == "silence":
            add(f"{cid}.silence_no_lock", "hard", clock.get("lockState") == "Acquiring",
                "silence must not reach a clock lock")
            add(f"{cid}.silence_no_confidence", "hard", clock.get("confidence01") in (0, 0.0),
                "silence must not fabricate clock confidence")
            add(f"{cid}.silence_no_drums", "hard", prog.get("drums_playing_seen") is False,
                "silence must not produce drums playback")
        if c.get("pipeline") == "disabled":
            add(f"{cid}.disabled_no_start", "hard", prog.get("requested_running_seen") is False,
                "control cell must not report requestedRunning")
            add(f"{cid}.disabled_no_drums", "hard", prog.get("drums_playing_seen") is False,
                "control cell must not report drums playing")

    for key, values in warm_by_rate_block.items():
        if len(values) != 1:
            errors.append(f"warm_blocks inconsistent for rate/block {key}: {sorted(values)}")

    add("enabled_measured_present", "hard", measured_enabled > 0,
        "a measured run with a usable backend must measure at least one enabled cell")

    # ---- findings cross-check (M8)
    declared = ev.get("findings", [])
    add("findings_is_list", "hard", isinstance(declared, list), "findings must be a list")
    declared_set = set()
    for fnd in declared if isinstance(declared, list) else []:
        declared_set.add((fnd.get("cell"), fnd.get("kind")))
    add("findings_not_hidden", "hard", declared_set == expected_findings,
        f"findings must exactly list callback alloc/free findings; "
        f"missing={sorted(expected_findings - declared_set)[:6]} extra={sorted(declared_set - expected_findings)[:6]}")
    want_status = STATUS_MEASURED_FINDINGS if expected_findings else STATUS_MEASURED
    add("status_matches_findings", "hard", status == want_status,
        f"status must be {want_status} for {len(expected_findings)} findings, got {status!r}")

    add("rt_gate", "gate", len(expected_findings) == 0,
        "the callback path must be allocation/free/lock free (RT gate)")
    validate_scenarios(ev, scope, add)

    # ---- overhead contrast (advisory)
    def key_of(c):
        return (c.get("rate"), c.get("block"), c.get("input"))
    disabled_by = {key_of(c): c for c in disabled_cells if c.get("measured")}
    for c in enabled_cells:
        d = disabled_by.get(key_of(c))
        if d is None:
            continue
        du = d["timing"]["callback_us_per_block"]
        eu = c["timing"]["callback_us_per_block"]
        ratio = eu / du if du and du > 0 else None
        add(f"{c['id']}.overhead_ratio", "advisory", ratio is not None,
            f"enabled/disabled callback us/block ratio = {ratio!r} (wall, not a deadline)")

    # ---- fixtures
    fixtures = ev.get("fixtures")
    add("fixtures_present", "hard", isinstance(fixtures, dict),
        "fixtures block must be present (entries or explicit unmeasured)")
    if isinstance(fixtures, dict):
        if fixtures.get("unmeasured") is True:
            add("fixtures_unmeasured_reason", "hard",
                isinstance(fixtures.get("reason"), str) and len(fixtures["reason"]) > 0,
                "unmeasured fixtures need an explicit reason")
        else:
            add("fixtures_manifest_sha256", "hard", is_hex64(fixtures.get("manifest_sha256")),
                "fixture manifest sha256 must be 64-hex")
            entries = fixtures.get("entries")
            add("fixtures_entries", "hard", isinstance(entries, list) and len(entries) > 0,
                "fixture entries must be a non-empty list")
            for e in entries if isinstance(entries, list) else []:
                for k in ("sha256", "sample_exact_checksum"):
                    if not is_hex64(e.get(k)):
                        add(f"fixture_{e.get('id')}_{k}", "hard", False,
                            f"fixture {e.get('id')}.{k} must be 64-hex")
                if not (isinstance(e.get("id"), str) and len(e["id"]) > 0):
                    add("fixture_id", "hard", False, "fixture id must be a non-empty string")
                if e.get("not_guitar_recording") is not True:
                    add(f"fixture_{e.get('id')}_identity", "hard", False,
                        "synthetic fixtures must be marked not_guitar_recording")
                for k in ("sample_rate", "channels", "frames", "sample_width_bytes"):
                    if not (isinstance(e.get(k), int) and e.get(k) > 0):
                        add(f"fixture_{e.get('id')}_{k}", "hard", False,
                            f"fixture {e.get('id')}.{k} must be a positive integer")
                if local.get("fixtures_dir"):
                    p = os.path.join(local["fixtures_dir"], e.get("path", ""))
                    if not os.path.isfile(p):
                        add(f"fixture_{e.get('id')}_local", "hard", False, f"fixture file missing: {p}")
                    elif sha256_file(p) != e.get("sha256"):
                        add(f"fixture_{e.get('id')}_local", "hard", False, f"fixture file sha256 mismatch: {p}")

    _cross_check_local(ev, predeclared, local, add, errors)
    return checks


def validate_timed_out(ev, errors, checks):
    def add(ident, severity, passed, detail=""):
        checks.append(Check(ident, severity, passed, detail))
        if not passed and severity == "hard":
            errors.append(f"[{ident}] {detail}")
    add("schema", "hard", ev.get("schema") == SCHEMA_MEASURED,
        f"timed-out schema must be {SCHEMA_MEASURED}")
    add("status", "hard", ev.get("status") == STATUS_TIMED_OUT, "status must be timed-out")
    add("invoked_binary", "hard", ev.get("invoked_binary") is True,
        "a timed-out run must record that the binary was invoked")
    add("clean_false", "hard", ev.get("clean") is False,
        "a timed-out run must not claim clean")
    add("timeout_s", "hard", finite(ev.get("timeout_s")) and 0 < ev.get("timeout_s") <= 300,
        "timeout_s must be in (0, 300]")
    add("log_sha256", "hard", is_hex64(ev.get("log_sha256")),
        "the preserved log hash must be a 64-hex sha256")
    add("not_awaiting", "hard", ev.get("status") != STATUS_AWAITING_PRODUCT,
        "a timeout is not an awaiting-product receipt")
    mp = ev.get("measured_partial")
    add("measured_partial_bool", "hard", isinstance(mp, bool),
        "measured_partial must be boolean")
    if mp is True:
        add("partial_cells_sha256", "hard", is_hex64(ev.get("partial_cells_sha256")),
            "measured_partial=true requires a preserved parsed cells hash")
        add("partial_cells_present", "hard", ev.get("partial_cells_present") is True,
            "measured_partial=true requires partial_cells_present=true")
    else:
        add("counters_unmeasured", "hard", ev.get("counters_measured") is False,
            "a timeout before any cells must report counters unmeasured (not zero)")
        add("no_partial_hash", "hard", ev.get("partial_cells_sha256") in (None,),
            "measured_partial=false must not carry a partial cells hash")
    return checks


def validate_awaiting(ev, errors, checks):
    def add(ident, severity, passed, detail=""):
        checks.append(Check(ident, severity, passed, detail))
        if not passed and severity == "hard":
            errors.append(f"[{ident}] {detail}")
    add("schema", "hard", ev.get("schema") in (SCHEMA_AWAITING, SCHEMA_MEASURED),
        f"awaiting schema unexpected: {ev.get('schema')!r}")
    add("status", "hard", ev.get("status") in (STATUS_AWAITING_PRODUCT, STATUS_AWAITING_BACKEND),
        f"awaiting status unexpected: {ev.get('status')!r}")
    add("not_clean", "hard", ev.get("clean") is False,
        "an awaiting receipt must explicitly not claim clean")
    add("binary_not_invoked", "hard", ev.get("invoked_binary") is False,
        "no binary may be invoked when the product is not live-capable")
    missing = ev.get("missing")
    add("missing_named", "hard", isinstance(missing, list) and len(missing) > 0,
        "missing items must be named")
    if isinstance(missing, list):
        add("missing_covers_facade_or_backend", "hard",
            bool({"facade_symbol_submit", "facade_symbol_read", "facade_definition",
                  "shared_archive", "source_matches_product", "backend_usable",
                  "backend_exact", "backend_macro"}.intersection(set(missing))),
            f"missing must include a facade/backend/identity gap; got {missing}")
    return checks


SCENARIO_REASON_CODES = {"seam_absent", "set_tracker_rejected"}


def validate_scenarios(ev, scope, add):
    scenarios = ev.get("scenarios")
    add("scenarios_is_list", "hard", isinstance(scenarios, list), "scenarios must be a list")
    by_id = {}
    dup = False
    for s in scenarios if isinstance(scenarios, list) else []:
        sid = s.get("id")
        if sid in by_id:
            dup = True
        by_id[sid] = s
    add("scenarios_unique", "hard", not dup, "scenario ids must be unique")

    if scope == "full":
        add("scenarios_full_default_present", "hard", "default_clean_long" in by_id,
            "full scope requires default_clean_long")
        add("scenarios_full_injected_present", "hard", "injected_join_stop_resync" in by_id,
            "full scope requires injected_join_stop_resync")
        for sid in ("default_clean_long", "injected_join_stop_resync"):
            s = by_id.get(sid)
            if isinstance(s, dict):
                add(f"scenario_{sid}_ran_bool", "hard", isinstance(s.get("ran"), bool),
                    "ran must be boolean")
                if s.get("ran") is not True:
                    add(f"scenario_{sid}_reason_code", "hard",
                        s.get("unmeasured_reason_code") in SCENARIO_REASON_CODES,
                        f"unmeasured scenario {sid} must carry a known reason code")
        d = by_id.get("default_clean_long", {}) or {}
        d_ok = (d.get("ran") is True and d.get("backend_kind") == "experimentalBTrack"
                and d.get("start_accepted") is True and d.get("audio_owner_delta_ok") is True
                and isinstance(d.get("audio_owner_observed_s"), (int, float))
                and d.get("audio_owner_observed_s", 0) > 0
                and d.get("callbacks", 0) > 0 and d.get("output_nonzero_blocks", 0) > 0)
        add("scenario_default_clean_long_gate", "gate", d_ok,
            "default_clean_long must run with the actual experimentalBTrack backend and real audio-owner advancement")
        inj = by_id.get("injected_join_stop_resync", {}) or {}
        inj_ok = (inj.get("ran") is True and inj.get("backend_kind") == "injectedTest"
                  and inj.get("start_accepted") is True and inj.get("join_observed") is True
                  and inj.get("steps_fired", 0) > 0 and inj.get("stop_now_stopped") is True
                  and inj.get("resync_accepted") is True
                  and inj.get("generation_changed_on_reprepare") is True
                  and inj.get("shutdown_released") is True
                  and inj.get("callbacks", 0) > 0 and inj.get("output_nonzero_blocks", 0) > 0)
        if inj.get("ran") is not True:
            add("join_gate", "gate", False,
                f"join proof unavailable: {inj.get('unmeasured_reason_code')}")
        else:
            add("join_gate", "gate", inj_ok,
                "injected join/stop/resync scenario must show real join, steps, StopNow, resync, reprepare and shutdown")
    else:
        add("join_gate", "gate", False,
            f"scope {scope} is partial; the first-audible join gate is only required for full scope")


def _cross_check_local(ev, predeclared, local, add, errors):
    if local.get("predeclared"):
        add("local_predeclared_hash", "hard",
            sha256_file(local["predeclared"]) == ev.get("protocol", {}).get("predeclared_sha256"),
            "recorded predeclared_sha256 must match the local predeclared file")
    if local.get("source"):
        src = ev.get("identity", {}).get("source", {})
        for rel, key in (("src/PluginProcessor.h", "plugin_processor_h_sha256"),
                         ("src/PluginProcessor.cpp", "plugin_processor_cpp_sha256"),
                         ("src/jam/JamLiveInterface.h", "jam_live_interface_h_sha256")):
            p = os.path.join(local["source"], rel)
            if not os.path.isfile(p):
                add(f"local_source_{key}", "hard", False, f"missing local source {p}")
            elif sha256_file(p) != src.get(key):
                add(f"local_source_{key}", "hard", False, f"recorded {key} mismatch for {p}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--evidence", required=True)
    p.add_argument("--predeclared")
    p.add_argument("--source")
    p.add_argument("--product-build")
    p.add_argument("--fixtures-dir")
    p.add_argument("--expect-status", choices=sorted(ALL_STATUSES))
    p.add_argument("--allow-synthetic-selftest", action="store_true",
                   help="accept a labelled synthetic self-test tree (never an actual clean run)")
    p.add_argument("--json", dest="json_out")
    p.add_argument("--summary-md")
    args = p.parse_args(argv)

    errors = []
    checks = []
    try:
        ev = load_json_strict(args.evidence)
    except Exception as e:  # noqa: BLE001
        print(f"FAIL: cannot parse evidence JSON: {e}", file=sys.stderr)
        return 2

    if args.expect_status and ev.get("status") != args.expect_status:
        errors.append(f"expected status {args.expect_status}, got {ev.get('status')!r}")

    status = ev.get("status")
    predeclared = NORMALIZED_PREDECLARED
    if args.predeclared:
        try:
            predeclared = load_json_strict(args.predeclared)
        except Exception as e:  # noqa: BLE001
            errors.append(f"cannot parse predeclared: {e}")
    local = {"source": args.source, "product_build": args.product_build,
             "predeclared": args.predeclared, "fixtures_dir": args.fixtures_dir}
    try:
        if status in MEASURED_STATUSES:
            checks = validate_measured(ev, predeclared, local, errors, [], args.allow_synthetic_selftest)
        elif status == STATUS_TIMED_OUT:
            checks = validate_timed_out(ev, errors, [])
        elif status in (STATUS_AWAITING_PRODUCT, STATUS_AWAITING_BACKEND):
            checks = validate_awaiting(ev, errors, [])
        else:
            errors.append(f"unknown status {status!r}")
    except Exception as e:  # noqa: BLE001
        errors.append(f"validation crashed: {e}")

    hard_fail = [c for c in checks if c.severity == "hard" and not c.pass_]
    adv_fail = [c for c in checks if c.severity == "advisory" and not c.pass_]
    gate_fail = [c for c in checks if c.severity == "gate" and not c.pass_]
    hard_pass = len(hard_fail) == 0 and not errors
    overall_pass = hard_pass and not gate_fail and status in MEASURED_STATUSES
    verdict = {
        "schema": "live-jam-replay/verdict/1.2",
        "evidence": os.path.basename(args.evidence),
        "status": status,
        "synthetic": ev.get("synthetic") is True,
        "hard_checks": len([c for c in checks if c.severity == "hard"]),
        "hard_failures": len(hard_fail),
        "advisory_checks": len([c for c in checks if c.severity == "advisory"]),
        "advisory_failures": len(adv_fail),
        "gate_checks": len([c for c in checks if c.severity == "gate"]),
        "gate_failures": len(gate_fail),
        "hard_pass": hard_pass,
        "pass": overall_pass,
        "gates": {c.id: c.pass_ for c in checks if c.severity == "gate"},
        "checks": [c.as_dict() for c in checks],
        "errors": errors,
    }

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(verdict, f, indent=2)
            f.write("\n")
    if args.summary_md:
        with open(args.summary_md, "w", encoding="utf-8") as f:
            f.write("# EVAL-LIVE-001 replay evidence verdict\n\n")
            f.write(f"- status: `{status}`\n")
            if verdict["synthetic"]:
                f.write("- **SYNTHETIC SELF-TEST** (not an actual measurement)\n")
            f.write(f"- hard checks: {verdict['hard_checks']} (failures {verdict['hard_failures']})\n")
            f.write(f"- advisory checks: {verdict['advisory_checks']} (failures {verdict['advisory_failures']})\n")
            f.write(f"- gate checks: {verdict['gate_checks']} (failures {verdict['gate_failures']})\n")
            f.write(f"- verdict: **{'PASS' if verdict['pass'] else 'FAIL'}** "
                    f"(hard_pass={verdict['hard_pass']}, gates={verdict['gates']})\n\n")
            for c in checks:
                f.write(f"- [{'PASS' if c.pass_ else 'FAIL'}][{c.severity}] {c.id}: {c.detail}\n")

    for e in errors:
        print(f"FAIL: {e}", file=sys.stderr)
    print(f"hard_pass={verdict['hard_pass']} pass={verdict['pass']} "
          f"hard_failures={verdict['hard_failures']} gate_failures={verdict['gate_failures']} "
          f"advisory_failures={verdict['advisory_failures']} synthetic={verdict['synthetic']} "
          f"gates={verdict['gates']}")
    return 0 if verdict["pass"] else 1


NORMALIZED_PREDECLARED = {
    "matrix": {
        "rates_hz": [48000, 96000],
        "block_sizes": [128, 512, 4096],
        "pipelines": ["disabled", "enabled", "enabled_pressure"],
        "inputs": ["clean", "noise", "silence"],
    }
}


if __name__ == "__main__":
    sys.exit(main())
