#!/usr/bin/env python3
"""Gate a rotated GRID voxel set's display against its own destination cells.

CanvasStress: --only orbit --focus-orbit 6 [--focus-exact-fit-plate]
--no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1
--zoom 8 --debug-overlay normals --sweep-yaw 0 4.7123889804 4. Default scale is
zoom 8 at 2560x1440 (32 x 16 pixels per iso unit); `--zoom` derives the scale
from the capture width instead (game width 1280, so a 1x host's 1280-wide
capture at zoom 8 is 16 x 8). Black is background.

The expectation is the CPU GRID inverse resample, derived independently of the
engine: the fixture's authored box (centered, round-half-up source cells), an
explicit double-precision rotation matrix, and the destination cells of world
space whose inverse image rounds into a source cell. Every covered cell is
expected; an exposed cell the renderer drops is a defect, not a tolerance. The
camera then views those world cells under its yaw. Each cell's camera-facing
exposed faces are projected as parallelograms in painter order and compared
pixel-for-pixel with the normals overlay, with one framebuffer pixel of
boundary uncertainty. `missing_exposed_cells` counts expected cells whose
interior pixels are mostly absent; `extra_pixels` and `wrong_face_pixels`
catch false geometry and faces owned by the wrong normal.

`--view beauty` grades a lit capture by silhouette and cell occupancy only: any
non-black pixel is occupied, and face colours are not compared.

`--view shadow` reads a `--debug-overlay shadow` capture of the same pose
and grades each sun-facing interior trixel against a ray through the expected
destination occupancy (the revox metric's lattice visibility). A conclusive
shadow row needs both lit and shadowed interiors; otherwise it reports
`occlusion_exercised: false` and exits 1.
"""

import argparse
import importlib.util
import itertools
import json
import math
from pathlib import Path

from render_metric_util import read_png, write_png
from render_revox_lattice import (
    Resample,
    quat_axis_angle,
    quat_inverse,
    rotate,
    round_half_up,
    source_cells,
)

_SPEC = importlib.util.spec_from_file_location(
    "revox_face_metric", Path(__file__).with_name("render-revox-face-metric.py"))
REVOX = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(REVOX)

# CanvasStress orbit 6: a centered GRID set posed at +45 degrees about +Y,
# allocated as the full 12^3 box or as an exact-fit 12 x 1 x 12 plate.
FIXTURES = {
    "cube": dict(extent=(12, 12, 12), carve=False, axis=(0.0, 1.0, 0.0), degrees=45.0),
    "plate": dict(extent=(12, 1, 12), carve=False, axis=(0.0, 1.0, 0.0), degrees=45.0),
}
TIE_EPSILON = 1e-6
# The game resolution's width; a capture is this times the output scale wide,
# and one iso unit spans 2 x zoom game pixels.
GAME_WIDTH = 1280


def rotation_matrix(axis, degrees):
    length = math.sqrt(sum(a * a for a in axis))
    x, y, z = (a / length for a in axis)
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    t = 1.0 - c
    return ((t * x * x + c, t * x * y - s * z, t * x * z + s * y),
            (t * x * y + s * z, t * y * y + c, t * y * z - s * x),
            (t * x * z - s * y, t * y * z + s * x, t * z * z + c))


def world_cells(fixture):
    """Destination world cells: R^T c rounds half up into a source cell."""
    source = source_cells(fixture)[0]
    matrix = rotation_matrix(fixture["axis"], fixture["degrees"])
    reach = max(abs(v) for cell in source for v in cell) + 1
    radius = math.ceil(reach * math.sqrt(3)) + 1
    cells, ties = set(), 0
    for cell in itertools.product(range(-radius, radius + 1), repeat=3):
        local = [sum(matrix[row][col] * cell[row] for row in range(3)) for col in range(3)]
        shifted = [v + 0.5 for v in local]
        ties += any(abs(v - round(v)) < TIE_EPSILON for v in shifted)
        if tuple(math.floor(v) for v in shifted) in source:
            cells.add(cell)
    return cells, ties


class GridLattice(Resample):
    """Expected destination occupancy in the camera view frame.

    World cells are integer, so a cardinal camera yaw maps them onto integer
    view cells; off-cardinal yaws are rejected by the caller.
    """

    def __init__(self, fixture, camera):
        world, self.world_ties = world_cells(fixture)
        inverse = quat_inverse(camera)
        self.cells = {tuple(round_half_up(v) for v in rotate(tuple(map(float, c)), inverse))
                      for c in world}
        self.anchor = (0.0, 0.0, 0.0)
        low = [min(c[i] for c in self.cells) - 1 for i in range(3)]
        high = [max(c[i] for c in self.cells) + 1 for i in range(3)]
        self.window = [range(low[i], high[i] + 1) for i in range(3)]
        self.memo = {}
        self.ties = set()

    def covered(self, cell):
        return cell in self.cells

    def occupied(self):
        return sorted(self.cells)


def expected_image(width, height, fixture, yaw, scale, origin, sun=None):
    camera = quat_axis_angle((0.0, 0.0, 1.0), yaw)
    lattice = GridLattice(fixture, camera)
    owner = [0] * (width * height)
    labels, palette, stats = REVOX.lattice_image(
        width, height, lattice, camera, scale, origin, sun, owner)
    neighbors = ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))
    exposed = sum(1 for cell in lattice.cells
                  if any(not lattice.covered(tuple(cell[i] + n[i] for i in range(3)))
                         for n in neighbors))
    stats.update(expected_cells=stats.pop("occupied_cells"), expected_exposed_cells=exposed,
                 tie_cells=lattice.world_ties)
    return labels, owner, palette, stats


def missing_cells(width, height, bpp, pixels, owner):
    """Expected cells whose interior (one-pixel-band) pixels are mostly absent."""
    interior, absent = {}, {}
    for index, cell in enumerate(owner):
        if not cell:
            continue
        x, y = index % width, index // width
        if any(owner[yy * width + xx] != cell
               for yy in range(max(0, y - 1), min(height, y + 2))
               for xx in range(max(0, x - 1), min(width, x + 2))):
            continue
        interior[cell] = interior.get(cell, 0) + 1
        absent[cell] = absent.get(cell, 0) + (not any(pixels[index * bpp:index * bpp + 3]))
    visible = len(interior)
    missing = sum(1 for cell, count in interior.items() if absent[cell] * 2 > count)
    return visible, missing


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--fixture", choices=sorted(FIXTURES), required=True)
    parser.add_argument("--yaw", type=float, required=True,
                        help="camera yaw in degrees, a multiple of 90")
    scale = parser.add_mutually_exclusive_group()
    scale.add_argument("--iso-scale", type=float, nargs=2, default=(32, 16))
    scale.add_argument("--zoom", type=float,
                       help="camera zoom; derives the iso scale from the capture width")
    parser.add_argument("--view", choices=("normals", "beauty", "shadow"), default="normals",
                        help="normals overlay (faces), lit capture (silhouette and cells), "
                             "or shadow overlay (sun visibility)")
    parser.add_argument("--sun", type=float, nargs=3, default=REVOX.DEFAULT_SUN)
    parser.add_argument("--terminator-tolerance", type=float, default=REVOX.TERMINATOR_TOLERANCE)
    parser.add_argument("--diagnostic-prefix", type=Path)
    args = parser.parse_args(argv)
    if not math.isfinite(args.yaw) or args.yaw % 90 != 0:
        parser.error("yaw must be a multiple of 90 degrees (GRID cells stay on the lattice)")
    if args.zoom is not None and not (math.isfinite(args.zoom) and args.zoom > 0):
        parser.error("zoom must be finite and positive")
    if not all(math.isfinite(v) and v > 0 for v in args.iso_scale):
        parser.error("scale must be finite and positive")
    shadow = args.view == "shadow"
    if not all(math.isfinite(v) for v in args.sun) or not any(args.sun):
        parser.error("sun must be a finite, non-zero direction")
    try:
        width, height, bpp, pixels = read_png(str(args.image))
        scale = args.iso_scale
        if args.zoom is not None:
            unit = args.zoom * 2 * width / GAME_WIDTH
            scale = (unit, unit / 2)
        expected, owner, palette, stats = expected_image(
            width, height, FIXTURES[args.fixture], math.radians(args.yaw), scale,
            (width / 2, height / 2), tuple(args.sun) if shadow else None)
        if shadow:
            lit = stats.pop("lit")
            result, errors = REVOX.compare_shadow(width, height, bpp, pixels, expected, lit,
                                                  stats.pop("trixels"),
                                                  args.terminator_tolerance)
            palette = [(0, 0, 0), (0, 0, 0), REVOX.SHADOW_MAGENTA, (64, 64, 64)]
            expected = lit
        else:
            result, errors = REVOX.compare(width, height, bpp, pixels, expected, palette,
                                           faces=args.view != "beauty")
            visible, missing = missing_cells(width, height, bpp, pixels, owner)
            result.update(visible_expected_cells=visible, missing_exposed_cells=missing)
            result["pass"] &= missing == 0
        result.update(stats)
        result.update(image=str(args.image),
                      scope=("grid_cell_sun" if shadow else
                             "grid_cell_silhouette" if args.view == "beauty" else
                             "grid_cell_faces"),
                      fixture=args.fixture, yaw=args.yaw, iso_scale=list(scale))
        result["pass"] &= not stats["clipped"] and stats["tie_cells"] == 0
        if args.diagnostic_prefix:
            prefix = str(args.diagnostic_prefix)
            expected_rgb = bytes(channel for label in expected for channel in palette[label])
            write_png(prefix + "-expected.png", width, height, expected_rgb, 3)
            write_png(prefix + "-errors.png", width, height, bytes(errors), 3)
        print(json.dumps(result))
        return 0 if result["pass"] else 1
    except (OSError, ValueError) as error:
        parser.exit(2, f"grid-cell metric: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
