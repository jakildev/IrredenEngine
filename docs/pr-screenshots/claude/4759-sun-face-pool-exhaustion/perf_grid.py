#!/usr/bin/env python3
"""Profile IRPerfGrid's sun cost with each ref's four sun-index shaders staged.

IRPerfGrid's 262,144-voxel grid exhausts the record pool every frame, so it
measures the per-tile exhaustion policy at the perf-gate cells (zoom 1 and 4,
full subdivision mode, base 1). Each ref's shaders are staged into the build's
runtime shader directory as capture.py does; the binary is shared. The working
tree's own shaders are restored afterwards and run as `head`.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from capture import POLICY_SHADERS, ROOT, SUBDIR, restore, sha256  # noqa: E402

sys.path.insert(0, str(ROOT / "scripts"))
from verify_common import find_exe, platform_launch_argv, run_result  # noqa: E402

ZOOMS = (1, 4)


def stage(shader_dir, ref):
    saved = {}
    for name in POLICY_SHADERS:
        path = shader_dir / name
        saved[name] = path.read_bytes()
        path.write_bytes(subprocess.check_output(
            ["git", "show", f"{ref}:engine/render/src/shaders/{SUBDIR}{name}"], cwd=ROOT))
    return saved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ref", action="append", default=[],
                        help="git ref whose sun shaders run as a variant (repeatable)")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = find_exe(ROOT / "build", "IRPerfGrid", "perf_grid")
    shader_dir = binary.parent / "shaders" / SUBDIR
    report = binary.parent / "save_files" / "profile_report.txt"
    variants = [(ref, ref) for ref in args.ref] + [("head", None)]
    runs = []
    for zoom in ZOOMS:
        for label, ref in variants:
            saved = stage(shader_dir, ref) if ref else {}
            try:
                hashes = {name: sha256(shader_dir / name) for name in POLICY_SHADERS}
                command = ["fleet-run", "--timeout", "400", "IRPerfGrid", "--auto-profile", "60",
                           "--zoom", str(zoom), "--subdivision-mode", "full",
                           "--base-subdivisions", "1"]
                name = f"{label.replace('/', '-')}-zoom{zoom}"
                result = subprocess.run(platform_launch_argv(command), cwd=ROOT,
                                        capture_output=True, text=True)
                (output / f"{name}.log").write_text(result.stdout + result.stderr)
                verdict = run_result(result.stdout + result.stderr)
                if result.returncode or verdict not in {"CLEAN", "CONTAMINATED"}:
                    raise RuntimeError(f"{name}: {verdict}")
                shutil.copy2(report, output / f"{name}.txt")
                runs.append({"variant": label, "zoom": zoom, "command": command,
                             "result": verdict, "shader_sha256": hashes})
                (output / "runs.json").write_text(json.dumps(runs, indent=2) + "\n")
                print(name, verdict, flush=True)
            finally:
                restore(shader_dir, saved)


if __name__ == "__main__":
    main()
