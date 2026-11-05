"""Grading for the gather-filter diagnostic over IRShapeDebug's fixture.

``IRShapeDebug --gather-filter-fixture`` draws, unlit on a black background,
three 16x16x2 checker bodies (GRID, DETACHED, DETACHED_REVOXELIZE, left to
right), an SDF box, two touching flat boxes, one voxel and a thin rotated
DETACHED bar. ``IR_GATHER_FILTER_DIAGNOSTIC`` selects the gather's colour
resolve: unset (nearest), ``unrestricted`` or ``owner``.

Every arm of one pose is graded against the nearest capture of that pose,
which carries the depth, entity id, alpha and hover the diagnostic never
changes:

* ``coverage_changes``: a pixel that is background in one capture and drawn in
  the other;
* ``bleed``: a drawn pixel whose colour leaves its nearest owner's palette (the
  checker plane r + g + b = 380, or the flat owner colour);
* ``owner_disagreements``: a drawn pixel whose colour is closest to an owner
  other than the one nearest drew there;
* ``changed``: pixels that differ at all, per owner (where the filter acted).

The temporal reading is the motion-compensated difference of the checker
interiors between consecutive shots of a zoom sweep. Shot k+1 is registered to
shot k by a least-squares scale and translation fitted to the nearest
captures' owner centroids; each interior framebuffer pixel of shot k is
compared with the bilinear sample of shot k+1 at its registered position.

Captures are read at framebuffer resolution: with an integer output scale s the
screen pixel ``s * i + s - 1`` lies inside framebuffer pixel i's block for any
upscale residual in [0, s).

Pure stdlib.
"""

from __future__ import annotations

from pathlib import Path

import render_metric_util as util

BACKGROUND = 0
CHECKER = 1
SDF = 2
PAIR_RED = 3
PAIR_GREEN = 4
VOXEL = 5
BAR = 6
OWNER_NAMES = {
    BACKGROUND: "background", CHECKER: "checker", SDF: "sdf_box", PAIR_RED: "pair_red",
    PAIR_GREEN: "pair_green", VOXEL: "one_voxel", BAR: "detached_bar",
}
CHECKER_PALETTE = ((200, 60, 120), (60, 200, 120), (120, 60, 200), (60, 120, 200))
CHECKER_PLANE = 380
FLAT_OWNERS = {
    BACKGROUND: (0, 0, 0),
    SDF: (20, 60, 120),
    PAIR_RED: (250, 40, 40),
    PAIR_GREEN: (40, 250, 40),
    VOXEL: (250, 250, 0),
    BAR: (255, 255, 255),
}
BODY_NAMES = ("grid", "detached", "revoxelized")
# Off-plane / off-colour slack for 8-bit rounding of a filtered mix.
PLANE_TOLERANCE = 3
FLAT_TOLERANCE = 2
INTERIOR_RADIUS = 2
# The DETACHED owners are drawn as source faces, never through the gather, and
# in IRShapeDebug their coverage differs between identical runs. They are
# fenced off by their nearest bounding box grown by this margin and reported
# apart from the gated counts.
SOURCE_FACE_OWNERS = ("checker_detached", "detached_bar")
SOURCE_FACE_MARGIN = 16

_EXACT = {rgb: CHECKER for rgb in CHECKER_PALETTE}
_EXACT.update({rgb: owner for owner, rgb in FLAT_OWNERS.items()})


class Frame:
    """One capture at framebuffer resolution: packed RGB rows."""

    def __init__(self, path: Path, scale: int):
        width, height, bpp, pixels = util.read_png(str(path))
        self.width = width // scale
        self.height = height // scale
        stride = width * bpp
        self.rgb: list[tuple[int, int, int]] = []
        for y in range(self.height):
            row = (y * scale + scale - 1) * stride
            for x in range(self.width):
                at = row + (x * scale + scale - 1) * bpp
                self.rgb.append((pixels[at], pixels[at + 1], pixels[at + 2]))

    def at(self, x: int, y: int) -> tuple[int, int, int]:
        return self.rgb[y * self.width + x]


class Owners:
    """Per-pixel owner of a nearest capture, checker pixels split by body."""

    def __init__(self, frame: Frame):
        self.frame = frame
        self.owner = [_EXACT.get(rgb, -1) for rgb in frame.rgb]
        self.unknown = sum(1 for owner in self.owner if owner < 0)
        drawn = [i for i, owner in enumerate(self.owner) if owner != BACKGROUND]
        if not drawn:
            raise ValueError("capture has no foreground")
        w = frame.width
        xs = [i % w for i in drawn]
        ys = [i // w for i in drawn]
        self.box = (max(min(xs) - 1, 0), max(min(ys) - 1, 0),
                    min(max(xs) + 2, w), min(max(ys) + 2, frame.height))
        self.body = self._split_bodies()
        self.fenced = self._source_face_boxes()

    def _split_bodies(self) -> list[int]:
        """Body index (0..2) of every checker pixel, -1 elsewhere: the bodies sit
        side by side, so the two widest column gaps separate them."""
        w = self.frame.width
        columns = sorted({i % w for i, owner in enumerate(self.owner) if owner == CHECKER})
        gaps = sorted(((b - a, a) for a, b in zip(columns, columns[1:]) if b - a > 1),
                      reverse=True)[:2]
        cuts = sorted(a for _, a in gaps)
        body = [-1] * len(self.owner)
        for i, owner in enumerate(self.owner):
            if owner == CHECKER:
                body[i] = sum(1 for cut in cuts if i % w > cut)
        return body

    def _source_face_boxes(self) -> list[tuple[int, int, int, int]]:
        w = self.frame.width
        boxes: dict[str, list[int]] = {}
        for i, owner in enumerate(self.owner):
            name = self.name(i)
            if name not in SOURCE_FACE_OWNERS:
                continue
            x, y = i % w, i // w
            box = boxes.setdefault(name, [x, y, x, y])
            box[0], box[1] = min(box[0], x), min(box[1], y)
            box[2], box[3] = max(box[2], x), max(box[3], y)
        m = SOURCE_FACE_MARGIN
        return [(b[0] - m, b[1] - m, b[2] + m, b[3] + m) for b in boxes.values()]

    def name(self, i: int) -> str:
        owner = self.owner[i]
        if owner == CHECKER:
            return f"checker_{BODY_NAMES[self.body[i]]}"
        return OWNER_NAMES.get(owner, "unknown")

    def is_fenced(self, x: int, y: int) -> bool:
        return any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in self.fenced)

    def neighbours_same(self, x: int, y: int, radius: int) -> bool:
        w, h = self.frame.width, self.frame.height
        key = self.owner[y * w + x], self.body[y * w + x]
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < w and 0 <= ny < h):
                    return False
                if (self.owner[ny * w + nx], self.body[ny * w + nx]) != key:
                    return False
        return True

    def centroids(self) -> dict[str, tuple[float, float, int]]:
        """Centroid and area of every flat owner and every checker body."""
        sums: dict[str, list[float]] = {}
        w = self.frame.width
        for i, owner in enumerate(self.owner):
            if owner <= BACKGROUND:
                continue
            key = (f"checker_{BODY_NAMES[self.body[i]]}" if owner == CHECKER
                   else OWNER_NAMES[owner])
            acc = sums.setdefault(key, [0.0, 0.0, 0])
            acc[0] += i % w
            acc[1] += i // w
            acc[2] += 1
        return {key: (sx / n, sy / n, n) for key, (sx, sy, n) in sums.items()}


def _closest_owner(rgb: tuple[int, int, int]) -> int:
    best, best_distance = CHECKER, abs(sum(rgb) - CHECKER_PLANE) / 3 ** 0.5
    for owner, flat in FLAT_OWNERS.items():
        distance = sum((a - b) ** 2 for a, b in zip(rgb, flat)) ** 0.5
        if distance < best_distance:
            best, best_distance = owner, distance
    return best


def _bleeds(owner: int, rgb: tuple[int, int, int]) -> bool:
    if owner == CHECKER:
        return abs(sum(rgb) - CHECKER_PLANE) > PLANE_TOLERANCE
    return max(abs(a - b) for a, b in zip(rgb, FLAT_OWNERS[owner])) > FLAT_TOLERANCE


def ownership(nearest: Owners, arm: Frame) -> dict:
    """Counts of one arm against the nearest capture of the same pose."""
    if (arm.width, arm.height) != (nearest.frame.width, nearest.frame.height):
        raise ValueError("arm and nearest captures differ in size")
    w = arm.width
    counts = {"coverage_changes": 0, "owner_disagreements": 0,
              "bleed_boundary": 0, "bleed_interior": 0, "source_face_differing": 0}
    changed: dict[str, int] = {}
    bleed_by_owner: dict[str, int] = {}
    x0, y0, x1, y1 = nearest.box
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = y * w + x
            owner = nearest.owner[i]
            rgb = arm.rgb[i]
            reference = nearest.frame.rgb[i]
            if nearest.is_fenced(x, y):
                counts["source_face_differing"] += rgb != reference
                continue
            if (owner == BACKGROUND) != (rgb == FLAT_OWNERS[BACKGROUND]):
                counts["coverage_changes"] += 1
                continue
            if owner <= BACKGROUND:
                continue
            name = nearest.name(i)
            if rgb != reference:
                changed[name] = changed.get(name, 0) + 1
            if _closest_owner(rgb) != owner:
                counts["owner_disagreements"] += 1
            if _bleeds(owner, rgb):
                bleed_by_owner[name] = bleed_by_owner.get(name, 0) + 1
                boundary = not nearest.neighbours_same(x, y, 1)
                counts["bleed_boundary" if boundary else "bleed_interior"] += 1
    counts["changed"] = changed
    counts["bleed_by_owner"] = bleed_by_owner
    counts["bleed"] = counts["bleed_boundary"] + counts["bleed_interior"]
    return counts


def registration(first: Owners, second: Owners) -> dict:
    """Least-squares scale s and translation t with second ~ s * first + t over
    the centroids both captures share, the source-face owners excluded."""
    a, b = first.centroids(), second.centroids()
    keys = sorted(k for k in a.keys() & b.keys() if k not in SOURCE_FACE_OWNERS)
    if len(keys) < 2:
        raise ValueError("fewer than two shared owners to register on")
    pa = [(a[k][0], a[k][1]) for k in keys]
    pb = [(b[k][0], b[k][1]) for k in keys]
    n = len(keys)
    ma = (sum(p[0] for p in pa) / n, sum(p[1] for p in pa) / n)
    mb = (sum(p[0] for p in pb) / n, sum(p[1] for p in pb) / n)
    num = sum((p[0] - ma[0]) * (q[0] - mb[0]) + (p[1] - ma[1]) * (q[1] - mb[1])
              for p, q in zip(pa, pb))
    den = sum((p[0] - ma[0]) ** 2 + (p[1] - ma[1]) ** 2 for p in pa)
    scale = num / den
    tx, ty = mb[0] - scale * ma[0], mb[1] - scale * ma[1]
    residual = max(((scale * p[0] + tx - q[0]) ** 2 + (scale * p[1] + ty - q[1]) ** 2) ** 0.5
                   for p, q in zip(pa, pb))
    return {"scale": scale, "translation": (tx, ty), "owners": keys,
            "max_residual_px": round(residual, 4)}


def _bilinear(frame: Frame, x: float, y: float) -> tuple[float, float, float]:
    ix, iy = int(x), int(y)
    fx, fy = x - ix, y - iy
    c00, c10 = frame.at(ix, iy), frame.at(ix + 1, iy)
    c01, c11 = frame.at(ix, iy + 1), frame.at(ix + 1, iy + 1)
    return tuple((c00[c] * (1 - fx) + c10[c] * fx) * (1 - fy) +
                 (c01[c] * (1 - fx) + c11[c] * fx) * fy for c in range(3))


def temporal(first_owner: Owners, second_owner: Owners, reg: dict,
             first: Frame, second: Frame) -> dict[str, tuple[float, int]]:
    """Per checker body: mean |shot k - registered shot k+1| over the interior
    (8-bit units, channel-averaged) and the interior pixel count."""
    scale, (tx, ty) = reg["scale"], reg["translation"]
    w, h = first.width, first.height
    sums: dict[str, list[float]] = {}
    x0, y0, x1, y1 = first_owner.box
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = y * w + x
            if first_owner.owner[i] != CHECKER or first_owner.is_fenced(x, y):
                continue
            if not first_owner.neighbours_same(x, y, INTERIOR_RADIUS):
                continue
            qx, qy = scale * x + tx, scale * y + ty
            rx, ry = int(qx + 0.5), int(qy + 0.5)
            if not (INTERIOR_RADIUS <= rx < w - INTERIOR_RADIUS - 1 and
                    INTERIOR_RADIUS <= ry < h - INTERIOR_RADIUS - 1):
                continue
            j = ry * w + rx
            if (second_owner.owner[j] != CHECKER or
                    second_owner.body[j] != first_owner.body[i] or
                    not second_owner.neighbours_same(rx, ry, INTERIOR_RADIUS)):
                continue
            sample = _bilinear(second, qx, qy)
            difference = sum(abs(a - b) for a, b in zip(first.rgb[i], sample)) / 3
            acc = sums.setdefault(BODY_NAMES[first_owner.body[i]], [0.0, 0])
            acc[0] += difference
            acc[1] += 1
    return {body: (total / count, count) for body, (total, count) in sums.items()}


def interior_mean(owners: Owners, frame: Frame) -> dict[str, tuple[float, float, float]]:
    """Per checker body: the mean colour of ``frame`` over the nearest
    capture's interior. A resolve that keeps each body's area-weighted palette
    holds it fixed across zoom; blurring alone cannot."""
    w = frame.width
    sums: dict[str, list[float]] = {}
    x0, y0, x1, y1 = owners.box
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = y * w + x
            if owners.owner[i] != CHECKER or owners.is_fenced(x, y):
                continue
            if not owners.neighbours_same(x, y, INTERIOR_RADIUS):
                continue
            acc = sums.setdefault(BODY_NAMES[owners.body[i]], [0.0, 0.0, 0.0, 0])
            for c in range(3):
                acc[c] += frame.rgb[i][c]
            acc[3] += 1
    return {body: (r / n, g / n, b / n) for body, (r, g, b, n) in sums.items()}

