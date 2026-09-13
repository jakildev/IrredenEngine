#!/usr/bin/env python3
"""Check presence of all four colors in the attached/detached shadow scene.

Capture IRCanvasStress --only revox,shadowattached,floor --no-spin
--no-auto-rotate --no-ao --subdivisions 1 --zoom 0.65 --auto-screenshot 6
--sweep-yaw 0 1.57079633 5 --voxel-face-shadows. Optional --probe-upright
provides the same geometry without entity tilt. Requires 2560x1440 captures.
This is a presence guard,
not an oracle for shape, shadow correctness, or depth ordering.
"""

import argparse
from pathlib import Path
from struct import iter_unpack

from PIL import Image


def measure(path: Path) -> bool:
    counts = dict.fromkeys(
        ("attached_orange", "detached_cyan", "detached_purple", "rainbow_blue"), 0
    )
    with Image.open(path) as image:
        for red, green, blue in iter_unpack("BBB", image.convert("RGB").tobytes()):
            counts["attached_orange"] += red > 80 and red > green * 1.2 and green > blue * 1.3
            counts["detached_cyan"] += (
                green > red * 1.5 and blue > red * 1.5 and abs(blue - green) < 40
            )
            counts["detached_purple"] += red > 90 and green < red * 0.8 and blue > red * 1.1
            counts["rainbow_blue"] += red < 80 and green < 80 and blue > 90
    passed = all(count >= (200 if name == "rainbow_blue" else 2000)
                 for name, count in counts.items())
    print(f"{path.name}: {counts} {'PASS' if passed else 'FAIL'}")
    return passed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path)
    args = parser.parse_args()
    results = [measure(path) for path in args.images]
    raise SystemExit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
