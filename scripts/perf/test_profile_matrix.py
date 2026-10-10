"""Validate shared matrix ordering and completeness of profiling evidence."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from compare_perf_runs import CellReport, FrameTiming, GpuStage, RunWitness
from profile_matrix import run_rounds, summarize, verify_artifacts, write_gpu_summary


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


class BudgetSummaryTest(unittest.TestCase):
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
            with patch("profile_matrix.parse_report", return_value=report):
                summarize(root, {"control": []})
            summary = (root / "budget-summary.md").read_text()
            self.assertIn("2 / 4 | 123 / 0", summary)
            self.assertNotIn("200.000", summary)
            missing = CellReport("control")
            with patch("profile_matrix.parse_report", side_effect=[report, missing]):
                summarize(root, {"control": []})
            self.assertIn("unwitnessed | unwitnessed | unwitnessed",
                          (root / "budget-summary.md").read_text())


class RoundOrderTest(unittest.TestCase):
    def test_rounds_alternate_and_each_case_gets_its_tree_and_runner_options(self):
        calls = []
        selected = {"a": ["--yaw", "0"], "b": ["--yaw", "1"]}
        environments = {"b": {"IRREDEN_BUILD_DIR": "/release"}}
        with (
            tempfile.TemporaryDirectory() as temporary,
            patch(
                "profile_matrix.subprocess.run",
                side_effect=lambda *a, **k: calls.append((a, k)),
            ),
        ):
            run_rounds(
                Path(temporary), selected, ["--freeze"], 2, lambda: None, environments,
                ["--timeout", "900"],
            )
        names = [Path(args[0][args[0].index("--output") + 1]).parent.name for args, _ in calls]
        self.assertEqual(names, ["a", "b", "b", "a"])
        command = calls[1][0][0]
        self.assertLess(command.index("--timeout"), command.index("--"))
        self.assertEqual(command[command.index("--") + 1:], ["--freeze", "--yaw", "1"])
        self.assertEqual([kwargs["env"] for _, kwargs in calls][:2], [None, environments["b"]])
        self.assertTrue(all(kwargs["check"] for _, kwargs in calls))


if __name__ == "__main__":
    unittest.main()
