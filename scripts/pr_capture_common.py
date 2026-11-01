"""Shared helpers for PR evidence capture drivers under docs/pr-screenshots/.

A driver stages a git ref's shaders into a build's runtime shader directory,
restores the working-tree copies afterwards, hashes what it ran, and grades a
receiver crop against a control crop. Pillow is imported only inside
``grade_crops`` so the staging helpers stay importable on a bare host.
"""

import hashlib
import subprocess
from pathlib import Path


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def stage_shaders(shader_dir, names, subdir, ref="origin/master", root=None):
    """Overwrite ``names`` under ``shader_dir`` with ``ref``'s copies; return the saved bytes."""
    saved = {}
    for name in names:
        path = Path(shader_dir) / name
        saved[name] = path.read_bytes()
        path.write_bytes(subprocess.check_output(
            ["git", "show", f"{ref}:engine/render/src/shaders/{subdir}{name}"], cwd=root))
    return saved


def restore_shaders(shader_dir, saved):
    for name, data in saved.items():
        (Path(shader_dir) / name).write_bytes(data)


def grade_crops(actual, control, output, band):
    """Changed receiver pixels, split by direction, outside ``band`` px of control edges.

    ``missing`` counts pixels brighter than the control (shadow lost),
    ``excess`` pixels darker (shadow gained); ``output`` receives the
    outside-band mask when any pixel changed there.
    """
    from PIL import Image, ImageChops, ImageFilter

    with Image.open(actual) as a, Image.open(control) as c:
        a, c = a.convert("L"), c.convert("L")
        edges = c.filter(ImageFilter.FIND_EDGES).point(lambda v: 255 if v else 0)
        halo = edges.filter(ImageFilter.MaxFilter(2 * band + 1))
        diff = ImageChops.difference(a, c).point(lambda v: 255 if v else 0)
        darker = ImageChops.subtract(c, a).point(lambda v: 255 if v else 0)
        brighter = ImageChops.subtract(a, c).point(lambda v: 255 if v else 0)
        outside = ImageChops.subtract(diff, halo)
        if outside.getbbox():
            outside.save(output)

        def count(image):
            return sum(1 for v in image.tobytes() if v)
        return {"changed": count(diff), "changed_outside_band": count(outside),
                "excess_shadow_outside_band": count(ImageChops.subtract(darker, halo)),
                "missing_shadow_outside_band": count(ImageChops.subtract(brighter, halo))}
