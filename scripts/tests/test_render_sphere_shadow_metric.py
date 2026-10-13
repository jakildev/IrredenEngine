#!/usr/bin/env python3
"""Independent geometry and image controls for the fixed sphere shadow metric."""

import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import render_sphere_shadow_metric as metric


def quaternion_matrix(angle):
    w = math.cos(angle / 2)
    x = math.sin(angle / 2) / math.sqrt(3)
    return (
        (1 - 4 * x * x, 2 * x * x - 2 * w * x, 2 * x * x + 2 * w * x),
        (2 * x * x + 2 * w * x, 1 - 4 * x * x, 2 * x * x - 2 * w * x),
        (2 * x * x - 2 * w * x, 2 * x * x + 2 * w * x, 1 - 4 * x * x),
    )


def transform(matrix, vector):
    return tuple(sum(a * b for a, b in zip(row, vector)) for row in matrix)


def reference_images(yaw):
    """Closest-approach sphere and quaternion-box rays, independent of classifier."""
    control = bytearray(2560 * 1440 * 3)
    image = bytearray(len(control))
    c, s = math.cos(yaw), math.sin(yaw)
    forward = ((c - s) / math.sqrt(3), (c + s) / math.sqrt(3), 1 / math.sqrt(3))
    inverse = quaternion_matrix(-0.47)
    sun_length = math.sqrt(0.42**2 + 0.60**2 + 0.55**2)
    sun = tuple(v / sun_length for v in (-0.42, -0.60, -0.55))
    blocker = tuple(math.floor(p + d * 12 + 0.5) for p, d in zip((0, 0, -12), sun))
    local_direction = transform(inverse, sun)
    for y in range(80, 256):
        for x in range(540, 740):
            u, v = (x + 0.5 - 640) / 16, (y + 0.5 - 360) / 8
            a, b = -u / 2 - v / 6, u / 2 - v / 6
            relative = (c * a - s * b, s * a + c * b, v / 3 + 12)
            along = sum(p * d for p, d in zip(relative, forward))
            closest = tuple(p - along * d for p, d in zip(relative, forward))
            chord_squared = (4 + 0.5 / 8) ** 2 - sum(p * p for p in closest)
            if chord_squared < 0:
                continue
            radial = tuple(p - math.sqrt(chord_squared) * d for p, d in zip(closest, forward))
            hit = (radial[0], radial[1], radial[2] - 12)
            local_origin = transform(inverse, tuple(p - q for p, q in zip(hit, blocker)))
            intersections = [
                sorted(((-1.5 - p) / d, (1.5 - p) / d))
                for p, d in zip(local_origin, local_direction)
            ]
            shadow = max(0, *(pair[0] for pair in intersections)) <= min(
                pair[1] for pair in intersections
            )
            for dy in range(2):
                for dx in range(2):
                    index = ((2 * y + dy) * 2560 + 2 * x + dx) * 3
                    control[index : index + 3] = bytes((40, 60, 180))
                    image[index : index + 3] = bytes((20, 30, 90) if shadow else (40, 60, 180))
    return (2560, 1440, 3, image), (2560, 1440, 3, control)


class GeometryTests(unittest.TestCase):
    def test_sphere_intersections(self):
        ray = metric.ray_sphere_interval
        self.assertEqual(ray((-3, 0, 0), (1, 0, 0), (0, 0, 0), 1), (2, 4))
        self.assertEqual(ray((-3, 0, 0), (2, 0, 0), (0, 0, 0), 1), (1, 2))
        self.assertEqual(ray((0, 0, 0), (1, 0, 0), (0, 0, 0), 1), (-1, 1))
        self.assertEqual(ray((-3, 1, 0), (1, 0, 0), (0, 0, 0), 1), (3, 3))
        self.assertIsNone(ray((-3, 2, 0), (1, 0, 0), (0, 0, 0), 1))
        with self.assertRaises(ValueError):
            ray((0, 0, 0), (0, 0, 0), (0, 0, 0), 1)

    def test_rotated_box_intersections(self):
        matrix = quaternion_matrix(0.47)
        direction = transform(matrix, (1, 0, 0))
        for local, expected in (
            ((-3, 0, 0), (1.5, 4.5)),
            ((3, 0, 0), (-4.5, -1.5)),
            ((-3, 2, 0), None),
        ):
            offset = transform(matrix, local)
            origin = tuple(p + c for p, c in zip(offset, metric.BLOCKER_CENTER))
            actual = metric.blocker_interval(origin, direction)
            if expected is None:
                self.assertIsNone(actual)
            else:
                for a, b in zip(actual, expected):
                    self.assertAlmostEqual(a, b)

    def test_square_morphology(self):
        mask = bytearray(25)
        mask[12] = 1
        actual = metric.box_filter(mask, 5, 5, 1, False)
        self.assertEqual(
            {i for i, value in enumerate(actual) if value}, {6, 7, 8, 11, 12, 13, 16, 17, 18}
        )
        self.assertEqual(metric.box_filter(actual, 5, 5, 1, True), mask)


class ImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image, cls.control = reference_images(math.pi / 4)

    def test_independent_render_and_all_lit_all_shadow_controls(self):
        actual = metric.score(self.image, self.control, math.pi / 4)
        self.assertTrue(actual["pass"], actual)
        self.assertGreater(actual["lit_interior"], 0)
        self.assertGreater(actual["shadow_interior"], 0)
        all_lit = metric.score(self.control, self.control, math.pi / 4)
        self.assertFalse(all_lit["pass"])
        self.assertGreater(all_lit["missing"], 0)
        dark = bytearray(self.control[3])
        for index in range(2, len(dark), 3):
            if dark[index]:
                dark[index] = 90
        all_shadow = metric.score((2560, 1440, 3, dark), self.control, math.pi / 4)
        self.assertFalse(all_shadow["pass"])
        self.assertGreater(all_shadow["excess"], 0)

    def test_hidden_external_shadow_is_valid(self):
        image, control = reference_images(math.pi)
        result = metric.score(image, control, math.pi)
        self.assertTrue(result["pass"], result)
        self.assertGreater(result["lit_interior"], 0)
        self.assertEqual(result["shadow_interior"], 0)

    def test_empty_input_is_not_success(self):
        empty = (2560, 1440, 3, bytes(2560 * 1440 * 3))
        result = metric.score(empty, empty, 0)
        self.assertFalse(result["pass"])
        self.assertEqual(result["eligible"], 0)

    def test_input_registration_guards(self):
        for invalid in ((2, 2, 3, bytes(12)), (2560, 1440, 3, b"")):
            with self.assertRaises(ValueError):
                metric.score(invalid, self.control, 0)
        with self.assertRaises(ValueError):
            metric.score(self.image, self.control, math.nan)
        misplaced = bytearray(self.control[3])
        misplaced[:3] = bytes((40, 60, 180))
        with self.assertRaisesRegex(ValueError, "outside"):
            metric.score(self.image, (2560, 1440, 3, misplaced), 0)


if __name__ == "__main__":
    unittest.main()
