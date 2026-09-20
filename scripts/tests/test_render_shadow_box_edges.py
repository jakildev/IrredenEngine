"""Literal projected-edge controls independent of the polygon rasterizer."""

import importlib.util
import sys
import unittest
from pathlib import Path

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
