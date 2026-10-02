import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import subprocess

root = Path.cwd()
phase = sys.argv[1] if len(sys.argv) > 1 else "parent"
out = Path("/tmp/codex-cardinal-coverage-" + phase)
out.mkdir(exist_ok=False)
binary = root / "build/creations/demos/perf_grid/IRPerfGrid"
common = ["--mode", "voxel_set", "--wave-freeze", "--auto-profile", "90", "--capture-frame", "60", "--grid-size", "64", "--zoom", "4", "--subdivision-mode", "full", "--base-subdivisions", "4", "--wave-amplitude", "5", "--pivot-origin", "--no-overlay"]
if len(sys.argv) > 2:
    common += ["--debug-overlay", sys.argv[2]]
manifest = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "runs": []}
(out / "source.patch").write_text(subprocess.check_output(["git", "diff"], text=True))
for pose, yaw in [("cardinal", "0"), ("diagonal", "0.785398163"), ("cardinal90", "1.570796327")]:
    for timing, preset in [("on", "million.lua")]:
        name = pose + "-" + timing
        folder = out / name
        folder.mkdir()
        cmd = ["fleet-run", "IRPerfGrid", *common, "--yaw", yaw, "--config-preset", "configs/perf/" + preset]
        with (folder / "run.log").open("w") as log:
            result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, env={**os.environ, "IR_FLEET_RUN_TIMEOUT": "600"})
        text = (folder / "run.log").read_text()
        if result.returncode or "RESULT=CLEAN" not in text:
            raise RuntimeError(name + " did not exit cleanly")
        shots = re.findall(r"Saved screenshot: ([^\r\n]+)", text)
        if len(shots) != 1:
            raise RuntimeError(name + " screenshot count " + str(len(shots)))
        path = Path(re.sub(r"\x1b\[[0-9;]*m", "", shots[0]).strip())
        if not path.is_absolute():
            path = binary.parent / path
        shutil.copy2(path, folder / "capture.png")
        manifest["runs"].append({"name": name, "command": cmd, "clean": True, "capture_sha256": hashlib.sha256((folder / "capture.png").read_bytes()).hexdigest()})
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print("CLEAN", name, flush=True)
