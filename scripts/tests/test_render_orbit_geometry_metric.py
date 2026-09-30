"""Fixed-pose orbit geometry oracle and structural-gate controls."""

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
import render_metric_util as util  # noqa: E402

SCRIPT = SCRIPTS / "render-orbit-geometry-metric.py"
loader = importlib.machinery.SourceFileLoader("orbit_geometry_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("orbit_geometry_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)


def put_game_pixel(pixels, gx, gy, rgb, scale):
    width = metric.GAME_SIZE[0] * scale
    for dy in range(scale):
        for dx in range(scale):
            x = scale * gx + dx
            y = scale * gy + dy
            offset = (y * width + x) * 3
            pixels[offset:offset + 3] = bytes(rgb)


def ideal_capture(scale):
    width = metric.GAME_SIZE[0] * scale
    height = metric.GAME_SIZE[1] * scale
    pixels = bytearray(width * height * 3)
    occupied = metric.occupied_cells()
    cross_section = {(x, z) for x, y, z in occupied if y == 0}
    x0, x1, y0, y1 = metric.GAME_WINDOW
    for gy in range(y0, y1):
        face = metric.ray_face(gy, cross_section)
        for gx in range(x0, x1):
            put_game_pixel(pixels, gx, gy, metric.expected_rgb(gx, face), scale)
    return pixels


class OrbitGeometryMetricTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def write(self, name, pixels, size):
        path = self.root / name
        width, height = size
        util.write_png(str(path), width, height, bytes(pixels), 3)
        return path

    def cli(self, path):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(path), "--max-mismatches", "0"],
            capture_output=True, text=True,
        )
        return result.returncode, json.loads(result.stdout)

    def test_inverse_resample_and_known_camera_rays(self):
        occupied = metric.occupied_cells()
        self.assertEqual(len(occupied), 1740)
        self.assertIn((0, 0, 0), occupied)
        self.assertNotIn((10, 0, 10), occupied)
        surface = {cell for cell in occupied if any(
            tuple(cell[i] + direction * (i == axis) for i in range(3))
            not in occupied
            for axis in range(3) for direction in (-1, 1))}
        self.assertEqual(len(surface), 610)
        section = {(x, z) for x, y, z in occupied if y == 0}
        self.assertEqual(len(section), 145)
        self.assertEqual(metric.ray_face(310, section), "z")
        self.assertEqual(metric.ray_face(330, section), "x")
        self.assertIsNone(metric.ray_face(293, section))
        self.assertEqual(metric.expected_rgb(701, "z"), (128, 128, 0))
        self.assertEqual(metric.expected_rgb(702, "z"), (0, 0, 0))

    def test_exact_capture_and_corruption_controls(self):
        for scale in (1, 2):
            with self.subTest(scale=scale):
                width = metric.GAME_SIZE[0] * scale
                height = metric.GAME_SIZE[1] * scale
                pixels = ideal_capture(scale)
                path = self.write(f"exact-{scale}.png", pixels, (width, height))
                code, result = self.cli(path)
                self.assertEqual(code, 0)
                self.assertTrue(result["pass"])
                self.assertEqual(result["output_scale"], scale)
                self.assertEqual(result["pixels"], 20586 * scale * scale)
                self.assertEqual(result["mismatches"], 0)

                # Each single-output-pixel corruption must fail: silhouette
                # excess, missing face, and wrong face color.
                edits = (
                    (702, 350, (255, 128, 128)),
                    (640, 330, (0, 0, 0)),
                    (640, 310, (255, 128, 128)),
                )
                for gx, gy, rgb in edits:
                    x, y = gx * scale, gy * scale
                    offset = (y * width + x) * 3
                    pixels[offset:offset + 3] = bytes(rgb)
                path = self.write(f"corrupt-{scale}.png", pixels, (width, height))
                code, result = self.cli(path)
                self.assertEqual(code, 1)
                self.assertFalse(result["pass"])
                self.assertEqual(result["mismatches"], 3)
                self.assertEqual(result["extras"], 1)
                self.assertEqual(result["missing"], 1)
                self.assertEqual(result["wrong_face_or_color"], 1)

    def test_empty_wrong_resolution_and_malformed_images(self):
        width, height = metric.GAME_SIZE
        empty = self.write("empty.png", bytearray(width * height * 3), (width, height))
        code, result = self.cli(empty)
        self.assertEqual(code, 1)
        self.assertGreater(result["missing"], 0)

        small = self.write("small.png", bytearray(4 * 4 * 3), (4, 4))
        code, result = self.cli(small)
        self.assertEqual(code, 2)
        self.assertIn("uniform positive integer output scale", result["error"])

        nonuniform = self.write("nonuniform.png", bytearray(2560 * 720 * 3),
                                (2560, 720))
        code, result = self.cli(nonuniform)
        self.assertEqual(code, 2)
        self.assertIn("uniform positive integer output scale", result["error"])

        malformed = self.root / "malformed.png"
        malformed.write_bytes(b"not a PNG")
        code, result = self.cli(malformed)
        self.assertEqual(code, 2)
        self.assertIn("error", result)


if __name__ == "__main__":
    unittest.main()
