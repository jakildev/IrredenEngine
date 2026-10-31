"""Execute the shared PR capture helpers: staging round-trip and crop grading."""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import pr_capture_common as common  # noqa: E402

SHADER = "ir_sun_projection.glsl"


class PrCaptureCommonTest(unittest.TestCase):
    def test_stage_then_restore_round_trips_the_working_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            shader_dir = Path(directory)
            (shader_dir / SHADER).write_bytes(b"working copy\n")
            saved = common.stage_shaders(shader_dir, (SHADER,), "", ref="HEAD", root=ROOT)
            staged = (shader_dir / SHADER).read_bytes()
            self.assertEqual(staged, subprocess.check_output(
                ["git", "show", f"HEAD:engine/render/src/shaders/{SHADER}"], cwd=ROOT))
            self.assertEqual(saved[SHADER], b"working copy\n")
            common.restore_shaders(shader_dir, saved)
            self.assertEqual((shader_dir / SHADER).read_bytes(), b"working copy\n")
            self.assertEqual(common.sha256(shader_dir / SHADER),
                             common.sha256(shader_dir / SHADER))

    @unittest.skipUnless(shutil.which("python3"), "needs python3")
    def test_grade_splits_missing_and_excess_outside_the_band(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow not installed")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            control = Image.new("L", (40, 40), 200)
            for x in range(10, 20):
                for y in range(10, 20):
                    control.putpixel((x, y), 50)
            control.save(path / "control.png")
            lost = Image.new("L", (40, 40), 200)
            lost.save(path / "lost.png")
            gained = control.copy()
            for x in range(30, 36):
                for y in range(30, 36):
                    gained.putpixel((x, y), 50)
            gained.save(path / "gained.png")
            missing = common.grade_crops(path / "lost.png", path / "control.png",
                                         path / "lost-outside.png", band=2)
            self.assertEqual(missing["excess_shadow_outside_band"], 0)
            self.assertEqual(missing["missing_shadow_outside_band"], 36)
            self.assertTrue((path / "lost-outside.png").exists())
            excess = common.grade_crops(path / "gained.png", path / "control.png",
                                        path / "gained-outside.png", band=2)
            self.assertEqual(excess["missing_shadow_outside_band"], 0)
            self.assertEqual(excess["excess_shadow_outside_band"], 36)
            same = common.grade_crops(path / "control.png", path / "control.png",
                                      path / "same-outside.png", band=2)
            self.assertEqual(same["changed"], 0)
            self.assertFalse((path / "same-outside.png").exists())


if __name__ == "__main__":
    unittest.main()
