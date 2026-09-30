"""Normals-overlay metric controls for text and structural JSON output."""

import contextlib
import importlib.machinery
import importlib.util
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-normal-facing-metric.py"
loader = importlib.machinery.SourceFileLoader("normal_facing_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("normal_facing_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)


def write_capture(path, face_color):
    pixels = bytearray()
    for y in range(12):
        for x in range(12):
            pixels.extend(face_color if 3 <= x < 9 and 3 <= y < 9 else (0, 0, 0))
    rmu.write_png(str(path), 12, 12, bytes(pixels), 3)


class NormalFacingMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_cli(self, path, *args):
        return subprocess.run(
            [sys.executable, str(SCRIPT), str(path), "--yaw", "135", *args],
            capture_output=True, text=True)

    def test_default_text_and_measure_api_remain(self):
        path = self.root / "front.png"
        write_capture(path, (255, 128, 128))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertTrue(metric.measure(path, 135))
        expected = "front.png: yaw=135 occupied=36 back_facing=0 invalid_normals=0 PASS\n"
        self.assertEqual(output.getvalue(), expected)
        result = self.run_cli(path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, expected)

    def test_json_counts_backfaces_and_invalid_normals(self):
        for name, color, expected in (
            ("front", (255, 128, 128), (36, 0, 0, True)),
            ("back", (0, 128, 128), (36, 36, 0, False)),
            ("invalid", (255, 0, 0), (36, 0, 36, False)),
            ("empty", (0, 0, 0), (0, 0, 0, False)),
        ):
            with self.subTest(case=name):
                path = self.root / f"{name}.png"
                write_capture(path, color)
                result = self.run_cli(path, "--output-format", "json")
                report = json.loads(result.stdout)
                self.assertEqual(
                    (report["occupied"], report["back_facing"],
                     report["invalid_normals"], report["pass"]), expected)
                self.assertEqual(result.returncode, 0 if expected[3] else 1)
                self.assertEqual(report["yaw"], 135)
                self.assertEqual(report["image"], str(path))
                self.assertEqual("reason" in report, not expected[3])

    def test_json_reports_png_format_errors(self):
        path = self.root / "bad.png"
        path.write_bytes(b"not a png")
        result = self.run_cli(path, "--output-format", "json")
        self.assertEqual(result.returncode, 2)
        self.assertIn("error", json.loads(result.stdout))


if __name__ == "__main__":
    unittest.main()
