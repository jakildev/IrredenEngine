"""Sampling must never attach to a different fleet run."""

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from repeat_profile import find_demo_pid, run_profile


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
                return subprocess.CompletedProcess(command, 0, "sampled", "")

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
            self.assertTrue((root / "run-1.sample.log").exists())
            return sampling

    def test_target_exit_while_wrapper_alive_rejects_partial_sample(self):
        result = self.run_sample(
            process_tables=["10 1 /bin/sh\n12 10 ./IRPerfGrid", "10 1 /bin/sh"]
        )
        self.assertFalse(result["complete"])
        self.assertFalse(result["target_alive_after_sample"])

    def test_timeout_and_launch_failure_are_retained(self):
        for error in (subprocess.TimeoutExpired("sample", 31), OSError("denied")):
            with self.subTest(error=error):
                result = self.run_sample(sample_error=error)
                self.assertFalse(result["complete"])
                self.assertIn("error", result)

    def test_process_table_failure_is_retained(self):
        result = self.run_sample(process_tables=[OSError("ps denied")])
        self.assertFalse(result["complete"])
        self.assertIn("ps denied", result["error"])

    def test_success_requires_trace_and_live_owned_target(self):
        self.assertTrue(self.run_sample()["complete"])


if __name__ == "__main__":
    unittest.main()
