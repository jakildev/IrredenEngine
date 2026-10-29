#!/usr/bin/env python3
"""Grade every <fixture>-<view>-yaw<deg>.png in a capture directory.

usage: grade.py <repo> <capture-dir>  -> writes <capture-dir>/metrics.json, prints a table
"""
import json
import subprocess
import sys
from pathlib import Path

repo, directory = Path(sys.argv[1]), Path(sys.argv[2])
flags = {"normals": [], "beauty": ["--view", "beauty"],
         "shadow": ["--view", "shadow", "--terminator-tolerance", "0"]}
keys = ("pass", "missing_exposed_cells", "visible_expected_cells", "missing_pixels",
        "extra_pixels", "wrong_face_pixels", "false_shadow_pixels", "missed_shadow_pixels",
        "lit_interior_pixels", "shadowed_interior_pixels", "occlusion_exercised")
rows = []
for image in sorted(directory.glob("*-yaw*.png")):
    fixture, view, yaw = image.stem.split("-")
    yaw = yaw.removeprefix("yaw")
    command = ["python3", "scripts/render-grid-cell-metric.py", str(image), "--fixture",
               fixture, "--yaw", yaw, *flags[view]]
    run = subprocess.run(command, cwd=repo, capture_output=True, text=True)
    result = json.loads(run.stdout) if run.stdout.strip() else {"error": run.stderr}
    rows.append(dict(image=image.name, exit=run.returncode,
                     command=["python3", "scripts/render-grid-cell-metric.py",
                              image.name, *command[4:]],
                     result=result))
    print(image.name, run.returncode, {k: result[k] for k in keys if k in result})
(directory / "metrics.json").write_text(json.dumps(rows, indent=2) + "\n")
