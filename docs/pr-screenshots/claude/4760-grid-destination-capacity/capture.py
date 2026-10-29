#!/usr/bin/env python3
"""Run one IRCanvasStress capture through fleet-run and copy its new screenshots.

usage: capture.py <build-dir> <out-dir> <label> <view> [extra args...]
view: normals | beauty | shadow. Writes <out-dir>/<label>-<view>-yaw<deg>.png
for the four cardinal yaws and appends the exact command to <out-dir>/captures.jsonl.
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

build, out, label, view, *extra = sys.argv[1:]
shots = Path(build) / "creations/demos/canvas_stress/save_files/screenshots"
out = Path(out)
out.mkdir(parents=True, exist_ok=True)
base = ["--auto-screenshot", "30", "--no-auto-rotate", "--no-spin", "--only", "orbit",
        "--focus-orbit", "6", "--pivot-origin", "--subdivisions", "1", "--zoom", "8",
        "--sweep-yaw", "0", "4.7123889804", "4"]
probe_path = out / f"{label}-{view}-probe.txt"
probe_args = (["--grid-capacity-probe", str(probe_path)]
              if "GRID_PROBE" in __import__("os").environ else [])
views = {"normals": ["--no-ao", "--no-shadows", "--debug-overlay", "normals"],
         "beauty": [],
         "shadow": ["--no-ao", "--debug-overlay", "shadow"]}
command = ["fleet-run", "--build-dir", build, "--timeout", "240", "IRCanvasStress",
           *base, *views[view], *probe_args, *extra]
before = {p.name: p.stat().st_mtime for p in shots.glob("*.png")}
run = subprocess.run(command, capture_output=True, text=True)
log = run.stdout + run.stderr
fresh = sorted((p for p in shots.glob("*.png") if before.get(p.name) != p.stat().st_mtime),
               key=lambda p: p.name)
if run.returncode != 0 or "RESULT=CLEAN" not in log or len(fresh) != 4:
    sys.exit(f"capture failed: exit={run.returncode} shots={len(fresh)}\n{log[-2000:]}")
images = []
for yaw, shot in zip((0, 90, 180, 270), fresh):
    target = out / f"{label}-{view}-yaw{yaw}.png"
    target.write_bytes(shot.read_bytes())
    images.append(dict(path=target.name, yaw=yaw,
                       sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
probe = [line.split("[grid-capacity] ", 1)[1]
         for line in (probe_path.read_text().splitlines() if probe_args else [])]
launch = next((line for line in log.splitlines() if line.startswith("ir-run: ")), "")
if build not in launch:
    sys.exit(f"capture ran an exe outside {build}: {launch}")
record = dict(label=label, view=view, command=command, launch=launch, clean_exit=True,
              probe=probe, images=images)
with (out / "captures.jsonl").open("a") as handle:
    handle.write(json.dumps(record) + "\n")
print(json.dumps(dict(label=label, view=view, probe=probe[-1:] if probe else [],
                      images=[i["path"] for i in images])))
