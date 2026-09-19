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

    def test_carved_fixture_drops_the_carved_quadrant(self):
        cells, anchor, _ = METRIC.source_cells(METRIC.FIXTURES["lprism"])
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
            SIZE, SIZE, METRIC.FIXTURES["cube"], 0.0, False, SCALE, CENTER, METRIC.DEFAULT_SUN)
        self.assertGreater(sum(v == METRIC.SHADOWED_LABEL for v in rotated["lit"]), 0)

    def test_shadow_overlay_false_and_missed(self):
        size, scale = 400, (12, 6)
        labels, _, stats = METRIC.expected_image(
            size, size, METRIC.FIXTURES["cube"], 0.0, False, scale, (size / 2, size / 2),
            METRIC.DEFAULT_SUN)
        lit = stats["lit"]
        overlay = bytearray(size * size * 3)
        for index, value in enumerate(lit):
            if value == METRIC.SHADOWED_LABEL:
                overlay[index * 3:index * 3 + 3] = bytes(METRIC.SHADOW_MAGENTA)
        result, _ = METRIC.compare_shadow(size, size, 3, overlay, labels, lit)
        self.assertTrue(result["pass"], result)
        self.assertGreater(result["shadowed_interior_pixels"], 0)
        first_lit = next(index for index, value in enumerate(lit)
                         if value == METRIC.LIT_LABEL and all(
                             lit[index + dx + dy * size] == value
                             and labels[index + dx + dy * size] == labels[index]
                             for dy in (-1, 0, 1) for dx in (-1, 0, 1)))
        overlay[first_lit * 3:first_lit * 3 + 3] = bytes(METRIC.SHADOW_MAGENTA)
        result, _ = METRIC.compare_shadow(size, size, 3, overlay, labels, lit)
        self.assertEqual(result["false_shadow_pixels"], 1)
        self.assertFalse(result["pass"])
        result, _ = METRIC.compare_shadow(size, size, 3, bytes(size * size * 3), labels, lit)
        self.assertEqual(result["missed_shadow_pixels"], result["shadowed_interior_pixels"])
        self.assertGreater(result["missed_shadow_pixels"], 0)

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
            self.assertEqual(zero_sun.exception.code, 2)

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
