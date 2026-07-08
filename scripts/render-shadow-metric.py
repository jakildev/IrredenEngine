#!/usr/bin/env python3
"""Structural sun-shadow metrics for deterministic render validation.

Where ``render-compare.py`` does a pixel-diff against a committed reference,
this measures a *structural* property of a single capture: how
hole-ridden / fragmented a cast shadow is inside a region of interest. Two
reasons that matters for the engine's render-validation:

  * **Deterministic at zoom.** Pixel-diff is excluded above ~2x magnification
    because CPU<->GPU float divergence drifts the match% run-to-run
    (canvas_stress's ``revoxelize_solids_zoom`` shot is excluded for exactly
    this). Occupancy classification + connected-component counts don't move
    under sub-pixel jitter, so the zoom shots can be gated.
  * **Backend-agnostic.** Metal and OpenGL produce pixel-different output, so
    pixel-diff needs a per-backend reference set. A structural metric measures
    the same property on both, so one threshold is a shared oracle.

Built for SHADOW debug-overlay captures (engine render mode ``SHADOW``:
black = lit, magenta = shadowed; see ``engine/render/CLAUDE.md``), where
shadow occupancy is encoded directly as colour and the classification is
exact. The swiss-cheese / cross-hatch-dithering failure mode (epic #1717
items 3-4) shows up as a high ``hole_ratio`` and an exploded ``components``
count; a clean shadow is ~0 holes and a handful of large components.

Metrics (within the ROI, default = whole image):
  * shadow_px / lit_px  — classified-pixel counts.
  * hole_ratio          — lit_px / (lit_px + shadow_px): fraction of the
                          cast-shadow region that reads as a lit hole.
  * components          — 4-connected shadow components (cross-hatch shatters
                          a clean blob into many tiny ones).
  * largest_frac        — largest component / shadow_px (~1 clean, low when
                          fragmented).

Pure stdlib; reuses ``read_png()`` from ``render-compare.py``.

Exit codes: 0 metrics within thresholds (or no thresholds given) · 1 a
threshold was exceeded · 2 I/O or format error.
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

# read_png lives in the sibling render-compare.py (dashed name -> importlib).
_CMP = Path(__file__).with_name("render-compare.py")
_loader = importlib.machinery.SourceFileLoader("render_compare", str(_CMP))
_spec = importlib.util.spec_from_loader("render_compare", _loader)
_mod = importlib.util.module_from_spec(_spec)
sys.modules["render_compare"] = _mod
_loader.exec_module(_mod)
read_png = _mod.read_png

# Classification thresholds, loose enough to absorb AA / FP jitter on the
# debug-overlay's flat fills (magenta = vec3(1,0,1), lit = vec3(0)).
SHADOW_MIN_RB = 180   # magenta: high red AND blue
SHADOW_MAX_G = 90     # magenta: low green
LIT_MAX = 70          # lit: all channels near zero

# Flood fill is O(ROI pixels); a full 1280x720 frame is ~900k px which is fine,
# but guard a pathological ROI so the tool never hangs silently.
MAX_FLOOD_PX = 4_000_000


def _classify(r: int, g: int, b: int) -> int:
    """1 = shadowed (magenta), -1 = lit (black), 0 = neither (bg/entity)."""
    if r >= SHADOW_MIN_RB and b >= SHADOW_MIN_RB and g <= SHADOW_MAX_G:
        return 1
    if r <= LIT_MAX and g <= LIT_MAX and b <= LIT_MAX:
        return -1
    return 0


def shadow_metrics(
    path: str,
    roi: tuple[int, int, int, int] | None = None,
    count_components: bool = True,
) -> dict:
    w, h, bpp, pix = read_png(path)
    px = array("B", pix)

    if roi is None:
        rx, ry, rw, rh = 0, 0, w, h
    else:
        rx, ry, rw, rh = roi
        if rx < 0 or ry < 0 or rx + rw > w or ry + rh > h or rw <= 0 or rh <= 0:
            raise ValueError(f"roi {roi} out of bounds for {w}x{h} image")

    # shadow mask over the ROI grid: True where shadowed.
    mask = bytearray(rw * rh)
    shadow_px = 0
    lit_px = 0
    for j in range(rh):
        row = (ry + j) * w
        mbase = j * rw
        for i in range(rw):
            o = (row + rx + i) * bpp
            c = _classify(px[o], px[o + 1], px[o + 2])
            if c == 1:
                mask[mbase + i] = 1
                shadow_px += 1
            elif c == -1:
                lit_px += 1

    classified = shadow_px + lit_px
    hole_ratio = (lit_px / classified) if classified else 0.0

    result = {
        "image": path,
        "roi": [rx, ry, rw, rh],
        "shadow_px": shadow_px,
        "lit_px": lit_px,
        "hole_ratio": round(hole_ratio, 4),
    }

    if count_components and shadow_px:
        if rw * rh > MAX_FLOOD_PX:
            result["components"] = None
            result["components_note"] = "roi too large for component count"
        else:
            comps, largest = _components(mask, rw, rh)
            result["components"] = comps
            result["largest_frac"] = round(largest / shadow_px, 4)

    return result


def _components(mask: bytearray, w: int, h: int) -> tuple[int, int]:
    """4-connected component count + largest size over the shadow mask."""
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


def _parse_roi(s: str | None) -> tuple[int, int, int, int] | None:
    if not s:
        return None
    parts = [int(p) for p in s.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("roi must be x,y,w,h")
    return tuple(parts)  # type: ignore[return-value]


def _main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", help="PNG capture (SHADOW debug-overlay mode).")
    ap.add_argument("--roi", type=_parse_roi, default=None,
                    help="x,y,w,h region of interest (default: whole image).")
    ap.add_argument("--max-hole-ratio", type=float, default=None,
                    help="fail if hole_ratio exceeds this.")
    ap.add_argument("--min-hole-ratio", type=float, default=None,
                    help="fail if hole_ratio falls below this. Inverts the "
                         "swiss-cheese upper bound for the zero-caster floor "
                         "self-shadow guard (#2092): a fully-lit floor reads "
                         "hole_ratio ~1.0, and self-shadow acne (shadow where "
                         "there should be none) drives it down.")
    ap.add_argument("--max-components", type=int, default=None,
                    help="fail if the shadow shatters into more than this many "
                         "4-connected components.")
    ap.add_argument("--min-largest-frac", type=float, default=None,
                    help="fail if the largest shadow component is a smaller "
                         "fraction of shadow_px than this. A single convex "
                         "caster reads ~1.0; fragmentation (swiss-cheese) "
                         "shatters it small, and a vanished shadow reads 0 — so "
                         "this one lower bound catches BOTH the fragment and the "
                         "missing-shadow regressions. Requires the component "
                         "pass (not --no-components).")
    ap.add_argument("--no-components", action="store_true",
                    help="skip the (slower) connected-component pass.")
    args = ap.parse_args(argv)

    try:
        m = shadow_metrics(args.image, args.roi, not args.no_components)
    except (OSError, ValueError) as e:
        print(json.dumps({"error": str(e)}))
        return 2

    failed = []
    if args.max_hole_ratio is not None and m["hole_ratio"] > args.max_hole_ratio:
        failed.append(f"hole_ratio {m['hole_ratio']} > {args.max_hole_ratio}")
    if args.min_hole_ratio is not None and m["hole_ratio"] < args.min_hole_ratio:
        failed.append(f"hole_ratio {m['hole_ratio']} < {args.min_hole_ratio}")
    if args.max_components is not None:
        if m.get("components") is None:
            print(
                "warning: --max-components requested but component count was "
                "skipped (roi too large; see components_note in output)",
                file=sys.stderr,
            )
            failed.append("max-components check skipped (roi too large)")
        elif m["components"] > args.max_components:
            failed.append(f"components {m['components']} > {args.max_components}")
    if args.min_largest_frac is not None:
        if args.no_components:
            print(
                "warning: --min-largest-frac requires the component pass; do "
                "not combine with --no-components",
                file=sys.stderr,
            )
            failed.append("min-largest-frac check skipped (--no-components)")
        elif "largest_frac" in m:
            if m["largest_frac"] < args.min_largest_frac:
                failed.append(
                    f"largest_frac {m['largest_frac']} < {args.min_largest_frac}"
                )
        elif "components" in m:  # present but None -> roi too large to flood
            print(
                "warning: --min-largest-frac requested but the component pass "
                "was skipped (roi too large; see components_note in output)",
                file=sys.stderr,
            )
            failed.append("min-largest-frac check skipped (roi too large)")
        else:  # shadow_px == 0: the shadow vanished entirely
            failed.append(
                f"largest_frac 0.0 < {args.min_largest_frac} (no shadow pixels)"
            )
    m["pass"] = not failed
    if failed:
        m["reason"] = "; ".join(failed)

    print(json.dumps(m, indent=2))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
