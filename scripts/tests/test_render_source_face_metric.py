"""Independent cube controls for source-face fidelity, not lattice consistency."""

import contextlib
import importlib.util
import io
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location(
    "source_face_metric", SCRIPTS / "render-source-face-metric.py")
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)


def cube_pixels(size=128, zoom=1):
    pixels = bytearray(size * size * 3)
    for y in range(size):
        for x in range(size):
            dx = (x + .5 - size / 2) / (16 * zoom)
            dy = (y + .5 - size / 2) / (8 * zoom)
            color = (0, 0, 0)
            if abs(dx) < 1 - abs(dy + 1):
                color = (128, 128, 0)
            elif -1 < dx < 0 and dx < dy < dx + 2:
                color = (128, 0, 128)
            elif 0 < dx < 1 and -dx < dy < 2 - dx:
                color = (0, 128, 128)
            pixels[(y * size + x) * 3:(y * size + x) * 3 + 3] = bytes(color)
    return pixels


class SourceFaceMetricTest(unittest.TestCase):
    def check_image(self, pixels, normals=True, zoom=1):
        labels, palette, clipped = METRIC.expected_image(
            128, 128, "voxel", 0, True, (16 * zoom, 8 * zoom), (64, 64))
        self.assertFalse(clipped)
        return METRIC.compare(128, 128, 3, pixels, labels, palette, normals)[0]

    def test_literal_cube_faces_pass_at_two_scales(self):
        for zoom in (1, 2):
            self.assertTrue(self.check_image(cube_pixels(zoom=zoom), zoom=zoom)["pass"])

    def test_literal_quarter_turn_basis_and_axis_scale(self):
        for magnitude in (1e-200, 1, 1e200, 1e308):
            for point, expected in (((0, 1, 0), (0, 0, 1)), ((0, 0, 1), (0, -1, 0))):
                actual = METRIC.rotate(point, axis_angle=(magnitude, 0, 0, 90))
                for a, b in zip(actual, expected):
                    self.assertAlmostEqual(a, b)
        point = (2, -3, 5)
        result = METRIC.rotate(point, axis_angle=(1e308, 1e308, 1e308, 73))
        self.assertAlmostEqual(math.hypot(*point), math.hypot(*result))

    def test_half_turns_preserve_literal_cube_world_faces(self):
        for axis in ((1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0)):
            labels, palette, clipped = METRIC.expected_image(
                128, 128, "voxel", 0, False, (16, 8), (64, 64), (*axis, 180))
            self.assertFalse(clipped)
            result, _ = METRIC.compare(128, 128, 3, cube_pixels(), labels, palette, True)
            self.assertTrue(result["pass"], (axis, result))

    def test_invalid_axis_is_rejected(self):
        for pose in ((0, 0, 0, 45), (1, 0, 0, float("nan"))):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                METRIC.main(["unused.png", "--shape", "voxel", "--yaw", "0",
                             "--axis-angle", *map(str, pose)])
            self.assertEqual(caught.exception.code, 2)

    def test_spike_fails_without_an_interior_hole(self):
        pixels = cube_pixels()
        for y in range(40, 48):
            for x in range(62, 66):
                pixels[(y * 128 + x) * 3:(y * 128 + x) * 3 + 3] = bytes((128, 128, 0))
        result = self.check_image(pixels)
        self.assertGreater(result["extra_pixels"], 0)
        self.assertEqual(result["missing_pixels"], 0)
        self.assertFalse(result["pass"])

    def test_missing_face_interior_fails(self):
        pixels = cube_pixels()
        for y in range(56, 59):
            for x in range(62, 66):
                pixels[(y * 128 + x) * 3:(y * 128 + x) * 3 + 3] = bytes(3)
        self.assertGreater(self.check_image(pixels)["missing_pixels"], 0)

    def test_equal_area_face_swap_fails_with_identical_silhouette(self):
        pixels = cube_pixels()
        a, b = (66 * 128 + 54) * 3, (66 * 128 + 74) * 3
        pixels[a:a + 3], pixels[b:b + 3] = pixels[b:b + 3], pixels[a:a + 3]
        result = self.check_image(pixels)
        self.assertEqual(result["missing_pixels"], 0)
        self.assertEqual(result["extra_pixels"], 0)
        self.assertEqual(result["wrong_face_pixels"], 2)
        self.assertFalse(result["pass"])

    def test_blank_does_not_pass_vacuously(self):
        self.assertFalse(self.check_image(bytes(128 * 128 * 3))["pass"])

    def test_boundary_band_cannot_consume_entire_fixture(self):
        for scale in ((1, 1), (2, 1)):
            labels, palette, clipped = METRIC.expected_image(
                64, 64, "voxel", 0, True, scale, (32, 32))
            self.assertFalse(clipped)
            for pixels in (bytes(64 * 64 * 3),
                           bytes(v for label in labels for v in palette[label])):
                result, _ = METRIC.compare(64, 64, 3, pixels, labels, palette, True)
                self.assertFalse(result["pass"])
                self.assertFalse(result["sufficient_resolution"])

    def test_cli_exit_code_and_determinism(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cube.png"
            METRIC.write_png(str(path), 128, 128, bytes(cube_pixels()), 3)
            args = [str(path), "--shape", "voxel", "--identity", "--yaw", "0", "--normals"]
            reports = []
            for _ in range(2):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(METRIC.main(args), 0)
                reports.append(json.loads(output.getvalue()))
            self.assertEqual(reports[0], reports[1])
            METRIC.write_png(str(path), 128, 128, bytes(128 * 128 * 3), 3)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(METRIC.main(args), 1)


if __name__ == "__main__":
    unittest.main()
