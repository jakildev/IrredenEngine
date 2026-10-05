#!/usr/bin/env python3
"""Compare GPU counter envelopes with synchronized whole-system timing.

Synchronized timing finishes outstanding GPU work before and after tagged
systems. It changes scheduling, includes CPU encoding/waits, and disables
GPU substage rows; its values are attribution probes, not shipping frame costs.
Run under ir-acquire benchmark. No engine source or binary is changed.
"""

import argparse
from pathlib import Path

from compare_perf_runs import parse_report
from rotation_controls import run_rounds, summarize, verify_artifacts, write_cases


def cases(output: Path, pose: str) -> dict[str, list[str]]:
    selected = {}
    poses = {"low": ("1", "1", "0"), "dense": ("4", "4", "0"),
             "rotated": ("4", "4", "0.7853981633974483")}
    for name, (zoom, base, yaw) in poses.items():
        if pose not in (name, "all"):
            continue
        for mode in ("counters", "synchronized"):
            selected[f"{name}-{mode}"] = [
                "--zoom", zoom, "--base-subdivisions", base, "--yaw", yaw,
                "--config-preset", str(output / f"{mode}.lua"),
            ]
    return selected


def verify_timing_modes(output: Path, selected: dict[str, list[str]]) -> None:
    for name in selected:
        paths = list((output / name).glob("round-*/run-1.txt"))
        if not paths:
            raise ValueError(f"{name}: missing timing reports")
        for path in paths:
            report = parse_report(path, name)
            light = report.gpu_by_name("computeLightVolume")
            if light is None or not light.samples:
                raise ValueError(f"{path}: missing sampled whole-system light timing")
            substage = report.gpu_by_name("voxelCompact")
            has_substage = substage is not None and bool(substage.samples)
            if has_substage != name.endswith("-counters"):
                raise ValueError(f"{path}: GPU substage presence disagrees with timing mode")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pose", choices=("all", "low", "dense", "rotated"), default="all")
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--frames", type=int, default=120)
    args = parser.parse_args()
    if args.rounds < 1 or args.frames < 1:
        parser.error("rounds and frames must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[2]
    preset = (root / "creations/demos/perf_grid/configs/perf/million.lua").read_text()
    for mode in ("counters", "synchronized"):
        legacy = "true" if mode == "synchronized" else "false"
        (output / f"{mode}.lua").write_text(
            preset + f"\nconfig.gpu_stage_timing_legacy = {legacy}\n")
    selected = cases(output, args.pose)
    common = [
        "--mode", "voxel_set", "--wave-freeze", "--auto-profile", str(args.frames),
        "--grid-size", "64", "--subdivision-mode", "full", "--wave-amplitude", "5",
        "--pivot-origin", "--no-overlay", "--window-mode", "offscreen",
    ]
    write_cases(output, args.rounds, common, selected)

    def after_round():
        verify_artifacts(output)
        verify_timing_modes(output, selected)
        summarize(output, selected, gpu_note=(
            "Counter rows are GPU timeline envelopes. Synchronized rows are "
            "CPU wall-clock intervals around finish-bracketed systems, including "
            "encoding and waits. They disable GPU substage samples and alter scheduling; "
            "neither kind establishes exclusive GPU busy time or a shipping speedup."
        ))

    run_rounds(output, selected, common, args.rounds, after_round)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
