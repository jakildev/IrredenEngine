#!/usr/bin/env python3
"""Count trixel-sized self-shadow speckles on voxel face boundaries.

A shadowed pixel is one whose 4-neighbour holds the same albedo at least
``--lit-ratio`` times brighter: the sun term toggled, nothing else. Pixels of
one exact colour form 4-connected components. A component no larger than
``--max-component-px`` whose 8-neighbour ring touches two or more differently
shaded lit levels of that albedo is an edge speckle: it sits where two faces
meet.

An edge speckle is the signature of a receiver point that left its face and was
occluded by the neighbouring voxel. A continuous receiver can also show a true
shadow tip one trixel wide, so a gate's ceiling is the count an independent
shadow oracle confirms as real, not zero.

Structural-metric contract (render-verify ``_run_structural_metric``): prints
a JSON object; exit 0 within thresholds, 1 exceeded, 2 I/O or format error.
"""

import argparse
import sys

import render_metric_util as rmu

NEIGHBOURS_4 = ((1, 0), (-1, 0), (0, 1), (0, -1))


def scalar_brighter(dark, lit, ratio: float) -> bool:
    """True when ``lit`` is ``dark`` scaled by one factor >= ``ratio``."""
    channel = max(range(3), key=lambda c: lit[c])
    if dark[channel] == 0:
        return False
    k = lit[channel] / dark[channel]
    if k < ratio:
        return False
    # 8-bit quantisation of each channel, scaled back through k.
    return all(abs(lit[c] - k * dark[c]) <= 2.5 * k for c in range(3))


def measure(path: str, roi, background, max_component_px: int,
            lit_ratio: float, sample_limit=8) -> dict:
    w, h, bpp, pixels = rmu.read_png(path)
    rx, ry, rw, rh = rmu.resolve_roi(roi, w, h)

    def rgb(x: int, y: int):
        i = (y * w + x) * bpp
        return tuple(pixels[i:i + 3])

    def inside(x: int, y: int) -> bool:
        return rx <= x < rx + rw and ry <= y < ry + rh

    seeds = []
    for y in range(ry, ry + rh):
        for x in range(rx, rx + rw):
            colour = rgb(x, y)
            if colour == background:
                continue
            for dx, dy in NEIGHBOURS_4:
                nx, ny = x + dx, y + dy
                if inside(nx, ny) and scalar_brighter(colour, rgb(nx, ny), lit_ratio):
                    seeds.append((x, y))
                    break

    seen = set()
    speckles = 0
    edge_speckles = 0
    edge_boxes = []
    for seed in seeds:
        if seed in seen:
            continue
        colour = rgb(*seed)
        seen.add(seed)
        stack = [seed]
        component = []
        while stack:
            q = stack.pop()
            component.append(q)
            for dx, dy in NEIGHBOURS_4:
                n = (q[0] + dx, q[1] + dy)
                if n not in seen and inside(*n) and rgb(*n) == colour:
                    seen.add(n)
                    stack.append(n)
        if len(component) > max_component_px:
            continue
        speckles += 1
        members = set(component)
        lit_levels = set()
        for qx, qy in component:
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    n = (qx + dx, qy + dy)
                    if n in members or not inside(*n):
                        continue
                    neighbour = rgb(*n)
                    if neighbour != background and scalar_brighter(colour, neighbour, lit_ratio):
                        lit_levels.add(neighbour)
        if len(lit_levels) >= 2:
            edge_speckles += 1
            xs = [p[0] for p in component]
            ys = [p[1] for p in component]
            edge_boxes.append([min(xs), min(ys), max(xs), max(ys)])
    return {
        "image": path,
        "roi": [rx, ry, rw, rh],
        "shadow_boundary_pixels": len(seeds),
        "speckles": speckles,
        "edge_speckles": edge_speckles,
        "edge_speckle_samples": edge_boxes[:sample_limit],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    rmu.add_common_args(ap)
    ap.add_argument("--max-component-px", type=int, default=4,
                    help="largest component counted as a speckle (default 4: "
                         "one trixel in a 2x HiDPI capture).")
    ap.add_argument("--lit-ratio", type=float, default=1.8,
                    help="minimum lit/shadowed luminance ratio of one albedo.")
    ap.add_argument("--max-edge-speckles", type=int, default=None,
                    help="fail when more face-boundary speckles are found.")
    args = ap.parse_args()
    try:
        result = measure(args.image, args.roi, tuple(args.bg),
                         args.max_component_px, args.lit_ratio)
    except (OSError, ValueError) as error:
        print(f"render-shadow-speckle-metric: {error}", file=sys.stderr)
        return 2
    failed = []
    if args.max_edge_speckles is not None and result["edge_speckles"] > args.max_edge_speckles:
        failed.append(f"edge_speckles {result['edge_speckles']} > {args.max_edge_speckles}")
    return rmu.emit(result, failed)


if __name__ == "__main__":
    sys.exit(main())
