#!/usr/bin/env python3
"""Voxel-face coverage metric for deterministic render validation.

Sibling to ``render-shadow-metric.py`` (#1765); part of the structural-metric
suite (epic #1766 T-3). Where the shadow metric measures hole-riddenness of a
*shadow* overlay, this measures hole-riddenness of a *solid body*: how much of
a rendered solid's silhouette reads as a background hole punched through it.

That is the deterministic gate for the missing-face / re-voxelize hole defect
(#1619 and its class): a solid that should present a continuous front face
instead shows the cleared field through gaps where voxel faces dropped out. A
clean solid is ~0 holes; a defective one shows interior background components.

Why structural rather than pixel-diff (same rationale as the shadow metric):

  * **Deterministic at zoom.** Occupancy classification + connected-component
    counts don't move under the sub-pixel CPU<->GPU float jitter that excludes
    pixel-diff above ~2x magnification.
  * **Backend-agnostic.** Metal and OpenGL produce pixel-different output but
    the same silhouette topology, so one threshold gates both reference sets.

Definition. Foreground = any pixel that differs from the clear colour (default
black) by more than ``--bg-tol``. A background pixel is an *interior hole* when
it cannot reach the ROI border through other background pixels — i.e. it is
enclosed by the solid, not part of the exterior field. The exterior is found by
flood-filling background inward from the border; everything it misses is a hole.

Metrics (within the ROI, default = whole image):
  * fg_px / hole_px   — solid pixels / enclosed-background (hole) pixels.
  * silhouette_px     — fg_px + hole_px (the filled-in solid footprint).
  * hole_ratio        — hole_px / silhouette_px: fraction of the silhouette
                        that reads as a background hole (0 clean).
  * coverage          — fg_px / silhouette_px: 1 - hole_ratio (1 clean).
  * hole_components   — 4-connected interior-hole blobs (a single dropped face
                        is one; cross-hatch shatter is many).
  * largest_hole_frac — largest hole / silhouette_px.

Reuses ``read_png`` (via ``render_metric_util``) from ``render-compare.py``.

Exit codes: 0 metrics within thresholds (or none given) · 1 a threshold was
exceeded · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import json
import sys

import render_metric_util as util


def coverage_metrics(
    path: str,
    roi: tuple[int, int, int, int] | None = None,
    bg: tuple[int, int, int] = util.DEFAULT_BG,
    bg_tol: int = util.DEFAULT_BG_TOL,
    count_components: bool = True,
) -> dict:
    mask, rw, rh, fg_px, rect = util.foreground_mask(path, roi, bg, bg_tol)

    holes = util.enclosed_holes(mask, rw, rh)
    hole_px = sum(holes)
    silhouette_px = fg_px + hole_px
    hole_ratio = (hole_px / silhouette_px) if silhouette_px else 0.0
    coverage = (fg_px / silhouette_px) if silhouette_px else 1.0

    result = {
        "image": path,
        "roi": list(rect),
        "fg_px": fg_px,
        "hole_px": hole_px,
        "silhouette_px": silhouette_px,
        "hole_ratio": round(hole_ratio, 4),
        "coverage": round(coverage, 4),
    }

    if count_components and hole_px:
        if rw * rh > util.MAX_FLOOD_PX:
            result["hole_components"] = None
            result["components_note"] = "roi too large for component count"
        else:
            comps, largest = util.components(holes, rw, rh)
            result["hole_components"] = comps
            result["largest_hole_frac"] = round(largest / silhouette_px, 4)

    return result


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    util.add_common_args(ap)
    ap.add_argument("--max-hole-ratio", type=float, default=None,
                    help="fail if hole_ratio exceeds this.")
    ap.add_argument("--min-coverage", type=float, default=None,
                    help="fail if coverage (1 - hole_ratio) is below this.")
    ap.add_argument("--max-hole-components", type=int, default=None,
                    help="fail if the silhouette holds more than this many "
                         "4-connected interior-hole components.")
    ap.add_argument("--no-components", action="store_true",
                    help="skip the (slower) connected-component pass.")
    args = ap.parse_args(argv)

    try:
        m = coverage_metrics(args.image, args.roi, args.bg, args.bg_tol,
                             not args.no_components)
    except (OSError, ValueError) as e:
        print(json.dumps({"error": str(e)}))
        return 2

    failed = []
    if args.max_hole_ratio is not None and m["hole_ratio"] > args.max_hole_ratio:
        failed.append(f"hole_ratio {m['hole_ratio']} > {args.max_hole_ratio}")
    if args.min_coverage is not None and m["coverage"] < args.min_coverage:
        failed.append(f"coverage {m['coverage']} < {args.min_coverage}")
    if args.max_hole_components is not None:
        if m.get("hole_components") is None:
            print("warning: --max-hole-components requested but component count "
                  "was skipped (roi too large; see components_note)",
                  file=sys.stderr)
            failed.append("max-hole-components check skipped (roi too large)")
        elif m["hole_components"] > args.max_hole_components:
            failed.append(
                f"hole_components {m['hole_components']} > {args.max_hole_components}")

    return util.emit(m, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
