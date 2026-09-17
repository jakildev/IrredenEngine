"""Tests for render-ao-staircase-metric.py — the rotated-staircase AO metric.

Proves the metric locates solids from the lit capture (coloured cubes, never
the grey floor or black field), counts only overlay-darkened pixels under
that mask, and discriminates venetian-blind banding (every other row of a
cube darkened) from a clean face. Synthetic PNGs only — no GL/Metal
context, no committed reference. Import via importlib (dashed name).
"""
import importlib.machinery
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
# The subject bare-imports render_metric_util, so scripts/ must be on sys.path.
sys.path.insert(0, str(_SCRIPTS))

_loader = importlib.machinery.SourceFileLoader(
    "render_ao_staircase_metric", str(_SCRIPTS / "render-ao-staircase-metric.py"))
_spec = importlib.util.spec_from_loader("render_ao_staircase_metric", _loader)
_mod = importlib.util.module_from_spec(_spec)
sys.modules["render_ao_staircase_metric"] = _mod
_loader.exec_module(_mod)

staircase_metrics = _mod.staircase_metrics
_main = _mod._main

import render_metric_util  # noqa: E402  (after the sys.path insert)

write_png = render_metric_util.write_png

BLACK = (0, 0, 0)
GRAY = (128, 128, 128)
BLUE = (120, 160, 255)
RED = (255, 120, 120)
UNOCCLUDED = (0, 255, 0)
W, H = 40, 30
# Lit layout: black field, grey floor band on the bottom third, a blue cube
# at x 4..19 and a red cube at x 22..37, both spanning y 2..17.
CUBE_Y = range(2, 18)
BLUE_X = range(4, 20)
RED_X = range(22, 38)


def _write(path: str, fn) -> None:
    """fn(x, y) -> (r, g, b); written as RGB PNG."""
    buf = bytearray(W * H * 3)
    for y in range(H):
        for x in range(W):
            r, g, b = fn(x, y)
            o = (y * W + x) * 3
            buf[o], buf[o + 1], buf[o + 2] = r, g, b
    write_png(path, W, H, bytes(buf), 3)


def _lit(x, y):
    if y in CUBE_Y and x in BLUE_X:
        return BLUE
    if y in CUBE_Y and x in RED_X:
        return RED
    return GRAY if y >= 20 else BLACK


def _overlay_banded(x, y):
    """Every other cube row darkened by 15/255 on the blue cube only; the
    floor carries a stronger 40/255 band that the mask must ignore."""
    if y in CUBE_Y and x in BLUE_X and y % 2 == 0:
        return (15, 240, 0)
    if y >= 20:
        return (40, 215, 0)
    return UNOCCLUDED


class TestStaircaseMetric(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.lit = str(self.dir / "lit.png")
        _write(self.lit, _lit)

    def tearDown(self):
        self._tmp.cleanup()

    def _overlay(self, name, fn):
        path = str(self.dir / name)
        _write(path, fn)
        return path

    def test_clean_face_reads_zero(self):
        ov = self._overlay("clean.png", lambda x, y: UNOCCLUDED)
        r = staircase_metrics(ov, self.lit)
        self.assertEqual(r["solid_px"], 2 * 16 * 16)
        self.assertEqual(r["occluded_px"], 0)
        self.assertEqual(r["occluded_frac"], 0.0)
        self.assertEqual(r["max_darkening"], 0.0)

    def test_banding_counts_only_solid_pixels(self):
        ov = self._overlay("banded.png", _overlay_banded)
        r = staircase_metrics(ov, self.lit)
        # 8 of the blue cube's 16 rows, 16 px each; the floor band is masked out
        # so its stronger 40/255 darkening never reaches max_darkening.
        self.assertEqual(r["occluded_px"], 8 * 16)
        self.assertEqual(r["occluded_frac"], round(128 / 512, 4))
        self.assertEqual(r["max_darkening"], round(15 / 255, 4))
        self.assertEqual(r["mean_darkening"], round(15 / 255, 4))

    def test_dominant_channel_isolates_one_cube(self):
        ov = self._overlay("banded.png", _overlay_banded)
        blue = staircase_metrics(ov, self.lit, dominant="b")
        red = staircase_metrics(ov, self.lit, dominant="r")
        self.assertEqual(blue["solid_px"], 256)
        self.assertEqual(blue["occluded_frac"], 0.5)
        self.assertEqual(red["solid_px"], 256)
        self.assertEqual(red["occluded_px"], 0)

    def test_roi_scopes_the_mask(self):
        ov = self._overlay("banded.png", _overlay_banded)
        r = staircase_metrics(ov, self.lit, roi=(22, 0, 18, 30))
        self.assertEqual(r["solid_px"], 256)
        self.assertEqual(r["occluded_px"], 0)

    def test_thresholds_drive_exit_code(self):
        ov = self._overlay("banded.png", _overlay_banded)
        self.assertEqual(_main([ov, "--lit", self.lit, "--max-occluded-frac", "0.3"]), 0)
        self.assertEqual(_main([ov, "--lit", self.lit, "--max-occluded-frac", "0.2"]), 1)
        self.assertEqual(_main([ov, "--lit", self.lit, "--max-darkening", "0.05"]), 1)
        # Positive control: a clean overlay fails a min-occluded floor.
        clean = self._overlay("clean.png", lambda x, y: UNOCCLUDED)
        self.assertEqual(_main([clean, "--lit", self.lit, "--min-occluded-px", "1"]), 1)

    def test_empty_mask_is_a_usage_error(self):
        ov = self._overlay("clean.png", lambda x, y: UNOCCLUDED)
        # Nothing but grey/black in the ROI: exit 2, never a vacuous pass.
        self.assertEqual(_main([ov, "--lit", self.lit, "--roi", "0,20,40,10"]), 2)

    def test_size_mismatch_is_an_error(self):
        small = str(self.dir / "small.png")
        write_png(small, 2, 2, bytes(12), 3)
        with self.assertRaises(ValueError):
            staircase_metrics(small, self.lit)


if __name__ == "__main__":
    unittest.main()
