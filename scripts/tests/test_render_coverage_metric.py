"""Tests for render-coverage-metric.py — the voxel-face coverage metric.

Proves the metric is deterministic and that it discriminates a clean solid
(~0 interior holes) from the missing-face / re-voxelize hole defect (#1619):
a punched-out interior hole reads hole_px > 0 / one hole component, while a
notch open to the exterior is NOT counted (it is the field, not a hole), and a
cross-hatch shatter reads many hole components. Synthetic PNGs only — no
GL/Metal context, no committed reference. Import via importlib (dashed name).
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


def _rc(argv) -> int:
    """Run the script's _main with stdout/stderr muted; return the exit code."""
    with contextlib.redirect_stdout(io.StringIO()), \
            contextlib.redirect_stderr(io.StringIO()):
        return _mod._main(argv)


_mod = _load("render_coverage_metric", "render-coverage-metric.py")
coverage_metrics = _mod.coverage_metrics

import render_metric_util as util  # noqa: E402

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


def _write(path: str, w: int, h: int, fn) -> None:
    """fn(x, y) -> (r, g, b); written as RGB PNG (black = background field)."""
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            r, g, b = fn(x, y)
            o = (y * w + x) * 3
            buf[o], buf[o + 1], buf[o + 2] = r, g, b
    util.write_png(path, w, h, bytes(buf), 3)


class TestCoverageMetric(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_solid_has_no_holes(self):
        p = str(self.dir / "solid.png")
        _write(p, 32, 32, lambda x, y: WHITE)
        m = coverage_metrics(p)
        self.assertEqual(m["fg_px"], 32 * 32)
        self.assertEqual(m["hole_px"], 0)
        self.assertEqual(m["hole_ratio"], 0.0)
        self.assertEqual(m["coverage"], 1.0)
        # hole_px == 0 -> component pass is skipped (nothing to count).
        self.assertNotIn("hole_components", m)

    def test_interior_hole_counts(self):
        # A solid with a punched-out background hole — the dropped-face mode.
        p = str(self.dir / "holed.png")

        def fn(x, y):
            if 12 <= x < 20 and 12 <= y < 20:
                return BLACK  # an 8x8 background hole inside...
            return WHITE      # ...a 32x32 solid
        _write(p, 32, 32, fn)
        m = coverage_metrics(p)
        self.assertEqual(m["hole_px"], 64)
        self.assertEqual(m["fg_px"], 32 * 32 - 64)
        self.assertGreater(m["hole_ratio"], 0.0)
        self.assertEqual(m["hole_components"], 1)
        self.assertAlmostEqual(m["coverage"], 1.0 - m["hole_ratio"], places=4)

    def test_edge_notch_is_not_a_hole(self):
        # A background notch open to the exterior is the field, not a hole:
        # it reaches the border, so coverage stays 1.0.
        p = str(self.dir / "notch.png")

        def fn(x, y):
            if 0 <= x < 8 and 12 <= y < 20:
                return BLACK  # a notch cut in from the left edge
            return WHITE
        _write(p, 32, 32, fn)
        m = coverage_metrics(p)
        self.assertEqual(m["hole_px"], 0)
        self.assertEqual(m["hole_ratio"], 0.0)
        self.assertEqual(m["coverage"], 1.0)

    def test_checkerboard_is_swiss_cheese(self):
        # Cross-hatch dropout: alternating solid/background shatters into many
        # isolated interior holes (border cells reach the field and don't count).
        p = str(self.dir / "checker.png")
        _write(p, 32, 32, lambda x, y: WHITE if (x + y) % 2 == 0 else BLACK)
        m = coverage_metrics(p)
        self.assertGreater(m["hole_ratio"], 0.3)
        self.assertGreater(m["hole_components"], 100)

    def test_all_background_is_vacuously_full(self):
        # Nothing rendered: no silhouette, so no holes. (The blank-frame case
        # is the clip metric's job, not coverage's.)
        p = str(self.dir / "blank.png")
        _write(p, 16, 16, lambda x, y: BLACK)
        m = coverage_metrics(p)
        self.assertEqual(m["fg_px"], 0)
        self.assertEqual(m["hole_px"], 0)
        self.assertEqual(m["hole_ratio"], 0.0)
        self.assertEqual(m["coverage"], 1.0)

    def test_roi_restricts_region(self):
        p = str(self.dir / "split.png")
        # Left half: solid with a hole. Right half: clean solid.
        def fn(x, y):
            if x < 16:
                if 4 <= x < 8 and 6 <= y < 10:
                    return BLACK
                return WHITE
            return WHITE
        _write(p, 32, 16, fn)
        left = coverage_metrics(p, roi=(0, 0, 16, 16))
        self.assertGreater(left["hole_px"], 0)
        right = coverage_metrics(p, roi=(16, 0, 16, 16))
        self.assertEqual(right["hole_px"], 0)

    def test_custom_background_colour(self):
        # White field, dark solid with a white interior hole: --bg white flips
        # which colour is the field.
        p = str(self.dir / "onwhite.png")

        def fn(x, y):
            if 8 <= x < 24 and 8 <= y < 24:
                if 14 <= x < 18 and 14 <= y < 18:
                    return WHITE  # hole back to the field colour
                return BLACK      # the solid
            return WHITE          # field
        _write(p, 32, 32, fn)
        m = coverage_metrics(p, bg=WHITE)
        self.assertEqual(m["hole_px"], 16)
        self.assertEqual(m["hole_components"], 1)

    def test_determinism_repeat_runs(self):
        p = str(self.dir / "checker2.png")
        _write(p, 24, 24, lambda x, y: WHITE if (x + y) % 2 == 0 else BLACK)
        self.assertEqual(coverage_metrics(p), coverage_metrics(p))

    def test_cli_max_hole_ratio_gate(self):
        p = str(self.dir / "holed_cli.png")

        def fn(x, y):
            if 12 <= x < 20 and 12 <= y < 20:
                return BLACK
            return WHITE
        _write(p, 32, 32, fn)
        # Clean threshold passes the same image only when generous.
        self.assertEqual(_rc([p, "--max-hole-ratio", "0.5"]), 0)
        self.assertEqual(_rc([p, "--max-hole-ratio", "0.001"]), 1)

    def test_cli_min_coverage_gate(self):
        p = str(self.dir / "holed_cov.png")

        def fn(x, y):
            return BLACK if (12 <= x < 20 and 12 <= y < 20) else WHITE
        _write(p, 32, 32, fn)
        self.assertEqual(_rc([p, "--min-coverage", "0.5"]), 0)
        self.assertEqual(_rc([p, "--min-coverage", "0.999"]), 1)

    def test_cli_bad_image_is_exit_2(self):
        self.assertEqual(_rc([str(self.dir / "nope.png"),
                                     "--max-hole-ratio", "0.1"]), 2)


if __name__ == "__main__":
    unittest.main()
