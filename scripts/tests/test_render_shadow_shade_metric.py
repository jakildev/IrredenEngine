"""Hermetic controls for the shadow shade-only metric."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

SCRIPT = Path(__file__).resolve().parents[1] / "render-shadow-shade-metric.py"


class ShadowShadeMetricTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.parent = self.root / "parent"
        self.head = self.root / "head"
        self.parent.mkdir()
        self.head.mkdir()

    def write_pair(self, parent_pixels, head_pixels):
        parent = Image.new("RGB", (3, 1))
        parent.putdata(parent_pixels)
        parent.save(self.parent / "row.png")
        head = Image.new("RGB", (3, 1))
        head.putdata(head_pixels)
        head.save(self.head / "row.png")

    def run_cli(self):
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(self.parent),
                str(self.head),
                "row.png",
            ],
            capture_output=True,
            text=True,
        )

    def test_shade_only_pair_passes(self):
        self.write_pair(
            [(20, 30, 40), (90, 80, 70), (5, 5, 5)],
            [(30, 40, 50), (80, 70, 60), (5, 5, 5)],
        )
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("| 2 | 1 | 1 | 0 | 1 | PASS |", result.stdout)

    def test_opposed_channel_pixel_fails(self):
        self.write_pair(
            [(20, 30, 40), (5, 5, 5), (5, 5, 5)],
            [(30, 20, 40), (5, 5, 5), (5, 5, 5)],
        )
        result = self.run_cli()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("| 1 | 0 | 0 | 1 | 1 | FAIL |", result.stdout)

    def test_identical_pair_is_not_moved(self):
        pixels = [(20, 30, 40), (5, 5, 5), (90, 80, 70)]
        self.write_pair(pixels, pixels)
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("| 0 | 0 | 0 | 0 | 0 | NOT MOVED |", result.stdout)


if __name__ == "__main__":
    unittest.main()
