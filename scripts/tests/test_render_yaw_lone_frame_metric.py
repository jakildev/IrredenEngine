"""Yaw lone-frame metric: isolated discontinuities, smooth motion, and CLI gates."""

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

SCRIPT = SCRIPTS / "render-yaw-lone-frame-metric.py"
loader = importlib.machinery.SourceFileLoader("yaw_lone_frame_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("yaw_lone_frame_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)


def frame(values):
    return bytes(channel for value in values for channel in (value, value, value))


class YawLoneFrameMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write_sweep(self, frames, first_index=1, bpp=3):
        for shot, values in enumerate(frames):
            pixels = bytearray()
            for value in values:
                pixels.extend((value, value, value))
                if bpp == 4:
                    pixels.append(shot)
            rmu.write_png(
                str(self.root / f"screenshot_{first_index + shot:06d}.png"),
                len(values), 1, bytes(pixels), bpp)

    def run_cli(self, *args):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(self.root), *args],
            capture_output=True, text=True)
        return result.returncode, json.loads(result.stdout)

    def test_lone_frame_scores_above_identical_neighbours(self):
        frames = {index: frame([0] * 10) for index in range(5)}
        frames[2] = frame([255] * 4 + [0] * 6)
        self.assertEqual(metric.lone_frame_score(frames, 2, 5), 40.0)
        self.assertEqual(metric.lone_frame_score(frames, 1, 5), 0.0)
        self.assertEqual(metric.lone_frame_score(frames, 3, 5), 0.0)

    def test_smoothly_moving_edge_scores_at_parity(self):
        frames = {
            index: frame([255] * edge + [0] * (10 - edge))
            for index, edge in enumerate(range(1, 6))
        }
        self.assertEqual([metric.lone_frame_score(frames, i, 5) for i in (1, 2, 3)],
                         [0.0, 0.0, 0.0])

    def test_cli_fails_a_lone_frame_and_reports_three_scores(self):
        frames = [[0] * 10 for _ in range(8)]
        frames[2] = [255] * 4 + [0] * 6
        self.write_sweep(frames)
        code, result = self.run_cli("--shots", "8", "--yaws", "90")
        self.assertEqual(code, 1, result)
        row = result["rows"][0]
        self.assertEqual(row["score_pct"], 40.0)
        self.assertEqual((row["previous_score_pct"], row["following_score_pct"]),
                         (0.0, 0.0))
        self.assertFalse(row["pass"])

    def test_cli_passes_smooth_motion_and_ignores_alpha(self):
        frames = [[255] * edge + [0] * (10 - edge) for edge in range(1, 9)]
        self.write_sweep(frames, bpp=4)
        code, result = self.run_cli("--shots", "8", "--yaws", "90,135,180")
        self.assertEqual(code, 0, result)
        self.assertTrue(result["pass"])
        self.assertTrue(all(row["ratio"] == 0.0 for row in result["rows"]))

    def test_newest_captures_form_the_sweep(self):
        self.write_sweep([[0] * 10 for _ in range(8)], first_index=1)
        frames = [[0] * 10 for _ in range(8)]
        frames[2] = [255] * 10
        self.write_sweep(frames, first_index=9)
        code, result = self.run_cli("--shots", "8", "--yaws", "90")
        self.assertEqual(code, 1, result)
        self.assertEqual(result["rows"][0]["images"], [
            "screenshot_000010.png", "screenshot_000011.png", "screenshot_000012.png"])

    def test_yaw_must_land_on_a_frame(self):
        with self.assertRaises(ValueError):
            metric.yaw_index(10.0, 8)
        self.assertEqual(metric.yaw_index(315.0, 8), 7)


if __name__ == "__main__":
    unittest.main()
