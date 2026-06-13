"""Shared helpers for the structural render-metric suite (epic #1766 T-3).

``render-shadow-metric.py`` (#1765) proved the pattern: a backend-agnostic,
zoom-stable structural property of a single capture, gated by render-verify's
``structural`` manifest block (#1768 T-2). T-3 adds three sibling metrics —
``render-coverage-metric.py`` (interior holes), ``render-silhouette-metric.py``
(edge raggedness) and ``render-clip-metric.py`` (bbox occupancy). They share
the same primitives, so the per-pixel classification, the ROI math and the
4-connected flood fill live here once rather than triplicated across the three
dashed-name scripts.

``read_png`` / ``write_png`` live in the dashed-name ``render-compare.py``;
this module does the ``importlib`` dance once so a metric script can simply
``import render_metric_util`` (its own ``scripts/`` dir is ``sys.path[0]``
when run as ``python3 scripts/render-<metric>-metric.py``).

Pure stdlib — no third-party deps, mirroring the rest of ``scripts/``.
"""

from __future__ import annotations

import argparse
import importlib.machinery
import importlib.util
import json
import sys
from array import array
from collections import deque
from pathlib import Path

# read_png / write_png live in the sibling render-compare.py (dashed name ->
# importlib). Centralised here so the metric scripts don't each repeat it.
_CMP = Path(__file__).with_name("render-compare.py")
_loader = importlib.machinery.SourceFileLoader("render_compare", str(_CMP))
_spec = importlib.util.spec_from_loader("render_compare", _loader)
_mod = importlib.util.module_from_spec(_spec)
sys.modules["render_compare"] = _mod
_loader.exec_module(_mod)
read_png = _mod.read_png
write_png = _mod.write_png

# A capture's empty field is the engine's clear colour — black in every demo
# that feeds these gates (verified against the committed shape_debug
# references). ``bg_tol`` absorbs the AA gradient where the silhouette meets
# the field; it is deliberately loose enough to swallow edge anti-aliasing but
# tight enough that a lit interior face (the dimmest sampled ~ (40,80,88)) still
# reads as foreground.
DEFAULT_BG = (0, 0, 0)
DEFAULT_BG_TOL = 16

# Flood fill / component passes are O(ROI pixels). A full 2560x1440 retina
# frame is ~3.7M px which is tractable in pure Python (a few seconds); guard a
# pathological ROI so a tool never hangs silently. Production manifest gates
# should ROI-scope to the solid to stay fast — full-frame is for calibration.
MAX_FLOOD_PX = 4_000_000


def parse_roi(s: str | None) -> tuple[int, int, int, int] | None:
    """``"x,y,w,h"`` -> tuple, or None. Raises argparse error on a bad form."""
    if not s:
        return None
    parts = [int(p) for p in s.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("roi must be x,y,w,h")
    return tuple(parts)  # type: ignore[return-value]


def parse_rgb(s: str) -> tuple[int, int, int]:
    """``"r,g,b"`` -> tuple. Raises argparse error on a bad form."""
    parts = [int(p) for p in s.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("colour must be r,g,b")
    return tuple(parts)  # type: ignore[return-value]


def resolve_roi(
    roi: tuple[int, int, int, int] | None, w: int, h: int
) -> tuple[int, int, int, int]:
    """Clamp/validate an ROI against image bounds (None -> whole image)."""
    if roi is None:
        return 0, 0, w, h
    rx, ry, rw, rh = roi
    if rx < 0 or ry < 0 or rx + rw > w or ry + rh > h or rw <= 0 or rh <= 0:
        raise ValueError(f"roi {roi} out of bounds for {w}x{h} image")
    return rx, ry, rw, rh


def foreground_mask(
    path: str,
    roi: tuple[int, int, int, int] | None = None,
    bg: tuple[int, int, int] = DEFAULT_BG,
    bg_tol: int = DEFAULT_BG_TOL,
) -> tuple[bytearray, int, int, int, tuple[int, int, int, int]]:
    """Build a 0/1 foreground mask over the ROI of a capture.

    A pixel is foreground (mask == 1) when it differs from ``bg`` by more than
    ``bg_tol`` on any channel — i.e. it is the rendered solid, not the cleared
    field. Returns ``(mask, rw, rh, fg_px, (rx, ry, rw, rh))`` where ``mask``
    is row-major over the ROI grid.
    """
    w, h, bpp, pix = read_png(path)
    px = array("B", pix)
    rx, ry, rw, rh = resolve_roi(roi, w, h)
    br, bg_g, bb = bg
    mask = bytearray(rw * rh)
    fg = 0
    for j in range(rh):
        row = (ry + j) * w
        mbase = j * rw
        for i in range(rw):
            o = (row + rx + i) * bpp
            if (abs(px[o] - br) > bg_tol or abs(px[o + 1] - bg_g) > bg_tol
                    or abs(px[o + 2] - bb) > bg_tol):
                mask[mbase + i] = 1
                fg += 1
    return mask, rw, rh, fg, (rx, ry, rw, rh)


def components(mask: bytearray, w: int, h: int) -> tuple[int, int]:
    """4-connected component count + largest size over a 0/1 mask."""
    seen = bytearray(len(mask))
    comps = 0
    largest = 0
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        comps += 1
        size = 0
        q = deque((start,))
        seen[start] = 1
        while q:
            idx = q.popleft()
            size += 1
            x = idx % w
            y = idx // w
            if x > 0 and mask[idx - 1] and not seen[idx - 1]:
                seen[idx - 1] = 1
                q.append(idx - 1)
            if x < w - 1 and mask[idx + 1] and not seen[idx + 1]:
                seen[idx + 1] = 1
                q.append(idx + 1)
            if y > 0 and mask[idx - w] and not seen[idx - w]:
                seen[idx - w] = 1
                q.append(idx - w)
            if y < h - 1 and mask[idx + w] and not seen[idx + w]:
                seen[idx + w] = 1
                q.append(idx + w)
        if size > largest:
            largest = size
    return comps, largest


def enclosed_holes(mask: bytearray, w: int, h: int) -> bytearray:
    """Return a 0/1 mask of *enclosed* background pixels.

    A background pixel (``mask == 0``) is enclosed when it cannot reach the
    ROI border through other background pixels — i.e. it is a hole punched in
    the solid rather than part of the exterior field. Computed by flood-filling
    the exterior from the border; whatever background it does not reach is a
    hole. 4-connected, matching ``components``.
    """
    n = len(mask)
    exterior = bytearray(n)  # 1 = background reachable from the border
    q: deque[int] = deque()

    def _seed(idx: int) -> None:
        if not mask[idx] and not exterior[idx]:
            exterior[idx] = 1
            q.append(idx)

    for x in range(w):
        _seed(x)              # top row
        _seed((h - 1) * w + x)  # bottom row
    for y in range(h):
        _seed(y * w)            # left column
        _seed(y * w + w - 1)    # right column

    while q:
        idx = q.popleft()
        x = idx % w
        y = idx // w
        if x > 0:
            _seed(idx - 1)
        if x < w - 1:
            _seed(idx + 1)
        if y > 0:
            _seed(idx - w)
        if y < h - 1:
            _seed(idx + w)

    holes = bytearray(n)
    for i in range(n):
        if not mask[i] and not exterior[i]:
            holes[i] = 1
    return holes


def perimeter(mask: bytearray, w: int, h: int) -> int:
    """Boundary-edge count of the foreground: for each fg pixel, the number of
    its 4-neighbours that are background or outside the grid. Scales linearly
    with the silhouette boundary length, so ``perimeter**2 / area`` is a
    scale-invariant raggedness measure.
    """
    edges = 0
    for idx in range(len(mask)):
        if not mask[idx]:
            continue
        x = idx % w
        y = idx // w
        if x == 0 or not mask[idx - 1]:
            edges += 1
        if x == w - 1 or not mask[idx + 1]:
            edges += 1
        if y == 0 or not mask[idx - w]:
            edges += 1
        if y == h - 1 or not mask[idx + w]:
            edges += 1
    return edges


def add_common_args(ap: argparse.ArgumentParser) -> None:
    """The ROI + background classification flags shared by every metric."""
    ap.add_argument("image", help="PNG capture to measure.")
    ap.add_argument("--roi", type=parse_roi, default=None,
                    help="x,y,w,h region of interest (default: whole image).")
    ap.add_argument("--bg", type=parse_rgb, default=DEFAULT_BG,
                    help="background/clear colour r,g,b (default: 0,0,0).")
    ap.add_argument("--bg-tol", type=int, default=DEFAULT_BG_TOL,
                    help="per-channel tolerance for the background match.")


def emit(result: dict, failed: list[str]) -> int:
    """Stamp pass/reason, print the JSON object, return the exit code.

    Uniform contract for the structural-metric scripts (see render-verify.py
    ``_run_structural_metric``): 0 = within thresholds, 1 = a threshold was
    exceeded. I/O / format errors (exit 2) are handled by the caller.
    """
    result["pass"] = not failed
    if failed:
        result["reason"] = "; ".join(failed)
    print(json.dumps(result, indent=2))
    return 1 if failed else 0
