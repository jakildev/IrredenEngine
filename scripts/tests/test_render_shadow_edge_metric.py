"""Tests for render-shadow-edge-metric.py — the graded shadow-rim oracle.

The two locks the oracle exists for are ``test_staircase_grades_above_straight``
(a quantized rim must score measurably worse than a straight one of the same
area, and a finer staircase must land between them) and
``test_coverage_growth_leaves_the_score_untouched`` (filling the mask's interior
must not move the score at all, while the coverage-style perimeter ratio moves
by orders of magnitude — the reason this is a second oracle rather than a
restatement of render-shadow-metric.py).

Synthetic SHADOW-overlay PNGs only — no GL/Metal context, no committed
reference. Import via importlib (dashed name).
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
    "render_shadow_edge_metric", str(_SCRIPTS / "render-shadow-edge-metric.py"))
_spec = importlib.util.spec_from_loader("render_shadow_edge_metric", _loader)
_mod = importlib.util.module_from_spec(_spec)
sys.modules["render_shadow_edge_metric"] = _mod
_loader.exec_module(_mod)

edge_metrics = _mod.edge_metrics
_main = _mod._main

import render_metric_util as rmu  # noqa: E402  (after the sys.path insert)

W, H = 120, 96
MAGENTA = (255, 0, 255)
BLACK = (0, 0, 0)
# Rasterizing a straight line of any orientation cannot put a pixel further
# than half a pixel off it, so this is the ceiling a clean rim reads under.
RASTER_CEILING = 0.5


def _straight(y: int) -> int:
    return 60


def _fine(y: int) -> int:
    """One-pixel comb: the tread of a rim filtered down to the pixel grid."""
    return 60 + (1 if y % 2 else -1)


def _coarse(y: int) -> int:
    """Four-pixel steps every four rows — a shadow-map-texel staircase."""
    return 60 + (4 if (y // 4) % 2 == 0 else -4)


class ShadowEdgeMetricTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def _write(self, name, shadowed, w=W, h=H) -> str:
        """shadowed(x, y) -> bool, written as a SHADOW-overlay RGB capture."""
        path = str(self.dir / name)
        buf = bytearray(w * h * 3)
        for y in range(h):
            for x in range(w):
                r, g, b = MAGENTA if shadowed(x, y) else BLACK
                o = (y * w + x) * 3
                buf[o], buf[o + 1], buf[o + 2] = r, g, b
        rmu.write_png(path, w, h, bytes(buf), 3)
        return path

    def _edge(self, name, edge_x) -> str:
        """A half-plane whose right rim follows edge_x(y); its other three
        sides sit on the grid border, so the rim under test is the only one."""
        return self._write(name, lambda x, y: x < edge_x(y))

    def test_straight_rim_reads_clean(self):
        r = edge_metrics(self._edge("straight.png", _straight))
        self.assertEqual(r["roughness_px"], 0.0)
        self.assertEqual(r["boundary_px"], H)      # one exposed column
        self.assertEqual(r["speck_frac"], 0.0)
        self.assertEqual(r["mean_run_px"], float(H))
        self.assertEqual(r["axis_frac"], 1.0)

    def test_straight_diagonal_rim_reads_clean_too(self):
        # Orientation invariance: a 45-degree rim steps one pixel per row and
        # must still read clean, or every iso-aligned edge would gate as rough.
        diagonal = edge_metrics(self._edge("diag45.png", lambda y: 20 + y))
        shallow = edge_metrics(self._edge("diag26.png", lambda y: 20 + y // 2))
        self.assertEqual(diagonal["roughness_px"], 0.0)
        self.assertLess(shallow["roughness_px"], RASTER_CEILING)
        self.assertEqual(diagonal["axis_frac"], 0.0)

    def test_staircase_grades_above_straight(self):
        straight = edge_metrics(self._edge("straight.png", _straight))
        fine = edge_metrics(self._edge("fine.png", _fine))
        coarse = edge_metrics(self._edge("coarse.png", _coarse))
        # Same area on all three: the score separates them on rim shape alone.
        self.assertEqual(straight["shadow_px"], fine["shadow_px"])
        self.assertEqual(straight["shadow_px"], coarse["shadow_px"])
        self.assertLess(straight["roughness_px"], fine["roughness_px"])
        self.assertLess(fine["roughness_px"], coarse["roughness_px"])
        self.assertGreater(coarse["roughness_px"], 2 * RASTER_CEILING)
        self.assertGreater(coarse["roughness_p95"], coarse["roughness_px"])

    def test_filtering_the_rim_lowers_the_score(self):
        # A wider shadow filter, binarized at the overlay's 0.999 threshold, is
        # an erosion of the hard mask: it eats the steps' protruding tips. The
        # score must fall as the filter widens, monotonically, or it cannot
        # register a softness change on the finite producer.
        scores = []
        for radius in (0, 1, 2):
            path = self._write(
                f"filtered{radius}.png",
                lambda x, y, r=radius: all(
                    x + dx < _coarse(min(max(y + dy, 0), H - 1))
                    for dy in range(-r, r + 1) for dx in range(-r, r + 1)))
            scores.append(edge_metrics(path)["roughness_px"])
        self.assertGreater(scores[0], scores[1])
        self.assertGreater(scores[1], scores[2])

    def test_coverage_growth_leaves_the_score_untouched(self):
        holed = self._write(
            "holed.png",
            lambda x, y: (x < _coarse(y)
                          and not (10 <= x < 40 and 10 <= y < 80 and (x + y) % 2 == 0)))
        filled = self._edge("filled.png", _coarse)
        thin = edge_metrics(holed)
        fat = edge_metrics(filled)
        self.assertGreater(fat["shadow_px"], thin["shadow_px"])  # coverage moved
        for key in ("boundary_px", "roughness_px", "roughness_p95", "speck_frac",
                    "mean_run_px", "run_p90", "axis_frac", "direction_hist"):
            self.assertEqual(thin[key], fat[key], key)
        # The coverage-shaped number the sibling metric gates on does move, by
        # two orders of magnitude — the two oracles are not interchangeable.
        ratios = []
        for path in (holed, filled):
            mask, rw, rh, shadow_px, _lit, _rect = rmu.shadow_mask(path)
            perimeter = rmu.perimeter(mask, rw, rh)
            ratios.append(perimeter * perimeter / shadow_px)
        self.assertGreater(ratios[0], 10 * ratios[1])

    def test_enclosed_holes_are_not_rim(self):
        square_hole = self._write(
            "hole.png",
            lambda x, y: x < _straight(y) and not (20 <= x < 40 and 20 <= y < 40))
        r = edge_metrics(square_hole)
        self.assertEqual(r["boundary_px"], H)
        self.assertEqual(r["roughness_px"], 0.0)

    def test_window_must_exceed_the_tread(self):
        wide = self._edge("wide.png", lambda y: 60 + (8 if (y // 8) % 2 == 0 else -8))
        inside_tread = edge_metrics(wide, window=4)
        spanning = edge_metrics(wide, window=12)
        self.assertLess(inside_tread["roughness_px"], RASTER_CEILING)
        self.assertGreater(spanning["roughness_px"], 4 * RASTER_CEILING)

    def test_separate_fragments_are_fitted_apart(self):
        # Two straight rims three pixels apart: fitting across the gap would
        # read the pair as one ragged edge. Each component is fitted alone.
        pair = self._write("pair.png", lambda x, y: x < 40 or 43 <= x < 80)
        r = edge_metrics(pair)
        self.assertEqual(r["roughness_px"], 0.0)
        self.assertEqual(r["boundary_px"], 3 * H)

    def test_speckle_is_excluded_and_reported(self):
        speckle = self._write("speckle.png",
                              lambda x, y: (x + y) % 2 == 0 and 20 <= x < 80)
        r = edge_metrics(speckle)
        self.assertEqual(r["speck_frac"], 1.0)
        self.assertEqual(r["roughness_px"], 0.0)
        self.assertEqual(r["fitted_px"], 0)

    def test_roi_crop_does_not_fabricate_a_rim(self):
        coarse = self._edge("coarse.png", _coarse)
        full = edge_metrics(coarse)
        # An ROI cut straight through the shadow: the cut is the grid edge, not
        # background, so it contributes no perfectly-straight rim to dilute the
        # score. Only the rim inside the crop is measured.
        cropped = edge_metrics(coarse, roi=(40, 0, 40, H))
        self.assertLess(cropped["shadow_px"], full["shadow_px"])
        self.assertEqual(cropped["boundary_px"], full["boundary_px"])
        self.assertEqual(cropped["roughness_px"], full["roughness_px"])

    def test_determinism_repeat_runs(self):
        path = self._edge("coarse.png", _coarse)
        self.assertEqual(edge_metrics(path), edge_metrics(path))

    def test_thresholds_drive_exit_code(self):
        coarse = self._edge("coarse.png", _coarse)
        self.assertEqual(_main([coarse, "--max-roughness", "3.0"]), 0)
        self.assertEqual(_main([coarse, "--max-roughness", "1.0"]), 1)
        self.assertEqual(_main([coarse, "--max-roughness-p95", "1.0"]), 1)
        self.assertEqual(_main([coarse, "--max-speck-frac", "0.0"]), 0)
        speckle = self._write("speckle.png",
                              lambda x, y: (x + y) % 2 == 0 and 20 <= x < 80)
        self.assertEqual(_main([speckle, "--max-speck-frac", "0.5"]), 1)

    def test_rimless_mask_fails_the_boundary_floor(self):
        # A shadow filling the ROI has no rim at all and reads roughness 0 —
        # indistinguishable from a perfect edge to an upper bound alone.
        full = self._write("full.png", lambda x, y: True)
        r = edge_metrics(full)
        self.assertEqual(r["boundary_px"], 0)
        self.assertEqual(r["roughness_px"], 0.0)
        self.assertEqual(_main([full, "--max-roughness", "0.1"]), 0)
        self.assertEqual(_main([full, "--min-boundary-px", "1"]), 1)

    def test_empty_mask_is_a_usage_error(self):
        lit = self._write("lit.png", lambda x, y: False)
        self.assertEqual(_main([lit]), 2)

    def test_bad_roi_is_an_error(self):
        coarse = self._edge("coarse.png", _coarse)
        self.assertEqual(_main([coarse, "--roi", str(W - 10) + ",0,40," + str(H)]), 2)
        with self.assertRaises(ValueError):
            edge_metrics(coarse, roi=(0, 0, W + 1, H))

    def test_invalid_fit_parameters_are_a_usage_error(self):
        coarse = self._edge("coarse.png", _coarse)
        self.assertEqual(_main([coarse, "--window", "0"]), 2)
        self.assertEqual(_main([coarse, "--min-fit-px", "2"]), 2)


if __name__ == "__main__":
    unittest.main()
