"""The capacity matrix must keep every budget explicit and platform-scoped."""

import copy
import json
import tempfile
import unittest
from pathlib import Path

from capacity_envelope import (
    REQUIRED_BUDGETS,
    load_spec,
    suite_command,
    validate_spec,
    verify_output,
    verify_resolution_configs,
)


class SpecificationTest(unittest.TestCase):
    def test_checked_in_spec_covers_every_required_budget_and_workload(self):
        spec = load_spec()
        self.assertEqual(spec["build_type"], "Release")
        self.assertGreaterEqual(spec["rounds"], 3)
        self.assertEqual({suite["id"] for suite in spec["suites"]}, {
            "million", "rotation", "surface", "private-canvases",
        })
        for suite in spec["suites"]:
            self.assertEqual(REQUIRED_BUDGETS - set(suite["budgets"]), set())

    def test_every_suite_gets_its_own_output_directory(self):
        spec = load_spec()
        root = Path("/engine")
        output = Path("/evidence/macos-metal")
        for suite in spec["suites"]:
            command = suite_command(root, output, suite, spec)
            output_index = command.index("--output")
            self.assertEqual(command[output_index + 1], str(output / suite["id"]))
            if "demo_argv" in suite:
                self.assertLess(output_index, command.index("--"))

    def test_top_level_run_controls_drive_every_suite(self):
        spec = load_spec()
        for suite in spec["suites"]:
            command = suite_command(Path("/engine"), Path("/evidence"), suite, spec)
            if suite["driver"] == "repeat_profile.py":
                self.assertEqual(command[command.index("--repeats") + 1], str(spec["rounds"]))
                self.assertEqual(
                    command[command.index("--profile-frames") + 1], str(spec["frames"])
                )
            else:
                self.assertEqual(command[command.index("--rounds") + 1], str(spec["rounds"]))
                self.assertEqual(command[command.index("--frames") + 1], str(spec["frames"]))

    def test_declared_resolution_matches_target_fixtures(self):
        spec = load_spec()
        root = Path(__file__).resolve().parents[2]
        fingerprints = verify_resolution_configs(root, spec)
        self.assertEqual(len(fingerprints), 2)

    def test_missing_budget_is_rejected(self):
        spec = load_spec()
        damaged = copy.deepcopy(spec)
        del damaged["suites"][0]["budgets"]["visible_occupancy"]
        with self.assertRaisesRegex(ValueError, "missing budgets: visible_occupancy"):
            validate_spec(damaged)

    def test_contaminated_run_is_rejected_before_report_qualification(self):
        spec = load_spec()
        with tempfile.TemporaryDirectory() as temporary:
            suite_output = Path(temporary) / spec["suites"][0]["id"]
            suite_output.mkdir()
            manifest = {
                "build_type": "Release",
                "head": "abc123",
                "host_power": "AC Power",
                "runs": [{
                    "exit_code": 0,
                    "fresh_report": True,
                    "clean": True,
                    "quiet": "unguarded",
                    "measurement_failure": None,
                    "gpu_measured": True,
                }],
            }
            (suite_output / "manifest.json").write_text(json.dumps(manifest))
            (suite_output / "run-1.txt").write_text("")
            with self.assertRaisesRegex(ValueError, "contaminated run: quiet"):
                verify_output(Path(temporary), spec)


if __name__ == "__main__":
    unittest.main()
