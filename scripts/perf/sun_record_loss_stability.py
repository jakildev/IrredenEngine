#!/usr/bin/env python3
"""Whether the set of sun-record-lost tiles is stable across frames and launches.

`capture` launches a fixture N times. Each launch writes one
`--sun-face-index-probe` CSV per identical-camera shot (CanvasStress
`--sweep-frames`) or one at the end of an auto-profile (PerfGrid). `compare`
reads the retained captures and reports, per fixture, how many tiles carry a
lost flag that differs between any two captures, in-run and across launches,
and how many pixels differ inside the fixture's receiver crop at exact RGB.

Raw captures (frames, full probe CSVs, logs) stay under `--raw`; `compare`
writes the compact evidence that is committed (`compare.json`, every
capture's lost-tile list, the manifest) under
`docs/perf/sun-record-loss-stability/<host>/<fixture>/`.

Usage:
    run.py capture <fixture> --launches N --raw DIR
    run.py compare <fixture> --raw DIR
"""

import argparse
import gzip
import hashlib
import itertools
import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/perf/sun-record-loss-stability"
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "perf"))
from sun_face_index_probe import read_capture  # noqa: E402
from verify_common import platform_launch_argv  # noqa: E402

HOST = "macos-metal" if sys.platform == "darwin" else (
    "windows-opengl" if platform.system() == "Windows" else "linux-opengl")
TIMEOUT = 900

# The historical exhaustion capture scene (docs/perf/sun-overflowed-tile-query):
# 128 coincident caster boxes 60 units above a grid floor, sun straight down.
SCENE = ["--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
         "--probe-floor-span", "40", "--no-spin", "--no-auto-rotate", "--pivot-origin",
         "--yaw", "0", "--zoom", "1", "--no-ao", "--subdivisions", "1",
         "--sun-direction", "0", "0", "-1", "--probe-overlapping-source",
         "--probe-overlapping-source-count", "128",
         "--probe-overlapping-source-offset", "0", "0", "-60"]
# A 300x300x1 record-pool filler plate created after the casters, 30 units
# under the floor. Its faces overlap the receiver's sun tiles, so a lost plate
# record marks a receiver tile lost, and the floor hides the plate from the
# receiver crop. The 62-unit shift puts the receiver on the edge of the band
# of tiles whose loss varies; centred, every receiver tile is always lost.
FILLER_UNDER = ["--probe-record-pool-filler", "300", "--probe-record-pool-filler-last",
                "--probe-record-pool-filler-offset", "0", "62", "30"]
FROZEN = ["--sun-record-capacity", "65536", "--sun-record-ceiling", "65536"]
# Ten identical-camera shots 30 frames apart, one probe CSV per shot.
SHOTS = ["--sun-face-index-probe", "--auto-screenshot", "10", "--sweep-frames", "10", "30"]
# 2560-wide (2x) frame rectangle: the floor band under the caster stack. It
# excludes the coincident caster stack above it, which z-fights between
# launches on its own.
RECEIVER_CROP = (1100, 712, 1460, 840)
# Sun-space (u_min, v_min, u_max, v_max) of the 40-unit receiver floor; the
# sun is straight down, so u = x and v = -y.
FLOOR_WINDOW = (-20.0, -20.0, 20.0, 20.0)

FIXTURES = {
    "canvas-unfrozen": {"target": "IRCanvasStress", "demo": "canvas_stress",
                        "args": [*SCENE, *FILLER_UNDER, *SHOTS],
                        "crop": RECEIVER_CROP},
    "canvas-frozen": {"target": "IRCanvasStress", "demo": "canvas_stress",
                      "args": [*SCENE, *FILLER_UNDER, *FROZEN, *SHOTS],
                      "crop": RECEIVER_CROP},
    "canvas-frozen-overlay": {"target": "IRCanvasStress", "demo": "canvas_stress",
                              "args": [*SCENE, *FILLER_UNDER, *FROZEN, *SHOTS,
                                       "--debug-overlay", "shadow"],
                              "crop": RECEIVER_CROP},
    # 262,144 voxels, wave frozen: requests 786,432 records against the
    # 131,072-record ceiling. One probe after the last profile frame.
    "perfgrid-64": {"target": "IRPerfGrid", "demo": "perf_grid",
                    "args": ["--mode", "voxel_set", "--grid-size", "64", "--wave-freeze",
                             "--auto-profile", "300", "--no-overlay",
                             "--sun-face-index-probe"],
                    "crop": None},
}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True,
                          text=True).stdout.strip()


def probe_paths(directory):
    return sorted(directory.glob("sun-face-index-*.csv"))


def capture(args):
    fixture = FIXTURES[args.fixture]
    build_dir = ROOT / args.build_dir
    binary = build_dir / "creations/demos" / fixture["demo"] / fixture["target"]
    binary = binary if binary.is_file() else binary.with_suffix(".exe")
    out = Path(args.raw).resolve() / HOST / args.fixture
    out.mkdir(parents=True, exist_ok=True)
    manifest = {"fixture": args.fixture, "host": HOST, "platform": platform.platform(),
                "head": git("rev-parse", "HEAD"),
                "dirty_engine": bool(git("status", "--porcelain", "--", "engine",
                                         "creations/demos")),
                "binary_sha256": sha256(binary),
                "command": ["fleet-run", "--timeout", str(TIMEOUT), "--build-dir",
                            str(args.build_dir), fixture["target"], "--config-preset",
                            "<launch>/config.lua", *fixture["args"]]}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    first = len(list(out.glob("launch-*")))
    for launch in range(first, first + args.launches):
        directory = out / f"launch-{launch:02d}"
        directory.mkdir()
        config = directory / "config.lua"
        config.write_text('config = {window_mode="offscreen", profiling_enabled=false, '
                          'gpu_stage_timing=false, init_window_width=1280, '
                          'init_window_height=720, game_resolution_width=1280, '
                          'game_resolution_height=720, '
                          f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
        for stale in probe_paths(binary.parent):
            stale.unlink()
        command = ["fleet-run", "--timeout", str(TIMEOUT), "--build-dir", str(build_dir),
                   fixture["target"], "--config-preset", str(config), *fixture["args"]]
        print(f"capture {args.fixture} launch {launch}", flush=True)
        with (directory / "run.log").open("w") as log:
            code = subprocess.run(platform_launch_argv(command), cwd=ROOT,
                                  stdin=subprocess.DEVNULL, stdout=log,
                                  stderr=subprocess.STDOUT).returncode
        text = (directory / "run.log").read_text(errors="replace")
        probes = probe_paths(binary.parent)
        if code or "written=false" in text or not probes:
            raise SystemExit(f"{directory}: exit {code}, {len(probes)} probe CSVs")
        for probe in probes:
            with probe.open("rb") as source, gzip.open(
                    directory / (probe.name + ".gz"), "wb") as dest:
                shutil.copyfileobj(source, dest)
            probe.unlink()
        with (directory / "run.log").open("rb") as source, gzip.open(
                directory / "run.log.gz", "wb") as dest:
            shutil.copyfileobj(source, dest)
        (directory / "run.log").unlink()


def lost_tiles(path, window=None):
    """The lost-flag tile set and the header of one probe CSV; with `window`
    (sun-space u_min, v_min, u_max, v_max), only tiles whose bounds overlap it."""
    metadata, lines = read_capture(path)
    header, *rows = lines
    columns = header.strip().split(",")
    names = ("cascade", "x", "y", "records_lost", "u_min", "v_min", "u_max", "v_max")
    at = {name: columns.index(name) for name in names}
    lost = set()
    for row in rows:
        fields = row.strip().split(",")
        if fields[at["records_lost"]] != "1":
            continue
        if window is not None:
            u_min, v_min, u_max, v_max = (float(fields[at[n]]) for n in names[4:])
            if u_max <= window[0] or u_min >= window[2] or v_max <= window[1] \
                    or v_min >= window[3]:
                continue
        lost.add((int(fields[at["cascade"]]), int(fields[at["x"]]), int(fields[at["y"]])))
    return lost, metadata


def changed_pixels(a, b, crop=None):
    """Pixels whose RGB differs at all between two frames, inside `crop`."""
    from PIL import Image, ImageChops
    first, second = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if first.size != second.size:
        raise ValueError(f"{a} and {b} differ in size")
    if crop is not None:
        scale = first.size[0] / 2560
        box = tuple(round(value * scale) for value in crop)
        first, second = first.crop(box), second.crop(box)
    # The per-channel maximum, not a luma conversion, which rounds a
    # one-unit blue delta to zero.
    red, green, blue = ImageChops.difference(first, second).split()
    widest = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    return widest.width * widest.height - widest.histogram()[0]


def set_summary(captures):
    """`captures` maps a label to a lost-tile set; the cross-capture spread."""
    sets = list(captures.values())
    union = set().union(*sets)
    common = set.intersection(*sets)
    pairs = [len(a ^ b) for a, b in itertools.combinations(sets, 2)]
    return {"captures": len(sets),
            "distinct_sets": len({frozenset(s) for s in sets}),
            "lost_min": min(map(len, sets)), "lost_max": max(map(len, sets)),
            "tiles_differing_any": len(union - common),
            "pairwise_max": max(pairs, default=0),
            "pairwise_min": min(pairs, default=0)}


def compare(args):
    fixture = FIXTURES[args.fixture]
    out = Path(args.raw).resolve() / HOST / args.fixture
    report_dir = EVIDENCE / HOST / args.fixture
    report_dir.mkdir(parents=True, exist_ok=True)
    launches = sorted(out.glob("launch-*"))
    if not launches:
        raise SystemExit(f"no launches under {out}")
    per_launch, per_launch_floor, first_shots = {}, {}, {}
    report = {"fixture": args.fixture, "host": HOST, "launches": {}}
    for launch in launches:
        shots, floor = {}, {}
        for probe in sorted(launch.glob("sun-face-index-*.csv.gz")):
            shot = probe.name.split(".")[0].removeprefix("sun-face-index-")
            shots[shot], metadata = lost_tiles(probe)
            floor[shot] = lost_tiles(probe, FLOOR_WINDOW)[0]
        frames = sorted(launch.glob("screenshot_*.png"))
        entry = {"requested": int(metadata["face_records_requested"]),
                 "capacity": int(metadata["face_record_capacity"]),
                 "in_run": set_summary(shots),
                 "floor_window_in_run": set_summary(floor)}
        if fixture["crop"] is not None and len(frames) > 1:
            entry["in_run"]["crop_pixels_vs_first"] = [
                changed_pixels(frame, frames[0], fixture["crop"]) for frame in frames[1:]]
            entry["in_run"]["frame_pixels_vs_first"] = [
                changed_pixels(frame, frames[0]) for frame in frames[1:]]
        report["launches"][launch.name] = entry
        per_launch[launch.name] = shots
        per_launch_floor[launch.name] = floor
        first_shots[launch.name] = (next(iter(shots.values())), frames[0] if frames else None)
    report["across_launches_first_shot"] = set_summary(
        {name: lost for name, (lost, _) in first_shots.items()})
    report["all_captures"] = set_summary(
        {f"{name}/{shot}": lost for name, shots in per_launch.items()
         for shot, lost in shots.items()})
    report["all_captures_floor_window"] = set_summary(
        {f"{name}/{shot}": lost for name, shots in per_launch_floor.items()
         for shot, lost in shots.items()})
    if fixture["crop"] is not None:
        reference = first_shots[launches[0].name][1]
        report["across_launches_first_shot"]["crop_pixels_vs_launch_00"] = [
            changed_pixels(frame, reference, fixture["crop"])
            for frame, in ((f,) for _, f in list(first_shots.values())[1:])]
    report["manifest"] = json.loads((out / "manifest.json").read_text())
    (report_dir / "compare.json").write_text(json.dumps(report, indent=2) + "\n")
    lists = {name: {shot: sorted(lost) for shot, lost in shots.items()}
             for name, shots in per_launch.items()}
    with gzip.open(report_dir / "lost-tiles.json.gz", "wt") as stream:
        json.dump(lists, stream)
    print(json.dumps({key: value for key, value in report.items()
                      if key not in ("launches", "manifest")},
                     indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="verb", required=True)
    for verb in ("capture", "compare"):
        p = sub.add_parser(verb)
        p.add_argument("fixture", choices=FIXTURES)
        p.add_argument("--raw", required=True)
        if verb == "capture":
            p.add_argument("--launches", type=int, default=1)
            p.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    {"capture": capture, "compare": compare}[args.verb](args)


if __name__ == "__main__":
    main()
