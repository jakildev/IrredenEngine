"""Sampling must never attach to a different fleet run."""

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from repeat_profile import (
    directory_digest,
    find_demo_pid,
    requested_yaw,
    run_profile,
    yaw_pose_mismatch,
)


class YawPoseTest(unittest.TestCase):
    LOG = "[info] Initial camera yaw: requested_rad={} yaw_deg={} residual_deg=0.0000\n"

    def test_radians_flag_must_match_the_logged_degrees(self):
        self.assertIsNone(
            yaw_pose_mismatch(["--yaw", "0.785398163"], self.LOG.format("0.785398", "45.000"))
        )
        self.assertIsNone(yaw_pose_mismatch(["--yaw=90"], self.LOG.format("90", "116.620")))
        self.assertIsNone(yaw_pose_mismatch(["--yaw", "-0.1"], self.LOG.format("-0.1", "-5.730")))

    def test_a_degrees_reading_of_the_flag_fails(self):
        reason = yaw_pose_mismatch(["--yaw", "0.785398163"], self.LOG.format("0.785398", "0.785"))
        self.assertIn("45.000 deg", reason)

    def test_the_last_yaw_wins_and_a_trailing_flag_is_ignored(self):
        self.assertEqual(requested_yaw(["--yaw", "1", "--yaw=2"]), 2.0)
        self.assertIsNone(requested_yaw(["--zoom", "4", "--yaw"]))
        self.assertIsNone(requested_yaw(["--yaw-ramp"]))

    def test_a_value_the_check_cannot_compare_is_refused_not_passed(self):
        for value in ("nan", "inf", "45deg", ""):
            with self.subTest(value=value), self.assertRaises(ValueError):
                requested_yaw([f"--yaw={value}"])

    def test_the_pi_wrap_compares_on_the_circle(self):
        self.assertIsNone(yaw_pose_mismatch(["--yaw", "-3.14149"], self.LOG.format("x", "180.006")))

    def test_missing_pose_line_fails_only_when_yaw_was_requested(self):
        self.assertIn("no 'Initial camera yaw'", yaw_pose_mismatch(["--yaw", "1"], "RESULT=CLEAN"))
        self.assertIsNone(yaw_pose_mismatch(["--zoom", "4"], "RESULT=CLEAN"))


class RuntimeAssetsTest(unittest.TestCase):
    def test_digest_tracks_content_and_paths_not_creation_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            left, right = root / "left", root / "right"
            left.mkdir()
            right.mkdir()
            for directory, names in ((left, ("config.lua", "scene.lua")),
                                     (right, ("scene.lua", "config.lua"))):
                for name in names:
                    (directory / name).write_text(name)
            original = directory_digest(left)
            self.assertEqual(original, directory_digest(right))
            (right / "config.lua").write_text("gpu_stage_timing = false")
            self.assertNotEqual(original, directory_digest(right))
            (right / "config.lua").write_text("config.lua")
            (right / "scene.lua").rename(right / "different.lua")
            self.assertNotEqual(original, directory_digest(right))


class DemoProcessTest(unittest.TestCase):
    def test_selects_only_owned_descendant_with_matching_executable(self):
        table = """10 1 /bin/sh
11 10 /bin/bash
12 11 ./IRPerfGrid
20 1 /bin/sh
21 20 /repo/build/IRPerfGrid
22 10 /repo/build/IRCanvasStress
"""
        self.assertEqual(find_demo_pid(10, Path("/repo/build/IRPerfGrid"), table), 12)
        self.assertEqual(find_demo_pid(20, Path("/repo/build/IRPerfGrid"), table), 21)
        self.assertIsNone(find_demo_pid(30, Path("/repo/build/IRPerfGrid"), table))

    def test_absolute_path_with_spaces(self):
        table = "10 1 /bin/sh\n12 10 /my repo/build/IRPerfGrid\n"
        self.assertEqual(find_demo_pid(10, Path("/my repo/build/IRPerfGrid"), table), 12)

    def test_disappeared_ancestor_or_cycle_is_not_owned(self):
        for table in ("12 11 ./IRPerfGrid", "12 11 ./IRPerfGrid\n11 12 /bin/sh"):
            self.assertIsNone(find_demo_pid(10, Path("/repo/IRPerfGrid"), table))

    def test_wrapper_can_exec_demo_in_place(self):
        self.assertEqual(find_demo_pid(10, Path("/repo/IRPerfGrid"), "10 1 ./IRPerfGrid"), 10)


class SamplingFailureTest(unittest.TestCase):
    def run_sample(self, sample_error=None, process_tables=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = MagicMock(pid=10)
            demo.__enter__.return_value = demo
            demo.poll.return_value = None
            demo.wait.return_value = 0
            table = "10 1 /bin/sh\n12 10 ./IRPerfGrid"

            def sample(command, **kwargs):
                if sample_error is not None:
                    raise sample_error
                Path(command[-1]).write_text("Call graph:\n")
                return subprocess.CompletedProcess(
                    command,
                    0,
                    f"Sample analysis written to {command[-1]} from {root / 'build' / 'demo'}",
                    "",
                )

            with (
                patch("repeat_profile.subprocess.Popen", return_value=demo),
                patch(
                    "repeat_profile.subprocess.check_output",
                    side_effect=process_tables or [table, table],
                ),
                patch("repeat_profile.subprocess.run", side_effect=sample),
            ):
                result, sampling = run_profile(
                    ["fleet-run"], root, root / "run-1.log", Path("/repo/IRPerfGrid"), 1, 0
                )
            self.assertEqual(result, 0)
            sample_log = (root / "run-1.sample.log").read_text()
            return sampling, sample_log, root

    def test_target_exit_while_wrapper_alive_rejects_partial_sample(self):
        result, _, _ = self.run_sample(
            process_tables=["10 1 /bin/sh\n12 10 ./IRPerfGrid", "10 1 /bin/sh"]
        )
        self.assertFalse(result["complete"])
        self.assertFalse(result["target_alive_after_sample"])

    def test_timeout_and_launch_failure_are_retained(self):
        for error in (
            subprocess.TimeoutExpired("sample", 31),
            OSError("denied"),
        ):
            with self.subTest(error=error):
                result, _, _ = self.run_sample(sample_error=error)
                self.assertFalse(result["complete"])
                self.assertIn("error", result)

    def test_process_table_failure_is_retained(self):
        result, _, _ = self.run_sample(process_tables=[OSError("ps denied")])
        self.assertFalse(result["complete"])
        self.assertIn("ps denied", result["error"])

    def test_success_requires_trace_and_live_owned_target(self):
        result, sample_log, root = self.run_sample()
        self.assertTrue(result["complete"])
        self.assertEqual(result["command"][-1], "run-1.sample.txt")
        self.assertNotIn(str(root), result["command"])
        self.assertNotIn(str(root), sample_log)
        self.assertIn("run-1.sample.txt", sample_log)
        self.assertIn("<repo>/build/demo", sample_log)


if __name__ == "__main__":
    unittest.main()
