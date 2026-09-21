#!/usr/bin/env python3
"""Repeat a fixed demo profile, retaining fresh reports, logs and run spread.

Pass demo arguments after --, including its auto-profile/exit options.
Optional macOS CPU sampling retains call stacks; sampled timings include its overhead.
The fleet runner owns benchmark resource coordination. No build is performed.
IRREDEN_BUILD_DIR selects the build tree (default build/), the same variable
fleet-run reads, so a Release tree profiles under its own binary fingerprint;
the manifest records the tree and its CMAKE_BUILD_TYPE.
"""

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import time
from pathlib import Path

from compare_perf_runs import RunWitness, parse_report

TARGETS = {"IRPerfGrid": "perf_grid", "IRCanvasStress": "canvas_stress"}
# The pose and the per-axis overflow loss are read from the profile report's
# run witness, which every build type writes; the log is not a witness, since
# IR_RELEASE compiles every log macro out.
YAW_POSE_TOLERANCE_DEG = 0.01
# Camera::kResidualYawDeadband: off a cardinal by more than this the camera
# rotates through the per-axis canvases, so the overflow lane must have been
# sampled. A test pins the value to camera.hpp.
RESIDUAL_YAW_DEADBAND_RAD = 1e-4
# A sweep's travelled arc is a sum of per-frame float32 yaw differences.
SWEEP_TRAVEL_TOLERANCE_DEG = 0.05
# IRPerfGrid's frame count for a bare --auto-profile.
DEFAULT_AUTO_PROFILE_FRAMES = 300
ENGINE_LOG_MARKERS = ("[EngineLog]", "[ClientLog]")


def directory_digest(directory: Path) -> str:
    if not directory.is_dir():
        raise FileNotFoundError(directory)
    digest = hashlib.sha256()
    for path in sorted(p for p in directory.rglob("*") if p.is_file()):
        name = path.relative_to(directory).as_posix().encode()
        digest.update(len(name).to_bytes(8, "little"))
        digest.update(name)
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def requested_radians(demo_args: list[str], flag: str) -> float | None:
    """The radians the demo will use for a flag (its last occurrence), else None.

    Raises ValueError for a value the pose check cannot compare: non-numeric
    or non-finite.
    """
    requested = None
    for index, argument in enumerate(demo_args):
        if argument == flag and index + 1 < len(demo_args):
            requested = float(demo_args[index + 1])
        elif argument.startswith(flag + "="):
            requested = float(argument.split("=", 1)[1])
    if requested is not None and not math.isfinite(requested):
        raise ValueError(f"{flag} {requested} is not finite")
    return requested


def requested_yaw(demo_args: list[str]) -> float | None:
    return requested_radians(demo_args, "--yaw")


def requested_yaw_step(demo_args: list[str]) -> float:
    """IRPerfGrid's per-rendered-frame yaw advance; 0.0 for a static pose."""
    return requested_radians(demo_args, "--yaw-step") or 0.0


def degrees_apart(left: float, right: float) -> float:
    apart = abs(left % 360.0 - right % 360.0)
    return min(apart, 360.0 - apart)


def shot_table_drives_camera(demo_args: list[str]) -> bool:
    """Every --auto-screenshot shot table sets the yaw and zoom of each shot.

    --yaw-ramp only selects which table; without --auto-screenshot it moves nothing.
    """
    return any(argument.split("=", 1)[0] == "--auto-screenshot" for argument in demo_args)


def checked_pose(target: str, demo_args: list[str]) -> str | None:
    """'static', 'sweep', or None where the tool cannot predict IRPerfGrid's poses."""
    if target != "IRPerfGrid" or shot_table_drives_camera(demo_args):
        return None
    if requested_yaw_step(demo_args) != 0.0:
        return "sweep"
    return "static" if requested_yaw(demo_args) is not None else None


def requested_frames(demo_args: list[str]) -> int | None:
    """How many frames --auto-profile asks IRPerfGrid to render; None if unreadable."""
    frames = None
    for index, argument in enumerate(demo_args):
        value = None
        if argument == "--auto-profile":
            following = demo_args[index + 1] if index + 1 < len(demo_args) else ""
            value = following if following.isdigit() else str(DEFAULT_AUTO_PROFILE_FRAMES)
        elif argument.startswith("--auto-profile="):
            value = argument.split("=", 1)[1]
        if value is not None:
            frames = int(value) if value.isdigit() else None
    return frames


def frame_yaw(demo_args: list[str], frame: int) -> float:
    """Radians IRPerfGrid renders 1-based frame N at: --yaw + (N - 1) * --yaw-step."""
    return (requested_yaw(demo_args) or 0.0) + (frame - 1) * requested_yaw_step(demo_args)


def off_cardinal(radians: float) -> bool:
    """True where the engine allocates the per-axis canvases for this yaw."""
    residual = radians % (math.pi / 2.0)
    return min(residual, math.pi / 2.0 - residual) > RESIDUAL_YAW_DEADBAND_RAD


def wrapped_step_deg(step: float) -> float:
    """The arc the witness adds per frame: it sums |wrapAnglePi(delta)|."""
    return abs(math.degrees((step + math.pi) % (2.0 * math.pi) - math.pi))


def yaw_pose_mismatch(target: str, demo_args: list[str], witness: RunWitness) -> str | None:
    """Why the poses the report witnessed contradict --yaw and --yaw-step, else None.

    Frame N renders at --yaw + (N - 1) * --yaw-step for the --auto-profile
    frame count, so the witness's sample count, first frame, last frame and
    travelled arc are all determined by the command line.
    """
    pose = checked_pose(target, demo_args)
    if pose is None:
        return None
    if witness.yaw_first_deg is None or witness.pose_samples == 0:
        return "the report witnessed no camera yaw"
    frames = requested_frames(demo_args)
    if frames is not None and witness.pose_samples != frames:
        return (
            f"the report witnessed {witness.pose_samples} frames; "
            f"--auto-profile asked for {frames}"
        )
    step = requested_yaw_step(demo_args)
    steps = witness.pose_samples - 1
    expected = (("first", 1), ("last", witness.pose_samples))
    for (label, frame), witnessed in zip(expected, (witness.yaw_first_deg, witness.yaw_last_deg)):
        expected_deg = math.degrees(frame_yaw(demo_args, frame)) % 360.0
        if not degrees_apart(expected_deg, witnessed) <= YAW_POSE_TOLERANCE_DEG:
            return (
                f"the {label} rendered frame should be at {expected_deg:.3f} deg "
                f"(--yaw {requested_yaw(demo_args) or 0.0} rad, --yaw-step {step} rad, "
                f"{witness.pose_samples} frames); it was at {witnessed:.3f} deg"
            )
    expected_travel = wrapped_step_deg(step) * steps
    tolerance = YAW_POSE_TOLERANCE_DEG if pose == "static" else SWEEP_TRAVEL_TOLERANCE_DEG
    if not abs(witness.yaw_travel_deg - expected_travel) <= tolerance:
        return (
            f"the camera yawed {witness.yaw_travel_deg:.3f} deg over {witness.pose_samples} "
            f"frames; --yaw-step {step} rad is {expected_travel:.3f} deg"
        )
    if witness.zoom_first != witness.zoom_last:
        return (
            f"the camera zoom went from {witness.zoom_first} to {witness.zoom_last} "
            "during the run"
        )
    return None


def overflow_failure(target: str, demo_args: list[str], witness: RunWitness) -> str | None:
    """Why the run's rotated coverage is incomplete or unwitnessed, else None."""
    if witness.overflow_max_dropped is None:
        return "the report witnessed no per-axis overflow counters"
    if witness.overflow_max_dropped > 0:
        return (
            f"the per-axis overflow list dropped up to {witness.overflow_max_dropped} "
            f"entries in a frame (cap {witness.overflow_cap})"
        )
    if checked_pose(target, demo_args) is not None and witness.overflow_samples == 0:
        frames = range(1, max(witness.pose_samples, 1) + 1)
        if any(off_cardinal(frame_yaw(demo_args, frame)) for frame in frames):
            return "a rotated pose never sampled the per-axis overflow counters"
    return None


def witness_checks(target: str, demo_args: list[str], witness: RunWitness) -> dict:
    return {
        "yaw_pose_mismatch": yaw_pose_mismatch(target, demo_args, witness),
        "overflow_failure": overflow_failure(target, demo_args, witness),
        "yaw_first_deg": witness.yaw_first_deg,
        "yaw_last_deg": witness.yaw_last_deg,
        "yaw_travel_deg": witness.yaw_travel_deg,
        "zoom_first": witness.zoom_first,
        "zoom_last": witness.zoom_last,
        "overflow_max_dropped": witness.overflow_max_dropped,
        "overflow_max_entries": witness.overflow_max_entries,
        "overflow_samples": witness.overflow_samples,
    }


def pose_text(run: dict) -> str:
    """'first (travel)' in degrees, or 'unwitnessed' for a report with no pose."""
    if run.get("yaw_first_deg") is None:
        return "unwitnessed"
    return f"{run['yaw_first_deg']:.3f} ({run['yaw_travel_deg']:.3f})"


def engine_logged(log_text: str) -> bool:
    """False for an IR_RELEASE build, which compiles every log macro out."""
    return any(marker in log_text for marker in ENGINE_LOG_MARKERS)


def percentile(values: list[float], percent: int) -> float:
    """The report writer's rule: the sorted value at index n * percent / 100."""
    ordered = sorted(values)
    return ordered[min(len(ordered) * percent // 100, len(ordered) - 1)]


def host_power_report() -> str | None:
    """pmset's battery report on macOS; None where it cannot be read."""
    if platform.system() != "Darwin":
        return None
    try:
        return subprocess.check_output(["pmset", "-g", "batt"], text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None


def host_power_source() -> str | None:
    """'AC Power' or 'Battery Power' on macOS; None where it cannot be read."""
    drawn = re.search(r"Now drawing from '([^']+)'", host_power_report() or "")
    return drawn.group(1) if drawn else None


def host_battery_percent() -> int | None:
    """Battery charge on macOS; None where it cannot be read or there is no battery."""
    charge = re.search(r"\t(\d+)%;", host_power_report() or "")
    return int(charge.group(1)) if charge else None


def host_load() -> float | None:
    """One-minute load average; None where the platform has none.

    The benchmark lock excludes cooperating builds only, so other work on the
    host is a condition of the measurement and is recorded with it. It is read
    when the run returns: a run can queue on the lock for minutes before it
    measures, and the minute before it ends is the minute it ran in.
    """
    try:
        return round(os.getloadavg()[0], 2)
    except (AttributeError, OSError):
        return None


def cmake_build_type(build_dir: Path) -> str | None:
    """CMAKE_BUILD_TYPE of a configured tree, from its cache."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text().splitlines():
        if line.startswith("CMAKE_BUILD_TYPE:"):
            return line.split("=", 1)[1] or None
    return None


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
    try:
        requested_yaw(demo_args)
        requested_yaw_step(demo_args)
    except ValueError as error:
        parser.error(f"--yaw and --yaw-step must be finite numbers of radians: {error}")
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = ["fleet-run", "--timeout", str(args.timeout), args.target, *demo_args]
    build_dir = Path(os.environ.get("IRREDEN_BUILD_DIR", root / "build")).resolve()
    binary = build_dir / "creations/demos" / TARGETS[args.target] / args.target
    manifest = {
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "changes": subprocess.check_output(["git", "status", "--short"], cwd=root, text=True),
        "host": platform.platform(),
        "host_power": host_power_source(),
        "host_cpus": os.cpu_count(),
        "build_dir": os.path.relpath(build_dir, root),
        "build_type": cmake_build_type(build_dir),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "shader_sha256": directory_digest(binary.parent / "shaders"),
        "runtime_scripts_sha256": directory_digest(binary.parent / "scripts"),
        "command": command,
        "runs": [],
    }
    report = build_dir / "creations/demos" / TARGETS[args.target] / "save_files/profile_report.txt"
    rows = []
    for index in range(1, args.repeats + 1):
        started = time.time_ns()
        battery_percent = host_battery_percent()
        log_path = output / f"run-{index}.log"
        exit_code, sampling = run_profile(
            command, root, log_path, binary, args.cpu_sample_seconds, args.cpu_sample_delay
        )
        fresh = report.exists() and report.stat().st_mtime_ns >= started
        log_text = log_path.read_text()
        clean = exit_code == 0 and "RESULT=CLEAN" in log_text
        run = {
            "index": index,
            "launched_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started / 1e9)),
            "finished_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "host_battery_percent": battery_percent,
            "host_load_1m": host_load(),
            "exit_code": exit_code,
            "cpu_sampling": sampling,
            "fresh_report": fresh,
            "clean": clean,
            "engine_logged": engine_logged(log_text),
        }
        manifest["runs"].append(run)
        if fresh:
            destination = output / f"run-{index}.txt"
            shutil.copy2(report, destination)
            row = parse_report(destination, f"run-{index}")
            run.update(witness_checks(args.target, demo_args, row.witness))
            run["gpu_measured"] = (
                any(stage.avg_ms > 0 for stage in row.gpu_stages)
                or (row.gpu_frame.supported is True and row.gpu_frame.valid > 0)
            )
            rows.append(row)
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if (
            not clean
            or not fresh
            or not run.get("gpu_measured")
            or run["yaw_pose_mismatch"] is not None
            or run["overflow_failure"] is not None
            or (sampling is not None and not sampling["complete"])
        ):
            reasons = [run.get("yaw_pose_mismatch"), run.get("overflow_failure")]
            print(
                f"run {index}: failed or missing requested measurements"
                f"{''.join(f'; {reason}' for reason in reasons if reason)}; inspect {log_path}",
                flush=True,
            )
            return 1
        print(
            f"run {index}: frame avg {rows[-1].frame.avg:.3f} ms; CPU/GPU report retained",
            flush=True,
        )

    metrics = {
        "frame avg": [row.frame.avg for row in rows],
        "frame p95": [row.frame.p95 for row in rows],
        "frame p99": [row.frame.p99 for row in rows],
    }
    if all(row.steady_frame is not None for row in rows):
        metrics.update(
            {
                "steady frame avg": [row.steady_frame.avg for row in rows],
                "steady frame p95": [row.steady_frame.p95 for row in rows],
                "steady frame p99": [row.steady_frame.p99 for row in rows],
            }
        )
    frame_names = sorted({metric.name for row in rows for metric in row.gpu_frame.metrics})
    for name in frame_names:
        values = [next((m.avg_ms for m in row.gpu_frame.metrics if m.name == name), None)
                  for row in rows]
        if all(value is not None for value in values):
            metrics[f"GPU frame {name}"] = values
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
    steady = [ms for row in rows for ms in row.steady_frame_times_ms()]
    if steady:
        lines.extend(["",
                      "| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms "
                      "| Max ms |",
                      "|---|---:|---:|---:|---:|---:|",
                      f"| first {rows[0].warmup_frames} of each run excluded | {len(steady)} "
                      f"| {statistics.mean(steady):.3f} | {percentile(steady, 95):.3f} "
                      f"| {percentile(steady, 99):.3f} | {max(steady):.3f} |"])
    ticks = [row.update_ticks_avg for row in rows]
    if all(value is not None for value in ticks):
        lines.extend(["",
                      "| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |",
                      "|---|---:|---:|---:|",
                      f"| update ticks | {statistics.mean(ticks):.2f} "
                      f"| {min(ticks):.2f}–{max(ticks):.2f} "
                      f"| {max(row.update_ticks_max for row in rows)} |"])
    lines.extend(["",
                  "| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers "
                  "| Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |",
                  "|---|---|---:|---:|---:|---:|---:|"])
    for run, row in zip(manifest["runs"], rows):
        coverage = row.gpu_frame
        lines.append(f"| {run['index']} | {coverage.supported} "
                     f"| {coverage.valid} / {coverage.attempted} "
                     f"| {coverage.invalid} | {coverage.command_buffers} "
                     f"| {pose_text(run)} "
                     f"| {run['overflow_max_entries']} / {run['overflow_max_dropped']} "
                     f"({run['overflow_samples']}) |")
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    print(output / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
