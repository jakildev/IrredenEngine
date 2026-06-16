"""Tests for render-silhouette-metric.py — the silhouette-raggedness metric.

Proves the metric is deterministic, scale-invariant, and that it discriminates
a clean compact silhouette from a ragged / speckled one: a filled square reads
perimeter_ratio 16 at any size and one component, while cross-hatch noise pushes
the ratio far higher and detached speckle shatters the component count.
Synthetic PNGs only — no GL/Metal context. Import via importlib (dashed name).
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


_mod = _load("render_silhouette_metric", "render-silhouette-metric.py")
silhouette_metrics = _mod.silhouette_metrics

import render_metric_util as util  # noqa: E402

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


def _write(path: str, w: int, h: int, fn) -> None:
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            r, g, b = fn(x, y)
            o = (y * w + x) * 3
            buf[o], buf[o + 1], buf[o + 2] = r, g, b
    util.write_png(path, w, h, bytes(buf), 3)


def _filled_rect(p, w, h, x0, y0, x1, y1):
    _write(p, w, h, lambda x, y: WHITE if (x0 <= x < x1 and y0 <= y < y1) else BLACK)


class TestSilhouetteMetric(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_filled_square_ratio_is_16(self):
        # A filled square: perimeter = 4s edges, area = s^2, P^2/A = 16.
        p = str(self.dir / "sq.png")
        _filled_rect(p, 32, 32, 8, 8, 24, 24)  # 16x16 solid
        m = silhouette_metrics(p)
        self.assertEqual(m["area"], 16 * 16)
        self.assertEqual(m["perimeter"], 4 * 16)
        self.assertAlmostEqual(m["perimeter_ratio"], 16.0, places=4)
        self.assertEqual(m["components"], 1)
        self.assertEqual(m["largest_frac"], 1.0)

    def test_scale_invariance(self):
        # The same shape at 2x scale has the same perimeter_ratio.
        small = str(self.dir / "small.png")
        big = str(self.dir / "big.png")
        _filled_rect(small, 32, 32, 8, 8, 16, 16)   # 8x8
        _filled_rect(big, 64, 64, 16, 16, 32, 32)   # 16x16 (2x)
        a = silhouette_metrics(small)["perimeter_ratio"]
        b = silhouette_metrics(big)["perimeter_ratio"]
        self.assertAlmostEqual(a, b, places=4)

    def test_cross_hatch_is_ragged(self):
        # A checkerboard maximises boundary length for its area: the ratio
        # explodes versus a clean fill, and it shatters into many components.
        p = str(self.dir / "checker.png")
        _write(p, 32, 32, lambda x, y: WHITE if (x + y) % 2 == 0 else BLACK)
        m = silhouette_metrics(p)
        self.assertGreater(m["perimeter_ratio"], 16.0)
        self.assertGreater(m["components"], 100)
        self.assertLess(m["largest_frac"], 0.05)

    def test_speckle_shatters_components(self):
        # A clean blob plus scattered detached specks: same rough area, but the
        # component count climbs and the largest fraction drops.
        p = str(self.dir / "speckle.png")

        def fn(x, y):
            if 10 <= x < 22 and 10 <= y < 22:
                return WHITE                 # the blob
            if x % 6 == 0 and y % 6 == 0:    # a grid of detached specks
                return WHITE
            return BLACK
        _write(p, 32, 32, fn)
        m = silhouette_metrics(p)
        self.assertGreater(m["components"], 5)
        self.assertLess(m["largest_frac"], 1.0)

    def test_roi_restricts_region(self):
        p = str(self.dir / "split.png")
        # Left: clean square. Right: empty.
        _filled_rect(p, 32, 16, 2, 2, 14, 14)
        left = silhouette_metrics(p, roi=(0, 0, 16, 16))
        self.assertGreater(left["area"], 0)
        right = silhouette_metrics(p, roi=(16, 0, 16, 16))
        self.assertEqual(right["area"], 0)
        self.assertEqual(right["perimeter_ratio"], 0.0)

    def test_determinism_repeat_runs(self):
        p = str(self.dir / "checker2.png")
        _write(p, 24, 24, lambda x, y: WHITE if (x + y) % 2 == 0 else BLACK)
        self.assertEqual(silhouette_metrics(p), silhouette_metrics(p))

    def test_cli_max_perimeter_ratio_gate(self):
        clean = str(self.dir / "clean.png")
        _filled_rect(clean, 32, 32, 8, 8, 24, 24)
        ragged = str(self.dir / "ragged.png")
        _write(ragged, 32, 32, lambda x, y: WHITE if (x + y) % 2 == 0 else BLACK)
        self.assertEqual(_rc([clean, "--max-perimeter-ratio", "20"]), 0)
        self.assertEqual(_rc([ragged, "--max-perimeter-ratio", "20"]), 1)

    def test_cli_max_components_gate(self):
        ragged = str(self.dir / "ragged2.png")
        _write(ragged, 32, 32, lambda x, y: WHITE if (x + y) % 2 == 0 else BLACK)
        self.assertEqual(_rc([ragged, "--max-components", "1000"]), 0)
        self.assertEqual(_rc([ragged, "--max-components", "5"]), 1)

    def test_cli_bad_image_is_exit_2(self):
        self.assertEqual(_rc([str(self.dir / "nope.png"),
                                     "--max-perimeter-ratio", "20"]), 2)


if __name__ == "__main__":
    unittest.main()
