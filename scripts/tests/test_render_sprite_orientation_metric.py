"""Synthetic controls for the sprite-orientation metric."""

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


def write_capture(path: Path, sprite_rows: dict[int, tuple[int, int, int]], bpp: int = 3) -> None:
    """A black field with one voxel pixel and ``sprite_rows`` painted over the sprite's columns."""
    pixels = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            color = FIELD
            if (x, y) == (0, HEIGHT - 1):
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

    def capture(self, name: str, sprite_rows, bpp: int = 3) -> Path:
        path = self.root / name
        write_capture(path, sprite_rows, bpp)
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
        self.assert_cli([path], 0, ["orientation=upright sprite_box=4,2,7,5"])

    def test_inverted_capture_fails(self):
        path = self.capture("inverted.png", INVERTED_ROWS)
        self.assert_cli([path], 1, ["orientation=inverted sprite_box=4,2,7,5"])

    def test_absent_fixture_colour_fails(self):
        top_only = self.capture("top_only.png", {2: TOP_HALF, 3: TOP_HALF})
        neither = self.capture("neither.png", {})
        self.assert_cli([top_only], 1, ["orientation=indeterminate sprite_box=4,2,7,3"])
        self.assert_cli([neither], 1, ["orientation=indeterminate sprite_box=none"])

    def test_empty_capture_list_fails(self):
        completed = self.run_cli()
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "RESULT=FAIL\n")

    def test_interleaved_rows_are_indeterminate(self):
        path = self.capture(
            "striped.png", {2: TOP_HALF, 3: BOTTOM_HALF, 4: TOP_HALF, 5: BOTTOM_HALF})
        self.assert_cli([path], 1, ["orientation=indeterminate sprite_box=4,2,7,5"])

    def test_one_inverted_capture_fails_the_set(self):
        upright = self.capture("upright.png", UPRIGHT_ROWS)
        inverted = self.capture("inverted.png", INVERTED_ROWS)
        self.assert_cli([upright, inverted], 1, [
            "orientation=upright sprite_box=4,2,7,5",
            "orientation=inverted sprite_box=4,2,7,5",
        ])
        self.assert_cli([upright, upright], 0, ["orientation=upright sprite_box=4,2,7,5"] * 2)

    def test_rgba_capture_matches_on_pixel_boundaries_only(self):
        # An opaque RGBA voxel pixel is (255, 0, 255, 255): its last three
        # bytes spell BOTTOM_HALF one byte off a pixel boundary.
        path = self.capture("rgba.png", UPRIGHT_ROWS, bpp=4)
        self.assert_cli([path], 0, ["orientation=upright sprite_box=4,2,7,5"])

    def test_unreadable_capture_fails(self):
        path = self.root / "missing.png"
        completed = self.run_cli(path)
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(
            completed.stdout,
            f"{path}: orientation=indeterminate sprite_box=none\nRESULT=FAIL\n")


if __name__ == "__main__":
    unittest.main()
