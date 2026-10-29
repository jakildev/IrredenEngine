"""Destination-cell controls for the rotated GRID display gate."""

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
    "grid_cell_metric", SCRIPTS / "render-grid-cell-metric.py")
METRIC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRIC)
import render_metric_util  # noqa: E402

SIZE = 280
SCALE = (8, 4)
CENTER = (SIZE / 2, SIZE / 2)


def expected(fixture="plate", yaw=0.0):
    return METRIC.expected_image(
        SIZE, SIZE, METRIC.FIXTURES[fixture], math.radians(yaw), SCALE, CENTER)


def render(labels, palette):
    return bytearray(channel for label in labels for channel in palette[label])


def blank(pixels, owner, cell):
    for index, label in enumerate(owner):
        if label == cell:
            pixels[index * 3:index * 3 + 3] = b"\x00\x00\x00"


def run(pixels, *args):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "capture.png"
        render_metric_util.write_png(str(path), SIZE, SIZE, bytes(pixels), 3)
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = METRIC.main([str(path), "--iso-scale", *map(str, SCALE), *args])
        return code, json.loads(stdout.getvalue())


class GridCellOracleTest(unittest.TestCase):
    def test_plate_covers_145_cells_all_exposed_without_ties(self):
        cells, ties = METRIC.world_cells(METRIC.FIXTURES["plate"])
        self.assertEqual(len(cells), 145)
        self.assertEqual(ties, 0)
        self.assertTrue(all(cell[1] == 0 for cell in cells))

    def test_cube_covers_1740_cells_with_610_exposed(self):
        cells, ties = METRIC.world_cells(METRIC.FIXTURES["cube"])
        self.assertEqual(len(cells), 1740)
        self.assertEqual(ties, 0)
        exposed = sum(
            1 for c in cells
            if any((c[0] + d[0], c[1] + d[1], c[2] + d[2]) not in cells
                   for d in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1),
                             (0, 0, -1))))
        self.assertEqual(exposed, 610)

    def test_cardinal_yaws_permute_the_same_cells(self):
        counts = {yaw: expected(yaw=yaw)[3]["expected_cells"] for yaw in (0, 90, 180, 270)}
        self.assertEqual(set(counts.values()), {145})
        self.assertNotEqual(bytes(expected(yaw=0)[0]), bytes(expected(yaw=90)[0]))


class GridCellMetricTest(unittest.TestCase):
    def test_exact_render_passes_every_cell(self):
        labels, _, palette, stats = expected()
        self.assertFalse(stats["clipped"])
        code, result = run(render(labels, palette), "--fixture", "plate", "--yaw", "0")
        self.assertEqual(code, 0)
        self.assertEqual(result["missing_exposed_cells"], 0)
        self.assertEqual(result["visible_expected_cells"], 145)
        self.assertEqual(result["wrong_face_pixels"], 0)

    def test_one_dropped_exposed_cell_fails(self):
        labels, owner, palette, _ = expected()
        pixels = render(labels, palette)
        dropped = owner[owner.index(next(v for v in owner if v))]
        for index, cell in enumerate(owner):
            if cell == dropped:
                pixels[index * 3:index * 3 + 3] = b"\x00\x00\x00"
        code, result = run(pixels, "--fixture", "plate", "--yaw", "0")
        self.assertEqual(code, 1)
        self.assertEqual(result["missing_exposed_cells"], 1)
        self.assertGreater(result["missing_pixels"], 0)

    def test_wrong_yaw_fails(self):
        labels, _, palette, _ = expected(yaw=0)
        code, result = run(render(labels, palette), "--fixture", "plate", "--yaw", "90")
        self.assertEqual(code, 1)
        self.assertGreater(result["missing_pixels"] + result["wrong_face_pixels"], 0)

    def test_beauty_grades_silhouette_only(self):
        labels, _, palette, _ = expected()
        lit = [(0, 0, 0), (60, 100, 180), (50, 90, 160), (40, 80, 140)]
        pixels = render(labels, lit)
        beauty = ("--fixture", "plate", "--yaw", "0", "--view", "beauty")
        self.assertEqual(run(pixels, *beauty)[0], 0)
        code, result = run(pixels, "--fixture", "plate", "--yaw", "0")
        self.assertEqual(code, 1)
        self.assertGreater(result["wrong_face_pixels"], 0)

    def test_beauty_detects_a_dropped_cell(self):
        labels, owner, _, _ = expected()
        pixels = render(labels, [(0, 0, 0), (60, 100, 180), (50, 90, 160), (40, 80, 140)])
        dropped = max(owner)
        for index, cell in enumerate(owner):
            if cell == dropped:
                pixels[index * 3:index * 3 + 3] = b"\x00\x00\x00"
        code, result = run(pixels, "--fixture", "plate", "--yaw", "0", "--view", "beauty")
        self.assertEqual(code, 1)
        self.assertEqual(result["missing_exposed_cells"], 1)

    def test_blank_capture_fails(self):
        code, result = run(bytearray(SIZE * SIZE * 3), "--fixture", "cube", "--yaw", "180")
        self.assertEqual(code, 1)
        self.assertEqual(result["observed_pixels"], 0)

    def test_zoom_derives_the_iso_scale_from_capture_width(self):
        # One iso unit spans 2 x zoom game pixels; a 280-wide capture of the
        # 1280-wide game is a 7/32 output scale, so zoom 16 is 7 x 3.5 pixels.
        labels, _, palette, _ = METRIC.expected_image(
            SIZE, SIZE, METRIC.FIXTURES["plate"], 0.0, (7, 3.5), CENTER)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.png"
            render_metric_util.write_png(str(path), SIZE, SIZE, bytes(render(labels, palette)), 3)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                code = METRIC.main([str(path), "--zoom", "16", "--fixture", "plate", "--yaw", "0"])
        result = json.loads(stdout.getvalue())
        self.assertEqual(result["iso_scale"], [7.0, 3.5])
        self.assertEqual(code, 0)

    def test_dropped_cell_is_named_in_view_coordinates(self):
        labels, owner, palette, stats = expected()
        pixels, dropped = render(labels, palette), max(owner)
        blank(pixels, owner, dropped)
        _, result = run(pixels, "--fixture", "plate", "--yaw", "0")
        self.assertEqual(result["missing_cells"], [list(stats["cells"][dropped - 1])])

    def test_off_cardinal_yaw_is_rejected(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            METRIC.main(["unused.png", "--fixture", "plate", "--yaw", "45"])
        self.assertEqual(raised.exception.code, 2)


class GridCellRegressionTest(unittest.TestCase):
    """A parent frame that lacks one cell may differ from head only inside that
    cell's footprint, dilated by one pixel."""

    LIT = [(0, 0, 0), (200, 200, 200), (150, 150, 150), (100, 100, 100)]

    def setUp(self):
        labels, self.owner, palette, _ = expected()
        self.gained = max(self.owner)
        self.normals = render(labels, palette)
        blank(self.normals, self.owner, self.gained)
        self.head = render(labels, self.LIT)
        self.parent = bytearray(self.head)
        blank(self.parent, self.owner, self.gained)

    def grade(self, head=None, normals=None):
        with tempfile.TemporaryDirectory() as directory:
            frames = dict(head=head or self.head, parent=self.parent,
                          normals=normals or self.normals)
            paths = {name: Path(directory) / f"{name}.png" for name in frames}
            for name, pixels in frames.items():
                render_metric_util.write_png(str(paths[name]), SIZE, SIZE, bytes(pixels), 3)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                code = METRIC.main([str(paths["head"]), "--iso-scale", *map(str, SCALE),
                                    "--fixture", "plate", "--yaw", "0",
                                    "--regression-parent", str(paths["parent"]),
                                    "--gained-from", str(paths["normals"])])
        return code, json.loads(stdout.getvalue())

    def outside_index(self, distance):
        """A background pixel exactly `distance` pixels (Chebyshev) from the footprint."""
        footprint = {i for i, cell in enumerate(self.owner) if cell == self.gained}
        for index in range(SIZE * SIZE):
            if self.owner[index]:
                continue
            x, y = index % SIZE, index // SIZE
            nearest = min(max(abs(x - f % SIZE), abs(y - f // SIZE)) for f in footprint)
            if nearest == distance:
                return index
        raise AssertionError(f"no background pixel at distance {distance}")

    def test_difference_inside_the_gained_cell_passes(self):
        code, result = self.grade()
        self.assertEqual(code, 0)
        self.assertEqual(len(result["gained_cells"]), 1)
        self.assertGreater(result["differing_pixels"], 0)
        self.assertEqual(result["outside_footprint_pixels"], 0)

    def test_difference_within_the_one_pixel_dilation_passes(self):
        head = bytearray(self.head)
        index = self.outside_index(1)
        head[index * 3:index * 3 + 3] = b"\x01\x02\x03"
        self.assertEqual(self.grade(head=head)[0], 0)

    def test_one_differing_pixel_outside_the_footprint_fails(self):
        head = bytearray(self.head)
        index = self.outside_index(2)
        head[index * 3:index * 3 + 3] = b"\x01\x02\x03"
        code, result = self.grade(head=head)
        self.assertEqual(code, 1)
        self.assertEqual(result["outside_footprint_pixels"], 1)

    def test_no_gained_cell_fails(self):
        labels, _, palette, _ = expected()
        code, result = self.grade(normals=render(labels, palette))
        self.assertEqual(code, 1)
        self.assertEqual(result["gained_cells"], [])


if __name__ == "__main__":
    unittest.main()
