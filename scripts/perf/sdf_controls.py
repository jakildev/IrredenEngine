#!/usr/bin/env python3
"""Profile direct SDF and baked occupancy across geometry and workload controls."""

import argparse
import json
from pathlib import Path

from profile_matrix import run_rounds, summarize, verify_artifacts, write_cases


def cases(suite: str = "baseline") -> dict[str, list[str]]:
    if suite == "qualification":
        return _qualification_cases()
    if suite != "baseline":
        raise ValueError(f"Unknown SDF control suite: {suite}")
    selected = {}
    for pose, yaw in (("0", "0"), ("45", "0.7853981633974483")):
        for shadows in ("on", "off"):
            for timing in ("on", "off"):
                for representation in ("direct", "baked"):
                    selected[f"{representation}-yaw{pose}-shadows-{shadows}-timing-{timing}"] = [
                        "--sdf-voxel-scene", representation, "--yaw", yaw,
                        "--config-preset", f"configs/perf/sdf-{timing}.lua",
                        *(["--no-shadows"] if shadows == "off" else []),
                    ]
    return selected


def _qualification_cases() -> dict[str, list[str]]:
    """Independent density, overlap, motion, presentation and lighting arms."""
    workloads = {}
    for representation, density in (("direct", 1), ("baked", 1), ("baked", 2)):
        workloads[f"density-{representation}-{density}"] = [
            "--sdf-voxel-scene", representation, "--sdf-voxel-density", str(density)]
    for representation in ("direct", "baked"):
        for copies in (1, 4, 16):
            for overlap in (False, True):
                name = f"population-{representation}-{copies}-overlap{int(overlap)}"
                workloads[name] = [
                    "--sdf-voxel-scene", representation, "--sdf-voxel-copies", str(copies),
                    *(["--sdf-voxel-overlap"] if overlap else []),
                ]
        for pose in ("root", "parented"):
            workloads[f"motion-{representation}-{pose}"] = [
                "--sdf-voxel-scene", representation, "--sdf-voxel-pose", pose,
                "--sdf-voxel-motion",
            ]
    for presentation in ("grid", "detached", "revoxelized"):
        for pose in ("root", "parented"):
            workloads[f"presentation-{presentation}-{pose}"] = [
                "--sdf-voxel-scene", "baked", "--sdf-voxel-presentation", presentation,
                "--sdf-voxel-pose", pose,
            ]
        workloads[f"local-light-only-{presentation}"] = [
            "--sdf-voxel-scene", "baked", "--sdf-voxel-presentation", presentation,
            "--no-shadows",
        ]
    selected = {}
    for name, arguments in workloads.items():
        for timing in ("on", "off"):
            selected[f"{name}-timing-{timing}"] = [
                *arguments, "--yaw", "0.7853981633974483",
                "--config-preset", f"configs/perf/sdf-{timing}.lua",
            ]
    return selected


def verify_configurations(output: Path) -> None:
    configurations = {}
    for path in output.glob("*/round-*/manifest.json"):
        manifest = json.loads(path.read_text())
        hashes = manifest.get("configuration_sha256")
        if not hashes:
            raise ValueError(f"{path}: no configuration fingerprint")
        for name, digest in hashes.items():
            if configurations.setdefault(name, digest) != digest:
                raise ValueError(f"{path}: configuration changed between cases: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--zoom", type=int, choices=(1, 2, 4, 8, 16), default=4)
    parser.add_argument("--suite", choices=("qualification", "baseline"),
                        default="qualification")
    parser.add_argument("--case", action="append", dest="case_names")
    args = parser.parse_args()
    if args.rounds < 1 or args.frames < 2:
        parser.error("rounds must be positive and frames must be at least two")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    selected = cases(args.suite)
    invalid = set(args.case_names or ()) - selected.keys()
    if invalid:
        parser.error(f"unknown {args.suite} cases: {', '.join(sorted(invalid))}")
    if args.case_names:
        selected = {name: selected[name] for name in args.case_names}
    common = ["--auto-profile", str(args.frames), "--zoom", str(args.zoom),
              "--sdf-voxel-blockers", "--no-ao", "--window-mode", "offscreen"]
    write_cases(output, args.rounds, common, selected, target="IRShapeDebug", suite=args.suite)

    def after_round():
        verify_artifacts(output)
        verify_configurations(output)
        summarize(output, selected, gpu_note=(
            "GPU stage rows are sampled timeline intervals, not additive exclusive costs. "
            "Timing-off cases retain the GPU frame envelope. Compare representations within "
            "the same pose, shadow and timing mode; finite occupancy differs from analytic "
            "geometry. The startup witness reports baker wall time and dense record payload; "
            "process peak/resident memory and driver allocation remain separate measurements."
        ))

    run_rounds(output, selected, common, args.rounds, after_round,
               runner_options=("--target", "IRShapeDebug"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
