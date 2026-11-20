#!/usr/bin/env python3
"""Verify that changed shadow pixels only become lighter or darker."""

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageOps


def changed_mask(parent: Image.Image, head: Image.Image) -> Image.Image:
    difference = ImageChops.difference(parent, head)
    red, green, blue = difference.split()
    mask = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    return mask.point(lambda value: 255 if value else 0)


def nonzero_mask(image: Image.Image) -> Image.Image:
    red, green, blue = image.split()
    mask = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    return mask.point(lambda value: 255 if value else 0)


def pixel_count(mask: Image.Image) -> int:
    return mask.histogram()[255]


def erode_once(mask: Image.Image) -> Image.Image:
    width, height = mask.size
    padded = ImageOps.expand(mask, border=1, fill=0)
    eroded = padded.filter(ImageFilter.MinFilter(3))
    return eroded.crop((1, 1, width + 1, height + 1))


def change_depth(mask: Image.Image) -> int:
    depth = 0
    while mask.getbbox() is not None:
        mask = erode_once(mask)
        depth += 1
    return depth


def analyze(parent_path: Path, head_path: Path) -> tuple[int, int, int, int, int]:
    parent = Image.open(parent_path).convert("RGB")
    head = Image.open(head_path).convert("RGB")
    if parent.size != head.size:
        raise ValueError(f"image sizes differ: {parent.size} != {head.size}")

    changed = changed_mask(parent, head)
    changed_count = pixel_count(changed)
    if changed_count == 0:
        return 0, 0, 0, 0, 0

    lighter_mask = nonzero_mask(ImageChops.subtract(head, parent))
    darker_mask = nonzero_mask(ImageChops.subtract(parent, head))
    opposed_mask = ImageChops.darker(lighter_mask, darker_mask)
    opposed_count = pixel_count(opposed_mask)
    lighter_count = pixel_count(lighter_mask) - opposed_count
    darker_count = pixel_count(darker_mask) - opposed_count
    return (
        changed_count,
        lighter_count,
        darker_count,
        opposed_count,
        change_depth(changed),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("parent_root", type=Path)
    parser.add_argument("head_root", type=Path)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="PNG paths relative to both roots; defaults to their shared PNGs",
    )
    args = parser.parse_args()

    paths = args.paths
    if not paths:
        parent_paths = {
            path.relative_to(args.parent_root)
            for path in args.parent_root.rglob("*.png")
        }
        head_paths = {
            path.relative_to(args.head_root)
            for path in args.head_root.rglob("*.png")
        }
        paths = sorted(parent_paths & head_paths)
        if not paths:
            parser.error("the parent and head roots have no shared PNG paths")

    print("| Row | Changed px | Lighter | Darker | Opposed | Depth | Result |")
    print("|---|---:|---:|---:|---:|---:|---|")
    for relative_path in paths:
        try:
            changed, lighter, darker, opposed, depth = analyze(
                args.parent_root / relative_path,
                args.head_root / relative_path,
            )
        except (OSError, ValueError) as error:
            print(f"{relative_path}: ERROR: {error}")
            return 2

        if changed == 0:
            print(f"| `{relative_path}` | 0 | 0 | 0 | 0 | 0 | NOT MOVED |")
            continue

        result = "PASS" if opposed == 0 else "FAIL"
        print(
            f"| `{relative_path}` | {changed} | {lighter} | {darker} | "
            f"{opposed} | {depth} | {result} |"
        )
        if opposed:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
