"""Region mode of render-detached-lighting-metric.py.

A secondary viewport's portrait is measured from one full-frame capture: the
probe is cropped out by ``--region`` and judged at ``--quarter-turns`` of yaw.
Synthetic PNGs only — the probe is painted with the metric's own expected
colours, so a pass proves the crop and the yaw mapping, and a repaint with the
wrong yaw's colours proves the gate still fires.
"""

import contextlib
import importlib.machinery
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))

_loader = importlib.machinery.SourceFileLoader(
    "render_detached_lighting_metric",
    str(_SCRIPTS / "render-detached-lighting-metric.py"))
_spec = importlib.util.spec_from_loader("render_detached_lighting_metric", _loader)
METRIC = importlib.util.module_from_spec(_spec)
_loader.exec_module(METRIC)

FRAME = (320, 200)
REGION = (200, 40, 100, 120)


def _paint_probe(image: Image.Image, region, cardinal: int) -> None:
    """A 64x64 probe inside ``region`` whose thirds carry the expected colours."""
    x, y, w, h = region
    left, top = x + (w - 64) // 2, y + (h - 64) // 2
    for face, u, v, normal in METRIC.FACE_SAMPLES:
        colour = METRIC.expected_color(normal, cardinal)
        # Each sample point sits in its own 16x16 patch.
        cx, cy = int(left + 64 * u), int(top + 64 * v)
        for py in range(cy - 8, cy + 8):
            for px in range(cx - 8, cx + 8):
                image.putpixel((px, py), colour)


class DetachedLightingRegionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def _frame(self, cardinal: int, decoy: bool = True) -> Path:
        image = Image.new("RGB", FRAME, (0, 0, 0))
        _paint_probe(image, REGION, cardinal)
        if decoy:
            # Content outside the region must not disturb the probe's bbox.
            for py in range(10, 30):
                for px in range(10, 60):
                    image.putpixel((px, py), (200, 30, 30))
        path = self.dir / f"frame_{cardinal}.png"
        image.save(path)
        return path

    def _measure(self, path: Path, cardinal: int) -> bool:
        with contextlib.redirect_stdout(io.StringIO()):
            return METRIC.measure(path, cardinal, REGION)

    def test_region_passes_at_each_quarter_turn(self):
        for cardinal in range(4):
            with self.subTest(cardinal=cardinal):
                self.assertTrue(self._measure(self._frame(cardinal), cardinal))

    def test_wrong_quarter_turn_fails(self):
        self.assertFalse(self._measure(self._frame(0), 1))

    def test_whole_frame_without_region_sees_the_decoy(self):
        # The decoy sits in the frame's bbox, so the un-cropped read is wrong:
        # the region is what makes a one-frame measurement meaningful.
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(METRIC.measure(self._frame(0), 0))

    def test_empty_region_fails(self):
        path = self.dir / "empty.png"
        Image.new("RGB", FRAME, (0, 0, 0)).save(path)
        self.assertFalse(self._measure(path, 0))

    def test_parse_region_rejects_bad_forms(self):
        self.assertEqual(METRIC.parse_region("1,2,3,4"), (1, 2, 3, 4))
        for bad in ("1,2,3", "1,2,0,4", "1,2,3,-4"):
            with self.assertRaises(Exception):
                METRIC.parse_region(bad)


if __name__ == "__main__":
    unittest.main()
