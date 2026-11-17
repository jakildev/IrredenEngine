"""Yaw crossing metric: receiver masks, the rotated-pair bound, and CLI gates."""

import importlib.machinery
import importlib.util
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-yaw-crossing-metric.py"
loader = importlib.machinery.SourceFileLoader("yaw_crossing_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("yaw_crossing_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)

WIDTH = 40
HEIGHT = 4
LIT = metric.LIT_COLOR
SHADOW = metric.SHADOW_COLOR
OTHER = (200, 10, 10)
LIMITS = ("--min-shadow-px", "8", "--min-receiver-px", "100")


class YawCrossingMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_frame(self, offset, shadow_columns, other_columns=(), bpp=3):
        """Every row: lit receiver, shadowed in ``shadow_columns``, foreign in ``other_columns``."""
        row = bytearray()
        for column in range(WIDTH):
            color = (OTHER if column in other_columns
                     else SHADOW if column in shadow_columns else LIT)
            row.extend(color)
            if bpp == 4:
                row.append(7)
        rmu.write_png(str(self.root / metric.frame_name(offset)), WIDTH, HEIGHT,
                      bytes(row) * HEIGHT, bpp)

    def write_edges(self, edges, **kwargs):
        """Shadow spans columns [10, edge) in the frame at each offset."""
        for offset, edge in zip(metric.OFFSETS, edges):
            self.write_frame(offset, range(10, edge), **kwargs)

    def run_cli(self, *args):
        """Grades the whole synthetic frame; the receiver-top region needs the fixture's size."""
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "grade", str(self.root), "--region", "frame", *args],
            capture_output=True, text=True)
        lines = result.stdout.splitlines()
        return result.returncode, json.loads(lines[0]), lines[1:]

    def test_smoothly_moving_edge_passes(self):
        self.write_edges([20, 21, 22, 23, 24])
        code, result, verdict = self.run_cli(*LIMITS)
        self.assertEqual(code, 0, result)
        self.assertEqual(verdict, ["RESULT=PASS"])
        self.assertEqual(result["cardinal_differing_px"], HEIGHT)
        self.assertEqual(result["rotated_differing_px"], HEIGHT)

    def test_cardinal_frame_on_another_model_fails(self):
        self.write_edges([20, 20, 26, 20, 20])
        code, result, verdict = self.run_cli(*LIMITS)
        self.assertEqual(code, 1, result)
        self.assertEqual(verdict, ["RESULT=FAIL"])
        self.assertEqual(result["rotated_differing_px"], 0)
        self.assertEqual(result["cardinal_differing_px"], 6 * HEIGHT)
        self.assertTrue(result["coverage_valid"])

    def test_one_sided_jump_fails(self):
        self.write_edges([20, 20, 20, 25, 25])
        code, result, _ = self.run_cli(*LIMITS)
        self.assertEqual(code, 1, result)
        self.assertEqual(result["pairs"]["cardinal_before"]["differing_px"], 0)
        self.assertEqual(result["pairs"]["cardinal_after"]["differing_px"], 5 * HEIGHT)

    def test_pixels_off_the_receiver_in_either_frame_are_not_compared(self):
        for offset in metric.OFFSETS:
            self.write_frame(offset, range(10, 20),
                             other_columns=range(18, 24) if offset == 0 else ())
        code, result, _ = self.run_cli(*LIMITS)
        self.assertEqual(code, 0, result)
        self.assertEqual(result["cardinal_differing_px"], 0)
        self.assertEqual(result["pairs"]["cardinal_before"]["common_px"],
                         (WIDTH - 6) * HEIGHT)

    def test_alpha_is_ignored(self):
        self.write_edges([20, 21, 22, 23, 24], bpp=4)
        code, result, _ = self.run_cli(*LIMITS)
        self.assertEqual(code, 0, result)

    def test_missing_shadow_is_not_a_pass(self):
        self.write_edges([10, 10, 10, 10, 10])
        code, result, verdict = self.run_cli(*LIMITS)
        self.assertEqual(code, 1, result)
        self.assertFalse(result["coverage_valid"])
        self.assertEqual(verdict, ["RESULT=FAIL"])

    def test_default_coverage_rejects_a_tiny_fixture(self):
        self.write_edges([20, 21, 22, 23, 24])
        code, result, _ = self.run_cli()
        self.assertEqual(code, 1, result)
        self.assertFalse(result["coverage_valid"])

    def test_missing_frame_is_unreadable(self):
        self.write_edges([20, 21, 22, 23, 24])
        (self.root / metric.frame_name(2)).unlink()
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "grade", str(self.root)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("yaw_+2.png", json.loads(result.stdout)["error"])

    def test_receiver_region_rejects_a_frame_of_another_size(self):
        self.write_edges([20, 21, 22, 23, 24])
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "grade", str(self.root)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("fixture requires 2560x1440", json.loads(result.stdout)["error"])

    def test_yaws_wrap_below_the_cardinal(self):
        self.assertAlmostEqual(metric.yaw_radians(0.0, 0.25, -1), math.radians(359.75))
        self.assertAlmostEqual(metric.yaw_radians(0.0, 0.25, 2), math.radians(0.5))
        self.assertAlmostEqual(metric.yaw_radians(270.0, 0.25, 0), math.radians(270.0))

    def test_env_pairs(self):
        self.assertEqual(metric.parse_env(["A=1", "B=x=y"]), {"A": "1", "B": "x=y"})
        with self.assertRaises(ValueError):
            metric.parse_env(["A"])


if __name__ == "__main__":
    unittest.main()
