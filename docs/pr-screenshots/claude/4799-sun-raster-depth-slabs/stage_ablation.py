#!/usr/bin/env python3
"""Attribute the slab layer's GPU cost by staging shader variants into one binary.

The head binary allocates and clears the slab words; staging origin/master's
bake, box-bake and receiver shaders over the head layout isolates the buffer
size and clear ("layout"), the writer alone ("writer") and the reader alone
("reader") from the complete head ("head"). Each variant profiles the raster
route CanvasStress control through repeat_profile.py; the runtime shader copies
are restored afterwards. A diagnostic, not a parity statement: the staged
master sampler reads the head layout's header offsets through the head layout
include, which both binaries share by value.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / "docs/pr-screenshots/claude/4759-sun-face-pool-exhaustion"))
from ab_timing import CELLS, parse_sample  # noqa: E402
from capture import SUBDIR, SUFFIX, restore, stage_parent  # noqa: E402
from verify_common import find_exe  # noqa: E402

WRITERS = tuple(f"{name}.{SUFFIX}" for name in ("c_bake_sun_shadow_map", "c_bake_box_sun_shadow"))
READERS = (f"ir_sun_shadow_sample.{SUFFIX}",)
VARIANTS = {
    "head": (),
    "layout": WRITERS + READERS,
    "writer": READERS,
    "reader": WRITERS,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--cell", default="canvas-lower", choices=sorted(CELLS))
    parser.add_argument("--rounds", type=int, default=2)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    target, demo_args = CELLS[args.cell]
    demo = "canvas_stress" if target == "IRCanvasStress" else "perf_grid"
    binary = find_exe(args.build_dir.resolve(), target, demo)
    shader_dir = binary.parent / "shaders" / SUBDIR
    results = {"cell": args.cell, "rounds": {}}
    for round_index in range(args.rounds):
        order = list(VARIANTS) if round_index % 2 == 0 else list(reversed(VARIANTS))
        for variant in order:
            saved = stage_parent(shader_dir, VARIANTS[variant])
            try:
                run_dir = output / f"round-{round_index}" / variant
                env = dict(os.environ, IRREDEN_BUILD_DIR=str(args.build_dir.resolve()))
                command = [sys.executable, "scripts/perf/repeat_profile.py", "--target", target,
                           "--output", str(run_dir), "--repeats", "1", "--timeout", "400", "--",
                           *demo_args]
                # A quiet-window breach contaminates one run; repeat_profile
                # exits non-zero and wants a fresh directory for the retake.
                for _ in range(3):
                    if subprocess.run(command, cwd=ROOT, env=env).returncode == 0:
                        break
                    shutil.rmtree(run_dir, ignore_errors=True)
                reports = sorted(run_dir.glob("run-*.txt"))
                if not reports:
                    raise RuntimeError(f"{run_dir}: no clean profile after three attempts")
                sample = parse_sample(reports[-1])
                results["rounds"].setdefault(variant, []).append(sample)
                shown = ("steady_mean_ms", "gpu_envelope_ms", "gpu:bakeSunShadowMap",
                         "gpu:cardinalSurfaceFb", "gpu:trixelToFb")
                print(variant, round_index,
                      {k: v for k, v in sample.items() if k in shown}, flush=True)
            finally:
                restore(shader_dir, saved)
        (output / "stage-ablation.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
