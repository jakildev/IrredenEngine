"""Attribute curved-receiver changes by restoring the rounded caster placement."""

import argparse
import gzip
import hashlib
import json
import math
import subprocess
from pathlib import Path

from capture import (
    ROOT,
    collect_full_frames,
    find_exe,
    platform_launch_argv,
    run_result,
    runtime_hashes,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = find_exe(ROOT / "build", "IRCanvasStress", "canvas_stress")
    hashes = runtime_hashes(binary)
    sun = (-.42, -.60, -.55)
    length = math.hypot(*sun)
    center = tuple(12 * value / length + origin for value, origin in zip(sun, (0, 0, -12)))
    offset = tuple(math.floor(value + .5) - value for value in center)
    report = {"caster_center": center, "rounded_offset": offset, "runtime_hashes": hashes,
              "cases": []}
    for pitched in (False, True):
        for rounded in (False, True):
            name = f"{'pitched' if pitched else 'upright'}-{'rounded' if rounded else 'authored'}"
            directory = output / name
            directory.mkdir()
            config = directory / "config.lua"
            config.write_text("config={window_mode=\"offscreen\",init_window_width=1280,"
                              "init_window_height=720,game_resolution_width=1280,"
                              f"game_resolution_height=720,"
                              f"screenshot_output_dir={json.dumps(str(directory))}}}\n")
            command = ["fleet-run", "--timeout", "120", "IRCanvasStress",
                       "--config-preset", str(config),
                       "--only", "shadowbox,floor", "--probe-analytic-shape", "cylinder",
                       "--pivot-origin", "--no-spin", "--no-auto-rotate", "--no-ao",
                       "--subdivisions", "1", "--zoom", "6", "--auto-screenshot", "6",
                       "--sweep-yaw", "0", "5.49778714", "8", "--probe-overlapping-source"]
            if pitched:
                command += ["--analytic-shape-tilt", ".45"]
            if rounded:
                command += ["--probe-overlapping-source-offset", *map(str, offset)]
            run = subprocess.run(platform_launch_argv(command), cwd=ROOT, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (directory / "run.log.gz").write_bytes(gzip.compress(run.stdout.encode(), mtime=0))
            shots = collect_full_frames(directory)
            if run.returncode or run_result(run.stdout) != "CLEAN" or len(shots) != 8:
                raise RuntimeError(f"{name}: incomplete or unclean capture")
            report["cases"].append({"case": name, "command": command, "result": "CLEAN",
                                    "images": [str(p.relative_to(output)) for p in shots],
                                    "sha256": [hashlib.sha256(p.read_bytes()).hexdigest()
                                               for p in shots]})
            (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")
            print(name, "CLEAN", flush=True)
    if hashes != runtime_hashes(binary):
        raise RuntimeError("runtime changed during captures")


if __name__ == "__main__":
    main()
