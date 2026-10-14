"""Synthetic controls for the sprite orientation and voxel alignment metric."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-sprite-orientation-metric.py"

TOP_HALF = (255, 128, 0)
BOTTOM_HALF = (0, 255, 255)
FIELD = (0, 0, 0)
VOXEL = (255, 0, 255)
WIDTH = 12
HEIGHT = 10
SPRITE_COLUMNS = range(4, 8)


# Centred under SPRITE_COLUMNS, on the row below the sprite rows.
ALIGNED_VOXEL = frozenset({(5, 6), (6, 6)})
ALIGNED = "voxel_box=5,6,6,6 voxel_margins=1,1 voxel=aligned"


def write_capture(
        path: Path, sprite_rows: dict[int, tuple[int, int, int]], bpp: int = 3,
        voxel=ALIGNED_VOXEL) -> None:
    """A black field with ``voxel`` pixels and ``sprite_rows`` painted over the sprite's columns."""
    pixels = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            color = FIELD
            if (x, y) in voxel:
                color = VOXEL
            elif x in SPRITE_COLUMNS and y in sprite_rows:
                color = sprite_rows[y]
            pixels.extend(color)
            if bpp == 4:
                pixels.append(255)
    rmu.write_png(str(path), WIDTH, HEIGHT, bytes(pixels), bpp)


UPRIGHT_ROWS = {2: TOP_HALF, 3: TOP_HALF, 4: BOTTOM_HALF, 5: BOTTOM_HALF}
INVERTED_ROWS = {2: BOTTOM_HALF, 3: BOTTOM_HALF, 4: TOP_HALF, 5: TOP_HALF}


class SpriteOrientationMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def capture(self, name: str, sprite_rows, bpp: int = 3, voxel=ALIGNED_VOXEL) -> Path:
        path = self.root / name
        write_capture(path, sprite_rows, bpp, voxel)
        return path

    def run_cli(self, *paths: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), *(str(path) for path in paths)],
            capture_output=True,
            text=True,
        )

    def assert_cli(self, paths, returncode: int, lines: list[str]) -> None:
        completed = self.run_cli(*paths)
        self.assertEqual(completed.returncode, returncode, completed.stderr)
        result = "RESULT=PASS" if returncode == 0 else "RESULT=FAIL"
        expected = [f"{path}: {line}" for path, line in zip(paths, lines)] + [result]
        self.assertEqual(completed.stdout.splitlines(), expected)

    def test_upright_capture_passes(self):
        path = self.capture("upright.png", UPRIGHT_ROWS)
        self.assert_cli([path], 0, [f"orientation=upright sprite_box=4,2,7,5 {ALIGNED}"])

    def test_inverted_capture_fails(self):
        path = self.capture("inverted.png", INVERTED_ROWS)
        self.assert_cli([path], 1, [f"orientation=inverted sprite_box=4,2,7,5 {ALIGNED}"])

    def test_absent_fixture_colour_fails(self):
        top_only = self.capture("top_only.png", {2: TOP_HALF, 3: TOP_HALF})
        neither = self.capture("neither.png", {})
        self.assert_cli([top_only], 1, [
            "orientation=indeterminate sprite_box=4,2,7,3"
            " voxel_box=5,6,6,6 voxel_margins=1,1 voxel=misaligned"])
        self.assert_cli([neither], 1, [
            "orientation=indeterminate sprite_box=none"
            " voxel_box=5,6,6,6 voxel_margins=none voxel=misaligned"])

    def test_voxel_off_centre_fails(self):
        path = self.capture("off_centre.png", UPRIGHT_ROWS, voxel={(6, 6), (7, 6)})
        self.assert_cli([path], 1, [
            "orientation=upright sprite_box=4,2,7,5"
            " voxel_box=6,6,7,6 voxel_margins=2,0 voxel=misaligned"])

    def test_centred_voxel_wider_than_the_sprite_fails(self):
        path = self.capture(
            "too_wide.png", UPRIGHT_ROWS, voxel={(x, 6) for x in range(3, 9)})
        self.assert_cli([path], 1, [
            "orientation=upright sprite_box=4,2,7,5"
            " voxel_box=3,6,8,6 voxel_margins=-1,-1 voxel=misaligned"])

    def test_voxel_as_wide_as_the_sprite_is_aligned(self):
        path = self.capture(
            "flush.png", UPRIGHT_ROWS, voxel={(x, 6) for x in SPRITE_COLUMNS})
        self.assert_cli([path], 0, [
            "orientation=upright sprite_box=4,2,7,5"
            " voxel_box=4,6,7,6 voxel_margins=0,0 voxel=aligned"])

    def test_voxel_not_directly_below_the_sprite_fails(self):
        gap = self.capture("gap.png", UPRIGHT_ROWS, voxel={(5, 7), (6, 7)})
        overlap = self.capture("overlap.png", UPRIGHT_ROWS, voxel={(5, 1), (6, 1)})
        self.assert_cli([gap], 1, [
            "orientation=upright sprite_box=4,2,7,5"
            " voxel_box=5,7,6,7 voxel_margins=1,1 voxel=misaligned"])
        self.assert_cli([overlap], 1, [
            "orientation=upright sprite_box=4,2,7,5"
            " voxel_box=5,1,6,1 voxel_margins=1,1 voxel=misaligned"])

    def test_absent_voxel_fails(self):
        path = self.capture("no_voxel.png", UPRIGHT_ROWS, voxel=frozenset())
        self.assert_cli([path], 1, [
            "orientation=upright sprite_box=4,2,7,5"
            " voxel_box=none voxel_margins=none voxel=absent"])

    def test_empty_capture_list_fails(self):
        completed = self.run_cli()
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "RESULT=FAIL\n")

    def test_interleaved_rows_are_indeterminate(self):
        path = self.capture(
            "striped.png", {2: TOP_HALF, 3: BOTTOM_HALF, 4: TOP_HALF, 5: BOTTOM_HALF})
        self.assert_cli([path], 1, [f"orientation=indeterminate sprite_box=4,2,7,5 {ALIGNED}"])

    def test_one_inverted_capture_fails_the_set(self):
        upright = self.capture("upright.png", UPRIGHT_ROWS)
        inverted = self.capture("inverted.png", INVERTED_ROWS)
        self.assert_cli([upright, inverted], 1, [
            f"orientation=upright sprite_box=4,2,7,5 {ALIGNED}",
            f"orientation=inverted sprite_box=4,2,7,5 {ALIGNED}",
        ])
        self.assert_cli(
            [upright, upright], 0, [f"orientation=upright sprite_box=4,2,7,5 {ALIGNED}"] * 2)

    def test_one_misaligned_capture_fails_the_set(self):
        aligned = self.capture("aligned.png", UPRIGHT_ROWS)
        off_centre = self.capture("off_centre.png", UPRIGHT_ROWS, voxel={(4, 6), (5, 6)})
        self.assert_cli([aligned, off_centre], 1, [
            f"orientation=upright sprite_box=4,2,7,5 {ALIGNED}",
            "orientation=upright sprite_box=4,2,7,5"
            " voxel_box=4,6,5,6 voxel_margins=0,2 voxel=misaligned",
        ])

    def test_rgba_capture_matches_on_pixel_boundaries_only(self):
        # An opaque RGBA voxel pixel is (255, 0, 255, 255): its last three
        # bytes spell BOTTOM_HALF one byte off a pixel boundary. The alpha
        # byte of the field pixel left of a BOTTOM_HALF row spells VOXEL the
        # same way.
        path = self.capture("rgba.png", UPRIGHT_ROWS, bpp=4)
        self.assert_cli([path], 0, [f"orientation=upright sprite_box=4,2,7,5 {ALIGNED}"])

    def test_unreadable_capture_fails(self):
        path = self.root / "missing.png"
        completed = self.run_cli(path)
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(
            completed.stdout,
            f"{path}: orientation=indeterminate sprite_box=none"
            " voxel_box=none voxel_margins=none voxel=absent\nRESULT=FAIL\n")


if __name__ == "__main__":
    unittest.main()
