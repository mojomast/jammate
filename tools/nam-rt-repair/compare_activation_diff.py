#!/usr/bin/env python3
"""RT-005 differential comparator: pinned-upstream vs patched NAM activations.

Reads the raw float outputs and JSON summaries produced by the two
NamActivationDiffTest processes (one linked against the pinned upstream NAM, one
against the generated patched overlay) and checks:

  * metadata (seed, model, model_fnv, warm_blocks, block, total_floats) is
    identical in both summaries;
  * exactly the four required sections (prelu, gating, blending, model) in order,
    no missing/empty/duplicate;
  * per-section offset/count is contiguous, non-overlapping and covers the whole
    binary exactly;
  * the declared per-section FNV-1a over the raw float bytes and the raw float
    summaries are recomputed from the binary (catches tampered summaries);
  * every sample is finite in both binaries;
  * every sample is within |a-b| <= ABS_TOL + REL_TOL * max(|a|,|b|);
  * allocation accounting: the *patched* process performs zero C++ `operator
    new`/`delete` calls inside every measured section, and the *pinned upstream*
    process performs at least one allocation in every section marked
    `expect_orig_positive` (the positive control).

`bit_exact` is a raw-byte property, not a float-equality property.
"""
import argparse
import json
import math
import struct
import sys

ABS_TOL = 1.0e-5
REL_TOL = 1.0e-5
REQUIRED_SECTIONS = ["prelu", "gating", "blending", "model"]
# Sections that must show positive C++ allocations in the pinned upstream
# process (they exercise ActivationPReLU::apply). The comparator reads the flag
# from the summaries but also requires exactly this set.
POSITIVE_CONTROL_SECTIONS = {"prelu", "gating", "blending", "model"}
METADATA_KEYS = ("seed", "model", "model_fnv", "warm_blocks", "block", "total_floats")


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
        for byte in struct.pack("<f", v):
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
    for key in METADATA_KEYS:
        if ojson.get(key) != pjson.get(key):
            errors.append(f"metadata mismatch {key}: {ojson.get(key)} vs {pjson.get(key)}")
    for key in ("seed", "warm_blocks", "block", "total_floats"):
        if not isinstance(ojson.get(key), int):
            errors.append(f"missing/invalid integer metadata {key}: {ojson.get(key)!r}")

    block = ojson.get("block") or 0
    warm = ojson.get("warm_blocks") or 0

    # --- section list --------------------------------------------------------
    def sections_of(j):
        s = j.get("sections")
        return s if isinstance(s, list) else []

    osc, psc = sections_of(ojson), sections_of(pjson)
    onames = [s.get("name") for s in osc]
    pnames = [s.get("name") for s in psc]
    if onames != REQUIRED_SECTIONS:
        errors.append(f"original section list must be exactly {REQUIRED_SECTIONS}, got {onames}")
    if pnames != REQUIRED_SECTIONS:
        errors.append(f"patched section list must be exactly {REQUIRED_SECTIONS}, got {pnames}")

    # --- structure: contiguous coverage and expected counts ------------------
    for label, sc, data in (("original", osc, orig), ("patched", psc, patched)):
        cursor = 0
        for s in sc:
            name = s.get("name")
            n = s.get("count")
            off = s.get("offset")
            if type(n) is not int or n <= 0:
                errors.append(f"{label}/{name}: invalid count {n!r}")
                continue
            if off != cursor:
                errors.append(f"{label}/{name}: offset {off} != contiguous cursor {cursor}")
            cursor = (off if isinstance(off, int) else cursor) + n
        if cursor != len(data):
            errors.append(f"{label}: declared coverage ends at {cursor}, binary has {len(data)} floats")

    if ojson.get("total_floats") != len(orig):
        errors.append("original total_floats != binary float count")
    if pjson.get("total_floats") != len(patched):
        errors.append("patched total_floats != binary float count")
    if len(orig) != len(patched):
        errors.append(f"binary float count differs: {len(orig)} vs {len(patched)}")

    # --- recompute declared raw summaries ------------------------------------
    def recompute(sc, data, label):
        for s in sc:
            name = s.get("name")
            off, n = s.get("offset"), s.get("count")
            if not isinstance(off, int) or not isinstance(n, int) or off < 0 or n <= 0 \
               or off + n > len(data):
                errors.append(f"{label}/{name}: declared range out of binary bounds")
                continue
            vals = data[off:off + n]
            if fnv1a(vals) != s.get("fnv1a"):
                errors.append(f"{label}/{name}: declared FNV != raw bytes")
            if not math.isclose(math.fsum(vals), s.get("sum", 0.0), rel_tol=1e-6, abs_tol=1e-6):
                errors.append(f"{label}/{name}: declared sum != raw bytes")
            if not math.isclose(min(vals), s.get("min", 0.0), rel_tol=1e-6, abs_tol=1e-6) \
               or not math.isclose(max(vals), s.get("max", 0.0), rel_tol=1e-6, abs_tol=1e-6):
                errors.append(f"{label}/{name}: declared min/max != raw bytes")
            if s.get("kind") not in ("prelu", "gating", "blending", "model"):
                errors.append(f"{label}/{name}: unknown kind {s.get('kind')!r}")

    recompute(osc, orig, "original")
    recompute(psc, patched, "patched")

    # --- allocation accounting ----------------------------------------------
    def alloc_map(sc):
        return {s.get("name"): s for s in sc}

    oalloc, palloc = alloc_map(osc), alloc_map(psc)
    for name in REQUIRED_SECTIONS:
        if name not in oalloc or name not in palloc:
            continue
        o = oalloc[name].get("alloc")
        p = palloc[name].get("alloc")
        of = oalloc[name].get("free")
        pf = palloc[name].get("free")
        eo = oalloc[name].get("expect_orig_positive")
        ep = palloc[name].get("expect_orig_positive")
        if eo is not ep:
            errors.append(f"{name}: expect_orig_positive differs original={eo} patched={ep}")
        expected_positive = name in POSITIVE_CONTROL_SECTIONS
        if eo is not expected_positive:
            errors.append(f"{name}: expect_orig_positive must be {expected_positive}, got {eo}")
        if not isinstance(p, int) or p != 0:
            errors.append(f"{name}: patched C++ allocations must be 0, got {p!r}")
        if pf != 0:
            errors.append(f"{name}: patched C++ frees must be 0, got {pf!r}")
        if expected_positive and (not isinstance(o, int) or o <= 0):
            errors.append(f"{name}: pinned upstream must allocate (>0) as positive control, got {o!r}")
        if not isinstance(o, int) or o < 0:
            errors.append(f"{name}: invalid original allocation count {o!r}")

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
        "sections": [], "max_abs_diff": 0.0, "max_rel_diff": 0.0,
        "raw_bytes_identical": ob == pb, "errors": list(errors),
    }
    budget_fail = False
    if len(orig) == len(patched):
        for os_, ps_ in zip(osc, psc):
            name = os_.get("name")
            off, n = os_.get("offset"), os_.get("count")
            if off != ps_.get("offset") or n != ps_.get("count") or not isinstance(n, int) \
               or not isinstance(off, int) or off + n > len(orig):
                result["sections"].append({"name": name, "pass": False, "note": "range mismatch"})
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
            result["sections"].append({"name": name, "count": n,
                                       "max_abs_diff": max_abs, "max_rel_diff": max_rel,
                                       "pass": first_bad < 0, "first_failing_index": first_bad})
    else:
        budget_fail = True

    result["budget_pass"] = not budget_fail
    result["bit_exact"] = result["raw_bytes_identical"]
    result["all_pass"] = (not errors) and not budget_fail
    json.dump(result, open(args.out_json, "w"), indent=2)

    for e in errors:
        print(f"  ERROR  {e}", file=sys.stderr)
    for c in result["sections"]:
        if "count" in c:
            print(f"  {'PASS' if c['pass'] else 'FAIL'}  {c['name']:10s} n={c['count']:8d} "
                  f"max_abs={c['max_abs_diff']:.3e} max_rel={c['max_rel_diff']:.3e}")
    print(f"metadata/finite/raw-summaries/alloc: {'OK' if not errors else 'FAILED'}; "
          f"within-budget: {result['budget_pass']}; "
          f"raw_bytes_identical: {result['raw_bytes_identical']}")
    print(f"overall {'PASS' if result['all_pass'] else 'FAIL'}")
    return 0 if result["all_pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
