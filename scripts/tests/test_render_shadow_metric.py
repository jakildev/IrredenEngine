"""Tests for render-shadow-metric.py — the structural sun-shadow metric.

Proves the metric is deterministic and that it discriminates a clean
contiguous shadow from the swiss-cheese / cross-hatch failure mode (epic
#1717 items 3-4): a solid magenta blob reads ~0 holes / 1 component, a
checkerboard reads ~50% holes / many components. Synthetic PNGs only — no
GL/Metal context, no committed reference. Import via importlib (dashed name).
"""
import importlib.machinery
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
_loader = importlib.machinery.SourceFileLoader(
    "render_shadow_metric", str(_SCRIPTS / "render-shadow-metric.py"))
_spec = importlib.util.spec_from_loader("render_shadow_metric", _loader)
_mod = importlib.util.module_from_spec(_spec)
sys.modules["render_shadow_metric"] = _mod
_loader.exec_module(_mod)

shadow_metrics = _mod.shadow_metrics

# write_png lives in render-compare.py (used to synthesize fixture PNGs).
_cmp_loader = importlib.machinery.SourceFileLoader(
    "render_compare", str(_SCRIPTS / "render-compare.py"))
_cmp_spec = importlib.util.spec_from_loader("render_compare", _cmp_loader)
_cmp = importlib.util.module_from_spec(_cmp_spec)
_cmp_loader.exec_module(_cmp)
write_png = _cmp.write_png

MAGENTA = (255, 0, 255)
BLACK = (0, 0, 0)
GRAY = (128, 128, 128)


def _write(path: str, w: int, h: int, fn) -> None:
    """fn(x, y) -> (r, g, b); written as RGB PNG."""
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            r, g, b = fn(x, y)
            o = (y * w + x) * 3
            buf[o], buf[o + 1], buf[o + 2] = r, g, b
    write_png(path, w, h, bytes(buf), 3)


class TestShadowMetric(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_solid_shadow_is_clean(self):
        p = str(self.dir / "solid.png")
        _write(p, 32, 32, lambda x, y: MAGENTA)
        m = shadow_metrics(p)
        self.assertEqual(m["lit_px"], 0)
        self.assertEqual(m["shadow_px"], 32 * 32)
        self.assertEqual(m["hole_ratio"], 0.0)
        self.assertEqual(m["components"], 1)
        self.assertEqual(m["largest_frac"], 1.0)

    def test_checkerboard_is_swiss_cheese(self):
        # The cross-hatch dithering signature: alternating lit/shadow.
        p = str(self.dir / "checker.png")
        _write(p, 32, 32, lambda x, y: MAGENTA if (x + y) % 2 == 0 else BLACK)
        m = shadow_metrics(p)
        self.assertAlmostEqual(m["hole_ratio"], 0.5, places=2)
        # A clean blob is 1 component; a checkerboard shatters into one per cell.
        self.assertGreater(m["components"], 100)
        self.assertLess(m["largest_frac"], 0.05)

    def test_all_lit_is_all_holes(self):
        p = str(self.dir / "lit.png")
        _write(p, 16, 16, lambda x, y: BLACK)
        m = shadow_metrics(p)
        self.assertEqual(m["shadow_px"], 0)
        self.assertEqual(m["hole_ratio"], 1.0)

    def test_gray_background_is_ignored(self):
        # Neither lit nor shadowed: a clean shadow blob on a gray field reads
        # 0 holes regardless of how much background surrounds it.
        p = str(self.dir / "blob_on_gray.png")

        def fn(x, y):
            return MAGENTA if (8 <= x < 24 and 8 <= y < 24) else GRAY
        _write(p, 32, 32, fn)
        m = shadow_metrics(p)
        self.assertEqual(m["shadow_px"], 16 * 16)
        self.assertEqual(m["lit_px"], 0)
        self.assertEqual(m["hole_ratio"], 0.0)
        self.assertEqual(m["components"], 1)

    def test_hole_inside_shadow_counts(self):
        # A solid shadow with a punched-out lit hole — the interior-gap mode.
        p = str(self.dir / "holed.png")

        def fn(x, y):
            if 14 <= x < 18 and 14 <= y < 18:
                return BLACK  # a 4x4 lit hole inside...
            return MAGENTA    # ...a 32x32 shadow
        _write(p, 32, 32, fn)
        m = shadow_metrics(p)
        self.assertEqual(m["lit_px"], 16)
        self.assertEqual(m["shadow_px"], 32 * 32 - 16)
        self.assertGreater(m["hole_ratio"], 0.0)
        # The shadow stays one connected ring around the hole.
        self.assertEqual(m["components"], 1)

    def test_roi_restricts_region(self):
        p = str(self.dir / "split.png")
        # Left half shadow, right half lit.
        _write(p, 32, 16, lambda x, y: MAGENTA if x < 16 else BLACK)
        left = shadow_metrics(p, roi=(0, 0, 16, 16))
        self.assertEqual(left["hole_ratio"], 0.0)
        right = shadow_metrics(p, roi=(16, 0, 16, 16))
        self.assertEqual(right["hole_ratio"], 1.0)

    def test_determinism_repeat_runs(self):
        p = str(self.dir / "checker2.png")
        _write(p, 24, 24, lambda x, y: MAGENTA if (x + y) % 2 == 0 else BLACK)
        a = shadow_metrics(p)
        b = shadow_metrics(p)
        self.assertEqual(a, b)

    def _run_cli(self, args):
        """Run the metric CLI capturing stdout/stderr; return (rc, out, err)."""
        import io
        out_buf, err_buf = io.StringIO(), io.StringIO()
        old_out, old_err = sys.stdout, sys.stderr
        sys.stdout, sys.stderr = out_buf, err_buf
        try:
            rc = _mod._main(args)
        finally:
            sys.stdout, sys.stderr = old_out, old_err
        return rc, out_buf.getvalue(), err_buf.getvalue()

    def test_min_largest_frac_clean_passes(self):
        # A solid blob is one dominant component (largest_frac == 1.0).
        p = str(self.dir / "solid_lf.png")
        _write(p, 32, 32, lambda x, y: MAGENTA)
        rc, _, _ = self._run_cli([p, "--min-largest-frac", "0.8"])
        self.assertEqual(rc, 0)

    def test_min_largest_frac_swiss_cheese_fails(self):
        # A checkerboard shatters the shadow — the largest component is tiny,
        # so the lower bound bites (the fragmentation guard).
        p = str(self.dir / "checker_lf.png")
        _write(p, 32, 32, lambda x, y: MAGENTA if (x + y) % 2 == 0 else BLACK)
        rc, out, _ = self._run_cli([p, "--min-largest-frac", "0.8"])
        self.assertEqual(rc, 1)
        self.assertIn("largest_frac", out)

    def test_min_largest_frac_vanished_shadow_fails(self):
        # No shadow pixels at all -> the lower bound must fail (the vanish
        # guard: max-only thresholds would wrongly pass an empty shadow).
        p = str(self.dir / "vanished_lf.png")
        _write(p, 16, 16, lambda x, y: BLACK)
        rc, out, _ = self._run_cli([p, "--min-largest-frac", "0.8"])
        self.assertEqual(rc, 1)
        self.assertIn("no shadow pixels", out)

    def test_min_largest_frac_skipped_with_no_components(self):
        # --no-components removes the data the bound needs; it must warn + fail
        # rather than silently pass.
        p = str(self.dir / "solid_nc.png")
        _write(p, 16, 16, lambda x, y: MAGENTA)
        rc, _, err = self._run_cli(
            [p, "--no-components", "--min-largest-frac", "0.8"])
        self.assertEqual(rc, 1)
        self.assertIn("warning", err)

    def test_min_hole_ratio_lit_floor_passes(self):
        # The #2092 floor self-shadow guard: a fully-lit floor (no self-shadow)
        # reads hole_ratio == 1.0, so the lower bound passes.
        p = str(self.dir / "lit_floor.png")
        _write(p, 32, 32, lambda x, y: BLACK)
        rc, out, _ = self._run_cli([p, "--min-hole-ratio", "0.98"])
        self.assertEqual(rc, 0)
        self.assertIn('"hole_ratio": 1.0', out)

    def test_min_hole_ratio_self_shadow_fails(self):
        # Self-shadow acne where the floor should be fully lit drops hole_ratio
        # below the bound (a fully self-occluded floor reads 0.0) -> fail.
        p = str(self.dir / "acne_floor.png")
        _write(p, 32, 32, lambda x, y: MAGENTA)
        rc, out, _ = self._run_cli([p, "--min-hole-ratio", "0.98"])
        self.assertEqual(rc, 1)
        self.assertIn("hole_ratio 0.0 < 0.98", out)

    def test_max_components_cli_fails_when_roi_too_large(self):
        """--max-components with an oversized ROI must warn to stderr + exit 1."""
        import io
        p = str(self.dir / "big_shadow.png")
        _write(p, 8, 8, lambda x, y: MAGENTA)
        orig_cap = _mod.MAX_FLOOD_PX
        _mod.MAX_FLOOD_PX = 10  # 8*8=64 > 10 triggers the skip
        try:
            stderr_buf = io.StringIO()
            stdout_buf = io.StringIO()
            old_err, old_out = sys.stderr, sys.stdout
            sys.stderr, sys.stdout = stderr_buf, stdout_buf
            try:
                rc = _mod._main([p, "--max-components", "5"])
            finally:
                sys.stderr, sys.stdout = old_err, old_out
            self.assertEqual(rc, 1)
            self.assertIn("warning", stderr_buf.getvalue())
        finally:
            _mod.MAX_FLOOD_PX = orig_cap


if __name__ == "__main__":
    unittest.main()
