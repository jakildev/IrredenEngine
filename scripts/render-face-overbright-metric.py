#!/usr/bin/env python3
"""Count over-bright pixels within a face-colour hue class."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import render_metric_util as rmu

DEFAULT_GREEN_RATIO_TOLERANCE = 0.15
DEFAULT_BLUE_RATIO_TOLERANCE = 0.2


def measure(
    path: Path,
    ceiling: tuple[int, int, int],
    green_ratio_tolerance: float = DEFAULT_GREEN_RATIO_TOLERANCE,
    blue_ratio_tolerance: float = DEFAULT_BLUE_RATIO_TOLERANCE,
) -> tuple[int, int]:
    """Return hue-class and over-bright pixel counts for one capture."""
    _, _, bytes_per_pixel, pixels = rmu.read_png(str(path))
    ceiling_red, ceiling_green, ceiling_blue = ceiling
    green_ratio = ceiling_green / ceiling_red
    blue_ratio = ceiling_blue / ceiling_red
    ceiling_luma = (
        0.2126 * ceiling_red + 0.7152 * ceiling_green + 0.0722 * ceiling_blue
    )
    hue_pixels = 0
    overbright_pixels = 0
    for offset in range(0, len(pixels), bytes_per_pixel):
        red, green, blue = pixels[offset:offset + 3]
        if red <= 20:
            continue
        if abs(green / red - green_ratio) > green_ratio_tolerance:
            continue
        if abs(blue / red - blue_ratio) > blue_ratio_tolerance:
            continue
        hue_pixels += 1
        luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue
        if luma > ceiling_luma:
            overbright_pixels += 1
    return hue_pixels, overbright_pixels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path)
    parser.add_argument("--ceiling", required=True, type=rmu.parse_rgb)
    args = parser.parse_args()

    failed = False
    invalid = False
    try:
        for path in args.images:
            hue_pixels, overbright_pixels = measure(path, args.ceiling)
            print(f"{path}: hue_px={hue_pixels} overbright_px={overbright_pixels}")
            failed = failed or overbright_pixels > 0
            invalid = invalid or hue_pixels == 0
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        print("RESULT=FAIL")
        return 2

    print("RESULT=FAIL" if failed or invalid else "RESULT=PASS")
    if invalid:
        return 2
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
