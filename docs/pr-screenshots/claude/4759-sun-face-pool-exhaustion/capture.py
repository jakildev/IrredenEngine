#!/usr/bin/env python3
"""Capture sun record-pool exhaustion controls for parent and head shaders.

Run under ir-acquire gpu from a checkout whose build contains IRCanvasStress
with --probe-record-pool-filler. The parent pass stages --parent-ref's
(default origin/master) four sun-index shaders (Metal on macOS, GLSL
elsewhere) into the build's runtime shader directory and restores the head
copies afterwards; the C++ binary is shared because the policy lives in
those shaders.

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
# Receiver rectangle in a 2560-wide (2x) framebuffer; 1x hosts capture 1280-wide frames.
CROP, CROP_FRAME_WIDTH = (1100, 600, 1460, 840), 2560
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
SHOT_RE = re.compile(r"sun-face-index-probe: shot=(\d+) ")
SPAWN_RE = re.compile(r"RECORD-POOL-FILLER spawned at frame (\d+)")
GROWN_RE = re.compile(r"SUN-RECORD-POOL grown (\d+) -> (\d+) records \((\d+) bytes\) "
                      r"requested=(\d+) sampled_frame=(\d+) applied_frame=(\d+)")
# Transition fixtures: the filler appears at this render frame after the warmup,
# so one running scene crosses the pool's capacity between consecutive shots.
TRANSITION_WARMUP, TRANSITION_SHOTS, TRANSITION_FILLER_FRAME = 6, 8, 9


def case(name, sun, casters=0, height=-60, filler=0, order=None, control=None,
         analytic=False, offset=None):
    prefix = "analytic-" if analytic else ""
    control = control or prefix + ("lower" if sun == DOWN else "oblique-lower")
    if offset is None and casters:
        offset = [18, 24, height] if sun == OBLIQUE else [0, 0, height]
    return {"name": name, "sun": sun, "casters": casters, "height": height,
            "filler": filler, "order": order, "control": control, "analytic": analytic,
            "offset": offset, "velocity": None, "shots": 1, "warmup": 10, "settle": 10,
            "filler_frame": 0}


def track(name, sun, filler=0, order=None, analytic=False):
    """Moving caster stack; each shot is graded against a static capture of its pose."""
    start = [-82, 24, -60] if sun == OBLIQUE else [-100, 0, -60]
    spec = case(name, sun, 128, filler=filler, order=order, offset=start, analytic=analytic)
    spec.update(control=None, velocity=VELOCITY, shots=TRACK_SHOTS, settle=TRACK_SETTLE,
                poses=("oblique-" if sun == OBLIQUE else "") + "moving-128-pose"
                + ("-analytic" if analytic else ""))
    return spec


def transition(name, sun, filler, analytic=False, filler_frame=TRANSITION_FILLER_FRAME):
    """Static scene whose filler spawns mid-run; shots before it grade against the
    filler-free control, shots after it against the sufficient-capacity control.
    Two spawn frames of opposite parity put one shot on the exhausted frame."""
    spec = case(name, sun, 128, filler=filler, analytic=analytic)
    prefix = "analytic-" if analytic else ""
    spec.update(control=None, shots=TRANSITION_SHOTS, warmup=TRANSITION_WARMUP, settle=1,
                filler_frame=filler_frame, control_before=prefix + "far-128",
                control_after=prefix + "far-128-filler-last")
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
    track("analytic-moving-128", DOWN, analytic=True),
    track("analytic-moving-128-filler-last", DOWN, 300, "last", analytic=True),
    track("analytic-moving-128-filler-first", DOWN, 300, "first", analytic=True),
]
# Demand crosses the pool's capacity inside one running scene.
TRANSITIONS = [
    transition("far-128-filler-at-frame-9", DOWN, 300),
    transition("far-128-filler-at-frame-10", DOWN, 300, filler_frame=10),
    transition("analytic-far-128-filler-at-frame-9", DOWN, 300, analytic=True),
    transition("analytic-far-128-filler-at-frame-10", DOWN, 300, analytic=True, filler_frame=10),
]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def scaled_crop(frame_width):
    if CROP_FRAME_WIDTH % frame_width:
        raise RuntimeError(f"frame width {frame_width} does not divide {CROP_FRAME_WIDTH}")
    return tuple(v * frame_width // CROP_FRAME_WIDTH for v in CROP)


def stage_parent(shader_dir, ref):
    saved = {}
    for name in POLICY_SHADERS:
        path = shader_dir / name
        saved[name] = path.read_bytes()
        source = subprocess.check_output(
            ["git", "show", f"{ref}:engine/render/src/shaders/{SUBDIR}{name}"], cwd=ROOT)
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


def capture(binary, build, variant_dir, spec, extra=()):
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
               "--sweep-frames", str(spec["shots"]), str(spec["settle"]),
               "--sun-direction", *map(str, spec["sun"])]
    command[command.index("--auto-screenshot") + 1] = str(spec["warmup"])
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
        if spec["filler_frame"]:
            command += ["--probe-record-pool-filler-frame", str(spec["filler_frame"])]
    command += list(extra)
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
    timeline = transition_timeline(log, count) if spec["filler_frame"] else {}
    shots = []
    for shot, (frame, csv_path) in enumerate(zip(frames, csv_paths)):
        suffix = f"-{shot}" if count > 1 else ""
        receiver = directory / f"receiver{suffix}.png"
        with Image.open(frame) as image:
            crop = scaled_crop(image.width)
            image.crop(crop).save(receiver)
        index = directory / f"index{suffix}.csv.gz"
        with csv_path.open("rb") as source, gzip.open(index, "wb") as dest:
            shutil.copyfileobj(source, dest)
        summary = summarize(index)
        shots.append({"frame": frame.name, "frame_sha256": sha256(frame), "crop": crop,
                      "receiver": receiver.name, "receiver_sha256": sha256(receiver),
                      "pose": poses.get(shot),
                      "index": {key: summary[key] for key in summary if key != "cascades"},
                      "cascades": summary["cascades"], **timeline.get(shot, {})})
    entry = {**spec, "command": command, "result": "CLEAN"}
    if spec["filler_frame"]:
        entry["growth_log"] = [m.groups() for m in GROWN_RE.finditer(log)]
        entry["spawn_log"] = [m[1] for m in SPAWN_RE.finditer(log)]
    if count == 1:
        entry.update(shots[0])
    else:
        entry["shots"] = shots
    return entry


def transition_timeline(log, count):
    """Per shot: whether the filler had spawned and the pool had grown when it was taken,
    read from the order of the engine's log lines."""
    events = sorted(
        [(m.start(), "shot", int(m[1])) for m in SHOT_RE.finditer(log)]
        + [(m.start(), "spawn", int(m[1])) for m in SPAWN_RE.finditer(log)]
        + [(m.start(), "grown", int(m[2])) for m in GROWN_RE.finditer(log)])
    spawned, capacity, grown_before = False, None, 0
    timeline = {}
    for _, kind, value in events:
        if kind == "spawn":
            spawned = True
        elif kind == "grown":
            capacity, grown_before = value, grown_before + 1
        else:
            timeline[value] = {"filler_spawned": spawned, "capacity_changes_before": grown_before,
                               "capacity_after_growth": capacity,
                               "control": None}
    if len(timeline) != count:
        raise RuntimeError("transition: missing probe line per shot")
    return timeline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--parent-ref", default="origin/master",
                        help="ref whose sun-index shaders the parent variant stages")
    parser.add_argument("--variants", default="parent,head",
                        help="comma-separated subset of parent,head; a build whose layout "
                             "differs from --parent-ref runs head only")
    parser.add_argument("--cases", default="",
                        help="comma-separated static case names (controls are added); "
                             "default runs every case")
    parser.add_argument("--tracks", default=None,
                        help="comma-separated track names, 'none' to skip; default runs all")
    parser.add_argument("--transitions", default=None,
                        help="comma-separated transition names, 'none' to skip; default all")
    parser.add_argument("--extra", action="append", default=[],
                        help="extra IRCanvasStress argument appended to every run (repeatable; "
                             "use --extra=--flag for dash values)")
    args = parser.parse_args()
    if os.environ.get("IR_ACQUIRE_HELD_VERB") not in {"gpu", "benchmark"}:
        parser.error("run under ir-acquire gpu -- to protect the shared capture file")
    variants = [v for v in ("parent", "head") if v in args.variants.split(",")]
    if not variants:
        parser.error("--variants must name parent, head or both")
    cases = select_cases(args.cases)
    tracks = TRACKS if args.tracks is None else [
        t for t in TRACKS if t["name"] in args.tracks.split(",")]
    transitions = TRANSITIONS if args.transitions is None else [
        t for t in TRANSITIONS if t["name"] in args.transitions.split(",")]
    if transitions:
        for spec in transitions:
            cases = select_cases(",".join(
                [c["name"] for c in cases] + [spec["control_before"], spec["control_after"]]))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    binary = find_exe(build, "IRCanvasStress", "canvas_stress")
    shader_dir = binary.parent / "shaders" / SUBDIR
    report = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             text=True).strip(),
              "parent": subprocess.check_output(["git", "rev-parse", args.parent_ref],
                                               cwd=ROOT, text=True).strip(),
              "binary_sha256": sha256(binary), "crop": CROP, "band_px": BAND,
              "extra": args.extra, "variants": {}}
    (output / "source.patch").write_bytes(
        subprocess.check_output(["git", "diff", "origin/master", "--", "engine", "creations"],
                                cwd=ROOT))
    for variant in variants:
        saved = stage_parent(shader_dir, args.parent_ref) if variant == "parent" else {}
        try:
            report["variants"][variant] = {
                "shader_sha256": {name: sha256(shader_dir / name) for name in POLICY_SHADERS},
                "cases": {}}
            captured = report["variants"][variant]["cases"]

            def run(spec):
                captured[spec["name"]] = capture(binary, build, output / variant, spec,
                                                 args.extra)
                (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")
                print(f"{variant} {spec['name']}: clean", flush=True)

            for spec in cases:
                run(spec)
            for spec in tracks:
                run(spec)
                poses = [shot["pose"] for shot in captured[spec["name"]]["shots"]]
                pose_names = [f"{spec['poses']}-{shot}" for shot in range(len(poses))]
                if pose_names[0] not in captured:
                    for name, pose in zip(pose_names, poses):
                        run(case(name, spec["sun"], 128, offset=pose, analytic=spec["analytic"]))
                elif poses != [captured[name]["offset"] for name in pose_names]:
                    raise RuntimeError(f"{spec['name']}: poses differ from its static controls")
            for spec in transitions:
                run(spec)
        finally:
            restore(shader_dir, saved)
    for variant, data in report["variants"].items():
        other = report["variants"].get("head" if variant == "parent" else "parent")
        for name, entry in data["cases"].items():
            directory = output / variant / name
            if not isinstance(entry["shots"], list):
                control = output / variant / entry["control"] / "receiver.png"
                entry["versus_control"] = grade(directory / "receiver.png", control,
                                                directory / "outside-band.png")
                if other is not None:
                    entry["receiver_equals_other_variant"] = (
                        entry["receiver_sha256"] == other["cases"][name]["receiver_sha256"])
                continue
            for shot, result in enumerate(entry["shots"]):
                if entry["filler_frame"]:
                    result["control"] = (entry["control_after"] if result["filler_spawned"]
                                         else entry["control_before"])
                else:
                    result["control"] = f"{entry['poses']}-{shot}"
                control = output / variant / result["control"] / "receiver.png"
                result["versus_control"] = grade(directory / result["receiver"], control,
                                                 directory / f"outside-band-{shot}.png")
                if other is not None:
                    result["receiver_equals_other_variant"] = (
                        result["receiver_sha256"] ==
                        other["cases"][name]["shots"][shot]["receiver_sha256"])
    (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")


def select_cases(names):
    """Static cases by name, with each selected case's control, in matrix order."""
    if not names:
        return list(CASES)
    by_name = {spec["name"]: spec for spec in CASES}
    pending = names.split(",")
    wanted = set()
    while pending:
        name = pending.pop()
        if name not in by_name:
            raise SystemExit(f"unknown case {name!r}; known: {', '.join(by_name)}")
        if name not in wanted:
            wanted.add(name)
            pending.append(by_name[name]["control"])
    return [spec for spec in CASES if spec["name"] in wanted]


if __name__ == "__main__":
    main()
