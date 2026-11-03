"""Recheck retained native raster controls, without boundary tolerance."""

import json
import tarfile
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent
FINE = ROOT.parent / "scatter-centered-projection" / "fine"
VARIANTS = {
    "captured_matrix": ((1284, 722), True, (2, 1, 1282, 721)),
    "direct_padded_reflected": ((1284, 722), True, (2, 1, 1282, 721)),
    "direct_unpadded_reflected": ((1280, 720), True, (0, 0, 1280, 720)),
    "direct_padded_unreflected": ((1284, 722), False, (2, 1, 1282, 721)),
}


def mismatch_count(first, second):
    return sum(pixel != (0, 0, 0) for pixel in
               ImageChops.difference(first, second).get_flattened_data())


def main():
    captures = {}
    for phase in ("before", "after"):
        for index in range(33):
            path = FINE / phase / f"shot-{index}.png"
            with Image.open(path) as image:
                full = image.convert("RGB")
            if full.size != (2560, 1440):
                raise ValueError(f"Unexpected output dimensions: {path}")
            game = full.resize((1280, 720), Image.Resampling.NEAREST)
            if mismatch_count(full, game.resize(full.size, Image.Resampling.NEAREST)):
                raise ValueError(f"Nonuniform game-pixel block: {path}")
            captures[phase, index] = game

    actual = {variant: [] for variant in VARIANTS}
    with tarfile.open(ROOT / "raw.tar.gz", "r|gz") as archive:
        for member in archive:
            path = Path(member.name)
            if path.suffix != ".rgba":
                continue
            variant = path.parts[0]
            dimensions, reflect, crop = VARIANTS[variant]
            index = int(path.stem.removeprefix("shot-"))
            raw = archive.extractfile(member).read()
            if len(raw) != dimensions[0] * dimensions[1] * 4:
                raise ValueError(f"Invalid raw target: {member.name}")
            reference = Image.frombytes("RGBA", dimensions, raw).convert("RGB")
            if reflect:
                reference = reference.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            reference = reference.crop(crop)
            row = {"index": index}
            for phase in ("before", "after"):
                row[phase] = mismatch_count(captures[phase, index], reference)
            actual[variant].append(row)
    for rows in actual.values():
        rows.sort(key=lambda row: row["index"])
        if [row["index"] for row in rows] != list(range(33)):
            raise ValueError("Missing or repeated raster output")
    expected = json.loads((ROOT / "comparison.json").read_text())
    if actual != expected:
        raise ValueError("Retained comparison does not reproduce")
    for variant, rows in actual.items():
        totals = {phase: sum(row[phase] for row in rows) for phase in ("before", "after")}
        print(f"{variant}: {totals}")
    for variant in ("captured_matrix", "direct_padded_reflected"):
        if any(row["after"] for row in actual[variant]):
            raise ValueError(f"Current renderer differs from native replay: {variant}")
        if not any(row["before"] for row in actual[variant]):
            raise ValueError(f"Former projection positive control did not fail: {variant}")


if __name__ == "__main__":
    main()
