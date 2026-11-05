#!/usr/bin/env python3
"""Localize pixel deltas between two PNGs.

usage: pixdiff.py A.png B.png [--tol 8] [--json OUT]
Prints changed-pixel count (any channel > tol), max delta, bbox, and the
connected clusters (8-neighbour) of changed pixels with bbox and size.
"""
import argparse
import importlib.util
import json
import sys
from pathlib import Path


def load_rc(repo: Path):
    script = repo / "scripts" / "render-compare.py"
    spec = importlib.util.spec_from_file_location("render_compare", script)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tol", type=int, default=8)
    ap.add_argument("--repo", default=".")
    ap.add_argument("--json", default=None)
    ap.add_argument("--clusters", type=int, default=12)
    o = ap.parse_args()
    rc = load_rc(Path(o.repo))
    wa, ha, bpa, pa = rc.read_png(o.a)
    wb, hb, bpb, pb = rc.read_png(o.b)
    if (wa, ha, bpa) != (wb, hb, bpb):
        raise SystemExit(f"size mismatch: {(wa, ha, bpa)} vs {(wb, hb, bpb)}")
    w, h, bpp = wa, ha, bpa
    changed = set()
    anychg = 0
    maxd = 0
    for y in range(h):
        row = y * w * bpp
        if pa[row:row + w * bpp] == pb[row:row + w * bpp]:
            continue
        for x in range(w):
            i = row + x * bpp
            d = max(abs(pa[i + c] - pb[i + c]) for c in range(min(3, bpp)))
            if d:
                anychg += 1
            if d > maxd:
                maxd = d
            if d > o.tol:
                changed.add((x, y))
    clusters = []
    seen = set()
    for p in changed:
        if p in seen:
            continue
        stack = [p]
        seen.add(p)
        pts = []
        while stack:
            q = stack.pop()
            pts.append(q)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    n = (q[0] + dx, q[1] + dy)
                    if n in changed and n not in seen:
                        seen.add(n)
                        stack.append(n)
        xs = [q[0] for q in pts]
        ys = [q[1] for q in pts]
        clusters.append({"size": len(pts), "bbox": [min(xs), min(ys), max(xs), max(ys)]})
    clusters.sort(key=lambda c: -c["size"])
    res = {"a": o.a, "b": o.b, "size": [w, h], "any_changed": anychg,
           "over_tol": len(changed), "max_delta": maxd, "clusters": len(clusters),
           "top": clusters[:o.clusters]}
    if changed:
        xs = [p[0] for p in changed]
        ys = [p[1] for p in changed]
        res["bbox"] = [min(xs), min(ys), max(xs), max(ys)]
    print(json.dumps(res))
    if o.json:
        Path(o.json).write_text(json.dumps(res, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
