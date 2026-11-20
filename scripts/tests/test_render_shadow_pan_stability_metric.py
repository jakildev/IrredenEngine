"""Hermetic controls for the shadow pan-stability metric."""

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

SCRIPT = SCRIPTS / "render-shadow-pan-stability-metric.py"
loader = importlib.machinery.SourceFileLoader("shadow_pan_stability_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("shadow_pan_stability_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)

WIDTH = 64
HEIGHT = 32


def write_shadow(path: Path, left: int | None) -> None:
    pixels = bytearray(WIDTH * HEIGHT * 3)
    if left is not None:
        for y in range(8, 24):
            for x in range(left, left + 12):
                offset = (y * WIDTH + x) * 3
                pixels[offset:offset + 3] = bytes((255, 0, 255))
    rmu.write_png(str(path), WIDTH, HEIGHT, bytes(pixels), 3)


class ShadowPanStabilityMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_cli(self):
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(self.root),
                "--shots",
                "2",
                "--pan-to",
                "2",
                "0",
                "--zoom",
                "1",
                "--game-width",
                str(WIDTH),
            ],
            capture_output=True,
            text=True,
        )
        return result.returncode, json.loads(result.stdout)

    def test_known_camera_shift_passes(self):
        write_shadow(self.root / "screenshot_000001.png", 10)
        write_shadow(self.root / "screenshot_000002.png", 14)
        code, result = self.run_cli()
        self.assertEqual(code, 0, result)
        self.assertTrue(result["pass"])
        self.assertEqual(result["max_differing_shadow_pixels"], 0)
        self.assertEqual(result["largest_edge_displacement_px"], 0.0)

    def test_one_pixel_edge_shift_fails(self):
        write_shadow(self.root / "screenshot_000001.png", 10)
        write_shadow(self.root / "screenshot_000002.png", 15)
        code, result = self.run_cli()
        self.assertEqual(code, 1, result)
        self.assertFalse(result["pass"])
        self.assertGreater(result["max_differing_shadow_pixels"], 0)
        self.assertEqual(result["largest_edge_displacement_px"], 1.0)

    def test_blank_pair_is_invalid(self):
        write_shadow(self.root / "screenshot_000001.png", None)
        write_shadow(self.root / "screenshot_000002.png", None)
        code, result = self.run_cli()
        self.assertEqual(code, 2)
        self.assertIn("blank", result["error"])


if __name__ == "__main__":
    unittest.main()
