#!/usr/bin/env python3
"""Bound re-rasterized reference changes by an exact-RGB pixel window."""

import argparse
import io
import subprocess
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


def git_image(base_ref: str, path: Path) -> Image.Image:
    result = subprocess.run(
        ["git", "show", f"{base_ref}:{path.as_posix()}"],
        check=True,
        capture_output=True,
    )
    return Image.open(io.BytesIO(result.stdout)).convert("RGB")


def file_image(path: Path) -> Image.Image:
    return Image.open(path).convert("RGB")


def unmatched(source: Image.Image, target: Image.Image, radius: int) -> tuple[int, int]:
    if source.size != target.size:
        raise ValueError(f"image sizes differ: {source.size} != {target.size}")
    width, height = source.size
    border = 2 * radius
    interior = Image.new("L", source.size)
    ImageDraw.Draw(interior).rectangle(
        (border, border, width - border - 1, height - border - 1), fill=255
    )

    def difference_mask(left: Image.Image, right: Image.Image) -> Image.Image:
        difference = ImageChops.difference(left, right)
        mask = ImageChops.lighter(difference.getchannel("R"), difference.getchannel("G"))
        mask = ImageChops.lighter(mask, difference.getchannel("B"))
        return mask.point(lambda value: 255 if value else 0)

    changed_mask = ImageChops.darker(difference_mask(source, target), interior)
    bounds = changed_mask.getbbox()
    if bounds is None:
        return 0, 0
    left, top, right, bottom = bounds
    crop = (
        max(0, left - radius),
        max(0, top - radius),
        min(width, right + radius),
        min(height, bottom + radius),
    )
    source = source.crop(crop)
    target = target.crop(crop)
    changed_mask = changed_mask.crop(crop)
    missing_mask = changed_mask
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            shifted = ImageChops.offset(source, dx, dy)
            missing_mask = ImageChops.darker(missing_mask, difference_mask(shifted, target))
    return changed_mask.histogram()[255], missing_mask.histogram()[255]


def changed_pngs(base_ref: str) -> list[Path]:
    result = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=M", base_ref, "--", "*.png"],
        check=True,
        capture_output=True,
        text=True,
    )
    return [Path(line) for line in result.stdout.splitlines() if line]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", required=True)
    parser.add_argument("--footprint-px", required=True, type=int)
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args()
    if args.footprint_px < 0:
        parser.error("--footprint-px must be non-negative")

    paths = args.paths or changed_pngs(args.base_ref)
    failed = False
    for path in paths:
        before = git_image(args.base_ref, path)
        after = file_image(path)
        forward_changed, forward_unmatched = unmatched(before, after, args.footprint_px)
        reverse_changed, reverse_unmatched = unmatched(after, before, args.footprint_px)
        status = "PASS" if forward_unmatched == reverse_unmatched == 0 else "FAIL"
        failed |= status == "FAIL"
        print(
            f"{path}: footprint={args.footprint_px}px changed={forward_changed} "
            f"head-in-parent-unmatched={forward_unmatched} "
            f"parent-in-head-unmatched={reverse_unmatched} {status}"
        )
    if not paths:
        print("no changed PNG references")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
