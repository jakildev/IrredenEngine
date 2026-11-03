#!/usr/bin/env python3
"""Darker / brighter changed pixels between two captures, and their isolated share.

usage: census.py REF.png CAP.png [REPO]

A pixel is darker or brighter by its RGB sum in CAP against REF. Changed pixels
of one class form 8-connected components. A component is isolated when no
unchanged 8-neighbour carries one of its own dark shades (CAP's value for the
darker class, REF's for the brighter class), so it is not part of a larger
shadow region.
"""
import importlib.util
import json
import sys
from pathlib import Path


def main():
    repo = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(".")
    spec = importlib.util.spec_from_file_location("rc", repo / "scripts/render-compare.py")
    rc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rc)
    w, h, bpp, a = rc.read_png(sys.argv[1])
    _, _, _, b = rc.read_png(sys.argv[2])

    def px(buf, x, y):
        i = (y * w + x) * bpp
        return tuple(buf[i:i + 3])

    cls = {}
    for y in range(h):
        r = y * w * bpp
        if a[r:r + w * bpp] == b[r:r + w * bpp]:
            continue
        for x in range(w):
            pa, pb = px(a, x, y), px(b, x, y)
            if pa != pb:
                cls[(x, y)] = "darker" if sum(pb) < sum(pa) else "brighter"
    out = {}
    for kind in ("darker", "brighter"):
        pts = {p for p, k in cls.items() if k == kind}
        seen, isolated = set(), 0
        for p in pts:
            if p in seen:
                continue
            comp, stack = [], [p]
            seen.add(p)
            while stack:
                q = stack.pop()
                comp.append(q)
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        n = (q[0] + dx, q[1] + dy)
                        if n in pts and n not in seen:
                            seen.add(n)
                            stack.append(n)
            shades = {px(b if kind == "darker" else a, *q) for q in comp}
            members = set(comp)
            joined = any(
                px(b, q[0] + dx, q[1] + dy) in shades
                for q in comp for dx in (-1, 0, 1) for dy in (-1, 0, 1)
                if (q[0] + dx, q[1] + dy) not in members
                and 0 <= q[0] + dx < w and 0 <= q[1] + dy < h
                and (q[0] + dx, q[1] + dy) not in cls)
            if not joined:
                isolated += len(comp)
        out[kind] = {"pixels": len(pts), "isolated": isolated}
    print(json.dumps(out))


if __name__ == "__main__":
    main()
