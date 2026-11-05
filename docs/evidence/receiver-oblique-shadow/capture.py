"""Capture authored-plane oblique shadows; each fleet-run acquires the GPU lease."""

import argparse
import gzip
import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
from render_receiver_shadow_polygon import check_image  # noqa: E402
from verify_common import (  # noqa: E402
    collect_full_frames,
    find_exe,
    platform_launch_argv,
    run_result,
)


def runtime_hashes(binary):
    files = [binary]
    for directory in ("shaders", "scripts"):
        files.extend(path for path in (binary.parent / directory).rglob("*") if path.is_file())
    return {path.relative_to(binary.parent).as_posix():
            hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    binary = find_exe(build, "IRCanvasStress", "canvas_stress")
    hashes = runtime_hashes(binary)
    (output / "runtime-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")
    report = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             text=True).strip(), "cases": []}
    patch = subprocess.check_output(["git", "diff", "HEAD", "--", "engine", "creations"], cwd=ROOT)
    (output / "source.patch").write_bytes(patch)
    common = ["--only", "shadowbox,floor", "--probe-grid", "--probe-floor-mode", "grid",
              "--probe-floor-span", "40", "--no-spin", "--no-auto-rotate", "--pivot-origin",
              "--zoom", "4", "--subdivisions", "1", "--no-ao", "--auto-screenshot", "10",
              "--sweep-frames", "1", "10", "--probe-overlapping-source"]
    cases = []
    for sign, label in ((1, "positive"), (-1, "negative")):
        sun = (sign * .6, 0, -.8)
        offset = (-sign * .2, 0, -.4)
        for quadrant in range(4):
            cases.append((f"{label}-q{quadrant}-integer", quadrant * math.pi / 2 + .6,
                          sun, (sign * 5.8, 0, -8.4), 1))
        cases.append((f"{label}-near-integer", .6, sun, offset, 1))
        cases.append((f"{label}-fractional", .6, sun, (0, 0, 0), 1))
        cases.append((f"{label}-sampled", .6, sun, offset, 0))
        cases.append((f"{label}-cardinal", 0, sun, offset, 1))
    for name, yaw, sun, offset, surface in cases:
        directory = output / name
        directory.mkdir()
        config = directory / "config.lua"
        config.write_text('config={window_mode="offscreen",profiling_enabled=false,'
                          'gpu_stage_timing=false,init_window_width=1280,init_window_height=720,'
                          'game_resolution_width=1280,game_resolution_height=720,'
                          f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
        command = ["fleet-run", "--env", f"IR_PERAXIS_SURFACE_LIGHTING={surface}",
                   "--env", "IR_PERAXIS_VISIBILITY_PREPASS=0",
                   "--env", "IR_PERAXIS_VISIBILITY_STATS=0", "--build-dir", str(build),
                   "IRCanvasStress", "--config-preset", str(config), *common,
                   "--sun-direction", *map(str, sun), "--yaw", str(yaw),
                   "--probe-overlapping-source-offset", *map(str, offset)]
        result = subprocess.run(platform_launch_argv(command), cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (directory / "run.log.gz").write_bytes(gzip.compress(result.stdout.encode(), mtime=0))
        shots = collect_full_frames(directory)
        if result.returncode or run_result(result.stdout) != "CLEAN" or len(shots) != 1:
            raise RuntimeError(f"{name}: missing clean capture")
        with Image.open(shots[0]) as image:
            verdict = check_image(image, yaw, sun=sun, caster_offset=offset)
            image.crop((1020, 260, 1510, 600)).save(directory / "receiver-detail.png")
        report["cases"].append({"case": name, "command": command, "result": "CLEAN",
                                "surface": surface, "image": str(shots[0].relative_to(output)),
                                "sha256": hashlib.sha256(shots[0].read_bytes()).hexdigest(),
                                **verdict})
        (output / "captures.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{name}: {verdict['counts']}", flush=True)
    if runtime_hashes(binary) != hashes:
        raise RuntimeError("runtime changed during capture")


if __name__ == "__main__":
    main()
