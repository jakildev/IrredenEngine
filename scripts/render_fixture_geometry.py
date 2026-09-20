"""Authored geometry of the IRCanvasStress orbit fixtures, shared by the
projected-face oracles (``render-source-face-metric.py``,
``render-mixed-canvas-metric.py``). Pure stdlib.

The focused orbit shape sits at the world origin, rotated 45 degrees about
the (1,1,1) diagonal unless the demo runs with ``--focus-identity``; the camera
Z-yaw rotates the world into the view frame as R_z(-yaw).
"""

import itertools
import math

ORBIT_EXTENTS = {"frame": 14, "octahedron": 10, "voxel": 1}


def rotate(point, identity=False, axis_angle=None):
    """Rigid axis-angle rotation; the orbit fixture defaults to (1,1,1), 45 degrees."""
    if identity:
        return point
    x, y, z, degrees = axis_angle or (1, 1, 1, 45)
    scale = max(abs(x), abs(y), abs(z))
    if not scale:
        raise ValueError("rotation axis must be nonzero")
    scaled = (x / scale, y / scale, z / scale)
    length = math.hypot(*scaled)
    axis = tuple(value / length for value in scaled)
    cosine, sine = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    cross = (axis[1]*point[2] - axis[2]*point[1],
             axis[2]*point[0] - axis[0]*point[2],
             axis[0]*point[1] - axis[1]*point[0])
    along = sum(a*b for a, b in zip(axis, point))
    return tuple(cosine*point[i] + (1-cosine)*along*axis[i] + sine*cross[i]
                 for i in range(3))


def view(point, yaw):
    """World to view frame under a continuous camera Z-yaw, R_z(-yaw)."""
    x, y, z = point
    return (math.cos(yaw) * x + math.sin(yaw) * y,
            -math.sin(yaw) * x + math.cos(yaw) * y, z)


def iso(point):
    x, y, z = point
    return (-x + y, -x - y + 2 * z)


def source_centers(shape):
    """Authored voxel centers of an orbit shape or the adjacent-pair probe."""
    if shape == "adjacent":
        yield (-.5, 0, 0)
        yield (.5, 0, 0)
        return
    extent = ORBIT_EXTENTS[shape]
    for index in itertools.product(range(extent), repeat=3):
        center = tuple(value - (extent - 1) / 2 for value in index)
        if shape == "frame" and sum(abs(v) >= extent / 2 - 1.5 for v in center) < 2:
            continue
        if shape == "octahedron" and sum(map(abs, center)) > extent / 2 * 1.35:
            continue
        yield center
