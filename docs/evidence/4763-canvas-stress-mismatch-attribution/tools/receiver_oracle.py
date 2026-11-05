#!/usr/bin/env python3
"""CPU oracle for CanvasStress cardinal-receiver sun visibility.

usage: receiver_oracle.py --dump DIR --shot N --ref REF.png --cap CAP.png
                          [--master MASTER.png] [--json OUT]

DIR holds one IRCanvasStress run's `--cardinal-receiver-probe` and
`--sun-face-geometry-probe` output; shot N is the frame both probes wrote for
the screenshot. For every framebuffer pixel whose colour differs between REF and
CAP, the oracle rebuilds the stored cardinal face from the canvas texel the
gather reads and evaluates the exact finite sun query (a double-precision port of
`sourceFaceRaySeparation` and its two containment predicates) at three receiver
points:

  centre   the stored micro-square centre (`cardinalVoxelSurfaceCenter`), the
           point the references were rendered at;
  ray      the fragment ray / face plane intersection, solved directly from the
           iso projection equations rather than the shader's helper path;
  limited  the ray point limited to the micro-square at every edge whose
           neighbour texel is not a coplanar continuation (the head rule).

A pixel is graded by the CPU answer at each point against the GPU answer (lit or
shadowed, read from which frame is the darker). `marginal` counts pixels whose
CPU answer flips when every face is grown or shrunk by 1e-3 in its own
parameters or the depth window moves by 1e-3 voxels.
"""
import argparse
import importlib.util
import json
import math
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
SHADOW_BIAS = 4.0 / 1024.0
MARGIN = 1e-3
CARDINAL_VOXEL_MASK = 1 << 18
# faceOffset_2x3: the two canvas texels of a micro-square, by slot (X, Y, Z).
FACE_OFFSET = {0: ((1, 1), (1, 2)), 1: ((0, 1), (0, 2)), 2: ((0, 0), (1, 0))}
# Iso step of each unit view axis.
AXIS_ISO = {0: (-1, -1), 1: (1, -1), 2: (0, 2)}
# faceInPlaneUnitAxes: in-plane view axes per slot.
IN_PLANE = {0: (1, 2), 1: (0, 2), 2: (0, 1)}


def load_png_reader():
    spec = importlib.util.spec_from_file_location(
        "render_compare", REPO / "scripts" / "render-compare.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.read_png


class Image:
    def __init__(self, read_png, path):
        self.w, self.h, self.bpp, self.data = read_png(str(path))

    def rgb(self, x, y):
        i = (y * self.w + x) * self.bpp
        return tuple(self.data[i:i + 3])


class Canvas:
    def __init__(self, prefix):
        self.meta = json.loads(Path(f"{prefix}.json").read_text())
        self.w, self.h = (int(v) for v in self.meta["canvas_size"])
        count = self.w * self.h
        self.distances = struct.unpack(f"<{count}i", Path(f"{prefix}.distances").read_bytes())
        self.ids = struct.unpack(f"<{2 * count}I", Path(f"{prefix}.ids").read_bytes())
        self.colors = Path(f"{prefix}.colors").read_bytes()
        options = [int(v) for v in self.meta["voxel_render_options"]]
        self.mode = options[0]
        self.density = max(options[1], 1) if options[0] != 0 else 1
        z1 = [int(v) for v in self.meta["trixel_canvas_offset_z1"]]
        camera = [float(v) for v in self.meta["camera_trixel_offset"]]
        self.frame = (z1[0] + math.floor(camera[0] * self.density),
                      z1[1] + math.floor(camera[1] * self.density))
        if int(self.meta["cardinal_index"]) != 0 or int(self.meta["sample_layout"]) != 0:
            raise SystemExit("oracle covers cardinal 0, RECTANGULAR canvases only")

    def inside(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def encoded(self, x, y):
        return self.distances[y * self.w + x]

    def cardinal(self, x, y):
        return (self.ids[2 * (y * self.w + x) + 1] & CARDINAL_VOXEL_MASK) != 0

    def alpha(self, x, y):
        return self.colors[4 * (y * self.w + x) + 3]

    def screen_to_framebuffer(self, x, y):
        m = self.meta
        center_x = m["viewport"][0] * 0.5 + m["blit_offset"][0]
        center_y = m["viewport"][1] * 0.5 - m["blit_offset"][1]
        size, scale = m["framebuffer_size"], m["output_scale"]
        return (math.floor(size[0] * 0.5 + (x + 0.5 - center_x) / scale[0]),
                math.floor(size[1] * 0.5 + (y + 0.5 - center_y) / scale[1]))

    def framebuffer_to_canvas(self, fx, fy):
        m = self.meta
        c = m["canvas_from_pixel"]
        fragment_y = m["framebuffer_size"][1] - fy - 0.5
        return ((fx + 0.5 - c[0]) * c[2] + 0.5 * self.w,
                -(fragment_y - c[1]) * c[3] + 0.5 * self.h)

    def centre(self, x, y, encoded):
        slot, flip, depth = encoded & 3, (encoded >> 2) & 1, encoded >> 3
        iso_x, iso_y = x - self.frame[0], y - self.frame[1]
        half = ((iso_x + iso_y) & 1) ^ (1 if slot == 1 else 0)
        offset = FACE_OFFSET[slot][half]
        ox, oy, d = iso_x - offset[0], iso_y - offset[1], float(depth)
        px = (2.0 * d - 3.0 * ox - oy) / 6.0
        origin = (px, px + ox, (oy + 2.0 * px + ox) / 2.0)
        if self.mode == 0:
            sign = 0.5 if flip else -0.5
            return tuple(origin[k] + (sign if k == slot else 0.0) for k in range(3))
        return tuple((origin[k] + (0.0 if k == slot else 0.5)) / self.density - 0.5
                     for k in range(3))

    def ray_point(self, canvas_x, canvas_y, slot, plane):
        a = (canvas_x - self.frame[0] - 1.0) / self.density  # -v0 + v1
        b = (canvas_y - self.frame[1] - 1.0) / self.density  # -v0 - v1 + 2 v2
        if slot == 0:
            v0 = plane
            v1 = a + v0
            v2 = (b + v0 + v1) / 2.0
        elif slot == 1:
            v1 = plane
            v0 = v1 - a
            v2 = (b + v0 + v1) / 2.0
        else:
            v2 = plane
            v1 = (a + 2.0 * v2 - b) / 2.0
            v0 = v1 - a
        return (v0, v1, v2)

    def limited(self, x, y, encoded, centre, point):
        slot, flip, depth = encoded & 3, (encoded >> 2) & 1, encoded >> 3
        point = list(point)
        half_extent = 0.5 / self.density
        for axis in IN_PLANE[slot]:
            s = point[axis] - centre[axis]
            if abs(s) <= half_extent + 1e-4:
                continue
            direction = 1 if s > 0 else -1
            nx = x + AXIS_ISO[axis][0] * direction
            ny = y + AXIS_ISO[axis][1] * direction
            if self.inside(nx, ny):
                neighbour = self.encoded(nx, ny)
                if (neighbour & 3 == slot and (neighbour >> 2) & 1 == flip
                        and neighbour >> 3 == depth + direction and self.cardinal(nx, ny)):
                    continue
            point[axis] = centre[axis] + direction * half_extent
        return tuple(point)


def as_float(word):
    return struct.unpack("<f", struct.pack("<I", int(word)))[0]


class SunFaces:
    def __init__(self, path):
        meta, records = {}, []
        for line in Path(path).read_text().splitlines():
            if not line.startswith("# "):
                continue
            key, value = line[2:].split("=", 1)
            if key.startswith("geometry_record_") and key[16:].isdigit():
                words = [int(v) for v in value.split(",")]
                records.append(words)
            else:
                meta[key] = value
        def vector(key):
            return tuple(float(v) for v in meta[key].split(","))
        self.u, self.v = vector("basis_u"), vector("basis_v")
        self.sun = vector("sun_direction")
        self.max_throw = float(meta["max_shadow_throw"])
        self.shapes = sum(1 for words in records if words[9] != 0)
        self.faces = []
        for words in records:
            if words[9] != 0:
                continue
            f = [as_float(w) for w in words[:9]]
            corner, edge_u, edge_v = f[0:3], f[3:6], f[6:9]
            xs = (corner[0], corner[0] + edge_u[0], corner[0] + edge_v[0],
                  corner[0] + edge_u[0] + edge_v[0])
            ys = (corner[1], corner[1] + edge_u[1], corner[1] + edge_v[1],
                  corner[1] + edge_u[1] + edge_v[1])
            self.faces.append((corner, edge_u, edge_v,
                               min(xs) - 0.01, max(xs) + 0.01, min(ys) - 0.01, max(ys) + 0.01))

    def blocked(self, point, normal, grow=0.0):
        if sum(n * s for n, s in zip(normal, self.sun)) <= 0.0:
            return None
        su = sum(p * a for p, a in zip(point, self.u))
        sv = sum(p * a for p, a in zip(point, self.v))
        sz = -sum(p * a for p, a in zip(point, self.sun))
        for corner, eu, ev, x0, x1, y0, y1 in self.faces:
            if su < x0 or su > x1 or sv < y0 or sv > y1:
                continue
            det = eu[0] * ev[1] - eu[1] * ev[0]
            if abs(det) < 1e-6:
                continue
            dx, dy = su - corner[0], sv - corner[1]
            fu = (dx * ev[1] - dy * ev[0]) / det
            fv = (eu[0] * dy - eu[1] * dx) / det
            if not (-grow <= fu <= 1.0 + grow and -grow <= fv <= 1.0 + grow):
                continue
            cu, cv = min(max(fu, 0.0), 1.0), min(max(fv, 0.0), 1.0)
            separation = sz - (corner[2] + cu * eu[2] + cv * ev[2])
            depth_slack = MARGIN if grow > 0 else (-MARGIN if grow < 0 else 0.0)
            if SHADOW_BIAS - depth_slack < separation < self.max_throw + depth_slack:
                return True
        return False


def visibility(faces, point, normal):
    """(lit?, marginal?) or None for a face turned from the sun."""
    exact = faces.blocked(point, normal)
    if exact is None:
        return None
    grown, shrunk = faces.blocked(point, normal, MARGIN), faces.blocked(point, normal, -MARGIN)
    return (not exact, grown != shrunk)


def shadowed_fraction(faces, centre, slot, normal, density, steps=8):
    """Shadowed share of the micro-square, sampled on a steps x steps in-plane grid."""
    eu, ev = IN_PLANE[slot]
    blocked = 0
    for i in range(steps):
        for j in range(steps):
            point = list(centre)
            point[eu] += ((i + 0.5) / steps - 0.5) / density
            point[ev] += ((j + 0.5) / steps - 0.5) / density
            blocked += bool(faces.blocked(point, normal))
    return blocked / (steps * steps)


def fraction_bins(values):
    bins = {"0": 0, "(0,.25]": 0, "(.25,.5]": 0, "(.5,.75]": 0, "(.75,1)": 0, "1": 0}
    for value in values:
        if value == 0.0:
            bins["0"] += 1
        elif value <= 0.25:
            bins["(0,.25]"] += 1
        elif value <= 0.5:
            bins["(.25,.5]"] += 1
        elif value <= 0.75:
            bins["(.5,.75]"] += 1
        elif value < 1.0:
            bins["(.75,1)"] += 1
        else:
            bins["1"] += 1
    return bins


def changed_framebuffer_pixels(canvas, ref, cap):
    pixels = {}
    for y in range(ref.h):
        row = y * ref.w * ref.bpp
        if ref.data[row:row + ref.w * ref.bpp] == cap.data[row:row + cap.w * cap.bpp]:
            continue
        for x in range(ref.w):
            a, b = ref.rgb(x, y), cap.rgb(x, y)
            if a != b:
                key = canvas.screen_to_framebuffer(x, y)
                pixels.setdefault(key, ("darker" if sum(b) < sum(a) else "brighter", (x, y)))
    return pixels


def grade_speckles(canvas, faces, cap_path):
    """CPU answer at the head's receiver point for every edge-speckle pixel."""
    spec = importlib.util.spec_from_file_location(
        "speckle_metric", REPO / "scripts" / "render-shadow-speckle-metric.py")
    module = importlib.util.module_from_spec(spec)
    import sys
    sys.path.insert(0, str(REPO / "scripts"))
    spec.loader.exec_module(module)
    import render_metric_util as rmu
    result = module.measure(cap_path, None, tuple(rmu.DEFAULT_BG), 4, 1.8, sample_limit=None)
    tally = {"edge_speckles": result["edge_speckles"], "cpu_shadowed": 0, "cpu_lit": 0,
             "not_cardinal_voxel": 0, "marginal": 0}
    for x0, y0, x1, y1 in result["edge_speckle_samples"]:
        answers = set()
        for fx, fy in {canvas.screen_to_framebuffer(x, y)
                       for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)}:
            cx, cy = canvas.framebuffer_to_canvas(fx, fy)
            tx, ty = math.floor(cx), math.floor(cy)
            if not canvas.inside(tx, ty) or not canvas.cardinal(tx, ty):
                answers.add("not_cardinal_voxel")
                continue
            encoded = canvas.encoded(tx, ty)
            slot, flip = encoded & 3, (encoded >> 2) & 1
            normal = [0.0, 0.0, 0.0]
            normal[slot] = 1.0 if flip else -1.0
            centre = canvas.centre(tx, ty, encoded)
            point = canvas.limited(tx, ty, encoded, centre,
                                   canvas.ray_point(cx, cy, slot, centre[slot]))
            answer = visibility(faces, point, normal)
            if answer is None:
                answers.add("not_cardinal_voxel")
                continue
            answers.add("cpu_lit" if answer[0] else "cpu_shadowed")
            if answer[1]:
                tally["marginal"] += 1
        for key in ("not_cardinal_voxel", "cpu_lit", "cpu_shadowed"):
            if key in answers:
                tally[key] += 1
                break
    return tally


def mapping_check(canvas, cap):
    background = cap.rgb(0, 0)
    covered_background = 0
    covered = 0
    for y in range(0, cap.h, 2):
        for x in range(0, cap.w, 2):
            fx, fy = canvas.screen_to_framebuffer(x, y)
            cx, cy = canvas.framebuffer_to_canvas(fx, fy)
            tx, ty = math.floor(cx), math.floor(cy)
            if not canvas.inside(tx, ty) or canvas.alpha(tx, ty) < 26:
                continue
            covered += 1
            covered_background += cap.rgb(x, y) == background
    return {"canvas_covered_samples": covered, "covered_but_background": covered_background}


def components(pixels):
    """8-connected framebuffer-pixel components per class."""
    seen, result = set(), []
    for start, (kind, _) in pixels.items():
        if start in seen:
            continue
        stack, members = [start], []
        seen.add(start)
        while stack:
            p = stack.pop()
            members.append(p)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (p[0] + dx, p[1] + dy)
                    if q in pixels and q not in seen and pixels[q][0] == kind:
                        seen.add(q)
                        stack.append(q)
        result.append((kind, members))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--dump", required=True, type=Path)
    ap.add_argument("--shot", required=True, type=int)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--cap", required=True)
    ap.add_argument("--master")
    ap.add_argument("--json")
    ap.add_argument("--speckles", action="store_true",
                    help="also grade every edge-speckle pixel of CAP")
    o = ap.parse_args()
    read_png = load_png_reader()
    canvas = Canvas(o.dump / f"cardinal-receiver-{o.shot}")
    faces = SunFaces(o.dump / f"sun-face-index-{o.shot}.csv")
    ref, cap = Image(read_png, o.ref), Image(read_png, o.cap)
    master = Image(read_png, o.master) if o.master else None
    pixels = changed_framebuffer_pixels(canvas, ref, cap)
    graded, fractions = {}, {}
    for (fx, fy), (kind, (sx, sy)) in pixels.items():
        cx, cy = canvas.framebuffer_to_canvas(fx, fy)
        tx, ty = math.floor(cx), math.floor(cy)
        if not canvas.inside(tx, ty) or not canvas.cardinal(tx, ty):
            graded[(fx, fy)] = {"kind": kind, "status": "not_cardinal_voxel"}
            continue
        encoded = canvas.encoded(tx, ty)
        slot, flip = encoded & 3, (encoded >> 2) & 1
        normal = [0.0, 0.0, 0.0]
        normal[slot] = 1.0 if flip else -1.0
        centre = canvas.centre(tx, ty, encoded)
        ray = canvas.ray_point(cx, cy, slot, centre[slot])
        limited = canvas.limited(tx, ty, encoded, centre, ray)
        row = {"kind": kind, "status": "graded", "texel": [tx, ty], "slot": slot,
               "ref_lit": kind == "darker", "cap_lit": kind == "brighter"}
        if master is not None:
            m, r = master.rgb(sx, sy), ref.rgb(sx, sy)
            row["master_lit"] = row["ref_lit"] if m == r else sum(m) > sum(r)
        for name, point in (("centre", centre), ("ray", ray), ("limited", limited)):
            answer = visibility(faces, point, normal)
            if answer is None:
                row["status"] = "sun_facing_away"
                break
            row[f"{name}_lit"], row[f"{name}_marginal"] = answer
        row["edge_limited"] = limited != ray
        if row["status"] == "graded":
            half_extent = 0.5 / canvas.density
            row["offset"] = [(ray[a] - centre[a]) / half_extent for a in IN_PLANE[slot]]
            key = tuple(round(c, 6) for c in centre)
            if key not in fractions:
                fractions[key] = shadowed_fraction(faces, centre, slot, normal, canvas.density)
            row["shadowed_fraction"] = fractions[key]
        graded[(fx, fy)] = row
    report = {"shot": o.shot, "changed_framebuffer_pixels": len(pixels),
              "shape_records": faces.shapes, "face_records": len(faces.faces),
              "mapping": mapping_check(canvas, cap), "classes": {}, "components": []}
    for kind in ("darker", "brighter"):
        rows = [r for r in graded.values() if r["kind"] == kind]
        ok = [r for r in rows if r["status"] == "graded"]
        tally = {"pixels": len(rows), "graded": len(ok),
                 "not_graded": {s: sum(1 for r in rows if r["status"] == s)
                                for s in ("not_cardinal_voxel", "sun_facing_away")},
                 "ref_agrees_cpu_centre": sum(r["ref_lit"] == r["centre_lit"] for r in ok),
                 "cap_agrees_cpu_limited": sum(r["cap_lit"] == r["limited_lit"] for r in ok),
                 "cap_agrees_cpu_ray": sum(r["cap_lit"] == r["ray_lit"] for r in ok),
                 "cpu_centre_lit": sum(r["centre_lit"] for r in ok),
                 "cpu_limited_lit": sum(r["limited_lit"] for r in ok),
                 "edge_limited": sum(r["edge_limited"] for r in ok),
                 "marginal": sum(r["centre_marginal"] or r["limited_marginal"] for r in ok)}
        tally["ray_inside_micro_square"] = sum(
            max(abs(v) for v in r["offset"]) <= 1.0 + 1e-6 for r in ok)
        tally["max_ray_offset"] = round(max((max(abs(v) for v in r["offset"]) for r in ok),
                                            default=0.0), 4)
        tally["shadowed_fraction_bins"] = fraction_bins(r["shadowed_fraction"] for r in ok)
        if master is not None:
            tally["master_agrees_cpu_ray"] = sum(r["master_lit"] == r["ray_lit"] for r in ok)
        report["classes"][kind] = tally
    for kind, members in components(pixels):
        rows = [graded[p] for p in members if graded[p]["status"] == "graded"]
        report["components"].append({
            "kind": kind, "pixels": len(members), "graded": len(rows),
            "bbox_framebuffer": [min(p[0] for p in members), min(p[1] for p in members),
                                 max(p[0] for p in members), max(p[1] for p in members)],
            "cpu_centre_lit": sum(r["centre_lit"] for r in rows),
            "cpu_limited_lit": sum(r["limited_lit"] for r in rows),
            "cap_agrees_cpu_limited": sum(r["cap_lit"] == r["limited_lit"] for r in rows),
            "ref_agrees_cpu_centre": sum(r["ref_lit"] == r["centre_lit"] for r in rows),
            "marginal": sum(r["centre_marginal"] or r["limited_marginal"] for r in rows)})
    report["components"].sort(key=lambda c: (c["kind"], -c["pixels"]))
    if o.speckles:
        report["edge_speckles"] = grade_speckles(canvas, faces, o.cap)
    text = json.dumps(report, indent=1)
    if o.json:
        Path(o.json).write_text(text + "\n")
    print(json.dumps({k: report[k] for k in ("shot", "changed_framebuffer_pixels", "shape_records",
                                             "face_records", "mapping", "classes",
                                             "edge_speckles") if k in report}, indent=1))


if __name__ == "__main__":
    main()
