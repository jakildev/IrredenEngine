"""Verify the retained native coverage controls without image tolerances."""
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read(name):
    with Image.open(ROOT / name) as image:
        return image.convert("RGB")


manifest = json.loads((ROOT / "manifest.json").read_text())
for name, expected in manifest["png_sha256"].items():
    require(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == expected, name)
for pose in ("cardinal", "cardinal90"):
    before = read(f"{pose}-parent.png")
    after = read(f"{pose}-corrected.png")
    require(before.size == after.size == (2560, 1440), "capture dimensions")
    require(before.getbbox() == (958, 540, 1600, 900), "clipping positive control")
    require(after.getbbox() == (256, 0, 2304, 1440), "full scene footprint")
    # The existing AO stencil reaches two world cells beyond a receiver.
    # At this fixture's zoom/output scale that is 32 display pixels.
    interior = (990, 572, 1568, 868)
    require(ImageChops.difference(before.crop(interior), after.crop(interior)).getbbox()
            is None, "interior beauty changed")
parent = read("diagonal-parent.png")
require(ImageChops.difference(parent, read("diagonal-corrected.png")).getbbox()
        is None, "rotated beauty changed")
require(ImageChops.difference(parent, read("rejected-shadow-window.png")).getbbox()
        is not None, "shadow-window positive control no longer discriminates")
require(ImageChops.difference(read("diagonal-parent-unlit.png"),
                             read("diagonal-candidate-unlit.png")).getbbox()
        is None, "rotated albedo changed")
before = read("cardinal-parent-unlit.png")
after = read("cardinal-candidate-unlit.png")
require(all(a == b for a, b in zip(after.getdata(), before.getdata()) if b != (0, 0, 0)),
        "previously visible cardinal albedo changed")
print("PASS: footprint, exact interior/rotation, albedo and positive controls")
