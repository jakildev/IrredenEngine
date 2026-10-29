#!/usr/bin/env python3
"""Capture sun record-pool exhaustion controls for parent and head shaders.

Run under ir-acquire gpu from a checkout whose build contains IRCanvasStress
with --probe-record-pool-filler. The parent pass stages origin/master's four
sun-index shaders (Metal on macOS, GLSL elsewhere) into the build's runtime
shader directory and restores the head copies afterwards; the C++ binary is
shared because the policy lives in those shaders.

Static cases grade one receiver crop against a control scene. Tracks move the
caster stack every frame and grade each shot against a static capture of the
pose that shot logged. Analytic cases replace the lower box with a sphere at
subdivisions 2, where it indexes a typed analytic record.
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
from perf.sun_face_index_probe import summarize  # noqa: E402
from verify_common import (  # noqa: E402
    collect_full_frames,
    find_exe,
    platform_launch_argv,
    run_result,
)

SUFFIX, SUBDIR = ("metal", "metal/") if sys.platform == "darwin" else ("glsl", "")
POLICY_SHADERS = tuple(f"{name}.{SUFFIX}" for name in (
    "ir_sun_face_query_layout", "ir_sun_face_index", "c_build_sun_hierarchy",
    "ir_sun_shadow_sample"))
CROP = (1100, 600, 1460, 840)
BAND = 2
DOWN, OBLIQUE = (0, 0, -1), (0.3, 0.4, -1)
FILLER = ["--probe-record-pool-filler-offset", "200", "-200", "0"]
COMMON = ["--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
          "--probe-floor-span", "40", "--no-spin", "--no-auto-rotate", "--pivot-origin",
          "--yaw", "0", "--zoom", "1", "--no-ao", "--auto-screenshot", "10",
          "--sun-face-index-probe"]
# Analytic sun-ray records need subdivisions > 1; the sphere replaces the lower box.
ANALYTIC = ["--probe-analytic-sphere"]
# Tracks move the caster stack along +X at 120 units/s (2 per 60 Hz UPDATE tick).
VELOCITY, TRACK_SHOTS, TRACK_SETTLE = (120, 0, 0), 7, 8
# A LOCK-FAILED run never started the exe, so retrying it cannot bias the capture.
LOCK_ATTEMPTS = 6
DRIFT_RE = re.compile(r"PROBE-DRIFT shot=(\d+) offset=\(([^,]+),([^,]+),([^)]+)\)")


def case(name, sun, casters=0, height=-60, filler=0, order=None, control=None,
         analytic=False, offset=None):
    prefix = "analytic-" if analytic else ""
    control = control or prefix + ("lower" if sun == DOWN else "oblique-lower")
    if offset is None and casters:
        offset = [18, 24, height] if sun == OBLIQUE else [0, 0, height]
    return {"name": name, "sun": sun, "casters": casters, "height": height,
            "filler": filler, "order": order, "control": control, "analytic": analytic,
            "offset": offset, "velocity": None, "shots": 1}


def track(name, sun, filler=0, order=None):
    """Moving caster stack; each shot is graded against a static capture of its pose."""
    start = [-82, 24, -60] if sun == OBLIQUE else [-100, 0, -60]
    spec = case(name, sun, 128, filler=filler, order=order, offset=start)
    spec.update(control=None, velocity=VELOCITY, shots=TRACK_SHOTS,
                poses=("oblique-" if sun == OBLIQUE else "") + "moving-128-pose")
    return spec


CASES = [
    case("lower", DOWN),
    case("far-128", DOWN, 128),
    case("lower-filler-last", DOWN, filler=300, order="last"),
    case("lower-filler-first", DOWN, filler=300, order="first"),
    case("far-128-filler-last", DOWN, 128, filler=300, order="last"),
    case("far-128-filler-first", DOWN, 128, filler=300, order="first"),
    case("far-128-h30", DOWN, 128, -30),
    case("far-128-h30-filler-last", DOWN, 128, -30, 300, "last", "far-128-h30"),
    case("far-128-h90", DOWN, 128, -90),
    case("far-128-h90-filler-last", DOWN, 128, -90, 300, "last", "far-128-h90"),
    case("far-128-filler-first-270", DOWN, 128, filler=270, order="first"),
    case("far-128-filler-first-280", DOWN, 128, filler=280, order="first"),
    case("oblique-lower", OBLIQUE),
    case("oblique-aligned-128", OBLIQUE, 128),
    case("oblique-aligned-128-filler-last", OBLIQUE, 128, filler=340, order="last"),
    case("oblique-aligned-128-filler-first", OBLIQUE, 128, filler=340, order="first"),
    case("analytic-lower", DOWN, analytic=True),
    case("analytic-far-128", DOWN, 128, analytic=True),
    case("analytic-far-128-filler-last", DOWN, 128, filler=300, order="last", analytic=True),
    case("analytic-far-128-filler-first", DOWN, 128, filler=300, order="first", analytic=True),
    case("analytic-oblique-lower", OBLIQUE, analytic=True),
    case("analytic-oblique-aligned-128-filler-last", OBLIQUE, 128, filler=340, order="last",
         analytic=True),
]
# The filler-free track comes first: its logged poses define the static controls.
TRACKS = [
    track("moving-128", DOWN),
    track("moving-128-filler-last", DOWN, 300, "last"),
    track("moving-128-filler-first", DOWN, 300, "first"),
    track("oblique-moving-128", OBLIQUE),
    track("oblique-moving-128-filler-last", OBLIQUE, 340, "last"),
]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def stage_parent(shader_dir):
    saved = {}
    for name in POLICY_SHADERS:
        path = shader_dir / name
        saved[name] = path.read_bytes()
        source = subprocess.check_output(
            ["git", "show", f"origin/master:engine/render/src/shaders/{SUBDIR}{name}"], cwd=ROOT)
        path.write_bytes(source)
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


def capture(binary, build, variant_dir, spec):
    directory = variant_dir / spec["name"]
    directory.mkdir(parents=True)
    config = directory / "config.lua"
    config.write_text('config = {window_mode="offscreen", profiling_enabled=false, '
                      'gpu_stage_timing=false, init_window_width=1280, init_window_height=720, '
                      'game_resolution_width=1280, game_resolution_height=720, '
                      f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
    command = ["fleet-run", "--timeout", "180", "--build-dir", str(build), "IRCanvasStress",
               "--config-preset", str(config), *COMMON,
               "--subdivisions", "2" if spec["analytic"] else "1",
               "--sweep-frames", str(spec["shots"]),
               str(TRACK_SETTLE if spec["velocity"] else 10),
               "--sun-direction", *map(str, spec["sun"])]
    if spec["analytic"]:
        command += ANALYTIC
    if spec["casters"]:
        command += ["--probe-overlapping-source", "--probe-overlapping-source-count",
                    str(spec["casters"]), "--probe-overlapping-source-offset",
                    *map(str, spec["offset"])]
    if spec["velocity"]:
        command += ["--probe-overlapping-source-velocity", *map(str, spec["velocity"])]
    if spec["filler"]:
        command += ["--probe-record-pool-filler", str(spec["filler"]), *FILLER]
        if spec["order"] == "last":
            command.append("--probe-record-pool-filler-last")
    count = spec["shots"]
    csv_paths = [binary.parent / f"sun-face-index-{shot}.csv" for shot in range(count)]
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
    if (result.returncode or run_result(log) != "CLEAN" or len(frames) != count
            or any(f"sun-face-index-{shot}.csv written=true" not in log
                   for shot in range(count))):
        raise RuntimeError(f"{spec['name']}: missing clean capture/index witness")
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
        index = directory / f"index{suffix}.csv.gz"
        with csv_path.open("rb") as source, gzip.open(index, "wb") as dest:
            shutil.copyfileobj(source, dest)
        summary = summarize(index)
        shots.append({"frame": frame.name, "frame_sha256": sha256(frame),
                      "receiver": receiver.name, "receiver_sha256": sha256(receiver),
                      "pose": poses.get(shot),
                      "index": {key: summary[key] for key in summary if key != "cascades"},
                      "cascades": summary["cascades"]})
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
    args = parser.parse_args()
    if os.environ.get("IR_ACQUIRE_HELD_VERB") not in {"gpu", "benchmark"}:
        parser.error("run under ir-acquire gpu -- to protect the shared capture file")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    binary = find_exe(build, "IRCanvasStress", "canvas_stress")
    shader_dir = binary.parent / "shaders" / SUBDIR
    report = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             text=True).strip(),
              "parent": subprocess.check_output(["git", "rev-parse", "origin/master"],
                                               cwd=ROOT, text=True).strip(),
              "binary_sha256": sha256(binary), "crop": CROP, "band_px": BAND,
              "variants": {}}
    (output / "source.patch").write_bytes(
        subprocess.check_output(["git", "diff", "origin/master", "--", "engine", "creations"],
                                cwd=ROOT))
    for variant in ("parent", "head"):
        saved = stage_parent(shader_dir) if variant == "parent" else {}
        try:
            report["variants"][variant] = {
                "shader_sha256": {name: sha256(shader_dir / name) for name in POLICY_SHADERS},
                "cases": {}}
            cases = report["variants"][variant]["cases"]

            def run(spec):
                cases[spec["name"]] = capture(binary, build, output / variant, spec)
                (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")
                print(f"{variant} {spec['name']}: clean", flush=True)

            for spec in CASES:
                run(spec)
            for spec in TRACKS:
                run(spec)
                poses = [shot["pose"] for shot in cases[spec["name"]]["shots"]]
                pose_names = [f"{spec['poses']}-{shot}" for shot in range(len(poses))]
                if pose_names[0] not in cases:
                    for name, pose in zip(pose_names, poses):
                        run(case(name, spec["sun"], 128, offset=pose))
                elif poses != [cases[name]["offset"] for name in pose_names]:
                    raise RuntimeError(f"{spec['name']}: poses differ from its static controls")
        finally:
            restore(shader_dir, saved)
    for variant, data in report["variants"].items():
        other = report["variants"]["head" if variant == "parent" else "parent"]["cases"]
        for name, entry in data["cases"].items():
            directory = output / variant / name
            if entry["velocity"] is None:
                control = output / variant / entry["control"] / "receiver.png"
                entry["versus_control"] = grade(directory / "receiver.png", control,
                                                directory / "outside-band.png")
                entry["receiver_equals_other_variant"] = (
                    entry["receiver_sha256"] == other[name]["receiver_sha256"])
                continue
            for shot, result in enumerate(entry["shots"]):
                control = output / variant / f"{entry['poses']}-{shot}" / "receiver.png"
                result["control"] = f"{entry['poses']}-{shot}"
                result["versus_control"] = grade(directory / result["receiver"], control,
                                                 directory / f"outside-band-{shot}.png")
                result["receiver_equals_other_variant"] = (
                    result["receiver_sha256"] == other[name]["shots"][shot]["receiver_sha256"])
    (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
