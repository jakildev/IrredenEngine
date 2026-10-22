import json
import math
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import render_receiver_shadow_polygon as oracle


class ReceiverShadowPolygonTests(unittest.TestCase):
    def image(self, yaw=0, transform=None):
        polygons = oracle.fixture_polygons(yaw)
        image = Image.new("RGB", oracle.IMAGE_SIZE, oracle.LIT_COLOR)
        shadow = polygons["shadow"]
        if transform:
            shadow = [transform(x, y) for x, y in shadow]
        ImageDraw.Draw(image).polygon(shadow, fill=oracle.SHADOW_COLOR)
        return image

    def test_independent_polygon_raster_all_quadrants(self):
        for yaw in (0, .6, math.pi / 2, math.pi, 3 * math.pi / 2):
            with self.subTest(yaw=yaw):
                result = oracle.check_image(self.image(yaw), yaw)
                self.assertTrue(result["passed"], result["counts"])
                self.assertEqual(result["counts"]["eligible"], result["counts"]["valid"])

    def test_missing_and_all_shadow_are_rejected(self):
        for color, error in ((oracle.LIT_COLOR, "missing"),
                             (oracle.SHADOW_COLOR, "excess"), ((0, 0, 0), "unknown")):
            with self.subTest(color=color):
                result = oracle.check_image(Image.new("RGB", oracle.IMAGE_SIZE, color), 0)
                self.assertFalse(result["passed"])
                self.assertGreater(result["counts"][error], 1000)

    def test_wrong_projection_and_rotation_are_rejected(self):
        transforms = (lambda x, y: (x + 8, y),
                      lambda x, y: (1280 - (y - 464), 464 + (x - 1280)))
        for transform in transforms:
            result = oracle.check_image(self.image(transform=transform), 0)
            self.assertFalse(result["passed"])
            self.assertGreater(result["counts"]["missing"], 100)
            self.assertGreater(result["counts"]["excess"], 100)

    def test_unknown_receiver_pixel_cannot_be_filtered_out(self):
        image = self.image()
        x, y, _ = next(sample for sample in oracle.receiver_samples(oracle.fixture_polygons(0))
                       if sample[2] < -10)
        image.putpixel((x, y), (230, 170, 60))
        result = oracle.check_image(image, 0)
        self.assertFalse(result["passed"])
        self.assertEqual(result["counts"]["unknown"], 1)

    def test_transparent_and_partial_alpha_are_rejected(self):
        for alpha in (0, 128, 254):
            with self.subTest(alpha=alpha):
                image = self.image().convert("RGBA")
                image.putalpha(alpha)
                result = oracle.check_image(image, 0)
                self.assertFalse(result["passed"])
                self.assertEqual(result["counts"]["unknown"], result["counts"]["eligible"])
        image = self.image().convert("RGBA")
        x, y, _ = next(sample for sample in oracle.receiver_samples(oracle.fixture_polygons(0))
                       if sample[2] < -10)
        image.putpixel((x, y), (*oracle.LIT_COLOR, 0))
        result = oracle.check_image(image, 0)
        self.assertFalse(result["passed"])
        self.assertEqual(result["counts"]["unknown"], 1)
        self.assertTrue(oracle.check_image(self.image().convert("RGBA"), 0)["passed"])

    def test_stale_palette_and_empty_coverage_fail(self):
        with patch.object(oracle, "LIT_COLOR", (81, 120, 240)):
            result = oracle.check_image(Image.new("RGB", oracle.IMAGE_SIZE, (80, 120, 240)), 0)
            self.assertGreater(result["counts"]["unknown"], 20000)
            self.assertFalse(result["passed"])
        with patch.object(oracle, "receiver_samples", return_value=iter(())):
            result = oracle.check_image(self.image(), 0)
            self.assertFalse(result["coverage_valid"])
            self.assertFalse(result["passed"])

    def test_boundary_tolerance_does_not_waive_palette_contract(self):
        image = self.image()
        sample = next(p for p in oracle.receiver_samples(oracle.fixture_polygons(0))
                      if abs(p[2]) < .75)
        x, y, _ = sample
        image.putpixel((x, y), oracle.SHADOW_COLOR)
        self.assertTrue(oracle.check_image(image, 0)["passed"])
        image.putpixel((x, y), (0, 0, 0))
        self.assertFalse(oracle.check_image(image, 0)["passed"])

    def test_pixel_centers_and_supporting_line_margin(self):
        square = [(0, 0), (10, 0), (10, 10), (0, 10)]
        self.assertEqual(oracle.polygon_margin((.5, .5), square), .5)
        self.assertEqual(oracle.polygon_margin((5, 5), square), 5)
        self.assertEqual(oracle.polygon_margin((-1, -1), square), -1)
        self.assertEqual(oracle.polygon_margin((11, 5), square), -1)

    def test_retained_native_pass_and_failure_controls(self):
        root = Path(__file__).resolve().parents[2]
        evidence = root / "docs/pr-screenshots/codex/receiver-shadow-polygon"
        expected = {row["case"]: row for row in
                    json.loads((evidence / "results.json").read_text())}
        for case in ("yaw0-surface0", "yaw0.6-surface0", "yaw0.6-surface1"):
            with self.subTest(case=case):
                row = expected[case]
                with Image.open(evidence / case / "screenshot_000001.png") as image:
                    result = oracle.check_image(image, row["yaw"])
                self.assertEqual(result["counts"], row["counts"])
                self.assertEqual(result["passed"], row["passed"])
        self.assertFalse(expected["yaw0.6-surface0"]["passed"])
        self.assertTrue(expected["yaw0.6-surface1"]["passed"])

    def test_fixture_sampling_contract(self):
        with self.assertRaises(ValueError):
            oracle.check_image(Image.new("RGB", (1280, 720)), 0)
        for yaw in (math.nan, math.inf):
            with self.assertRaises(ValueError):
                oracle.fixture_polygons(yaw)
        polygons = oracle.fixture_polygons(0)
        self.assertEqual(len(polygons["shadow"]), 6)
        self.assertAlmostEqual(min(p[1] for p in polygons["shadow"]), 440)
        self.assertAlmostEqual(max(p[1] for p in polygons["shadow"]), 488)


if __name__ == "__main__":
    unittest.main()
