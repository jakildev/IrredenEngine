"""Independent slab, ownership and nonvacuous shadow-gate controls."""

import importlib.util
import math
import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location(
    "source_occlusion", SCRIPTS / "render-source-occlusion-metric.py")
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)


class SourceOcclusionTest(unittest.TestCase):
    def test_slab_hit_miss_and_parallel_edge(self):
        self.assertEqual(METRIC.ray_box_interval((-2, 0, 0), (1, 0, 0), (0, 0, 0)), (1.5, 2.5))
        self.assertIsNone(METRIC.ray_box_interval((-2, 1, 0), (1, 0, 0), (0, 0, 0)))
        self.assertIsNone(METRIC.ray_box_interval((-2, .5, 0), (1, 0, 0), (0, 0, 0)))
        self.assertIsNone(METRIC.ray_box_interval((-2, 0, 0), (-1, 0, 0), (0, 0, 0)))

    def test_own_cell_is_not_an_occluder(self):
        self.assertFalse(METRIC.blocked((-.5, 0, 0), (-1, 0, 0), {(0, 0, 0)}, (0, 0, 0)))
        self.assertTrue(METRIC.blocked((-.5, 0, 0), (-1, 0, 0),
                                       {(0, 0, 0), (-2, 0, 0)}, (0, 0, 0)))

    def test_unit_cube_ownership_matches_literal_faces(self):
        owners, faces, clipped = METRIC.expected(128, 128, "voxel", 0, True,
                                                (16, 8), METRIC.SUN)
        self.assertFalse(clipped)
        for pixel, normal in [((64, 58), (0, 0, -1)), ((56, 67), (0, -1, 0)),
                              ((72, 67), (-1, 0, 0))]:
            self.assertEqual(faces[owners[pixel[1] * 128 + pixel[0]]]["normal"], normal)

    def test_frame_exercises_lit_and_occluded_faces(self):
        _, faces, _ = METRIC.expected(256, 256, "frame", math.pi / 4, False,
                                      (6, 3), METRIC.SUN)
        self.assertTrue({True, False}.issubset({f["visible_to_sun"] for f in faces[1:]}))

    def test_sun_magnitude_does_not_change_visibility(self):
        reference = METRIC.expected(128, 128, "frame", 0, False, (3, 1.5), METRIC.SUN)
        for magnitude in (1e-20, 1e20):
            actual = METRIC.expected(128, 128, "frame", 0, False, (3, 1.5),
                                     tuple(v * magnitude for v in METRIC.SUN))
            self.assertEqual(actual, reference)

    def test_blank_and_missing_shadow_fail(self):
        width, height = 20, 10
        owners = [1 if x < 10 else 2 for y in range(height) for x in range(width)]
        faces = [None, dict(visible_to_sun=True), dict(visible_to_sun=False)]
        pixels = bytes(c for owner in owners for c in ((0, 0, 0) if owner == 1 else (255, 0, 255)))
        good, _ = METRIC.compare(width, height, 3, pixels, owners, faces, True)
        self.assertTrue(good["pass"])
        bad, _ = METRIC.compare(width, height, 3, bytes(len(pixels)), owners, faces, True)
        self.assertGreater(bad["missed_shadow_pixels"], 0)
        self.assertFalse(bad["pass"])
        inverted = bytes(c for owner in owners
                         for c in ((255, 0, 255) if owner == 1 else (0, 0, 0)))
        bad, _ = METRIC.compare(width, height, 3, inverted, owners, faces, True)
        self.assertGreater(bad["false_shadow_pixels"], 0)
        self.assertFalse(bad["pass"])

    def test_all_lit_is_inconclusive_and_wrong_overlay_fails(self):
        owners = [1] * 100
        for visible, pixels in [(True, bytes(300)), (False, bytes([128] * 300))]:
            result, _ = METRIC.compare(10, 10, 3, pixels, owners,
                                       [None, dict(visible_to_sun=visible)], True)
            self.assertFalse(result["pass"])


if __name__ == "__main__":
    unittest.main()
