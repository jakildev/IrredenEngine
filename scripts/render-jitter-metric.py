#!/usr/bin/env python3
"""Temporal-jitter metric for the rotated-render validation suite (epic #1881).

The structural sibling metrics (``render-silhouette-metric.py`` /
``render-coverage-metric.py``, epic #1766) each score a *single* capture. This
one is **temporal**: it scores a *sequence* of frames captured across a fine
continuous camera Z-yaw sweep (consecutive frames ≈ a sub-degree yaw step) and
quantifies how much the rendered surface *shimmers* frame-to-frame.

The artifact it targets (epic #1881): under camera Z-yaw the rendered surface
shimmers instead of moving smoothly. Two crawl modes appear — per-axis trixel-
scatter SEAM crawl on hard edges and flat faces at grazing iso angles (box, wedge),
and a concentric depth-band crawl on curved SDF surfaces (child #1920, where a
band flips between adjacent integer depths frame-to-frame), the stronger of the
two on master (curved_panel crawls hardest, ellipsoid next). Either way an
interior pixel oscillates dark → light → dark while the camera turns. The
challenge (the issue's stated crux) is separating that genuine jitter from the
*expected* smooth sub-pixel motion every pixel undergoes as the whole image
rotates — which is why the metric keys on curvature, not speed (below).

The discriminator is the **second temporal difference** of per-pixel luminance:

    d2[p,t] = | L[p,t-1] - 2·L[p,t] + L[p,t+1] |

This is the discrete temporal Laplacian — a high-pass filter. A pixel whose
luminance ramps *smoothly* with yaw (ordinary motion of a shaded gradient) has
near-zero curvature, so ``d2 ≈ 0``. A pixel that *oscillates* (a crawling band
toggling across an integer boundary) has large ``d2`` every time it flips. The
first difference ``d1 = |L[p,t] - L[p,t-1]|`` cannot tell the two apart —
smooth motion drives ``d1`` just as hard — which is why curvature, not speed,
is the signal.

Measured over the **interior** only: a pixel that is foreground (the solid, not
the cleared field) in *every* frame of the sweep. AND-ing the per-frame masks
drops the moving silhouette boundary (where a one-time bg→fg crossing would
otherwise read as a spurious ``d2`` spike) and the background, leaving the
stable surface where shading is supposed to vary smoothly — exactly where the
band-crawl lives.

Metrics (over the interior, ROI-scoped, default = whole image):
  * frames          — sequence length N (need N ≥ 3 for a second difference).
  * interior_px     — pixels foreground in all N frames (the measured set).
  * jitter_score    — mean over interior of each pixel's mean |d2|. The
                      headline number: ≈0 for a stable sweep, several luminance
                      units for a crawling one.
  * flicker_p95     — 95th-percentile per-pixel mean |d2|. A few crawling rings
                      on an otherwise-stable sphere barely move the mean but
                      spike the tail, so this catches localized jitter.
  * flicker_frac    — fraction of interior pixels whose mean |d2| exceeds
                      ``--flicker-pixel-tol`` (how much of the surface crawls).
  * mean_abs_d1     — mean interior first difference (overall motion magnitude;
                      diagnostic only — high for smooth motion too).

Backend-agnostic and zoom-robust: luminance curvature measures the same
property on Metal and OpenGL, so one threshold is a shared oracle. Requires a
*fine* sweep — at coarse yaw steps even smooth motion is under-sampled and
``d2`` rises, so feed it ≤~2° steps (see ``scripts/dev/shape-rotate-jitter-sweep``).

Pure stdlib; reuses ``read_png`` / ``write_png`` (via ``render_metric_util``).

Exit codes: 0 metrics within thresholds (or none given) · 1 a threshold was
exceeded · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from array import array

import render_metric_util as util

# Rec. 601 luma weights, scaled to /256 integer arithmetic so the whole metric
# stays in stdlib ints (no float image buffers): luma = (77R + 150G + 29B) >> 8.
_LR, _LG, _LB = 77, 150, 29


def _luma_and_mask(
    path: str,
    roi: tuple[int, int, int, int] | None,
    bg: tuple[int, int, int],
    bg_tol: int,
) -> tuple[array, bytearray, int, int, tuple[int, int, int, int]]:
    """One decode → (luma over ROI, foreground mask over ROI, rw, rh, rect).

    Mirrors ``util.foreground_mask``'s classification (a pixel is foreground
    when any channel differs from ``bg`` by more than ``bg_tol``) but also
    emits the integer luminance in the same pass, so the sequence is decoded
    once per frame rather than twice.
    """
    w, h, bpp, pix = util.read_png(path)
    px = array("B", pix)
    rx, ry, rw, rh = util.resolve_roi(roi, w, h)
    br, bgg, bb = bg
    luma = array("h", bytes(2 * rw * rh))
    mask = bytearray(rw * rh)
    for j in range(rh):
        row = (ry + j) * w
        mbase = j * rw
        for i in range(rw):
            o = (row + rx + i) * bpp
            r = px[o]
            g = px[o + 1]
            b = px[o + 2]
            luma[mbase + i] = (_LR * r + _LG * g + _LB * b) >> 8
            if abs(r - br) > bg_tol or abs(g - bgg) > bg_tol or abs(b - bb) > bg_tol:
                mask[mbase + i] = 1
    return luma, mask, rw, rh, (rx, ry, rw, rh)


def jitter_metrics(
    paths: list[str],
    roi: tuple[int, int, int, int] | None = None,
    bg: tuple[int, int, int] = util.DEFAULT_BG,
    bg_tol: int = util.DEFAULT_BG_TOL,
    flicker_pixel_tol: float = 3.0,
    diff_out: str | None = None,
) -> dict:
    """Score temporal jitter over a yaw-sweep frame sequence.

    Streams the sequence with a 3-frame sliding window so memory stays O(ROI
    pixels) regardless of sequence length: per-pixel ``sum|d2|`` / ``sum|d1|``
    accumulate as frames arrive, and the interior mask is AND-ed incrementally.
    """
    n = len(paths)
    if n < 3:
        raise ValueError(f"need at least 3 frames for a second difference, got {n}")

    luma0, interior, rw, rh, rect = _luma_and_mask(paths[0], roi, bg, bg_tol)
    npx = rw * rh
    sum_d2 = array("d", bytes(8 * npx))  # per-pixel Σ|d2|
    sum_d1 = array("d", bytes(8 * npx))  # per-pixel Σ|d1| (diagnostic)

    prev2 = luma0
    prev1, mask1, _, _, _ = _luma_and_mask(paths[1], roi, bg, bg_tol)
    for i in range(npx):
        interior[i] &= mask1[i]
        sum_d1[i] += abs(prev1[i] - prev2[i])

    for k in range(2, n):
        cur, mask, _, _, _ = _luma_and_mask(paths[k], roi, bg, bg_tol)
        for i in range(npx):
            interior[i] &= mask[i]
            c = cur[i]
            sum_d1[i] += abs(c - prev1[i])
            sum_d2[i] += abs(prev2[i] - 2 * prev1[i] + c)
        prev2, prev1 = prev1, cur

    # Per-pixel means: N-1 first differences, N-2 second differences.
    inv_d1 = 1.0 / (n - 1)
    inv_d2 = 1.0 / (n - 2)
    per_pixel_d2: list[float] = []
    sum_interior_d2 = 0.0
    sum_interior_d1 = 0.0
    crawling = 0
    for i in range(npx):
        if not interior[i]:
            continue
        m2 = sum_d2[i] * inv_d2
        per_pixel_d2.append(m2)
        sum_interior_d2 += m2
        sum_interior_d1 += sum_d1[i] * inv_d1
        if m2 > flicker_pixel_tol:
            crawling += 1

    interior_px = len(per_pixel_d2)
    if interior_px == 0:
        raise ValueError(
            "no interior pixels (foreground in every frame) — check ROI / "
            "background colour, or the shape left the ROI during the sweep"
        )

    per_pixel_d2.sort()
    # Nearest-rank 95th percentile: ceil(0.95 * N) - 1. For N >= 1 (N == 0 is
    # rejected above) this lands in [0, N-1]. The old floor-based index
    # collapsed to the max (interior_px - 1) for small N rather than the 95th.
    p95 = per_pixel_d2[math.ceil(0.95 * interior_px) - 1]

    result = {
        "frames": n,
        "roi": list(rect),
        "interior_px": interior_px,
        "jitter_score": round(sum_interior_d2 / interior_px, 4),
        "flicker_p95": round(p95, 4),
        "flicker_frac": round(crawling / interior_px, 4),
        "mean_abs_d1": round(sum_interior_d1 / interior_px, 4),
        "flicker_pixel_tol": flicker_pixel_tol,
    }

    if diff_out is not None:
        _write_heatmap(diff_out, sum_d2, interior, rw, rh, inv_d2)
        result["diff_out"] = diff_out

    return result


def _write_heatmap(
    path: str, sum_d2: array, interior: bytearray, w: int, h: int, inv_d2: float
) -> None:
    """Grayscale RGBA heatmap of per-pixel mean |d2| (interior only).

    Brightness ∝ jitter so a reviewer can see *where* the surface crawls — the
    crawling rings light up, the stable interior stays dark. Scaled so a mean
    |d2| of 32 luminance units saturates to white (heavy crawl reads clearly
    without a long dim tail).
    """
    scale = 255.0 / 32.0
    out = bytearray(4 * w * h)
    for i in range(w * h):
        v = 0
        if interior[i]:
            v = int(sum_d2[i] * inv_d2 * scale)
            v = 255 if v > 255 else v
        o = i * 4
        out[o] = out[o + 1] = out[o + 2] = v
        out[o + 3] = 255
    util.write_png(path, w, h, bytes(out), 4)


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("images", nargs="+",
                    help="PNG captures in yaw-sweep order (≥3, consecutive "
                         "frames a sub-degree yaw step apart).")
    ap.add_argument("--roi", type=util.parse_roi, default=None,
                    help="x,y,w,h region of interest (default: whole image).")
    ap.add_argument("--bg", type=util.parse_rgb, default=util.DEFAULT_BG,
                    help="background/clear colour r,g,b (default: 0,0,0).")
    ap.add_argument("--bg-tol", type=int, default=util.DEFAULT_BG_TOL,
                    help="per-channel tolerance for the background match.")
    ap.add_argument("--flicker-pixel-tol", type=float, default=3.0,
                    help="a pixel counts as crawling when its mean |d2| exceeds "
                         "this many luminance units (drives flicker_frac).")
    ap.add_argument("--max-jitter", type=float, default=None,
                    help="fail if jitter_score (mean interior |d2|) exceeds this.")
    ap.add_argument("--max-flicker-frac", type=float, default=None,
                    help="fail if flicker_frac (crawling interior fraction) "
                         "exceeds this.")
    ap.add_argument("--max-flicker-p95", type=float, default=None,
                    help="fail if flicker_p95 (per-pixel |d2| tail) exceeds this.")
    ap.add_argument("--diff-out", default=None,
                    help="write a per-pixel jitter heatmap PNG here.")
    args = ap.parse_args(argv)

    try:
        m = jitter_metrics(args.images, args.roi, args.bg, args.bg_tol,
                           args.flicker_pixel_tol, args.diff_out)
    except (OSError, ValueError) as e:
        print(json.dumps({"error": str(e)}))
        return 2

    failed = []
    if args.max_jitter is not None and m["jitter_score"] > args.max_jitter:
        failed.append(f"jitter_score {m['jitter_score']} > {args.max_jitter}")
    if args.max_flicker_frac is not None and m["flicker_frac"] > args.max_flicker_frac:
        failed.append(f"flicker_frac {m['flicker_frac']} > {args.max_flicker_frac}")
    if args.max_flicker_p95 is not None and m["flicker_p95"] > args.max_flicker_p95:
        failed.append(f"flicker_p95 {m['flicker_p95']} > {args.max_flicker_p95}")

    return util.emit(m, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
