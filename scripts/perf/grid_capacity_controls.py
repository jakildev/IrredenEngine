#!/usr/bin/env python3
"""Profile GRID destination-capacity growth against a parent build, interleaved.

Four cases: {parent, head} x {plate, cube}. The plate is IRCanvasStress orbit 6
as an exact-fit 12 x 1 x 12 GRID set (it grows to hold its exposed cells); the
cube is the same orbit as the full 12^3 box (interior cells never grow it). The
set spins about +Y through at least one full revolution after the report's
warmup window; the camera stays fixed. Runs are capture-free Release profiles
through repeat_profile.py, forward on odd rounds and reverse on even rounds, each
case under its own IRREDEN_BUILD_DIR. Run under `ir-acquire benchmark`.

Per-system rows carry the report's mean / min / max only: the profile report
keeps a per-frame series for whole-frame time alone. Capacity readings come
from a separate untimed head run per fixture (`--grid-capacity-probe`).
"""

import argparse
import json
import os
import statistics
import subprocess
from pathlib import Path

from compare_perf_runs import parse_report
from profile_matrix import run_rounds, write_cases
from repeat_profile import percentile

FIXTURES = {"plate": ["--focus-exact-fit-plate"], "cube": []}
SIDES = ("parent", "head")
SYSTEM = "RebuildGridVoxels"
# Orbit 6 spins 1.5 x pi/450 rad per UPDATE tick: one revolution is 600 ticks.
TICKS_PER_REVOLUTION = 600


def cases() -> dict[str, list[str]]:
    return {f"{side}-{fixture}": flags for side in SIDES for fixture, flags in FIXTURES.items()}


def common_args(frames: int) -> list[str]:
    return ["--auto-profile", "--profile-frames", str(frames), "--only", "orbit",
            "--focus-orbit", "6", "--no-auto-rotate", "--pivot-origin", "--subdivisions", "1",
            "--zoom", "8", "--window-mode", "offscreen"]


def verify_builds(output: Path, selected: dict[str, list[str]]) -> dict[str, dict]:
    """One binary, shader set, power source and Release tree per side."""
    identities: dict[str, set] = {side: set() for side in SIDES}
    for name in selected:
        for path in (output / name).glob("round-*/manifest.json"):
            manifest = json.loads(path.read_text())
            identities[name.split("-", 1)[0]].add(
                (manifest["binary_sha256"], manifest["shader_sha256"], manifest["host_power"],
                 manifest["build_type"]))
    for side, seen in identities.items():
        if len(seen) != 1:
            raise ValueError(f"{side}: runs disagree on binary, shaders, power or build type")
        if next(iter(seen))[3] != "Release":
            raise ValueError(f"{side}: not a Release tree")
    if identities["parent"] == identities["head"]:
        raise ValueError("parent and head ran the same binary")
    return {side: dict(zip(("binary", "shaders", "power", "build_type"), next(iter(seen))))
            for side, seen in identities.items()}


def summarize(output: Path, selected: dict[str, list[str]]) -> list[dict]:
    rows = []
    for name in selected:
        reports = [parse_report(p, name) for p in sorted((output / name).glob("round-*/run-1.txt"))]
        if not reports:
            continue
        steady = [ms for report in reports for ms in report.steady_frame_times_ms()]
        systems = [report.system_by_name(SYSTEM) for report in reports]
        if any(system is None for system in systems):
            raise ValueError(f"{name}: a report has no {SYSTEM} row")
        ticks = [sum(report.frame_update_ticks[report.warmup_frames:]) for report in reports]
        if min(ticks) < TICKS_PER_REVOLUTION:
            raise ValueError(f"{name}: a steady window spans less than one revolution")
        rows.append(dict(
            case=name, runs=len(reports),
            frame_mean_ms=statistics.mean(r.frame.avg for r in reports),
            steady_mean_ms=statistics.mean(steady),
            steady_p95_ms=percentile(steady, 95), steady_p99_ms=percentile(steady, 99),
            system_mean_ms=statistics.mean(s.avg_ms for s in systems),
            system_min_ms=min(s.min_ms for s in systems),
            system_max_ms=max(s.max_ms for s in systems),
            min_steady_update_ticks=min(ticks)))
    lines = [
        f"| Case | Runs | Frame mean ms | Steady mean / p95 / p99 ms | {SYSTEM} mean "
        "(min–max) ms | Steady update ticks (min) |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {row['case']} | {row['runs']} | {row['frame_mean_ms']:.3f} | "
            f"{row['steady_mean_ms']:.3f} / {row['steady_p95_ms']:.3f} / "
            f"{row['steady_p99_ms']:.3f} | {row['system_mean_ms']:.4f} "
            f"({row['system_min_ms']:.4f}–{row['system_max_ms']:.4f}) | "
            f"{row['min_steady_update_ticks']} |")
    (output / "summary.md").write_text("\n".join(lines) + "\n")
    (output / "summary.json").write_text(json.dumps(rows, indent=2) + "\n")
    return rows


def probe(output: Path, build: Path, frames: int) -> dict[str, list[str]]:
    """Untimed head runs that record every change of the capacity readings."""
    readings = {}
    for fixture, flags in FIXTURES.items():
        path = output / f"probe-{fixture}.txt"
        command = ["fleet-run", "--build-dir", str(build), "--timeout", "240", "IRCanvasStress",
                   *common_args(frames)[1:], *flags, "--grid-capacity-probe", str(path)]
        run = subprocess.run(command, capture_output=True, text=True)
        if run.returncode != 0 or "RESULT=CLEAN" not in run.stdout + run.stderr:
            raise ValueError(f"probe run failed for {fixture}")
        readings[fixture] = dict(command=command, lines=path.read_text().splitlines())
    (output / "probe.json").write_text(json.dumps(readings, indent=2) + "\n")
    return readings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--parent-build", type=Path, required=True)
    parser.add_argument("--head-build", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=4)
    parser.add_argument("--frames", type=int, default=2000)
    args = parser.parse_args()
    if args.rounds < 1 or args.frames * 3 // 4 < TICKS_PER_REVOLUTION:
        parser.error("rounds must be positive and the steady window must cover one revolution")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    selected = cases()
    builds = {"parent": args.parent_build.resolve(), "head": args.head_build.resolve()}
    environments = {name: {**os.environ, "IRREDEN_BUILD_DIR": str(builds[name.split("-")[0]])}
                    for name in selected}
    common = common_args(args.frames)
    write_cases(output, args.rounds, common, selected, target="IRCanvasStress",
                builds={side: str(path) for side, path in builds.items()})

    def after_round():
        verify_builds(output, selected)
        summarize(output, selected)

    run_rounds(output, selected, common, args.rounds, after_round, environments,
               runner_options=("--target", "IRCanvasStress", "--timeout", "240"))
    probe(output, builds["head"], args.frames)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
