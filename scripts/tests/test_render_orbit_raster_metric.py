"""Finite-face raster coverage and strict image-gate controls."""

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

SCRIPT = SCRIPTS / "render-orbit-raster-metric.py"
spec = importlib.util.spec_from_file_location("orbit_raster", SCRIPT)
metric = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metric)


class OrbitRasterMetricTest(unittest.TestCase):
    def test_shared_diagonal_has_exactly_one_owner_and_winding_is_irrelevant(self):
        corners = ((0, 0), (3, 0), (3, 3), (0, 3))
        a = set(metric.triangle_samples(corners[:3], 8, (4, 4)))
        b = set(metric.triangle_samples((corners[0], *corners[2:]), 8, (4, 4)))
        self.assertFalse(a & b)
        self.assertEqual(a | b, {(x, y) for x in range(3) for y in range(3)})
        self.assertEqual(a, set(metric.triangle_samples(corners[2::-1], 8, (4, 4))))
        self.assertEqual(list(metric.triangle_samples(((1, 1),) * 3, 8, (4, 4))), [])

    def test_subpixel_quantization_changes_boundary_ownership_not_filtering(self):
        # The left vertical edge rounds to x=0.5 only for the first triangle.
        a = ((0.5001, 0), (0.5001, 2), (2, 0))
        b = ((0.5021, 0), (0.5021, 2), (2, 0))
        self.assertIn((0, 0), set(metric.triangle_samples(a, 8, (3, 3))))
        self.assertNotIn((0, 0), set(metric.triangle_samples(b, 8, (3, 3))))

    def test_single_cube_world_normals_and_depth_occlusion(self):
        colors = metric.raster_faces({(0, 0, 0)}, 0.0, 8)
        self.assertEqual(colors[640, 360], (0, 128, 128))
        self.assertEqual(colors[639, 360], (128, 0, 128))
        self.assertEqual(colors[640, 358], (128, 128, 0))
        self.assertNotIn((640, 350), colors)
        # A cube translated along the view ray is entirely hidden.
        self.assertEqual(colors, metric.raster_faces({(0, 0, 0), (2, 2, 2)}, 0.0, 8))
        self.assertEqual(metric.raster_faces(set(), 0.0, 8), {})

    def test_image_gate_checks_every_output_pixel_and_rejects_corruption(self):
        yaw = 1.9634955
        colors = metric.raster_faces(metric.geometry.occupied_cells(), yaw, 8)
        scale = 2
        width, height = (v * scale for v in metric.geometry.GAME_SIZE)
        pixels = bytearray(width * height * 3)
        for (x, y), color in colors.items():
            for dy in range(scale):
                for dx in range(scale):
                    at = ((scale * y + dy) * width + scale * x + dx) * 3
                    pixels[at:at + 3] = bytes(color)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.png"
            util.write_png(str(path), width, height, bytes(pixels), 3)
            self.assertTrue(metric.measure(path, yaw, 8)["pass"])
            lit = sorted(colors)
            changes = ((0, 0, (128, 128, 0)),
                       (*lit[0], (0, 0, 0)), (*lit[-1], (255, 0, 255)))
            for x, y, color in changes:
                at = ((scale * y + 1) * width + scale * x + 1) * 3
                pixels[at:at + 3] = bytes(color)
            util.write_png(str(path), width, height, bytes(pixels), 3)
            result = subprocess.run([sys.executable, str(SCRIPT), str(path),
                                     "--yaw-radians", str(yaw), "--subpixel-bits", "8"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            report = json.loads(result.stdout)
            self.assertEqual(report["mismatches"], 3)
            self.assertEqual(report["counts"],
                             {"extras": 1, "missing": 1, "wrong_face_or_color": 1})

    def test_invalid_raster_parameters(self):
        for yaw, bits in ((float("nan"), 8), (float("inf"), 8), (0, 0), (0, 17)):
            with self.assertRaises(ValueError):
                metric.raster_faces({(0, 0, 0)}, yaw, bits)


if __name__ == "__main__":
    unittest.main()
