"""Compare curved controls to retained references without changing reference files."""

import argparse
import io
import json
import subprocess
from pathlib import Path

from capture import ROOT
from PIL import Image, ImageChops


def positive(image):
    return image.point(lambda value: 255 if value else 0)


def blue(image):
    red, green, channel = image.split()
    return ImageChops.multiply(positive(ImageChops.subtract(channel, red)),
                               positive(ImageChops.subtract(channel, green)))


def count(mask):
    return mask.histogram()[255]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reference-ref", default="b09400aa2f436f73c382bc2bf944543ca2862dc7")
    args = parser.parse_args()
    report = json.loads((args.directory / "captures.json").read_text())
    references = ROOT / "creations/demos/canvas_stress/test/references/macos-debug"
    results = []
    for case in report["cases"]:
        prefix = ("cylinder_receiver_pitched" if case["case"].startswith("pitched")
                  else "cylinder_receiver")
        for angle, image_path in zip(range(0, 360, 45), case["images"], strict=True):
            reference = references / f"{prefix}_{angle:03}.png"
            retained = subprocess.check_output(
                ["git", "show", f"{args.reference_ref}:{reference.relative_to(ROOT).as_posix()}"],
                cwd=ROOT,
            )
            with Image.open(args.directory / image_path) as image, \
                    Image.open(io.BytesIO(retained)) as ref:
                image, ref = image.convert("RGB"), ref.convert("RGB")
                if image.size != ref.size:
                    raise ValueError("reference and captured dimensions differ")
                channels = ImageChops.difference(image, ref).split()
                changed = positive(ImageChops.lighter(
                    ImageChops.lighter(*channels[:2]), channels[2]))
                current_blue, reference_blue = blue(image), blue(ref)
                both_blue = ImageChops.multiply(current_blue, reference_blue)
                either_blue = ImageChops.lighter(current_blue, reference_blue)
                results.append({
                    "case": case["case"], "angle": angle, "changed": count(changed),
                    "both_blue": count(both_blue),
                    "changed_blue": count(ImageChops.multiply(changed, both_blue)),
                    "blue_coverage_changes": count(ImageChops.difference(
                        current_blue, reference_blue)),
                    "changed_outside_blue": count(ImageChops.subtract(changed, either_blue)),
                })
    (args.directory / "reference-comparison.json").write_text(json.dumps(
        {"reference_ref": args.reference_ref, "rows": results}, indent=2) + "\n")
    for name in (case["case"] for case in report["cases"]):
        rows = [row for row in results if row["case"] == name]
        print(name, {key: sum(row[key] for row in rows)
                     for key in ("changed", "changed_blue", "blue_coverage_changes",
                                 "changed_outside_blue")})


if __name__ == "__main__":
    main()
