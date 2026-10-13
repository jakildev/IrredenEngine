#!/usr/bin/env python3
"""Which way up IRSpriteDemo draws its two-tone fixture sprite.

``assets/demos/sprite_demo/test_anchor.png`` is one frame whose upper half is
``TOP_HALF`` and whose lower half is ``BOTTOM_HALF``. A capture is ``upright``
when both colours are present and every ``TOP_HALF`` row is above every
``BOTTOM_HALF`` row, ``inverted`` when the reverse holds, and ``indeterminate``
otherwise (a colour absent, the rows interleaved, the capture unreadable).
``sprite_box`` is the inclusive ``x0,y0,x1,y1`` box of both colours together.
Rows count from the top of the capture, and colours match exactly.

    fleet-run IRSpriteDemo --auto-screenshot 10
    python3 scripts/render-sprite-orientation-metric.py <capture.png>...

The captures land in ``build/creations/demos/sprite_demo/save_files/screenshots``.
A run appends to that directory, so name the run's own captures: the two with
the highest index.

Exit 0 with ``RESULT=PASS`` only when at least one capture was given and every
one is ``upright``; otherwise exit 1 with ``RESULT=FAIL``.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

import render_metric_util as rmu

TOP_HALF = (255, 128, 0)
BOTTOM_HALF = (0, 255, 255)

Box = tuple[int, int, int, int]


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


def measure(path: Path) -> tuple[str, Box | None]:
    """Orientation and both-colour box of the fixture sprite in one capture."""
    width, height, bpp, pixels = rmu.read_png(str(path))
    top = color_box(pixels, width, height, bpp, TOP_HALF)
    bottom = color_box(pixels, width, height, bpp, BOTTOM_HALF)
    present = [box for box in (top, bottom) if box is not None]
    if not present:
        return "indeterminate", None
    sprite_box = (
        min(box[0] for box in present), min(box[1] for box in present),
        max(box[2] for box in present), max(box[3] for box in present))
    if top is None or bottom is None:
        return "indeterminate", sprite_box
    if top[3] < bottom[1]:
        return "upright", sprite_box
    if bottom[3] < top[1]:
        return "inverted", sprite_box
    return "indeterminate", sprite_box


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", nargs="*", type=Path,
                        help="auto-screenshot captures of one IRSpriteDemo run")
    args = parser.parse_args()

    upright = 0
    for path in args.captures:
        try:
            orientation, sprite_box = measure(path)
        except (OSError, ValueError, struct.error, IndexError, zlib.error) as error:
            print(error, file=sys.stderr)
            orientation, sprite_box = "indeterminate", None
        box = "none" if sprite_box is None else ",".join(map(str, sprite_box))
        print(f"{path}: orientation={orientation} sprite_box={box}")
        upright += orientation == "upright"

    if not args.captures:
        print("no captures given", file=sys.stderr)
    passed = bool(args.captures) and upright == len(args.captures)
    print("RESULT=PASS" if passed else "RESULT=FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
