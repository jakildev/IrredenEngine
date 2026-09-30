#!/usr/bin/env python3
"""Check camera-facing shading normals in an isolated normals-overlay capture."""

import argparse
import json
import math
import sys
from collections import Counter
from pathlib import Path

import render_metric_util as rmu


def measure(path: Path, yaw: float, output_format: str = "text") -> bool:
    """Treat black as background and require RGB = world normal * .5 + .5."""
    _, _, bpp, pixels = rmu.read_png(str(path))
    colors = Counter(tuple(pixels[i:i + 3]) for i in range(0, len(pixels), bpp))
    colors.pop((0, 0, 0), None)
    angle = math.radians(yaw)
    # Camera yaw rotates the fixed isometric depth axis into world space.
    view_axis = (math.cos(angle) - math.sin(angle),
                 math.sin(angle) + math.cos(angle), 1.0)
    occupied = sum(colors.values())
    back_facing = 0
    invalid = 0
    for rgb, count in colors.items():
        normal = tuple(channel / 127.5 - 1.0 for channel in rgb)
        if abs(sum(value * value for value in normal) - 1.0) > 0.025:
            invalid += count
        # RGB8 rounding perturbs each component by at most 1/255.
        tolerance = sum(abs(value) for value in view_axis) / 255.0 + 1e-6
        if sum(a * b for a, b in zip(normal, view_axis)) > tolerance:
            back_facing += count
    passed = occupied > 0 and invalid == 0 and back_facing == 0
    if output_format == "json":
        result = {
            "image": str(path),
            "yaw": yaw,
            "occupied": occupied,
            "back_facing": back_facing,
            "invalid_normals": invalid,
            "pass": passed,
        }
        if not passed:
            result["reason"] = (
                f"occupied={occupied} back_facing={back_facing} "
                f"invalid_normals={invalid}"
            )
        print(json.dumps(result))
    else:
        verdict = "PASS" if passed else "FAIL"
        print(f"{path.name}: yaw={yaw:g} occupied={occupied} "
              f"back_facing={back_facing} invalid_normals={invalid} {verdict}")
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", type=float, default=0.0,
                        help="Camera Z yaw in degrees; pitch and roll must be zero")
    parser.add_argument("--output-format", choices=("text", "json"), default="text")
    args = parser.parse_args()
    try:
        passed = measure(args.image, args.yaw, args.output_format)
    except (OSError, ValueError) as error:
        if args.output_format == "json":
            print(json.dumps({"error": str(error)}))
        else:
            print(error, file=sys.stderr)
        return 2
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
