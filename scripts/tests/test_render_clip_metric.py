"""Tests for render-clip-metric.py — the clip-detection metric.

Proves the metric is deterministic and that it discriminates a present entity
from a clipped one: an entity that drops out of its expected bbox reads
occupancy ~0 (and trips --min-occupancy), while one cut off at the bbox edge
reads a high edge_touch_frac (and trips --max-edge-touch-frac). Synthetic PNGs
only — no GL/Metal context. Import via importlib (dashed name).
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


_mod = _load("render_clip_metric", "render-clip-metric.py")
clip_metrics = _mod.clip_metrics

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


class TestClipMetric(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_full_bbox_is_fully_occupied(self):
        p = str(self.dir / "full.png")
        _write(p, 16, 16, lambda x, y: WHITE)
        m = clip_metrics(p)
        self.assertEqual(m["occupancy"], 1.0)
        self.assertEqual(m["edge_touch_frac"], 1.0)

    def test_blank_bbox_is_clipped(self):
        # The clip signature: the expected bbox reads all background.
        p = str(self.dir / "blank.png")
        _write(p, 16, 16, lambda x, y: BLACK)
        m = clip_metrics(p)
        self.assertEqual(m["fg_px"], 0)
        self.assertEqual(m["occupancy"], 0.0)
        self.assertEqual(m["edge_touch_frac"], 0.0)

    def test_centered_solid_does_not_touch_edges(self):
        # A solid sitting well inside its expected bbox: healthy occupancy,
        # zero edge contact — the not-clipped case.
        p = str(self.dir / "centered.png")

        def fn(x, y):
            return WHITE if (10 <= x < 22 and 10 <= y < 22) else BLACK
        _write(p, 32, 32, fn)
        m = clip_metrics(p)
        self.assertGreater(m["occupancy"], 0.1)
        self.assertEqual(m["edge_touch_frac"], 0.0)

    def test_edge_clipped_solid_touches_border(self):
        # A solid running off the right edge of the bbox: occupancy can look
        # fine, but the entity hugs the border -> high edge_touch_frac.
        p = str(self.dir / "edge.png")

        def fn(x, y):
            return WHITE if x >= 20 else BLACK
        _write(p, 32, 32, fn)
        m = clip_metrics(p)
        self.assertGreater(m["edge_touch_frac"], 0.2)

    def test_roi_is_the_expected_bbox(self):
        # Occupancy is measured over the ROI, not the whole frame.
        p = str(self.dir / "corner.png")

        def fn(x, y):
            return WHITE if (0 <= x < 16 and 0 <= y < 16) else BLACK
        _write(p, 32, 32, fn)
        # bbox over the solid: full; bbox over the empty quadrant: clipped.
        full = clip_metrics(p, roi=(0, 0, 16, 16))
        self.assertEqual(full["occupancy"], 1.0)
        empty = clip_metrics(p, roi=(16, 16, 16, 16))
        self.assertEqual(empty["occupancy"], 0.0)

    def test_determinism_repeat_runs(self):
        p = str(self.dir / "c.png")

        def fn(x, y):
            return WHITE if (8 <= x < 20 and 8 <= y < 20) else BLACK
        _write(p, 32, 32, fn)
        self.assertEqual(clip_metrics(p), clip_metrics(p))

    def test_cli_min_occupancy_gate(self):
        present = str(self.dir / "present.png")
        _write(present, 16, 16, lambda x, y: WHITE)
        clipped = str(self.dir / "clipped.png")
        _write(clipped, 16, 16, lambda x, y: BLACK)
        self.assertEqual(_rc([present, "--min-occupancy", "0.5"]), 0)
        self.assertEqual(_rc([clipped, "--min-occupancy", "0.5"]), 1)

    def test_cli_max_edge_touch_gate(self):
        p = str(self.dir / "edge2.png")
        _write(p, 32, 32, lambda x, y: WHITE if x >= 20 else BLACK)
        self.assertEqual(_rc([p, "--max-edge-touch-frac", "0.9"]), 0)
        self.assertEqual(_rc([p, "--max-edge-touch-frac", "0.1"]), 1)

    def test_cli_bad_image_is_exit_2(self):
        self.assertEqual(_rc([str(self.dir / "nope.png"),
                                     "--min-occupancy", "0.5"]), 2)


if __name__ == "__main__":
    unittest.main()
