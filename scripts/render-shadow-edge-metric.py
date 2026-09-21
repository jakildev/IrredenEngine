#!/usr/bin/env python3
"""Graded edge-geometry oracle for SHADOW debug-overlay captures.

``render-shadow-metric.py`` binarizes the same capture and measures *how much*
of the region is shadowed and how badly that coverage is fragmented
(``hole_ratio``, ``components``, ``largest_frac``). It is blind to the shape of
the rim: a shadow can be one solid component and still meet the floor as a
coarse staircase, and no coverage number moves when that staircase is filtered
into a straighter edge. This measures the rim itself.

The discriminator is **local straightness**. Around every rim pixel, the rim's
own pixels within ``--window`` are fitted with a line (principal axis of their
covariance); the RMS distance from that line is the pixel's deviation, in
pixels. A rim that follows a straight line at *any* orientation reads ~0.5 or
less — rasterizing a diagonal cannot deviate further than half a pixel. A rim
quantized into treads and risers reads on the order of its step amplitude: at
the default window, steps of plus/minus 4 px read ~2.1 and plus/minus 1 px read
~0.8. Filtering an edge shrinks its excursions, so the score falls; widening
the shadow does not move it at all.

Metrics (within the ROI, default = whole image):
  * shadow_px      — mask size, for context only.
  * boundary_px    — mask pixels touching *exterior* background (enclosed holes
                     are not rim; see "Blind to" below).
  * boundary_frac  — boundary_px / shadow_px. Coverage-dependent, unlike the
                     rest: reported so a table shows when the mask itself moved.
  * roughness_px   — mean local deviation from a straight rim. **The score.**
  * roughness_p95  — its 95th percentile: one coarse facet in an otherwise
                     clean rim shows up here before it shows up in the mean.
  * speck_frac     — rim pixels whose component is too small to fit a line
                     through (``--min-fit-px``), so they are excluded from the
                     two roughness numbers. Dither speckle lands here rather
                     than inflating the score, which is why it is reported: a
                     high speck_frac means the score describes little of the
                     mask.
  * mean_run_px /
    run_p90        — tread statistics: the mean and 90th-percentile length of
                     the rim's maximal axis-aligned runs.
  * direction_hist — fraction of fitted rim pixels per 22.5-degree orientation
                     bin (bin centres, degrees, 0 = horizontal). Smoothing
                     moves rim off the two axis-aligned facet families into the
                     intermediate bins; ``axis_frac`` is bins 0 and 90 summed.

Blind to:
  * How much is shadowed. Interior growth adds no rim, so the score does not
    move — that invariance is what makes it a softness oracle rather than a
    second coverage oracle, and it is locked by the suite.
  * Enclosed holes and the dither speckle around them. Their rims are excluded
    (holes) or fall under ``--min-fit-px`` (speckle); ``render-shadow-metric.py``
    owns that failure mode.
  * Penumbra itself. The overlay is binarized before this ever sees it, so a
    soft shadow is read through the *geometry* of its fully-shadowed core, not
    as a gradient. A filter that only greys pixels without moving the 0.999
    contour is invisible here.
  * Structure coarser than ``--window``: a window that fits inside one tread
    sees a straight edge. Set it above the expected tread length (the shadow
    map's texel footprint in screen pixels) or the oracle reads clean.
  * Scale. Deviations are in screen pixels, so the same geometry reads larger
    at higher zoom. Compare a row against the same zoom, never across.
  * Whatever the overlay does not classify. A pixel that is neither magenta
    nor black (an entity or overlay colour; the finite-caster captures carry
    none inside their ROIs) reads as background here, so a solid drawn over a
    shadow would contribute rim along its own silhouette.
  * Orientation, for the run and direction numbers only. A straight 45-degree
    rim runs one pixel per step exactly like a shattered one, so run length is
    a diagnostic and has no threshold; roughness is the gated number.

Pure stdlib; mask, ROI and flood-fill primitives come from
``render_metric_util``.

Exit codes: 0 metrics within thresholds (or none given) · 1 a threshold was
exceeded · 2 I/O or format error, or an empty mask.
"""

from __future__ import annotations

import argparse
import math
import sys
from array import array
from bisect import bisect_left, bisect_right
from collections import deque

import render_metric_util as rmu

DEFAULT_WINDOW = 6
# Three collinear pixels fit any line with zero deviation, so a fit needs a
# fourth before its deviation says anything about the rim.
DEFAULT_MIN_FIT_PX = 4
DIRECTION_BIN_DEG = 22.5

# Bit per exposed side of a rim pixel, in the order the run pass walks them.
_NORTH, _SOUTH, _WEST, _EAST = 1, 2, 4, 8


def _component_labels(mask: bytearray, w: int, h: int) -> array:
    """4-connected component id per pixel (0 = background), row-major."""
    labels = array("i", bytes(4 * len(mask)))
    current = 0
    for start in range(len(mask)):
        if not mask[start] or labels[start]:
            continue
        current += 1
        labels[start] = current
        q = deque((start,))
        while q:
            idx = q.popleft()
            x = idx % w
            y = idx // w
            if x > 0 and mask[idx - 1] and not labels[idx - 1]:
                labels[idx - 1] = current
                q.append(idx - 1)
            if x < w - 1 and mask[idx + 1] and not labels[idx + 1]:
                labels[idx + 1] = current
                q.append(idx + 1)
            if y > 0 and mask[idx - w] and not labels[idx - w]:
                labels[idx - w] = current
                q.append(idx - w)
            if y < h - 1 and mask[idx + w] and not labels[idx + w]:
                labels[idx + w] = current
                q.append(idx + w)
    return labels


def _rim(mask: bytearray, holes: bytearray, w: int, h: int) -> tuple[array, int]:
    """Exposed-side bitmask per pixel, plus the rim pixel count.

    A side is exposed when the neighbour on it is background the ROI border can
    reach — an enclosed hole is not rim, and neither is the grid edge, so
    cropping the ROI through a shadow does not fabricate a perfectly straight
    rim where the crop fell.
    """
    dirs = array("B", bytes(len(mask)))
    rim_px = 0
    for idx in range(len(mask)):
        if not mask[idx]:
            continue
        x = idx % w
        y = idx // w
        d = 0
        if y > 0 and not mask[idx - w] and not holes[idx - w]:
            d |= _NORTH
        if y < h - 1 and not mask[idx + w] and not holes[idx + w]:
            d |= _SOUTH
        if x > 0 and not mask[idx - 1] and not holes[idx - 1]:
            d |= _WEST
        if x < w - 1 and not mask[idx + 1] and not holes[idx + 1]:
            d |= _EAST
        if d:
            dirs[idx] = d
            rim_px += 1
    return dirs, rim_px


def _run_lengths(dirs: array, w: int, h: int) -> list[int]:
    """Maximal axis-aligned runs of one exposed side — the rim's treads.

    North/south sides run along rows, west/east along columns. A rim pixel
    exposed on two sides belongs to a run in each, so a lone pixel in open
    background contributes four runs of one.
    """
    runs: list[int] = []
    for side in (_NORTH, _SOUTH):
        for y in range(h):
            row = y * w
            length = 0
            for x in range(w):
                if dirs[row + x] & side:
                    length += 1
                elif length:
                    runs.append(length)
                    length = 0
            if length:
                runs.append(length)
    for side in (_WEST, _EAST):
        for x in range(w):
            length = 0
            for y in range(h):
                if dirs[y * w + x] & side:
                    length += 1
                elif length:
                    runs.append(length)
                    length = 0
            if length:
                runs.append(length)
    return runs


def _percentile(sorted_values: list[float] | array, q: float) -> float:
    """Nearest-rank percentile over an already-sorted sequence."""
    if not len(sorted_values):
        return 0.0
    rank = max(1, math.ceil(q * len(sorted_values)))
    return float(sorted_values[rank - 1])


def edge_metrics(
    path: str,
    roi: tuple[int, int, int, int] | None = None,
    window: int = DEFAULT_WINDOW,
    min_fit_px: int = DEFAULT_MIN_FIT_PX,
) -> dict:
    mask, rw, rh, shadow_px, _lit, rect = rmu.shadow_mask(path, roi)
    if rw * rh > rmu.MAX_FLOOD_PX:
        raise ValueError(
            f"roi {rw}x{rh} exceeds the {rmu.MAX_FLOOD_PX} px flood-fill guard"
        )

    result = {
        "image": path,
        "roi": list(rect),
        "window": window,
        "shadow_px": shadow_px,
        "boundary_px": 0,
        "boundary_frac": 0.0,
        "fitted_px": 0,
        "speck_frac": 0.0,
        "roughness_px": 0.0,
        "roughness_p95": 0.0,
        "runs": 0,
        "mean_run_px": 0.0,
        "run_p90": 0.0,
        "axis_frac": 0.0,
        "direction_hist": {},
    }
    if not shadow_px:
        return result

    holes = rmu.enclosed_holes(mask, rw, rh)
    labels = _component_labels(mask, rw, rh)
    dirs, rim_px = _rim(mask, holes, rw, rh)
    result["boundary_px"] = rim_px
    result["boundary_frac"] = round(rim_px / shadow_px, 4)

    runs = _run_lengths(dirs, rw, rh)
    if runs:
        runs.sort()
        result["runs"] = len(runs)
        result["mean_run_px"] = round(sum(runs) / len(runs), 4)
        result["run_p90"] = _percentile(runs, 0.90)
    if not rim_px:
        return result

    # Rim pixels per row, x-sorted: the line fit walks the window's rows and
    # bisects into each one, so its cost follows the rim, not the ROI area.
    rim_rows: list[array] = [array("i") for _ in range(rh)]
    for idx in range(len(dirs)):
        if dirs[idx]:
            rim_rows[idx // rw].append(idx % rw)

    bins = int(180 / DIRECTION_BIN_DEG)
    hist = [0] * bins
    deviations = array("d")
    radius_sq = window * window
    for y in range(rh):
        row = y * rw
        for x in rim_rows[y]:
            label = labels[row + x]
            n = sx = sy = sxx = syy = sxy = 0
            for ny in range(max(0, y - window), min(rh, y + window + 1)):
                xs = rim_rows[ny]
                dy = ny - y
                span = int(math.sqrt(radius_sq - dy * dy)) if abs(dy) <= window else -1
                if span < 0:
                    continue
                lo = bisect_left(xs, x - span)
                hi = bisect_right(xs, x + span)
                nrow = ny * rw
                for k in range(lo, hi):
                    nx = xs[k]
                    if labels[nrow + nx] != label:
                        continue
                    n += 1
                    sx += nx
                    sy += ny
                    sxx += nx * nx
                    syy += ny * ny
                    sxy += nx * ny
            if n < min_fit_px:
                continue
            cxx = sxx / n - (sx / n) ** 2
            cyy = syy / n - (sy / n) ** 2
            cxy = sxy / n - (sx / n) * (sy / n)
            # Smaller eigenvalue of the 2x2 covariance = variance across the
            # fitted line; its root is the RMS distance from the line.
            root = math.sqrt(max((cxx - cyy) ** 2 + 4.0 * cxy * cxy, 0.0))
            deviations.append(math.sqrt(max((cxx + cyy - root) / 2.0, 0.0)))
            angle = math.degrees(0.5 * math.atan2(2.0 * cxy, cxx - cyy)) % 180.0
            hist[int(((angle + DIRECTION_BIN_DEG / 2) % 180.0) // DIRECTION_BIN_DEG)] += 1

    fitted = len(deviations)
    result["fitted_px"] = fitted
    result["speck_frac"] = round((rim_px - fitted) / rim_px, 4)
    if fitted:
        result["roughness_px"] = round(sum(deviations) / fitted, 4)
        result["roughness_p95"] = round(_percentile(sorted(deviations), 0.95), 4)
        result["direction_hist"] = {
            f"{i * DIRECTION_BIN_DEG:g}": round(hist[i] / fitted, 4)
            for i in range(bins)
        }
        result["axis_frac"] = round((hist[0] + hist[bins // 2]) / fitted, 4)
    return result


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", help="PNG capture (SHADOW debug-overlay mode).")
    ap.add_argument("--roi", type=rmu.parse_roi, default=None,
                    help="x,y,w,h region of interest (default: whole image).")
    ap.add_argument("--window", type=int, default=DEFAULT_WINDOW,
                    help="line-fit radius in pixels. Must exceed the tread "
                         "length you want to see: a window inside one tread "
                         "reads a staircase as a straight edge.")
    ap.add_argument("--min-fit-px", type=int, default=DEFAULT_MIN_FIT_PX,
                    help="rim pixels needed in the window before a line is "
                         "fitted; below it the pixel counts as speckle.")
    ap.add_argument("--max-roughness", type=float, default=None,
                    help="fail if roughness_px exceeds this.")
    ap.add_argument("--max-roughness-p95", type=float, default=None,
                    help="fail if roughness_p95 exceeds this.")
    ap.add_argument("--max-speck-frac", type=float, default=None,
                    help="fail if too much of the rim was too fragmented to "
                         "fit — the guard against a clean roughness number "
                         "that describes a sliver of the mask.")
    ap.add_argument("--min-boundary-px", type=int, default=None,
                    help="fail if the rim is shorter than this. The positive "
                         "control that a shadow edge is present at all: a "
                         "vanished shadow reads roughness 0, which no upper "
                         "bound can catch.")
    args = ap.parse_args(argv)

    if args.window < 1 or args.min_fit_px < 3:
        print("error: --window must be >= 1 and --min-fit-px >= 3", file=sys.stderr)
        return 2
    try:
        result = edge_metrics(args.image, args.roi, args.window, args.min_fit_px)
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if not result["shadow_px"]:
        print("error: no shadow pixels in the ROI — wrong capture, ROI or "
              "render mode (this reads SHADOW debug-overlay captures)",
              file=sys.stderr)
        return 2

    failed: list[str] = []
    if args.max_roughness is not None and result["roughness_px"] > args.max_roughness:
        failed.append(f"roughness_px {result['roughness_px']} > {args.max_roughness}")
    if (args.max_roughness_p95 is not None
            and result["roughness_p95"] > args.max_roughness_p95):
        failed.append(
            f"roughness_p95 {result['roughness_p95']} > {args.max_roughness_p95}")
    if args.max_speck_frac is not None and result["speck_frac"] > args.max_speck_frac:
        failed.append(f"speck_frac {result['speck_frac']} > {args.max_speck_frac}")
    if args.min_boundary_px is not None and result["boundary_px"] < args.min_boundary_px:
        failed.append(f"boundary_px {result['boundary_px']} < {args.min_boundary_px}")
    return rmu.emit(result, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
