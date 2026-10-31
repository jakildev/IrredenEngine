#!/usr/bin/env python3
"""Capture raster-only sun-depth collision controls on the legacy canvas bake.

Run under ir-acquire gpu from a checkout whose build contains IRCanvasStress
with --probe-raster-sun-route and --sun-primary-probe. Each case renders the
GRID floor receiver under a lower caster, a distant caster stack beyond the
64-unit throw, or both, and grades the receiver crop against its lower-only
control. The primary-map probe retains the nearest word and the slab extrema
under the receiver, so a case also witnesses which depths reached the bake.

--build-dir selects the binary; a baseline binary lacks the route flag and
runs the same scenes through --legacy-depth-shadows, which takes the same
canvas-depth bake. --route picks that for a head binary too.
"""

import argparse
import collections
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from verify_common import (  # noqa: E402
    collect_full_frames,
    find_exe,
    platform_launch_argv,
    run_result,
)

CROP = (1100, 600, 1460, 840)
BAND = 2
DOWN, OBLIQUE = (0, 0, -1), (0.3, 0.4, -1)
PROBE_RECT = ["-12", "-6", "12", "6"]
COMMON = ["--only", "shadowbox,floor", "--probe-floor-mode", "grid", "--probe-floor-span", "40",
          "--no-spin", "--no-auto-rotate", "--pivot-origin", "--yaw", "0", "--zoom", "1",
          "--no-ao", "--auto-screenshot", "10", "--subdivisions", "1"]
ROUTES = {"particles": ["--probe-raster-sun-route"], "legacy-flag": ["--legacy-depth-shadows"]}
# Casters move toward the sun at 20 units/s: 2.667 units per 8-frame shot
# interval, crossing the throw boundary between shots 2 and 4.
TRACK_VELOCITY, TRACK_SHOTS, TRACK_SETTLE = (0, 0, -20), 7, 8
LOCK_ATTEMPTS = 6
EMPTY = 0xFFFFFFFF
DRIFT_RE = re.compile(r"PROBE-DRIFT shot=(\d+) offset=\(([^,]+),([^,]+),([^)]+)\)")
ROUTE_RE = re.compile(r"SUN-ROUTE shot=(\d+) finite_coverage=(\d) particle_pass=(\d)")


def case(name, sun, casters=0, height=-60, mode="grid", floor="grid", lower=True, lit=True,
         order=None, layers=1, layer_step=0.0, control=None, offset=None):
    """One static capture; `control` names the lower-only scene it is graded against."""
    # Toward the sun: the oblique direction (0.3, 0.4, -1) scaled by -height.
    along = [-0.3 * height, -0.4 * height, height] if sun == OBLIQUE else [0, 0, height]
    if control is None and lit:
        control = f"{'oblique-' if sun == OBLIQUE else ''}lower{'' if floor == 'grid' else '-' + floor}"
        if mode != "grid":
            control += f"-{mode}"
    return {"name": name, "sun": sun, "casters": casters, "height": height, "mode": mode,
            "floor": floor, "lower": lower, "lit": lit, "order": order, "layers": layers,
            "layer_step": layer_step, "control": control,
            "offset": offset if offset is not None else (along if casters else None),
            "velocity": None, "shots": 1}


def track(name, sun, start=-30, mode="grid"):
    spec = case(name, sun, 8, start, mode=mode)
    spec.update(control=None, velocity=TRACK_VELOCITY, shots=TRACK_SHOTS,
                poses=f"{name}-pose")
    return spec


def matrix():
    cases = []
    for prefix, sun in (("", DOWN), ("oblique-", OBLIQUE)):
        cases += [
            case(f"{prefix}lower", sun),
            case(f"{prefix}lower-unlit", sun, lit=False),
            case(f"{prefix}floor", sun, lower=False),
            case(f"{prefix}distant-8-h60", sun, 8, lower=False, control=f"{prefix}floor"),
            case(f"{prefix}stacked-8-h60", sun, 8),
            case(f"{prefix}stacked-8-h60-unlit", sun, 8, lit=False),
            case(f"{prefix}stacked-8-h60-last", sun, 8, order="last"),
            case(f"{prefix}stacked-128-h60", sun, 128),
            case(f"{prefix}stacked-8-h90", sun, 8, -90),
            case(f"{prefix}stacked-8-h30", sun, 8, -30),
            case(f"{prefix}layered-2-h60-step40", sun, 8, -60, layers=2, layer_step=40.0),
            case(f"{prefix}layered-3-h60-step64", sun, 8, -60, layers=3, layer_step=64.0),
            case(f"{prefix}layered-4-h60-step64", sun, 8, -60, layers=4, layer_step=64.0),
        ]
    cases += [
        case("lower-revoxelize", DOWN, mode="revoxelize"),
        case("stacked-8-h60-revoxelize", DOWN, 8, mode="revoxelize"),
        case("lower-revoxelized", DOWN, floor="revoxelized"),
        case("stacked-8-h60-revoxelized", DOWN, 8, floor="revoxelized"),
    ]
    tracks = [track("moving-8", DOWN), track("oblique-moving-8", OBLIQUE)]
    return cases, tracks


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def grade(actual, control, output):
    """Changed receiver pixels, split by direction, outside a band around control edges."""
    with Image.open(actual) as a, Image.open(control) as c:
        a, c = a.convert("L"), c.convert("L")
        edges = c.filter(ImageFilter.FIND_EDGES).point(lambda v: 255 if v else 0)
        band = edges.filter(ImageFilter.MaxFilter(2 * BAND + 1))
        diff = ImageChops.difference(a, c).point(lambda v: 255 if v else 0)
        darker = ImageChops.subtract(c, a).point(lambda v: 255 if v else 0)
        brighter = ImageChops.subtract(a, c).point(lambda v: 255 if v else 0)
        outside = ImageChops.subtract(diff, band)
        if outside.getbbox():
            outside.save(output)

        def count(image):
            return sum(1 for v in image.tobytes() if v)
        return {"changed": count(diff), "changed_outside_band": count(outside),
                "excess_shadow_outside_band": count(ImageChops.subtract(darker, band)),
                "missing_shadow_outside_band": count(ImageChops.subtract(brighter, band))}


def depth_of(word):
    return (word >> 8) / 1024.0 - 512.0


def summarize_probe(path):
    """Per-cascade slab occupancy under the receiver: distinct depth bands per texel,
    overflow count, and the depth histogram of nearest words and slab extrema."""
    header, per_cascade = {}, {}
    with open(path) as stream:
        for row in csv.reader(stream):
            if not row:
                continue
            if row[0].startswith("#"):
                key, _, value = row[0][2:].partition("=")
                header[key] = ",".join([value] + row[1:])
                continue
            if row[0] == "cascade":
                slots = (len(row) - 5) // 2
                continue
            cascade = int(row[0])
            entry = per_cascade.setdefault(cascade, {
                "texels": 0, "overflow": 0, "slab_count_histogram": collections.Counter(),
                "nearest_depths": collections.Counter(), "slab_extrema": collections.Counter()})
            entry["texels"] += 1
            entry["overflow"] += int(row[-1])
            nearest = int(row[3])
            if nearest != EMPTY:
                entry["nearest_depths"][round(depth_of(nearest))] += 1
            slabs = 0
            for slot in range(slots):
                low, high = int(row[4 + 2 * slot]), int(row[5 + 2 * slot])
                if low == EMPTY:
                    continue
                slabs += 1
                entry["slab_extrema"][f"{(low >> 8) // 65536}:{round(depth_of(low))}..{round(depth_of(high))}"] += 1
            entry["slab_count_histogram"][slabs] += 1
    summary = {"header": header, "cascades": {}}
    for cascade, entry in sorted(per_cascade.items()):
        summary["cascades"][str(cascade)] = {
            "texels": entry["texels"], "overflow": entry["overflow"],
            "slab_count_histogram": dict(sorted(entry["slab_count_histogram"].items())),
            "nearest_depths": dict(sorted(entry["nearest_depths"].items())),
            "slab_extrema": dict(sorted(entry["slab_extrema"].items())),
        }
    return summary


def command_for(build, spec, route, config, probe):
    command = ["fleet-run", "--timeout", "240", "--build-dir", str(build), "IRCanvasStress",
               "--config-preset", str(config), *COMMON, *ROUTES[route],
               "--sweep-frames", str(spec["shots"]),
               str(TRACK_SETTLE if spec["velocity"] else 10),
               "--sun-direction", *map(str, spec["sun"])]
    if spec["floor"] != "grid":
        command[command.index("--probe-floor-mode") + 1] = spec["floor"]
    if spec["mode"] == "grid":
        command.append("--probe-grid")
    if not spec["lower"]:
        command.append("--probe-hidden-box")
    if not spec["lit"]:
        command.append("--no-shadows")
    if probe:
        command += ["--sun-primary-probe", *PROBE_RECT]
    if spec["casters"]:
        command += ["--probe-overlapping-source", "--probe-overlapping-source-mode", spec["mode"],
                    "--probe-overlapping-source-count", str(spec["casters"]),
                    "--probe-overlapping-source-offset", *map(str, spec["offset"])]
        if spec["layers"] > 1:
            command += ["--probe-overlapping-source-layers", str(spec["layers"]),
                        "--probe-overlapping-source-layer-step", str(spec["layer_step"])]
        if spec["order"] == "last":
            command.append("--probe-overlapping-source-last")
        if spec["velocity"]:
            command += ["--probe-overlapping-source-velocity", *map(str, spec["velocity"])]
    return command


def capture(binary, build, variant_dir, spec, route, probe):
    directory = variant_dir / spec["name"]
    directory.mkdir(parents=True)
    config = directory / "config.lua"
    config.write_text('config = {window_mode="offscreen", profiling_enabled=false, '
                      'gpu_stage_timing=false, init_window_width=1280, init_window_height=720, '
                      'game_resolution_width=1280, game_resolution_height=720, '
                      f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
    # The probe reads the bake system, which is inert with shadows off.
    probe = probe and spec["lit"]
    command = command_for(build, spec, route, config, probe)
    count = spec["shots"]
    csv_paths = [binary.parent / f"sun-primary-{shot}.csv" for shot in range(count)]
    for path in csv_paths:
        path.unlink(missing_ok=True)
    for _ in range(LOCK_ATTEMPTS):
        with (directory / "run.log").open("w") as stream:
            result = subprocess.run(platform_launch_argv(command), cwd=ROOT,
                                    stdout=stream, stderr=subprocess.STDOUT)
        log = (directory / "run.log").read_text(errors="replace")
        if run_result(log) != "LOCK-FAILED":
            break
    frames = collect_full_frames(directory)
    # A binary that predates the SUN-ROUTE line (the baseline) logs no route.
    routes = {int(m[1]): (int(m[2]), int(m[3])) for m in ROUTE_RE.finditer(log)}
    if (result.returncode or run_result(log) != "CLEAN" or len(frames) != count
            or (routes and sorted(routes) != list(range(count)))
            or (probe and any(f"sun-primary-{shot}.csv written=true" not in log
                              for shot in range(count)))):
        raise RuntimeError(f"{spec['name']}: missing clean capture, route or probe witness")
    if spec["lit"] and any(finite for finite, _ in routes.values()):
        raise RuntimeError(f"{spec['name']}: finite coverage stayed on; not the raster route")
    poses = {int(m[1]): [float(m[2]), float(m[3]), float(m[4])]
             for m in DRIFT_RE.finditer(log)}
    if spec["velocity"] and sorted(poses) != list(range(count)):
        raise RuntimeError(f"{spec['name']}: missing PROBE-DRIFT pose per shot")
    shots = []
    for shot, (frame, csv_path) in enumerate(zip(frames, csv_paths)):
        suffix = f"-{shot}" if count > 1 else ""
        receiver = directory / f"receiver{suffix}.png"
        with Image.open(frame) as image:
            image.crop(CROP).save(receiver)
        entry = {"frame": frame.name, "frame_sha256": sha256(frame), "receiver": receiver.name,
                 "receiver_sha256": sha256(receiver), "pose": poses.get(shot),
                 "route": ({"finite_coverage": routes[shot][0], "particle_pass": routes[shot][1]}
                           if shot in routes else None)}
        if probe:
            kept = directory / f"primary{suffix}.csv"
            shutil.copyfile(csv_path, kept)
            entry["probe"] = summarize_probe(kept)
        shots.append(entry)
    entry = {**spec, "command": command, "result": "CLEAN"}
    if count == 1:
        entry.update(shots[0])
    else:
        entry["shots"] = shots
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--label", default="head")
    parser.add_argument("--route", choices=sorted(ROUTES), default="particles")
    parser.add_argument("--no-probe", action="store_true",
                        help="skip --sun-primary-probe (a binary without it)")
    parser.add_argument("--cases", help="comma-separated case names; default all")
    parser.add_argument("--no-tracks", action="store_true")
    args = parser.parse_args()
    if os.environ.get("IR_ACQUIRE_HELD_VERB") not in {"gpu", "benchmark"}:
        parser.error("run under ir-acquire gpu -- to protect the shared capture file")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build = args.build_dir.resolve()
    binary = find_exe(build, "IRCanvasStress", "canvas_stress")
    cases, tracks = matrix()
    selected = set(args.cases.split(",")) if args.cases else None
    report_path = output / f"captures-{args.label}.json"
    report = {"label": args.label, "route": args.route, "build_dir": str(build),
              "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                              text=True).strip(),
              "binary_sha256": sha256(binary), "crop": CROP, "band_px": BAND, "cases": {}}
    variant_dir = output / args.label
    results = report["cases"]

    def run(spec):
        if selected and spec["name"] not in selected:
            return False
        results[spec["name"]] = capture(binary, build, variant_dir, spec, args.route,
                                        not args.no_probe)
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"{args.label} {spec['name']}: clean", flush=True)
        return True

    for spec in cases:
        run(spec)
    if not args.no_tracks:
        for spec in tracks:
            if not run(spec):
                continue
            poses = [shot["pose"] for shot in results[spec["name"]]["shots"]]
            for shot, pose in enumerate(poses):
                name = f"{spec['poses']}-{shot}"
                static = case(name, spec["sun"], spec["casters"], mode=spec["mode"], offset=pose)
                results[name] = capture(binary, build, variant_dir, static, args.route,
                                        not args.no_probe)
                report_path.write_text(json.dumps(report, indent=2) + "\n")
                print(f"{args.label} {name}: clean", flush=True)
    for name, entry in results.items():
        directory = variant_dir / name
        if entry["velocity"] is None:
            if entry["control"] and entry["control"] in results:
                control = variant_dir / entry["control"] / "receiver.png"
                entry["versus_control"] = grade(directory / "receiver.png", control,
                                                directory / "outside-band.png")
            continue
        for shot, result in enumerate(entry["shots"]):
            control_name = f"{entry['poses']}-{shot}"
            result["control"] = control_name
            if control_name in results:
                result["versus_control"] = grade(
                    directory / result["receiver"], variant_dir / control_name / "receiver.png",
                    directory / f"outside-band-{shot}.png")
    report_path.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
