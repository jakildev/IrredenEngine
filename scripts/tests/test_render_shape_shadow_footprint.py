"""Independent geometry controls for the shape shadow-footprint metric."""

import importlib.util
import math
import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location(
    "shape_shadow_footprint", SCRIPTS / "render-shape-shadow-footprint.py"
)
METRIC = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = METRIC
SPEC.loader.exec_module(METRIC)


class ShapeShadowFootprintTest(unittest.TestCase):
    def test_projection_round_trip_on_receiver_plane(self):
        origin = (640.0, 360.0)
        step = (8.0, 4.0)
        for yaw in (0.0, math.pi / 4.0):
            for point in ((-12.0, -8.0, 4.0), (12.0, 8.0, 4.0), (1.25, -3.5, 4.0)):
                pixel = METRIC.project(point, yaw, step, origin)
                actual = METRIC.floor_point(pixel, yaw, step, origin)
                for got, want in zip(actual, point):
                    self.assertAlmostEqual(got, want)

    def test_voxel_fixture_occupancy_is_nonempty_and_bounded(self):
        cone, torus = METRIC.SHAPES[0], METRIC.SHAPES[2]
        cone_boxes = METRIC.occupied_boxes(cone)
        torus_boxes = METRIC.occupied_boxes(torus)
        self.assertEqual(len(cone_boxes), 233)
        self.assertEqual(len(torus_boxes), 504)
        self.assertEqual(
            METRIC.bounds(cone, cone_boxes), ((-16.5, -7.5), (-12.5, -3.5), (-4.5, 4.5))
        )
        self.assertEqual(
            METRIC.bounds(torus, torus_boxes), ((5.5, 18.5), (-14.5, -1.5), (-1.5, 3.5))
        )

    def test_wrong_size_controls_change_coverage(self):
        image_size = (320, 180)
        origin = (160.0, 90.0)
        step = (2.0, 1.0)
        for yaw in (0.0, math.pi / 4.0):
            for shape in METRIC.SHAPES:
                expected, _, _, _, _ = METRIC.expected_mask(shape, yaw, step, origin, image_size)
                too_large, _, _, _, _ = METRIC.expected_mask(
                    shape, yaw, step, origin, image_size, 1.35
                )
                too_small, _, _, _, _ = METRIC.expected_mask(
                    shape, yaw, step, origin, image_size, 0.65
                )
                self.assertTrue(expected)
                self.assertTrue(too_large)
                self.assertTrue(too_small)
                self.assertGreater(len(too_large - expected), len(too_large) * 0.02)
                self.assertGreater(len(expected - too_small), len(expected) * 0.02)
                large_missing = {
                    point for point in too_large if not METRIC.near(expected, point, 2)
                }
                small_excess = {point for point in expected if not METRIC.near(too_small, point, 2)}
                self.assertGreater(len(large_missing) / len(too_large), 0.15)
                self.assertGreater(len(small_excess) / len(expected), 0.15)


if __name__ == "__main__":
    unittest.main()
