"""Projection oracle controls independent of renderer screenshots."""

import importlib.util
import math
import sys
import unittest
from pathlib import Path

from PIL import Image, ImageChops

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location(
    "shape_footprint", SCRIPTS / "render-shape-footprint-metric.py")
metric = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metric)


class ShapeFootprintTest(unittest.TestCase):
    def test_box_projection_matches_independent_floor_ray_intersections(self):
        for subdivisions, floor_z in ((1, 4), (2, 4.25), (8, 4.4375)):
            for center_z in (0, 4, floor_z + 0.5, 6):
                for yaw in (0, 0.71, 2, -2.8):
                    with self.subTest(subdivisions=subdivisions, center_z=center_z, yaw=yaw):
                        self.check_floor_rays(subdivisions, floor_z, center_z, yaw)

    def check_floor_rays(self, subdivisions, floor_z, center_z, yaw):
        # Avoid exact pixel-edge ties between half-open raster coverage and closed slabs.
        size, scale = (512, 512), (11.2, 7.3)
        center = (0, 0, center_z)
        actual, clipped = metric.expected_mask(size, [center], yaw, scale, subdivisions)
        self.assertFalse(clipped)
        expected = bytearray(size[0] * size[1])
        for y in range(size[1]):
            for x in range(size[0]):
                ix = (x + .5 - size[0] / 2) / scale[0]
                iy = (y + .5 - size[1] / 2) / scale[1] - 2 * floor_z
                vx, vy = (-iy - ix) / 2, (-iy + ix) / 2
                origin = (math.cos(yaw) * vx - math.sin(yaw) * vy,
                          math.sin(yaw) * vx + math.cos(yaw) * vy, floor_z)
                near, far = 0, math.inf
                for coordinate, direction, box_center in zip(
                        origin, (.35, .85, -.4), center):
                    bounds = sorted(((box_center - .5 - coordinate) / direction,
                                     (box_center + .5 - coordinate) / direction))
                    near, far = max(near, bounds[0]), min(far, bounds[1])
                expected[y * size[0] + x] = 255 if near <= far else 0
        self.assertEqual(actual.tobytes(), bytes(expected))

    def test_analytic_origin_defaults_to_screen_center(self):
        for shape in ("cone", "torus"):
            options = dict(slices=256, sectors=128)
            default, _ = metric.analytic_expected_mask((512, 512), shape, .71, (8, 4), 8,
                                                        **options)
            centered, _ = metric.analytic_expected_mask((512, 512), shape, .71, (8, 4), 8,
                                                         origin=(256, 256), **options)
            shifted, _ = metric.analytic_expected_mask((512, 512), shape, .71, (8, 4), 8,
                                                       origin=(263, 251), **options)
            self.assertEqual(default.tobytes(), centered.tobytes())
            self.assertEqual(shifted.tobytes(), ImageChops.offset(centered, 7, -5).tobytes())

    def test_analytic_sections_match_independent_field_boundaries(self):
        for threshold in (.5, .125, .0625):
            for shape in ("cone", "torus"):
                height = (4 if shape == "cone" else 2) + threshold
                for index in range(301):
                    z = -height + 2 * height * index / 300
                    inner, outer = metric.analytic_cross_section(shape, z, threshold)
                    for radius in (inner, outer):
                        if radius == 0:
                            continue
                        if shape == "torus":
                            distance = math.hypot(radius - 4, z) - 2
                        else:
                            dr = radius - 4 * (1 - min(1, max(0, (z + 4) / 8)))
                            dz = abs(z) - 4
                            distance = math.hypot(max(dr, 0), max(dz, 0))
                            distance += min(max(dr, dz), 0)
                        self.assertAlmostEqual(distance, threshold, places=6)

    def test_analytic_reference_converges_inside_pixel_tolerance(self):
        size = (512, 512)
        visible = Image.new("L", size, 255)
        for shape in ("cone", "torus"):
            first, clipped = metric.analytic_expected_mask(
                size, shape, .71, (8, 4), 8, slices=256, sectors=128)
            second, _ = metric.analytic_expected_mask(
                size, shape, .71, (8, 4), 8, slices=512, sectors=256)
            self.assertFalse(clipped)
            result, _ = metric.compare_masks(first, second, visible)
            self.assertTrue(result["strict_edges_passed"])

    def test_native_fixture_occupancy(self):
        self.assertEqual(len(metric.occupied_cells("torus")), 504)
        self.assertEqual(len(metric.occupied_cells("cone")), 233)

    def test_correct_shadow_passes_and_wrong_size_fails(self):
        size = (800, 800)
        cells = metric.occupied_cells("torus")
        expected, clipped = metric.expected_mask(size, cells, 2, (12, 6), 2)
        self.assertFalse(clipped)
        visible = Image.new("L", size, 255)
        result, _ = metric.compare_masks(expected, expected, visible)
        self.assertTrue(result["strict_edges_passed"])
        enlarged = expected.resize((960, 960)).crop((80, 80, 880, 880))
        result, _ = metric.compare_masks(expected, enlarged, visible)
        self.assertFalse(result["strict_edges_passed"])
        self.assertGreater(result["excess_pixels"], 100)

    def test_missing_shadow_reports_failure(self):
        size = (64, 64)
        expected = Image.new("L", size)
        expected.paste(255, (16, 16, 48, 48))
        result, errors = metric.compare_masks(expected, Image.new("L", size),
                                              Image.new("L", size, 255))
        self.assertFalse(result["strict_edges_passed"])
        self.assertEqual((result["area_ratio"], result["iou"]), (0, 0))
        self.assertEqual(result["missing_pixels"], 900)
        self.assertEqual(errors.getpixel((32, 32)), (0, 255, 255))

    def test_same_area_wrong_position_fails(self):
        expected = Image.new("L", (64, 64))
        expected.paste(255, (16, 16, 32, 32))
        shifted = ImageChops.offset(expected, 8, 0)
        result, _ = metric.compare_masks(expected, shifted, Image.new("L", expected.size, 255))
        self.assertEqual(result["area_ratio"], 1)
        self.assertFalse(result["strict_edges_passed"])
        self.assertGreater(result["missing_pixels"], 0)
        self.assertGreater(result["excess_pixels"], 0)

    def test_floor_mask_excludes_caster_and_background(self):
        control = Image.new("RGB", (40, 40))
        control.paste((150, 150, 160), (5, 5, 35, 35))
        control.paste((70, 160, 210), (15, 15, 25, 25))
        shadowed = control.point(lambda value: value // 2)
        observed, floor = metric.shadow_masks(shadowed, control)
        for point in ((0, 0), (20, 20), (15, 14)):
            self.assertEqual(floor.getpixel(point), 0)
            self.assertEqual(observed.getpixel(point), 0)
        self.assertEqual(observed.getpixel((10, 10)), 255)

    def test_empty_expected_region_is_invalid(self):
        with self.assertRaisesRegex(ValueError, "expected visible"):
            metric.compare_masks(Image.new("L", (8, 8)), Image.new("L", (8, 8)),
                                 Image.new("L", (8, 8), 255))

    def test_different_screenshot_dimensions_are_invalid(self):
        with self.assertRaisesRegex(ValueError, "dimensions differ"):
            metric.shadow_masks(Image.new("RGB", (8, 8)), Image.new("RGB", (9, 8)))


if __name__ == "__main__":
    unittest.main()
