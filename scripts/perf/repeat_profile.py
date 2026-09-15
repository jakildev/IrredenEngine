#!/usr/bin/env python3
"""Repeat a fixed demo profile, retaining fresh reports, logs and run spread.

Pass demo arguments after --, including its auto-profile/exit options.
The fleet runner owns benchmark resource coordination. No build is performed.
"""

import argparse
import hashlib
import json
import platform
import shutil
import statistics
import subprocess
import time
from pathlib import Path

from compare_perf_runs import parse_report

TARGETS = {"IRPerfGrid": "perf_grid", "IRCanvasStress": "canvas_stress"}


def shader_digest(directory: Path) -> str:
    if not directory.is_dir():
        raise FileNotFoundError(directory)
    digest = hashlib.sha256()
    for path in sorted(p for p in directory.rglob("*") if p.is_file()):
        name = path.relative_to(directory).as_posix().encode()
        digest.update(len(name).to_bytes(8, "little"))
        digest.update(name)
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=TARGETS, default="IRPerfGrid")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("demo_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.repeats < 1 or args.timeout < 1:
        parser.error("repeats and timeout must be positive")
    demo_args = args.demo_args[1:] if args.demo_args[:1] == ["--"] else args.demo_args
    if "--auto-profile" not in demo_args:
        parser.error("demo arguments must include --auto-profile")
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = ["fleet-run", "--timeout", str(args.timeout), args.target, *demo_args]
    binary = root / "build/creations/demos" / TARGETS[args.target] / args.target
    manifest = {
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "changes": subprocess.check_output(["git", "status", "--short"], cwd=root, text=True),
        "host": platform.platform(),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "shader_sha256": shader_digest(binary.parent / "shaders"),
        "command": command,
        "runs": [],
    }
    report = root / "build/creations/demos" / TARGETS[args.target] / "save_files/profile_report.txt"
    rows = []
    for index in range(1, args.repeats + 1):
        started = time.time_ns()
        log_path = output / f"run-{index}.log"
        with log_path.open("w") as log:
            result = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)
        fresh = report.exists() and report.stat().st_mtime_ns >= started
        clean = result.returncode == 0 and "RESULT=CLEAN" in log_path.read_text()
        run = {
            "index": index,
            "exit_code": result.returncode,
            "fresh_report": fresh,
            "clean": clean,
        }
        manifest["runs"].append(run)
        if fresh:
            destination = output / f"run-{index}.txt"
            shutil.copy2(report, destination)
            row = parse_report(destination, f"run-{index}")
            run["gpu_measured"] = any(stage.avg_ms > 0 for stage in row.gpu_stages)
            rows.append(row)
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if not clean or not fresh or not run.get("gpu_measured"):
            print(
                f"run {index}: failed or missing GPU measurements; inspect {log_path}", flush=True
            )
            return 1
        print(
            f"run {index}: frame avg {rows[-1].frame.avg:.3f} ms; CPU/GPU report retained",
            flush=True,
        )

    metrics = {"frame avg": [row.frame.avg for row in rows]}
    names = sorted({stage.name for row in rows for stage in row.gpu_stages})
    for name in names:
        stages = [row.gpu_by_name(name) for row in rows]
        if all(stage is not None for stage in stages):
            metrics[f"GPU {name}"] = [stage.avg_ms for stage in stages]
    lines = ["| Measurement | Mean ms | Run min–max ms |", "|---|---:|---:|"]
    for name, values in metrics.items():
        lines.append(
            f"| {name} | {statistics.mean(values):.3f} | {min(values):.3f}–{max(values):.3f} |"
        )
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print(output / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
