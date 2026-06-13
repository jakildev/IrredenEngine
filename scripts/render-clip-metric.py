#!/usr/bin/env python3
"""Clip-detection metric for deterministic render validation.

Sibling to ``render-shadow-metric.py`` (#1765); part of the structural-metric
suite (epic #1766 T-3). Detects an entity that should be visible inside an
expected bounding box but isn't — the zoom-clip / cull-clip failure class
(#1431, #1570, #1740): a solid that drops out of frame (or gets clipped at the
viewport edge) leaves its expected bbox reading all background.

Two signals, both over the ROI (which *is* the expected bbox — pass it via
``--roi``; a whole-image ROI only catches a totally blank frame):

  * **occupancy** — foreground fraction of the bbox. An entity that clipped
    away entirely reads ~0; a present one reads a healthy fraction. Gate with
    ``--min-occupancy``: fail when the bbox is emptier than it should be.
  * **edge_touch_frac** — fraction of the bbox *border* that is foreground. A
    solid cut off by the viewport runs hard along the frame/bbox edge instead
    of sitting inside it, so a high edge-touch fraction flags an edge-clip even
    when occupancy looks fine. Gate with ``--max-edge-touch-frac``.

Why structural: occupancy and edge contact are stable under sub-pixel
CPU<->GPU jitter and identical on Metal and OpenGL, so one threshold gates both
reference sets — and unlike pixel-diff, the metric needs no committed reference
image, only the expected bbox.

Metrics:
  * fg_px / total_px  — foreground / bbox-area pixel counts.
  * occupancy         — fg_px / total_px (0 = fully clipped/blank bbox).
  * edge_px / edge_total / edge_touch_frac — fg on the bbox border / border
                        length / their ratio (high = entity cut at the edge).

Reuses ``read_png`` (via ``render_metric_util``) from ``render-compare.py``.

Exit codes: 0 metrics within thresholds (or none given) · 1 a threshold was
exceeded · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import json
import sys

import render_metric_util as util


def clip_metrics(
    path: str,
    roi: tuple[int, int, int, int] | None = None,
    bg: tuple[int, int, int] = util.DEFAULT_BG,
    bg_tol: int = util.DEFAULT_BG_TOL,
) -> dict:
    mask, rw, rh, fg_px, rect = util.foreground_mask(path, roi, bg, bg_tol)

    total_px = rw * rh
    occupancy = (fg_px / total_px) if total_px else 0.0

    # Border = the one-pixel ring of the ROI; a corner counts once. For a
    # 1xN or Nx1 ROI every pixel is border, which the set-of-indices form
    # below handles without double-counting.
    edge_idx = set()
    for x in range(rw):
        edge_idx.add(x)
        edge_idx.add((rh - 1) * rw + x)
    for y in range(rh):
        edge_idx.add(y * rw)
        edge_idx.add(y * rw + rw - 1)
    edge_total = len(edge_idx)
    edge_px = sum(1 for i in edge_idx if mask[i])
    edge_touch_frac = (edge_px / edge_total) if edge_total else 0.0

    return {
        "image": path,
        "roi": list(rect),
        "fg_px": fg_px,
        "total_px": total_px,
        "occupancy": round(occupancy, 4),
        "edge_px": edge_px,
        "edge_total": edge_total,
        "edge_touch_frac": round(edge_touch_frac, 4),
    }


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    util.add_common_args(ap)
    ap.add_argument("--min-occupancy", type=float, default=None,
                    help="fail if the bbox foreground fraction is below this "
                         "(the entity clipped away / blank bbox).")
    ap.add_argument("--max-edge-touch-frac", type=float, default=None,
                    help="fail if more than this fraction of the bbox border "
                         "is foreground (the entity is cut at the edge).")
    args = ap.parse_args(argv)

    try:
        m = clip_metrics(args.image, args.roi, args.bg, args.bg_tol)
    except (OSError, ValueError) as e:
        print(json.dumps({"error": str(e)}))
        return 2

    failed = []
    if args.min_occupancy is not None and m["occupancy"] < args.min_occupancy:
        failed.append(f"occupancy {m['occupancy']} < {args.min_occupancy}")
    if (args.max_edge_touch_frac is not None
            and m["edge_touch_frac"] > args.max_edge_touch_frac):
        failed.append(
            f"edge_touch_frac {m['edge_touch_frac']} > {args.max_edge_touch_frac}")

    return util.emit(m, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
