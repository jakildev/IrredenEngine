#!/usr/bin/env python3
"""Copy a capture.py / timing.py output into this evidence directory.

Retains every receiver crop, outside-band image, config and gzipped log,
captures.json and the timing tables; full frames only for the cases named in
KEEP_FRAMES so the directory stays small.
"""

import argparse
import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
KEEP_FRAMES = {("head", "lower"), ("head", "stacked"), ("second", "stacked"),
               ("head", "oblique-stacked"), ("head", "far-stacked"),
               ("head", "moving-pose-1"), ("head", "moving-pose-2")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--timing", type=Path)
    args = parser.parse_args()
    report = json.loads((args.captures / "captures.json").read_text())
    shutil.copy2(args.captures / "captures.json", HERE / "captures.json")
    shutil.copy2(args.captures / "source.patch", HERE / "source.patch")
    for variant, data in report["variants"].items():
        for name, entry in data["cases"].items():
            src = args.captures / variant / name
            dst = HERE / variant / name
            dst.mkdir(parents=True, exist_ok=True)
            for path in src.iterdir():
                if path.suffix == ".png" and path.name.startswith("screenshot_"):
                    if (variant, name) in KEEP_FRAMES:
                        shutil.copy2(path, dst / path.name)
                    continue
                shutil.copy2(path, dst / path.name)
    if args.timing:
        timing = HERE / "timing"
        timing.mkdir(exist_ok=True)
        for name in ("timing.md", "matrix.json"):
            shutil.copy2(args.timing / name, timing / name)
        for summary in args.timing.glob("*/*/pair-*/summary.md"):
            rel = summary.relative_to(args.timing)
            (timing / rel.parent).mkdir(parents=True, exist_ok=True)
            shutil.copy2(summary, timing / rel)
            shutil.copy2(summary.with_name("manifest.json"), timing / rel.parent / "manifest.json")


if __name__ == "__main__":
    main()
