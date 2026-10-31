#!/usr/bin/env python3
"""Locality census of an old/new frame pair from the canvas_stress compare pass.

Lists every changed pixel, classifies its old and new colour against the three
label colours, and groups the changed pixels into 8-connected cells with their
bounding-box size. Stdlib only; PNG decoding reuses scripts/render-compare.py.

Usage: census.py <old.png> <new.png> [<old.png> <new.png> ...]
"""
from __future__ import annotations

import importlib.util
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
_spec = importlib.util.spec_from_file_location(
    "render_compare", REPO / "scripts" / "render-compare.py")
render_compare = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(render_compare)

LABEL_COLORS = {
    (235, 130, 90): "orange",
    (110, 200, 240): "blue",
    (180, 235, 110): "green",
}


def pixels(path: str) -> tuple[int, int, list[tuple[int, int, int]]]:
    width, height, bpp, data = render_compare.read_png(path)
    rgb = [tuple(data[i:i + 3]) for i in range(0, len(data), bpp)]
    return width, height, rgb


def colour_class(rgb: tuple[int, int, int]) -> str:
    if rgb in LABEL_COLORS:
        return LABEL_COLORS[rgb]
    if rgb == (0, 0, 0):
        return "black"
    return "other"


def census(old_path: str, new_path: str) -> None:
    width, height, old = pixels(old_path)
    new_width, new_height, new = pixels(new_path)
    if (width, height) != (new_width, new_height):
        raise SystemExit(f"size mismatch: {width}x{height} vs {new_width}x{new_height}")
    changed = {i for i, (a, b) in enumerate(zip(old, new)) if a != b}
    old_classes = Counter(colour_class(old[i]) for i in changed)
    new_classes = Counter(colour_class(new[i]) for i in changed)

    cells = []
    unvisited = set(changed)
    while unvisited:
        stack = [unvisited.pop()]
        members = []
        while stack:
            index = stack.pop()
            members.append(index)
            x, y = index % width, index // width
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    neighbour = ny * width + nx
                    if 0 <= nx < width and 0 <= ny < height and neighbour in unvisited:
                        unvisited.remove(neighbour)
                        stack.append(neighbour)
        xs = [i % width for i in members]
        ys = [i // width for i in members]
        cells.append((min(ys), min(xs), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1,
                      len(members), Counter(colour_class(old[i]) for i in members)))
    cells.sort()

    print(f"{Path(old_path).name} -> {Path(new_path).name}  ({width}x{height})")
    print(f"  changed px: {len(changed)}")
    print(f"  old colour: {dict(sorted(old_classes.items()))}")
    print(f"  new colour: {dict(sorted(new_classes.items()))}")
    print(f"  cells: {len(cells)}  by old colour: "
          f"{dict(sorted(Counter(c[5].most_common(1)[0][0] for c in cells).items()))}")
    for top, left, w, h, count, colours in cells:
        print(f"    origin ({left},{top}) {w}x{h} px={count} old={dict(colours)}")


def main(argv: list[str]) -> int:
    if len(argv) < 2 or len(argv) % 2:
        print(__doc__, file=sys.stderr)
        return 2
    for i in range(0, len(argv), 2):
        census(argv[i], argv[i + 1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
