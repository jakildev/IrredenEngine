#!/usr/bin/env python3
"""Ray-check focused orbit-6 GRID 45° Y rotation at camera yaw 135°."""
import argparse
import json
import math
import sys
from collections import Counter
from pathlib import Path

repo = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "scripts/render_metric_util.py").is_file())
sys.path.insert(0, str(repo / "scripts"))
from render_metric_util import read_png  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", type=Path)
parser.add_argument("--output", type=Path, help="Optional JSON result path")
args = parser.parse_args()
path = args.image
w, h, bpp, pixels = read_png(str(path))
if (w, h) != (2560, 1440):
    parser.error("fixture requires a 2560×1440 screenshot")
c = math.sqrt(.5)
occupied = {(x, y, z) for z in range(-10, 11) for y in range(-5, 7)
            for x in range(-10, 11)
            if -5 <= math.floor(c*x - c*z + .5) <= 6
            and -5 <= math.floor(c*x + c*z + .5) <= 6}
if len(occupied) != 1740:
    raise ValueError("orbit-6 inverse resample changed covered-cell count")
surface = {p for p in occupied if any(
    tuple(p[i] + d*(i == a) for i in range(3)) not in occupied
    for a in range(3) for d in (-1, 1))}
if len(surface) != 610:
    raise ValueError("orbit-6 inverse resample changed surface-cell count")
cross_section = {(a[0], a[2]) for a in occupied if a[1] == 0}

def pixel(x, y):
    i = (y*w + x)*bpp
    return tuple(pixels[i:i+3])

def ray(x, y):
    u = (x + .5 - w/2)/16
    v = (y + .5 - h/2)/8
    vx, vy, vz = -u/2-v/6, u/2-v/6, v/3
    oy = c*(vx-vy)
    ox = -c*(vx+vy)
    oz = vz
    dx = -math.sqrt(2)
    cy = math.floor(oy + .5)
    if cy < -5 or cy > 6:
        return None
    hits = []
    for cx, zz in cross_section:
        lo_x, hi_x = (cx+.5-ox)/dx, (cx-.5-ox)/dx
        lo_z, hi_z = zz-.5-oz, zz+.5-oz
        near, far = max(lo_x, lo_z), min(hi_x, hi_z)
        if near < far - 1e-8:
            axis = 'x' if lo_x > lo_z else 'z'
            hits.append((near, axis, cx, zz, abs(lo_x-lo_z)))
    return min(hits) if hits else None

palette = {'x': (255, 128, 128), 'z': (128, 128, 0)}
report = {'image': path.name, 'size': [w,h], 'occupancy': len(occupied),
          'surface': len(surface), 'columns': {}}
for x in (1170, 1230, 1280, 1330, 1360, 1390):
    counts = Counter()
    mismatches = []
    runs = []
    prev = None
    for y in range(570, 900):
        hit = ray(x, y)
        expected = hit[1] if hit else None
        observed = pixel(x, y)
        observed_label = None if observed == (0,0,0) else min(
            palette, key=lambda a: sum((observed[i]-palette[a][i])**2 for i in range(3)))
        if observed == (0,0,0):
            observed_label = None
        match = expected == observed_label and (expected is None or
            max(abs(observed[i]-palette[expected][i]) for i in range(3)) <= 1)
        counts['expected_'+str(expected)] += 1
        counts['observed_'+str(observed_label)] += 1
        if not match:
            counts['mismatch'] += 1
            if len(mismatches) < 30:
                mismatches.append(dict(y=y, expected=expected, observed=observed,
                                       hit=hit))
        state = (expected, observed)
        if state != prev:
            runs.append([y, expected, observed])
            prev = state
    report['columns'][str(x)] = dict(counts=dict(counts), mismatches=mismatches, runs=runs)
print('columns', {x: r['counts'] for x,r in report['columns'].items()})

# At camera yaw 135 degrees, the camera ray has exactly zero world-Y travel.
# The GRID cube is a Y extrusion, so one cross-section ray result per image row
# applies to every interior X column. This gives a full image-window gate at
# one-pixel face/silhouette boundary tolerance, not an observed-ROI alignment.
x0, x1, y0, y1 = 1128, 1410, 586, 878
row_labels = {y: (ray(1280,y)[1] if ray(1280,y) else None) for y in range(y0-1,y1+1)}
def expected_label(x,y):
    u = (x + .5 - w/2)/16
    wy = -u/math.sqrt(2)
    return row_labels[y] if -5.5 < wy < 6.5 else None

counts = Counter()
examples = []
for y in range(y0,y1):
    for x in range(x0,x1):
        e = expected_label(x,y)
        rgb = pixel(x,y)
        o = None if rgb == (0,0,0) else next((k for k,v in palette.items() if rgb==v),'other')
        counts['expected_'+str(e)] += 1
        counts['observed_'+str(o)] += 1
        if e == o:
            continue
        # Exclude one native screenshot pixel around expected face boundaries.
        interior = all(expected_label(xx,yy)==e for yy in (y-1,y,y+1)
                       for xx in (x-1,x,x+1))
        if interior:
            counts['interior_mismatch'] += 1
            counts['interior_'+str(e)+'_to_'+str(o)] += 1
            if len(examples)<12:
                examples.append((x,y,e,o))
        else:
            counts['boundary_mismatch'] += 1
report['full_window'] = dict(bounds=[x0,x1,y0,y1], counts=dict(counts), examples=examples,
                             boundary_tolerance_pixels=1)
face_owner_errors = sum(value for key, value in counts.items()
                        if key.startswith(('interior_x_to_', 'interior_z_to_')))
report['full_window']['face_owner_errors'] = face_owner_errors
report['full_window']['face_owners_pass'] = face_owner_errors == 0
if args.output:
    args.output.write_text(json.dumps(report, indent=2) + "\n")
print('full_window', report['full_window'])
