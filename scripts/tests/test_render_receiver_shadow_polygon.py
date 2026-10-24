import io
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import render_receiver_shadow_polygon as oracle


def box_blocks_ray(receiver, sun, caster_offset=(0, 0, 0)):
    """Independent slabs for a 3-cube twelve units sunward of (0,0,-12)."""
    component = math.sin(.47 / 2) / math.sqrt(3)
    scalar = math.cos(.47 / 2)
    diagonal = 1 - 4 * component * component
    plus = 2 * component * component + 2 * scalar * component
    minus = 2 * component * component - 2 * scalar * component
    inverse = ((diagonal, plus, minus), (minus, diagonal, plus),
               (plus, minus, diagonal))
    relative = (receiver[0] - 12 * sun[0], receiver[1] - 12 * sun[1],
                receiver[2] + 12 - 12 * sun[2])
    relative = tuple(value - offset for value, offset in zip(relative, caster_offset, strict=True))
    origin = [sum(a * b for a, b in zip(row, relative)) for row in inverse]
    direction = [sum(a * b for a, b in zip(row, sun)) for row in inverse]
    near, far = 0.0, math.inf
    for position, velocity in zip(origin, direction):
        if abs(velocity) < 1e-12:
            if abs(position) > 1.5:
                return False
            continue
        first, last = sorted(((-1.5 - position) / velocity, (1.5 - position) / velocity))
        near, far = max(near, first), min(far, last)
    return near <= far


class ReceiverShadowPolygonTests(unittest.TestCase):
    def image(self, yaw=0, transform=None, sun=(0, 0, -1), lit_color=oracle.LIT_COLOR,
              caster_offset=(0, 0, 0)):
        polygons = oracle.fixture_polygons(yaw, sun=sun, caster_offset=caster_offset)
        image = Image.new("RGB", oracle.IMAGE_SIZE, lit_color)
        shadow = polygons["shadow"]
        if transform:
            shadow = [transform(x, y) for x, y in shadow]
        ImageDraw.Draw(image).polygon(shadow, fill=oracle.SHADOW_COLOR)
        return image

    def test_sun_shadow_polygons_match_independent_box_rays_in_all_quadrants(self):
        cases = (((0, 0, -1), (0, 0, 0)),
                 ((.6, 0, -.8), (0, 0, 0)), ((-.6, 0, -.8), (0, 0, 0)),
                 ((.6, 0, -.8), (-.2, 0, -.4)), ((-.6, 0, -.8), (.2, 0, -.4)),
                 ((.6, 0, -.8), (.137, -.213, .251)))
        for sun, offset in cases:
            for quadrant in range(4):
                yaw = .3 + quadrant * math.pi / 2
                polygon = oracle.fixture_polygons(yaw, sun=sun, caster_offset=offset)["shadow"]
                hits = misses = 0
                for ix in range(-36, 37):
                    for iy in range(-12, 13):
                        x, y = ix / 4 + .017, iy / 4 + .029
                        view_x = math.cos(yaw) * x + math.sin(yaw) * y
                        view_y = -math.sin(yaw) * x + math.cos(yaw) * y
                        screen = (1280 + 16 * (view_y - view_x),
                                  464 - 8 * (view_x + view_y))
                        margin = oracle.polygon_margin(screen, polygon)
                        if abs(margin) < 1e-7:
                            continue
                        blocked = box_blocks_ray((x, y, -16), sun, offset)
                        self.assertEqual(margin > 0, blocked, (sun, offset, yaw, x, y, margin))
                        hits += blocked
                        misses += not blocked
                self.assertGreater(hits, 100)
                self.assertGreater(misses, 1000)

    def test_oblique_sun_rasters_and_fixed_ambient_palette_all_quadrants(self):
        # 0.30 ambient + 0.70 * 0.8 Lambert gives RGB8 (69,103,206).
        for sun in ((.6, 0, -.8), (-.6, 0, -.8)):
            for quadrant in range(4):
                yaw = .3 + quadrant * math.pi / 2
                with self.subTest(sun=sun, yaw=yaw):
                    image = self.image(yaw, sun=sun, lit_color=(69, 103, 206))
                    result = oracle.check_image(image, yaw, sun=sun)
                    self.assertTrue(result["passed"], result["counts"])
                    self.assertEqual(result["lit_color"], [69, 103, 206])
                    for actual, expected in zip(result["sun_direction"], sun, strict=True):
                        self.assertAlmostEqual(actual, expected)

    def test_oblique_sun_rejects_wrong_direction_vertical_shadow_and_old_palette(self):
        for sun in ((.6, 0, -.8), (-.6, 0, -.8)):
            for wrong_sun in ((-sun[0], 0, -.8), (0, 0, -1)):
                with self.subTest(sun=sun, wrong_sun=wrong_sun):
                    image = self.image(.6, sun=wrong_sun, lit_color=(69, 103, 206))
                    result = oracle.check_image(image, .6, sun=sun)
                    self.assertFalse(result["passed"])
                    self.assertEqual(result["counts"]["unknown"], 0)
                    self.assertGreater(result["counts"]["missing"], 1000)
                    self.assertGreater(result["counts"]["excess"], 1000)
            stale = oracle.check_image(self.image(.6, sun=sun), .6, sun=sun)
            self.assertFalse(stale["passed"])
            self.assertGreater(stale["counts"]["unknown"], 18000)

    def test_sun_validation_scale_invariance_and_vertical_compatibility(self):
        for sun in (None, (), (0, -1), (0, 0, -1, 0), ("bad", 0, -1),
                    (math.nan, 0, -1), (0, math.inf, -1), (0, 0, -math.inf),
                    (0, 0, 0), (1, 0, 0), (0, 0, 1), (1e308, 0, -5e-324)):
            with self.subTest(sun=sun), self.assertRaises(ValueError):
                oracle.fixture_polygons(0, sun=sun)
        image = Image.new("RGB", oracle.IMAGE_SIZE, oracle.SHADOW_COLOR)
        with self.assertRaisesRegex(ValueError, "distinguish"):
            oracle.check_image(image, 0, sun=(1, 0, -1e-8))
        for sun in ((0, 0, -1), (.6, 0, -.8), (-.6, 0, -.8)):
            expected = oracle.fixture_polygons(.6, (-.375, .375), sun)
            for scale in (1e-300, 7, 1e308):
                scaled = tuple(value * scale for value in sun)
                actual = oracle.fixture_polygons(.6, (-.375, .375), scaled)
                for name, polygon in expected.items():
                    for point, other in zip(polygon, actual[name], strict=True):
                        self.assertAlmostEqual(point[0], other[0])
                        self.assertAlmostEqual(point[1], other[1])
                self.assertEqual(oracle.receiver_lit_color(oracle.sun_direction(scaled)),
                                 oracle.LIT_COLOR if sun[0] == 0 else (69, 103, 206))
        self.assertEqual(oracle.fixture_polygons(.6),
                         oracle.fixture_polygons(.6, sun=(0, 0, -5)))

    def test_oblique_sun_cli_reports_normalized_direction_and_palette(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oblique.png"
            self.image(.6, sun=(-.6, 0, -.8), lit_color=(69, 103, 206),
                       caster_offset=(.137, -.213, .251)).save(path)
            stdout = io.StringIO()
            with patch("sys.argv", ["oracle", str(path), "--yaw", ".6",
                                     "--sun-direction", "-3", "0", "-4",
                                     "--source-offset", ".137", "-.213", ".251"]), \
                    patch("sys.stdout", stdout):
                self.assertEqual(oracle.main(), 0)
            result = json.loads(stdout.getvalue())
            self.assertTrue(result["passed"])
            self.assertEqual(result["lit_color"], [69, 103, 206])
            self.assertEqual(result["source_offset"], [.137, -.213, .251])
            for actual, expected in zip(result["sun_direction"], (-.6, 0, -.8), strict=True):
                self.assertAlmostEqual(actual, expected)

    def test_rounded_caster_shadow_fails_against_authored_fractional_center(self):
        for sun, rounded_offset in (((.6, 0, -.8), (-.2, 0, -.4)),
                                    ((-.6, 0, -.8), (.2, 0, -.4))):
            with self.subTest(sun=sun):
                authored = self.image(.6, sun=sun, lit_color=(69, 103, 206))
                self.assertTrue(oracle.check_image(authored, .6, sun=sun)["passed"])
                rounded = self.image(.6, sun=sun, lit_color=(69, 103, 206),
                                     caster_offset=rounded_offset)
                result = oracle.check_image(rounded, .6, sun=sun)
                self.assertFalse(result["passed"])
                self.assertTrue(result["coverage_valid"])
                self.assertEqual(result["counts"]["unknown"], 0)
                self.assertGreater(result["counts"]["missing"], 100)
                self.assertGreater(result["counts"]["excess"], 100)

    def test_source_offset_validation_and_whole_caster_plane_clearance(self):
        for offset in (None, (), (0, 0), (0, 0, 0, 0), (0, "bad", 0),
                       (math.nan, 0, 0), (0, math.inf, 0), (0, 0, -math.inf)):
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "source offset"):
                oracle.fixture_polygons(.6, caster_offset=offset)
        # Quaternion-derived support extent, independent of the corner hull.
        component = math.sin(.47 / 2) / math.sqrt(3)
        scalar = math.cos(.47 / 2)
        half_height = 1.5 * (abs(1 - 4 * component * component)
                             + abs(2 * component * component + 2 * scalar * component)
                             + abs(2 * component * component - 2 * scalar * component))
        touching_offset = 8 - half_height
        oracle.fixture_polygons(0, caster_offset=(0, 0, touching_offset - 1e-9))
        for offset_z in (touching_offset + 1e-9, 7, 8, 9):
            with self.subTest(offset_z=offset_z), \
                    self.assertRaisesRegex(ValueError, "whole caster"):
                oracle.fixture_polygons(0, caster_offset=(0, 0, offset_z))
        for offset_z in (4, 5.6, 7):
            with self.subTest(oblique_offset_z=offset_z), \
                    self.assertRaisesRegex(ValueError, "whole caster"):
                oracle.fixture_polygons(0, sun=(.6, 0, -.8), caster_offset=(0, 0, offset_z))

    def test_independent_polygon_raster_all_quadrants(self):
        for yaw in (0, .6, math.pi / 2, math.pi, 3 * math.pi / 2):
            with self.subTest(yaw=yaw):
                result = oracle.check_image(self.image(yaw), yaw)
                self.assertTrue(result["passed"], result["counts"])
                self.assertEqual(result["counts"]["eligible"], result["counts"]["valid"])

    def test_fractional_camera_positions_match_independently_shifted_rasters(self):
        # Literal screen translations derive from zoom4 * iso pitch(2,1) * output2.
        # The raster never calls the camera-aware polygon projection.
        positions = (((-.375, -.625), (-6, -5)), ((.4375, .375), (7, 3)),
                     ((-1.25, .875), (-20, 7)), ((1.375, -.875), (22, -7)),
                     ((.1, -.2), (1, -2)), ((-.1, .2), (-2, 1)),
                     ((-.001, -.001), (-1, -1)))
        for yaw in (0, .6):
            for camera_iso, (dx, dy) in positions:
                with self.subTest(yaw=yaw, camera_iso=camera_iso):
                    image = self.image(yaw, transform=lambda x, y: (x + dx, y + dy))
                    result = oracle.check_image(image, yaw, camera_iso)
                    self.assertTrue(result["passed"], result["counts"])
                    self.assertEqual(result["camera_iso"], list(camera_iso))
                    self.assertEqual(result["camera_shift_pixels"], [dx, dy])
                    wrong = oracle.check_image(image, yaw, (-camera_iso[0], -camera_iso[1]))
                    self.assertFalse(wrong["passed"])
                    self.assertGreater(wrong["counts"]["missing"] + wrong["counts"]["excess"], 0)

    def test_camera_translation_is_after_yaw_and_snaps_fractional_pixels(self):
        for yaw in (0, .6, math.pi / 2):
            base = oracle.fixture_polygons(yaw)
            translated = oracle.fixture_polygons(yaw, (-.1, .2))
            for name in base:
                for point, shifted in zip(base[name], translated[name], strict=True):
                    self.assertAlmostEqual(shifted[0] - point[0], -2)
                    self.assertAlmostEqual(shifted[1] - point[1], 1)

    def test_float32_camera_rounding_at_pixel_and_negative_integer_boundaries(self):
        for camera, expected in (((.0624999999, .1249999999), (1, 1)),
                                 ((-1e-10, -1e-10), (0, 0)),
                                 ((-1.00000001, -1.00000001), (-16, -8))):
            with self.subTest(camera=camera):
                self.assertEqual(oracle.camera_screen_offset(camera), expected)
                image = self.image(.6, transform=lambda x, y: (x + expected[0], y + expected[1]))
                self.assertTrue(oracle.check_image(image, .6, camera)["passed"])

    def test_unsnapped_oracle_misses_independently_rasterized_camera_shadow(self):
        camera = (-.001, -.001)
        image = self.image(.6, transform=lambda x, y: (x - 1, y - 1))
        self.assertTrue(oracle.check_image(image, .6, camera)["passed"])
        with patch.object(oracle, "camera_screen_offset", return_value=(-.016, -.008)):
            result = oracle.check_image(image, .6, camera)
        self.assertFalse(result["passed"])
        self.assertGreater(result["counts"]["missing"], 0)
        self.assertGreater(result["counts"]["excess"], 0)

    def test_sampling_clips_all_image_edges_and_offscreen_coverage_fails(self):
        width, height = oracle.IMAGE_SIZE
        for left, top in ((-10, -10), (width - 10, -10),
                          (-10, height - 10), (width - 10, height - 10)):
            receiver = [(left, top), (left + 20, top),
                        (left + 20, top + 20), (left, top + 20)]
            polygons = dict(receiver=receiver, shadow=receiver,
                            occluder=[(-100, -100), (-90, -100), (-90, -90), (-100, -90)])
            samples = list(oracle.receiver_samples(polygons))
            self.assertTrue(samples)
            self.assertTrue(all(0 <= x < width and 0 <= y < height for x, y, _ in samples))
        image = Image.new("RGB", oracle.IMAGE_SIZE, oracle.LIT_COLOR)
        for position in ((-200, 0), (200, 0), (0, -200), (0, 200), (-1e30, 1e30)):
            with self.subTest(position=position):
                result = oracle.check_image(image, .6, position)
                self.assertEqual(result["counts"]["eligible"], 0)
                self.assertFalse(result["coverage_valid"])
                self.assertFalse(result["passed"])

    def test_camera_input_validation_and_cli_json(self):
        for position in ((math.nan, 0), (0, math.inf), (-math.inf, 0),
                         (0,), (0, 0, 0), None, (1e308, 0)):
            with self.subTest(position=position), self.assertRaises(ValueError):
                oracle.fixture_polygons(0, position)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shifted.png"
            self.image(.6, transform=lambda x, y: (x - 6, y + 3)).save(path)
            stdout = io.StringIO()
            with patch("sys.argv", ["oracle", str(path), "--yaw", ".6",
                                     "--camera-iso", "-.375", ".375"]), \
                    patch("sys.stdout", stdout):
                self.assertEqual(oracle.main(), 0)
            result = json.loads(stdout.getvalue())
            self.assertEqual(result["camera_iso"], [-.375, .375])
            self.assertTrue(result["passed"])

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

    def test_retained_native_camera_phase_pass_and_failure_controls(self):
        root = Path(__file__).resolve().parents[2]
        evidence = root / "docs/pr-screenshots/codex/receiver-shadow-phase"
        expected = {row["case"]: row for row in
                    json.loads((evidence / "results.json").read_text())}
        for case, passed in (("near0-before", True), ("mid0-phase", True),
                             ("sampled-phase", False), ("subpixel-positive", True),
                             ("subpixel-negative", True), ("subpixel-sampled", False)):
            with self.subTest(case=case):
                row = expected[case]
                with Image.open(evidence / case / "screenshot_000001.png") as image:
                    result = oracle.check_image(image, row["yaw"], row["camera_iso"])
                self.assertEqual(result["counts"], row["counts"])
                self.assertEqual(result["camera_iso"], row["camera_iso"])
                self.assertEqual(result["camera_shift_pixels"], row["camera_shift_pixels"])
                self.assertTrue(result["coverage_valid"])
                self.assertEqual(result["passed"], passed)
                if not passed:
                    self.assertEqual(result["counts"]["missing"], 18)
                    self.assertEqual(result["counts"]["excess"], 638)

    def assert_native_evidence(self, directory, count, expected_pass):
        root = Path(__file__).resolve().parents[2]
        evidence = root / "docs/pr-screenshots/codex" / directory
        rows = json.loads((evidence / "captures.json").read_text())["cases"]
        self.assertEqual(len(rows), count)
        for row in rows:
            with self.subTest(case=row["case"]):
                with Image.open(evidence / row["image"]) as image:
                    result = oracle.check_image(image, row["yaw"], sun=row["sun_direction"],
                                                caster_offset=row["source_offset"])
                passed = expected_pass(row["case"])
                self.assertEqual(result["passed"], passed)
                self.assertEqual(result["counts"], row["counts"])
                self.assertTrue(result["coverage_valid"])
                self.assertEqual(result["counts"]["unknown"], 0)
                if not passed:
                    self.assertGreater(result["counts"]["missing"], 100)
                    self.assertGreater(result["counts"]["excess"], 100)

    def test_retained_native_oblique_pass_and_failure_controls(self):
        self.assert_native_evidence(
            "receiver-oblique-shadow", 16,
            lambda case: not case.endswith(("-sampled", "-fractional")),
        )

    def test_retained_native_rigid_origin_fix_and_translation_sweep(self):
        self.assert_native_evidence(
            "rigid-canvas-world-origin", 42, lambda case: not case.endswith("-sampled"),
        )


if __name__ == "__main__":
    unittest.main()
