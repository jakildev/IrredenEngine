"""View-coverage metric: sweep selection, background share, pass/fail."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-view-coverage-metric.py"
SIDE = 10  # 100 pixels: one background pixel is one percentage point
FLOOR = (96, 104, 128)


def write_capture(path, background_pixels, background=(0, 0, 0), bpp=3):
    pixels = bytearray()
    for i in range(SIDE * SIDE):
        pixels.extend(background if i < background_pixels else FLOOR)
        if bpp == 4:
            pixels.append(255)
    rmu.write_png(str(path), SIDE, SIDE, bytes(pixels), bpp)


class ViewCoverageMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_sweep(self, background_pixels, first_index=1):
        for shot, count in enumerate(background_pixels):
            write_capture(self.root / f"screenshot_{first_index + shot:06d}.png", count)

    def run_cli(self, *args):
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.root), *args],
                                capture_output=True, text=True)
        return result.returncode, json.loads(result.stdout)

    def test_a_fully_covered_sweep_passes(self):
        self.write_sweep([0] * 19)
        code, result = self.run_cli()
        self.assertEqual(code, 0)
        self.assertTrue(result["pass"])
        self.assertEqual(result["frames_examined"], 19)
        self.assertEqual(result["max_background_pct"], 0.0)

    def test_one_shot_with_dropped_content_fails_and_is_named(self):
        self.write_sweep([0] * 7 + [44] + [0] * 11)
        code, result = self.run_cli()
        self.assertEqual(code, 1)
        self.assertEqual(result["frames_over_threshold"], 1)
        self.assertEqual(result["max_background_pct"], 44.0)
        self.assertEqual(result["worst_frames"][0]["image"], "screenshot_000008.png")

    def test_only_the_newest_shots_are_the_sweep(self):
        # An older, fully black run left in the directory must not be graded.
        self.write_sweep([100] * 4)
        self.write_sweep([0] * 19, first_index=5)
        code, result = self.run_cli()
        self.assertEqual(code, 0)
        self.assertEqual(result["frames_examined"], 19)

    def test_threshold_and_background_colour_are_configurable(self):
        for shot in range(3):
            write_capture(self.root / f"screenshot_{shot + 1:06d}.png", 2,
                          background=(10, 20, 30), bpp=4)
        self.assertEqual(self.run_cli("--shots", "3")[0], 0)
        code, result = self.run_cli("--shots", "3", "--background", "10,20,30")
        self.assertEqual(code, 1)
        self.assertEqual(result["max_background_pct"], 2.0)
        code, _ = self.run_cli("--shots", "3", "--background", "10,20,30",
                               "--max-background-pct", "2.0")
        self.assertEqual(code, 0)

    def test_a_short_sweep_is_an_error_not_a_pass(self):
        self.write_sweep([0] * 5)
        code, result = self.run_cli()
        self.assertEqual(code, 2)
        self.assertIn("error", result)


if __name__ == "__main__":
    unittest.main()
