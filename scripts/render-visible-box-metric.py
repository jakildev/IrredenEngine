#!/usr/bin/env python3
"""Compare the visible shadowbox silhouette to its authored box projection.

Use IRCanvasStress --only shadowbox,floor --no-spin --no-auto-rotate --no-ao
--subdivisions 1 --zoom 2 --auto-screenshot 6 --sweep-yaw 0 0 1.
Supply the actual camera yaw in degrees. Keep the complete receiver plate visible.
For the magnified single-voxel fixture, add --probe-single-voxel to the demo,
use --zoom 16 --no-shadows, and pass --single-voxel here. Both recipes require base
subdivisions 1 and the full subdivision mode.
This checks silhouette coverage, not face shading, picking or temporal stability.
"""

import argparse
import itertools
import math
from pathlib import Path
from struct import iter_unpack

from PIL import Image, ImageDraw
from render_probe_geometry import (
    BOX_HALF_CENTER_SPAN,
    BOX_Z,
    FLOOR_HALF_SPAN,
    FLOOR_TOP,
    convex_hull,
    plate_bounds,
)


def project(point, yaw):
    x, y, z = point
    cosine, sine = math.cos(yaw), math.sin(yaw)
    view_x, view_y = cosine * x + sine * y, -sine * x + cosine * y
    return -view_x + view_y, -view_x - view_y + 2 * z


def expected_polygon(image, yaw, single_voxel=False):
    left, top, right, bottom = plate_bounds(image)
    if left == 0 or top == 0 or right == image.width or bottom == image.height:
        raise ValueError("receiver plate is clipped; reduce zoom")
    center = ((left + right - 1) / 2, (top + bottom - 1) / 2)
    # The smooth SDF box spans authored size minus one plus one microcell;
    # the magnified fixture has size 12x12x4 and effective subdivisions 16.
    floor_half_span = (12 - 1 + 1 / 16) / 2 if single_voxel else FLOOR_HALF_SPAN
    floor_top = 4 - (4 - 1 + 1 / 16) / 2 if single_voxel else FLOOR_TOP
    half_center_span = (0, 0, 0) if single_voxel else BOX_HALF_CENTER_SPAN
    box_z = -2 if single_voxel else BOX_Z
    floor = [project((x, y, 0), yaw) for x, y in
             itertools.product((-floor_half_span, floor_half_span), repeat=2)]
    scale = ((right - left) / (max(x for x, _ in floor) - min(x for x, _ in floor)),
             (bottom - top) / (max(y for _, y in floor) - min(y for _, y in floor)))
    points = []
    for x, y, z in itertools.product(*[(-h - 0.5, h + 0.5) for h in half_center_span]):
        px, py = project((x, y, z + box_z - floor_top), yaw)
        points.append((center[0] + px * scale[0], center[1] + py * scale[1]))
    return convex_hull(points)


def measure(path: Path, yaw: float, overlay: Path | None, placement_only=False,
            single_voxel=False) -> bool:
    with Image.open(path) as opened:
        image = opened.convert("RGB")
    polygon = expected_polygon(image, math.radians(yaw), single_voxel)
    expected = Image.new("L", image.size)
    ImageDraw.Draw(expected).polygon(polygon, fill=255)
    observed = bytes(255 if blue > 50 and blue > red * 1.25 and blue > green * 1.15 else 0
                     for red, green, blue in iter_unpack("BBB", image.tobytes()))
    intersection = union = predicted_area = observed_area = 0
    predicted_x = predicted_y = observed_x = observed_y = 0
    for index, (predicted, actual) in enumerate(zip(expected.tobytes(), observed)):
        x, y = index % image.width, index // image.width
        if predicted:
            predicted_x += x
            predicted_y += y
        if actual:
            observed_x += x
            observed_y += y
        intersection += bool(predicted and actual)
        union += bool(predicted or actual)
        predicted_area += bool(predicted)
        observed_area += bool(actual)
    if predicted_area == 0 or observed_area == 0:
        raise ValueError("expected or observed box is absent")
    iou = intersection / union
    area_ratio = observed_area / predicted_area
    dx = observed_x / observed_area - predicted_x / predicted_area
    dy = observed_y / observed_area - predicted_y / predicted_area
    placement_passed = abs(dx) <= 2 and abs(dy) <= 2
    passed = placement_passed and (placement_only or (iou >= 0.95 and 0.95 <= area_ratio <= 1.05))
    mode = "placement" if placement_only else "silhouette"
    print(f"{path.name}: yaw={yaw:g} iou={iou:.3f} area_ratio={area_ratio:.3f} "
          f"centroid_delta=({dx:.2f},{dy:.2f}) mode={mode} "
          f"{'PASS' if passed else 'FAIL'}")
    if overlay is not None:
        overlay.parent.mkdir(parents=True, exist_ok=True)
        ImageDraw.Draw(image).line(polygon + [polygon[0]], fill="red", width=1)
        image.save(overlay)
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--yaw", type=float, default=0, help="Camera yaw in degrees")
    parser.add_argument("--overlay", type=Path)
    parser.add_argument("--placement-only", action="store_true",
                        help="Check centroid within two pixels only; does not validate shape")
    parser.add_argument("--single-voxel", action="store_true",
                        help="Match --probe-single-voxel: zoom 16, base subdivisions 1, full mode")
    args = parser.parse_args()
    try:
        passed = measure(args.image, args.yaw, args.overlay, args.placement_only,
                         args.single_voxel)
    except (OSError, ValueError) as error:
        parser.exit(2, f"{error}\n")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
