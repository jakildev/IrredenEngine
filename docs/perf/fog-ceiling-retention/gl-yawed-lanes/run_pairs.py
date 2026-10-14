#!/usr/bin/env python3
"""Interleaved master/head IRPerfGrid pairs through scripts/perf/repeat_profile.py.

Usage: run_pairs.py --out DIR --base-build DIR --head-build DIR --pairs N
       [--lane NAME=ARGS ...] [--common ARGS]

Each pair runs every lane once on head, then once on base, so a drift in host
load lands on both arms. Arm directories are named <base|head>-<lane>-<pair>
and hold repeat_profile.py's manifest.json and run-1.txt; summarize_pairs.py
reads them. The build directory of each arm is passed to repeat_profile.py as
IRREDEN_BUILD_DIR, which fleet-run reads, so no build or checkout happens here.
"""

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
REPEAT = ROOT / "scripts/perf/repeat_profile.py"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--base-build", required=True)
    parser.add_argument("--head-build", required=True)
    parser.add_argument("--pairs", type=int, default=3)
    parser.add_argument("--common", default="--auto-profile 300 --grid-size 64 --yaw 0.35")
    parser.add_argument("--lane", action="append", default=[],
                        help="NAME=demo args appended to --common; repeatable")
    parser.add_argument("--timeout", default="180")
    args = parser.parse_args()
    lanes = []
    for spec in args.lane or ["nofog=", "identity=--fog-reveal",
                              "treatment=--fog-reveal --fog-reveal-surface"]:
        name, _, extra = spec.partition("=")
        lanes.append((name, shlex.split(args.common) + shlex.split(extra)))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    failures = []
    for pair in range(1, args.pairs + 1):
        for arm, build in (("head", args.head_build), ("base", args.base_build)):
            for name, demo_args in lanes:
                target = out / f"{arm}-{name}-{pair}"
                if (target / "run-1.txt").exists():
                    print(f"skip {target.name}: already recorded", flush=True)
                    continue
                if target.exists():
                    # A failed arm leaves its directory behind; retry it afresh.
                    shutil.rmtree(target)
                env = dict(os.environ, IRREDEN_BUILD_DIR=build)
                command = [sys.executable, str(REPEAT), "--target", "IRPerfGrid",
                           "--output", str(target), "--repeats", "1",
                           "--timeout", args.timeout, "--", *demo_args]
                print(f"run {target.name}: {' '.join(demo_args)}", flush=True)
                result = subprocess.run(command, cwd=ROOT, env=env)
                if result.returncode != 0:
                    failures.append((target.name, result.returncode))
                    print(f"FAILED {target.name} exit={result.returncode}", flush=True)
    for name, code in failures:
        print(f"failed arm: {name} exit={code}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
