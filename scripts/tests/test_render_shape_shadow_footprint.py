"""Independent geometry controls for the shape shadow-footprint metric."""

import contextlib
import importlib.util
import io
import math
import sys
import tempfile
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
    def write_shadow_pair(self, directory, scale):
        width, height = 320, 180
        origin = (width * 0.5, height * 0.5)
        step = (2.0, 1.0)
        unshadowed = bytearray(bytes((160, 160, 160, 255)) * width * height)
        shadowed = bytearray(unshadowed)
        for shape in METRIC.SHAPES:
            mask, _, _, _, _ = METRIC.expected_mask(
                shape, 0.0, step, origin, (width, height), scale
            )
            for x, y in mask:
                offset = (y * width + x) * 4
                shadowed[offset : offset + 3] = bytes((80, 80, 80))

        shadowed_path = directory / f"shadowed-{scale}.png"
        unshadowed_path = directory / "unshadowed.png"
        METRIC.rmu.write_png(str(shadowed_path), width, height, bytes(shadowed), 4)
        METRIC.rmu.write_png(str(unshadowed_path), width, height, bytes(unshadowed), 4)
        return shadowed_path, unshadowed_path

    def test_projection_round_trip_on_receiver_plane(self):
        origin = (640.0, 360.0)
        step = (8.0, 4.0)
        for yaw in (0.0, math.pi / 4.0):
            for point in (
                (-12.0, -8.0, 4.375),
                (12.0, 8.0, 4.375),
                (1.25, -3.5, 4.375),
            ):
                pixel = METRIC.project(point, yaw, step, origin)
                actual = METRIC.floor_point(pixel, yaw, step, origin, 4.375)
                for got, want in zip(actual, point):
                    self.assertAlmostEqual(got, want)

    def test_density_sets_receiver_plane_and_analytic_boundary(self):
        self.assertAlmostEqual(METRIC.receiver_top_z(1), 4.0)
        self.assertAlmostEqual(METRIC.receiver_top_z(4), 4.375)
        self.assertAlmostEqual(METRIC.analytic_surface_threshold(4), 0.125)

        torus = METRIC.SHAPES[3]
        boundary_point = (torus.center[0] + 6.0625, torus.center[1], torus.center[2])
        self.assertGreater(METRIC.sdf(torus, boundary_point), 0.0)
        self.assertLessEqual(
            METRIC.sdf(torus, boundary_point), METRIC.analytic_surface_threshold(4)
        )
        self.assertGreaterEqual(METRIC.analytic_half_bounds(torus, 1.0, 0.125)[0], 6.0625)

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

    def test_wrong_size_capture_fails_metric_exit_gate(self):
        with tempfile.TemporaryDirectory() as temp_directory:
            directory = Path(temp_directory)
            for scale, expected_exit in ((1.0, 0), (1.35, 1)):
                shadowed, unshadowed = self.write_shadow_pair(directory, scale)
                with contextlib.redirect_stdout(io.StringIO()):
                    actual_exit = METRIC.main(
                        [
                            str(shadowed),
                            "--unshadowed",
                            str(unshadowed),
                            "--yaw",
                            "0",
                            "--effective-subdivisions",
                            "4",
                        ]
                    )
                self.assertEqual(actual_exit, expected_exit)


if __name__ == "__main__":
    unittest.main()
