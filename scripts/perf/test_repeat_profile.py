"""Sampling must never attach to a different fleet run."""

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from compare_perf_runs import RunWitness
from repeat_profile import (
    cmake_build_type,
    directory_digest,
    engine_logged,
    find_demo_pid,
    host_battery_percent,
    host_load,
    host_power_source,
    overflow_failure,
    percentile,
    requested_yaw,
    requested_yaw_step,
    run_profile,
    witness_checks,
    yaw_pose_mismatch,
)


def witnessed(yaw=45.0, last=None, travel=0.0, samples=300, dropped=0, overflow_samples=299):
    return RunWitness(
        yaw_first_deg=yaw,
        yaw_last_deg=yaw if last is None else last,
        yaw_travel_deg=travel,
        pose_samples=samples,
        zoom_first=4.0,
        zoom_last=4.0,
        overflow_max_entries=630842,
        overflow_max_dropped=dropped,
        overflow_cap=1048576,
        overflow_samples=overflow_samples,
    )


class YawPoseTest(unittest.TestCase):
    def mismatch(self, demo_args, witness, target="IRPerfGrid"):
        return yaw_pose_mismatch(target, demo_args, witness)

    def test_radians_flag_must_match_the_witnessed_degrees(self):
        self.assertIsNone(self.mismatch(["--yaw", "0.785398163"], witnessed(45.0)))
        self.assertIsNone(self.mismatch(["--yaw=90"], witnessed(116.620)))
        self.assertIsNone(self.mismatch(["--yaw", "-0.1"], witnessed(-5.730)))

    def test_a_degrees_reading_of_the_flag_fails(self):
        reason = self.mismatch(["--yaw", "0.785398163"], witnessed(0.785))
        self.assertIn("45.000 deg", reason)
        self.assertIn("first rendered frame should be at 45.000 deg", reason)
        self.assertIn("it was at 0.785 deg", reason)

    def test_the_last_yaw_wins_and_a_trailing_flag_is_ignored(self):
        self.assertEqual(requested_yaw(["--yaw", "1", "--yaw=2"]), 2.0)
        self.assertIsNone(requested_yaw(["--zoom", "4", "--yaw"]))
        self.assertIsNone(requested_yaw(["--yaw-ramp"]))

    def test_a_value_the_check_cannot_compare_is_refused_not_passed(self):
        for value in ("nan", "inf", "45deg", ""):
            with self.subTest(value=value), self.assertRaises(ValueError):
                requested_yaw([f"--yaw={value}"])

    def test_the_pi_wrap_compares_on_the_circle(self):
        for reported in (180.006, -179.994):
            self.assertIsNone(self.mismatch(["--yaw", "-3.14149"], witnessed(reported)))

    def test_a_pose_that_did_not_hold_fails(self):
        for reason, witness in (
            ("last rendered frame", witnessed(0.0, last=3.0)),
            ("yawed 720.000 deg", witnessed(0.0, travel=720.0)),
            ("yawed nan", witnessed(0.0, travel=float("nan"))),
        ):
            self.assertIn(reason, self.mismatch(["--yaw", "0"], witness))

    def test_an_absent_witness_fails_only_when_a_static_pose_was_requested(self):
        for absent in (RunWitness(), witnessed(samples=0)):
            self.assertIn("witnessed no camera yaw", self.mismatch(["--yaw", "1"], absent))
        self.assertIsNone(self.mismatch(["--zoom", "4"], RunWitness()))
        swept = witnessed(0.0, last=-93.0, travel=267.0)
        ramp = ["--yaw", "0", "--yaw-ramp"]
        self.assertIsNone(self.mismatch([*ramp, "--auto-screenshot", "4"], swept))
        self.assertIsNone(self.mismatch([*ramp, "--auto-screenshot=4"], swept))
        self.assertIn("last rendered frame", self.mismatch(ramp, swept))
        self.assertIsNone(self.mismatch(["--yaw", "1"], RunWitness(), target="IRCanvasStress"))


class YawSweepTest(unittest.TestCase):
    FULL_TURN = ["--yaw-step", "0.020943951"]

    def mismatch(self, demo_args, **witness):
        return yaw_pose_mismatch("IRPerfGrid", demo_args, witnessed(**witness))

    def test_frame_n_renders_at_yaw_plus_n_minus_one_steps(self):
        self.assertEqual(requested_yaw_step(self.FULL_TURN), 0.020943951)
        self.assertEqual(requested_yaw_step(["--yaw", "1"]), 0.0)
        self.assertIsNone(self.mismatch(self.FULL_TURN, yaw=0.0, last=-1.2, travel=358.8))
        self.assertIsNone(self.mismatch(self.FULL_TURN, yaw=0.0, last=358.8, travel=358.8))
        offset = ["--yaw", "0.010471976", *self.FULL_TURN]
        self.assertIsNone(self.mismatch(offset, yaw=0.6, last=-0.6, travel=358.8))
        backwards = ["--yaw-step=-0.020943951"]
        self.assertIsNone(self.mismatch(backwards, yaw=0.0, last=1.2, travel=358.8))

    def test_a_sweep_that_stalled_or_ran_a_different_window_fails(self):
        stalled = self.mismatch(self.FULL_TURN, yaw=0.0, last=-1.2, travel=100.0)
        self.assertIn("yawed 100.000 deg over 300 frames", stalled)
        self.assertIn("is 358.800 deg", stalled)
        longer = self.mismatch(self.FULL_TURN, yaw=0.0, last=-1.2, travel=358.8, samples=301)
        self.assertIn("last rendered frame should be at", longer)
        self.assertIn("301 frames); it was at -1.200 deg", longer)
        unmoved = self.mismatch(self.FULL_TURN, yaw=0.0, last=0.0, travel=0.0)
        self.assertIn("last rendered frame should be at 358.800 deg", unmoved)

    def test_a_sweep_must_have_sampled_the_overflow_lane(self):
        idle = witnessed(yaw=0.0, last=-1.2, travel=358.8, overflow_samples=0)
        self.assertIn("never sampled", overflow_failure("IRPerfGrid", self.FULL_TURN, idle))

    def test_a_first_frame_pose_is_checked_and_its_jump_counts_as_travel(self):
        held = ["--yaw", "0.816814", "--yaw-first-frame=0"]
        self.assertIsNone(self.mismatch(held, yaw=0.0, last=46.8, travel=46.8, samples=75))
        same = ["--yaw", "0.816814", "--yaw-first-frame", "0.816814"]
        self.assertIsNone(self.mismatch(same, yaw=46.8, last=46.8, travel=0.0, samples=75))
        ignored = self.mismatch(held, yaw=46.8, last=46.8, travel=0.0, samples=75)
        self.assertIn("first rendered frame should be at 0.000 deg", ignored)
        swept = ["--yaw", "0", "--yaw-step", "0.020943951", "--yaw-first-frame=1.5707963"]
        # 90 degrees, then 1.2, 2.4, 3.6: 88.8 across the jump and 1.2 twice after it.
        self.assertIsNone(self.mismatch(swept, yaw=90.0, last=3.6, travel=91.2, samples=4))
        short = self.mismatch(swept, yaw=90.0, last=3.6, travel=3.6, samples=4)
        self.assertIn("yawed 3.600 deg", short)

    def test_a_held_rotated_pose_must_have_sampled_the_overflow_lane(self):
        held = ["--yaw", "0.816814", "--yaw-first-frame=0"]
        idle = witnessed(yaw=0.0, last=46.8, travel=46.8, overflow_samples=0)
        self.assertIn("never sampled", overflow_failure("IRPerfGrid", held, idle))
        cardinal = ["--yaw", "0", "--yaw-first-frame=0"]
        self.assertIsNone(overflow_failure("IRPerfGrid", cardinal, witnessed(overflow_samples=0)))

    def test_a_step_the_check_cannot_compare_is_refused(self):
        with self.assertRaises(ValueError):
            requested_yaw_step(["--yaw-step", "nan"])


class OverflowWitnessTest(unittest.TestCase):
    ROTATED = ["--yaw", "0.785398163"]

    def test_a_dropping_run_is_refused_with_its_count_and_cap(self):
        reason = overflow_failure("IRPerfGrid", self.ROTATED, witnessed(dropped=9))
        self.assertIn("dropped up to 9 entries", reason)
        self.assertIn("cap 1048576", reason)

    def test_an_absent_counter_is_a_failure_never_a_zero(self):
        for target in ("IRPerfGrid", "IRCanvasStress"):
            self.assertIn("witnessed no per-axis", overflow_failure(target, [], RunWitness()))

    def test_a_rotated_pose_must_have_sampled_the_lane_and_a_cardinal_need_not(self):
        idle = witnessed(overflow_samples=0)
        self.assertIn("never sampled", overflow_failure("IRPerfGrid", self.ROTATED, idle))
        self.assertIn("never sampled", overflow_failure("IRPerfGrid", ["--yaw", "-0.5"], idle))
        for cardinal in ("0", "1.5707963", "-1.5707963", "3.14159265"):
            self.assertIsNone(overflow_failure("IRPerfGrid", ["--yaw", cardinal], idle))
        self.assertIsNone(overflow_failure("IRPerfGrid", self.ROTATED, witnessed()))

    def test_the_manifest_records_what_was_witnessed(self):
        checks = witness_checks("IRPerfGrid", self.ROTATED, witnessed(dropped=9))
        self.assertIsNone(checks["yaw_pose_mismatch"])
        self.assertIn("dropped", checks["overflow_failure"])
        self.assertEqual(
            (checks["yaw_first_deg"], checks["overflow_max_dropped"], checks["overflow_samples"]),
            (45.0, 9, 299),
        )


class EngineLoggedTest(unittest.TestCase):
    def test_a_build_with_logging_compiled_out_is_told_apart(self):
        self.assertTrue(engine_logged("[EngineLog] [info] Clean shutdown complete.\n"))
        self.assertTrue(engine_logged("[ClientLog] [info] Auto-profile\n"))
        self.assertFalse(engine_logged("ir-run: RESULT=CLEAN exe=IRPerfGrid exit=0\n"))


class PercentileTest(unittest.TestCase):
    def test_matches_the_report_writers_index_rule(self):
        frames = [float(value) for value in range(1, 226)]
        self.assertEqual(percentile(frames, 99), 223.0)
        self.assertEqual(percentile(frames, 95), 214.0)
        self.assertEqual(percentile([7.0], 99), 7.0)
        self.assertEqual(percentile(list(reversed(frames)), 99), 223.0)


class HostPowerTest(unittest.TestCase):
    def read(self, system, output=None, error=None):
        with (
            patch("repeat_profile.platform.system", return_value=system),
            patch("repeat_profile.subprocess.check_output", return_value=output, side_effect=error),
        ):
            return host_power_source()

    def test_reads_the_source_pmset_names(self):
        battery = "Now drawing from 'Battery Power'\n -InternalBattery-0\t91%; discharging\n"
        self.assertEqual(self.read("Darwin", battery), "Battery Power")
        self.assertEqual(self.read("Darwin", "Now drawing from 'AC Power'\n"), "AC Power")

    def test_battery_charge_is_read_beside_the_source(self):
        reports = {
            "Now drawing from 'Battery Power'\n -InternalBattery-0 (id=1)\t91%; discharging\n": 91,
            "Now drawing from 'AC Power'\n": None,
        }
        for report, charge in reports.items():
            with (
                patch("repeat_profile.platform.system", return_value="Darwin"),
                patch("repeat_profile.subprocess.check_output", return_value=report),
            ):
                self.assertEqual(host_battery_percent(), charge)

    def test_load_is_a_number_or_none_where_the_platform_has_none(self):
        with patch("repeat_profile.os.getloadavg", return_value=(19.414, 13.0, 9.0)):
            self.assertEqual(host_load(), 19.41)
        with patch("repeat_profile.os.getloadavg", side_effect=OSError("unobtainable")):
            self.assertIsNone(host_load())

    def test_unknown_is_none_never_a_guess(self):
        self.assertIsNone(self.read("Linux", "Now drawing from 'AC Power'\n"))
        self.assertIsNone(self.read("Darwin", "unexpected"))
        self.assertIsNone(self.read("Darwin", error=OSError("no pmset")))


class BuildTreeTest(unittest.TestCase):
    def test_build_type_comes_from_the_cache_and_is_none_when_unknown(self):
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            self.assertIsNone(cmake_build_type(tree))
            cache = tree / "CMakeCache.txt"
            cache.write_text("CMAKE_BUILD_TYPE_INIT:STRING=Debug\nCMAKE_BUILD_TYPE:STRING=Release\n")
            self.assertEqual(cmake_build_type(tree), "Release")
            cache.write_text("CMAKE_BUILD_TYPE:STRING=\n")
            self.assertIsNone(cmake_build_type(tree))


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
