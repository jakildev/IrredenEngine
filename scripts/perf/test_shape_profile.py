"""Fixed SDF comparisons must prove their pose and retain timing inputs."""

import io
import json
import os
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from compare_perf_runs import CellReport, FrameTiming, GpuFrameTiming, GpuStage
from repeat_profile import (
    main,
    shape_configuration,
    shape_measurement_failure,
    shape_profile_arguments,
    witness_checks,
)
from test_profile_parser import WITNESSED_REPORT
from test_repeat_profile import witnessed

ARGS = ["--sdf-voxel-scene", "baked", "--auto-profile", "8", "--yaw", "0.785398163",
        "--zoom", "4", "--config-preset", "configs/perf/sdf-on.lua"]


class ShapeProfileArgumentsTest(unittest.TestCase):
    def test_explicit_fixed_comparison_and_timing_controls(self):
        for representation in ("direct", "baked"):
            for timing in ("on", "off"):
                args = [*ARGS]
                args[1] = representation
                args[-1] = f"configs/perf/sdf-{timing}.lua"
                parsed = shape_profile_arguments([*args, "--sdf-voxel-blockers", "--no-ao",
                                                  "--no-shadows"])
                self.assertEqual(parsed["--sdf-voxel-scene"], representation)

    def test_ambiguous_or_scene_changing_flags_are_rejected(self):
        for extra in (["--auto-screenshot", "5"], ["--auto-record", "30"],
                      ["--spin-yaw", "30"], ["--lod-scene"], ["--yaw", "0"],
                      ["--zoom-continuous"], ["--capture-frame", "3"],
                      ["--window-mode", "visible"], ["--debug-overlay", "normals"]):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                shape_profile_arguments([*ARGS, *extra])

    def test_missing_values_invalid_numbers_and_unmatched_configs_fail(self):
        for flag in ("--auto-profile", "--yaw", "--zoom", "--config-preset",
                     "--sdf-voxel-scene"):
            args = [*ARGS]
            index = args.index(flag)
            del args[index:index + 2]
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                shape_profile_arguments(args)
        for flag, value in (("--auto-profile", "0"), ("--auto-profile", "-1"),
                            ("--auto-profile", "3.5"), ("--yaw", "nan"),
                            ("--yaw", "inf"), ("--zoom", "inf"), ("--zoom", "0"),
                            ("--zoom", "1.6"), ("--sdf-voxel-scene", "off"),
                            ("--config-preset", "unknown.lua")):
            args = [*ARGS]
            args[args.index(flag) + 1] = value
            with self.subTest(flag=flag, value=value), self.assertRaises(ValueError):
                shape_profile_arguments(args)

    def test_config_content_and_preset_selection_are_fingerprinted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "configs/perf").mkdir(parents=True)
            (root / "scripts").mkdir()
            (root / "scripts/config.lua").write_text("base")
            preset = root / "configs/perf/sdf-on.lua"
            preset.write_text("on")
            (root / "configs/perf/sdf-off.lua").write_text("off")
            values = shape_profile_arguments(ARGS)
            before = shape_configuration(root / "IRShapeDebug", values)
            preset.write_text("changed")
            self.assertNotEqual(before, shape_configuration(root / "IRShapeDebug", values))
            values["--config-preset"] = "configs/perf/sdf-off.lua"
            self.assertNotEqual(before, shape_configuration(root / "IRShapeDebug", values))


class ShapeProfileWitnessTest(unittest.TestCase):
    def checks(self, witness):
        return witness_checks("IRShapeDebug", ARGS, witness)

    def test_fixed_pose_passes_without_claiming_explicit_focus(self):
        result = self.checks(witnessed(samples=8, pinned=0))
        for key in ("yaw_pose_mismatch", "pivot_mismatch", "overflow_failure"):
            self.assertIsNone(result[key])

    def test_frame_envelope_does_not_substitute_for_requested_stage_timing(self):
        report = CellReport("fixture", recorded_frames=8, frame_times_ms=[1.0] * 8,
                            steady_frame=FrameTiming(avg=1), warmup_frames=2,
                            gpu_frame=GpuFrameTiming(supported=True, valid=8))
        values = shape_profile_arguments(ARGS)
        self.assertIn("stage samples", shape_measurement_failure(values, report))
        report.gpu_stages = [GpuStage("voxelStage1", 1.0, 1.0, samples=8)]
        self.assertIsNone(shape_measurement_failure(values, report))
        values["--config-preset"] = "configs/perf/sdf-off.lua"
        self.assertIn("stage samples", shape_measurement_failure(values, report))
        report.gpu_stages = []
        self.assertIsNone(shape_measurement_failure(values, report))
        for invalid in (0, -1, float("inf"), float("nan")):
            report.frame_times_ms[0] = invalid
            self.assertIn("nonpositive or nonfinite", shape_measurement_failure(values, report))

    def test_corrupted_pose_count_zoom_or_overflow_is_rejected(self):
        good = witnessed(samples=8, pinned=0)
        cases = (
            ("yaw_pose_mismatch", {"pose_samples": 7}),
            ("yaw_pose_mismatch", {"yaw_first_deg": 0}),
            ("yaw_pose_mismatch", {"yaw_last_deg": 0}),
            ("yaw_pose_mismatch", {"yaw_travel_deg": 360}),
            ("yaw_pose_mismatch", {"yaw_travel_deg": float("nan")}),
            ("yaw_pose_mismatch", {"zoom_min": None}),
            ("yaw_pose_mismatch", {"zoom_max": 8}),
            ("yaw_pose_mismatch", {"zoom_first": 2, "zoom_last": 2,
                                  "zoom_min": 2, "zoom_max": 2}),
            ("pivot_mismatch", {"explicit_pivot_samples": None}),
            ("pivot_mismatch", {"explicit_pivot_samples": 8}),
            ("overflow_failure", {"overflow_samples": 0}),
            ("overflow_failure", {"overflow_samples": None}),
            ("overflow_failure", {"overflow_max_dropped": None}),
            ("overflow_failure", {"overflow_max_dropped": 1}),
        )
        for key, changes in cases:
            with self.subTest(changes=changes):
                self.assertIsNotNone(self.checks(replace(good, **changes))[key])

    def test_runner_refuses_unqualified_reports_and_preserves_evidence(self):
        launched_ns = 1_700_000_000_000_000_000
        for failure in (None, "quiet", "stale", "unclean", "config", "pose", "gpu", "frames"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                demo = root / "build/creations/demos/shape_debug"
                (demo / "save_files").mkdir(parents=True)
                (demo / "shaders").mkdir()
                (demo / "scripts").mkdir()
                (demo / "configs/perf").mkdir(parents=True)
                (demo / "IRShapeDebug").write_bytes(b"fixture executable")
                (demo / "scripts/config.lua").write_text("base")
                preset = demo / "configs/perf/sdf-on.lua"
                preset.write_text("on")
                output = root / "results"

                def capture(command, repo, log, binary, seconds, delay):
                    self.assertEqual(command[-2:], ["--window-mode", "offscreen"])
                    quiet = "unguarded" if failure == "quiet" else "guarded"
                    result = "ERROR" if failure == "unclean" else "CLEAN"
                    log.write_text(f"ir-run: QUIET={quiet}\nRESULT={result}\n")
                    text = WITNESSED_REPORT.replace("-135.000", "45.000")
                    text = text.replace("maxDropped=7", "maxDropped=0")
                    text = text.replace("explicit focus on 8", "explicit focus on 0")
                    if failure == "pose":
                        text = text.replace("travel=0.000", "travel=360.000")
                    if failure == "gpu":
                        text = text.replace("voxelStage1 1.000 0.500 2.000 8\n", "")
                    if failure == "frames":
                        text = text.replace("19.000 18.000 18.000", "19.000 18.000")
                    report = demo / "save_files/profile_report.txt"
                    report.write_text(text)
                    delta = -1 if failure == "stale" else 1
                    os.utime(report, ns=(launched_ns, launched_ns + delta * 1_000_000_000))
                    if failure == "config":
                        preset.write_text("changed during run")
                    return 0, None

                with (
                    patch("repeat_profile.__file__", str(root / "scripts/perf/repeat_profile.py")),
                    patch.dict("os.environ", {"IRREDEN_BUILD_DIR": str(root / "build")}),
                    patch("sys.argv", ["repeat_profile.py", "--target", "IRShapeDebug",
                                       "--output", str(output), "--repeats", "1", "--", *ARGS]),
                    patch("repeat_profile.subprocess.check_output", return_value="fixture"),
                    patch("repeat_profile.platform.platform", return_value="fixture host"),
                    patch("repeat_profile.host_power_source", return_value=None),
                    patch("repeat_profile.host_battery_percent", return_value=None),
                    patch("repeat_profile.time.time_ns", return_value=launched_ns),
                    patch("repeat_profile.run_profile", side_effect=capture),
                    patch("sys.stdout", new_callable=io.StringIO) as stdout,
                ):
                    self.assertEqual(main(), 0 if failure is None else 1, stdout.getvalue())
                self.assertEqual((output / "summary.md").exists(), failure is None)
                manifest = json.loads((output / "manifest.json").read_text())
                self.assertIn("configuration_sha256", manifest)
                self.assertIn("not witnessed", manifest["pivot_policy"])


if __name__ == "__main__":
    unittest.main()
