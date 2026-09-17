#!/usr/bin/env python3
"""Repeat a fixed demo profile, retaining fresh reports, logs and run spread.

Pass demo arguments after --, including its auto-profile/exit options.
Optional macOS CPU sampling retains call stacks; sampled timings include its overhead.
The fleet runner owns benchmark resource coordination. No build is performed.
"""

import argparse
import hashlib
import json
import math
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


def find_demo_pid(parent_pid: int, binary: Path, process_table: str) -> int | None:
    processes = {}
    for line in process_table.splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) == 3:
            processes[int(fields[0])] = (int(fields[1]), fields[2])
    for pid, (_, executable) in processes.items():
        if executable not in (str(binary), "./" + binary.name, binary.name):
            continue
        ancestor = pid
        visited = set()
        while ancestor in processes and ancestor not in visited:
            if ancestor == parent_pid:
                return pid
            visited.add(ancestor)
            ancestor = processes[ancestor][0]
    return None


def redact_sample_output(sample_log: str, root: Path, trace: Path) -> str:
    return sample_log.replace(str(trace), trace.name).replace(str(root), "<repo>")


def run_profile(command, root, log_path, binary, sample_seconds, sample_delay):
    sampling = None
    with log_path.open("w") as log:
        with subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT) as demo:
            if sample_seconds:
                try:
                    launched = time.monotonic()
                    pid = None
                    discovered = None
                    while demo.poll() is None:
                        table = subprocess.check_output(
                            ["ps", "-axo", "pid=,ppid=,comm="], text=True
                        )
                        pid = find_demo_pid(demo.pid, binary, table)
                        if pid is not None:
                            if discovered is None:
                                discovered = time.monotonic()
                            if time.monotonic() - discovered >= sample_delay:
                                break
                        time.sleep(0.1)
                    if demo.poll() is None and pid is not None:
                        trace = log_path.with_suffix(".sample.txt")
                        sample_command = [
                            "/usr/bin/sample",
                            str(pid),
                            str(sample_seconds),
                            "1",
                            "-mayDie",
                            "-file",
                            str(trace),
                        ]
                        sample_started = time.monotonic()
                        sampling = {
                            "command": [*sample_command[:-1], trace.name],
                            "seconds_after_launch": sample_started - launched,
                            "trace": trace.name,
                            "complete": False,
                        }
                        try:
                            result = subprocess.run(
                                sample_command,
                                capture_output=True,
                                text=True,
                                timeout=sample_seconds + 30,
                            )
                            sample_log = result.stdout + result.stderr
                            table = subprocess.check_output(
                                ["ps", "-axo", "pid=,ppid=,comm="], text=True
                            )
                            target_alive = find_demo_pid(demo.pid, binary, table) == pid
                            sampling.update(
                                {
                                    "exit_code": result.returncode,
                                    "target_alive_after_sample": target_alive,
                                    "complete": (
                                        result.returncode == 0
                                        and target_alive
                                        and trace.exists()
                                        and "Call graph:" in trace.read_text()
                                    ),
                                }
                            )
                        except (OSError, subprocess.SubprocessError) as error:
                            sample_log = str(error)
                            sampling["error"] = redact_sample_output(sample_log, root, trace)
                        log_path.with_suffix(".sample.log").write_text(
                            redact_sample_output(sample_log, root, trace)
                        )
                    else:
                        sampling = {"complete": False, "error": "Demo exited before sampling"}
                except (OSError, subprocess.SubprocessError) as error:
                    sampling = {"complete": False, "error": str(error)}
                    log_path.with_suffix(".sample.log").write_text(str(error))
            exit_code = demo.wait()
    return exit_code, sampling


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=TARGETS, default="IRPerfGrid")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "--cpu-sample-seconds",
        type=int,
        default=0,
        help="macOS call-stack sample duration; 0 disables sampling",
    )
    parser.add_argument(
        "--cpu-sample-delay",
        type=float,
        default=5.0,
        help="seconds after discovering this run’s demo before sampling",
    )
    parser.add_argument("demo_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.repeats < 1 or args.timeout < 1:
        parser.error("repeats and timeout must be positive")
    if (
        args.cpu_sample_seconds < 0
        or args.cpu_sample_delay < 0
        or not math.isfinite(args.cpu_sample_delay)
    ):
        parser.error("CPU sampling duration and delay must be finite and nonnegative")
    if args.cpu_sample_seconds and platform.system() != "Darwin":
        parser.error("CPU sampling currently requires macOS /usr/bin/sample")
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
        exit_code, sampling = run_profile(
            command, root, log_path, binary, args.cpu_sample_seconds, args.cpu_sample_delay
        )
        fresh = report.exists() and report.stat().st_mtime_ns >= started
        clean = exit_code == 0 and "RESULT=CLEAN" in log_path.read_text()
        run = {
            "index": index,
            "exit_code": exit_code,
            "cpu_sampling": sampling,
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
        if (
            not clean
            or not fresh
            or not run.get("gpu_measured")
            or (sampling is not None and not sampling["complete"])
        ):
            print(
                f"run {index}: failed or missing requested measurements; inspect {log_path}",
                flush=True,
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
