#!/usr/bin/env python3
"""Capture raster-only sun-depth controls for parent, head and second-depth variants.

Run under ir-acquire gpu from a checkout whose build contains IRCanvasStress
with --probe-ray-casters. Every case registers the stateless particle pass, so
BAKE_SUN_SHADOW_MAP takes the raster-only primary bake. The parent pass stages
origin/master's bake and sampler shaders (Metal on macOS, GLSL elsewhere) into
the build's runtime shader directory; the head pass restores this branch's
copies, and the second pass adds --sun-second-depth to the same binary.

Static cases grade one receiver crop against a lower-only control. Tracks move
the ray casters every frame and grade each shot against a static capture of
the pose that shot logged. Probe lines (primary texel words, caster pixel
census) are retained per shot.
"""

import argparse
import gzip
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

SUFFIX, SUBDIR = ("metal", "metal/") if sys.platform == "darwin" else ("glsl", "")
POLICY_SHADERS = tuple(f"{name}.{SUFFIX}" for name in (
    "c_bake_sun_shadow_map", "ir_sun_shadow_sample"))
# The head bake is a wrapper over a shared body; the second-depth wrapper is
# the opt-in variant. Hashed alongside the staged files.
HASHED_SHADERS = POLICY_SHADERS + tuple(f"{name}.{SUFFIX}" for name in (
    "c_bake_sun_shadow_map_body", "c_bake_sun_shadow_map_second_depth"))
# Receiver crops (framebuffer pixels) per receiver placement; see README.
CROPS = {"near": (1100, 600, 1460, 840), "far": (1100, 760, 1460, 1000)}
BAND = 2
DOWN, OBLIQUE = (0, 0, -1), (0.3, 0.4, -1)
# Shadow throw is 64 units. The lower box spans 4 units above its centre, so a
# caster 72 units up the ray is beyond the throw from the box top as well as
# from the floor, and the lower box stays lit in every stacked case.
INSIDE, BEYOND, FAR = 40, 72, 92
COMMON = ["--only", "shadowbox,floor", "--probe-grid", "--probe-floor-mode", "grid",
          "--probe-floor-span", "120", "--no-spin", "--no-auto-rotate", "--pivot-origin",
          "--yaw", "0", "--zoom", "1", "--no-ao", "--subdivisions", "1",
          "--auto-screenshot", "10", "--probe-particle-raster", "--sun-primary-probe"]
# The far placement moves the fixture so the floor receiver's iso depth (-78)
# lies below the cascade split (-51.2) by more than the blend range: cascade 0.
PLACEMENTS = {"near": [], "far": ["--probe-box-offset", "-40", "-40", "0"]}
TRACK_SPEED, TRACK_SHOTS, TRACK_SETTLE = 60, 7, 8
LOCK_ATTEMPTS = 6
DRIFT_RE = re.compile(r"PROBE-DRIFT shot=(\d+) offset=\(([^,]+),([^,]+),([^)]+)\)")
PROBE_RE = re.compile(r"(SUN-PRIMARY-PROBE|SUN-PRIMARY-TEXEL|SUN-CASTER-CENSUS) shot=(\d+) (.*)")


def case(name, sun, casters=(), lower=True, shadows=True, first=False, control=None,
         placement="near", velocity=None, shots=1):
    prefix = {"near": "", "far": "far-"}[placement] + ("" if sun == DOWN else "oblique-")
    return {"name": prefix + name, "sun": sun, "casters": list(casters), "lower": lower,
            "shadows": shadows, "first": first, "placement": placement,
            "control": None if control is None else prefix + control,
            "velocity": velocity, "shots": shots}


def matrix(sun, placement):
    cases = [
        case("lower", sun, placement=placement),
        case("unlit-lower", sun, shadows=False, placement=placement),
        case("unlit-stacked", sun, [BEYOND], shadows=False, control="unlit-lower",
             placement=placement),
        case("distant", sun, [BEYOND], lower=False, control="lower", placement=placement),
        case("inside", sun, [INSIDE], control="lower", placement=placement),
        case("stacked", sun, [BEYOND], control="lower", placement=placement),
        case("stacked-first", sun, [BEYOND], first=True, control="lower", placement=placement),
        case("stacked-dup", sun, [BEYOND, BEYOND], control="lower", placement=placement),
        case("stacked-3", sun, [BEYOND, FAR], control="lower", placement=placement),
    ]
    return cases


CASES = matrix(DOWN, "near") + matrix(OBLIQUE, "near") + matrix(DOWN, "far")
# The lead caster starts 10 units up the ray and climbs one unit per 60 Hz
# tick (one tick per captured frame): shots land at 29, 39, 49, ... units, and
# the floor's 64-unit throw (plus bias) falls between shots 1 and 2.


def track(sun):
    """Moving lead caster along the unit sun ray at TRACK_SPEED units per second."""
    norm = sum(c * c for c in sun) ** 0.5
    velocity = tuple(round(c / norm * TRACK_SPEED, 4) for c in sun)
    return case("moving", sun, [10], velocity=velocity, shots=TRACK_SHOTS)


TRACKS = [track(DOWN), track(OBLIQUE)]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def stage_parent(shader_dir):
    saved = {}
    for name in POLICY_SHADERS:
        path = shader_dir / name
        saved[name] = path.read_bytes()
        path.write_bytes(subprocess.check_output(
            ["git", "show", f"origin/master:engine/render/src/shaders/{SUBDIR}{name}"], cwd=ROOT))
    return saved


def restore(shader_dir, saved):
    for name, data in saved.items():
        (shader_dir / name).write_bytes(data)


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


def command_for(build, config, spec, second):
    command = ["fleet-run", "--timeout", "180", "--build-dir", str(build), "IRCanvasStress",
               "--config-preset", str(config), *COMMON, *PLACEMENTS[spec["placement"]],
               "--sweep-frames", str(spec["shots"]),
               str(TRACK_SETTLE if spec["velocity"] else 10),
               "--sun-direction", *map(str, spec["sun"])]
    if spec["casters"]:
        casters = spec["casters"]
        step = casters[1] - casters[0] if len(casters) > 1 else 0
        command += ["--probe-ray-casters", str(len(casters)), str(casters[0]), str(step)]
    if spec["first"]:
        command.append("--probe-ray-casters-first")
    if not spec["lower"]:
        command.append("--probe-hidden-box")
    if not spec["shadows"]:
        command.append("--no-shadows")
    if spec["velocity"]:
        command += ["--probe-ray-casters-velocity", *map(str, spec["velocity"])]
    if second:
        command.append("--sun-second-depth")
    return command


def capture(build, variant_dir, spec, second):
    directory = variant_dir / spec["name"]
    directory.mkdir(parents=True)
    config = directory / "config.lua"
    config.write_text('config = {window_mode="offscreen", profiling_enabled=false, '
                      'gpu_stage_timing=false, init_window_width=1280, init_window_height=720, '
                      'game_resolution_width=1280, game_resolution_height=720, '
                      f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
    command = command_for(build, config, spec, second)
    count = spec["shots"]
    for _ in range(LOCK_ATTEMPTS):
        with (directory / "run.log").open("w") as stream:
            result = subprocess.run(platform_launch_argv(command), cwd=ROOT,
                                    stdout=stream, stderr=subprocess.STDOUT)
        log = (directory / "run.log").read_text(errors="replace")
        if run_result(log) != "LOCK-FAILED":
            break
    frames = collect_full_frames(directory)
    probes = {}
    for match in PROBE_RE.finditer(log):
        probes.setdefault(int(match[2]), []).append(f"{match[1]} {match[3]}")
    if (result.returncode or run_result(log) != "CLEAN" or len(frames) != count
            or any("SUN-CASTER-CENSUS" not in " ".join(probes.get(shot, []))
                   for shot in range(count))):
        raise RuntimeError(f"{spec['name']}: missing clean capture/probe witness")
    poses = {int(m[1]): [float(m[2]), float(m[3]), float(m[4])]
             for m in DRIFT_RE.finditer(log)}
    if spec["velocity"] and sorted(poses) != list(range(count)):
        raise RuntimeError(f"{spec['name']}: missing PROBE-DRIFT pose per shot")
    with gzip.open(directory / "run.log.gz", "wb") as dest, (directory / "run.log").open("rb") as src:
        shutil.copyfileobj(src, dest)
    (directory / "run.log").unlink()
    shots = []
    for shot, frame in enumerate(frames):
        suffix = f"-{shot}" if count > 1 else ""
        receiver = directory / f"receiver{suffix}.png"
        with Image.open(frame) as image:
            native = image.size
            image.crop(CROPS[spec["placement"]]).save(receiver)
        shots.append({"frame": frame.name, "frame_sha256": sha256(frame), "native": native,
                      "receiver": receiver.name, "receiver_sha256": sha256(receiver),
                      "pose": poses.get(shot), "probe": probes.get(shot, [])})
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
    parser.add_argument("--variants", default="parent,head,second")
    parser.add_argument("--cases", default="", help="comma-separated case names (default all)")
    parser.add_argument("--keep-frames", action="store_true",
                        help="retain every full frame (default: only cases with a changed crop)")
    parser.add_argument("--grade-only", action="store_true",
                        help="re-grade an existing --output without capturing")
    args = parser.parse_args()
    output = args.output.resolve()
    if args.grade_only:
        report = json.loads((output / "captures.json").read_text())
        grade_report(report, output, args.keep_frames)
        return
    if os.environ.get("IR_ACQUIRE_HELD_VERB") not in {"gpu", "benchmark"}:
        parser.error("run under ir-acquire gpu -- to protect the shared capture file")
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    binary = find_exe(build, "IRCanvasStress", "canvas_stress")
    shader_dir = binary.parent / "shaders" / SUBDIR
    selected = set(filter(None, args.cases.split(",")))
    cases = [c for c in CASES if not selected or c["name"] in selected]
    tracks = [c for c in TRACKS if not selected or c["name"] in selected]
    report = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             text=True).strip(),
              "parent": subprocess.check_output(["git", "rev-parse", "origin/master"],
                                               cwd=ROOT, text=True).strip(),
              "binary_sha256": sha256(binary), "crops": CROPS, "band_px": BAND,
              "variants": {}}
    (output / "source.patch").write_bytes(
        subprocess.check_output(["git", "diff", "origin/master", "--", "engine", "creations"],
                                cwd=ROOT))
    for variant in args.variants.split(","):
        saved = stage_parent(shader_dir) if variant == "parent" else {}
        try:
            report["variants"][variant] = {
                "shader_sha256": {name: sha256(shader_dir / name) for name in HASHED_SHADERS
                                  if (shader_dir / name).exists()},
                "cases": {}}
            entries = report["variants"][variant]["cases"]

            def run(spec):
                entries[spec["name"]] = capture(build, output / variant, spec,
                                                variant == "second")
                (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")
                print(f"{variant} {spec['name']}: clean", flush=True)

            for spec in cases:
                run(spec)
            for spec in tracks:
                run(spec)
                poses = [shot["pose"] for shot in entries[spec["name"]]["shots"]]
                for shot, pose in enumerate(poses):
                    name = f"{spec['name']}-pose-{shot}"
                    if name in entries:
                        continue
                    # The lead caster started 10 units up the ray and moved along it.
                    distance = 10 + sum(c * c for c in pose) ** 0.5
                    static = case("", spec["sun"], [distance], control="lower")
                    static["name"] = name
                    run(static)
        finally:
            restore(shader_dir, saved)
    grade_report(report, output, args.keep_frames)


def shots_of(entry):
    return entry["shots"] if isinstance(entry["shots"], list) else [entry]


def grade_report(report, output, keep_frames):
    for variant, data in report["variants"].items():
        for name, entry in data["cases"].items():
            directory = output / variant / name
            others = {v: d["cases"].get(name) for v, d in report["variants"].items() if v != variant}
            if entry["velocity"] is None:
                if entry["control"]:
                    control = output / variant / entry["control"] / "receiver.png"
                    entry["versus_control"] = grade(directory / "receiver.png", control,
                                                    directory / "outside-band.png")
                entry["receiver_equals"] = {
                    v: o is not None and o["receiver_sha256"] == entry["receiver_sha256"]
                    for v, o in others.items()}
                entry["frame_equals"] = {
                    v: o is not None and o["frame_sha256"] == entry["frame_sha256"]
                    for v, o in others.items()}
                continue
            for shot, result in enumerate(entry["shots"]):
                control = output / variant / f"{name}-pose-{shot}" / "receiver.png"
                result["control"] = f"{name}-pose-{shot}"
                result["versus_control"] = grade(directory / result["receiver"], control,
                                                 directory / f"outside-band-{shot}.png")
    (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")
    if keep_frames:
        return
    for variant, data in report["variants"].items():
        for name, entry in data["cases"].items():
            for shot in shots_of(entry):
                keep = shot.get("versus_control", {}).get("changed_outside_band", 1) != 0
                frame = output / variant / name / shot["frame"]
                if not keep and frame.exists():
                    frame.unlink()


if __name__ == "__main__":
    main()
