#!/usr/bin/env python3
"""Stroke coverage and adjacent halo for the scale-one GUI text fixture.

The fixed ``IRShapeDebug --text-size-sample`` fixture places the shipped LARGE
and SMALL character sheets at parity-aligned origins on the 642x962 retained
GUI backing for a 640x960 logical canvas, displayed into a 1280x960 frame. This
metric derives every exposed stroke and across-edge neighbour sample from those
glyph bitmaps. It reports
the darkest owned stroke, brightest neighbouring halo, and worst stroke/halo
ratio across glyph rows that contain both sample classes.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from array import array
from pathlib import Path

import render_metric_util as util
from trixel_font_util import parse_font_header, triangle_owner_and_across

ROOT = Path(__file__).resolve().parent.parent
LARGE_FONT = ROOT / "engine/render/include/irreden/render/trixel_font.hpp"
SMALL_FONT = ROOT / "engine/render/include/irreden/render/trixel_font_small.hpp"
SHEET = "ABCDEFGHIJKLMNOPQRSTU\nVWXYZ0123456789.,:;!?\n-_/\\()[]=+@#*% |<>~'\""
CANVAS_SIZE = (642, 962)
QUAD_SIZE = (1284, 964)
QUAD_ORIGIN = (-2.0, -1.0)
ORIGINS = {"large": (8, 8), "small": (192, 8)}


def fixture_masks() -> tuple[set[tuple[int, int]], dict[tuple[int, int], tuple]]:
    lit: set[tuple[int, int]] = set()
    groups: dict[tuple[int, int], tuple] = {}
    for set_name, path, prefix in (
        ("large", LARGE_FONT, ""),
        ("small", SMALL_FONT, "Small"),
    ):
        glyphs, char_map, _, _, step_x, step_y = parse_font_header(path, prefix)
        origin_x, origin_y = ORIGINS[set_name]
        for line_index, line in enumerate(SHEET.splitlines()):
            for glyph_index, char in enumerate(line):
                name = char_map.get(char) or char_map.get(char.upper()) or "Question"
                glyph = glyphs[name]
                for row_index, row in enumerate(glyph):
                    for column_index, enabled in enumerate(row):
                        if not enabled:
                            continue
                        texel = (
                            origin_x + glyph_index * step_x + column_index,
                            origin_y + line_index * step_y + row_index,
                        )
                        lit.add(texel)
                        groups[texel] = (set_name, line_index, glyph_index, row_index)
    return lit, groups


def _sample(
    px: int,
    py: int,
    sx: float,
    sy: float,
    origin: tuple[float, float] = QUAD_ORIGIN,
) -> tuple[tuple[int, int], tuple[int, int]]:
    x = (px + 0.5 - origin[0]) / sx
    y = (py + 0.5 - origin[1]) / sy
    owner, across, _ = triangle_owner_and_across(x, y, sx, sy)
    return owner, across


def contrast_metrics(path: str) -> dict:
    width, height, bpp, raw = util.read_png(path)
    if (width, height) != (1280, 960):
        return {
            "image": path,
            "error": f"fixture must be 1280x960, got {width}x{height}",
        }
    pixels = array("B", raw)
    sx, sy = QUAD_SIZE[0] / CANVAS_SIZE[0], QUAD_SIZE[1] / CANVAS_SIZE[1]
    lit, group_for = fixture_masks()
    readings: dict[tuple, dict[str, float]] = {}
    min_x = min(x for x, _ in lit) - 1
    max_x = max(x for x, _ in lit) + 2
    min_y = min(y for _, y in lit) - 2
    max_y = max(y for _, y in lit) + 2
    for py in range(
        math.floor(QUAD_ORIGIN[1] + min_y * sy),
        math.ceil(QUAD_ORIGIN[1] + max_y * sy),
    ):
        for px in range(
            math.floor(QUAD_ORIGIN[0] + min_x * sx),
            math.ceil(QUAD_ORIGIN[0] + max_x * sx),
        ):
            owner, across = _sample(px, py, sx, sy)
            if owner in lit and across not in lit:
                group, kind = group_for[owner], "stroke"
            elif owner not in lit and across in lit:
                group, kind = group_for[across], "halo"
            else:
                continue
            offset = (py * width + px) * bpp
            value = max(pixels[offset : offset + 3]) / 255.0
            group_readings = readings.setdefault(group, {})
            if kind == "stroke":
                group_readings[kind] = min(group_readings.get(kind, 1.0), value)
            else:
                group_readings[kind] = max(group_readings.get(kind, 0.0), value)

    rows = []
    for group, samples in readings.items():
        if "stroke" not in samples or "halo" not in samples:
            continue
        stroke, halo = samples["stroke"], samples["halo"]
        rows.append((group, stroke, halo, stroke / max(halo, 1.0 / 255.0)))
    set_counts = {set_name: sum(group[0] == set_name for group, *_ in rows) for set_name in ORIGINS}
    if not rows or any(count == 0 for count in set_counts.values()):
        return {
            "image": path,
            "row_count": len(rows),
            "set_row_counts": set_counts,
            "error": "capture has no measurable exposed stroke/halo rows for both glyph sets",
        }
    return {
        "image": path,
        "row_count": len(rows),
        "set_row_counts": set_counts,
        "min_stroke_coverage": round(min(row[1] for row in rows), 4),
        "max_halo_coverage": round(max(row[2] for row in rows), 4),
        "min_stroke_to_halo_ratio": round(min(row[3] for row in rows), 4),
    }


def _main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image")
    parser.add_argument("--roi", type=util.parse_roi, default=None)
    parser.add_argument("--min-stroke-coverage", type=float, default=None)
    parser.add_argument("--max-halo-coverage", type=float, default=None)
    parser.add_argument("--min-stroke-to-halo-ratio", type=float, default=None)
    args = parser.parse_args(argv)
    if args.roi is not None:
        print(json.dumps({"error": "the fixed text fixture metric does not accept an ROI"}))
        return 2
    try:
        result = contrast_metrics(args.image)
    except (OSError, ValueError, KeyError, AttributeError) as error:
        print(json.dumps({"error": str(error)}))
        return 2
    if "error" in result:
        return util.emit(result, [result.pop("error")])
    failures = []
    checks = (
        (
            "min_stroke_coverage",
            args.min_stroke_coverage,
            lambda actual, limit: actual < limit,
            "<",
        ),
        ("max_halo_coverage", args.max_halo_coverage, lambda actual, limit: actual > limit, ">"),
        (
            "min_stroke_to_halo_ratio",
            args.min_stroke_to_halo_ratio,
            lambda actual, limit: actual < limit,
            "<",
        ),
    )
    for name, limit, failed, relation in checks:
        if limit is not None and failed(result[name], limit):
            failures.append(f"{name} {result[name]} {relation} {limit}")
    return util.emit(result, failures)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
