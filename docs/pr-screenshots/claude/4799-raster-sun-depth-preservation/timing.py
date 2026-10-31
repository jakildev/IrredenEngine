#!/usr/bin/env python3
"""Interleaved parent/head/second profiles for the raster-only sun bake.

Run under ir-acquire benchmark. Each pair profiles every variant once per
scene before the next pair starts, so host drift lands on every variant
alike. The parent pass stages origin/master's bake and sampler shaders into
the build's runtime shader directory as capture.py does; head restores this
branch's copies; second adds --sun-second-depth (IRCanvasStress only; the
switch is inert on IRPerfGrid's finite route). repeat_profile.py retains each
run's report, log and manifest; timing.md pools the per-variant spread.
"""

import argparse
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from capture import HASHED_SHADERS, ROOT, SUBDIR, restore, sha256, stage_parent  # noqa: E402

sys.path.insert(0, str(ROOT / "scripts"))
from verify_common import find_exe  # noqa: E402

RASTER = ["--only", "shadowbox,floor", "--probe-grid", "--probe-floor-mode", "grid",
          "--probe-floor-span", "120", "--sun-direction", "0", "0", "-1", "--no-spin",
          "--no-auto-rotate", "--pivot-origin", "--no-ao", "--yaw", "0", "--zoom", "1",
          "--subdivisions", "1", "--probe-particle-raster", "--probe-ray-casters", "1", "72", "0",
          "--auto-profile", "--profile-frames", "360", "--window-mode", "offscreen"]
GRID = ["--auto-profile", "60", "--subdivision-mode", "full", "--base-subdivisions", "1"]
SCENES = [
    ("raster-stacked", "IRCanvasStress", "canvas_stress", RASTER, ("parent", "head", "second")),
    ("perf-grid-zoom1", "IRPerfGrid", "perf_grid", [*GRID, "--zoom", "1"], ("parent", "head")),
    ("perf-grid-zoom4", "IRPerfGrid", "perf_grid", [*GRID, "--zoom", "4"], ("parent", "head")),
]
ROWS = ("steady frame avg", "steady frame p99", "GPU frame envelope", "GPU canvasClear",
        "GPU bakeSunShadowMap", "GPU voxelSunFaces", "GPU computeSunShadow",
        "GPU computeLightVolume", "GPU lightingToTrixel", "GPU trixelToFb",
        "GPU entityCanvasToFb")
ROW_RE = re.compile(r"^\| (.+?) \| ([0-9.]+) \| ")


def read_summary(path):
    values = {}
    for line in path.read_text().splitlines():
        match = ROW_RE.match(line)
        if match:
            values[match[1]] = float(match[2])
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    matrix = []
    for pair in range(args.pairs):
        for scene, target, demo, demo_args, variants in SCENES:
            order = variants if pair % 2 == 0 else tuple(reversed(variants))
            shader_dir = find_exe(build, target, demo).parent / "shaders" / SUBDIR
            for variant in order:
                saved = stage_parent(shader_dir) if variant == "parent" else {}
                try:
                    hashes = {name: sha256(shader_dir / name) for name in HASHED_SHADERS
                              if (shader_dir / name).exists()}
                    run_dir = output / scene / variant / f"pair-{pair}"
                    command = ["python3", "scripts/perf/repeat_profile.py", "--target", target,
                               "--output", str(run_dir), "--repeats", "1", "--timeout", "400",
                               "--", *demo_args]
                    if variant == "second":
                        command.append("--sun-second-depth")
                    print(scene, variant, f"pair {pair}", flush=True)
                    subprocess.run(command, cwd=ROOT, check=True)
                    matrix.append({"scene": scene, "variant": variant, "pair": pair,
                                   "shader_sha256": hashes, "command": command,
                                   "summary": read_summary(run_dir / "summary.md")})
                    (output / "matrix.json").write_text(json.dumps(matrix, indent=2) + "\n")
                finally:
                    restore(shader_dir, saved)
    lines = []
    for scene, _, _, _, variants in SCENES:
        lines += [f"## {scene}", "",
                  "| Row | " + " | ".join(f"{v} mean (min–max) ms" for v in variants)
                  + " | head / parent | second / head |", "|---|" + "---:|" * (len(variants) + 2)]
        for row in ROWS:
            means = {}
            cells = []
            for variant in variants:
                values = [m["summary"][row] for m in matrix
                          if m["scene"] == scene and m["variant"] == variant
                          and row in m["summary"]]
                if not values:
                    cells.append("n/a")
                    continue
                means[variant] = statistics.mean(values)
                cells.append(f"{means[variant]:.3f} ({min(values):.3f}–{max(values):.3f})")
            if not means:
                continue

            def ratio(a, b):
                return (f"{means[a] / means[b]:.3f}" if a in means and b in means and means[b]
                        else "n/a")
            lines.append(f"| {row} | " + " | ".join(cells)
                         + f" | {ratio('head', 'parent')} | {ratio('second', 'head')} |")
        lines.append("")
    (output / "timing.md").write_text("\n".join(lines))
    print(output / "timing.md")


if __name__ == "__main__":
    main()
