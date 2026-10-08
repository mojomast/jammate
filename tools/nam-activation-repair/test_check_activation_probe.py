#!/usr/bin/env python3
"""Adversarial acceptance tests for RT-005 check_activation_probe.py.

The evidence test runs the validator against the committed run tree. The
remaining tests copy the real run tree into a temporary directory, inject one
coherent defect, and require the validator to fail closed. Each defect is the
kind that would let a wrong archive or an incomplete matrix pass silently.
"""
import csv
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
RUNS = REPO / "docs/research/nam-activation-repair/runs"
PREDECLARED = HERE / "predeclared.json"
MODELS_DIR = pathlib.Path(
    "/home/mojo/projects/guitars/third_party/NeuralAmpModelerCore/example_models")


def run_validator(runs_dir):
    with tempfile.TemporaryDirectory() as d:
        out = pathlib.Path(d) / "verdict.json"
        r = subprocess.run(
            [sys.executable, str(HERE / "check_activation_probe.py"),
             "--runs", str(runs_dir),
             "--predeclared", str(PREDECLARED),
             "--models-dir", str(MODELS_DIR),
             "--out-json", str(out)],
            capture_output=True, text=True)
        return r.returncode, out


def edit_csv(path, mutate):
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames
        rows = list(reader)
    mutate(rows)
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def edit_status(path, mutate):
    with open(path) as f:
        lines = [l for l in f.read().splitlines() if "=" in l]
    d = dict(l.split("=", 1) for l in lines)
    mutate(d)
    with open(path, "w") as f:
        for k, v in d.items():
            f.write(f"{k}={v}\n")


class ActivationProbeTests(unittest.TestCase):
    def assert_rejected(self, mutate):
        with tempfile.TemporaryDirectory() as d:
            copy = pathlib.Path(d) / "runs"
            shutil.copytree(RUNS, copy)
            mutate(copy)
            rc, _ = run_validator(copy)
            self.assertNotEqual(rc, 0)

    def test_committed_evidence_passes(self):
        rc, _ = run_validator(RUNS)
        self.assertEqual(rc, 0)

    def test_new_archive_allocation_rejected(self):
        def mutate(root):
            edit_csv(root / "new/a2_wavenet_max/cases.csv", lambda rows: next(
                r for r in rows if r["tag"] == "nam").__setitem__(
                "warm_cxxnew", str(int(next(
                    r for r in rows if r["tag"] == "nam")["warm_cxxnew"]) + 1)))
        self.assert_rejected(mutate)

    def test_missing_positive_control_rejected(self):
        def mutate(root):
            def zero(rows):
                for r in rows:
                    if r["tag"].startswith("nam"):
                        for k in ("warm_cxxnew", "warm_cxxnewarr"):
                            r[k] = "0"
            edit_csv(root / "original/a2_wavenet_max/cases.csv", zero)
        self.assert_rejected(mutate)

    def test_missing_run_rejected(self):
        self.assert_rejected(lambda root: shutil.rmtree(root / "new/a2_wavenet_max"))

    def test_nonzero_exit_rejected(self):
        self.assert_rejected(lambda root: edit_status(
            root / "new/lstm_control/exit-status.txt",
            lambda d: d.__setitem__("exit", "1")))

    def test_dry_nonzero_rejected(self):
        def mutate(root):
            def setdry(rows):
                r = next(r for r in rows if r["tag"].startswith("dry"))
                r["warm_malloc"] = "1"
            edit_csv(root / "new/lstm_control/cases.csv", setdry)
        self.assert_rejected(mutate)

    def test_out_rms_mismatch_rejected(self):
        def mutate(root):
            def bump(rows):
                r = next(r for r in rows if r["tag"] == "nam")
                r["out_rms"] = str(float(r["out_rms"]) + 1.0)
            edit_csv(root / "new/a2_wavenet_max/cases.csv", bump)
        self.assert_rejected(mutate)

    def test_capture_overflow_in_clean_run_rejected(self):
        def mutate(root):
            def overflow(rows):
                r = next(r for r in rows if r["tag"] == "nam")
                r["warm_alloc_overflow"] = "4"
            edit_csv(root / "new/lstm_control/cases.csv", overflow)
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
