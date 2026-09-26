#!/usr/bin/env python3
"""Line-of-sight fog boundary metric for fog_demo's --occlusion rows.

Reads a smooth-gate capture against its hard-gate control (the same scene
without ``--los-softness``) and reports how far the smooth line-of-sight gate
moves each boundary off the cell lattice:

  * **Flank ROIs** (``--scene ground``): the two shadow flanks running from the
    ground observer past the ridge ends, scanned across in pixel rows or
    columns. Per scanline: the lit and fogged levels are its 90th / 10th
    luminance percentiles, the boundary is its first 50 % crossing, and the
    step is the largest adjacent-pixel luminance difference within one cell
    (32 px) of that crossing.
      - stair: the max residual of the crossings from their least-squares
        line, in pixels.
      - step: the max step over the ROI's scanlines.
    Gates: the control fires (median hard step >= 0.5 x its scanline contrast),
    smooth stair <= 0.75 x hard, smooth step <= 0.35 x hard.
  * **Base-strip ROI** (``--scene high-ground``): the -X edge of the strip the
    ridge hides at its own base from an observer on its top, scanned the same
    way along world lines on the slab top (y -6..7, clear of |y| < 2 beside the
    observer's column) against a quadratic. Gate: smooth step <= 0.5 x hard; the stair is
    reported, not gated (the horizon field itself is rough along the strip).
  * **Cliff-face ROI** (``--scene high-ground``): the ridge's camera-facing
    -X face, one luminance sample per voxel (a 12 x 12 px patch at its centre).
    A voxel is isolated when it differs from both neighbours along its row by
    more than 16 while they agree within 4 — the tooth comb the hard gate
    leaves (y = 0, beside the observer's own column, is lit on both gates and
    skipped). Gate: count > 0 on hard, 0 on smooth.
  * **Facing surface** (``--scene blocker``): the SDF box's -X face, mean
    luminance over the face's pixels as the inert capture shows them. Gate:
    smooth >= 0.98 x the inert control (``--inert``); the hard row is reported
    (its occluded ends read black) as the control that fires.

The capture pose is fog_demo's occlusion pose (zoom 6 snapped to 8, camera at
the origin, 2560 x 1440): a world point (x, y, z) lands at pixel
(1279.5 + 32 (y - x), 720.5 + 16 (2 z - x - y)). The ROIs are world-space;
a change to the pose or the scenes moves them.

    python3 scripts/render-fog-los-boundary-metric.py --scene ground \\
        fog_occlusion_smooth.png --hard fog_occlusion_ground.png

Pure stdlib; reuses ``read_png()`` via ``render_metric_util``.

Exit codes: 0 every gate holds · 1 a gate failed · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import statistics
import sys
from array import array

import render_metric_util as rmu

PIXELS_PER_CELL = 32
ORIGIN = (1279.5, 720.5)
GROUND_TOP = 3.5  # the slab's top plane (top voxel centre z = 4)
# Pixel ROIs (x0, x1, y0, y1) of the ground scene's two flanks: the -y flank
# below-left of the ridge, and the stretch of the +y flank right of the ridge's
# far end (the ridge hides the rest of it). Both run through the rim halo.
FLANK_NEG_Y_ROI = (700, 1010, 930, 1060)
FLANK_POS_Y_ROI = (1575, 1700, 560, 715)
MIN_CONTRAST = 12.0

FLANK_STAIR_GATE = 0.75
FLANK_STEP_GATE = 0.35
FLANK_CONTROL_STEP = 0.5
BASE_STRIP_STEP_GATE = 0.5
TOOTH_DIFF = 16
TOOTH_AGREE = 4
FACING_GATE = 0.98
# The face voxel at y = 0 looks into the column beside the observer's own, which
# nothing hides; it is lit on both gates and is not a tooth.
EYE_ROW_INDEX = 6


class Capture:
    def __init__(self, path: str) -> None:
        self.path = path
        self.width, self.height, self.bpp, pixels = rmu.read_png(path)
        self.pixels = array("B", pixels)

    def luminance(self, px: int, py: int) -> float:
        px = min(max(px, 0), self.width - 1)
        py = min(max(py, 0), self.height - 1)
        at = (py * self.width + px) * self.bpp
        p = self.pixels
        return 0.299 * p[at] + 0.587 * p[at + 1] + 0.114 * p[at + 2]

    def at_world(self, x: float, y: float, z: float) -> float:
        sx = ORIGIN[0] + PIXELS_PER_CELL * (y - x)
        sy = ORIGIN[1] + PIXELS_PER_CELL / 2 * (2 * z - x - y)
        return self.luminance(int(sx), int(sy))


def _fit_residual(points: list[tuple[float, float]], degree: int) -> float:
    """Max |residual| of a least-squares polynomial fit of degree 1 or 2."""
    n = len(points)
    size = degree + 1
    # Normal equations, solved by Gaussian elimination.
    sums = [sum(t ** k for t, _ in points) for k in range(2 * degree + 1)]
    rhs = [sum(v * t ** k for t, v in points) for k in range(size)]
    m = [[sums[r + c] for c in range(size)] + [rhs[r]] for r in range(size)]
    for col in range(size):
        pivot = max(range(col, size), key=lambda r: abs(m[r][col]))
        m[col], m[pivot] = m[pivot], m[col]
        for r in range(size):
            if r != col and m[col][col] != 0.0:
                f = m[r][col] / m[col][col]
                m[r] = [a - f * b for a, b in zip(m[r], m[col])]
    coef = [m[r][size] / m[r][r] for r in range(size)]
    if n == 0:
        return 0.0
    return max(abs(v - sum(c * t ** k for k, c in enumerate(coef))) for t, v in points)


def _scan_boundary(capture: Capture, lines: list[tuple[float, list[tuple[int, int]]]]) -> dict:
    """Crossings (in pixels) and steps for scanlines of (parameter, pixels dark->lit)."""
    crossings: list[tuple[float, float]] = []
    steps: list[float] = []
    ratios: list[float] = []
    for t, pixels in lines:
        values = [capture.luminance(px, py) for px, py in pixels]
        ordered = sorted(values)
        fogged = ordered[len(ordered) // 10]
        lit = ordered[(9 * len(ordered)) // 10]
        contrast = lit - fogged
        if contrast < MIN_CONTRAST:
            continue
        half = (fogged + lit) / 2
        cross = next((i for i in range(1, len(values)) if values[i] >= half > values[i - 1]), None)
        if cross is None:
            continue
        frac = (half - values[cross - 1]) / (values[cross] - values[cross - 1])
        crossings.append((t, cross - 1 + frac))
        lo = max(cross - PIXELS_PER_CELL, 1)
        hi = min(cross + PIXELS_PER_CELL, len(values) - 1)
        step = max(abs(values[i] - values[i - 1]) for i in range(lo, hi + 1))
        steps.append(step)
        ratios.append(step / contrast)
    return {
        "scanlines": len(crossings),
        "crossings": crossings,
        "step": max(steps) if steps else 0.0,
        "step_over_contrast_median": statistics.median(ratios) if ratios else 0.0,
    }


def _flank_lines(side: int) -> list[tuple[float, list[tuple[int, int]]]]:
    """Pixel scanlines across one ground flank of the ground scene.

    side -1: the -y flank (lower left of the ridge), one column per scanline,
    fogged above, lit below. side +1: the +y flank's visible stretch right of
    the ridge's end, one row per scanline, fogged left, lit right."""
    if side < 0:
        x0, x1, y0, y1 = FLANK_NEG_Y_ROI
        return [(float(x), [(x, y) for y in range(y0, y1)]) for x in range(x0, x1)]
    x0, x1, y0, y1 = FLANK_POS_Y_ROI
    return [(float(y), [(x, y) for x in range(x0, x1)]) for y in range(y0, y1)]


def _world_line(samples: list[tuple[float, float, float]]) -> list[tuple[int, int]]:
    return [
        (
            int(ORIGIN[0] + PIXELS_PER_CELL * (y - x)),
            int(ORIGIN[1] + PIXELS_PER_CELL / 2 * (2 * z - x - y)),
        )
        for x, y, z in samples
    ]


def _base_strip_lines() -> list[tuple[float, list[tuple[int, int]]]]:
    """Scanlines across the -X base strip, from the ridge foot outward."""
    lines = []
    y = -6.0
    while y <= 7.0:
        if abs(y) >= 2.0:
            samples = [
                (-0.75 - k / PIXELS_PER_CELL, y, GROUND_TOP)
                for k in range(5 * PIXELS_PER_CELL)
            ]
            lines.append((y, _world_line(samples)))
        y += 1.0 / 16
    return lines


def _boundary_report(smooth: Capture, hard: Capture, lines, degree: int) -> dict:
    report = {}
    for name, capture in (("hard", hard), ("smooth", smooth)):
        scan = _scan_boundary(capture, lines)
        report[name] = {
            "scanlines": scan["scanlines"],
            "stair_px": round(_fit_residual(scan["crossings"], degree), 3),
            "step": round(scan["step"], 2),
            "step_over_contrast_median": round(scan["step_over_contrast_median"], 3),
        }
    h, s = report["hard"], report["smooth"]
    report["stair_ratio"] = round(s["stair_px"] / h["stair_px"], 3) if h["stair_px"] else None
    report["step_ratio"] = round(s["step"] / h["step"], 3) if h["step"] else None
    return report


def _isolated_face_voxels(capture: Capture) -> int:
    """Isolated voxels on the ridge's -X face (voxels x 0, y -6..8, z 0..3)."""
    isolated = 0
    for z in range(4):
        row = []
        for y in range(-6, 9):
            cx = 1295.5 + PIXELS_PER_CELL * y
            cy = 728.5 - 16 * y + PIXELS_PER_CELL * z
            patch = [
                capture.luminance(int(cx) + dx, int(cy) + dy)
                for dx in range(-6, 6)
                for dy in range(-6, 6)
            ]
            row.append(statistics.median(patch))
        for i in range(1, len(row) - 1):
            if i == EYE_ROW_INDEX:
                continue
            if (
                abs(row[i] - row[i - 1]) > TOOTH_DIFF
                and abs(row[i] - row[i + 1]) > TOOTH_DIFF
                and abs(row[i - 1] - row[i + 1]) <= TOOTH_AGREE
            ):
                isolated += 1
    return isolated


def _facing_mask(inert: Capture) -> list[tuple[int, int]]:
    """The SDF box's -X face pixels in the inert capture: purple (red and blue
    over green by 20) below the lit top face's brightness, inside the face's
    screen columns (y -7.5..8.5 on the x = -0.5 plane) clear of the -Y end."""
    mask = []
    first = int(ORIGIN[0] + PIXELS_PER_CELL * -7.0) + 16
    last = int(ORIGIN[0] + PIXELS_PER_CELL * 9.0) - 2
    for sx in range(first, last):
        for sy in range(0, inert.height):
            at = (sy * inert.width + sx) * inert.bpp
            r, g, b = inert.pixels[at], inert.pixels[at + 1], inert.pixels[at + 2]
            if r > g + 20 and b > g + 20 and inert.luminance(sx, sy) <= 120:
                mask.append((sx, sy))
    return mask


def _mask_mean(capture: Capture, mask: list[tuple[int, int]]) -> float:
    return sum(capture.luminance(sx, sy) for sx, sy in mask) / len(mask) if mask else 0.0


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--scene", required=True, choices=("ground", "high-ground", "blocker"))
    ap.add_argument("smooth", help="the --los-softness capture")
    ap.add_argument("--hard", required=True, help="the same scene on the hard gate")
    ap.add_argument("--inert", help="--scene blocker: the blocker-inert capture")
    args = ap.parse_args(argv)
    try:
        smooth = Capture(args.smooth)
        hard = Capture(args.hard)
        inert = Capture(args.inert) if args.inert else None
    except (OSError, ValueError) as exc:
        print(f"render-fog-los-boundary-metric: {exc}", file=sys.stderr)
        return 2

    failed: list[str] = []
    result: dict = {"scene": args.scene, "smooth": args.smooth, "hard": args.hard}
    if args.scene == "ground":
        for name, side in (("flank_pos_y", 1), ("flank_neg_y", -1)):
            report = _boundary_report(smooth, hard, _flank_lines(side), 1)
            result[name] = report
            if report["hard"]["step_over_contrast_median"] < FLANK_CONTROL_STEP:
                failed.append(f"{name}: hard control step does not fire")
            if report["stair_ratio"] is None or report["stair_ratio"] > FLANK_STAIR_GATE:
                failed.append(f"{name}: stair ratio {report['stair_ratio']} > {FLANK_STAIR_GATE}")
            if report["step_ratio"] is None or report["step_ratio"] > FLANK_STEP_GATE:
                failed.append(f"{name}: step ratio {report['step_ratio']} > {FLANK_STEP_GATE}")
    elif args.scene == "high-ground":
        report = _boundary_report(smooth, hard, _base_strip_lines(), 2)
        result["base_strip_neg_x"] = report
        if report["step_ratio"] is None or report["step_ratio"] > BASE_STRIP_STEP_GATE:
            failed.append(f"base strip: step ratio {report['step_ratio']} > {BASE_STRIP_STEP_GATE}")
        teeth = {"hard": _isolated_face_voxels(hard), "smooth": _isolated_face_voxels(smooth)}
        result["cliff_face_isolated_voxels"] = teeth
        if teeth["hard"] == 0:
            failed.append("cliff face: hard control shows no teeth")
        if teeth["smooth"] != 0:
            failed.append(f"cliff face: {teeth['smooth']} isolated voxels on smooth")
    else:
        if inert is None:
            print("render-fog-los-boundary-metric: --scene blocker needs --inert", file=sys.stderr)
            return 2
        mask = _facing_mask(inert)
        means = {
            "inert": round(_mask_mean(inert, mask), 3),
            "hard": round(_mask_mean(hard, mask), 3),
            "smooth": round(_mask_mean(smooth, mask), 3),
        }
        result["facing_mask_px"] = len(mask)
        result["facing_mean_luminance"] = means
        result["smooth_over_inert"] = round(means["smooth"] / means["inert"], 4)
        result["hard_over_inert"] = round(means["hard"] / means["inert"], 4)
        if result["hard_over_inert"] >= FACING_GATE:
            failed.append("facing surface: hard control does not fire")
        if result["smooth_over_inert"] < FACING_GATE:
            failed.append(f"facing surface: smooth {result['smooth_over_inert']} < {FACING_GATE}")
    return rmu.emit(result, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
