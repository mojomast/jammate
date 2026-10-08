#!/usr/bin/env python3
"""Adversarial unit tests for the corrected EVAL-LIVE-001 evidence validator.

Proves the validator accepts a consistent synthetic record only under the
explicit --allow-synthetic-selftest path, and fails closed on independent
mutations: scope/smoke/full, async coalescing cursor semantics, lag null policy,
allocation scope and hidden findings, backend identity, fixtures, counters and
statuses.
"""
import copy
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import make_synthetic_evidence as ms  # noqa: E402
import validate_evidence as ve  # noqa: E402

PREDECLARED_PATH = os.path.join(HERE, "predeclared.json")


def hard_pass(ev, fixtures_dir=None, source=None, allow_synthetic=True):
    errors = []
    checks = []
    predeclared = ve.load_json_strict(PREDECLARED_PATH)
    ve.validate_measured(ev, predeclared,
                         {"fixtures_dir": fixtures_dir, "source": source,
                          "predeclared": None, "product_build": None},
                         errors, checks, allow_synthetic)
    hard_fail = [c for c in checks if c.severity == "hard" and not c.pass_]
    return (not hard_fail and not errors), errors, checks


class CorrectedValidatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="evallive001c-")
        cls.fixtures_dir = os.path.join(cls.tmp, "fixtures")
        cls.fixtures = ms.make_fixtures(cls.fixtures_dir)

    def fresh(self, scope="full"):
        return ms.make_evidence(PREDECLARED_PATH, copy.deepcopy(self.fixtures), scope=scope)

    def cell(self, ev, cid):
        for c in ev["cells"]:
            if c["id"] == cid:
                return c
        raise AssertionError(f"no cell {cid}")

    def enabled_clean(self, ev):
        return self.cell(ev, "enabled_r48000_b128_clean")

    # -- positive / synthetic policy ----------------------------------------
    def test_positive_full_with_opt_in(self):
        ok, errors, checks = hard_pass(self.fresh("full"), allow_synthetic=True)
        self.assertTrue(ok, f"errors={errors}")
        self.assertGreater(len(checks), 100)

    def test_positive_smoke_with_opt_in(self):
        ok, errors, _ = hard_pass(self.fresh("smoke"), allow_synthetic=True)
        self.assertTrue(ok, f"errors={errors}")

    def test_synthetic_rejected_by_default(self):
        ok, errors, _ = hard_pass(self.fresh(), allow_synthetic=False)
        self.assertFalse(ok, "synthetic evidence must be rejected by default")
        self.assertTrue(any("synthetic" in e for e in errors), errors)

    def test_cli_synthetic_rejected_by_default(self):
        path = os.path.join(self.tmp, "ev-synth.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        self.assertEqual(ve.main(["--evidence", path]), 1)

    def test_cli_synthetic_accepted_with_flag(self):
        path = os.path.join(self.tmp, "ev-synth2.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        self.assertEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 0)

    def test_nan_rejected(self):
        ev = self.fresh()
        self.enabled_clean(ev)["timing"]["callback_us_per_block"] = float("nan")
        path = os.path.join(self.tmp, "ev-nan.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        self.assertEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 2)

    def test_infinity_rejected(self):
        ev = self.fresh()
        self.enabled_clean(ev)["output"]["rms_mean"] = float("inf")
        path = os.path.join(self.tmp, "ev-inf.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        self.assertEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 2)

    # -- scope ---------------------------------------------------------------
    def test_full_missing_cell(self):
        ev = self.fresh()
        ev["cells"] = ev["cells"][:-1]
        ev["expected_cell_ids"] = ev["expected_cell_ids"][:-1]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("full scope" in e for e in errors), errors)

    def test_full_extra_cell(self):
        ev = self.fresh()
        extra = copy.deepcopy(ev["cells"][0]); extra["id"] = "bogus_r48000_b128_clean"
        ev["cells"].append(extra)
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_smoke_missing_cell(self):
        ev = self.fresh("smoke")
        ev["cells"] = ev["cells"][:-1]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("smoke" in e for e in errors), errors)

    def test_smoke_extra_cell(self):
        ev = self.fresh("smoke")
        extra = copy.deepcopy(ev["cells"][0]); extra["id"] = "bogus_r48000_b128_clean"
        ev["cells"].append(extra)
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_full_dimension_mismatch(self):
        ev = self.fresh()
        ev["matrix"]["block_sizes"] = [128, 512]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_diagnostic_without_reason(self):
        ev = self.fresh("smoke")
        ev["scope"] = "diagnostic"
        ev["scope_reason"] = ""
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_diagnostic_claiming_full(self):
        ev = self.fresh("full")
        ev["scope"] = "diagnostic"
        ev["scope_reason"] = "pretend full"
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok, "a diagnostic scope must not present the full matrix")

    # -- counters ------------------------------------------------------------
    def test_nonzero_alloc_overflow(self):
        ev = self.fresh(); self.enabled_clean(ev)["warm"]["alloc_overflow"] = 3
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_alloc_total_inconsistent(self):
        ev = self.fresh(); self.enabled_clean(ev)["warm"]["alloc_cxx_total"] = 5
        ok, errors, _ = hard_pass(ev); self.assertFalse(ok)
        self.assertTrue(any("alloc_cxx_total" in e for e in errors), errors)

    def test_free_total_inconsistent(self):
        ev = self.fresh(); self.enabled_clean(ev)["warm"]["free_total"] = 2
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_negative_counter(self):
        ev = self.fresh(); self.enabled_clean(ev)["cold"]["lock"] = -1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_missing_snapshot_field(self):
        ev = self.fresh(); del self.enabled_clean(ev)["warm"]["noop_frees"]
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- C1 cursor semantics -------------------------------------------------
    def test_audio_owner_delta_ok_false(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_delta_ok"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_audio_owner_delta_mismatch(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_delta_mismatches"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_audio_owner_backward(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_backward"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_reported_monotonic_false(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["reported_monotonic"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_reported_future(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["reported_future"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_many_coalesced_reads_are_acceptable(self):
        # Coalescing itself is legitimate; only a bad produced cursor fails.
        ev = self.fresh()
        c = self.cell(ev, "enabled_pressure_r48000_b128_clean")["progression"]
        c["coalesced_reads"] = 500
        c["skipped_publications"] = 500
        ok, errors, _ = hard_pass(ev)
        self.assertTrue(ok, f"errors={errors}")

    # -- M9 lag null policy --------------------------------------------------
    def test_receipt_order_violation(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["receipt_order_violation"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_receipt_measured_but_lag_null(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["receipt_availability_lag_last"] = None
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_no_receipt_but_lag_non_null(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r48000_b128_silence")["progression"]
        c["receipt_availability_lag_last"] = 128
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_receipt_without_new_receipts(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["new_receipts"] = 0
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_worker_cursor_lag_measured_but_null(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["worker_cursor_lag_last"] = None
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_worker_cursor_lag_measured_when_owner_unmeasured(self):
        ev = self.fresh()
        c = self.cell(ev, "enabled_r48000_b128_silence")["progression"]
        c["audio_owner_measured"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- silence / control ---------------------------------------------------
    def test_silence_fabricated_lock(self):
        ev = self.fresh(); self.cell(ev, "enabled_r48000_b128_silence")["state_end"]["clock"]["lockState"] = "Locked"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_silence_fabricated_confidence(self):
        ev = self.fresh(); self.cell(ev, "enabled_r48000_b128_silence")["state_end"]["clock"]["confidence01"] = 0.7
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_silence_drums_playing(self):
        ev = self.fresh(); self.cell(ev, "enabled_r48000_b128_silence")["progression"]["drums_playing_seen"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_disabled_requested_running(self):
        ev = self.fresh(); self.cell(ev, "disabled_r48000_b128_clean")["progression"]["requested_running_seen"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_disabled_drums_playing(self):
        ev = self.fresh(); self.cell(ev, "disabled_r48000_b128_noise")["progression"]["drums_playing_seen"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_state_end_sample_rate_mismatch(self):
        ev = self.fresh(); self.cell(ev, "enabled_r96000_b512_clean")["state_end"]["sampleRate"] = 48000.0
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- identity / backend --------------------------------------------------
    def test_selfcheck_failed(self):
        ev = self.fresh(); ev["identity"]["instrument"]["selfcheck_pass"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_backend_unusable(self):
        ev = self.fresh(); ev["identity"]["backend"]["usable"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_backend_wrong_kind(self):
        ev = self.fresh(); ev["identity"]["backend"]["kind"] = "injectedTest"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_backend_macro_missing(self):
        ev = self.fresh(); ev["identity"]["backend"]["macro_defined"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_source_matches_product_false(self):
        ev = self.fresh(); ev["identity"]["product"]["source_matches_product"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_missing_facade_symbol(self):
        ev = self.fresh(); ev["identity"]["facade_symbols"]["readJamLiveState"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_bad_source_hash(self):
        ev = self.fresh(); ev["identity"]["source"]["src_tree_hash"] = "xyz"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- timing / fields -----------------------------------------------------
    def test_callback_count_mismatch(self):
        ev = self.fresh(); self.enabled_clean(ev)["timing"]["callback_count"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_missing_required_cell_field(self):
        ev = self.fresh(); del self.enabled_clean(ev)["commands"]
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_unmeasured_without_reason(self):
        ev = self.fresh(); c = self.enabled_clean(ev); c["measured"] = False; c["unmeasured_reason"] = None
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- M8 allocation scope and findings -----------------------------------
    def test_allocation_scope_wrong(self):
        ev = self.fresh(); ev["allocation_scope"] = "pipeline-wide"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_worker_allocations_claimed_measured(self):
        ev = self.fresh(); ev["worker_allocations"]["measured"] = True
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_hidden_callback_finding(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["cxx_new"] = 1
        c["warm"]["alloc_cxx_total"] = 1
        # findings left empty -> hidden
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("findings" in e for e in errors), errors)

    def test_findings_force_measured_findings_status(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["cxx_new"] = 1
        c["warm"]["alloc_cxx_total"] = 1
        ev["findings"] = [{"cell": c["id"], "kind": "warm_callback_rt_ops", "detail": "alloc=1"}]
        ev["status"] = "measured"  # wrong: must be measured-findings
        ok, _, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_findings_status_correct(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["cxx_new"] = 1
        c["warm"]["alloc_cxx_total"] = 1
        ev["findings"] = [{"cell": c["id"], "kind": "warm_callback_rt_ops", "detail": "alloc=1"}]
        ev["status"] = "measured-findings"
        ok, errors, _ = hard_pass(ev)
        self.assertTrue(ok, f"errors={errors}")

    def test_free_only_finding_set(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["c_free"] = 1
        c["warm"]["free_total"] = 1
        ev["findings"] = [{"cell": c["id"], "kind": "warm_callback_rt_ops", "detail": "free=1"}]
        ev["status"] = "measured-findings"
        ok, errors, _ = hard_pass(ev)
        self.assertTrue(ok, f"errors={errors}")

    def test_lock_only_finding_set(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["cold"]["lock"] = 1
        ev["findings"] = [{"cell": c["id"], "kind": "cold_callback_rt_ops", "detail": "lock=1"}]
        ev["status"] = "measured-findings"
        ok, errors, _ = hard_pass(ev)
        self.assertTrue(ok, f"errors={errors}")

    def test_free_only_finding_hidden_fails(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["warm"]["c_free"] = 1
        c["warm"]["free_total"] = 1
        ok, _, _ = hard_pass(ev)
        self.assertFalse(ok, "a free-only finding must not be hidden")

    # -- fixtures ------------------------------------------------------------
    def test_fixture_local_hash_mismatch(self):
        ev = self.fresh(); ev["fixtures"]["entries"][0]["sha256"] = "0" * 64
        ok, _, _ = hard_pass(ev, fixtures_dir=self.fixtures_dir); self.assertFalse(ok)

    def test_fixture_unmeasured_without_reason(self):
        ev = self.fresh(); ev["fixtures"] = {"unmeasured": True, "reason": ""}
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_fixture_bad_sample_exact_checksum(self):
        ev = self.fresh(); ev["fixtures"]["entries"][1]["sample_exact_checksum"] = "nothex"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_fixture_not_marked_synthetic(self):
        ev = self.fresh(); ev["fixtures"]["entries"][0]["not_guitar_recording"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- N1 bootstrap --------------------------------------------------------
    def test_bootstrap_missing(self):
        ev = self.fresh(); del ev["bootstrap"]
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_bootstrap_not_ready(self):
        ev = self.fresh(); ev["bootstrap"]["ready"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_bootstrap_backend_wrong(self):
        ev = self.fresh(); ev["bootstrap"]["backend_kind"] = "injectedTest"
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_immutable_pins_false(self):
        ev = self.fresh(); ev["identity"]["source"]["immutable_pins_ok"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- N8 audio-owner ------------------------------------------------------
    def test_audio_owner_unmeasured(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_measured"] = False
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_audio_owner_start_wrong(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_start"] = 0
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_audio_owner_end_wrong(self):
        ev = self.fresh(); self.enabled_clean(ev)["progression"]["audio_owner_end"] = 1
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_audio_owner_forged_no_advance(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)["progression"]
        c["audio_owner_start"] = 128
        c["audio_owner_end"] = 128
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    # -- fourth: input timeline metadata -------------------------------------
    def test_device_rate_mismatch(self):
        ev = self.fresh()
        self.cell(ev, "enabled_r96000_b128_clean")["device_rate"] = 48000.0
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("device_rate" in e for e in errors), errors)

    def test_input_signal_kind_missing(self):
        ev = self.fresh(); del self.enabled_clean(ev)["input_signal_kind"]
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_wav_source_rate_required(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["input_source"] = "wav:/tmp/strum_120.wav"
        c["input_source_rate"] = 0
        ok, _, _ = hard_pass(ev); self.assertFalse(ok)

    def test_wav_source_rate_declared_ok(self):
        ev = self.fresh()
        c = self.enabled_clean(ev)
        c["input_source"] = "wav:/tmp/strum_120.wav"
        c["input_source_rate"] = 48000.0
        ok, errors, _ = hard_pass(ev)
        self.assertTrue(ok, f"a declared WAV source rate must pass: {errors}")

    # -- N3 scenarios / gates ------------------------------------------------
    def test_full_missing_injected_scenario(self):
        ev = self.fresh()
        ev["scenarios"] = [s for s in ev["scenarios"] if s["id"] != "injected_join_stop_resync"]
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)
        self.assertTrue(any("injected" in e for e in errors), errors)

    def test_full_injected_not_joined_gate(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "injected_join_stop_resync":
                s["join_observed"] = False
        ok, _, checks = hard_pass(ev)
        self.assertTrue(ok, "structural validity is independent of the join gate")
        self.assertFalse([c for c in checks if c.id == "join_gate"][0].pass_)

    def test_full_injected_steps_zero_gate(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "injected_join_stop_resync":
                s["steps_fired"] = 0
        _, _, checks = hard_pass(ev)
        self.assertFalse([c for c in checks if c.id == "join_gate"][0].pass_)

    def test_full_default_bad_backend_gate(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "default_clean_long":
                s["backend_kind"] = "injectedTest"
        _, _, checks = hard_pass(ev)
        self.assertFalse([c for c in checks if c.id == "scenario_default_clean_long_gate"][0].pass_)

    def test_full_injected_seam_absent_gate(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "injected_join_stop_resync":
                s["ran"] = False
                s["unmeasured_reason_code"] = "seam_absent"
        ok, _, checks = hard_pass(ev)
        self.assertTrue(ok)
        self.assertFalse([c for c in checks if c.id == "join_gate"][0].pass_)

    def test_full_injected_ran_false_bad_reason_code(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "injected_join_stop_resync":
                s["ran"] = False
                s["unmeasured_reason_code"] = "whatever"
        ok, _, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_smoke_join_gate_partial(self):
        ev = self.fresh("smoke")
        ok, _, checks = hard_pass(ev)
        self.assertTrue(ok, "smoke is structurally valid")
        self.assertFalse([c for c in checks if c.id == "join_gate"][0].pass_,
                         "smoke must not claim the full join gate")

    # -- narrow: default backend identity independent of join ----------------
    def test_default_no_join_is_diagnostic_not_identity_failure(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "default_clean_long":
                s["join_observed"] = False
                s["steps_fired"] = 0
        ok, errors, checks = hard_pass(ev)
        self.assertTrue(ok, f"a no-join default run must remain structurally valid: {errors}")
        self.assertTrue([c for c in checks if c.id == "scenario_default_clean_long_gate"][0].pass_,
                        "default gate is backend/advance, not join")

    def test_scenario_backend_changed_fails(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "default_clean_long":
                s["backend_changed"] = True
                s["backend_last"] = "injectedTest"
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok, "a mid-session backend change must fail closed")
        self.assertTrue(any("backend_changed" in e for e in errors), errors)

    def test_scenario_backend_kind_mismatch_first(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "default_clean_long":
                s["backend_kind"] = "experimentalBTrack"
                s["backend_first"] = "injectedTest"
        ok, errors, _ = hard_pass(ev)
        self.assertFalse(ok)

    def test_scenario_backend_unavailable_recorded(self):
        ev = self.fresh()
        for s in ev["scenarios"]:
            if s["id"] == "default_clean_long":
                s["backend_kind"] = "unavailable"
                s["backend_first"] = "unavailable"
                s["backend_last"] = "unavailable"
        ok, _, checks = hard_pass(ev)
        self.assertTrue(ok, "unavailable must be recorded structurally, not faked")
        self.assertFalse([c for c in checks if c.id == "scenario_default_clean_long_gate"][0].pass_,
                         "an unavailable default backend fails the identity gate")

    # -- status --------------------------------------------------------------
    def test_unknown_status(self):
        ev = self.fresh(); ev["status"] = "trust-me"
        path = os.path.join(self.tmp, "ev-unknown.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ev, f)
        self.assertNotEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest"]), 0)

    def test_expect_status_mismatch(self):
        path = os.path.join(self.tmp, "ev-expect.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.fresh(), f)
        self.assertNotEqual(ve.main(["--evidence", path, "--allow-synthetic-selftest",
                                     "--expect-status", "awaiting-product"]), 0)


class AwaitingAndTimeoutTests(unittest.TestCase):
    def base_awaiting(self, status="awaiting-product"):
        return {"schema": ve.SCHEMA_AWAITING, "task": "EVAL-LIVE-001", "status": status,
                "clean": False, "invoked_binary": False,
                "missing": ["facade_symbol_submit", "facade_symbol_read", "facade_definition"],
                "identity": {}}

    def test_valid_awaiting_product(self):
        errors = []; checks = []
        ve.validate_awaiting(self.base_awaiting(), errors, checks)
        self.assertFalse(errors, errors)

    def test_valid_awaiting_backend(self):
        ev = self.base_awaiting("awaiting-backend")
        ev["missing"] = ["backend_usable", "backend_exact", "backend_macro"]
        errors = []; ve.validate_awaiting(ev, errors, [])
        self.assertFalse(errors, errors)

    def test_awaiting_claims_clean_fails(self):
        ev = self.base_awaiting(); ev["clean"] = True
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_awaiting_invoked_binary_fails(self):
        ev = self.base_awaiting(); ev["invoked_binary"] = True
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_awaiting_missing_empty_fails(self):
        ev = self.base_awaiting(); ev["missing"] = []
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_awaiting_missing_without_facade_or_backend_fails(self):
        ev = self.base_awaiting(); ev["missing"] = ["something_else"]
        errors = []; ve.validate_awaiting(ev, errors, []); self.assertTrue(errors)

    def test_timed_out_before_cells_valid(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": True,
              "clean": False, "measured_partial": False, "partial_cells_present": False,
              "partial_cells_sha256": None, "counters_measured": False,
              "log_sha256": "a" * 64, "timeout_s": 300}
        errors = []; checks = []
        ve.validate_timed_out(ev, errors, checks)
        self.assertFalse(errors, errors)

    def test_timed_out_partial_valid(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": True,
              "clean": False, "measured_partial": True, "partial_cells_present": True,
              "partial_cells_sha256": "b" * 64, "counters_measured": True,
              "log_sha256": "a" * 64, "timeout_s": 300}
        errors = []; ve.validate_timed_out(ev, errors, [])
        self.assertFalse(errors, errors)

    def test_timed_out_partial_without_hash_fails(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": True,
              "clean": False, "measured_partial": True, "partial_cells_present": True,
              "partial_cells_sha256": None, "counters_measured": True,
              "log_sha256": "a" * 64, "timeout_s": 300}
        errors = []; ve.validate_timed_out(ev, errors, [])
        self.assertTrue(errors)

    def test_timed_out_invoked_false_fails(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": False,
              "clean": False, "measured_partial": False, "partial_cells_present": False,
              "partial_cells_sha256": None, "counters_measured": False,
              "log_sha256": "a" * 64, "timeout_s": 300}
        errors = []; ve.validate_timed_out(ev, errors, []); self.assertTrue(errors)

    def test_timed_out_deadline_over_300_fails(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": True,
              "clean": False, "measured_partial": False, "partial_cells_present": False,
              "partial_cells_sha256": None, "counters_measured": False,
              "log_sha256": "a" * 64, "timeout_s": 301}
        errors = []; ve.validate_timed_out(ev, errors, []); self.assertTrue(errors)

    def test_timed_out_missing_log_hash_fails(self):
        ev = {"schema": ve.SCHEMA_MEASURED, "status": "timed-out", "invoked_binary": True,
              "clean": False, "measured_partial": False, "partial_cells_present": False,
              "partial_cells_sha256": None, "counters_measured": False,
              "log_sha256": None, "timeout_s": 300}
        errors = []; ve.validate_timed_out(ev, errors, []); self.assertTrue(errors)


class OverrideAndLinkTests(unittest.TestCase):
    def setUp(self):
        sys.path.insert(0, HERE)
        import replay_lib as rl
        self.rl = rl
        self.tmp = tempfile.mkdtemp(prefix="evallive001n4-")
        self.source = self.tmp
        os.makedirs(os.path.join(self.source, "src", "jam"), exist_ok=True)
        os.makedirs(os.path.join(self.source, "src"), exist_ok=True)
        # create the allowlisted files with arbitrary bytes
        for rel in ("src/jam/DrumClockBridge.h", "src/jam/RhythmTypes.h",
                    "src/PluginProcessor.h", "src/PluginProcessor.cpp"):
            with open(os.path.join(self.source, rel), "wb") as f:
                f.write(b"x")

    def _entry(self, rel, sha):
        return {"path": rel, "sha256": sha}

    def test_unknown_path_rejected(self):
        ok, missing, _ = self.rl.apply_source_pin_overrides(
            self.source, {}, [self._entry("src/other.h", "a" * 64)])
        self.assertFalse(ok)
        self.assertTrue(any("override_unknown_path" in m for m in missing), missing)

    def test_immutable_path_rejected(self):
        ok, missing, _ = self.rl.apply_source_pin_overrides(
            self.source, {}, [self._entry("src/jam/RhythmTypes.h", "a" * 64)])
        self.assertFalse(ok)
        self.assertTrue(any("override_immutable" in m for m in missing), missing)

    def test_non_preregistered_hash_rejected(self):
        ok, missing, _ = self.rl.apply_source_pin_overrides(
            self.source, {}, [self._entry("src/jam/DrumClockBridge.h", "a" * 64)])
        self.assertFalse(ok)
        self.assertTrue(any("override_hash_not_preregistered" in m for m in missing), missing)

    def test_duplicate_rejected(self):
        e = self._entry("src/jam/DrumClockBridge.h",
                        self.rl.OVERRIDE_ALLOWLIST["src/jam/DrumClockBridge.h"])
        ok, missing, _ = self.rl.apply_source_pin_overrides(self.source, {}, [e, e])
        self.assertFalse(ok)
        self.assertTrue(any("override_duplicate" in m for m in missing), missing)

    def test_path_traversal_rejected(self):
        ok, missing, _ = self.rl.apply_source_pin_overrides(
            self.source, {}, [self._entry("../src/jam/DrumClockBridge.h", "a" * 64)])
        self.assertFalse(ok)
        self.assertTrue(any("override_unknown_path" in m for m in missing), missing)

    def test_preregistered_hash_but_file_mismatch(self):
        e = self._entry("src/jam/DrumClockBridge.h",
                        self.rl.OVERRIDE_ALLOWLIST["src/jam/DrumClockBridge.h"])
        ok, missing, _ = self.rl.apply_source_pin_overrides(self.source, {}, [e])
        self.assertFalse(ok)
        self.assertTrue(any("override_mismatch" in m for m in missing), missing)

    def test_immutable_pin_changed(self):
        ok, missing, _ = self.rl.apply_source_pin_overrides(
            self.source, {"src/jam/RhythmTypes.h": "0" * 64}, [])
        self.assertFalse(ok)
        self.assertTrue(any("immutable_pin_changed" in m for m in missing), missing)

    def test_link_metadata_unavailable_fails_closed(self):
        import subprocess as sp
        orig = self.rl.run

        def fake_run(cmd, cwd=None, timeout=600, env=None):
            if cmd and cmd[0] == "ninja":
                raise FileNotFoundError("ninja")
            return orig(cmd, cwd=cwd, timeout=timeout, env=env)

        self.rl.run = fake_run
        try:
            closure = self.rl.extract_link_closure(self.tmp)
        finally:
            self.rl.run = orig
        self.assertFalse(closure["ok"])
        self.assertEqual(closure["missing"], "missing_link_metadata_tool")


class RunReplayInputTests(unittest.TestCase):
    def test_valid_timeout_bounds(self):
        import run_replay as rr
        self.assertTrue(rr.valid_timeout(300))
        self.assertTrue(rr.valid_timeout(1))
        for bad in (0, -1, float("nan"), float("inf"), float("-inf"),
                    300.0001, 301, True, False, "300"):
            self.assertFalse(rr.valid_timeout(bad), repr(bad))

    def test_main_rejects_bad_timeout_without_subprocess(self):
        import run_replay as rr
        orig = rr.subprocess.run

        def boom(*a, **k):
            raise AssertionError("subprocess must not run for a bad timeout")

        rr.subprocess.run = boom
        try:
            for bad in ("0", "-1", "nan", "inf", "301"):
                out = tempfile.mkdtemp(prefix="evallive001to-")
                rc = rr.main(["--source", "/nonexistent", "--product-build", "/nonexistent",
                              "--out", out, "--timeout-s", bad])
                self.assertEqual(rc, 64, f"bad timeout {bad} must exit 64")
        finally:
            rr.subprocess.run = orig

    def test_main_accepts_default_300(self):
        import run_replay as rr
        # The default is within the frozen bound and must not be rejected by the
        # timeout guard (it may proceed past it).
        self.assertTrue(rr.valid_timeout(rr.DEFAULT_TIMEOUT_S))
        self.assertEqual(rr.MAX_TIMEOUT_S, 300.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
