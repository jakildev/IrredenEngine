"""Read immutable Windows references and retain exact changed pixels/crops."""

import argparse
import io
import json
import subprocess
from pathlib import Path

from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("--repo", type=Path, default=Path.cwd())
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
ref = "fef7e27fd6cf6f1b8ff1abf416d077dd62d218e5"
results = {}
for name in (
    "revoxelize_solids",
    "so3_grid_cross",
    "so3_offsnap_disc",
    "so3_offsnap_wide",
    "so3_smooth_sweep",
    "so3_wide_parity",
):
    path = f"creations/demos/canvas_stress/test/references/windows-debug/{name}.png"
    images = []
    for version, label in ((ref + "^", "before"), (ref, "after")):
        data = subprocess.run(
            ["git", "-C", str(args.repo), "show", version + ":" + path],
            capture_output=True,
            check=True,
        ).stdout
        images.append(Image.open(io.BytesIO(data)).convert("RGB"))
        (args.output / f"{name}-{label}.png").write_bytes(data)
    changes = []
    for y in range(images[0].height):
        for x in range(images[0].width):
            before = images[0].getpixel((x, y))
            after = images[1].getpixel((x, y))
            if before == after:
                continue
            neighborhood = sorted(images[0].crop((x - 2, y - 2, x + 3, y + 3)).getcolors())
            changes.append(
                dict(x=x, y=y, before=before, after=after, before_neighborhood_colors=neighborhood)
            )
            if name == "revoxelize_solids":
                for image, label in zip(images, ("before", "after")):
                    image.crop((x - 8, y - 8, x + 9, y + 9)).resize(
                        (272, 272), Image.Resampling.NEAREST
                    ).save(args.output / f"point-{x}-{y}-{label}.png")
    results[name] = changes
    print(f"{name}: {len(changes)} changed pixels")
(args.output / "windows-reference-diff.json").write_text(json.dumps(results, indent=2))
