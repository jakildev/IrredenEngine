#!/usr/bin/env python3
"""Compare direct SDF and baked occupancy with matched pose and timing controls.

The four-subject fixture is a representation probe, not a population benchmark.
Reports include warmup and steady windows; startup baking is not timed here.
"""

import argparse
import json
from pathlib import Path

from profile_matrix import run_rounds, summarize, verify_artifacts, write_cases


def cases() -> dict[str, list[str]]:
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
    args = parser.parse_args()
    if args.rounds < 1 or args.frames < 2:
        parser.error("rounds must be positive and frames must be at least two")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    selected = cases()
    common = ["--auto-profile", str(args.frames), "--zoom", str(args.zoom),
              "--sdf-voxel-blockers", "--no-ao", "--window-mode", "offscreen"]
    write_cases(output, args.rounds, common, selected, target="IRShapeDebug")

    def after_round():
        verify_artifacts(output)
        verify_configurations(output)
        summarize(output, selected, gpu_note=(
            "GPU stage rows are sampled timeline intervals, not additive exclusive costs. "
            "Timing-off cases retain the GPU frame envelope. Compare representations within "
            "the same pose, shadow and timing mode; finite occupancy differs from analytic "
            "geometry. Four subjects do not establish population scaling or Release performance."
        ))

    run_rounds(output, selected, common, args.rounds, after_round,
               runner_options=("--target", "IRShapeDebug"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
