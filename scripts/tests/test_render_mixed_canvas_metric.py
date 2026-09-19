"""Lifecycle and footprint controls for the mixed voxel/SDF private-canvas gate."""

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
    "mixed_canvas_metric", SCRIPTS / "render-mixed-canvas-metric.py")
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)

SIZE = 200
SCALE = (4, 2)
CENTER = (SIZE / 2, SIZE / 2)


def expected(yaw=0.0, identity=False):
    return METRIC.expected_image(SIZE, SIZE, math.radians(yaw), identity, SCALE, CENTER)


def render(labels, palette):
    return bytearray(channel for label in labels for channel in palette[label])


def compare(pixels, labels, palette, ambiguous, guards):
    return METRIC.compare(SIZE, SIZE, 3, pixels, labels, palette, ambiguous, guards, SCALE)[0]


def marker_pixels(labels):
    return [index for index, label in enumerate(labels) if label == METRIC.MARKER_LABEL]


class MixedCanvasMetricTest(unittest.TestCase):
    def test_both_producers_have_interior_pixels(self):
        for yaw in (0, 45):
            labels, palette, ambiguous, guards, clipped = expected(yaw=yaw)
            self.assertFalse(clipped)
            self.assertEqual(len(guards), len(METRIC.MARKER_CENTERS))
            result = compare(render(labels, palette), labels, palette, ambiguous, guards)
            self.assertTrue(result["lifecycle_pass"], result)
            self.assertTrue(result["footprint_pass"], result)
            self.assertEqual(result["marker_centroid_error_px"], [0.0, 0.0])
            self.assertEqual(result["marker_area_ratio"], 1.0)

    def test_revoxelized_fixture_places_owner_and_markers(self):
        markers = ((-8.0, -8.0, -8.0), (-5.0, -8.0, -8.0))
        labels, palette, ambiguous, guards, clipped = METRIC.expected_image(
            SIZE, SIZE, 0.0, False, SCALE, CENTER, "parity", (0.0, 0.0, 0.0), markers)
        self.assertFalse(clipped)
        self.assertEqual(palette[METRIC.FRAME_LABEL], METRIC.FIXTURE_ALBEDO["parity"])
        result = compare(render(labels, palette), labels, palette, ambiguous, guards)
        self.assertTrue(result["footprint_pass"], result)
        # A translated owner moves the solid, not the world-anchored markers.
        shifted, _, _, _, _ = METRIC.expected_image(
            SIZE, SIZE, 0.0, False, SCALE, CENTER, "parity", (4.0, 2.0, -1.0), markers)
        self.assertNotEqual(bytes(shifted), bytes(labels))
        self.assertEqual(marker_pixels(shifted), marker_pixels(labels))
        # One iso row down (the parity box's half-cell phase at density 1) keeps
        # the lifecycle tolerance and fails the strict footprint.
        moved = render(labels, palette)
        for index in marker_pixels(labels):
            moved[index * 3:index * 3 + 3] = bytes(palette[METRIC.FRAME_LABEL])
        for index in marker_pixels(labels):
            target = index + SCALE[1] * SIZE
            moved[target * 3:target * 3 + 3] = bytes(palette[METRIC.MARKER_LABEL])
        result = compare(moved, labels, palette, ambiguous, guards)
        self.assertEqual(result["marker_centroid_error_px"], [0.0, float(SCALE[1])])
        self.assertTrue(result["lifecycle_pass"], result)
        self.assertFalse(result["footprint_pass"], result)

    def test_marker_snaps_to_the_revoxelized_lattice(self):
        resample = METRIC.lattice(0.0, False, "parity")
        self.assertEqual(resample.anchor, (-0.5, -0.5, 0.0))
        for center, cell in (((-8.5, -8.5, -8.0), (-8.5, -8.5, -8.0)),
                             ((-8.25, -8.25, -8.0), (-8.5, -8.5, -8.0)),
                             ((-8.75, -8.75, -8.0), (-8.5, -8.5, -8.0)),
                             ((-8.5, -8.5, -8.4), (-8.5, -8.5, -8.0))):
            self.assertEqual(METRIC.marker_cell(center, 0.0, resample), cell)
        self.assertEqual(METRIC.marker_cell((3.0, 0.25, 0.0), 0.0, None), (3.0, 0.25, 0.0))

    def test_absent_marker_fails_lifecycle(self):
        labels, palette, ambiguous, guards, _ = expected()
        pixels = render(labels, palette)
        for index in marker_pixels(labels):
            pixels[index * 3:index * 3 + 3] = bytes(palette[METRIC.FRAME_LABEL])
        result = compare(pixels, labels, palette, ambiguous, guards)
        self.assertFalse(result["marker_present"])
        self.assertFalse(result["lifecycle_pass"])

    def test_marker_displaced_by_three_texels_fails_lifecycle(self):
        labels, palette, ambiguous, guards, _ = expected()
        pixels = render(labels, palette)
        shift = 3 * SCALE[0]
        for index in marker_pixels(labels):
            pixels[index * 3:index * 3 + 3] = bytes(palette[METRIC.FRAME_LABEL])
        for index in marker_pixels(labels):
            x, y = index % SIZE + shift, index // SIZE
            if x < SIZE:
                target = y * SIZE + x
                pixels[target * 3:target * 3 + 3] = bytes(palette[METRIC.MARKER_LABEL])
        result = compare(pixels, labels, palette, ambiguous, guards)
        self.assertFalse(result["marker_placed"])
        self.assertFalse(result["lifecycle_pass"])

    def test_marker_dilated_by_one_texel_keeps_lifecycle_but_not_footprint(self):
        labels, palette, ambiguous, guards, _ = expected()
        pixels = render(labels, palette)
        for index in marker_pixels(labels):
            x, y = index % SIZE, index // SIZE
            for dy in range(0, 2 * SCALE[1]):
                target = (y + dy) * SIZE + x
                if y + dy < SIZE and labels[target] != METRIC.MARKER_LABEL:
                    pixels[target * 3:target * 3 + 3] = bytes(palette[METRIC.MARKER_LABEL])
        result = compare(pixels, labels, palette, ambiguous, guards)
        self.assertTrue(result["lifecycle_pass"], result)
        self.assertFalse(result["footprint_pass"])

    def test_frame_damage_outside_the_guard_fails(self):
        labels, palette, ambiguous, guards, _ = expected()
        pixels = render(labels, palette)
        damaged = next(index for index, label in enumerate(labels)
                       if label == METRIC.FRAME_LABEL
                       and not METRIC.inside_guard(index % SIZE, index // SIZE, guards)
                       and all(labels[index + dx + dy * SIZE] == METRIC.FRAME_LABEL
                               for dy in (-1, 0, 1) for dx in (-1, 0, 1)))
        pixels[damaged * 3:damaged * 3 + 3] = bytes(3)
        result = compare(pixels, labels, palette, ambiguous, guards)
        self.assertEqual(result["frame_outside_guard"]["missing_pixels"], 1)
        self.assertFalse(result["lifecycle_pass"])

    def test_control_difference_ignores_the_guards(self):
        labels, palette, _, guards, _ = expected()
        pixels = render(labels, palette)
        control = bytearray(pixels)
        inside = marker_pixels(labels)[0]
        control[inside * 3:inside * 3 + 3] = bytes(3)
        self.assertEqual(METRIC.control_difference(
            SIZE, SIZE, 3, pixels, (SIZE, SIZE, 3, control), guards), 0)
        outside = next(index for index in range(SIZE * SIZE)
                       if not METRIC.inside_guard(index % SIZE, index // SIZE, guards))
        control[outside * 3:outside * 3 + 3] = bytes((1, 2, 3))
        self.assertEqual(METRIC.control_difference(
            SIZE, SIZE, 3, pixels, (SIZE, SIZE, 3, control), guards), 1)

    def test_blank_does_not_pass_vacuously(self):
        labels, palette, ambiguous, guards, _ = expected()
        result = compare(bytes(SIZE * SIZE * 3), labels, palette, ambiguous, guards)
        self.assertFalse(result["lifecycle_pass"])

    def test_cli_strict_and_default_exit_codes(self):
        labels, palette, _, _, _ = expected(yaw=45)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mixed.png"
            METRIC.write_png(str(path), SIZE, SIZE, bytes(render(labels, palette)), 3)
            args = [str(path), "--yaw", "45", "--iso-scale", str(SCALE[0]), str(SCALE[1])]
            reports = []
            for extra in ([], ["--strict"]):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(METRIC.main(args + extra), 0)
                reports.append(json.loads(output.getvalue()))
            self.assertEqual(reports[0]["footprint"], reports[1]["footprint"])
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(METRIC.main([str(path), "--yaw", "0", "--iso-scale",
                                              str(SCALE[0]), str(SCALE[1])]), 1)
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as bad_markers:
                    METRIC.main(args + ["--fixture", "parity", "--markers", "1", "2"])
            self.assertEqual(bad_markers.exception.code, 2)
            markers = ["--markers", "-8", "-8", "-8", "-5", "-8", "-8"]
            labels, palette, _, _, _ = METRIC.expected_image(
                SIZE, SIZE, 0.0, False, SCALE, CENTER, "parity", (0.0, 0.0, 0.0),
                ((-8.0, -8.0, -8.0), (-5.0, -8.0, -8.0)))
            METRIC.write_png(str(path), SIZE, SIZE, bytes(render(labels, palette)), 3)
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(METRIC.main([str(path), "--yaw", "0", "--fixture", "parity",
                                              "--iso-scale", str(SCALE[0]), str(SCALE[1]),
                                              "--strict"] + markers), 0)
            self.assertEqual(json.loads(output.getvalue())["fixture"], "parity")


if __name__ == "__main__":
    unittest.main()
