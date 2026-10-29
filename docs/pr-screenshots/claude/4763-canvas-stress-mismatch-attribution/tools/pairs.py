#!/usr/bin/env python3
"""Classify changed pixels between a reference and a capture.

usage: pairs.py REF.png CAP.png [--json OUT]
For every pixel whose RGB differs: record the (ref, cap) colour pair, whether
either side is background, whether the pair is a pure scalar luminance change
(same albedo: cap ~= k * ref per channel), and the Chebyshev distance to the
nearest pixel that is the darker colour of the pair in the UNCHANGED set
(locality to an existing shadow boundary).
"""
import argparse
import importlib.util
import json
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location("rc", REPO / "scripts" / "render-compare.py")
rc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rc)


def proportional(a, b):
    """True when b ~= k*a for one scalar k (8-bit quantisation tolerance)."""
    if max(a) == 0 or max(b) == 0:
        return False
    i = max(range(3), key=lambda c: a[c])
    k = b[i] / a[i]
    return all(abs(b[c] - k * a[c]) <= 2.5 for c in range(3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("cap")
    ap.add_argument("--json")
    ap.add_argument("--bg", default="0,0,0")
    o = ap.parse_args()
    w, h, bpp, a = rc.read_png(o.ref)
    w2, h2, bpp2, b = rc.read_png(o.cap)
    if (w, h, bpp) != (w2, h2, bpp2):
        raise SystemExit(f"size mismatch: {(w, h, bpp)} vs {(w2, h2, bpp2)}")
    bg = tuple(int(v) for v in o.bg.split(","))
    pairs = Counter()
    changed = []
    bg_ref = bg_cap = 0
    for y in range(h):
        r0 = y * w * bpp
        if a[r0:r0 + w * bpp] == b[r0:r0 + w * bpp]:
            continue
        for x in range(w):
            i = r0 + x * bpp
            ca = tuple(a[i:i + 3])
            cb = tuple(b[i:i + 3])
            if ca != cb:
                changed.append((x, y))
                pairs[(ca, cb)] += 1
                bg_ref += ca == bg
                bg_cap += cb == bg
    # silhouette: count object pixels (non-bg) in each image
    obj_ref = sum(1 for i in range(0, len(a), bpp) if tuple(a[i:i + 3]) != bg)
    obj_cap = sum(1 for i in range(0, len(b), bpp) if tuple(b[i:i + 3]) != bg)
    prop = sum(n for (ca, cb), n in pairs.items() if proportional(ca, cb))
    rows = []
    for (ca, cb), n in pairs.most_common():
        k = (sum(cb) / sum(ca)) if sum(ca) else None
        rows.append({"ref": ca, "cap": cb, "count": n,
                     "scalar": proportional(ca, cb),
                     "k": round(k, 3) if k else None})
    res = {"ref": o.ref, "cap": o.cap, "changed": len(changed),
           "background_in_ref": bg_ref, "background_in_cap": bg_cap,
           "object_pixels_ref": obj_ref, "object_pixels_cap": obj_cap,
           "scalar_luminance_pairs": prop, "distinct_pairs": len(pairs),
           "darker_in_cap": sum(n for (ca, cb), n in pairs.items() if sum(cb) < sum(ca)),
           "brighter_in_cap": sum(n for (ca, cb), n in pairs.items() if sum(cb) > sum(ca)),
           "pairs": rows[:40]}
    if changed:
        xs = [p[0] for p in changed]
        ys = [p[1] for p in changed]
        res["bbox"] = [min(xs), min(ys), max(xs), max(ys)]
    print(json.dumps({k: v for k, v in res.items() if k != "pairs"}))
    for r in rows[:12]:
        print("   ", r)
    if o.json:
        Path(o.json).write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
