#!/usr/bin/env python3
"""Profile the million control across build tree, stage profiling and pose.

Every case runs once per round, forward then reverse (rotation_controls.py's
ordering), so host drift lands on every case instead of on the last group; the
summary prints each case's per-round means so the drift stays visible. Each run
goes through repeat_profile.py for freshness, fingerprints and the pose and
overflow-drop checks, which read each report's run witness, so a Release run
vouches for itself. No build is performed: build IRPerfGrid in every tree named
with --tree first.
"""

import argparse
import json
import os
import re
import statistics
from pathlib import Path

from compare_perf_runs import parse_report
from repeat_profile import cmake_build_type, overflow_failure, percentile, yaw_pose_mismatch
from rotation_controls import run_rounds, write_cases, write_gpu_summary

PRESETS = {
    "on": "configs/perf/million.lua",
    "off": "configs/perf/million-profiling-off.lua",
}
POSES = {"0": "0", "45": "0.785398163"}
COMMON = ["--wave-freeze", "--no-overlay"]
ROUND_RE = re.compile(r"round-(\d+)")
MILLION_ENTITIES = 1_000_000


def cases(builds: list[str]) -> dict[str, list[str]]:
    """Case name -> demo arguments, named <build>-profiling-<on|off>-yaw<deg>."""
    return {
        f"{build}-profiling-{profiling}-yaw{degrees}": [
            "--config-preset",
            preset,
            "--yaw",
            radians,
        ]
        for build in builds
        for profiling, preset in PRESETS.items()
        for degrees, radians in POSES.items()
    }


def span(values: list[float], digits: int = 2) -> str:
    return (
        f"{statistics.mean(values):.{digits}f} "
        f"({min(values):.{digits}f}–{max(values):.{digits}f})"
    )


def load(output: Path, name: str):
    """(manifest, report) per completed round of one case, in round order."""
    rounds = []
    found = [(ROUND_RE.fullmatch(d.name), d) for d in (output / name).glob("round-*")]
    for _, directory in sorted((int(m.group(1)), d) for m, d in found if m):
        report = directory / "run-1.txt"
        if report.exists():
            manifest = json.loads((directory / "manifest.json").read_text())
            rounds.append((manifest, parse_report(report, name)))
    return rounds


def conditions(output: Path, selected: dict[str, list[str]]) -> list[str]:
    """What the table was taken under: power source, head and one binary per build."""
    manifests = [manifest for name in selected for manifest, _ in load(output, name)]
    if not manifests:
        return []
    power = sorted({manifest["host_power"] or "unknown" for manifest in manifests})
    charge = [
        run["host_battery_percent"]
        for manifest in manifests
        for run in manifest["runs"]
        if run.get("host_battery_percent") is not None
    ]
    battery = f" (battery {max(charge)}% to {min(charge)}%)" if charge else ""
    loads = [
        run["host_load_1m"]
        for manifest in manifests
        for run in manifest["runs"]
        if run.get("host_load_1m") is not None
    ]
    load_text = (
        f" Host load at run start {min(loads):.1f} to {max(loads):.1f} "
        f"on {manifests[0].get('host_cpus')} CPUs."
        if loads
        else ""
    )
    heads = sorted({manifest["head"][:9] for manifest in manifests})
    binaries = sorted(
        {(manifest["build_type"], manifest["binary_sha256"][:16]) for manifest in manifests}
    )
    return [
        f"Power source: {', '.join(power)}{battery}.{load_text} Head: {', '.join(heads)}. "
        f"Shaders `{manifests[0]['shader_sha256'][:16]}`, "
        f"runtime scripts `{manifests[0]['runtime_scripts_sha256'][:16]}`. "
        + " ".join(f"{build} binary `{digest}`." for build, digest in binaries),
        "",
    ]


def summarize(output: Path, selected: dict[str, list[str]]) -> None:
    lines = conditions(output, selected) + [
        "| Case | Frame mean ms (round range) | Per-round means ms "
        "| Steady mean / p95 / p99 ms (frames pooled) | All-frame p99 ms "
        "| GPU frame envelope ms | Updates / frame | Yaw deg "
        "| Overflow max entries / dropped |",
        "|---|---:|---|---:|---:|---:|---:|---:|---:|",
    ]
    for name in selected:
        rounds = load(output, name)
        if not rounds:
            continue
        reports = [report for _, report in rounds]
        frames = [report.frame.avg for report in reports]
        envelopes = [
            metric.avg_ms
            for report in reports
            for metric in report.gpu_frame.metrics
            if metric.name == "envelope"
        ]
        ticks = [report.update_ticks_avg for report in reports]
        steady = [ms for r in reports for ms in r.steady_frame_times_ms()]
        steady_text = (
            f"{statistics.mean(steady):.2f} / {percentile(steady, 95):.2f} / "
            f"{percentile(steady, 99):.2f} ({len(steady)})"
            if steady
            else "—"
        )
        witnesses = [r.witness for r in reports]
        if any(w.yaw_first_deg is None or w.overflow_max_dropped is None for w in witnesses):
            yaw_text = overflow_text = "unwitnessed"
        else:
            yaw_text = "/".join(sorted({f"{w.yaw_first_deg:.3f}" for w in witnesses}))
            overflow_text = (
                f"{max(w.overflow_max_entries for w in witnesses)} / "
                f"{max(w.overflow_max_dropped for w in witnesses)}"
            )
        lines.append(
            f"| {name} | {span(frames)} | {' / '.join(f'{value:.2f}' for value in frames)} "
            f"| {steady_text} "
            f"| {min(r.frame.p99 for r in reports):.2f}–{max(r.frame.p99 for r in reports):.2f} "
            f"| {span(envelopes) if len(envelopes) == len(reports) else '—'} "
            f"| {span(ticks, 1) if all(t is not None for t in ticks) else '—'} "
            f"| {yaw_text} | {overflow_text} |"
        )
    (output / "summary.md").write_text("\n".join(lines) + "\n")

    write_gpu_summary(
        output / "gpu-summary.md",
        {name: [report for _, report in load(output, name)] for name in selected},
        "Sampled GPU invocation means from the stage-profiling-on cases; "
        "rows are not full-frame totals.",
    )


def verify_artifacts(output: Path, builds: list[str]) -> None:
    """One binary per build tree; one shader set, script set and power source overall."""
    binaries: dict[str, set[str]] = {build: set() for build in builds}
    shared = set()
    for path in output.glob("*/round-*/manifest.json"):
        manifest = json.loads(path.read_text())
        build = path.parts[-3].split("-profiling-")[0]
        binaries[build].add(manifest["binary_sha256"])
        shared.add(
            (
                manifest["shader_sha256"],
                manifest["runtime_scripts_sha256"],
                manifest["host_power"],
            )
        )
    if any(len(found) > 1 for found in binaries.values()):
        raise ValueError("A build tree's binary changed during the matrix")
    if len(shared) > 1:
        raise ValueError("Shaders, runtime scripts or the power source changed during the matrix")
    verify_cases(output)
    verify_poses(output)


def verify_cases(output: Path) -> None:
    """Each run is the million scene, on the arm its name says, from the build it says.

    A preset that failed to load is non-fatal and leaves the default 64³ scene
    with profiling on, and a Release build would not log it. The pose and the
    overflow loss come from the report's run witness, whatever the build logs.
    """
    for path in output.glob("*/round-*/manifest.json"):
        name = path.parts[-3]
        manifest = json.loads(path.read_text())
        report = parse_report(path.with_name("run-1.txt"), name)
        if report.entity_count < MILLION_ENTITIES:
            raise ValueError(f"{name}: {report.entity_count} entities; the preset did not load")
        profiled = "-profiling-on-" in name
        if bool(report.gpu_stages) != profiled:
            raise ValueError(f"{name}: GPU stage rows {'missing' if profiled else 'present'}")
        logged = manifest["runs"][0]["engine_logged"]
        if logged == (manifest["build_type"] == "Release"):
            raise ValueError(f"{name}: a {manifest['build_type']} build logged={logged}")
        pose = ["--yaw", POSES[name.rsplit("-yaw", 1)[1]]]
        for fault in (
            yaw_pose_mismatch("IRPerfGrid", pose, report.witness),
            overflow_failure("IRPerfGrid", pose, report.witness),
        ):
            if fault is not None:
                raise ValueError(f"{name}: {fault}")


def verify_poses(output: Path) -> None:
    """Every build's stage-profiling-on case at one pose must cull to the same counts.

    With the wave frozen the visible and per-axis counts are a fingerprint of the
    scene at a pose, so two trees that disagree were not built from one source.
    """
    fingerprints: dict[str, set] = {}
    for report_path in output.glob("*-profiling-on-yaw*/round-*/run-1.txt"):
        pose = report_path.parts[-3].rsplit("-yaw", 1)[1]
        cull = parse_report(report_path, pose).cull
        if cull.samples:
            fingerprints.setdefault(pose, set()).add((cull.avg_visible, cull.avg_axis_entries))
    for pose, found in fingerprints.items():
        if len(found) > 1:
            raise ValueError(f"yaw{pose}: builds disagree on the culled counts {sorted(found)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--tree",
        action="append",
        type=Path,
        help="configured build tree, repeatable (default: build); named by its CMAKE_BUILD_TYPE",
    )
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument(
        "--timeout",
        type=int,
        default=900,
        help="seconds per run, counted from the benchmark lock's acquisition",
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.rounds < 1 or args.frames < 1 or args.timeout < 1:
        parser.error("rounds, frames and timeout must be positive")
    root = Path(__file__).resolve().parents[2]
    trees = {}
    for tree in args.tree or [root / "build"]:
        build_type = cmake_build_type(tree.resolve())
        if build_type is None:
            parser.error(f"{tree} is not a configured tree with a CMAKE_BUILD_TYPE")
        if build_type.lower() in trees:
            parser.error(f"two trees are both {build_type}")
        trees[build_type.lower()] = tree.resolve()
    selected = cases(list(trees))
    common = [*COMMON, "--auto-profile", str(args.frames)]
    if args.dry_run:
        print(json.dumps({name: common + extra for name, extra in selected.items()}, indent=2))
        return 0
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    write_cases(
        output,
        args.rounds,
        common,
        selected,
        trees={build: os.path.relpath(tree, root) for build, tree in trees.items()},
    )
    environments = {
        name: {**os.environ, "IRREDEN_BUILD_DIR": str(trees[name.split("-profiling-")[0]])}
        for name in selected
    }

    def after_round():
        # Verify first: a summary of a contaminated round must say so.
        try:
            verify_artifacts(output, list(trees))
        except ValueError as error:
            summarize(output, selected)
            summary = output / "summary.md"
            summary.write_text(f"**ABORTED: {error}**\n\n" + summary.read_text())
            raise
        summarize(output, selected)

    run_rounds(
        output,
        selected,
        common,
        args.rounds,
        after_round,
        environments,
        ["--timeout", str(args.timeout)],
    )
    print(output / "summary.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
