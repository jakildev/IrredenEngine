#!/usr/bin/env python3
"""Check the three projected faces of one camera-aligned resampled voxel.

Fixture: IRCanvasStress --only shadowbox --probe-single-voxel --no-spin
--no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 16
--debug-overlay normals. Default scale matches 2560x1440, output scale 2.
Camera yaw changes world normals, not this isolated resampled cell geometry.
This does not measure fidelity to the rotated authored cube or picking/depth.
"""

import argparse
import itertools
import math
from pathlib import Path
from struct import iter_unpack

from PIL import Image, ImageDraw
from render_probe_geometry import convex_hull


def measure(path, yaw, scale):
    with Image.open(path) as opened:
        image = opened.convert("RGB")
    center = (image.width / 2, image.height / 2 - 4 * scale[1])
    angle = math.radians(yaw)
    cosine, sine = math.cos(angle), math.sin(angle)
    normals = ((-cosine, -sine, 0), (sine, -cosine, 0), (0, 0, -1))
    colors = [tuple(round((v + 1) * 127.5) for v in n) for n in normals]
    pixels = list(iter_unpack("BBB", image.tobytes()))
    unexpected = sum(count for count, rgb in image.getcolors(image.width * image.height)
                     if rgb != (0, 0, 0)
                     and not any(all(abs(a - b) <= 1 for a, b in zip(rgb, color))
                                 for color in colors))
    print(f"{path.name}: unexpected_normal_pixels={unexpected}")
    passed = unexpected == 0
    for axis, color in enumerate(colors):
        points = []
        for other in itertools.product((-0.5, 0.5), repeat=2):
            point = list(other)
            point.insert(axis, -0.5)
            x, y, z = point
            points.append((center[0] + (-x + y) * scale[0],
                           center[1] + (-x - y + 2 * z) * scale[1]))
        expected = Image.new("L", image.size)
        ImageDraw.Draw(expected).polygon(convex_hull(points), fill=255)
        predicted = expected.tobytes()
        observed = [all(abs(a - b) <= 1 for a, b in zip(pixel, color))
                    for pixel in pixels]
        intersection = sum(bool(a and b) for a, b in zip(predicted, observed))
        union = sum(bool(a or b) for a, b in zip(predicted, observed))
        area = sum(observed)
        geometric_area = 2 * scale[0] * scale[1]
        iou = intersection / union if union else 0
        ratio = area / geometric_area
        face_passed = iou >= 0.95 and 0.98 <= ratio <= 1.02
        passed = passed and face_passed
        verdict = "PASS" if face_passed else "FAIL"
        print(f"{path.name}: yaw={yaw:g} axis={axis} iou={iou:.3f} "
              f"area_ratio={ratio:.3f} {verdict}")
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", type=float, default=0)
    parser.add_argument("--iso-scale", type=float, nargs=2, default=(64, 32),
                        metavar=("X", "Y"), help="Screenshot pixels per iso unit")
    args = parser.parse_args()
    if min(args.iso_scale) <= 0:
        parser.error("iso scale must be positive")
    return 0 if measure(args.image, args.yaw, args.iso_scale) else 1


if __name__ == "__main__":
    raise SystemExit(main())
