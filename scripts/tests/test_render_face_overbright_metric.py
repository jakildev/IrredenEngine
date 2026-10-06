"""Synthetic controls for the face over-brightness metric."""

import importlib.machinery
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
import render_metric_util as rmu  # noqa: E402

SCRIPT = SCRIPTS / "render-face-overbright-metric.py"
loader = importlib.machinery.SourceFileLoader("face_overbright_metric", str(SCRIPT))
spec = importlib.util.spec_from_loader("face_overbright_metric", loader)
metric = importlib.util.module_from_spec(spec)
loader.exec_module(metric)

CEILING = (64, 128, 141)
CLEAN_FACE = (61, 122, 134)
BRIGHT_EDGE = (73, 147, 161)
OFF_HUE_BRIGHT = (96, 96, 102)


def write_capture(path: Path, colors: list[tuple[int, int, int]]) -> None:
    rmu.write_png(str(path), len(colors), 1,
                  bytes(channel for color in colors for channel in color), 3)


class FaceOverbrightMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_cli(self, *paths: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--ceiling", "64,128,141",
             *(str(path) for path in paths)],
            capture_output=True,
            text=True,
        )

    def assert_cli_result(
        self, path: Path, returncode: int, counts: str, result: str
    ) -> None:
        completed = self.run_cli(path)
        self.assertEqual(completed.returncode, returncode, completed.stderr)
        self.assertIn(counts, completed.stdout)
        self.assertTrue(completed.stdout.endswith(f"RESULT={result}\n"))

    def test_exact_overbright_count_fails(self):
        path = self.root / "bright.png"
        write_capture(path, [CLEAN_FACE, BRIGHT_EDGE, BRIGHT_EDGE, OFF_HUE_BRIGHT])
        self.assertEqual(metric.measure(path, CEILING), (3, 2))
        self.assert_cli_result(path, 1, "hue_px=3 overbright_px=2", "FAIL")

    def test_clean_face_passes_and_ignores_off_hue_bright_pixel(self):
        path = self.root / "clean.png"
        write_capture(path, [CLEAN_FACE, CLEAN_FACE, OFF_HUE_BRIGHT])
        self.assert_cli_result(path, 0, "hue_px=2 overbright_px=0", "PASS")

    def test_no_hue_capture_is_invalid(self):
        path = self.root / "empty.png"
        write_capture(path, [(0, 0, 0), OFF_HUE_BRIGHT])
        self.assert_cli_result(path, 2, "hue_px=0 overbright_px=0", "FAIL")


if __name__ == "__main__":
    unittest.main()
