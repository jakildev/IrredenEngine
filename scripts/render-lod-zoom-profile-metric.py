#!/usr/bin/env python3
"""Pixel gate for ShapeDebug's --lod-zoom-profile fixture.

Every shot logs one ``LOD-PROFILE-REGION`` line per variant pair (window
points, the variant the pair must show) before its ``Saved screenshot`` line.
Inside each box this reads the capture: the coarse member is red, the fine
member blue, so a region passes when the expected colour covers a real
footprint and the other colour is absent. A stacked or stale variant shows as
the wrong colour; a missing one as an empty footprint.

Run the fixture first, keeping its log:

    fleet-run IRShapeDebug --lod-zoom-profile --auto-screenshot 20 > lod.log 2>&1
    python3 scripts/render-lod-zoom-profile-metric.py --log lod.log \\
        --screenshots build/creations/demos/shape_debug

Pure stdlib.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from functools import lru_cache
from pathlib import Path

from render_metric_util import read_png

REGION = re.compile(
    r"LOD-PROFILE-REGION shot=(\d+) name=(\S+) expect=(coarse|fine) "
    r"x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+) window=(\d+)x(\d+)"
)
SAVED = re.compile(r"Saved screenshot: (\S+\.png)")

# Shaded faces of the fixture's red (225,55,45) and blue (50,95,235) members
# stay within these ratios; the clear colour is black.
MIN_CHANNEL = 40
# A footprint smaller than this share of its box is a missing variant.
MIN_EXPECTED_SHARE = 0.02


def classify(r: int, g: int, b: int) -> str | None:
    if r >= MIN_CHANNEL and r > 1.8 * g and r > 1.8 * b:
        return "coarse"
    if b >= MIN_CHANNEL and b > 1.5 * r and b > 1.2 * g:
        return "fine"
    return None


def parse_log(text: str) -> tuple[dict[int, list[dict]], list[str]]:
    regions: dict[int, list[dict]] = {}
    saved: list[str] = []
    for line in text.splitlines():
        if m := REGION.search(line):
            shot = int(m.group(1))
            regions.setdefault(shot, []).append({
                "name": m.group(2),
                "expect": m.group(3),
                "box": tuple(int(m.group(i)) for i in range(4, 8)),
                "window": (int(m.group(8)), int(m.group(9))),
            })
        elif m := SAVED.search(line):
            saved.append(m.group(1))
    return regions, saved


def overlaps(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> bool:
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


@lru_cache(maxsize=2)
def decode(png: Path) -> tuple[int, int, int, bytes]:
    return read_png(str(png))


def measure(png: Path, region: dict) -> dict:
    width, height, bpp, pixels = decode(png)
    scale_x = width / region["window"][0]
    scale_y = height / region["window"][1]
    x0, y0, x1, y1 = region["box"]
    px0 = max(0, int(x0 * scale_x))
    py0 = max(0, int(y0 * scale_y))
    px1 = min(width, int(x1 * scale_x) + 1)
    py1 = min(height, int(y1 * scale_y) + 1)
    counts = {"coarse": 0, "fine": 0}
    for y in range(py0, py1):
        row = y * width
        for x in range(px0, px1):
            i = (row + x) * bpp
            label = classify(pixels[i], pixels[i + 1], pixels[i + 2])
            if label is not None:
                counts[label] += 1
    area = max(1, (px1 - px0) * (py1 - py0))
    expected = region["expect"]
    other = "fine" if expected == "coarse" else "coarse"
    passed = counts[expected] >= MIN_EXPECTED_SHARE * area and counts[other] == 0
    return {
        "name": region["name"],
        "expect": expected,
        "coarse_px": counts["coarse"],
        "fine_px": counts["fine"],
        "box_px": area,
        "pass": passed,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--log", required=True, type=Path, help="the fixture run's log")
    ap.add_argument(
        "--screenshots",
        required=True,
        type=Path,
        help="the directory the logged screenshot paths are relative to (the exe dir)",
    )
    ap.add_argument("--json", action="store_true", help="print per-region results as JSON")
    args = ap.parse_args()

    regions, saved = parse_log(args.log.read_text(errors="replace"))
    if not regions:
        print("no LOD-PROFILE-REGION lines in the log", file=sys.stderr)
        return 2
    shots = sorted(regions)
    if shots != list(range(len(shots))) or len(saved) < len(shots):
        print(f"shots {shots} do not pair with {len(saved)} saved screenshots", file=sys.stderr)
        return 2

    failed: list[str] = []
    results = []
    for shot in shots:
        boxes = regions[shot]
        for i, a in enumerate(boxes):
            for b in boxes[i + 1:]:
                if overlaps(a["box"], b["box"]):
                    failed.append(f"shot {shot}: regions {a['name']} and {b['name']} overlap")
        png = args.screenshots / saved[shot]
        for region in boxes:
            result = measure(png, region)
            result["shot"] = shot
            results.append(result)
            if not result["pass"]:
                failed.append(
                    f"shot {shot} {result['name']}: expected {result['expect']}, "
                    f"coarse={result['coarse_px']} fine={result['fine_px']} "
                    f"box={result['box_px']}"
                )

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        for shot in shots:
            row = [r for r in results if r["shot"] == shot]
            cells = " ".join(
                f"{r['name']}={r['expect'][0].upper() if r['pass'] else 'X'}" for r in row
            )
            print(f"shot {shot:2d}: {cells}")
    for message in failed:
        print(f"FAIL {message}")
    print(f"{len(results) - sum(not r['pass'] for r in results)}/{len(results)} regions pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
