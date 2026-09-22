"""Literal projected-edge controls independent of the polygon rasterizer."""

import contextlib
import importlib.util
import io
import itertools
import math
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location(
    "shadow_box_metric", SCRIPTS / "render-shadow-box-metric.py")
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)


def rectangle(scale=1, shift=0):
    image = Image.new("RGB", (64 * scale, 64 * scale), (108, 109, 115))
    for y in range(16 * scale, 48 * scale):
        for x in range(16 * scale + shift, 48 * scale + shift):
            image.putpixel((x, y), (60, 60, 64))
    return image


class ShadowBoxEdgesTest(unittest.TestCase):
    def check(self, image, scale=1, polygon=None):
        if polygon is None:
            polygon = [(16 * scale, 16 * scale), (48 * scale, 16 * scale),
                       (48 * scale, 48 * scale), (16 * scale, 48 * scale)]
        observed, visible = METRIC.shadow_masks(image, Image.new("L", image.size, 255))
        return METRIC.strict_edges(observed, visible, polygon)[0]

    def test_source_third_turn_permutates_box_axes(self):
        plate = Image.new("RGB", (240, 120), (108, 109, 115))
        rotated, _ = METRIC.expected_polygon(
            plate, 0, False, True, box_pose=2 * math.pi / 3)
        x, y, z = METRIC.BOX_HALF_CENTER_SPAN
        with patch.object(METRIC, "BOX_HALF_CENTER_SPAN", (z, x, y)):
            permuted, _ = METRIC.expected_polygon(plate, 0, False, True)
        self.assertEqual(len(rotated), len(permuted))
        for point, expected in zip(rotated, permuted):
            for actual, value in zip(point, expected):
                self.assertAlmostEqual(actual, value)

    def test_caster_density_preserves_half_centers_and_odd_density_snap(self):
        plate = Image.new("RGB", (240, 120), (108, 109, 115))
        for cardinal in range(4):
            source, _ = METRIC.expected_polygon(plate, cardinal, True, True)
            even, _ = METRIC.expected_polygon(plate, cardinal, True, False, subdivisions=2)
            self.assertEqual(even, source)
            for density in (1, 3):
                snapped, _ = METRIC.expected_polygon(
                    plate, cardinal, True, False, subdivisions=density)
                offset = .5 / density
                dx = offset * (1 - METRIC.SUN[0] / METRIC.SUN[2])
                dy = offset * (1 - METRIC.SUN[1] / METRIC.SUN[2])
                for _ in range(cardinal):
                    dx, dy = dy, -dx
                delta = (-dx + dy, (-dx - dy) * .5)
                for actual, expected in zip(snapped, source):
                    self.assertAlmostEqual(actual[0] - expected[0], delta[0])
                    self.assertAlmostEqual(actual[1] - expected[1], delta[1])

    def test_source_invariance_and_revoxelized_cardinal_occupancy(self):
        plate = Image.new("RGB", (240, 120), (108, 109, 115))
        quarter_turns = (
            lambda p: p,
            lambda p: (-p[1], p[0], p[2]),
            lambda p: (-p[0], -p[1], p[2]),
            lambda p: (p[1], -p[0], p[2]),
        )
        source_centers = set(itertools.product(
            range(-8, 10), range(-2, 4), range(-3, 5)))
        source_centers = {tuple(c - .5 for c in p) for p in source_centers}
        for cardinal in range(4):
            source, _ = METRIC.expected_polygon(plate, cardinal, False, True)
            to_world = quarter_turns[cardinal]
            to_view = quarter_turns[-cardinal % 4]
            occupied = [tuple(c - .5 for c in cell)
                        for cell in itertools.product(range(-10, 12), repeat=3)
                        if to_world(tuple(c - .5 for c in cell)) in source_centers]
            self.assertEqual(len(occupied), 18 * 6 * 8)
            for density in (1, 2, 3, 4):
                unchanged, _ = METRIC.expected_polygon(
                    plate, cardinal, False, True, subdivisions=density)
                self.assertEqual(unchanged, source)
                detached, _ = METRIC.expected_polygon(
                    plate, cardinal, False, False, subdivisions=density)
                phase = -.5 - math.floor(-.5 * density + .5) / density
                points = []
                for center in occupied:
                    raster = tuple(math.floor(c * density + .5) / density + phase
                                   for c in center)
                    for actual, value in zip(raster, center):
                        self.assertAlmostEqual(actual, value)
                    for delta in itertools.product((-.5, .5), repeat=3):
                        x, y, z = to_world(tuple(c + d for c, d in zip(raster, delta)))
                        z += METRIC.BOX_Z
                        distance = (METRIC.FLOOR_TOP - z) / METRIC.SUN[2]
                        x += distance * METRIC.SUN[0]
                        y += distance * METRIC.SUN[1]
                        x, y, _ = to_view((x, y, METRIC.FLOOR_TOP))
                        points.append((119.5 - x + y, 59.5 - .5 * (x + y)))
                expected = METRIC.convex_hull(points)
                for polygon in (detached, expected):
                    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
                        normal = (b[1] - a[1], a[0] - b[0])
                        for sign in (-1, 1):
                            def support(poly):
                                return max(sign * (p[0] * normal[0] + p[1] * normal[1])
                                           for p in poly)
                            self.assertAlmostEqual(support(detached), support(expected))

    def test_known_projection_does_not_depend_on_floor_color_bounds(self):
        wide = Image.new("RGB", (320, 240), (108, 109, 115))
        narrow = Image.new("RGB", (320, 240), (0, 0, 0))
        for y in range(50, 190):
            for x in range(50, 270):
                narrow.putpixel((x, y), (108, 109, 115))
        for cardinal in range(4):
            first, _ = METRIC.expected_polygon(
                wide, cardinal, True, False, subdivisions=2,
                iso_scale=(8, 4), screen_origin=(160, 120))
            second, _ = METRIC.expected_polygon(
                narrow, cardinal, True, False, subdivisions=2,
                iso_scale=(8, 4), screen_origin=(160, 120))
            self.assertEqual(first, second)

    def test_strict_cli_requires_known_projection(self):
        with patch.object(sys, "argv", ["metric", "a", "b", "c", "d", "--strict-edges"]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                METRIC.main()
            self.assertEqual(error.exception.code, 2)

    def test_literal_rectangle_and_one_pixel_uncertainty_pass(self):
        for scale in (1, 2):
            for shift in (-1, 0, 1):
                self.assertTrue(self.check(rectangle(scale, shift), scale)["passed"])

    def test_literal_diagonal_edges_pass_and_sawtooth_fails(self):
        for scale in (1, 2):
            image = Image.new("RGB", (64 * scale, 64 * scale), (108, 109, 115))
            polygon = [(x * scale, y * scale) for x, y in
                       ((32, 8), (56, 32), (32, 56), (8, 32))]
            for y in range(image.height):
                for x in range(image.width):
                    if abs(x + .5 - 32 * scale) + abs(y + .5 - 32 * scale) < 24 * scale:
                        image.putpixel((x, y), (60, 60, 64))
            self.assertTrue(self.check(image, polygon=polygon)["passed"])
            for y in range(20 * scale, 25 * scale):
                for x in range(48 * scale, 53 * scale):
                    image.putpixel((x, y), (60, 60, 64))
            result = self.check(image, polygon=polygon)
            self.assertFalse(result["passed"])
            self.assertGreater(result["extra_pixels"], 0)

    def test_equal_area_shift_fails_at_both_scales(self):
        for scale in (1, 2):
            result = self.check(rectangle(scale, 3), scale)
            self.assertFalse(result["passed"])
            self.assertGreater(result["missing_pixels"], 0)
            self.assertGreater(result["extra_pixels"], 0)

    def test_tooth_and_hole_fail(self):
        image = rectangle()
        for y in range(25, 30):
            for x in range(48, 52):
                image.putpixel((x, y), (60, 60, 64))
        image.putpixel((30, 30), (108, 109, 115))
        result = self.check(image)
        self.assertFalse(result["passed"])
        self.assertGreater(result["extra_pixels"], 0)
        self.assertEqual(result["missing_pixels"], 1)

    def test_empty_and_fully_occluded_do_not_pass(self):
        for color in ((108, 109, 115), (80, 120, 240)):
            self.assertFalse(self.check(Image.new("RGB", (64, 64), color))["passed"])

    def test_caster_occlusion_is_excluded(self):
        image = rectangle()
        for y in range(20, 36):
            for x in range(20, 36):
                image.putpixel((x, y), (80, 120, 240))
        self.assertTrue(self.check(image)["passed"])

    def test_thin_and_clipped_expectations_fail(self):
        for polygon in ([(20, 20), (21, 20), (21, 40), (20, 40)],
                        [(-1, 20), (30, 20), (30, 40), (-1, 40)]):
            self.assertFalse(self.check(rectangle(), polygon=polygon)["passed"])


if __name__ == "__main__":
    unittest.main()
