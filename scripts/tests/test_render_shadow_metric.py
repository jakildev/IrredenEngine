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


if __name__ == "__main__":
    unittest.main()
