#!/usr/bin/env python3
"""Independent finite-voxel ray oracle for the frozen orbit-6 sweep."""

import importlib.machinery
import importlib.util
import json
import math
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

REPO = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "scripts/render-orbit-geometry-metric.py").is_file())
METRIC = REPO / "scripts/render-orbit-geometry-metric.py"
sys.path.insert(0, str(METRIC.parent))
loader = importlib.machinery.SourceFileLoader("orbit_metric", str(METRIC))
spec = importlib.util.spec_from_loader("orbit_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)
OCCUPIED = metric.occupied_cells()
MIN_BOUNDS = tuple(min(p[i] for p in OCCUPIED) - 0.5 for i in range(3))
MAX_BOUNDS = tuple(max(p[i] for p in OCCUPIED) + 0.5 for i in range(3))
RGB = {
    "x-": (0, 128, 128), "x+": (255, 128, 128),
    "y-": (128, 0, 128), "y+": (128, 255, 128),
    "z-": (128, 128, 0), "z+": (128, 128, 255),
}
RGB_TO_FACE = {v: k for k, v in RGB.items()}
RGB_TO_FACE[(0, 0, 0)] = None
EPS = 1e-7


def projection(point, sine, cosine):
    x, y, z = point
    vx = cosine * x + sine * y
    vy = -sine * x + cosine * y
    return 640 + 8 * (-vx + vy), 360 + 4 * (-vx - vy + 2 * z)


def roi(sine, cosine):
    xs, ys = [], []
    for x in (MIN_BOUNDS[0], MAX_BOUNDS[0]):
        for y in (MIN_BOUNDS[1], MAX_BOUNDS[1]):
            for z in (MIN_BOUNDS[2], MAX_BOUNDS[2]):
                px, py = projection((x, y, z), sine, cosine)
                xs.append(px)
                ys.append(py)
    return (max(0, math.floor(min(xs)) - 3),
            min(1280, math.ceil(max(xs)) + 4),
            max(0, math.floor(min(ys)) - 3),
            min(720, math.ceil(max(ys)) + 4))


def face_for_axis(axis, direction):
    return "xyz"[axis] + ("-" if direction > 0 else "+")


def ray(gx, gy, sine, cosine):
    u = (gx + 0.5 - 640) / 8
    v = (gy + 0.5 - 360) / 4
    vx, vy, vz = -u / 2 - v / 6, u / 2 - v / 6, v / 3
    origin = (cosine * vx - sine * vy, sine * vx + cosine * vy, vz)
    direction = (cosine - sine, sine + cosine, 1.0)
    lo, hi = -math.inf, math.inf
    entry_axes = []
    for axis in range(3):
        d = direction[axis]
        if abs(d) < 1e-10:
            if not MIN_BOUNDS[axis] < origin[axis] < MAX_BOUNDS[axis]:
                return None
            continue
        t1 = (MIN_BOUNDS[axis] - origin[axis]) / d
        t2 = (MAX_BOUNDS[axis] - origin[axis]) / d
        enter, leave = min(t1, t2), max(t1, t2)
        if enter > lo + EPS:
            lo = enter
            entry_axes = [axis]
        elif abs(enter - lo) <= EPS:
            entry_axes.append(axis)
        hi = min(hi, leave)
    if lo >= hi - EPS:
        return None
    t = lo + EPS
    cell = tuple(math.floor(origin[i] + t * direction[i] + 0.5)
                 for i in range(3))
    for _ in range(90):
        if cell in OCCUPIED:
            return frozenset(face_for_axis(a, direction[a]) for a in entry_axes)
        next_ts = []
        for axis in range(3):
            d = direction[axis]
            if abs(d) < 1e-10:
                next_ts.append(math.inf)
            else:
                boundary = cell[axis] + (0.5 if d > 0 else -0.5)
                next_ts.append((boundary - origin[axis]) / d)
        next_t = min(next_ts)
        if next_t >= hi - EPS:
            return None
        entry_axes = [i for i, value in enumerate(next_ts)
                      if abs(value - next_t) <= EPS]
        cell = tuple(cell[i] + ((1 if direction[i] > 0 else -1)
                                  if i in entry_axes else 0) for i in range(3))
        t = next_t
    raise RuntimeError("DDA escaped 90 voxel steps")


def compare(index, previous_dir, candidate_dir):
    angle = 2 * math.pi * index / 16
    sine, cosine = math.sin(angle), math.cos(angle)
    left, right, top, bottom = roi(sine, cosine)
    old = Image.open(previous_dir / f"shot-{index}.png").convert("RGB")
    new = Image.open(candidate_dir / f"shot-{index}.png").convert("RGB")
    if old.size != (2560, 1440) or new.size != old.size:
        raise ValueError("fixture capture must be 2560x1440")
    old_px, new_px = old.load(), new.load()
    labels = {}
    for gy in range(top - 1, bottom + 1):
        for gx in range(left - 1, right + 1):
            labels[gx, gy] = ray(gx, gy, sine, cosine)
    reports = {"old": Counter(), "candidate": Counter()}
    examples = {"old": [], "candidate": []}
    for gy in range(top, bottom):
        for gx in range(left, right):
            expected = labels[gx, gy]
            boundary = any(labels[gx + dx, gy + dy] != expected
                           for dy in (-1, 0, 1) for dx in (-1, 0, 1))
            for name, pixels in (("old", old_px), ("candidate", new_px)):
                report = reports[name]
                rgb = pixels[2 * gx, 2 * gy]
                observed = RGB_TO_FACE.get(rgb, "invalid")
                report["checked"] += 1
                if observed != "invalid" and (
                        (expected is None and observed is None) or
                        (expected is not None and observed in expected)):
                    continue
                if expected is None:
                    kind = "extra"
                elif observed is None:
                    kind = "missing"
                else:
                    kind = "wrong_face"
                report[kind] += 1
                report[("boundary_" if boundary else "interior_") + kind] += 1
                if len(examples[name]) < 10:
                    examples[name].append([gx, gy, sorted(expected) if expected else None,
                                           observed, boundary])
    return {"yaw_deg": round(index * 22.5, 3), "roi_game": [left, right, top, bottom],
            "old": dict(reports["old"]), "candidate": dict(reports["candidate"]),
            "examples": examples}


if __name__ == "__main__":
    old_dir = Path(sys.argv[1])
    candidate_dir = Path(sys.argv[2])
    results = []
    for index in range(17):
        result = compare(index, old_dir, candidate_dir)
        results.append(result)
        print(index, result["yaw_deg"], result["roi_game"],
              result["old"], result["candidate"], flush=True)
    output = Path(sys.argv[3])
    output.write_text(json.dumps(results, indent=2) + "\n")
