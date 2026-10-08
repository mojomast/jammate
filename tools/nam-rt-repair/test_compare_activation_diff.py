#!/usr/bin/env python3
"""Adversarial acceptance tests for RT-005 compare_activation_diff.py.

Every fixture here is a coherent scenario that the comparator must ACCEPT unless
the specific defect is injected. The tests drive the real comparator script via a
subprocess, so the thing under test is exactly what runs in ctest.
"""
import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
COMPARATOR = HERE / "compare_activation_diff.py"

SECTIONS = [
    ("prelu", "prelu", [0.0, -1.0, 0.5, -0.25]),
    ("gating", "gating", [1.0, -2.0, 3.0]),
    ("blending", "blending", [0.125, -0.5, 2.0]),
    ("model", "model", [0.0, 0.0, 0.5, -0.5, 1.5]),
]


def fnv1a(values):
    h = 2166136261
    for v in values:
        for byte in struct.pack("<f", v):
            h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def make_scenario(patched=False, mutate=None):
    """Return (original_bin, patched_bin, original_json, patched_json dicts)."""
    orig_floats = []
    for _, _, vals in SECTIONS:
        orig_floats.extend(vals)
    # patched floats are byte-identical (the accepted strongest case) unless
    # the caller mutates them through the callback.
    patched_floats = list(orig_floats)

    sections = []
    cursor = 0
    for name, kind, vals in SECTIONS:
        sections.append({
            "name": name, "kind": kind, "offset": cursor, "count": len(vals),
            "sum": math.fsum(vals), "sumsq": math.fsum(v * v for v in vals),
            "min": min(vals), "max": max(vals), "fnv1a": fnv1a(vals),
            "alloc": 100, "free": 0, "expect_orig_positive": True,
        })
        cursor += len(vals)

    oj = {"seed": 1, "model": "/m.nam", "model_fnv": "abcd", "warm_blocks": 4,
          "block": 8, "all_finite": True, "total_floats": len(orig_floats),
          "sections": [dict(s) for s in sections]}
    pj = {"seed": 1, "model": "/m.nam", "model_fnv": "abcd", "warm_blocks": 4,
          "block": 8, "all_finite": True, "total_floats": len(patched_floats),
          "sections": [dict(s) for s in sections]}
    if patched:
        # The repaired process is allocation-free in every measured section.
        for s in pj["sections"]:
            s["alloc"] = 0
            s["free"] = 0

    if mutate:
        mutate(oj, pj, orig_floats, patched_floats)

    def to_bytes(floats):
        return struct.pack("<%df" % len(floats), *floats)

    return to_bytes(orig_floats), to_bytes(patched_floats), oj, pj


def run_comparator(ob, pb, oj, pj):
    with tempfile.TemporaryDirectory() as d:
        d = pathlib.Path(d)
        (d / "o.bin").write_bytes(ob)
        (d / "p.bin").write_bytes(pb)
        (d / "o.json").write_text(json.dumps(oj))
        (d / "p.json").write_text(json.dumps(pj))
        out = d / "verdict.json"
        r = subprocess.run([sys.executable, str(COMPARATOR),
                            "--original-bin", str(d / "o.bin"),
                            "--patched-bin", str(d / "p.bin"),
                            "--original-json", str(d / "o.json"),
                            "--patched-json", str(d / "p.json"),
                            "--out-json", str(out)],
                           capture_output=True, text=True)
        verdict = json.loads(out.read_text()) if out.exists() else {}
        return r.returncode, verdict


class ComparatorTests(unittest.TestCase):
    def assert_rejected(self, mutate):
        ob, pb, oj, pj = make_scenario(patched=True, mutate=mutate)
        rc, verdict = run_comparator(ob, pb, oj, pj)
        self.assertNotEqual(rc, 0, verdict)

    def test_clean_scenario_passes(self):
        # Pinned upstream has alloc > 0, patched has alloc == 0, byte-identical.
        ob, pb, oj, pj = make_scenario(patched=True)
        rc, verdict = run_comparator(ob, pb, oj, pj)
        self.assertEqual(rc, 0, verdict)
        self.assertTrue(verdict["bit_exact"])

    def test_missing_section_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: pj["sections"].pop())

    def test_duplicate_section_rejected(self):
        def mutate(oj, pj, *_):
            pj["sections"][1]["name"] = "prelu"
        self.assert_rejected(mutate)

    def test_offset_gap_rejected(self):
        def mutate(oj, pj, *_):
            oj["sections"][2]["offset"] += 1
        self.assert_rejected(mutate)

    def test_tampered_fnv_rejected(self):
        def mutate(oj, pj, *_):
            oj["sections"][0]["fnv1a"] ^= 0xFFFF
        self.assert_rejected(mutate)

    def test_tampered_sum_rejected(self):
        def mutate(oj, pj, *_):
            oj["sections"][0]["sum"] += 1.0
        self.assert_rejected(mutate)

    def test_nonfinite_value_rejected(self):
        def mutate(oj, pj, orig, patched):
            patched[0] = float("nan")
            pj["sections"][0]["fnv1a"] = fnv1a(patched[:pj["sections"][0]["count"]])
        self.assert_rejected(mutate)

    def test_all_finite_false_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: pj.__setitem__("all_finite", False))

    def test_metadata_mismatch_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: pj.__setitem__("block", 16))

    def test_patched_allocation_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: pj["sections"][0].__setitem__("alloc", 1))

    def test_patched_free_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: pj["sections"][2].__setitem__("free", 1))

    def test_missing_positive_control_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: oj["sections"][0].__setitem__("alloc", 0))

    def test_wrong_positive_flag_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: oj["sections"][0].__setitem__("expect_orig_positive", False))

    def test_wrong_kind_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: pj["sections"][0].__setitem__("kind", "bogus"))

    def test_out_of_budget_value_rejected(self):
        def mutate(oj, pj, orig, patched):
            patched[0] = 10.0
            pj["sections"][0].update({
                "sum": math.fsum(patched[:pj["sections"][0]["count"]]),
                "fnv1a": fnv1a(patched[:pj["sections"][0]["count"]]),
                "min": min(patched[:pj["sections"][0]["count"]]),
                "max": max(patched[:pj["sections"][0]["count"]]),
            })
        self.assert_rejected(mutate)

    def test_total_floats_wrong_rejected(self):
        self.assert_rejected(lambda oj, pj, *_: pj.__setitem__("total_floats", 999))

    def test_count_mismatch_rejected(self):
        def mutate(oj, pj, *_):
            oj["sections"][1]["count"] += 1
        self.assert_rejected(mutate)


if __name__ == "__main__":
    unittest.main()
