"""Tests for render-text-size-ratio-metric.py — the glyph-set height gate.

Synthetic captures prove the measurement (column split, first-line run, ROI
edge guard) and that a SMALL column drawn at LARGE height fails the band. The
last case runs the manifest's own gate over the committed macOS reference, so
the band the issue sets is recorded against the shipped capture.
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
_ROOT = _SCRIPTS.parent
sys.path.insert(0, str(_SCRIPTS))

import render_metric_util as util  # noqa: E402

_loader = importlib.machinery.SourceFileLoader(
    "render_text_size_ratio_metric", str(_SCRIPTS / "render-text-size-ratio-metric.py"))
_spec = importlib.util.spec_from_loader("render_text_size_ratio_metric", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
ratio_metrics = _mod.ratio_metrics

_MANIFEST = _ROOT / "creations/demos/shape_debug/test/references/manifest.json"
_REFERENCE = _ROOT / "creations/demos/shape_debug/test/references/macos-debug/text_size_sample.png"

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


def _rc(argv) -> int:
    with contextlib.redirect_stdout(io.StringIO()), \
            contextlib.redirect_stderr(io.StringIO()):
        return _mod._main(argv)


def _write(path: str, w: int, h: int, lit) -> None:
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            if lit(x, y):
                o = (y * w + x) * 3
                buf[o], buf[o + 1], buf[o + 2] = WHITE
    util.write_png(path, w, h, bytes(buf), 3)


def _two_columns(large_rows: int, small_rows: int, gap_rows: int = 4):
    """Two 'sheet' columns of glyph blocks, each with three lines of the given
    height separated by ``gap_rows``, letters 6 wide with 2-column spacing."""
    def lines(y: int, height: int) -> bool:
        if y < 2:
            return False
        return (y - 2) % (height + gap_rows) < height and y - 2 < 3 * (height + gap_rows)

    def lit(x: int, y: int) -> bool:
        if 2 <= x < 42 and (x - 2) % 8 < 6:
            return lines(y, large_rows)
        if 70 <= x < 110 and (x - 70) % 8 < 6:
            return lines(y, small_rows)
        return False
    return lit


class TestTextSizeRatioMetric(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_measures_the_first_line_of_each_column(self):
        p = str(self.dir / "pair.png")
        _write(p, 120, 80, _two_columns(12, 8))
        m = ratio_metrics(p)
        self.assertEqual(m["large"]["height"], 12)
        self.assertEqual(m["small"]["height"], 8)
        self.assertEqual(m["large"]["x"], [2, 39])
        self.assertEqual(m["small"]["x"], [70, 107])
        self.assertAlmostEqual(m["ratio"], 0.6667, places=4)

    def test_a_small_column_at_large_height_fails_the_band(self):
        # The regression the gate exists for: SMALL text drawn with the LARGE set.
        same = str(self.dir / "same.png")
        _write(same, 120, 80, _two_columns(12, 12))
        self.assertEqual(ratio_metrics(same)["ratio"], 1.0)
        self.assertEqual(_rc([same, "--min-ratio", "0.62", "--max-ratio", "0.72"]), 1)
        good = str(self.dir / "good.png")
        _write(good, 120, 80, _two_columns(12, 8))
        self.assertEqual(_rc([good, "--min-ratio", "0.62", "--max-ratio", "0.72"]), 0)

    def test_one_column_is_not_a_pass(self):
        p = str(self.dir / "one.png")
        _write(p, 120, 80, lambda x, y: 2 <= x < 8 and 2 <= y < 14)
        self.assertEqual(_rc([p, "--min-ratio", "0.62", "--max-ratio", "0.72"]), 1)

    def test_blank_capture_is_not_a_pass(self):
        p = str(self.dir / "blank.png")
        _write(p, 40, 40, lambda x, y: False)
        self.assertEqual(_rc([p, "--min-ratio", "0.62"]), 1)

    def test_an_roi_that_cuts_the_first_line_is_not_a_pass(self):
        p = str(self.dir / "cut.png")
        _write(p, 120, 80, _two_columns(12, 8))
        self.assertEqual(_rc([p, "--roi", "0,0,120,10", "--min-ratio", "0.62"]), 1)
        self.assertEqual(_rc([p, "--roi", "0,0,120,40", "--min-ratio", "0.62"]), 0)

    def test_dim_pixels_are_not_lit(self):
        # A grey outline below the threshold must not extend a line's run.
        base = _two_columns(12, 8)
        p = str(self.dir / "dim.png")
        w, h = 120, 80
        buf = bytearray(w * h * 3)
        for y in range(h):
            for x in range(w):
                o = (y * w + x) * 3
                if base(x, y):
                    buf[o], buf[o + 1], buf[o + 2] = WHITE
                elif y == 14 and 70 <= x < 110:
                    buf[o], buf[o + 1], buf[o + 2] = (90, 90, 90)
        util.write_png(p, w, h, bytes(buf), 3)
        self.assertEqual(ratio_metrics(p)["small"]["height"], 8)

    def test_bad_image_is_exit_2(self):
        self.assertEqual(_rc([str(self.dir / "nope.png"), "--min-ratio", "0.62"]), 2)

    def test_committed_reference_passes_the_manifest_gate(self):
        manifest = json.loads(_MANIFEST.read_text(encoding="utf-8"))
        run = next(r for r in manifest["extra_runs"] if r["name"] == "text_sizes")
        (entry,) = run["structural"]["text_size_sample"]
        self.assertEqual(entry["metric"], "text-size-ratio")
        roi = ",".join(str(v) for v in entry["roi"])
        argv = [str(_REFERENCE), "--roi", roi,
                "--min-ratio", str(entry["min_ratio"]),
                "--max-ratio", str(entry["max_ratio"])]
        self.assertEqual(_rc(argv), 0)
        m = ratio_metrics(str(_REFERENCE), tuple(entry["roi"]))
        self.assertEqual(m["large"]["height"], 24)
        self.assertEqual(m["small"]["height"], 16)
        self.assertAlmostEqual(m["ratio"], 0.6667, places=4)


if __name__ == "__main__":
    unittest.main()
