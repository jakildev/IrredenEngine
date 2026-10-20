"""Capture the bounded-shadow overlap diagnostic; exit 1 if blockers erase shadow.

Run under `ir-acquire gpu --`; requires a built IRCanvasStress and Pillow.
Output must be a new directory.
This fixed-pose diagnostic is intentionally red until same-layer loss is fixed.
"""

import argparse
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
from perf.sun_face_index_probe import summarize  # noqa: E402
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
        parser.error("run under ir-acquire gpu -- to preserve the index capture until copied")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    binary = find_exe(build, "IRCanvasStress", "canvas_stress")
    common = [
        "--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
        "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1",
        "--no-spin", "--no-auto-rotate", "--pivot-origin", "--yaw", "0", "--zoom", "1",
        "--subdivisions", "1", "--no-ao", "--auto-screenshot", "10",
        "--sweep-frames", "1", "10", "--sun-face-index-probe",
    ]
    cases = [
        ("lower", 0, 0, False), ("near-1", 1, 0, False),
        ("near-128", 128, 0, False), ("far-1", 1, -60, False),
        ("far-8", 8, -60, False), ("far-16", 16, -60, False),
        ("far-128", 128, -60, False), ("far-128-repeat", 128, -60, False),
        ("boundary-36", 128, -36, False), ("boundary-40", 128, -40, False),
        ("boundary-44", 128, -44, False),
        ("lower-unlit", 0, 0, True), ("near-unlit", 128, 0, True),
        ("far-unlit", 128, -60, True),
        ("boundary-36-unlit", 128, -36, True),
        ("boundary-40-unlit", 128, -40, True),
        ("boundary-44-unlit", 128, -44, True),
    ]
    report = {"head": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(), "cases": {},
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    patch = subprocess.check_output(["git", "diff", "HEAD"], cwd=ROOT)
    (output / "source.patch").write_bytes(patch)
    crops = {}
    for name, count, offset, unlit in cases:
        directory = output / name
        directory.mkdir()
        config = directory / "config.lua"
        config.write_text(
            'config = {window_mode="offscreen", profiling_enabled=false, '
            'gpu_stage_timing=false, init_window_width=1280, init_window_height=720, '
            'game_resolution_width=1280, game_resolution_height=720, '
            f'screenshot_output_dir={json.dumps(directory.as_posix())}}}\n')
        command = ["fleet-run", "--timeout", "180", "--build-dir", str(build), "IRCanvasStress",
                   "--config-preset", str(config), *common]
        if count:
            command += ["--probe-overlapping-source", "--probe-overlapping-source-count",
                        str(count), "--probe-overlapping-source-offset", "0", "0", str(offset)]
        if unlit:
            command += ["--debug-overlay", "unlit"]
        with (directory / "run.log").open("w") as log:
            result = subprocess.run(platform_launch_argv(command), cwd=ROOT,
                                    stdout=log, stderr=subprocess.STDOUT)
        log = (directory / "run.log").read_text(errors="replace")
        if result.returncode != 0 or run_result(log) != "CLEAN":
            raise RuntimeError(f"{name}: capture did not exit cleanly")
        shots = collect_full_frames(directory)
        if len(shots) != 1 or "sun-face-index-0.csv written=true" not in log:
            raise RuntimeError(f"{name}: missing capture/index witness")
        index = directory / "index.csv"
        shutil.copyfile(binary.parent / "sun-face-index-0.csv", index)
        index_summary = summarize(index, world_point=(0, 0, 2))
        with Image.open(shots[0]) as image:
            if image.width * 9 != image.height * 16:
                raise RuntimeError(f"{name}: expected 16:9 output")
            roi = tuple(round(v * image.width / 2560) for v in (1100, 700, 1450, 820))
            crops[name] = image.convert("RGB").crop(roi)
        report["cases"][name] = {
            "command": command, "result": run_result(log),
            "capture_sha256": hashlib.sha256(shots[0].read_bytes()).hexdigest(),
            "index": index_summary,
            "receiver_roi": roi,
        }
        print(f"{name}: clean capture", flush=True)
    for name, _, _, is_unlit in cases:
        if crops[name].size != crops["lower"].size:
            raise RuntimeError(f"{name}: output size changed")
        if is_unlit and ImageChops.difference(crops["lower-unlit"], crops[name]).getbbox():
            raise RuntimeError(f"{name}: receiver geometry changed in the unlit control")
    for name in ("lower", "near-1", "far-1", "near-128", "far-128"):
        incomplete = sum(c["incomplete_tiles"] for c in report["cases"][name]["index"]["cascades"])
        if bool(incomplete) != (name in {"near-128", "far-128"}):
            raise RuntimeError(f"{name}: did not exercise the intended index completeness regime")
    baseline = list(zip(*(crops["lower"].getchannel(c).tobytes() for c in "RGB")))
    unlit = list(zip(*(crops["lower-unlit"].getchannel(c).tobytes() for c in "RGB")))
    mask = [u == (150, 152, 160) and max(u) - max(b) > 32
            for b, u in zip(baseline, unlit)]
    if sum(mask) < 20:
        raise RuntimeError("receiver ROI has insufficient shadow signal")
    failed = False
    for name, _, _, is_unlit in cases:
        if is_unlit:
            continue
        candidate = crops[name]
        pixels = zip(*(candidate.getchannel(c).tobytes() for c in "RGB"))
        lost = sum(selected and any(c > b + 1 for c, b in zip(pixel, base))
                   for selected, pixel, base in zip(mask, pixels, baseline))
        report["cases"][name]["brightened_receiver_pixels"] = lost
        failed |= lost != 0
        print(f"{name}: {lost} brightened receiver pixels", flush=True)
    report["receiver_geometry_control"] = "identical"
    report["monotone"] = not failed
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
