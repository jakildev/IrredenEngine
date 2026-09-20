"""The destination lattice a `DETACHED_REVOXELIZE` canvas rasters, for the
offline oracles: the CanvasStress proof solids, the engine's float32
quaternion path, the anchored round-half-up inverse resample over the
destination cube (`revoxSourceCellForDest`), and lattice sun visibility.
Pure stdlib.

Cells live in the camera-composed frame (`quatInverse(R_z(yaw)) * entity`),
which is the view frame the isometric projection reads directly; a solid's
true points sit at `cell + anchor`, the anchor being -0.5 on every even-sized
centered axis and 0 on odd ones.
"""

import itertools
import math
import struct

from render_metric_util import ray_box_distance

FIXTURES = {
    "lprism": dict(axis=(1.0, 0.6, 0.3), angle=math.pi / 4.5, carve=True, extent=(12, 12, 12)),
    "cube": dict(axis=(0.3, 1.0, 0.5), angle=math.pi / 5.0, carve=False, extent=(12, 12, 12)),
    "grounded": dict(axis=(0.5, 1.0, 0.2), angle=math.pi / 4.2, carve=False,
                     extent=(12, 12, 12)),
    "parity": dict(axis=(0.3, 1.0, 0.5), angle=math.pi / 5.0, carve=False, extent=(12, 12, 11)),
    "halftexel": dict(axis=(0.3, 1.0, 0.5), angle=math.pi / 5.0, carve=False,
                      extent=(12, 11, 11)),
}
IDENTITY = (0.0, 0.0, 0.0, 1.0)
LIT_LABEL, SHADOWED_LABEL, BACKFACING_LABEL = 1, 2, 3
TIE_EPSILON = 1e-3


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def vec_f32(vector):
    return tuple(f32(v) for v in vector)


def quat_axis_angle(axis, angle):
    length = math.sqrt(sum(a * a for a in axis))
    unit = vec_f32(a / length for a in axis)
    half = f32(angle * 0.5)
    sine, cosine = f32(math.sin(half)), f32(math.cos(half))
    return vec_f32((unit[0] * sine, unit[1] * sine, unit[2] * sine, cosine))


def quat_mul(a, b):
    return vec_f32((
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ))


def quat_inverse(q):
    return (-q[0], -q[1], -q[2], q[3])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def rotate(vector, q):
    u = q[:3]
    t = vec_f32(2.0 * c for c in cross(u, vector))
    w = q[3]
    second = cross(u, t)
    return vec_f32(vector[i] + w * t[i] + second[i] for i in range(3))


def round_half_up(value):
    return math.floor(value + 0.5)


def composed_rotation(fixture, yaw, upright):
    entity = IDENTITY if upright else quat_axis_angle(fixture["axis"], fixture["angle"])
    camera = quat_axis_angle((0.0, 0.0, 1.0), yaw)
    return quat_mul(quat_inverse(camera), entity), camera


def source_cells(fixture):
    """Authored cells of the centered solid, keyed like the GPU source grid."""
    offset = tuple(-(extent - 1) * 0.5 for extent in fixture["extent"])
    cells = set()
    for index in itertools.product(*(range(extent) for extent in fixture["extent"])):
        position = tuple(value + offset[i] for i, value in enumerate(index))
        if fixture["carve"] and position[0] > 0 and position[1] > 0:
            continue
        cells.add(tuple(round_half_up(v) for v in position))
    anchor = tuple(offset[i] - round_half_up(offset[i]) for i in range(3))
    radius = math.sqrt(sum(o * o for o in offset))
    return cells, anchor, radius


class Resample:
    """The destination-lattice occupancy the inverse-resample kernel authors."""

    def __init__(self, fixture, rotation):
        self.cells, self.anchor, radius = source_cells(fixture)
        self.inverse = quat_inverse(rotation)
        center = math.ceil(radius)
        shift = tuple(1 if a < -0.25 else 0 for a in self.anchor)
        self.window = [range(-center + shift[i], center + 1 + shift[i]) for i in range(3)]
        self.memo = {}
        self.ties = set()

    def covered(self, cell):
        if cell in self.memo:
            return self.memo[cell]
        point = tuple(cell[i] + self.anchor[i] for i in range(3))
        rotated = rotate(point, self.inverse)
        unrounded = tuple(rotated[i] - self.anchor[i] + 0.5 for i in range(3))
        if any(abs(v - round(v)) < TIE_EPSILON for v in unrounded):
            self.ties.add(cell)
        source = tuple(math.floor(v) for v in unrounded)
        result = source in self.cells
        self.memo[cell] = result
        return result

    def occupied(self):
        return [cell for cell in itertools.product(*self.window) if self.covered(cell)]

    def exposed(self, cell, normal):
        neighbor = tuple(cell[i] + normal[i] for i in range(3))
        return not self.covered(neighbor)

    def walk(self, cell, start, sun):
        """Cells the ray from `start` toward the sun crosses, own cell first.

        A 3D DDA over the unit lattice from a point on one of the cell's
        faces until the ray leaves the destination window; its first step is
        the zero-length crossing into the neighbour beyond that face. A ray
        that only grazes an edge or corner of a cell still crosses it.
        """
        current = list(cell)
        step = [1 if s > 0 else -1 for s in sun]
        next_t, delta_t = [], []
        for i in range(3):
            if sun[i] == 0:
                next_t.append(math.inf)
                delta_t.append(math.inf)
                continue
            boundary = current[i] + self.anchor[i] + (0.5 if sun[i] > 0 else -0.5)
            next_t.append((boundary - start[i]) / sun[i])
            delta_t.append(1.0 / abs(sun[i]))
        bounds = [(r.start - 1, r.stop) for r in self.window]
        while all(bounds[i][0] <= current[i] <= bounds[i][1] for i in range(3)):
            yield tuple(current)
            axis = min(range(3), key=lambda i: next_t[i])
            current[axis] += step[axis]
            next_t[axis] += delta_t[axis]

    def sun_visible(self, cell, normal, sun, start):
        """Lattice sun visibility at one point of a face: LIT, SHADOWED or BACKFACING.

        Any occupied cell the ray from `start` crosses, other than the face's
        own, shadows the point.
        """
        if sum(n * s for n, s in zip(normal, sun)) <= 0:
            return BACKFACING_LABEL
        if any(visited != cell and self.covered(visited)
               for visited in self.walk(cell, start, sun)):
            return SHADOWED_LABEL
        return LIT_LABEL

    def ray_clearance(self, cell, start, sun):
        """Closest approach, in cells, of the ray from `start` toward the sun
        to any occupied cell other than its own: exact below one cell,
        reported as 1.0 beyond.

        A cell the ray clears by less than one cell is a lattice neighbour of
        a cell the walk crosses, so only that neighbourhood is searched.
        """
        candidates = set()
        for visited in self.walk(cell, start, sun):
            for offset in itertools.product((-1, 0, 1), repeat=3):
                neighbor = tuple(visited[i] + offset[i] for i in range(3))
                if neighbor != cell and self.covered(neighbor):
                    candidates.add(neighbor)
        span = 2.0 * sum(len(r) for r in self.window)
        clearance = 1.0
        for candidate in candidates:
            low = [candidate[i] + self.anchor[i] - 0.5 for i in range(3)]
            high = [candidate[i] + self.anchor[i] + 0.5 for i in range(3)]
            clearance = min(clearance, ray_box_distance(start, sun, low, high, span))
        return clearance


def view_sun(sun, camera):
    """The world sun direction in the camera-composed frame the cells live in."""
    length = math.sqrt(sum(s * s for s in sun))
    return rotate(tuple(s / length for s in sun), quat_inverse(camera))


def camera_face_hits(cells, anchor, iso_point):
    """Intersect an orthographic camera ray with occupied unit boxes, nearest first.

    The ray is p(t) = (-u/2-v/6, u/2-v/6, v/3) + t*(1,1,1).
    Slab intersections do not depend on triangle parity or polygon draw order.
    Edge/corner hits report every entering axis instead of choosing a face.
    """
    u, v = iso_point
    origin = (-u / 2 - v / 6, u / 2 - v / 6, v / 3)
    hits = []
    for cell in cells:
        near = [cell[i] + anchor[i] - 0.5 - origin[i] for i in range(3)]
        far = [cell[i] + anchor[i] + 0.5 - origin[i] for i in range(3)]
        entry, leave = max(near), min(far)
        if entry >= leave - 1e-9:
            continue
        axes = [i for i in range(3) if abs(near[i] - entry) <= 1e-9]
        hits.append(dict(cell=cell, axes=axes, depth=3 * entry,
                         point=tuple(value + entry for value in origin)))
    return sorted(hits, key=lambda hit: (hit["depth"], hit["cell"]))
