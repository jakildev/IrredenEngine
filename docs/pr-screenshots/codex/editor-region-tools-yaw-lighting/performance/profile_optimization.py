"""Compare two frozen executables with identical staged runtime assets.

Invoke through ir-acquire benchmark; qualification requires the outer wrapper's
exit code and completed quiet report, which cannot be known inside this process.
"""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path("scripts/perf").resolve()))
from surface_lighting_controls import scene_arguments, verify_routes
from profile_matrix import summarize
from repeat_profile import directory_digest, host_power_source, RENDER_EXPERIMENT_ENV

root = Path("/tmp/irreden-yaw-lighting/optimization-perf")
binary = Path("build-release/creations/demos/canvas_stress/IRCanvasStress")
saved = Path("/tmp/irreden-yaw-lighting")
quiet_report = os.environ.get("IR_QUIET_REPORT_FILE")
if not quiet_report:
    raise RuntimeError("IR_QUIET_REPORT_FILE must retain the outer benchmark verdict")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


expected_binaries = {arm: sha256(saved / f"IRCanvasStress-{arm}")
                     for arm in ("baseline", "candidate")}
if expected_binaries["baseline"] == expected_binaries["candidate"]:
    raise RuntimeError("baseline and candidate executables are identical")
expected_assets = {
    "shader_sha256": directory_digest(binary.parent / "shaders"),
    "runtime_scripts_sha256": directory_digest(binary.parent / "scripts"),
}
power = host_power_source()
environment = dict(os.environ, IR_RUN_NO_DISPLAY_GRACE="30",
                   IR_PERAXIS_SURFACE_LIGHTING="1", IR_PERAXIS_VISIBILITY_PREPASS="0",
                   IR_PERAXIS_VISIBILITY_STATS="0")
expected_environment = {name: environment.get(name) for name in RENDER_EXPERIMENT_ENV}
for key in ("IR_PERAXIS_OVERFLOW_DISABLE", "IR_OVERFLOW_LIGHTING_DISABLE",
            "IR_OVERFLOW_FOG_DISABLE"):
    if expected_environment[key] is not None:
        raise RuntimeError(f"correctness-changing inherited control: {key}")

cases = {f"per-axis-zoom{zoom}-{arm}": (zoom, arm)
         for zoom in (1, 4) for arm in ("baseline", "candidate")}
common = [*scene_arguments("grid-overlap"), "--auto-profile", "--profile-frames", "300",
          "--window-mode", "offscreen", "--yaw", "0.6", "--subdivisions", "1"]
root.mkdir(exist_ok=False)
(root / "cases.json").write_text(json.dumps({
    "cases": cases, "common": common, "rounds": 5,
    "order": "forward/reverse alternating; adjacent baseline/candidate pairs per zoom",
    "expected_binary_sha256": expected_binaries, "expected_assets": expected_assets,
    "expected_render_environment": expected_environment, "host_power": power,
    "outer_quiet_report": quiet_report,
    "qualification": "pending outer wrapper exit and quiet report",
}, indent=2) + "\n")


def verify_runtime(arm):
    if sha256(binary) != expected_binaries[arm]:
        raise RuntimeError(f"wrong {arm} executable installed")
    for key, directory in (("shader_sha256", "shaders"),
                           ("runtime_scripts_sha256", "scripts")):
        if directory_digest(binary.parent / directory) != expected_assets[key]:
            raise RuntimeError(f"staged {directory} changed during comparison")
    if host_power_source() != power:
        raise RuntimeError("power source changed during comparison")


try:
    for round_index in range(1, 6):
        order = list(cases) if round_index % 2 else list(reversed(cases))
        for name in order:
            zoom, arm = cases[name]
            if sha256(saved / f"IRCanvasStress-{arm}") != expected_binaries[arm]:
                raise RuntimeError(f"saved {arm} executable changed")
            shutil.copy2(saved / f"IRCanvasStress-{arm}", binary)
            verify_runtime(arm)
            output = root / name / f"round-{round_index}"
            command = ["python3", "scripts/perf/repeat_profile.py", "--target", "IRCanvasStress",
                       "--release", "--timeout", "120", "--output", str(output),
                       "--repeats", "1", "--", *common, "--zoom", str(zoom)]
            print(name, round_index, flush=True)
            subprocess.run(command, env=environment, check=True)
            verify_runtime(arm)
            manifest_path = output / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            if manifest.get("binary_sha256") != expected_binaries[arm]:
                raise RuntimeError(f"{name}: measured binary differs from intended arm")
            if any(manifest.get(key) != value for key, value in expected_assets.items()):
                raise RuntimeError(f"{name}: measured runtime assets differ")
            if manifest.get("render_environment") != expected_environment:
                raise RuntimeError(f"{name}: measured render controls differ")
            if manifest.get("build_type") != "Release" or manifest.get("host_power") != power:
                raise RuntimeError(f"{name}: build type or power changed")
            manifest["comparison_arm"] = arm
            manifest["comparison_integrity_verified"] = True
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        verify_routes(root, cases, rounds=round_index)
        summarize(root, cases)
finally:
    if sha256(saved / "IRCanvasStress-candidate") != expected_binaries["candidate"]:
        raise RuntimeError("cannot restore changed candidate executable")
    shutil.copy2(saved / "IRCanvasStress-candidate", binary)
print(root, flush=True)
