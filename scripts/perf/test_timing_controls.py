"""Timing attribution requires sampled evidence of the requested timer mode."""

import tempfile
import unittest
from pathlib import Path

from timing_controls import cases, verify_timing_modes


class TimingControlsTest(unittest.TestCase):
    def test_pairs_change_only_the_timer_preset(self):
        selected = cases(Path("captures"), "all")
        self.assertEqual(len(selected), 6)
        for pose in ("low", "dense", "rotated"):
            counter = selected[f"{pose}-counters"]
            synchronized = selected[f"{pose}-synchronized"]
            self.assertEqual(counter[:-1], synchronized[:-1])
            self.assertEqual(counter[-2], "--config-preset")
            self.assertEqual(Path(counter[-1]).name, "counters.lua")
            self.assertEqual(Path(synchronized[-1]).name, "synchronized.lua")
            self.assertEqual(set(cases(Path("captures"), pose)),
                             {f"{pose}-counters", f"{pose}-synchronized"})

    def test_missing_and_unsampled_reports_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            selected = {"low-counters": []}
            with self.assertRaisesRegex(ValueError, "missing timing reports"):
                verify_timing_modes(root, selected)
            path = root / "low-counters/round-1/run-1.txt"
            path.parent.mkdir(parents=True)
            for light in ("", "computeLightVolume 1 0 2 0\n",
                          "computeLightVolume 1 2\n"):
                path.write_text("--- GPU stage timing ---\n" + light
                                + "voxelCompact 1 0 2 10\n")
                with self.assertRaisesRegex(ValueError, "missing sampled whole-system"):
                    verify_timing_modes(root, selected)

    def test_mode_mismatch_is_rejected_in_every_round(self):
        for mode in ("counters", "synchronized"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                name = f"low-{mode}"
                prefix = "--- GPU stage timing ---\ncomputeLightVolume 1 0 2 10\n"
                sampled = "voxelCompact 1 0 2 10\n"
                unwired = "voxelCompact 0 0 0 0\n"
                for index in (1, 2):
                    path = root / name / f"round-{index}/run-1.txt"
                    path.parent.mkdir(parents=True)
                    path.write_text(prefix + (sampled if mode == "counters" else unwired))
                verify_timing_modes(root, {name: []})
                if mode == "synchronized":
                    path.write_text(prefix)
                    verify_timing_modes(root, {name: []})
                path.write_text(prefix + (unwired if mode == "counters" else sampled))
                with self.assertRaisesRegex(ValueError, "disagrees with timing mode"):
                    verify_timing_modes(root, {name: []})


if __name__ == "__main__":
    unittest.main()
