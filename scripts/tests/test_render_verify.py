"""Tests for render-verify.py — the ROI-crop + structural-metric gate (T-2).

Proves the gate wiring added in epic #1766 T-2 without a GL/Metal build:

  * full-frame pixel-diff still passes/fails as before (backward compat);
  * a manifest-declared ROI crop is compared against a committed reference
    crop and FAILS on a seeded regression;
  * a manifest-declared structural gate runs render-shadow-metric.py and
    FAILS when the shadow exceeds its hole_ratio threshold (seeded
    swiss-cheese);
  * misconfigurations (unknown shot, missing reference, un-captured crop,
    unimplemented metric, threshold-less gate) are surfaced loudly.

Synthetic PNGs only — no engine, no committed references. Import the
dashed-name scripts via importlib, matching test_render_shadow_metric.py.
"""
import importlib.machinery
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_rv = _load("render_verify", "render-verify.py")
_cmp = _load("render_compare", "render-compare.py")
write_png = _cmp.write_png

evaluate_shots = _rv.evaluate_shots
_run_structural_metric = _rv._run_structural_metric
_crop_capture_path = _rv._crop_capture_path

MAGENTA = (255, 0, 255)
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)


def _write(path: Path, w: int, h: int, fn) -> None:
    """fn(x, y) -> (r, g, b); written as an 8-bit RGB PNG."""
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            r, g, b = fn(x, y)
            o = (y * w + x) * 3
            buf[o], buf[o + 1], buf[o + 2] = r, g, b
    write_png(str(path), w, h, bytes(buf), 3)


class RenderVerifyHarness(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.caps = root / "caps"            # captured screenshots
        self.refs = root / "refs"            # committed references (backend dir)
        self.diffs = root / "diffs"
        self.caps.mkdir()
        self.refs.mkdir()
        # One full frame per label, in manifest order: screenshot_<idx>.png.
        self.labels = ["shotA", "shotB"]
        self.frames = []
        for i, _label in enumerate(self.labels):
            p = self.caps / f"screenshot_{i:06d}.png"
            _write(p, 16, 16, lambda x, y: BLACK)
            self.frames.append(p)

    def tearDown(self):
        self._tmp.cleanup()

    def _ref(self, name: str, fn=lambda x, y: BLACK, w=16, h=16):
        _write(self.refs / name, w, h, fn)

    def _eval(self, crops=None, structural=None):
        return evaluate_shots(
            captured=self.frames,
            shot_labels=self.labels,
            ref_dir=self.refs,
            diff_dir=self.diffs,
            thresholds={},
            crops=crops,
            structural=structural,
        )

    def _row(self, rows, label):
        matches = [r for r in rows if r["label"] == label]
        self.assertEqual(len(matches), 1, f"expected exactly one row for {label}")
        return matches[0]

    # ── full-frame (backward compat) ─────────────────────────────────────
    def test_clean_frame_passes(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        rows = self._eval()
        self.assertEqual([r["kind"] for r in rows], ["frame", "frame"])
        self.assertTrue(all(r["pass"] for r in rows))

    def test_regressed_frame_fails(self):
        self._ref("shotA.png")
        self._ref("shotB.png", fn=lambda x, y: WHITE)  # ref differs from capture
        rows = self._eval()
        self.assertTrue(self._row(rows, "shotA")["pass"])
        self.assertFalse(self._row(rows, "shotB")["pass"])

    def test_missing_frame_reference_fails(self):
        self._ref("shotA.png")  # shotB.png absent
        rows = self._eval()
        b = self._row(rows, "shotB")
        self.assertFalse(b["pass"])
        self.assertIn("no reference", b["reason"])

    def test_backward_compat_no_gate_blocks(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        rows = self._eval(crops=None, structural=None)
        self.assertTrue(all(r["kind"] == "frame" for r in rows))
        self.assertEqual(len(rows), 2)

    # ── ROI crops ────────────────────────────────────────────────────────
    def _write_crop_capture(self, label, crop_label, fn):
        idx = self.labels.index(label)
        path = _crop_capture_path(self.frames[idx], label, crop_label)
        _write(path, 8, 8, fn)
        return path

    def test_clean_crop_passes(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._write_crop_capture("shotA", "center", lambda x, y: MAGENTA)
        self._ref("shotA__crop_center.png", fn=lambda x, y: MAGENTA, w=8, h=8)
        rows = self._eval(crops={"shotA": ["center"]})
        self.assertTrue(self._row(rows, "shotA:center")["pass"])

    def test_regressed_crop_fails(self):
        # Seeded regression: the captured crop diverges from its reference.
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._write_crop_capture("shotA", "center", lambda x, y: WHITE)
        self._ref("shotA__crop_center.png", fn=lambda x, y: MAGENTA, w=8, h=8)
        rows = self._eval(crops={"shotA": ["center"]})
        crop = self._row(rows, "shotA:center")
        self.assertEqual(crop["kind"], "crop")
        self.assertFalse(crop["pass"])

    def test_missing_crop_reference_fails(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._write_crop_capture("shotA", "center", lambda x, y: MAGENTA)
        # no shotA__crop_center.png reference committed
        rows = self._eval(crops={"shotA": ["center"]})
        crop = self._row(rows, "shotA:center")
        self.assertFalse(crop["pass"])
        self.assertIn("no reference", crop["reason"])

    def test_uncaptured_crop_fails(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        self._ref("shotA__crop_center.png", fn=lambda x, y: MAGENTA, w=8, h=8)
        # the crop PNG was never emitted by the demo
        rows = self._eval(crops={"shotA": ["center"]})
        crop = self._row(rows, "shotA:center")
        self.assertFalse(crop["pass"])
        self.assertIn("not captured", crop["reason"])

    def test_unknown_shot_in_crops_raises(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        with self.assertRaises(SystemExit):
            self._eval(crops={"nonexistent": ["center"]})

    # ── structural-metric gates ──────────────────────────────────────────
    def test_structural_shadow_clean_passes(self):
        # A solid magenta SHADOW overlay reads 0 holes -> within threshold.
        self._ref("shotA.png")
        self._ref("shotB.png")
        _write(self.frames[0], 32, 32, lambda x, y: MAGENTA)
        rows = self._eval(structural={
            "shotA": [{"metric": "shadow", "max_hole_ratio": 0.05}]})
        struct = self._row(rows, "shotA:shadow")
        self.assertEqual(struct["kind"], "struct")
        self.assertTrue(struct["pass"])

    def test_structural_shadow_swiss_cheese_fails(self):
        # Seeded regression: a checkerboard SHADOW overlay (~50% holes,
        # exploded component count) exceeds the gate.
        self._ref("shotA.png")
        self._ref("shotB.png")
        _write(self.frames[0], 32, 32,
               lambda x, y: MAGENTA if (x + y) % 2 == 0 else BLACK)
        rows = self._eval(structural={
            "shotA": [{"metric": "shadow",
                       "max_hole_ratio": 0.05, "max_components": 8}]})
        struct = self._row(rows, "shotA:shadow")
        self.assertFalse(struct["pass"])
        self.assertTrue(struct.get("reason"))

    def test_structural_roi_forwarded(self):
        # Left half shadow, right half lit; an ROI over the lit half is all
        # holes and fails, proving --roi is forwarded to the metric.
        self._ref("shotA.png")
        self._ref("shotB.png")
        _write(self.frames[0], 32, 32, lambda x, y: MAGENTA if x < 16 else BLACK)
        rows = self._eval(structural={
            "shotA": [{"metric": "shadow", "roi": [16, 0, 16, 32],
                       "max_hole_ratio": 0.05}]})
        self.assertFalse(self._row(rows, "shotA:shadow")["pass"])

    def test_structural_unimplemented_metric_raises(self):
        # shadow/coverage/silhouette/clip are implemented; an unknown metric
        # name must still fail loudly (no render-<metric>-metric.py script).
        with self.assertRaises(SystemExit) as cm:
            _run_structural_metric(self.frames[0],
                                   {"metric": "nonexistent", "max_hole_ratio": 0.9},
                                   "shotA")
        self.assertIn("not", str(cm.exception).lower())

    def test_structural_no_thresholds_raises(self):
        with self.assertRaises(SystemExit):
            _run_structural_metric(self.frames[0], {"metric": "shadow"}, "shotA")

    def test_structural_missing_metric_key_raises(self):
        with self.assertRaises(SystemExit):
            _run_structural_metric(self.frames[0], {"max_hole_ratio": 0.05}, "shotA")

    def test_unknown_shot_in_structural_raises(self):
        self._ref("shotA.png")
        self._ref("shotB.png")
        with self.assertRaises(SystemExit):
            self._eval(structural={"nope": [{"metric": "shadow",
                                             "max_hole_ratio": 0.05}]})

    # ── crop-path derivation ─────────────────────────────────────────────
    def test_crop_capture_path_pairs_with_frame_index(self):
        frame = Path("/x/save/screenshot_000007.png")
        got = _crop_capture_path(frame, "shotA", "center_cube_top")
        self.assertEqual(
            got.name, "screenshot_000007_shotA__crop_center_cube_top.png")
        self.assertEqual(got.parent, frame.parent)


if __name__ == "__main__":
    unittest.main()
