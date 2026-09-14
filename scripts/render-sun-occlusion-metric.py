#!/usr/bin/env python3
"""Check direct-light occlusion on the canvas_stress wall/plate fixture.

Capture --only shadowocclusion --pivot-origin --no-spin --no-auto-rotate
--no-ao --subdivisions 1 --zoom 1 --auto-screenshot 6 --sweep-yaw 3.14159265
3.14159265 1. Pass full 2560x1440 PNGs, with the default sun, albedo and
ambient. --unblocked checks captures made with --probe-unblocked.
--staircase instead requires --probe-staircase --probe-grid
--source-face-shadows at zoom 4. The default depth caster still has a
known false shadow at the outside sample.
Its blocked sample is near world (4.25,2.5,-1), inside the overhead
blocker's shadow: the sun ray reaches z=-2.5 at x=3.105,y=0.864.
The camera ray passes beyond the blocker's y=2 edge before that height.

Sample regions avoid silhouette, contact and shadow boundaries. On the
authored floor (z=0), the ray from (0,0,0) toward the sun intersects the
wall at y=-12, x=-8.4, z=-11. The ray from (-12,8,0) crosses that plane
at x=-26, beyond the wall's x extent. Both floor regions have normal -Z.
"""

import argparse
import math
from pathlib import Path

from PIL import Image

SUN = (-0.42, -0.60, -0.55)
ALBEDO = (160, 200, 240)
AMBIENT = 0.30


def measure(path: Path, unblocked: bool, staircase: bool) -> bool:
    with Image.open(path) as source:
        image = source.convert("RGB")
    if image.size != (2560, 1440):
        print(f"{path.name}: FAIL (requires full 2560x1440 capture)")
        return False
    sun_length = math.sqrt(sum(value * value for value in SUN))
    top_factor = AMBIENT + (1 - AMBIENT) * -SUN[2] / sun_length
    # Screen coordinates use yaw pi; scale is 4x2 at zoom 1, 16x8 at zoom 4.
    blocked_pixel = (1308, 758) if staircase else (1280, 718)
    outside_pixel = (1080, 780) if staircase else (1200, 710)
    samples = [
        ("blocked-floor", blocked_pixel, top_factor if unblocked else AMBIENT),
        ("outside-floor", outside_pixel, top_factor),
    ]
    if not unblocked and not staircase:
        samples.append(("wall-away-from-sun", (1330, 655), AMBIENT))
    passed = True
    for label, (x, y), factor in samples:
        expected = tuple(round(value * factor) for value in ALBEDO)
        pixels = [image.getpixel((x + dx, y + dy))
                  for dy in range(-2, 3) for dx in range(-2, 3)]
        error = max(abs(value - target) for pixel in pixels
                    for value, target in zip(pixel, expected))
        ok = error <= 2
        passed &= ok
        print(f"{path.name} {label}: expected={expected} "
              f"center={image.getpixel((x, y))} max_error={error} "
              f"{'PASS' if ok else 'FAIL'}")
    return passed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path, metavar="PNG")
    parser.add_argument("--unblocked", action="store_true")
    parser.add_argument(
        "--staircase", action="store_true",
        help="Use --probe-staircase --probe-grid --source-face-shadows at zoom 4, yaw pi",
    )
    args = parser.parse_args()
    results = [measure(path, args.unblocked, args.staircase) for path in args.images]
    raise SystemExit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
