#!/usr/bin/env python3
"""Which way up IRSpriteDemo draws its two-tone fixture sprite, and where.

``assets/demos/sprite_demo/test_anchor.png`` is one frame whose upper half is
``TOP_HALF`` and whose lower half is ``BOTTOM_HALF``. A capture is ``upright``
when both colours are present and every ``TOP_HALF`` row is above every
``BOTTOM_HALF`` row, ``inverted`` when the reverse holds, and ``indeterminate``
otherwise (a colour absent, the rows interleaved, the capture unreadable).
``sprite_box`` is the inclusive ``x0,y0,x1,y1`` box of both colours together.
Rows count from the top of the capture, and colours match exactly.

The demo puts a ``VOXEL``-coloured unit voxel at the sprite's world position,
and the sprite's default anchor is its bottom centre. ``voxel_box`` is the
inclusive box of that colour and ``voxel_margins`` is ``left,right``: the
columns between the sprite box's edges and the voxel box's, negative where the
voxel box reaches past that edge. ``voxel`` is ``aligned`` when the voxel's
first row is the row directly below the sprite box and the two margins are
equal and not negative, ``absent`` when the capture has no ``VOXEL`` pixel,
and ``misaligned`` otherwise. The checks are in capture pixels, so they hold
at any output scale. The sprite keeps its screen size while the voxel grows
with zoom, so a zoom that draws the voxel wider than the sprite reads
``misaligned``.

    fleet-run IRSpriteDemo --auto-screenshot 10
    python3 scripts/render-sprite-orientation-metric.py <capture.png>...

The captures land in ``build/creations/demos/sprite_demo/save_files/screenshots``.
A run appends to that directory, so name the run's own captures: the two with
the highest index.

Exit 0 with ``RESULT=PASS`` only when at least one capture was given and every
one is ``upright`` with its voxel ``aligned``; otherwise exit 1 with
``RESULT=FAIL``.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path
from typing import NamedTuple

import render_metric_util as rmu

TOP_HALF = (255, 128, 0)
BOTTOM_HALF = (0, 255, 255)
VOXEL = (255, 0, 255)

Box = tuple[int, int, int, int]


class Measurement(NamedTuple):
    orientation: str = "indeterminate"
    sprite_box: Box | None = None
    voxel_box: Box | None = None
    voxel_margins: tuple[int, int] | None = None
    voxel: str = "absent"


def color_box(pixels: bytes, width: int, height: int, bpp: int, rgb) -> Box | None:
    """Inclusive ``x0, y0, x1, y1`` box of the pixels that equal ``rgb``."""
    needle = bytes(rgb)
    stride = width * bpp
    box = None
    for y in range(height):
        row = pixels[y * stride:(y + 1) * stride]
        first = row.find(needle)
        while first >= 0 and first % bpp:
            first = row.find(needle, first + 1)
        if first < 0:
            continue
        last = row.rfind(needle)
        while last % bpp:
            last = row.rfind(needle, 0, last + len(needle) - 1)
        x0, x1 = first // bpp, last // bpp
        box = (x0, y, x1, y) if box is None else (
            min(box[0], x0), box[1], max(box[2], x1), y)
    return box


def sprite_orientation(top: Box | None, bottom: Box | None) -> str:
    if top is None or bottom is None:
        return "indeterminate"
    if top[3] < bottom[1]:
        return "upright"
    if bottom[3] < top[1]:
        return "inverted"
    return "indeterminate"


def measure(path: Path) -> Measurement:
    """Orientation and box of the fixture sprite, and the voxel's place under it."""
    width, height, bpp, pixels = rmu.read_png(str(path))
    top = color_box(pixels, width, height, bpp, TOP_HALF)
    bottom = color_box(pixels, width, height, bpp, BOTTOM_HALF)
    voxel_box = color_box(pixels, width, height, bpp, VOXEL)
    present = [box for box in (top, bottom) if box is not None]
    sprite_box = None
    if present:
        sprite_box = (
            min(box[0] for box in present), min(box[1] for box in present),
            max(box[2] for box in present), max(box[3] for box in present))
    voxel_margins = None
    voxel = "absent"
    if voxel_box is not None:
        voxel = "misaligned"
        if sprite_box is not None:
            left, right = voxel_box[0] - sprite_box[0], sprite_box[2] - voxel_box[2]
            voxel_margins = (left, right)
            below = voxel_box[1] == sprite_box[3] + 1
            if below and left == right and left >= 0:
                voxel = "aligned"
    return Measurement(
        sprite_orientation(top, bottom), sprite_box, voxel_box, voxel_margins, voxel)


def field(values: tuple[int, ...] | None) -> str:
    return "none" if values is None else ",".join(map(str, values))


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", nargs="*", type=Path,
                        help="auto-screenshot captures of one IRSpriteDemo run")
    args = parser.parse_args()

    good = 0
    for path in args.captures:
        try:
            measured = measure(path)
        except (OSError, ValueError, struct.error, IndexError, zlib.error) as error:
            print(error, file=sys.stderr)
            measured = Measurement()
        print(
            f"{path}: orientation={measured.orientation}"
            f" sprite_box={field(measured.sprite_box)}"
            f" voxel_box={field(measured.voxel_box)}"
            f" voxel_margins={field(measured.voxel_margins)}"
            f" voxel={measured.voxel}")
        good += measured.orientation == "upright" and measured.voxel == "aligned"

    if not args.captures:
        print("no captures given", file=sys.stderr)
    passed = bool(args.captures) and good == len(args.captures)
    print("RESULT=PASS" if passed else "RESULT=FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
