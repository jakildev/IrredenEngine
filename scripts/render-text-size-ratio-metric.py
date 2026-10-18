#!/usr/bin/env python3
"""Glyph-set height ratio of the shape_debug ``text_size_sample`` capture.

The capture draws the character sheet twice, side by side: the LARGE (6x11)
set in the left column and the SMALL (4x7) set in the right. The SMALL set is
only proven to have reached the GPU when its lines are measurably shorter, so
this gate measures the first sheet line of each column and gates their ratio.

Measurement, over the ROI:

  * A pixel is **lit** when its R+G+B exceeds ``--lit-threshold`` (white glyph
    triangles on the cleared black field).
  * The two columns are the lit-column runs either side of the widest unlit
    gap; the left one is LARGE, the right one SMALL.
  * A column's **line height** is the contiguous run of image rows, from its
    first lit row down, that hold any lit pixel in that column's x-range —
    the continuous triangle reach of the first line. The following line starts
    after an unlit gap, which ends the run.

The expected ratio is 8 / 12 (~0.667): a cell of h rows reaches h + 1 triangle
rows, 8 for the 7-row SMALL cell and 12 for the 11-row LARGE one. A SMALL
column drawn with the LARGE set reads 1.0.

Exit codes: 0 within thresholds (or none given) · 1 a threshold was exceeded
or the capture does not hold two measurable columns · 2 I/O or format error.
"""

from __future__ import annotations

import argparse
import json
import sys
from array import array

import render_metric_util as util

DEFAULT_LIT_THRESHOLD = 300


def _lit_runs(flags: list[bool]) -> list[tuple[int, int]]:
    """Inclusive (first, last) index pairs of each run of True in ``flags``."""
    runs: list[tuple[int, int]] = []
    start = None
    for i, flag in enumerate(flags):
        if flag and start is None:
            start = i
        elif not flag and start is not None:
            runs.append((start, i - 1))
            start = None
    if start is not None:
        runs.append((start, len(flags) - 1))
    return runs


def ratio_metrics(
    path: str,
    roi: tuple[int, int, int, int] | None = None,
    lit_threshold: int = DEFAULT_LIT_THRESHOLD,
) -> dict:
    w, h, bpp, pix = util.read_png(path)
    px = array("B", pix)
    rx, ry, rw, rh = util.resolve_roi(roi, w, h)

    lit = bytearray(rw * rh)
    for j in range(rh):
        row = (ry + j) * w
        for i in range(rw):
            o = (row + rx + i) * bpp
            if px[o] + px[o + 1] + px[o + 2] > lit_threshold:
                lit[j * rw + i] = 1

    result: dict = {"image": path, "roi": [rx, ry, rw, rh]}
    column_lit = [any(lit[j * rw + i] for j in range(rh)) for i in range(rw)]
    column_runs = _lit_runs(column_lit)
    if len(column_runs) < 2:
        result["error"] = "fewer than two lit column runs: no LARGE / SMALL pair"
        return result

    # The widest gap between consecutive lit column runs separates the two
    # glyph columns; narrower gaps are letter spacing inside a column.
    gaps = [(column_runs[k + 1][0] - column_runs[k][1], k)
            for k in range(len(column_runs) - 1)]
    _, split = max(gaps)
    columns = {
        "large": (column_runs[0][0], column_runs[split][1]),
        "small": (column_runs[split + 1][0], column_runs[-1][1]),
    }

    for name, (x0, x1) in columns.items():
        row_lit = [any(lit[j * rw + i] for i in range(x0, x1 + 1)) for j in range(rh)]
        first, last = _lit_runs(row_lit)[0]
        result[name] = {
            "x": [rx + x0, rx + x1],
            "rows": [ry + first, ry + last],
            "height": last - first + 1,
        }
        if last == rh - 1:
            result["error"] = (f"{name} first line reaches the ROI bottom edge: "
                               f"the ROI cuts the line")
            return result

    result["ratio"] = round(result["small"]["height"] / result["large"]["height"], 4)
    return result


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", help="PNG capture to measure.")
    ap.add_argument("--roi", type=util.parse_roi, default=None,
                    help="x,y,w,h region of interest (default: whole image).")
    ap.add_argument("--lit-threshold", type=int, default=DEFAULT_LIT_THRESHOLD,
                    help="R+G+B above which a pixel is lit glyph coverage.")
    ap.add_argument("--min-ratio", type=float, default=None,
                    help="fail if SMALL / LARGE line height is below this.")
    ap.add_argument("--max-ratio", type=float, default=None,
                    help="fail if SMALL / LARGE line height is above this.")
    args = ap.parse_args(argv)

    try:
        m = ratio_metrics(args.image, args.roi, args.lit_threshold)
    except (OSError, ValueError) as e:
        print(json.dumps({"error": str(e)}))
        return 2

    if "error" in m:
        return util.emit(m, [m.pop("error")])
    failed = []
    if args.min_ratio is not None and m["ratio"] < args.min_ratio:
        failed.append(f"ratio {m['ratio']} < {args.min_ratio}")
    if args.max_ratio is not None and m["ratio"] > args.max_ratio:
        failed.append(f"ratio {m['ratio']} > {args.max_ratio}")
    return util.emit(m, failed)


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
