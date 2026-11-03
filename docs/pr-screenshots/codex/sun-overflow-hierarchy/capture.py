#!/usr/bin/env python3
"""Capture the finite-pool overflow controls; run under ir-acquire gpu."""

import argparse
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from perf.sun_geometry_probe import load_quads  # noqa: E402
from verify_common import (  # noqa: E402
    collect_full_frames,
    find_exe,
    platform_launch_argv,
    run_result,
)


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
    report = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             text=True).strip(),
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "cases": {}}
    patch = subprocess.check_output(["git", "diff", "HEAD", "--", "engine"], cwd=ROOT)
    (output / "source.patch").write_bytes(patch)
    common = ["--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
              "--probe-floor-span", "40", "--no-spin", "--no-auto-rotate", "--pivot-origin",
              "--yaw", "0", "--zoom", "1", "--subdivisions", "1", "--no-ao",
              "--auto-screenshot", "10", "--sweep-frames", "1", "10"]
    cases = [("lower", 0, 0, (0, 0, -1)), ("far-8", 8, -60, (0, 0, -1)),
             ("far-16", 16, -60, (0, 0, -1)), ("far-128", 128, -60, (0, 0, -1)),
             ("near-128", 128, 0, (0, 0, -1)),
             ("oblique-lower", 0, 0, (0.3, 0.4, -1)),
             ("oblique-far-128", 128, -60, (0.3, 0.4, -1)),
             ("lower-no-probe", 0, 0, (0, 0, -1)),
             ("far-128-no-probe", 128, -60, (0, 0, -1)),
             ("oblique-aligned-16", 16, -60, (0.3, 0.4, -1)),
             ("oblique-aligned-128", 128, -60, (0.3, 0.4, -1))]
    for name, count, offset, sun in cases:
        directory = output / name
        directory.mkdir()
        config = directory / "config.lua"
        config.write_text('config = {window_mode="offscreen", profiling_enabled=false, '
                          'gpu_stage_timing=false, init_window_width=1280, init_window_height=720, '
                          'game_resolution_width=1280, game_resolution_height=720, '
                          f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
        command = ["fleet-run", "--timeout", "180", "--build-dir", str(build), "IRCanvasStress",
                   "--config-preset", str(config), *common, "--sun-direction", *map(str, sun)]
        include_geometry = not name.endswith("-no-probe")
        if include_geometry:
            command += ["--sun-face-geometry-probe"]
        if count:
            command += ["--probe-overlapping-source", "--probe-overlapping-source-count",
                        str(count),
                        "--probe-overlapping-source-offset",
                        *(["18", "24", str(offset)] if "aligned" in name
                          else ["0", "0", str(offset)])]
        with (directory / "run.log").open("w") as stream:
            result = subprocess.run(platform_launch_argv(command), cwd=ROOT,
                                    stdout=stream, stderr=subprocess.STDOUT)
        log = (directory / "run.log").read_text(errors="replace")
        shots = collect_full_frames(directory)
        if (result.returncode or run_result(log) != "CLEAN" or len(shots) != 1
                or (include_geometry and "sun-face-index-0.csv written=true" not in log)):
            raise RuntimeError(f"{name}: missing clean capture/index witness")
        entry = {"command": command, "result": "CLEAN", "frame": shots[0].name,
                 "frame_sha256": hashlib.sha256(shots[0].read_bytes()).hexdigest()}
        if include_geometry:
            capture = directory / "index.csv.gz"
            with ((binary.parent / "sun-face-index-0.csv").open("rb") as source,
                  gzip.open(capture, "wb") as dest):
                shutil.copyfileobj(source, dest)
            quads, index = load_quads(capture)
            entry.update({"records": len(quads), "index": index,
                          "geometry_sha256": hashlib.sha256(capture.read_bytes()).hexdigest()})
        else:
            paired = name.removesuffix("-no-probe")
            previous = output / paired / report["cases"][paired]["frame"]
            with Image.open(previous) as before, Image.open(shots[0]) as after:
                if before.size != after.size or ImageChops.difference(
                        before.convert("RGB"), after.convert("RGB")).getbbox():
                    raise RuntimeError(f"{name}: probe changed rendered pixels")
            entry["probe_pixel_equivalence"] = True
        report["cases"][name] = entry
        (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{name}: clean capture", flush=True)


if __name__ == "__main__":
    main()
