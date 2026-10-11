"""Validate fixed-scene rotation, density and motion matrix arguments."""

import math
import subprocess
import sys
import unittest
from pathlib import Path

from repeat_profile import frame_yaw
from rotation_controls import cases


class ZoomControlsTest(unittest.TestCase):
    def test_zoom_matrix_holds_scene_and_pivot_fixed(self):
        selected = cases("zoom")
        self.assertEqual(len(selected), 8)
        combinations = set()
        for arguments in selected.values():
            def value(flag):
                return arguments[arguments.index(flag) + 1]
            self.assertEqual(value("--grid-size"), "32")
            self.assertEqual(value("--base-subdivisions"), "1")
            self.assertEqual(value("--wave-amplitude"), "0")
            self.assertIn("--pivot-origin", arguments)
            self.assertIn("--no-overlay", arguments)
            combinations.add((value("--yaw"), value("--zoom"), value("--subdivision-mode")))
        self.assertEqual(combinations, {
            (yaw, zoom, mode)
            for yaw in ("0", "0.785398163")
            for zoom in ("1", "4")
            for mode in ("none", "full")
        })


class MotionControlsTest(unittest.TestCase):
    def test_single_frame_cannot_claim_a_moving_camera(self):
        command = [sys.executable, str(Path(__file__).with_name("rotation_controls.py")),
                   "--suite", "motion", "--frames", "1", "--output", "unused", "--dry-run"]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("at least two frames", result.stderr)

    def test_full_turn_and_density_pairs_at_each_frame_count(self):
        for frames in (180, 300):
            selected = cases("motion", frames)
            self.assertEqual(len(selected), 18)
            for wave in (0, 5):
                for zoom, base in ((1, 1), (1, 4), (4, 4)):
                    prefix = f"motion-wave{wave}-zoom{zoom}-base{base}"
                    sweep = selected[prefix + "-sweep"]
                    self.assertAlmostEqual(frame_yaw(sweep, frames + 1), math.tau)
                    self.assertEqual(frame_yaw(sweep, 1), 0)
                    for pose in ("cardinal", "diagonal", "sweep"):
                        args = selected[prefix + "-" + pose]
                        def value(flag):
                            return args[args.index(flag) + 1]
                        self.assertEqual(value("--grid-size"), "64")
                        self.assertEqual(value("--wave-amplitude"), str(wave))
                        self.assertEqual(value("--zoom"), str(zoom))
                        self.assertEqual(value("--base-subdivisions"), str(base))
                        self.assertIn("--pivot-origin", args)
                        self.assertNotIn("--capture-frame", args)


if __name__ == "__main__":
    unittest.main()
