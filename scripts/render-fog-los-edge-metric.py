#!/usr/bin/env python3
"""Straightness oracle for the fog line-of-sight shadow edges of fog_demo's
--occlusion rows.

The line-of-sight gate hides the ground behind an occluder along the lines
the occluder's edges project to from the source's eye: a wall's flank shadow
ends on the ray through the wall's corner, and a plateau's base strip ends
where the eye's ray clears the plateau's far edge. Both are straight lines in
world space and, on the flat slab, straight lines on screen. This measures
how straight the captured edge is.

For every scanline of a region of interest (rows, or columns with
``--scan cols``) the boundary is the scanline's sharpest step: the boundary
between the two adjacent pixels whose luminance differs most. A shadow edge
is a step of tens of levels between neighbours, while the reveal rim's fade
changes by a fraction of a level per pixel, so a fade crossing the ROI never
wins. The steps are fitted with a least-squares line and the residuals
reported in pixels:

  * ``scanlines``    — steps found (the positive control: an ROI with no
                       edge in it reads 0 and fails ``--min-scanlines``).
  * ``rms_px``       — RMS residual from the fitted line. **The score.** A
                       straight line rasterized on the trixel lattice (4 x 2
                       screen px at the occlusion pose) reads about a pixel
                       or less; a cell-lattice staircase reads on the order
                       of its tread height (a cell is 32 px wide there, so a
                       one-cell stair reads several pixels).
  * ``max_px``       — the largest residual, so one bad tread shows before it
                       moves the RMS.
  * ``slope``        — the fitted line's slope (boundary pixels per scanline),
                       for context.

Scene presets (``--scene``) carry the ROIs of the occlusion pose (zoom 6
snapped to 8, camera at the origin, 2560 x 1440, where a world point
(x, y, z) lands at pixel (1279.5 + 32 (y - x), 720.5 + 16 (2 z - x - y))):
``ground`` scans the two flanks of the ridge's shadow seen from the ground
observer, ``high-ground`` the far edge of the base strip seen from the ridge
top. The ROIs are screen-space; a change to the pose or the scenes moves
them. A manifest structural gate passes ``--roi x,y,w,h`` (render-verify's
convention) with ``--scan`` and the thresholds instead.

    python3 scripts/render-fog-los-edge-metric.py --scene ground \\
        fog_occlusion_ground.png --max-rms 1.0

Pure stdlib; ``read_png`` comes from ``render_metric_util``.

Exit codes: 0 every gate holds · 1 a gate failed · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import math
import sys
from array import array

import render_metric_util as rmu

MIN_CONTRAST = 12.0

# (name, (x0, x1, y0, y1), scan) per preset; scan = "rows" walks each pixel
# row across x, "cols" walks each pixel column down y. The ground flanks are
# the rays from the eye (-6, 0) through the ridge's corners (-0.5, -7.5) and
# (-0.5, 8.5) on the slab: the -Y flank from one unit past the corner out to
# the rim fade, scanned down its columns; the +Y flank scanned along rows
# below the reveal rim, where its lit side is solid. From the ridge top the
# hidden base strip lies under the ridge's own top face on screen, so its far
# edge (x = 6) is measured where it emerges past the ridge's -Y corner, along
# y in -12..-5.5, scanned along rows. Each ROI crosses exactly one edge.
SCENE_ROIS = {
    "ground": [
        ("flank_neg_y", (700, 1010, 930, 1060), "cols"),
        ("flank_pos_y", (1570, 1680, 600, 690), "rows"),
    ],
    "high-ground": [
        ("base_strip_far_edge", (640, 960, 838, 942), "rows"),
    ],
}


def _luminance(px: array, bpp: int, offset: int) -> float:
    return 0.299 * px[offset] + 0.587 * px[offset + 1] + 0.114 * px[offset + 2]


def _crossing(lums: list[float]) -> float | None:
    """The sharpest step along ``lums`` — the boundary between the two
    adjacent samples whose luminance differs most — in fractional sample
    index; None when no adjacent pair differs by MIN_CONTRAST."""
    best = None
    best_jump = MIN_CONTRAST
    for i in range(1, len(lums)):
        jump = abs(lums[i] - lums[i - 1])
        if jump > best_jump:
            best, best_jump = i, jump
    if best is None:
        return None
    return (best - 1) + 0.5


def edge_metrics(
    path: str,
    roi: tuple[int, int, int, int],
    scan: str,
) -> dict:
    w, h, bpp, pix = rmu.read_png(path)
    px = array("B", pix)
    x0, x1, y0, y1 = roi
    if x0 < 0 or y0 < 0 or x1 > w or y1 > h or x1 <= x0 or y1 <= y0:
        raise ValueError(f"roi {roi} out of bounds for {w}x{h} image")
    points: list[tuple[float, float]] = []
    if scan == "rows":
        for y in range(y0, y1):
            lums = [_luminance(px, bpp, (y * w + x) * bpp) for x in range(x0, x1)]
            crossing = _crossing(lums)
            if crossing is not None:
                points.append((float(y), x0 + crossing))
    else:
        for x in range(x0, x1):
            lums = [_luminance(px, bpp, (y * w + x) * bpp) for y in range(y0, y1)]
            crossing = _crossing(lums)
            if crossing is not None:
                points.append((float(x), y0 + crossing))

    result = {
        "image": path,
        "roi": list(roi),
        "scan": scan,
        "scanlines": len(points),
        "slope": 0.0,
        "rms_px": 0.0,
        "max_px": 0.0,
    }
    if len(points) < 2:
        return result
    n = float(len(points))
    mean_t = sum(t for t, _ in points) / n
    mean_v = sum(v for _, v in points) / n
    covariance = sum((t - mean_t) * (v - mean_v) for t, v in points)
    variance = sum((t - mean_t) ** 2 for t, _ in points)
    slope = covariance / variance if variance > 0.0 else 0.0
    intercept = mean_v - slope * mean_t
    residuals = [abs(v - (slope * t + intercept)) for t, v in points]
    result["slope"] = round(slope, 4)
    result["rms_px"] = round(math.sqrt(sum(r * r for r in residuals) / n), 3)
    result["max_px"] = round(max(residuals), 3)
    return result


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", help="PNG capture of a fog_demo --occlusion row.")
    ap.add_argument("--scene", choices=sorted(SCENE_ROIS), default=None,
                    help="preset ROIs for one occlusion scene.")
    ap.add_argument("--roi", type=rmu.parse_roi, default=None,
                    help="x,y,w,h region of interest instead of a preset.")
    ap.add_argument("--scan", choices=("rows", "cols"), default="rows",
                    help="scanline direction for --roi (presets carry their own).")
    ap.add_argument("--max-rms", type=float, default=None,
                    help="fail if any ROI's rms_px exceeds this.")
    ap.add_argument("--max-px", type=float, default=None,
                    help="fail if any ROI's max_px exceeds this.")
    ap.add_argument("--min-scanlines", type=int, default=8,
                    help="fail if an ROI finds fewer crossings than this.")
    args = ap.parse_args(argv)

    rois: list[tuple[str, tuple[int, int, int, int], str]]
    if args.roi is not None:
        rx, ry, rw, rh = args.roi
        rois = [("roi", (rx, rx + rw, ry, ry + rh), args.scan)]
    elif args.scene is not None:
        rois = SCENE_ROIS[args.scene]
    else:
        print("error: pass --scene or --roi", file=sys.stderr)
        return 2

    failed: list[str] = []
    results = {}
    for name, roi, scan in rois:
        try:
            result = edge_metrics(args.image, roi, scan)
        except (OSError, ValueError) as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
        results[name] = result
        if result["scanlines"] < args.min_scanlines:
            failed.append(f"{name}: scanlines {result['scanlines']} < {args.min_scanlines}")
        if args.max_rms is not None and result["rms_px"] > args.max_rms:
            failed.append(f"{name}: rms_px {result['rms_px']} > {args.max_rms}")
        if args.max_px is not None and result["max_px"] > args.max_px:
            failed.append(f"{name}: max_px {result['max_px']} > {args.max_px}")
    return rmu.emit({"image": args.image, "rois": results}, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
