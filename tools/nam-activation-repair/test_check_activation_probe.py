#!/usr/bin/env python3
"""Adversarial acceptance tests for RT-005 check_activation_probe.py.

The evidence test runs the validator against the committed run tree. The
remaining tests copy the real run tree into a temporary directory, inject one
coherent defect, and require the validator to fail closed. Each defect is the
kind that would let a wrong archive or an incomplete matrix pass silently,
including the authoritative-total blind spots: nothrow/aligned `new` visible only
in `warm_alloc_cxx_total`, cold overflow counters, cold free/lock counters, a
missing authoritative column, and unexpected run directories/files.
"""
import csv
import importlib.util
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
RUNS = REPO / "docs/research/nam-activation-repair/runs"
PREDECLARED = HERE / "predeclared.json"
MODELS_DIR = pathlib.Path(
    "/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models")
# A run that must be completely zero: the repaired archive on the LSTM model.
CLEAN = "new/lstm_control"


def run_validator(runs_dir):
    with tempfile.TemporaryDirectory() as d:
        out = pathlib.Path(d) / "verdict.json"
        r = subprocess.run(
            [sys.executable, str(HERE / "check_activation_probe.py"),
             "--runs", str(runs_dir),
             "--predeclared", str(PREDECLARED),
             "--evidence-only",
             "--models-dir", str(MODELS_DIR),
             "--out-json", str(out)],
            capture_output=True, text=True)
        return r.returncode, out


def read_csv(path):
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        return reader.fieldnames, list(reader)


def write_csv(path, fields, rows):
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def edit_csv(path, mutate):
    fields, rows = read_csv(path)
    mutate(rows)
    write_csv(path, fields, rows)


def set_nam_field(path, field, value):
    edit_csv(path, lambda rows: [r.__setitem__(field, value)
                                 for r in rows if r["tag"].startswith("nam")])


def edit_status(path, mutate):
    with open(path) as f:
        lines = [l for l in f.read().splitlines() if "=" in l]
    d = dict(l.split("=", 1) for l in lines)
    mutate(d)
    with open(path, "w") as f:
        for k, v in d.items():
            f.write(f"{k}={v}\n")


class ActivationProbeTests(unittest.TestCase):
    def check_local_artifact_contract(self, evidence_only, mismatch=False):
        spec = importlib.util.spec_from_file_location("activation_validator",
                                                     HERE / "check_activation_probe.py")
        validator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validator)
        pre = json.loads(PREDECLARED.read_text())
        arch = pre['architectures'][0]
        want = pre['variants']['new']
        local_builds = {want['binary'], want['nam_archive'], want['shared_archive']}
        original = validator.os.path.isfile

        def available(path):
            if str(path) in local_builds:
                return mismatch  # simulate unavailable builds, or present bad bytes
            return original(path)

        original_sha = validator.sha256

        def digest(path):
            return '0' * 64 if str(path) in local_builds else original_sha(path)

        errors = []
        with patch.object(validator.os.path, 'isfile', available), \
                patch.object(validator, 'sha256', digest):
            result = validator.check_run('new', arch, str(RUNS / CLEAN), pre,
                                         str(MODELS_DIR), errors, evidence_only=evidence_only)
        self.assertIsNotNone(result)
        return errors

    def test_recorded_evidence_accepts_missing_local_builds(self):
        self.assertEqual(self.check_local_artifact_contract(True), [])

    def test_strict_local_validation_rejects_missing_builds(self):
        errors = self.check_local_artifact_contract(False)
        self.assertTrue(any('on-disk binary missing' in e for e in errors))
        self.assertTrue(any('on-disk NAM archive missing' in e for e in errors))

    def test_recorded_evidence_rejects_present_mismatched_builds(self):
        errors = self.check_local_artifact_contract(True, mismatch=True)
        self.assertTrue(any('on-disk binary sha' in e for e in errors))
        self.assertTrue(any('on-disk NAM archive sha' in e for e in errors))

    def assert_rejected(self, mutate):
        with tempfile.TemporaryDirectory() as d:
            copy = pathlib.Path(d) / "runs"
            shutil.copytree(RUNS, copy)
            mutate(copy)
            rc, _ = run_validator(copy)
            self.assertNotEqual(rc, 0)

    def assert_accepted(self, mutate):
        with tempfile.TemporaryDirectory() as d:
            copy = pathlib.Path(d) / "runs"
            shutil.copytree(RUNS, copy)
            mutate(copy)
            rc, _ = run_validator(copy)
            self.assertEqual(rc, 0)

    def test_committed_evidence_passes(self):
        rc, _ = run_validator(RUNS)
        self.assertEqual(rc, 0)

    # --- F1: authoritative totals ------------------------------------------
    def test_nothrow_aligned_only_in_authoritative_total_rejected(self):
        # Regular columns stay zero; only the authoritative total is positive,
        # exactly how a nothrow/aligned `new` would appear.
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "warm_alloc_cxx_total", "1"))

    def test_authoritative_c_total_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "warm_alloc_c_total", "1"))

    def test_regular_exceeding_authoritative_rejected(self):
        def mutate(root):
            p = root / f"{CLEAN}/cases.csv"
            fields, rows = read_csv(p)
            for r in rows:
                if r["tag"].startswith("nam"):
                    r["warm_cxxnew"] = "3"
                    r["warm_alloc_cxx_total"] = "2"
            write_csv(p, fields, rows)
        self.assert_rejected(mutate)

    def test_new_archive_allocation_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / "new/a2_wavenet_max/cases.csv", "warm_cxxnew", "1"))

    def test_missing_authoritative_column_rejected(self):
        def mutate(root):
            p = root / f"{CLEAN}/cases.csv"
            fields, rows = read_csv(p)
            fields.remove("warm_alloc_c_total")
            for r in rows:
                r.pop("warm_alloc_c_total", None)
            write_csv(p, fields, rows)
        self.assert_rejected(mutate)

    def test_missing_counter_column_rejected(self):
        def mutate(root):
            p = root / f"{CLEAN}/cases.csv"
            fields, rows = read_csv(p)
            fields.remove("cold_noopfree")
            for r in rows:
                r.pop("cold_noopfree", None)
            write_csv(p, fields, rows)
        self.assert_rejected(mutate)

    def test_malformed_authoritative_count_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "warm_alloc_cxx_total", "abc"))

    # --- F1: cold/free/lock/overflow completeness --------------------------
    def test_cold_alloc_overflow_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "cold_alloc_overflow", "1"))

    def test_cold_lock_overflow_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "cold_lock_overflow", "1"))

    def test_cold_regular_alloc_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "cold_malloc", "1"))

    def test_cold_free_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "cold_free", "1"))

    def test_cold_lock_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "cold_unlock", "1"))

    def test_warm_cxx_delete_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "warm_cxxdelsized", "1"))

    def test_warm_lock_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "warm_trylock", "1"))

    def test_warm_overflow_rejected(self):
        self.assert_rejected(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "warm_alloc_overflow", "4"))

    def test_dry_nonzero_rejected(self):
        def mutate(root):
            def setdry(rows):
                r = next(r for r in rows if r["tag"].startswith("dry"))
                r["warm_malloc"] = "1"
            edit_csv(root / f"{CLEAN}/cases.csv", setdry)
        self.assert_rejected(mutate)

    def test_noopfree_is_deliberately_excluded(self):
        # free(NULL) is not a heap operation; the probe reports it separately and
        # it must not make an otherwise-zero run unclean.
        self.assert_accepted(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "warm_noopfree", "7"))

    def test_cold_noopfree_is_deliberately_excluded(self):
        self.assert_accepted(lambda root: set_nam_field(
            root / f"{CLEAN}/cases.csv", "cold_noopfree", "7"))

    # --- positive control ---------------------------------------------------
    def test_missing_positive_control_rejected(self):
        def mutate(root):
            p = root / "original/a2_wavenet_max/cases.csv"
            fields, rows = read_csv(p)
            for r in rows:
                if r["tag"].startswith("nam"):
                    for k in ("warm_cxxnew", "warm_cxxnewarr", "warm_alloc_cxx_total"):
                        r[k] = "0"
            write_csv(p, fields, rows)
        self.assert_rejected(mutate)

    def test_positive_overflow_and_free_retained(self):
        # Original a2 is a positive control with capture overflow; it must still
        # pass (overflow preserved as a finding, counters exact).
        rc, _ = run_validator(RUNS)
        self.assertEqual(rc, 0)

    # --- F5: runs-tree hygiene ---------------------------------------------
    def test_extra_csv_file_rejected(self):
        def mutate(root):
            (root / f"{CLEAN}/stale-copy.csv").write_text("tag\n")
        self.assert_rejected(mutate)

    def test_extra_file_rejected(self):
        def mutate(root):
            (root / f"{CLEAN}/notes.txt").write_text("x")
        self.assert_rejected(mutate)

    def test_unexpected_variant_dir_rejected(self):
        def mutate(root):
            (root / "bogus").mkdir()
        self.assert_rejected(mutate)

    def test_unexpected_model_dir_rejected(self):
        def mutate(root):
            (root / "new/bogus_model").mkdir()
        self.assert_rejected(mutate)

    def test_unexpected_root_file_rejected(self):
        def mutate(root):
            (root / "stray.csv").write_text("tag\n")
        self.assert_rejected(mutate)

    def test_missing_run_rejected(self):
        self.assert_rejected(lambda root: shutil.rmtree(root / "new/a2_wavenet_max"))

    # --- other structural ---------------------------------------------------
    def test_nonzero_exit_rejected(self):
        self.assert_rejected(lambda root: edit_status(
            root / "new/lstm_control/exit-status.txt",
            lambda d: d.__setitem__("exit", "1")))

    def test_out_rms_mismatch_rejected(self):
        def mutate(root):
            def bump(rows):
                r = next(r for r in rows if r["tag"] == "nam")
                r["out_rms"] = str(float(r["out_rms"]) + 1.0)
            edit_csv(root / "new/a2_wavenet_max/cases.csv", bump)
        self.assert_rejected(mutate)

    def test_model_sha_mismatch_rejected(self):
        self.assert_rejected(lambda root: edit_status(
            root / "new/lstm_control/exit-status.txt",
            lambda d: d.__setitem__("model_sha256", "0" * 64)))

    def test_wrong_variant_recorded_rejected(self):
        self.assert_rejected(lambda root: edit_status(
            root / "lstm_only/a2_wavenet_max/exit-status.txt",
            lambda d: d.__setitem__("variant", "new")))


if __name__ == "__main__":
    unittest.main()
