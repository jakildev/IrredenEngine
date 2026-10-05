"""Prevent a matrix from silently combining different runtime artifacts."""

import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from compare_perf_runs import CellReport, FrameTiming, GpuStage, RunWitness
from repeat_profile import frame_yaw
from rotation_controls import cases, summarize, verify_artifacts, write_gpu_summary


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


class ArtifactIdentityTest(unittest.TestCase):
    def test_shader_or_binary_change_rejects_matrix(self):
        for changed in ("binary_sha256", "shader_sha256", "runtime_scripts_sha256",
                        "host_power", "render_environment"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = {"binary_sha256": "binary", "shader_sha256": "shaders",
                            "runtime_scripts_sha256": "scripts", "host_power": "AC Power",
                            "render_environment": {"IR_PERAXIS_OVERFLOW_DISABLE": None}}
                for case in ("first", "second"):
                    directory = root / case / "round-1"
                    directory.mkdir(parents=True)
                    (directory / "manifest.json").write_text(json.dumps(manifest))
                verify_artifacts(root)
                manifest[changed] = "different"
                (root / "second/round-1/manifest.json").write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    verify_artifacts(root)

    def test_empty_matrix_is_not_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                verify_artifacts(Path(temporary))


class GpuSummaryTest(unittest.TestCase):
    def test_unsampled_rows_do_not_claim_zero_cost(self):
        reports = [CellReport("test", gpu_stages=[
            GpuStage("sampledZero", 0, 0, samples=10),
            GpuStage("unwired", 0, 0, samples=0),
            GpuStage("partial", 1, 1, samples=count),
            GpuStage("historical", 2, 2),
        ]) for count in (10, 0)]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "gpu-summary.md"
            write_gpu_summary(path, {"test": reports}, "timing semantics")
            text = path.read_text()
            self.assertTrue(text.startswith("timing semantics"))
            self.assertIn("| sampledZero | 0.000 |", text)
            self.assertIn("| historical | 2.000 |", text)
            self.assertNotIn("unwired", text)
            self.assertNotIn("partial", text)


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

    def test_budget_excludes_warmup_and_requires_complete_evidence(self):
        report = CellReport("control", steady_frame=FrameTiming(avg=15),
                            warmup_frames=1, recorded_frames=3,
                            frame_times_ms=[200, 10, 20],
                            witness=RunWitness(overflow_max_entries=123,
                                               overflow_max_dropped=0))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index in (1, 2):
                directory = root / "control" / f"round-{index}"
                directory.mkdir(parents=True)
                (directory / "run-1.txt").touch()
            with patch("rotation_controls.parse_report", return_value=report):
                summarize(root, {"control": []})
            summary = (root / "budget-summary.md").read_text()
            self.assertIn("2 / 4 | 123 / 0", summary)
            self.assertNotIn("200.000", summary)
            missing = CellReport("control")
            with patch("rotation_controls.parse_report", side_effect=[report, missing]):
                summarize(root, {"control": []})
            self.assertIn("unwitnessed | unwitnessed | unwitnessed",
                          (root / "budget-summary.md").read_text())


if __name__ == "__main__":
    unittest.main()
