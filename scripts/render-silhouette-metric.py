#!/usr/bin/env python3
"""Silhouette-raggedness metric for deterministic render validation.

Sibling to ``render-shadow-metric.py`` (#1765); part of the structural-metric
suite (epic #1766 T-3). Measures how *ragged* a rendered solid's silhouette is
versus its clean iso projection: a clean shape has a smooth, compact boundary,
while a defective one (edge-voxel dropout, speckle, cross-hatch noise) carries a
longer, noisier boundary for the same area, and may shed detached fragments.

The discriminator is the boundary length relative to area. For a filled shape,
scaling by ``k`` scales the boundary by ``k`` and the area by ``k**2``, so
``perimeter**2 / area`` is scale-invariant — the same clean shape gates the same
at every zoom and on both backends (the iso staircase contributes a fixed
overhead a calibrated threshold absorbs; genuine raggedness pushes it past).

Why structural rather than pixel-diff: occupancy classification, boundary-edge
counts and component counts are stable under the sub-pixel CPU<->GPU float
jitter that excludes pixel-diff above ~2x magnification, and they measure the
same property on Metal and OpenGL, so one threshold is a shared oracle.

Metrics (within the ROI, default = whole image):
  * area              — foreground (solid) pixel count.
  * perimeter         — foreground/background boundary edges.
  * perimeter_ratio   — perimeter**2 / area: scale-invariant raggedness
                        (lower = cleaner; a filled square is 16, a disc ~4*pi).
  * components        — 4-connected foreground components (speckle / dropout
                        sheds a clean silhouette into detached fragments).
  * largest_frac      — largest component / area (~1 clean, low when fragmented).

Reuses ``read_png`` (via ``render_metric_util``) from ``render-compare.py``.

Exit codes: 0 metrics within thresholds (or none given) · 1 a threshold was
exceeded · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import json
import sys

import render_metric_util as util


def silhouette_metrics(
    path: str,
    roi: tuple[int, int, int, int] | None = None,
    bg: tuple[int, int, int] = util.DEFAULT_BG,
    bg_tol: int = util.DEFAULT_BG_TOL,
    count_components: bool = True,
) -> dict:
    mask, rw, rh, area, rect = util.foreground_mask(path, roi, bg, bg_tol)

    perim = util.perimeter(mask, rw, rh)
    perimeter_ratio = (perim * perim / area) if area else 0.0

    result = {
        "image": path,
        "roi": list(rect),
        "area": area,
        "perimeter": perim,
        "perimeter_ratio": round(perimeter_ratio, 4),
    }

    if count_components and area:
        if rw * rh > util.MAX_FLOOD_PX:
            result["components"] = None
            result["components_note"] = "roi too large for component count"
        else:
            comps, largest = util.components(mask, rw, rh)
            result["components"] = comps
            result["largest_frac"] = round(largest / area, 4)

    return result


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    util.add_common_args(ap)
    ap.add_argument("--max-perimeter-ratio", type=float, default=None,
                    help="fail if perimeter**2/area exceeds this (too ragged).")
    ap.add_argument("--max-components", type=int, default=None,
                    help="fail if the silhouette shatters into more than this "
                         "many 4-connected foreground components.")
    ap.add_argument("--no-components", action="store_true",
                    help="skip the (slower) connected-component pass.")
    args = ap.parse_args(argv)

    try:
        m = silhouette_metrics(args.image, args.roi, args.bg, args.bg_tol,
                              not args.no_components)
    except (OSError, ValueError) as e:
        print(json.dumps({"error": str(e)}))
        return 2

    failed = []
    if (args.max_perimeter_ratio is not None
            and m["perimeter_ratio"] > args.max_perimeter_ratio):
        failed.append(
            f"perimeter_ratio {m['perimeter_ratio']} > {args.max_perimeter_ratio}")
    if args.max_components is not None:
        if m.get("components") is None:
            print("warning: --max-components requested but component count was "
                  "skipped (roi too large; see components_note)",
                  file=sys.stderr)
            failed.append("max-components check skipped (roi too large)")
        elif m["components"] > args.max_components:
            failed.append(f"components {m['components']} > {args.max_components}")

    return util.emit(m, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
