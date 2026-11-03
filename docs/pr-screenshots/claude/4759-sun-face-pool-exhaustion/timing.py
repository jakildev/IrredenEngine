#!/usr/bin/env python3
"""Profile parent/head/parent sun shaders on exhausted and complete record pools.

Timing only: no index probe, no screenshots. repeat_profile.py owns benchmark
resource coordination. The parent passes stage --parent-ref's sun-index shaders
into the build's runtime shader directory exactly as capture.py does.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from capture import POLICY_SHADERS, ROOT, SUBDIR, restore, sha256, stage_parent  # noqa: E402

sys.path.insert(0, str(ROOT / "scripts"))
from verify_common import find_exe  # noqa: E402

COMMON = ["--only", "shadowbox,floor", "--probe-source-box", "--probe-floor-mode", "grid",
          "--probe-floor-span", "40", "--sun-direction", "0", "0", "-1", "--no-spin",
          "--no-auto-rotate", "--pivot-origin", "--no-ao", "--yaw", "0", "--zoom", "1",
          "--auto-profile", "--profile-frames", "360", "--window-mode", "offscreen",
          "--subdivisions", "1", "--probe-overlapping-source",
          "--probe-overlapping-source-count", "128",
          "--probe-overlapping-source-offset", "0", "0", "-60"]
FILLER = ["--probe-record-pool-filler", "300",
          "--probe-record-pool-filler-offset", "200", "-200", "0"]
# A quiet-window breach rejects the run before any number is kept, so a retry
# only waits for a quieter host; any other failure stops the matrix.
QUIET_ATTEMPTS = 8
CASES = [("far-128", []),
         ("far-128-filler-last", [*FILLER, "--probe-record-pool-filler-last"]),
         ("far-128-filler-first", FILLER)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--parent-ref", default="origin/master",
                        help="ref whose sun-index shaders the parent passes stage")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = find_exe(ROOT / "build", "IRCanvasStress", "canvas_stress")
    shader_dir = binary.parent / "shaders" / SUBDIR
    matrix = []
    for passname, variant in (("parent-a", "parent"), ("head", "head"), ("parent-b", "parent")):
        saved = stage_parent(shader_dir, args.parent_ref) if variant == "parent" else {}
        try:
            hashes = {name: sha256(shader_dir / name) for name in POLICY_SHADERS}
            for name, extra in CASES:
                directory = output / passname / name
                command = ["python3", "scripts/perf/repeat_profile.py", "--target",
                           "IRCanvasStress", "--output", str(directory),
                           "--repeats", "1", "--timeout", "180", "--", *COMMON, *extra]
                print(passname, name, flush=True)
                for breaches in range(QUIET_ATTEMPTS):
                    if subprocess.run(command, cwd=ROOT).returncode == 0:
                        break
                    log = (directory / "run-1.log").read_text(errors="replace")
                    if "ir-run: RESULT=CONTAMINATED" not in log:
                        raise RuntimeError(f"{passname} {name}: run failed; see {directory}")
                    shutil.rmtree(directory)
                else:
                    raise RuntimeError(f"{passname} {name}: no quiet window in "
                                       f"{QUIET_ATTEMPTS} attempts")
                matrix.append({"pass": passname, "variant": variant, "case": name,
                               "shader_sha256": hashes, "command": command,
                               "quiet_breaches_discarded": breaches})
                (output / "matrix.json").write_text(json.dumps(matrix, indent=2) + "\n")
        finally:
            restore(shader_dir, saved)


if __name__ == "__main__":
    main()
