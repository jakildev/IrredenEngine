#!/usr/bin/env python3
"""Run the build and matrix commands shared by the hosted perf gate."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

TARGET_BINARIES = {
    "IRPerfGrid": Path("build/creations/demos/perf_grid/IRPerfGrid"),
    "IRLuaPerfGrid": Path("build/creations/demos/lua_perf_grid/IRLuaPerfGrid"),
}
BUILD_COMMAND = [
    "cmake", "--build", "build", "--target", *TARGET_BINARIES,
    "ir_ref_bench", "--parallel", "4",
]
MATRIX_COMMAND = [
    "ir-perf-grid", "--quick", "--frames", "60", "--timeout", "300",
    "--target", "both",
]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("build", "matrix"))
    parser.add_argument("--label", default="ci")
    args = parser.parse_args()
    root = Path.cwd()
    command = (
        BUILD_COMMAND
        if args.phase == "build"
        else [*MATRIX_COMMAND, "--label", args.label]
    )
    subprocess.run(command, cwd=root, check=True)


if __name__ == "__main__":
    main()
