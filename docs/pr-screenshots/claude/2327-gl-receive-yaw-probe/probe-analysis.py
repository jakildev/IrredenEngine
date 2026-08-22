"""Measurement script for the #2327 GL detached-receive camera-yaw probe.

Spike artifact (see the findings comment on issue #2327). Not part of the
engine's gated harness set and not intended to merge; it lives here so the
numbers quoted in that comment are reproducible rather than asserted.

The probe scene it reads is `IRCanvasStress --only receiveprobe,floor`, added as
a throwaway opt-in group on this same branch. Each pose is captured twice at the
same camera state -- once normally, once with `--debug-overlay shadow` (shadowed
pixels read magenta) -- and the two are passed together.

Why the receiver's RASTER frame is the discriminating coordinate
---------------------------------------------------------------
At camera yaw theta a DETACHED_REVOXELIZE pool holds cells at R_c^-1 . R_e . local
and the canvas rasters cardinal, so a screen position inverts to the POOL-frame
point p = R_c^-1 . local (R_e = identity for the unrotated receiver). The
world-receive branch then samples the sun map at `worldReceivePos = p + T`
(`c_lighting_to_trixel.glsl`) where the surface point's true world position is
local + T. So, for a caster whose world shadow set is S:

    camera-compensated  ->  shadowed pool-frame set = R_c^-1 (S - T):  ROTATES
    as coded            ->  shadowed pool-frame set = (S - T):         PINNED

"pool-frame centroid constant across camera yaw" is therefore the confirmation
and "rotates by qZ(-theta)" the refutation.

Subcommands
-----------
    faces  <normal.png> <overlay.png> [label]
        Per-face mean colour + shadowed fraction + the received patch's
        pool-frame centroid. Valid at camera CARDINALS only: it reconstructs the
        top face as the rhombus T-R-B-L of an iso axis-aligned cube, which the
        pool only is when the camera yaw is a multiple of pi/2.
    coverage <normal.png> <overlay.png> [label]
        Shadowed fraction of the whole receiver silhouette. Needs no face
        segmentation, so it is the fallback at residual yaw -- but read it as
        indicative: which faces are visible changes with yaw, so the denominator
        is not constant.
    floor <normal.png> [label]
        The same-frame control. The floor is a C_ShapeDescriptor box on the MAIN
        canvas, lit through the ordinary camera-compensated path. With
        faceFactor = sunAmbient + (1 - sunAmbient) * lambert (ambient 0.30) and
        the probe sun, its predicted red channel per world face is
        -z 118, -x 108, +y 87, +x / -y 45 -- so its two visible side edges must
        walk {108,45} -> {45,45} -> {45,87} -> {87,108} across yaw 0/90/180/270.
        (r=78 is the shadowed floor top, not a side face.)

Pure stdlib; reuses scripts/render-compare.py's read_png.
"""

from __future__ import annotations

import importlib.machinery
import importlib.util
import sys
from array import array
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
_CMP = REPO / "scripts" / "render-compare.py"
_loader = importlib.machinery.SourceFileLoader("render_compare", str(_CMP))
_spec = importlib.util.spec_from_loader("render_compare", _loader)
_mod = importlib.util.module_from_spec(_spec)
sys.modules["render_compare"] = _mod
_loader.exec_module(_mod)
read_png = _mod.read_png

HALF_EXTENT = 12.0  # the probe receiver is 24^3, so pool coords run -12 .. +12


def _pixels(path):
    w, h, bpp, pix = read_png(str(path))
    return w, h, bpp, array("B", pix)


def receiver_pixels(path):
    """Receiver mask by hue -- it is the only saturated blue in the scene."""
    w, h, bpp, px = _pixels(path)
    pts, rgb = [], {}
    for y in range(h):
        for x in range(w):
            o = ((y * w) + x) * bpp
            if px[o + 2] - px[o] > 40:
                pts.append((x, y))
                rgb[(x, y)] = (px[o], px[o + 1], px[o + 2])
    return pts, rgb


def magenta_pixels(path):
    w, h, bpp, px = _pixels(path)
    pts = []
    for y in range(h):
        for x in range(w):
            o = ((y * w) + x) * bpp
            if px[o] > 200 and px[o + 1] < 60 and px[o + 2] > 200:
                pts.append((x, y))
    return pts


def rhombus(pts):
    """T / L / R / B of an iso axis-aligned cube silhouette's top face."""
    top = min(pts, key=lambda p: (p[1], p[0]))
    left = min(pts, key=lambda p: (p[0], p[1]))
    right = max(pts, key=lambda p: (p[0], -p[1]))
    bottom = (left[0] + right[0] - top[0], left[1] + right[1] - top[1])
    return top, left, right, bottom


def basis(top, left, right, bottom):
    cx = (top[0] + bottom[0]) / 2.0
    cy = (top[1] + bottom[1]) / 2.0
    return cx, cy, right[0] - cx, right[1] - cy, top[0] - cx, top[1] - cy


def to_face(p, b):
    """Screen point -> (pool x, pool y, a, b) in the top-face rhombus basis.

    Under iso (iso.x = -x + y, iso.y = -x - y + 2z) with screen-right = +iso.x
    and screen-down = +iso.y, the top-face corners map as T = pool(+12, +12),
    R = pool(-12, +12), L = pool(+12, -12), B = pool(-12, -12), so
    pool = (12(b - a), 12(a + b)). Inside the rhombus iff |a| + |b| <= 1.
    """
    cx, cy, rx, ry, tx, ty = b
    det = rx * ty - ry * tx
    if abs(det) < 1e-6:
        return None
    dx, dy = p[0] - cx, p[1] - cy
    a = (dx * ty - dy * tx) / det
    bb = (rx * dy - ry * dx) / det
    return HALF_EXTENT * (bb - a), HALF_EXTENT * (a + bb), a, bb


def mean_rgb(pts, rgb):
    if not pts:
        return None
    n = len(pts)
    return tuple(round(sum(rgb[p][i] for p in pts) / n, 1) for i in range(3))


def cmd_faces(normal, overlay, label):
    rpts, rgb = receiver_pixels(normal)
    if not rpts:
        print(f"{label}: no receiver pixels in {Path(normal).name}")
        return
    top, left, right, bottom = rhombus(rpts)
    b = basis(top, left, right, bottom)

    face_top, face_left, face_right = [], [], []
    for p in rpts:
        f = to_face(p, b)
        if f and abs(f[2]) + abs(f[3]) <= 1.0:
            face_top.append(p)
        elif p[0] < bottom[0]:
            face_left.append(p)
        else:
            face_right.append(p)

    mset = set(magenta_pixels(overlay))
    rset = set(rpts)
    on_face, off_receiver = [], []
    for p in mset:
        f = to_face(p, b)
        if f and abs(f[2]) + abs(f[3]) <= 1.0:
            on_face.append((f[0], f[1]))
        elif p not in rset:
            off_receiver.append(p)

    # A face at ambient-only brightness is ambiguous alone: faceFactor collapses
    # to sunAmbient both when the face is backlit and when it is lit but fully
    # shadowed. Pair the mean colour with the shadowed fraction to separate them.
    def shadow_frac(face):
        if not face:
            return None
        return round(100.0 * sum(1 for p in face if p in mset) / len(face), 1)

    print(f"--- {label}")
    print(f"    silhouette T={top} L={left} R={right} B={bottom}")
    print("    SHADING (mean rgb / % shadowed, pool-frame slot):"
          f" top={mean_rgb(face_top, rgb)}/{shadow_frac(face_top)}%"
          f" left={mean_rgb(face_left, rgb)}/{shadow_frac(face_left)}%"
          f" right={mean_rgb(face_right, rgb)}/{shadow_frac(face_right)}%")
    cov = 100.0 * len(on_face) / max(len(face_top), 1)
    print(f"    RECEIVE top-face px={len(face_top)} shadowed={len(on_face)}"
          f" coverage={cov:.1f}%")
    if on_face:
        cx = sum(p[0] for p in on_face) / len(on_face)
        cy = sum(p[1] for p in on_face) / len(on_face)
        print(f"            centroid (pool frame) = ({cx:+.2f}, {cy:+.2f})")
    else:
        print("            centroid: NONE (top face fully lit)")
    if off_receiver:
        ccx, ccy = b[0], b[1]
        fx = sum(p[0] for p in off_receiver) / len(off_receiver)
        fy = sum(p[1] for p in off_receiver) / len(off_receiver)
        print(f"    CAST floor-shadow px={len(off_receiver)} centroid offset from"
              f" receiver centre = ({fx - ccx:+.1f}, {fy - ccy:+.1f}) screen px")


def cmd_coverage(normal, overlay, label):
    rpts, _ = receiver_pixels(normal)
    mset = set(magenta_pixels(overlay))
    hit = sum(1 for p in rpts if p in mset)
    print(f"{label:>14}: receiver px={len(rpts):6d}  shadowed={hit:6d}"
          f"  {100.0 * hit / max(len(rpts), 1):5.1f}%")


def cmd_floor(normal, label):
    w, h, bpp, px = _pixels(normal)
    counts = {}
    for y in range(h):
        for x in range(w):
            o = ((y * w) + x) * bpp
            r, b = px[o], px[o + 2]
            # The floor is the scene's only neutral: b - r is about +8 lit.
            if r > 8 and 0 <= b - r <= 14:
                counts[r] = counts.get(r, 0) + 1
    rows = sorted((k for k, v in counts.items() if v >= 400), reverse=True)
    detail = "  ".join(f"r={k}({counts[k]})" for k in rows)
    print(f"{label:>10}: floor greys >=400px: {detail}")


if __name__ == "__main__":
    sub = sys.argv[1]
    if sub == "faces":
        cmd_faces(sys.argv[2], sys.argv[3],
                  sys.argv[4] if len(sys.argv) > 4 else "pose")
    elif sub == "coverage":
        cmd_coverage(sys.argv[2], sys.argv[3],
                     sys.argv[4] if len(sys.argv) > 4 else "pose")
    elif sub == "floor":
        cmd_floor(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "pose")
    else:
        raise SystemExit(f"unknown subcommand {sub!r} (faces|coverage|floor)")
