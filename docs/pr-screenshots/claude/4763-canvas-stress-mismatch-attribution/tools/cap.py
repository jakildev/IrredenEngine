#!/usr/bin/env python3
"""Capture CanvasStress transition scenes with the manifest's exact argv.

usage: cap.py --build-dir B --out OUT --scene NAME [--extra ARG ...]
Each scene is one fresh process; full frames + log land in OUT/NAME/.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

SCENES = {
    "modeswitch": (12, ["--only", "modeswitch", "--no-auto-rotate", "--no-spin"],
                   ["modeswitch_grid", "modeswitch_detached", "modeswitch_revox",
                    "modeswitch_grid_return"]),
    "detach": (30, ["--only", "detach", "--no-auto-rotate", "--no-spin"],
               ["detach_hosted", "detach_own_canvas", "detach_grid"]),
    "impulse": (6, ["--only", "impulse", "--no-auto-rotate"],
                ["impulse_midspin", "impulse_settled"]),
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scene", required=True, choices=sorted(SCENES))
    ap.add_argument("--label", default=None)
    ap.add_argument("--extra", action="append", default=[])
    ap.add_argument("--warmup", type=int, default=None)
    a = ap.parse_args()
    warm, args, names = SCENES[a.scene]
    if a.warmup is not None:
        warm = a.warmup
    build = Path(a.build_dir).resolve()
    exe_dir = build / "creations" / "demos" / "canvas_stress"
    shots = exe_dir / "save_files" / "screenshots"
    if shots.exists():
        shutil.rmtree(shots)
    shots.mkdir(parents=True)
    out = Path(a.out) / (a.label or a.scene)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    cmd = ["fleet-run", "--build-dir", str(build), "--timeout", "240", "IRCanvasStress",
           "--auto-screenshot", str(warm), *args, *a.extra]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    log = proc.stdout + proc.stderr
    (out / "run.log").write_text(log)
    result = None
    for line in log.splitlines():
        if "RESULT=" in line and "ir-run" in line:
            result = line.split("RESULT=", 1)[1].split()[0]
    frames = sorted(p for p in shots.glob("screenshot_*.png")
                    if p.stem.split("_")[-1].isdigit())
    record = {"cmd": cmd, "rc": proc.returncode, "result": result, "shots": {}}
    for name, src in zip(names, frames):
        dst = out / f"{name}.png"
        shutil.copy2(src, dst)
        record["shots"][name] = hashlib.sha256(dst.read_bytes()).hexdigest()
    record["frame_count"] = len(frames)
    exe = exe_dir / "IRCanvasStress"
    record["exe_sha256"] = hashlib.sha256(exe.read_bytes()).hexdigest()
    (out / "capture.json").write_text(json.dumps(record, indent=1))
    print(json.dumps({k: record[k] for k in ("rc", "result", "frame_count")}), out)
    return 0 if (proc.returncode == 0 and result == "CLEAN" and len(frames) >= len(names)) else 1


if __name__ == "__main__":
    sys.exit(main())
