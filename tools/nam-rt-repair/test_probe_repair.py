"""Reject incomplete callback evidence and missed C++ heap frees."""
import csv
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
EVIDENCE = HERE.parents[1] / "docs/research/nam-rt-repair"


class ProbeEvidenceTests(unittest.TestCase):
    def check_rejection(self, mutate_rows=None, mutate_findings=None):
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            with (EVIDENCE / "probe/processor-probe-combos.csv").open(newline="") as file:
                reader = csv.DictReader(file)
                fields = reader.fieldnames
                rows = list(reader)
            findings = json.loads((EVIDENCE / "probe/findings.json").read_text())
            if mutate_rows:
                mutate_rows(rows)
            if mutate_findings:
                mutate_findings(findings)
            csv_path = directory / "patched.csv"
            with csv_path.open("w", newline="") as file:
                writer = csv.DictWriter(file, fieldnames=fields)
                writer.writeheader()
                writer.writerows(rows)
            findings_path = directory / "findings.json"
            findings_path.write_text(json.dumps(findings))
            result = subprocess.run([
                sys.executable, str(HERE / "check_probe_repair.py"),
                "--patched-csv", str(csv_path),
                "--patched-findings", str(findings_path),
                "--baseline-csv", str(EVIDENCE / "probe-baseline/processor-probe-combos.csv"),
                "--baseline-findings", str(EVIDENCE / "probe-baseline/findings.json"),
                "--out-json", str(directory / "verdict.json")], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_duplicate_rows_cannot_stand_in_for_matrix(self):
        self.check_rejection(mutate_rows=lambda rows: rows.__setitem__(1, dict(rows[0])))

    def test_missing_scene_checks_are_not_a_pass(self):
        self.check_rejection(mutate_findings=lambda data: data.__setitem__("checks", {}))

    def test_cpp_delete_is_a_heap_free(self):
        def mutate(rows):
            next(row for row in rows if row["tag"] == "nam")["warm_cxxdel"] = "1"
        self.check_rejection(mutate_rows=mutate)

    def test_fractional_counts_cannot_round_to_zero(self):
        self.check_rejection(mutate_rows=lambda rows: rows[0].__setitem__("warm_malloc", "0.5"))


if __name__ == "__main__":
    unittest.main()
