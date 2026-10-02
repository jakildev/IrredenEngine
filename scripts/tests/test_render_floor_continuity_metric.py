"""Floor-share continuity metric: window selection, share reading, pass/fail."""

import importlib.machinery
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-floor-continuity-metric.py"
loader = importlib.machinery.SourceFileLoader("floor_continuity_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("floor_continuity_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)

LIT, SHADOWED = metric.DEFAULT_FLOOR_COLORS
SIDE = 10  # 100 pixels: one floor pixel is one percentage point


def write_capture(path, lit, shadowed=0, bpp=3, alpha=255, other=(200, 40, 40)):
    pixels = bytearray()
    for i in range(SIDE * SIDE):
        rgb = LIT if i < lit else SHADOWED if i < lit + shadowed else other
        pixels.extend(rgb)
        if bpp == 4:
            pixels.append(alpha)
    rmu.write_png(str(path), SIDE, SIDE, bytes(pixels), bpp)


def write_sweep(directory, floor_pct, first_index=1):
    for shot, pct in enumerate(floor_pct):
        write_capture(directory / f"screenshot_{first_index + shot:06d}.png", pct)


class FloorContinuityMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_cli(self, *args):
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.root), *args],
                                capture_output=True, text=True)
        return result.returncode, json.loads(result.stdout)

    def test_issue_window_examines_93_shots_of_the_720_sweep(self):
        shots = metric.shots_in_window(720, 290.0, 336.0)
        self.assertEqual(len(shots), 93)
        self.assertEqual((shots[0], shots[-1]), (580, 672))
        self.assertEqual(metric.shots_in_window(8, 90.0, 270.0), [2, 3, 4, 5, 6])
        with self.assertRaises(ValueError):
            metric.shots_in_window(720, 336.0, 290.0)

    def test_share_counts_both_floor_colours_only_and_ignores_alpha(self):
        for bpp, alpha in ((3, 255), (4, 255), (4, 0)):
            with self.subTest(bpp=bpp, alpha=alpha):
                path = self.root / f"share_{bpp}_{alpha}.png"
                write_capture(path, lit=30, shadowed=12, bpp=bpp, alpha=alpha)
                self.assertAlmostEqual(metric.floor_share(path, metric.DEFAULT_FLOOR_COLORS), 42.0)
        path = self.root / "near_miss.png"
        write_capture(path, lit=0, other=(96, 96, 103))
        self.assertEqual(metric.floor_share(path, metric.DEFAULT_FLOOR_COLORS), 0.0)

    def test_smooth_sweep_passes(self):
        write_sweep(self.root, [50, 0, 30, 31, 32, 33, 34, 0])
        code, result = self.run_cli("--shots", "8", "--yaw-min", "90", "--yaw-max", "270")
        self.assertEqual(code, 0, result)
        self.assertTrue(result["pass"])
        self.assertEqual(result["frames_examined"], 5)
        self.assertEqual(result["max_step_pp"], 1.0)

    def test_band_drop_inside_the_window_fails_and_is_located(self):
        write_sweep(self.root, [50, 50, 30, 31, 22, 22, 23, 50])
        code, result = self.run_cli("--shots", "8", "--yaw-min", "90", "--yaw-max", "270")
        self.assertEqual(code, 1, result)
        self.assertFalse(result["pass"])
        self.assertEqual(result["max_step_pp"], 9.0)
        worst = result["worst_steps"][0]
        self.assertEqual((worst["from_yaw"], worst["to_yaw"]), (135.0, 180.0))
        self.assertEqual(worst["images"], ["screenshot_000004.png", "screenshot_000005.png"])

    def test_threshold_is_strict(self):
        write_sweep(self.root, [0, 0, 30, 32, 32, 32, 32, 0])
        code, result = self.run_cli("--shots", "8", "--yaw-min", "90", "--yaw-max", "270")
        self.assertEqual(code, 1, result)
        code, result = self.run_cli("--shots", "8", "--yaw-min", "90", "--yaw-max", "270",
                                    "--max-step-pp", "2.5")
        self.assertEqual(code, 0, result)

    def test_newest_captures_form_the_sweep(self):
        write_sweep(self.root, [90, 0, 90, 0, 90, 0, 90, 0], first_index=1)
        write_sweep(self.root, [0, 0, 30, 30, 30, 30, 30, 0], first_index=9)
        code, result = self.run_cli("--shots", "8", "--yaw-min", "90", "--yaw-max", "270")
        self.assertEqual(code, 0, result)
        examined = {name for step in result["worst_steps"] for name in step["images"]}
        self.assertEqual(examined, {f"screenshot_{i:06d}.png" for i in range(11, 16)})

    def test_short_sweep_is_an_error_not_a_pass(self):
        write_sweep(self.root, [30, 30, 30])
        code, result = self.run_cli("--shots", "8")
        self.assertEqual(code, 2)
        self.assertIn("error", result)


if __name__ == "__main__":
    unittest.main()
