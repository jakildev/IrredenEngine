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

SIDE_CASES = (
    ("x-neg", (-.8, 0, -.6), (-8, 0, 0), (0, 3)),
    ("x-pos", (.8, 0, -.6), (8, 0, 0), (1, 2)),
    ("y-neg", (0, -.8, -.6), (0, 0, 0), (0, 1)),
    ("y-pos", (0, .8, -.6), (0, 0, 0), (2, 3)),
)


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
              caster_offset=(0, 0, 0), face="top"):
        polygons = oracle.fixture_polygons(yaw, sun=sun, caster_offset=caster_offset, face=face)
        image = Image.new("RGB", oracle.IMAGE_SIZE, lit_color)
        shadow = polygons["shadow"]
        if transform:
            shadow = [transform(x, y) for x, y in shadow]
        ImageDraw.Draw(image).polygon(shadow, fill=oracle.SHADOW_COLOR)
        return image

    def test_signed_side_polygons_match_independent_world_rays(self):
        for face, sun, base_offset, quadrants in SIDE_CASES:
            axis = 0 if face.startswith("x") else 1
            plane = (-1 if face.endswith("neg") else 1) * (9 if axis == 0 else 3)
            span = 3 if axis == 0 else 9
            for residual in ((0, 0, 0), (.137, -.213, .251)):
                offset = tuple(a + b for a, b in zip(base_offset, residual, strict=True))
                for quadrant in quadrants:
                    yaw = .3 + quadrant * math.pi / 2
                    with self.subTest(face=face, offset=offset, yaw=yaw):
                        polygons = oracle.fixture_polygons(
                            yaw, sun=sun, caster_offset=offset, face=face)
                        hits = misses = 0
                        for horizontal in range(-span * 4, span * 4):
                            for vertical in range(-64, -32):
                                point = [0, 0, vertical / 4 + .029]
                                point[axis] = plane
                                point[1 - axis] = horizontal / 4 + .017
                                x, y, z = point
                                view_x = math.cos(yaw) * x + math.sin(yaw) * y
                                view_y = -math.sin(yaw) * x + math.cos(yaw) * y
                                screen = (1280 + 16 * (view_y - view_x),
                                          720 + 8 * (-view_x - view_y + 2 * z))
                                margin = oracle.polygon_margin(screen, polygons["shadow"])
                                if abs(margin) < 1e-7:
                                    continue
                                blocked = box_blocks_ray(point, sun, offset)
                                self.assertEqual(margin > 0, blocked,
                                                 (face, yaw, offset, point, margin))
                                hits += blocked
                                misses += not blocked
                        self.assertGreater(hits, 50)
                        self.assertGreater(misses, 300)

    def test_signed_side_rasters_palette_and_geometric_coverage(self):
        for face, sun, offset, quadrants in SIDE_CASES:
            for quadrant in quadrants:
                yaw = .3 + quadrant * math.pi / 2
                with self.subTest(face=face, yaw=yaw):
                    image = self.image(yaw, sun=sun, caster_offset=offset,
                                       lit_color=(69, 103, 206), face=face)
                    result = oracle.check_image(image, yaw, sun=sun,
                                                caster_offset=offset, face=face)
                    self.assertTrue(result["passed"], result)
                    self.assertEqual(result["face"], face)
                    self.assertEqual(result["lit_color"], [69, 103, 206])
                    self.assertGreaterEqual(result["counts"]["inside"], 500)
                    self.assertGreaterEqual(result["counts"]["outside"], 2000)
                    self.assertEqual(result["counts"]["unknown"], 0)
        top = oracle.check_image(self.image(), 0)
        self.assertEqual(top["coverage_requirements"],
                         dict(eligible=20000, inside=2500, outside=18000))
        for quadrant, eligible, outside in ((0, 4865, 2000), (3, 9223, 3075)):
            yaw = .3 + quadrant * math.pi / 2
            # X-side projected area: 6*8 world units times the 16*16 screen
            # determinant, scaled by abs(cos(yaw)-sin(yaw)).
            area = 48 * 256 * abs(math.cos(yaw) - math.sin(yaw))
            polygons = oracle.fixture_polygons(yaw, sun=(-.8, 0, -.6),
                                                caster_offset=(-8, 0, 0), face="x-neg")
            requirements = oracle.coverage_requirements(polygons, "x-neg")
            self.assertEqual(requirements["eligible"], eligible)
            self.assertEqual(eligible, math.ceil(.6 * area))
            self.assertEqual(requirements["outside"], outside)

    def test_side_shift_parity_palette_and_missing_shadow_controls(self):
        for face, sun, offset, quadrants in SIDE_CASES:
            yaw = .3 + quadrants[0] * math.pi / 2
            for dx, dy in ((8, 0), (0, 4), (-8, -4)):
                with self.subTest(face=face, shift=(dx, dy)):
                    image = self.image(yaw, sun=sun, caster_offset=offset, face=face,
                                       lit_color=(69, 103, 206),
                                       transform=lambda x, y: (x + dx, y + dy))
                    result = oracle.check_image(image, yaw, sun=sun,
                                                caster_offset=offset, face=face)
                    self.assertTrue(result["coverage_valid"])
                    self.assertFalse(result["passed"])
                    self.assertEqual(result["counts"]["unknown"], 0)
                    self.assertGreater(result["counts"]["missing"], 0)
                    self.assertGreater(result["counts"]["excess"], 0)
            for color, failure in (((69, 103, 206), "missing"),
                                   (oracle.SHADOW_COLOR, "excess"),
                                   (oracle.LIT_COLOR, "unknown")):
                with self.subTest(face=face, color=color):
                    result = oracle.check_image(Image.new("RGB", oracle.IMAGE_SIZE, color),
                                                yaw, sun=sun, caster_offset=offset, face=face)
                    self.assertTrue(result["coverage_valid"])
                    self.assertFalse(result["passed"])
                    self.assertGreater(result["counts"][failure], 500)

    def test_side_camera_phase_and_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "side.png"
            yaw = .3 + math.pi
            self.image(yaw, sun=(.8, 0, -.6), caster_offset=(8, 0, 0), face="x-pos",
                       lit_color=(69, 103, 206),
                       transform=lambda x, y: (x - 6, y + 3)).save(path)
            stdout = io.StringIO()
            with patch("sys.argv", ["oracle", str(path), "--yaw", str(yaw),
                                     "--face", "x-pos", "--sun-direction", "4", "0", "-3",
                                     "--source-offset", "8", "0", "0",
                                     "--camera-iso", "-.375", ".375"]), \
                    patch("sys.stdout", stdout):
                self.assertEqual(oracle.main(), 0)
            result = json.loads(stdout.getvalue())
            self.assertTrue(result["passed"])
            self.assertEqual(result["face"], "x-pos")
            self.assertEqual(result["camera_shift_pixels"], [-6, 3])
            self.assertEqual(result["lit_color"], [69, 103, 206])

    def test_sides_reject_back_faces_unlit_faces_and_insufficient_clearance(self):
        for face, expected in (("top", (2, -16, -1)), ("x-neg", (0, -9, -1)),
                               ("x-pos", (0, 9, 1)), ("y-neg", (1, -3, -1)),
                               ("y-pos", (1, 3, 1))):
            self.assertEqual(oracle.receiver_face(face), expected)
        for face in (None, "bottom", "x", 0):
            with self.subTest(face=face), self.assertRaisesRegex(ValueError, "receiver face"):
                oracle.receiver_face(face)
            with self.subTest(face=face), self.assertRaisesRegex(ValueError, "receiver face"):
                oracle.fixture_polygons(0, face=face)
        for face, sun, offset, quadrants in SIDE_CASES:
            yaw = .3 + quadrants[0] * math.pi / 2
            with self.subTest(face=face):
                with self.assertRaisesRegex(ValueError, "camera-visible"):
                    oracle.fixture_polygons(yaw + math.pi, sun=sun, caster_offset=offset, face=face)
                with self.assertRaisesRegex(ValueError, "sunlit"):
                    oracle.fixture_polygons(yaw, caster_offset=offset, face=face)
                opposite_sun = (-sun[0], -sun[1], sun[2])
                with self.assertRaisesRegex(ValueError, "sunlit"):
                    oracle.receiver_lit_color(opposite_sun, face)
                axis = 0 if face.startswith("x") else 1
                sign = -1 if face.endswith("neg") else 1
                plane = sign * (9 if axis == 0 else 3)
                component = math.sin(.47 / 2) / math.sqrt(3)
                scalar = math.cos(.47 / 2)
                support = 1.5 * (abs(1 - 4 * component * component)
                                  + abs(2 * component * component + 2 * scalar * component)
                                  + abs(2 * component * component - 2 * scalar * component))
                touching = [0, 0, 0]
                touching[axis] = plane + sign * support - 12 * sun[axis]
                for clearance in (-1e-8, 1e-8):
                    position = touching.copy()
                    position[axis] += sign * clearance
                    if clearance < 0:
                        with self.assertRaisesRegex(ValueError, "whole caster"):
                            oracle.fixture_polygons(yaw, sun=sun, caster_offset=position, face=face)
                    else:
                        oracle.fixture_polygons(yaw, sun=sun, caster_offset=position, face=face)

    def test_side_coverage_fails_for_grazing_offscreen_or_unshadowed_receiver(self):
        cases = ((math.pi / 4 - .001, (0, 0), (-8, 0, 0)),
                 (.3, (200, 0), (-8, 0, 0)),
                 (.3, (0, 0), (-8, 0, 100)))
        for yaw, camera, offset in cases:
            with self.subTest(yaw=yaw, camera=camera, offset=offset):
                result = oracle.check_image(Image.new("RGB", oracle.IMAGE_SIZE, (69, 103, 206)),
                                            yaw, camera, (-.8, 0, -.6), offset, "x-neg")
                self.assertFalse(result["coverage_valid"])
                self.assertFalse(result["passed"])

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

    def local_light_references(self, yaw=0, ambient=oracle.DEFAULT_AMBIENT):
        """Lit/dark references sharing a striped orange local-light term."""
        lit_color = oracle.receiver_lit_color(oracle.sun_direction((0, 0, -1)), ambient=ambient)
        shadow_color = oracle.receiver_shadow_color(ambient)
        lit = Image.new("RGB", oracle.IMAGE_SIZE)
        dark = Image.new("RGB", oracle.IMAGE_SIZE)
        lit_draw, dark_draw = ImageDraw.Draw(lit), ImageDraw.Draw(dark)
        for x in range(0, oracle.IMAGE_SIZE[0], 7):
            light = ((x // 7) % 23, (x // 7) % 11, 0)
            box = (x, 0, x + 6, oracle.IMAGE_SIZE[1])
            lit_draw.rectangle(box, fill=tuple(map(sum, zip(lit_color, light))))
            dark_draw.rectangle(box, fill=tuple(map(sum, zip(shadow_color, light))))
        return lit, dark

    def composite(self, lit, dark, shadow):
        mask = Image.new("L", oracle.IMAGE_SIZE)
        ImageDraw.Draw(mask).polygon(shadow, fill=255)
        image = lit.copy()
        image.paste(dark, mask=mask)
        return image

    def test_ambient_palettes_pass_and_reject_the_default_palette(self):
        for ambient, shadow_color in ((.15, (12, 18, 36)), (.55, (44, 66, 132))):
            with self.subTest(ambient=ambient):
                polygons = oracle.fixture_polygons(.6)
                image = Image.new("RGB", oracle.IMAGE_SIZE, oracle.LIT_COLOR)
                ImageDraw.Draw(image).polygon(polygons["shadow"], fill=shadow_color)
                result = oracle.check_image(image, .6, ambient=ambient)
                self.assertTrue(result["passed"], result["counts"])
                self.assertEqual(result["shadow_color"], list(shadow_color))
                self.assertEqual(result["sun_ambient"], ambient)
                default = oracle.check_image(image, .6)
                self.assertFalse(default["passed"])
                self.assertEqual(default["counts"]["missing"], default["counts"]["inside"])
                self.assertGreaterEqual(default["counts"]["unknown"], default["counts"]["inside"])
        # An oblique sun makes the lit color ambient-dependent: .55 + .45 * .8 = .91.
        sun = oracle.sun_direction((.6, 0, -.8))
        self.assertEqual(oracle.receiver_lit_color(sun, ambient=.55), (73, 109, 218))
        self.assertEqual(oracle.receiver_lit_color(sun), (69, 103, 206))

    def test_ambient_validation_and_rounding_ties(self):
        for ambient in (None, "bad", math.nan, math.inf, -.01, 1, 1.5):
            with self.subTest(ambient=ambient), self.assertRaisesRegex(ValueError, "ambient"):
                oracle.receiver_shadow_color(ambient)
        with self.assertRaisesRegex(ValueError, "rounding tie"):
            oracle.receiver_shadow_color(8.5 / 80)
        image = Image.new("RGB", oracle.IMAGE_SIZE, oracle.LIT_COLOR)
        with self.assertRaisesRegex(ValueError, "ambient"):
            oracle.check_image(image, 0, ambient=1)

    def test_local_light_references_grade_geometry_exactly(self):
        for yaw in (0, .6, math.pi + .6):
            with self.subTest(yaw=yaw):
                lit, dark = self.local_light_references(yaw)
                polygons = oracle.fixture_polygons(yaw)
                image = self.composite(lit, dark, polygons["shadow"])
                result = oracle.check_image(image, yaw, references=(lit, dark))
                self.assertTrue(result["passed"], result["counts"])
                self.assertEqual(result["palette"], "reference")
                self.assertGreater(result["counts"]["local_light"],
                                   result["counts"]["eligible"] // 2)
                self.assertFalse(oracle.check_image(image, yaw)["passed"])
                shifted = self.composite(lit, dark, [(x + 8, y) for x, y in polygons["shadow"]])
                result = oracle.check_image(shifted, yaw, references=(lit, dark))
                self.assertFalse(result["passed"])
                self.assertEqual(result["counts"]["unknown"], 0)
                self.assertGreater(result["counts"]["missing"], 100)
                self.assertGreater(result["counts"]["excess"], 100)
                foreign = image.copy()
                x, y, _ = next(sample for sample in oracle.receiver_samples(polygons)
                               if sample[2] < -10)
                foreign.putpixel((x, y), dark.getpixel((x + 7, y)))
                result = oracle.check_image(foreign, yaw, references=(lit, dark))
                self.assertEqual(result["counts"]["unknown"], 1)

    def test_local_light_references_match_ambient_and_no_light_controls(self):
        lit, dark = self.local_light_references(ambient=.55)
        polygons = oracle.fixture_polygons(0)
        image = self.composite(lit, dark, polygons["shadow"])
        self.assertTrue(oracle.check_image(image, 0, ambient=.55, references=(lit, dark))["passed"])
        with self.assertRaisesRegex(ValueError, "sun-term"):
            oracle.check_image(image, 0, references=(lit, dark))
        plain = (Image.new("RGB", oracle.IMAGE_SIZE, oracle.LIT_COLOR),
                 Image.new("RGB", oracle.IMAGE_SIZE, oracle.SHADOW_COLOR))
        result = oracle.check_image(self.image(), 0, references=plain)
        self.assertTrue(result["passed"])
        self.assertEqual(result["counts"]["local_light"], 0)
        self.assertEqual(result["counts"]["missing"] + result["counts"]["excess"], 0)

    def test_references_reject_swapped_shadowed_clipped_and_transparent_pairs(self):
        lit, dark = self.local_light_references()
        shadow = oracle.fixture_polygons(0)["shadow"]
        clipped = lit.copy()
        ImageDraw.Draw(clipped).rectangle((0, 0, *oracle.IMAGE_SIZE), fill=(255, 255, 240))
        transparent = lit.convert("RGBA")
        transparent.putalpha(254)
        for name, pair in (("swapped", (dark, lit)), ("identical", (lit, lit)),
                           ("shadowed lit", (self.composite(lit, dark, shadow), dark)),
                           ("clipped", (clipped, dark)), ("transparent", (transparent, dark)),
                           ("one image", (lit,))):
            with self.subTest(pair=name), self.assertRaises(ValueError):
                oracle.check_image(self.composite(lit, dark, shadow), 0, references=pair)
        with self.assertRaisesRegex(ValueError, "requires"):
            oracle.check_image(self.image(), 0, references=(lit, dark.resize((1280, 720))))

    def test_reference_cli_requires_both_images_and_reports_local_light(self):
        lit, dark = self.local_light_references()
        image = self.composite(lit, dark, oracle.fixture_polygons(0)["shadow"])
        with tempfile.TemporaryDirectory() as directory:
            paths = {name: Path(directory) / f"{name}.png" for name in ("image", "lit", "dark")}
            for name, picture in (("image", image), ("lit", lit), ("dark", dark)):
                picture.save(paths[name])
            stdout = io.StringIO()
            with patch("sys.argv", ["oracle", str(paths["image"]), "--yaw", "0",
                                     "--lit-reference", str(paths["lit"]),
                                     "--dark-reference", str(paths["dark"])]), \
                    patch("sys.stdout", stdout):
                self.assertEqual(oracle.main(), 0)
            result = json.loads(stdout.getvalue())
            self.assertTrue(result["passed"])
            self.assertEqual(result["palette"], "reference")
            self.assertGreater(result["counts"]["local_light"], 0)
            with patch("sys.argv", ["oracle", str(paths["image"]), "--yaw", "0",
                                     "--lit-reference", str(paths["lit"])]), \
                    patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit) as exit_:
                oracle.main()
            self.assertEqual(exit_.exception.code, 2)

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

    def test_retained_signed_sides_and_live_cardinal_crossing(self):
        evidence = (Path(__file__).resolve().parents[2]
                    / "docs/pr-screenshots/codex/receiver-side-shadow")
        rows = json.loads((evidence / "captures.json").read_text())
        self.assertEqual(len(rows), 19)
        self.assertEqual(sum(row["case"] == "live-cardinal-crossing" for row in rows), 7)
        for row in rows:
            with self.subTest(image=row["image"]):
                with Image.open(evidence / row["image"]) as image:
                    result = oracle.check_image(
                        image, row["yaw"], sun=row["sun_direction"],
                        caster_offset=row["source_offset"], face=row["face"],
                    )
                self.assertTrue(result["coverage_valid"])
                self.assertEqual(result["counts"], row["counts"])
                self.assertEqual(result["counts"]["unknown"], 0)
                self.assertEqual(result["passed"], not row["case"].endswith("-sampled"))
                if row["case"].endswith("-sampled"):
                    self.assertGreater(result["counts"]["missing"], 50)
                    self.assertGreater(result["counts"]["excess"], 50)


    def test_retained_native_ambient_and_local_light_controls(self):
        evidence = (Path(__file__).resolve().parents[2]
                    / "docs/pr-screenshots/claude/4757-receiver-shadow-ambient-local-light")
        rows = json.loads((evidence / "results.json").read_text())
        retained = sorted(path.parent for path in (evidence / "retained").glob("*/*/main.png"))
        self.assertEqual(len(retained), 4)
        for folder in retained:
            key = f"{folder.parent.name}/{folder.name}"
            row = rows[key]
            with self.subTest(case=key):
                references = None
                if (folder / "lit.png").exists():
                    references = (Image.open(folder / "lit.png"), Image.open(folder / "dark.png"))
                with Image.open(folder / "main.png") as image:
                    result = oracle.check_image(image, row["yaw"], row["camera_iso"],
                                                ambient=row["sun_ambient"], references=references)
                self.assertEqual(result["counts"], row["counts"])
                self.assertEqual(result["passed"], row["passed"])
                self.assertEqual(result["passed"], key.endswith("surface1"))
                self.assertEqual(result["counts"]["unknown"], 0)
                if references is not None:
                    self.assertGreater(result["counts"]["local_light"],
                                       result["counts"]["eligible"] // 2)

if __name__ == "__main__":
    unittest.main()
