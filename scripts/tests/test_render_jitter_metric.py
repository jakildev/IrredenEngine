"""Tests for render-jitter-metric.py — the temporal-jitter metric.

Proves the core claim the metric exists to make: a frame sequence undergoing
*smooth* motion (a gradient that ramps or pans frame-to-frame) scores a near-
zero ``jitter_score``, while a sequence whose interior *oscillates* (a crawling-
band pattern that toggles each frame — the epic #1881 / #1920 artifact) scores
an order of magnitude higher and trips the ``--max-jitter`` gate. The second
temporal difference is what separates the two; a first-difference metric would
flag both. Synthetic PNGs only — no GL/Metal context. Import via importlib
(dashed name), mirroring test_render_silhouette_metric.py.
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import math
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


_mod = _load("render_jitter_metric", "render-jitter-metric.py")
jitter_metrics = _mod.jitter_metrics
_util = _mod.util

W, H = 48, 48
# A centred 24x24 foreground square; its silhouette is identical in every frame
# (only the interior shading varies), so the AND-of-masks interior is the whole
# square — exactly the "stable surface" the metric measures.
SQ_LO, SQ_HI = 12, 36


def _rc(argv) -> int:
    with contextlib.redirect_stdout(io.StringIO()), \
            contextlib.redirect_stderr(io.StringIO()):
        return _mod._main(argv)


def _write_seq(d: Path, n: int, shade):
    """Write n gray RGBA frames; ``shade(x, y, t)`` -> 0..255 luminance.

    Background (outside the square) is black; the square carries the shade.
    Returns the ordered list of paths.
    """
    paths = []
    for t in range(n):
        buf = bytearray(4 * W * H)
        for y in range(H):
            for x in range(W):
                inside = SQ_LO <= x < SQ_HI and SQ_LO <= y < SQ_HI
                v = int(shade(x, y, t)) if inside else 0
                v = 0 if v < 0 else (255 if v > 255 else v)
                o = (y * W + x) * 4
                buf[o] = buf[o + 1] = buf[o + 2] = v
                buf[o + 3] = 255
        p = d / f"frame_{t:03d}.png"
        _util.write_png(str(p), W, H, bytes(buf), 4)
        paths.append(str(p))
    return paths


# Shade functions (all keep the square foreground: value >= ~60 > bg_tol).
def _static(x, y, t):
    return 140


def _temporal_ramp(x, y, t):
    # Uniform brightness rising linearly with the frame: per-pixel linear in t,
    # so the second temporal difference is exactly zero.
    return 80 + t


def _moving_linear_gradient(x, y, t):
    # A spatial gradient sliding 1px/frame: per pixel still linear in t -> d2=0.
    return 90 + (x + t)


def _moving_sinusoid(x, y, t):
    # A sinusoidal gradient sliding with the frame — genuine curved smooth
    # motion. Per pixel it is a low-frequency sinusoid in t, so d2 is small but
    # nonzero (bounded by amplitude * angular_freq**2).
    return 128 + 40.0 * math.sin((x + t) * 0.15)


def _crawling_rings(x, y, t):
    # The artifact: concentric shells (by Manhattan distance) whose parity flips
    # every frame, so each interior pixel toggles 60<->200 — maximal curvature.
    d = abs(x - 24) + abs(y - 24)
    return 200 if (d + t) % 2 == 0 else 60


class JitterMetricTest(unittest.TestCase):
    def _score(self, shade, n=12, **kw):
        with tempfile.TemporaryDirectory() as td:
            paths = _write_seq(Path(td), n, shade)
            return jitter_metrics(paths, **kw)

    def test_static_sequence_is_zero(self):
        m = self._score(_static)
        self.assertEqual(m["jitter_score"], 0.0)
        self.assertEqual(m["mean_abs_d1"], 0.0)
        self.assertEqual(m["interior_px"], (SQ_HI - SQ_LO) ** 2)

    def test_temporal_ramp_zero_jitter_but_moves(self):
        m = self._score(_temporal_ramp)
        # Linear-in-t -> second difference vanishes, but first difference does
        # not (it moves). This is the crux: motion without jitter.
        self.assertLess(m["jitter_score"], 0.01)
        self.assertGreater(m["mean_abs_d1"], 0.5)

    def test_moving_linear_gradient_zero_jitter(self):
        m = self._score(_moving_linear_gradient)
        self.assertLess(m["jitter_score"], 0.01)
        self.assertGreater(m["mean_abs_d1"], 0.5)

    def test_moving_sinusoid_small_jitter(self):
        m = self._score(_moving_sinusoid, n=16)
        # Smooth curved motion: bounded, well under a crawl.
        self.assertLess(m["jitter_score"], 3.0)

    def test_crawling_rings_high_jitter(self):
        m = self._score(_crawling_rings)
        self.assertGreater(m["jitter_score"], 50.0)
        # Every interior pixel toggles, so essentially all of it crawls.
        self.assertGreater(m["flicker_frac"], 0.9)
        self.assertGreater(m["flicker_p95"], 50.0)

    def test_discriminates_crawl_from_smooth(self):
        smooth = self._score(_moving_sinusoid, n=16)
        crawl = self._score(_crawling_rings, n=16)
        # The headline property: an order of magnitude of separation.
        self.assertGreater(crawl["jitter_score"], 10.0 * smooth["jitter_score"] + 10.0)

    def test_threshold_gate_exit_codes(self):
        # Smooth passes a 3.0 gate; crawl fails it. Run via _main for the
        # exit-code contract (0 within thresholds, 1 exceeded).
        with tempfile.TemporaryDirectory() as td:
            sp = _write_seq(Path(td), 16, _moving_sinusoid)
            self.assertEqual(_rc([*sp, "--max-jitter", "3.0"]), 0)
        with tempfile.TemporaryDirectory() as td:
            cp = _write_seq(Path(td), 16, _crawling_rings)
            self.assertEqual(_rc([*cp, "--max-jitter", "3.0"]), 1)

    def test_too_few_frames_is_io_error(self):
        with tempfile.TemporaryDirectory() as td:
            paths = _write_seq(Path(td), 2, _static)
            self.assertEqual(_rc(paths), 2)

    def test_roi_scopes_measurement(self):
        # An ROI fully inside the square keeps every ROI pixel interior.
        m = self._score(_crawling_rings, roi=(16, 16, 8, 8))
        self.assertEqual(m["roi"], [16, 16, 8, 8])
        self.assertEqual(m["interior_px"], 64)

    def test_diff_out_written_at_frame_dims(self):
        with tempfile.TemporaryDirectory() as td:
            paths = _write_seq(Path(td), 8, _crawling_rings)
            out = str(Path(td) / "heat.png")
            jitter_metrics(paths, diff_out=out)
            w, h, _bpp, _pix = _util.read_png(out)
            self.assertEqual((w, h), (W, H))


if __name__ == "__main__":
    unittest.main()
