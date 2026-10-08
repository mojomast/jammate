#!/usr/bin/env python3
"""RT-003 differential comparator: pinned-upstream vs patched NAM LSTM.

Reads the raw float outputs and JSON summaries produced by the two
NamLstmDiffTest processes (one linked against the pinned upstream NAM, one
against the generated patched overlay) and checks every sample within a
predeclared tolerance.

Fail-closed structural + numerical checks (all must hold to pass):

  * metadata (seed, warm_blocks, block) identical in both summaries;
  * exactly the six required configurations, no missing/empty/duplicate;
  * per-config offset/count contiguous, non-overlapping and covering the whole
    binary, with count == out_channels * block * (warm_blocks + 1);
  * the declared per-config FNV-1a over the raw float bytes and the raw float
    summaries are recomputed from the binary (catches tampered summaries);
  * every sample finite in both binaries;
  * every sample within |a-b| <= ABS_TOL + REL_TOL * max(|a|,|b|).

`bit_exact` is reported true only when the two raw binaries are byte-identical;
a within-budget but not byte-identical pair still passes (bit_exact=false).
Diff == 0 on float values is not byte identity (it does not distinguish signed
zero or NaN payloads), which is why the field comes from the bytes.
"""
import argparse
import json
import math
import struct
import sys

ABS_TOL = 1.0e-5
REL_TOL = 1.0e-5
REQUIRED_CONFIGS = ["mono_1x3", "mono_2x5", "mono_4x16",
                    "stereo_3x7", "multi_io_2x11", "zero_layer"]


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def decode_floats(data):
    if len(data) % 4 != 0:
        raise ValueError(f"size {len(data)} not a multiple of 4")
    return list(struct.unpack("<%df" % (len(data) // 4), data))


def fnv1a(values):
    h = 2166136261
    for v in values:
        b = struct.pack("<f", v)
        for byte in b:
            h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--original-bin", required=True)
    ap.add_argument("--patched-bin", required=True)
    ap.add_argument("--original-json", required=True)
    ap.add_argument("--patched-json", required=True)
    ap.add_argument("--out-json", required=True)
    args = ap.parse_args()

    errors = []

    ob = read_bytes(args.original_bin)
    pb = read_bytes(args.patched_bin)
    try:
        orig = decode_floats(ob)
        patched = decode_floats(pb)
    except ValueError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 2

    try:
        ojson = json.load(open(args.original_json))
        pjson = json.load(open(args.patched_json))
    except Exception as e:
        print(f"FAIL: cannot read summaries: {e}", file=sys.stderr)
        return 2

    # --- metadata ------------------------------------------------------------
    for key in ("seed", "warm_blocks", "block"):
        if ojson.get(key) != pjson.get(key):
            errors.append(f"metadata mismatch {key}: {ojson.get(key)} vs {pjson.get(key)}")
    if ojson.get("seed") is None or ojson.get("warm_blocks") is None or ojson.get("block") is None:
        errors.append("missing experiment metadata (seed/warm_blocks/block)")

    block = ojson.get("block") or 0
    warm = ojson.get("warm_blocks") or 0
    if type(block) is not int or block <= 0 or type(warm) is not int or warm <= 0:
        errors.append("block and warm_blocks must be positive integers")
    frames = (warm + 1) if (block and warm) else 0

    # --- config list ---------------------------------------------------------
    def configs_of(j):
        cs = j.get("configs")
        return cs if isinstance(cs, list) else []

    oc, pc = configs_of(ojson), configs_of(pjson)
    onames = [c.get("name") for c in oc]
    pnames = [c.get("name") for c in pc]
    if onames != REQUIRED_CONFIGS:
        errors.append(f"original config list must be exactly {REQUIRED_CONFIGS}, got {onames}")
    if pnames != REQUIRED_CONFIGS:
        errors.append(f"patched config list must be exactly {REQUIRED_CONFIGS}, got {pnames}")
    if len(set(onames)) != len(onames):
        errors.append("duplicate config names in original summary")
    if len(set(pnames)) != len(pnames):
        errors.append("duplicate config names in patched summary")

    # --- structure: contiguous coverage and expected counts ------------------
    expected_total = 0
    for label, cs, data in (("original", oc, orig), ("patched", pc, patched)):
        cursor = 0
        for c in cs:
            name = c.get("name")
            n = c.get("count")
            off = c.get("offset")
            oc_ch = c.get("out_channels")
            if type(n) is not int or n <= 0:
                errors.append(f"{label}/{name}: invalid count {n!r}")
                continue
            if type(oc_ch) is not int or oc_ch <= 0:
                errors.append(f"{label}/{name}: missing or invalid output channel count")
            if off != cursor:
                errors.append(f"{label}/{name}: offset {off} is not the contiguous cursor {cursor} "
                              f"(gap/overlap)")
            if frames and isinstance(oc_ch, int):
                want = oc_ch * block * frames
                if n != want:
                    errors.append(f"{label}/{name}: count {n} != out_channels*block*(warm+1) = {want}")
            cursor = (off if isinstance(off, int) else cursor) + n
        if cursor != len(data):
            errors.append(f"{label}: declared coverage ends at {cursor}, binary has {len(data)} floats")
        expected_total = len(data)

    if len(orig) != len(patched):
        errors.append(f"binary float count differs: {len(orig)} vs {len(patched)}")

    # --- recompute declared raw summaries ------------------------------------
    def recompute(cs, data, label):
        for c in cs:
            name = c.get("name")
            off, n = c.get("offset"), c.get("count")
            if not isinstance(off, int) or not isinstance(n, int) or off < 0 or n < 0 \
               or off + n > len(data):
                errors.append(f"{label}/{name}: declared range out of binary bounds")
                continue
            vals = data[off:off + n]
            got_fnv = fnv1a(vals)
            if got_fnv != c.get("fnv1a"):
                errors.append(f"{label}/{name}: declared FNV {c.get('fnv1a')} != raw {got_fnv}")
            got_sum = math.fsum(vals)
            if not math.isclose(got_sum, c.get("sum", 0.0), rel_tol=1e-6, abs_tol=1e-6):
                errors.append(f"{label}/{name}: declared sum {c.get('sum')} != raw {got_sum}")
            if vals and (not math.isclose(min(vals), c.get("min", 0.0), rel_tol=1e-6, abs_tol=1e-6)
                         or not math.isclose(max(vals), c.get("max", 0.0), rel_tol=1e-6, abs_tol=1e-6)):
                errors.append(f"{label}/{name}: declared min/max do not match raw bytes")

    recompute(oc, orig, "original")
    recompute(pc, patched, "patched")

    # --- finiteness ----------------------------------------------------------
    for label, data in (("original", orig), ("patched", patched)):
        for i, v in enumerate(data):
            if not math.isfinite(v):
                errors.append(f"{label}: non-finite value at index {i}: {v}")
                break
    if not ojson.get("all_finite", True):
        errors.append("original summary reports all_finite=false")
    if not pjson.get("all_finite", True):
        errors.append("patched summary reports all_finite=false")

    # --- per-sample tolerance ------------------------------------------------
    result = {
        "abs_tol": ABS_TOL, "rel_tol": REL_TOL, "block": block,
        "warm_blocks": warm, "seed": ojson.get("seed"),
        "configs": [], "max_abs_diff": 0.0, "max_rel_diff": 0.0,
        "raw_bytes_identical": ob == pb, "errors": list(errors),
    }
    budget_fail = False
    if len(orig) == len(patched):
        for oc_c, pc_c in zip(oc, pc):
            name = oc_c.get("name")
            off, n = oc_c.get("offset"), oc_c.get("count")
            if off != pc_c.get("offset") or n != pc_c.get("count") or not isinstance(n, int) \
               or off is None or off + n > len(orig):
                result["configs"].append({"name": name, "pass": False,
                                          "note": "range mismatch"})
                budget_fail = True
                continue
            max_abs = max_rel = 0.0
            first_bad = -1
            for k in range(n):
                a, b = orig[off + k], patched[off + k]
                d = abs(a - b)
                denom = max(abs(a), abs(b))
                r = d / denom if denom > 0 else 0.0
                if d > ABS_TOL + REL_TOL * denom:
                    budget_fail = True
                    if first_bad < 0:
                        first_bad = k
                if d > max_abs:
                    max_abs = d
                if r > max_rel:
                    max_rel = r
            result["max_abs_diff"] = max(result["max_abs_diff"], max_abs)
            result["max_rel_diff"] = max(result["max_rel_diff"], max_rel)
            result["configs"].append({"name": name, "count": n,
                                      "max_abs_diff": max_abs, "max_rel_diff": max_rel,
                                      "pass": first_bad < 0, "first_failing_index": first_bad})
    else:
        budget_fail = True

    result["budget_pass"] = not budget_fail
    # bit-exact is a raw-byte property, not a float-equality property.
    result["bit_exact"] = result["raw_bytes_identical"]
    result["all_pass"] = (not errors) and not budget_fail
    json.dump(result, open(args.out_json, "w"), indent=2)

    for e in errors:
        print(f"  ERROR  {e}", file=sys.stderr)
    for c in result["configs"]:
        if "count" in c:
            print(f"  {'PASS' if c['pass'] else 'FAIL'}  {c['name']:16s} n={c['count']:8d} "
                  f"max_abs={c['max_abs_diff']:.3e} max_rel={c['max_rel_diff']:.3e}")
    print(f"metadata/finite/raw-summaries: {'OK' if not errors else 'FAILED'}; "
          f"within-budget: {result['budget_pass']}; "
          f"raw_bytes_identical: {result['raw_bytes_identical']} "
          f"(bit_exact={result['bit_exact']})")
    print(f"overall {'PASS' if result['all_pass'] else 'FAIL'}")
    return 0 if result["all_pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
