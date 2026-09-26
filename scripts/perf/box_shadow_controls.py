#!/usr/bin/env python3
"""Profile analytic-box extent and coincident-caster pressure in CanvasStress."""

import argparse
from pathlib import Path

from rotation_controls import run_rounds, summarize, verify_artifacts, write_cases


def cases():
    return {
        f"span{span}-count{count}": [
            "--analytic-box-span", str(span), "--analytic-box-count", str(count),
        ]
        for span, count in ((18, 1), (128, 1), (512, 1), (18, 65))
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=3)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("rounds must be positive")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    selected = cases()
    common = [
        "--only", "shadowbox,floor", "--probe-analytic-box", "--no-spin",
        "--no-auto-rotate", "--pivot-origin", "--zoom", "1", "--subdivisions", "1",
        "--auto-profile", "--auto-screenshot", "30", "--sweep-frames", "2", "120",
    ]
    write_cases(output, args.rounds, common, selected)

    def after_round():
        verify_artifacts(output)
        summarize(output, selected)

    run_rounds(output, selected, common, args.rounds, after_round,
               runner_options=["--target", "IRCanvasStress"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
