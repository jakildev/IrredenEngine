"""Paired face/coverage controls for the bounded authored LOD fixture gate."""

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as util  # noqa: E402

SCRIPT = SCRIPTS / "render-lod-dense-face-metric.py"
SPEC = importlib.util.spec_from_file_location("lod_dense_metric", SCRIPT)
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)
FLOOR = (120, 125, 135)
BLUE = (80, 165, 215)


def put(pixels, position, rgb, scale=1):
    x, y = position
    width = METRIC.GAME_SIZE[0] * scale
    for dy in range(scale):
        for dx in range(scale):
            offset = ((y * scale + dy) * width + x * scale + dx) * 3
            pixels[offset:offset + 3] = bytes(rgb)


def pair(scale=1):
    width, height = (v * scale for v in METRIC.GAME_SIZE)
    normals = bytearray(bytes(METRIC.NORMAL_RGB[2]) * width * height)
    material = bytearray(bytes(FLOOR) * width * height)
    for point, axis in METRIC.expected_faces().items():
        put(normals, point, METRIC.NORMAL_RGB[axis], scale)
        put(material, point, BLUE, scale)
    return (width, height, 3, normals), (width, height, 3, material)


class LodDenseFaceMetricTest(unittest.TestCase):
    def test_native_pair_matches_independent_finite_faces(self):
        evidence = (SCRIPTS.parent / "docs/pr-screenshots/codex/authored-lod-scene"
                    / "finite-face-proof")
        result = METRIC.measure(evidence / "normals-yaw180.png",
                                evidence / "material-yaw180.png")
        self.assertTrue(result["pass"], result)
        self.assertEqual(result["expected_material_pixels"], 17664)

    def test_fixed_geometry_and_known_face_witnesses(self):
        cells = METRIC.occupied_cells()
        self.assertEqual(len(cells), 136)
        self.assertIn((-12.5, -17.5, -3.0), cells)
        self.assertNotIn((-10.5, -15.5, -1.0), cells)
        faces = METRIC.expected_faces()
        self.assertEqual(Counter(faces.values()), {0: 1472, 1: 1472, 2: 1472})
        self.assertNotIn((545, 410), faces)
        # Explicit witnesses protect the axis mapping independently of the
        # synthetic writer below, which necessarily consumes expected_faces.
        self.assertEqual(faces[600, 450], 2)
        self.assertEqual(faces[560, 460], 1)
        self.assertEqual(faces[639, 460], 0)

    def test_exact_pairs_at_positive_integer_output_scales(self):
        for scale in (1, 2, 3):
            with self.subTest(scale=scale):
                result = METRIC.compare(*pair(scale))
                self.assertTrue(result["pass"], result)
                self.assertEqual(result["expected_material_pixels"], 4416 * scale * scale)

    def test_single_channel_change_is_rejected(self):
        normals, material = pair()
        point = next(iter(METRIC.expected_faces()))
        axis = METRIC.expected_faces()[point]
        rgb = list(METRIC.NORMAL_RGB[axis])
        rgb[1] += 1 if rgb[1] < 255 else -1
        put(normals[3], point, rgb)
        result = METRIC.compare(normals, material)
        self.assertFalse(result["pass"])
        self.assertEqual(result["wrong_normal_pixels"], 1)
        self.assertEqual(result["missing_pixels"] + result["extra_pixels"], 0)

    def test_missing_top_face_is_detected_when_floor_normal_is_identical(self):
        normals, material = pair()
        point = next(p for p, axis in METRIC.expected_faces().items() if axis == 2)
        put(material[3], point, FLOOR)
        result = METRIC.compare(normals, material)
        self.assertFalse(result["pass"])
        self.assertEqual(result["wrong_normal_pixels"], 0)
        self.assertEqual(result["missing_pixels"], 1)

    def test_extra_blue_pixel_is_rejected(self):
        normals, material = pair()
        put(material[3], (545, 410), BLUE)
        result = METRIC.compare(normals, material)
        self.assertFalse(result["pass"])
        self.assertEqual(result["extra_pixels"], 1)

    def test_wrong_axis_with_exact_silhouette_is_rejected(self):
        normals, material = pair()
        for point, axis in METRIC.expected_faces().items():
            if axis < 2:
                put(normals[3], point, METRIC.NORMAL_RGB[1 - axis])
        result = METRIC.compare(normals, material)
        self.assertFalse(result["pass"])
        self.assertEqual(result["wrong_normal_pixels"], 2944)
        self.assertEqual(result["missing_pixels"] + result["extra_pixels"], 0)

    def test_shifted_rows_are_rejected(self):
        normals, material = pair()
        for point in METRIC.expected_faces():
            put(normals[3], point, METRIC.NORMAL_RGB[2])
            put(material[3], point, FLOOR)
        for (x, y), axis in METRIC.expected_faces().items():
            put(normals[3], (x, y + 1), METRIC.NORMAL_RGB[axis])
            put(material[3], (x, y + 1), BLUE)
        result = METRIC.compare(normals, material)
        self.assertFalse(result["pass"])
        self.assertGreater(result["missing_pixels"], 0)
        self.assertGreater(result["extra_pixels"], 0)
        self.assertGreater(result["wrong_normal_pixels"], 0)

    def test_pair_dimensions_and_noninteger_scale_are_rejected(self):
        normals, material = pair()
        with self.assertRaisesRegex(ValueError, "identical dimensions"):
            METRIC.compare(normals, (1279, 720, *material[2:]))
        with self.assertRaisesRegex(ValueError, "positive integer"):
            METRIC.compare((1279, 720, *normals[2:]), (1279, 720, *material[2:]))

    def test_png_cli_pair_and_failure_exit(self):
        normals, material = pair()
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ("normals.png", "material.png")]
            for path, data in zip(paths, (normals, material)):
                util.write_png(str(path), data[0], data[1], bytes(data[3]), data[2])
            argv = [sys.executable, str(SCRIPT), "--normals", str(paths[0]),
                    "--material", str(paths[1])]
            result = subprocess.run(argv, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertTrue(json.loads(result.stdout)["pass"])
            put(material[3], (545, 410), BLUE)
            util.write_png(str(paths[1]), material[0], material[1], bytes(material[3]), 3)
            result = subprocess.run(argv, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stderr + result.stdout)
            self.assertEqual(json.loads(result.stdout)["extra_pixels"], 1)


if __name__ == "__main__":
    unittest.main()
