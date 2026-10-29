"""Edge-speckle metric: synthetic controls and the retained CanvasStress frames."""

import functools
import importlib.machinery
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parent
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-shadow-speckle-metric.py"
loader = importlib.machinery.SourceFileLoader("shadow_speckle_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("shadow_speckle_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)

ALBEDO = (70, 210, 210)
TOP = tuple(round(c * 0.76) for c in ALBEDO)
SIDE = tuple(round(c * 0.62) for c in ALBEDO)
SHADOW = tuple(round(c * 0.30) for c in ALBEDO)

REFERENCES = REPO / "creations/demos/canvas_stress/test/references/macos-debug"
MANIFEST = REFERENCES.parent / "manifest.json"
RETAINED = REPO / "docs/pr-screenshots/claude/4763-canvas-stress-mismatch-attribution/master"


def speckle_gates():
    """Every `shadow-speckle` structural gate in the CanvasStress manifest, by row."""
    gates = {}
    for run in json.loads(MANIFEST.read_text())["extra_runs"]:
        for row, entries in run.get("structural", {}).items():
            for entry in entries:
                if entry["metric"] == "shadow-speckle":
                    gates[row] = entry["max_edge_speckles"]
    return gates


def read_frame(image, measured_at=None):
    return metric.measure(str(image), None, tuple(rmu.DEFAULT_BG), 1, 1.8,
                          trixel_px=measured_at, sample_limit=None)


# Committed frames are read once across both tests; rescaled copies reuse one
# temporary path and are never cached.
read_committed_frame = functools.lru_cache(maxsize=None)(read_frame)


def two_face_capture(path, dark_cells):
    """A 16x16 tile: TOP face above row 8, SIDE face below, black border."""
    pixels = bytearray()
    for y in range(16):
        for x in range(16):
            if not (2 <= x < 14 and 2 <= y < 14):
                colour = (0, 0, 0)
            elif (x, y) in dark_cells:
                colour = SHADOW
            else:
                colour = TOP if y < 8 else SIDE
            pixels.extend(colour)
    rmu.write_png(str(path), 16, 16, bytes(pixels), 3)


def rescaled(src, dst, num, den):
    """Nearest-neighbour copy of ``src`` at ``num/den`` of its size: what the
    same frame looks like at another capture scale."""
    w, h, bpp, pixels = rmu.read_png(str(src))
    out_w, out_h = w * num // den, h * num // den
    columns = [(x * den // num) * bpp for x in range(out_w)]
    rows = []
    for y in range(out_h):
        start = (y * den // num) * w * bpp
        rows.append(b"".join(pixels[start + c:start + c + 3] for c in columns))
    rmu.write_png(str(dst), out_w, out_h, b"".join(rows), 3)


def block(x, y, size=2):
    return {(x + dx, y + dy) for dx in range(size) for dy in range(size)}


class ShadowSpeckleMetricTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def measure(self, dark_cells):
        path = self.root / "capture.png"
        two_face_capture(path, dark_cells)
        return metric.measure(str(path), None, (0, 0, 0), 1, 1.8, trixel_px=2)

    def test_trixel_on_face_boundary_is_an_edge_speckle(self):
        result = self.measure(block(6, 7))
        self.assertEqual((result["speckles"], result["edge_speckles"]), (1, 1))

    def test_trixel_inside_one_face_is_not_an_edge_speckle(self):
        result = self.measure(block(6, 3))
        self.assertEqual((result["speckles"], result["edge_speckles"]), (1, 0))

    def test_projected_shadow_band_is_not_a_speckle(self):
        band = {(x, y) for x in range(4, 12) for y in range(5, 11)}
        result = self.measure(band)
        self.assertEqual(result["speckles"], 0)
        self.assertGreater(result["shadow_boundary_pixels"], 0)

    def test_unshadowed_faces_report_nothing(self):
        result = self.measure(set())
        self.assertEqual(
            (result["shadow_boundary_pixels"], result["speckles"]), (0, 0))

    def test_cli_exit_code_follows_threshold(self):
        path = self.root / "capture.png"
        two_face_capture(path, block(6, 7) | block(10, 7))
        for limit, code in (("2", 0), ("1", 1)):
            proc = subprocess.run(
                [sys.executable, str(SCRIPT), str(path), "--trixel-px", "2",
                 "--max-edge-speckles", limit],
                capture_output=True, text=True)
            self.assertEqual(proc.returncode, code, proc.stdout + proc.stderr)
            self.assertEqual(json.loads(proc.stdout)["edge_speckles"], 2)

    def test_missing_image_is_a_format_error(self):
        proc = subprocess.run(
            [sys.executable, str(SCRIPT), str(self.root / "absent.png")],
            capture_output=True, text=True)
        self.assertEqual(proc.returncode, 2)


class RetainedCanvasStressFramesTest(unittest.TestCase):
    """The manifest gates pass the committed references and fail the retained
    frames of the unlimited receiver, row for row."""

    def edge_speckles(self, image, trixel_px=2, measured_at=None):
        committed = image.parent in (REFERENCES, RETAINED)
        result = (read_committed_frame if committed else read_frame)(image, measured_at)
        self.assertEqual(result["trixel_px"], measured_at or trixel_px)
        self.assertEqual(len(result["edge_speckle_samples"]), result["edge_speckles"])
        return result["edge_speckles"]

    def test_gates_pass_references_and_fail_unlimited_receiver_frames(self):
        gates = speckle_gates()
        retained = sorted(path.stem for path in RETAINED.glob("*.png"))
        self.assertEqual(retained, sorted(set(gates) - {"modeswitch_grid_return"}))
        for row, limit in gates.items():
            with self.subTest(row=row):
                self.assertLessEqual(self.edge_speckles(REFERENCES / f"{row}.png"), limit)
                if row in retained:
                    self.assertGreater(self.edge_speckles(RETAINED / f"{row}.png"), limit)

    def test_ceilings_grade_every_capture_scale_alike(self):
        """A 1x capture (Windows/OpenGL) and a 3x one of the same frame read
        the 2x count; a pixel cutoff fixed at the 2x trixel overcounts at 1x."""
        gates = speckle_gates()
        frames = [REFERENCES / f"{row}.png" for row in gates]
        frames += sorted(RETAINED.glob("*.png"))
        with tempfile.TemporaryDirectory() as temporary:
            scaled = Path(temporary) / "scaled.png"
            for frame in frames:
                with self.subTest(frame=frame.relative_to(REPO)):
                    count = self.edge_speckles(frame)
                    rescaled(frame, scaled, 1, 2)
                    self.assertEqual(self.edge_speckles(scaled, trixel_px=1), count)
            settled = REFERENCES / "impulse_settled.png"
            rescaled(settled, scaled, 3, 2)
            self.assertEqual(self.edge_speckles(scaled, trixel_px=3),
                             self.edge_speckles(settled))
            rescaled(settled, scaled, 1, 2)
            self.assertGreater(self.edge_speckles(scaled, measured_at=2),
                               gates["impulse_settled"])


if __name__ == "__main__":
    unittest.main()
