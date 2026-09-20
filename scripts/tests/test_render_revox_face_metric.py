"""Resampled-cell face controls for the revoxelized detached display gate."""

import contextlib
import importlib.util
import io
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location(
    "revox_face_metric", SCRIPTS / "render-revox-face-metric.py")
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)
import render_metric_util  # noqa: E402
import render_revox_lattice  # noqa: E402

SIZE = 160
SCALE = (4, 2)
CENTER = (SIZE / 2, SIZE / 2)


def expected(fixture="cube", yaw=0.0, upright=False):
    return METRIC.expected_image(
        SIZE, SIZE, METRIC.FIXTURES[fixture], math.radians(yaw), upright, SCALE, CENTER)


def render(labels, palette):
    return bytearray(channel for label in labels for channel in palette[label])


class RevoxFaceMetricTest(unittest.TestCase):
    def test_identity_resample_is_the_authored_cube(self):
        labels, palette, stats = expected(upright=True)
        self.assertFalse(stats["clipped"])
        self.assertEqual(stats["occupied_cells"], METRIC.SOLID_EXTENT ** 3)
        face_area = METRIC.SOLID_EXTENT ** 2 * 2 * SCALE[0] * SCALE[1]
        for label in (1, 2, 3):
            self.assertEqual(sum(v == label for v in labels), face_area)

    def test_rotated_resample_differs_from_the_authored_cube(self):
        rotated, _, stats = expected()
        identity, _, _ = expected(upright=True)
        self.assertNotEqual(stats["occupied_cells"], METRIC.SOLID_EXTENT ** 3)
        self.assertNotEqual(bytes(rotated), bytes(identity))

    def test_mixed_parity_fixture_anchors_per_axis(self):
        cells, anchor, _ = render_revox_lattice.source_cells(METRIC.FIXTURES["parity"])
        self.assertEqual(len(cells), 12 * 12 * 11)
        self.assertEqual(anchor, (-0.5, -0.5, 0.0))

    def test_carved_fixture_drops_the_carved_quadrant(self):
        cells, anchor, _ = render_revox_lattice.source_cells(METRIC.FIXTURES["lprism"])
        self.assertEqual(len(cells), METRIC.SOLID_EXTENT ** 3 * 3 // 4)
        self.assertEqual(anchor, (-0.5, -0.5, -0.5))

    def test_yaw_rotates_the_normal_palette_and_recomposes_the_resample(self):
        _, palette_zero, stats_zero = expected(yaw=0)
        _, palette_ninety, _ = expected(yaw=90)
        _, _, stats_diagonal = expected(yaw=45)
        self.assertNotEqual(stats_zero["occupied_cells"], stats_diagonal["occupied_cells"])
        self.assertEqual(palette_zero[1:], [(0, 128, 128), (128, 0, 128), (128, 128, 0)])
        for color, expectation in zip(palette_ninety[1:],
                                      [(128, 0, 128), (255, 128, 128), (128, 128, 0)]):
            self.assertTrue(all(abs(a - b) <= 1 for a, b in zip(color, expectation)), color)

    def test_synthetic_display_of_the_resampled_cells_passes(self):
        for yaw in (0, 45):
            labels, palette, _ = expected(yaw=yaw)
            result, _ = METRIC.compare(SIZE, SIZE, 3, render(labels, palette), labels, palette)
            self.assertTrue(result["pass"], result)
            self.assertEqual(result["expected_pixels"], result["observed_pixels"])

    def test_face_swap_fails_with_identical_silhouette(self):
        labels, palette, _ = expected()
        pixels = render(labels, palette)
        interior = [index for index, label in enumerate(labels)
                    if label == 1 and all(labels[index + dx + dy * SIZE] == 1
                                          for dy in (-1, 0, 1) for dx in (-1, 0, 1))]
        first = interior[len(interior) // 2]
        pixels[first * 3:first * 3 + 3] = bytes(palette[3])
        result, _ = METRIC.compare(SIZE, SIZE, 3, pixels, labels, palette)
        self.assertEqual(result["missing_pixels"], 0)
        self.assertEqual(result["extra_pixels"], 0)
        self.assertEqual(result["wrong_face_pixels"], 1)
        self.assertFalse(result["pass"])

    def test_hole_and_spike_fail(self):
        labels, palette, _ = expected()
        holed = render(labels, palette)
        interior = next(index for index, label in enumerate(labels)
                        if label and all(labels[index + dx + dy * SIZE]
                                         for dy in (-1, 0, 1) for dx in (-1, 0, 1)))
        holed[interior * 3:interior * 3 + 3] = bytes(3)
        result, _ = METRIC.compare(SIZE, SIZE, 3, holed, labels, palette)
        self.assertGreater(result["missing_pixels"], 0)
        spiked = render(labels, palette)
        spiked[3:6] = bytes(palette[1])
        result, _ = METRIC.compare(SIZE, SIZE, 3, spiked, labels, palette)
        self.assertGreater(result["extra_pixels"], 0)

    def test_wrong_pose_fails_against_a_faithful_display(self):
        labels, palette, _ = expected(yaw=45)
        pixels = render(labels, palette)
        other, _, _ = expected(yaw=22.5)
        result, _ = METRIC.compare(SIZE, SIZE, 3, pixels, other, palette)
        self.assertFalse(result["pass"])
        self.assertGreater(result["missing_pixels"] + result["extra_pixels"], 0)

    def test_clipped_expectation_fails(self):
        labels, palette, stats = METRIC.expected_image(
            SIZE, SIZE, METRIC.FIXTURES["cube"], 0.0, True, (40, 20), CENTER)
        self.assertTrue(stats["clipped"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "clipped.png"
            METRIC.write_png(str(path), SIZE, SIZE, bytes(render(labels, palette)), 3)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(METRIC.main([str(path), "--fixture", "cube", "--yaw", "0",
                                              "--upright", "--iso-scale", "40", "20"]), 1)

    def test_blank_does_not_pass_vacuously(self):
        labels, palette, _ = expected()
        result, _ = METRIC.compare(SIZE, SIZE, 3, bytes(SIZE * SIZE * 3), labels, palette)
        self.assertFalse(result["pass"])

    def test_sun_visibility_classes(self):
        _, _, stats = METRIC.expected_image(
            SIZE, SIZE, METRIC.FIXTURES["cube"], 0.0, True, SCALE, CENTER, METRIC.DEFAULT_SUN)
        counts = [sum(v == label for v in stats["lit"]) for label in range(4)]
        self.assertGreater(counts[METRIC.LIT_LABEL], 0)
        self.assertEqual(counts[METRIC.SHADOWED_LABEL], 0)
        self.assertEqual(counts[METRIC.BACKFACING_LABEL], 0)
        _, _, averted = METRIC.expected_image(
            SIZE, SIZE, METRIC.FIXTURES["cube"], 0.0, True, SCALE, CENTER, (1.0, 1.0, 1.0))
        self.assertEqual(sum(v == METRIC.LIT_LABEL for v in averted["lit"]), 0)
        self.assertGreater(sum(v == METRIC.BACKFACING_LABEL for v in averted["lit"]), 0)
        _, _, rotated = METRIC.expected_image(
            SIZE, SIZE, METRIC.FIXTURES["cube"], math.radians(90), False, SCALE, CENTER,
            METRIC.DEFAULT_SUN)
        self.assertGreater(sum(v == METRIC.SHADOWED_LABEL for v in rotated["lit"]), 0)

    def test_face_halves_are_lit_independently(self):
        _, _, stats = METRIC.expected_image(
            SIZE, SIZE, METRIC.FIXTURES["cube"], math.radians(75), False, SCALE, CENTER,
            METRIC.DEFAULT_SUN)
        lit, trixels = stats["lit"], stats["trixels"]
        visibility = {owner: lit[index] for index, owner in enumerate(trixels.owner)
                      if owner and lit[index]}
        split = 0
        for first in range(1, len(trixels.entries) - 1, 2):
            second = first + 1
            if trixels.entries[first][0] != trixels.entries[second][0]:
                continue
            labels = {visibility.get(first), visibility.get(second)}
            split += labels == {METRIC.LIT_LABEL, METRIC.SHADOWED_LABEL}
        self.assertGreater(split, 0)

    def test_pixel_explanation_separates_camera_and_sun_visibility(self):
        args = ((102, 89), (SIZE, SIZE), METRIC.FIXTURES["cube"], 0.0, True, SCALE)
        explained = METRIC.explain_pixel(*args, METRIC.DEFAULT_SUN)
        self.assertFalse(explained["boundary_ambiguous"])
        self.assertEqual(explained["world_normal"], (-1.0, 0.0, 0.0))
        self.assertEqual(explained["sun_visibility"], METRIC.LIT_LABEL)
        self.assertIsNone(explained["blocker_cell"])
        backlit = METRIC.explain_pixel(*args, (1.0, 1.0, 1.0))
        self.assertEqual(backlit["camera_hits"], explained["camera_hits"])
        self.assertEqual(backlit["sun_visibility"], METRIC.BACKFACING_LABEL)
        empty = METRIC.explain_pixel((0, 0), *args[1:], METRIC.DEFAULT_SUN)
        self.assertEqual(empty["camera_hits"], [])

    def test_pixel_explanation_reports_blocker_and_boundary_ambiguities(self):
        shadow = METRIC.explain_pixel(
            (1412, 688), (2560, 1440), METRIC.FIXTURES["grounded"], math.radians(135),
            False, (32, 16), METRIC.DEFAULT_SUN)
        self.assertEqual(shadow["sun_visibility"], METRIC.SHADOWED_LABEL)
        self.assertEqual(shadow["blocker_cell"], (-6, 0, -4))
        corner = METRIC.explain_pixel(
            (80, 80), (161, 161), METRIC.FIXTURES["cube"], 0, True, (1, 1), METRIC.DEFAULT_SUN)
        self.assertTrue(corner["boundary_ambiguous"])
        self.assertNotIn("world_normal", corner)
        diagonal = METRIC.explain_pixel(
            (69, 91), (161, 161), METRIC.FIXTURES["cube"], 0, True, (2, 2), METRIC.DEFAULT_SUN)
        self.assertFalse(diagonal["boundary_ambiguous"])
        self.assertTrue(diagonal["shadow_sample_ambiguous"])
        self.assertEqual(diagonal["world_normal"], (0, -1, 0))
        self.assertNotIn("shadow_sample", diagonal)

    def test_partial_face_is_explained_by_nearer_voxel(self):
        # A nearer box covers one half of the farther box's X face.
        cells = [(0, 0, 0), (-1, -1, 0)]
        halves = METRIC.face_triangles((0, 0, 0), (-1, 0, 0))
        owners = []
        for triangle in halves:
            center = render_metric_util.centroid(triangle)
            hits = render_revox_lattice.camera_face_hits(cells, (0, 0, 0), METRIC.iso(center))
            owners.append(hits[0]["cell"])
        self.assertEqual(set(owners), set(cells))

    def test_polygon_face_ownership_matches_camera_box_rays(self):
        size, scale = 160, (4, 2)
        for yaw in (0, 45, 90, 135, 180, 225, 270, 315):
            fixture = METRIC.FIXTURES["grounded"]
            angle = math.radians(yaw)
            labels, _, _ = METRIC.expected_image(
                size, size, fixture, angle, False, scale, (size / 2, size / 2))
            rotation, _ = render_revox_lattice.composed_rotation(fixture, angle, False)
            lattice = render_revox_lattice.Resample(fixture, rotation)
            cells = lattice.occupied()
            checked = 0
            for y in range(2, size - 2, 3):
                for x in range(2, size - 2, 3):
                    label = labels[y * size + x]
                    if not label or any(labels[(y + dy) * size + x + dx] != label
                                        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))):
                        continue
                    point = ((x + 0.5 - size / 2) / scale[0],
                             (y + 0.5 - size / 2) / scale[1])
                    hits = render_revox_lattice.camera_face_hits(cells, lattice.anchor, point)
                    self.assertTrue(hits, (yaw, point))
                    self.assertIn(label - 1, hits[0]["axes"], (yaw, point, hits[0]))
                    checked += 1
            self.assertGreater(checked, 10)

    def test_ray_box_distance(self):
        box = ((0.0, 0.0, 0.0), (1.0, 1.0, 1.0))
        distance = render_metric_util.ray_box_distance
        self.assertAlmostEqual(
            distance((-2.0, 0.5, 3.0), (1.0, 0.0, 0.0), *box, 10.0), 2.0, places=4)
        self.assertAlmostEqual(
            distance((-2.0, 0.5, 0.5), (1.0, 0.0, 0.0), *box, 10.0), 0.0, places=4)
        self.assertAlmostEqual(
            distance((2.0, 0.5, 0.5), (1.0, 0.0, 0.0), *box, 10.0), 1.0, places=4)

    def test_shadow_overlay_false_and_missed(self):
        size, scale = 400, (12, 6)
        labels, _, stats = METRIC.expected_image(
            size, size, METRIC.FIXTURES["cube"], math.radians(90), False, scale,
            (size / 2, size / 2), METRIC.DEFAULT_SUN)
        lit, trixels = stats["lit"], stats["trixels"]
        overlay = bytearray(size * size * 3)
        for index, value in enumerate(lit):
            if value == METRIC.SHADOWED_LABEL:
                overlay[index * 3:index * 3 + 3] = bytes(METRIC.SHADOW_MAGENTA)
        result, _ = METRIC.compare_shadow(size, size, 3, overlay, labels, lit, trixels)
        self.assertTrue(result["pass"], result)
        self.assertGreater(result["shadowed_interior_pixels"], 0)
        first_lit = next(index for index, value in enumerate(lit)
                         if value == METRIC.LIT_LABEL and all(
                             trixels.owner[index + dx + dy * size] == trixels.owner[index]
                             and labels[index + dx + dy * size] == labels[index]
                             for dy in (-1, 0, 1) for dx in (-1, 0, 1)))
        overlay[first_lit * 3:first_lit * 3 + 3] = bytes(METRIC.SHADOW_MAGENTA)
        result, _ = METRIC.compare_shadow(size, size, 3, overlay, labels, lit, trixels, 0.0)
        self.assertEqual(result["false_shadow_pixels"], 1)
        self.assertEqual(result["grazing_false_shadow_pixels"], 0)
        self.assertFalse(result["pass"])
        result, _ = METRIC.compare_shadow(size, size, 3, bytes(size * size * 3), labels, lit,
                                          trixels)
        self.assertEqual(result["missed_shadow_pixels"], result["shadowed_interior_pixels"])
        self.assertGreater(result["missed_shadow_pixels"], 0)

    def test_grazing_false_shadow_is_tolerated_and_clear_is_not(self):
        size, scale = 400, (12, 6)
        labels, _, stats = METRIC.expected_image(
            size, size, METRIC.FIXTURES["cube"], math.radians(67.5), False, scale,
            (size / 2, size / 2), METRIC.DEFAULT_SUN)
        lit, trixels = stats["lit"], stats["trixels"]
        interior = {}
        for index, owner in enumerate(trixels.owner):
            if owner and lit[index] == METRIC.LIT_LABEL and all(
                    trixels.owner[index + dx + dy * size] == owner
                    for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                interior.setdefault(owner, []).append(index)
        by_clearance = sorted(interior, key=trixels.clearance)
        grazing, clear = by_clearance[0], by_clearance[-1]
        self.assertLessEqual(trixels.clearance(grazing), METRIC.TERMINATOR_TOLERANCE)
        self.assertGreater(trixels.clearance(clear), METRIC.TERMINATOR_TOLERANCE)
        for owner, expect_pass in ((grazing, True), (clear, False)):
            overlay = bytearray(size * size * 3)
            for index, value in enumerate(lit):
                if value == METRIC.SHADOWED_LABEL or trixels.owner[index] == owner:
                    overlay[index * 3:index * 3 + 3] = bytes(METRIC.SHADOW_MAGENTA)
            result, _ = METRIC.compare_shadow(size, size, 3, overlay, labels, lit, trixels)
            self.assertEqual(result["false_shadow_pixels"], len(interior[owner]))
            self.assertEqual(result["grazing_false_shadow_pixels"],
                             len(interior[owner]) if expect_pass else 0)
            self.assertEqual(result["pass"], expect_pass, result)

    def test_all_lit_shadow_fixture_cannot_certify_occlusion(self):
        size = 320
        labels, _, stats = METRIC.expected_image(
            size, size, METRIC.FIXTURES["cube"], math.radians(22.5), False,
            (8, 4), (size / 2, size / 2), METRIC.DEFAULT_SUN)
        result, _ = METRIC.compare_shadow(
            size, size, 3, bytes(size * size * 3), labels, stats["lit"], stats["trixels"])
        self.assertGreater(result["lit_interior_pixels"], 0)
        self.assertEqual(result["shadowed_interior_pixels"], 0)
        self.assertEqual(result["false_shadow_pixels"], 0)
        self.assertEqual(result["missed_shadow_pixels"], 0)
        self.assertFalse(result["occlusion_exercised"])
        self.assertFalse(result["pass"])

    def test_cli_shadow_overlay(self):
        size, scale = 400, (12, 6)
        labels, _, stats = METRIC.expected_image(
            size, size, METRIC.FIXTURES["cube"], math.radians(45), False, scale,
            (size / 2, size / 2), METRIC.DEFAULT_SUN)
        overlay = bytearray(size * size * 3)
        for index, value in enumerate(stats["lit"]):
            if value == METRIC.SHADOWED_LABEL:
                overlay[index * 3:index * 3 + 3] = bytes(METRIC.SHADOW_MAGENTA)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shadow.png"
            METRIC.write_png(str(path), size, size, bytes(overlay), 3)
            args = [str(path), "--fixture", "cube", "--yaw", "45", "--shadow-overlay",
                    "--iso-scale", str(scale[0]), str(scale[1]),
                    "--diagnostic-prefix", str(Path(directory) / "diag")]
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(METRIC.main(args), 0)
            report = json.loads(output.getvalue())
            self.assertEqual(report["scope"], "resampled_cell_sun")
            self.assertGreater(report["observed_shadow_pixels"], 0)
            self.assertTrue((Path(directory) / "diag-errors.png").exists())
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as zero_sun:
                    METRIC.main(args + ["--sun", "0", "0", "0"])
                with self.assertRaises(SystemExit) as negative_tolerance:
                    METRIC.main(args + ["--terminator-tolerance", "-1"])
            self.assertEqual(zero_sun.exception.code, 2)
            self.assertEqual(negative_tolerance.exception.code, 2)

    def test_cli_exit_code_and_determinism(self):
        labels, palette, _ = expected(yaw=45)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "revox.png"
            METRIC.write_png(str(path), SIZE, SIZE, bytes(render(labels, palette)), 3)
            args = [str(path), "--fixture", "cube", "--yaw", "45",
                    "--iso-scale", str(SCALE[0]), str(SCALE[1])]
            reports = []
            for _ in range(2):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(METRIC.main(args), 0)
                reports.append(json.loads(output.getvalue()))
            self.assertEqual(reports[0], reports[1])
            wrong_yaw = [value if value != "45" else "0" for value in args]
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(METRIC.main(wrong_yaw), 1)


if __name__ == "__main__":
    unittest.main()
