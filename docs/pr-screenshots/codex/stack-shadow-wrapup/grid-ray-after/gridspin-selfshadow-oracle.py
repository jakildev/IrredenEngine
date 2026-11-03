"""Independent center-ray probe of GRID inverse-resampled 12^3 cube geometry.

This models the documented destination-lattice inverse map and exact ray/AABB
hits on exposed voxel faces. It does not model camera projection or sun-map
quantization and therefore is a geometry expectation, not screenshot scoring.
"""

import json
import math
from pathlib import Path


SOURCE_MIN = -5
SOURCE_MAX = 6
SUN = (-0.42, -0.60, -0.55)
L = math.sqrt(sum(c*c for c in SUN))
SUN = tuple(c/L for c in SUN)
FACES = [(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)]
QUANT_BIAS = 4.0 / 1024.0


def round_half_up(x):
    return math.floor(x+0.5)


def occupancy(angle):
    c, s = math.cos(angle), math.sin(angle)
    source = {tuple(v) for v in []}
    # The transformed 12-cell box fits in this 18-cell destination window.
    for z in range(-8, 9):
        for y in range(-10, 11):
            for x in range(-10, 11):
                sx = round_half_up(c*x+s*y)
                sy = round_half_up(-s*x+c*y)
                sz = round_half_up(z)
                if all(SOURCE_MIN <= v <= SOURCE_MAX for v in (sx,sy,sz)):
                    source.add((x,y,z))
    return source


def hit_ray_box(origin, direction, cell):
    t0, t1 = 1e-5, 100.0
    for i in range(3):
        lo, hi = cell[i]-0.5, cell[i]+0.5
        if abs(direction[i]) < 1e-12:
            if origin[i] < lo or origin[i] > hi:
                return False
            continue
        a, b = (lo-origin[i])/direction[i], (hi-origin[i])/direction[i]
        t0, t1 = max(t0,min(a,b)), min(t1,max(a,b))
        if t1 <= t0:
            return False
    return True


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    size = math.sqrt(sum(x*x for x in a))
    return tuple(x/size for x in a)


U_HAT = unit(cross((0, 0, 1), SUN))
V_HAT = unit(cross(SUN, U_HAT))


def project(point):
    return (sum(a*b for a,b in zip(point,U_HAT)),
            sum(a*b for a,b in zip(point,V_HAT)),
            -sum(a*b for a,b in zip(point,SUN)))


def finite_face_quads(cells):
    quads = []
    for cell in cells:
        for normal in FACES:
            if tuple(cell[i]+normal[i] for i in range(3)) in cells:
                continue
            center = tuple(cell[i]+0.5*normal[i] for i in range(3))
            inplane = [axis for axis in FACES[::2] if sum(a*b for a,b in zip(axis,normal)) == 0]
            assert len(inplane) == 2
            corner = tuple(center[i]-0.5*inplane[0][i]-0.5*inplane[1][i]
                           for i in range(3))
            p = project(corner)
            pu = project(tuple(corner[i]+inplane[0][i] for i in range(3)))
            pv = project(tuple(corner[i]+inplane[1][i] for i in range(3)))
            edge_u = tuple(pu[i]-p[i] for i in range(3))
            edge_v = tuple(pv[i]-p[i] for i in range(3))
            quads.append((p, edge_u, edge_v))
    return quads


def projected_face_separation(receiver, face):
    """Exact algebra of sourceFaceRaySeparation, using our independent quads."""
    corner, eu, ev = face
    determinant = eu[0]*ev[1]-eu[1]*ev[0]
    if abs(determinant) < 1e-6:
        return -1.0
    du, dv = receiver[0]-corner[0], receiver[1]-corner[1]
    u = (du*ev[1]-dv*ev[0])/determinant
    v = (eu[0]*dv-eu[1]*du)/determinant
    if u < 0 or v < 0 or u > 1 or v > 1:
        return -1.0
    return receiver[2] - (corner[2]+u*eu[2]+v*ev[2])


def measure(angle):
    cells = occupancy(angle)
    by_normal = {}
    witnesses = []
    quads = finite_face_quads(cells)
    disagreements = []
    for normal in FACES:
        if sum(a*b for a,b in zip(normal,SUN)) <= 0:
            continue
        exposed, shadowed = 0, 0
        for cell in cells:
            if tuple(cell[i]+normal[i] for i in range(3)) in cells:
                continue
            exposed += 1
            center = tuple(cell[i]+0.5*normal[i] for i in range(3))
            blocked = any(hit_ray_box(center,SUN,other) for other in cells if other != cell)
            receiver = project(center)
            quad_blocked = any(QUANT_BIAS < projected_face_separation(receiver,quad) < 100
                               for quad in quads)
            if blocked != quad_blocked:
                disagreements.append((cell,normal,blocked,quad_blocked))
            if blocked:
                shadowed += 1
                if len(witnesses) < 8:
                    witnesses.append((cell,normal,center))
        by_normal[normal] = (shadowed, exposed)
    print(f'angle={math.degrees(angle):.1f} cells={len(cells)} '
          f'lit-side shadowed/exposed={by_normal} '
          f'quad-vs-box-disagreements={len(disagreements)} '
          f'first-disagreements={disagreements[:5]} witnesses={witnesses}')
    return dict(angle_degrees=math.degrees(angle), occupied_cells=len(cells),
                sun_facing_faces={str(k): dict(shadowed=v[0], exposed=v[1])
                                  for k,v in by_normal.items()},
                projected_quad_vs_box_disagreements=len(disagreements),
                first_disagreements=disagreements[:5], witnesses=witnesses)


if __name__ == '__main__':
    results = [measure(math.radians(degrees))
               for degrees in (0,15,30,45,60,75,90)]
    Path('/tmp/codex-gridspin-selfshadow-oracle-results.json').write_text(
        json.dumps(results, indent=2) + '\n')
