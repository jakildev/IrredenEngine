#!/usr/bin/env python3
"""Grade the captures in this directory.

usage: grade.py <repo> <capture-dir>
         grades every <fixture>-<view>-yaw<deg>.png; writes <capture-dir>/metrics.json
       grade.py <repo> --shadow-regression <evidence-dir>
         grades the parent/head shadow frames recorded in captures.json; writes
         <evidence-dir>/shadow-regression.json and exits 1 on any failed row
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

METRIC = "scripts/render-grid-cell-metric.py"


def grade_directory(repo, directory):
    flags = {"normals": [], "beauty": ["--view", "beauty"],
             "shadow": ["--view", "shadow", "--terminator-tolerance", "0"]}
    keys = ("pass", "missing_exposed_cells", "missing_cells", "visible_expected_cells",
            "missing_pixels", "extra_pixels", "wrong_face_pixels", "false_shadow_pixels",
            "missed_shadow_pixels", "lit_interior_pixels", "shadowed_interior_pixels",
            "occlusion_exercised")
    rows = []
    for image in sorted(directory.glob("*-yaw*.png")):
        fixture, view, yaw = image.stem.split("-")
        yaw = yaw.removeprefix("yaw")
        command = ["python3", METRIC, str(image), "--fixture", fixture, "--yaw", yaw,
                   *flags[view]]
        run = subprocess.run(command, cwd=repo, capture_output=True, text=True)
        result = json.loads(run.stdout) if run.stdout.strip() else {"error": run.stderr}
        rows.append(dict(image=image.name, exit=run.returncode,
                         command=["python3", METRIC, image.name, *command[3:]],
                         result=result))
        print(image.name, run.returncode, {k: result[k] for k in keys if k in result})
    (directory / "metrics.json").write_text(json.dumps(rows, indent=2) + "\n")
    return 0


def grade_shadow_regression(repo, evidence):
    """Shadow overlay, regression form: parent and head frames are byte-identical
    except the plate at 270 degrees, whose differences must stay inside the
    gained cell's footprint dilated by one pixel."""
    stored = {}
    for record in json.loads((evidence / "captures.json").read_text()):
        for image in record["images"]:
            path = evidence / image["stored"]
            if hashlib.sha256(path.read_bytes()).hexdigest() != image["sha256"]:
                sys.exit(f"{path}: sha256 differs from captures.json")
            stored[record["side"], record["label"], record["view"], image["yaw"]] = image
    rows, failed = [], 0
    for fixture in ("cube", "plate"):
        for yaw in (0, 90, 180, 270):
            parent = stored["parent", fixture, "shadow", yaw]
            head = stored["head", fixture, "shadow", yaw]
            row = dict(fixture=fixture, yaw=yaw, parent=parent["stored"], head=head["stored"],
                       parent_sha256=parent["sha256"], head_sha256=head["sha256"])
            if (fixture, yaw) != ("plate", 270):
                row.update(grade="byte-identical", passed=parent["sha256"] == head["sha256"])
            else:
                normals = stored["parent", fixture, "normals", yaw]["stored"]
                arguments = [head["stored"], "--fixture", fixture, "--yaw", str(yaw),
                             "--zoom", "8", "--regression-parent", parent["stored"],
                             "--gained-from", normals]
                run = subprocess.run(["python3", str(repo / METRIC), *arguments], cwd=evidence,
                                     capture_output=True, text=True)
                result = json.loads(run.stdout) if run.stdout.strip() else {"error": run.stderr}
                row.update(grade="gained-cell containment",
                           command=["python3", METRIC, *arguments],
                           result=result,
                           passed=run.returncode == 0
                           and len(result.get("gained_cells", [])) == 1)
            failed += not row["passed"]
            rows.append(row)
            print(fixture, yaw, row["grade"], "PASS" if row["passed"] else "FAIL",
                  {k: row["result"][k] for k in ("gained_cells", "differing_pixels",
                                                 "footprint_pixels", "dilated_footprint_pixels",
                                                 "outside_footprint_pixels")
                   if k in row.get("result", {})})
    (evidence / "shadow-regression.json").write_text(json.dumps(rows, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    repo = Path(sys.argv[1]).resolve()
    if sys.argv[2] == "--shadow-regression":
        raise SystemExit(grade_shadow_regression(repo, Path(sys.argv[3]).resolve()))
    raise SystemExit(grade_directory(repo, Path(sys.argv[2])))
