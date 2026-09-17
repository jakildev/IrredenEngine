#!/usr/bin/env python3
"""Rotated-staircase AO metric: how much of a solid the AO pass darkens.

A rotated GRID / re-voxelized solid is real voxels, so its tilted-flat
surface is a true staircase whose every tread/riser corner is locally a
concave crease. ``c_compute_voxel_ao`` must not read that quantization
as contact shading — the failure mode is venetian-blind banding across the
whole tilted face. This measures it: the fraction of a solid's pixels the
AO overlay marks occluded, plus how dark the darkest one is.

Two captures of the SAME camera: a flagless (lit) frame that locates the
solids by colour, and an ``--debug-overlay ao`` frame where AO is encoded
as ``(1 - ao, ao, 0)`` — pure green is unoccluded, any red is darkening
(``c_lighting_to_trixel``). The solid mask is every lit pixel whose chroma
(max - min channel) exceeds ``--chroma-min``: the coloured cubes, never
the grey floor or the black field. ``--dominant r|g|b`` narrows it to one
cube by its dominant channel.

Metrics (within the ROI, default = whole image):
  * solid_px       — mask size.
  * occluded_px    — mask pixels the overlay darkens (R > 0 on G > 128, B == 0).
  * occluded_frac  — occluded_px / solid_px: the banding coverage.
  * max_darkening  — max R / 255 over occluded pixels (1 - min ao).
  * mean_darkening — mean R / 255 over occluded pixels.

Capture recipe (yaw 0 puts the GRID cubes on the single-canvas route the
resample guards; the revox solids raster into private canvases at every yaw):

  fleet-run IRCanvasStress --no-auto-rotate --no-spin --frozen-pose 0.47 \\
      --only gridspin,floor --zoom 4 --sweep-yaw 0 0.785398163 3 \\
      --auto-screenshot 6                     # lit: shots 1..3
  ... same flags + --debug-overlay ao         # overlay: shots 1..3
  python3 scripts/render-ao-staircase-metric.py <overlay.png> --lit <lit.png>

Pure stdlib; reuses ``read_png()`` via ``render_metric_util``.

Exit codes: 0 metrics within thresholds (or no thresholds given) · 1 a
threshold was exceeded · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import sys
from array import array

import render_metric_util as rmu

# Overlay classification: the AO overlay writes (1 - ao, ao, 0), so an
# overlay pixel belongs to the pass when green dominates and blue is exactly
# zero (albedo-coloured overflow regions carry blue and drop out).
OVERLAY_MIN_G = 128
DEFAULT_CHROMA_MIN = 40


def staircase_metrics(
    overlay: str,
    lit: str,
    roi: tuple[int, int, int, int] | None = None,
    chroma_min: int = DEFAULT_CHROMA_MIN,
    dominant: str | None = None,
) -> dict:
    w, h, bpp, pix = rmu.read_png(lit)
    ow, oh, obpp, opix = rmu.read_png(overlay)
    if (ow, oh) != (w, h):
        raise ValueError(
            f"{overlay} is {ow}x{oh} but {lit} is {w}x{h}; captures must share a camera"
        )
    lp = array("B", pix)
    op = array("B", opix)
    rx, ry, rw, rh = rmu.resolve_roi(roi, w, h)
    dom = {"r": 0, "g": 1, "b": 2}.get(dominant or "", None)

    solid_px = occluded_px = 0
    max_r = sum_r = 0
    for j in range(rh):
        row = (ry + j) * w
        for i in range(rw):
            lo = (row + rx + i) * bpp
            rgb = (lp[lo], lp[lo + 1], lp[lo + 2])
            if max(rgb) - min(rgb) <= chroma_min:
                continue
            if dom is not None and rgb[dom] != max(rgb):
                continue
            solid_px += 1
            oo = (row + rx + i) * obpp
            r, g, b = op[oo], op[oo + 1], op[oo + 2]
            if g > OVERLAY_MIN_G and b == 0 and r > 0:
                occluded_px += 1
                sum_r += r
                max_r = max(max_r, r)

    return {
        "overlay": overlay,
        "lit": lit,
        "roi": [rx, ry, rw, rh],
        "solid_px": solid_px,
        "occluded_px": occluded_px,
        "occluded_frac": round(occluded_px / solid_px, 4) if solid_px else 0.0,
        "max_darkening": round(max_r / 255.0, 4),
        "mean_darkening": round(sum_r / occluded_px / 255.0, 4) if occluded_px else 0.0,
    }


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", help="PNG capture in AO debug-overlay mode.")
    ap.add_argument("--lit", required=True,
                    help="flagless capture of the same camera; locates the solids.")
    ap.add_argument("--roi", type=rmu.parse_roi, default=None,
                    help="x,y,w,h region of interest (default: whole image).")
    ap.add_argument("--chroma-min", type=int, default=DEFAULT_CHROMA_MIN,
                    help="lit pixel is a solid when max-min channel exceeds this.")
    ap.add_argument("--dominant", choices=("r", "g", "b"), default=None,
                    help="restrict the solid mask to one cube by dominant channel.")
    ap.add_argument("--max-occluded-frac", type=float, default=None,
                    help="fail if occluded_frac exceeds this.")
    ap.add_argument("--min-occluded-px", type=int, default=None,
                    help="fail if fewer pixels are occluded — the positive control "
                         "that AO is running at all on the measured solid.")
    ap.add_argument("--max-darkening", type=float, default=None,
                    help="fail if max_darkening exceeds this.")
    args = ap.parse_args(argv)

    try:
        result = staircase_metrics(
            args.image, args.lit, args.roi, args.chroma_min, args.dominant
        )
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if result["solid_px"] == 0:
        print("error: solid mask is empty — wrong --lit capture, ROI or --chroma-min",
              file=sys.stderr)
        return 2

    failed: list[str] = []
    if args.max_occluded_frac is not None and result["occluded_frac"] > args.max_occluded_frac:
        failed.append(f"occluded_frac {result['occluded_frac']} > {args.max_occluded_frac}")
    if args.min_occluded_px is not None and result["occluded_px"] < args.min_occluded_px:
        failed.append(f"occluded_px {result['occluded_px']} < {args.min_occluded_px}")
    if args.max_darkening is not None and result["max_darkening"] > args.max_darkening:
        failed.append(f"max_darkening {result['max_darkening']} > {args.max_darkening}")
    return rmu.emit(result, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
