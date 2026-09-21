"""The million matrix must interleave honestly and refuse a moved fingerprint."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from million_controls import PRESETS, cases, summarize, verify_artifacts
from rotation_controls import run_rounds

REPORT = (
    "Frame time:   avg={avg:.2f}ms   p50=1.00ms   p95=2.00ms   p99={p99:.2f}ms   "
    "min=0.50ms   max=9.00ms\n"
    "{steady}"
    "Update ticks: avg=3.0/frame  max=5\n"
    "Entity count: {entities} (42 archetypes)\n"
    "--- GPU frame timing ---\n"
    "Coverage: supported=1 attempted=10 valid=10 invalid=0 commandBuffers=10\n"
    "envelope {envelope:.3f} 1.000 9.000 10\n"
    "{stages}"
    "--- Voxel cull stats ---\n"
    "Visible {visible:.1f} 9 12\nTotal 1000000.0 1000000 12\nAxisEntries 0.0 0 12\n"
    "{witness}"
)
STEADY = (
    "Steady frame time (first 1 of 4 frames excluded):   avg=1.00ms   p50=1.00ms   "
    "p95=2.00ms   p99=2.00ms   min=0.50ms   max=2.00ms\n"
)
WITNESS = (
    "--- Run witness ---\n"
    "Camera yaw: first={yaw:.3f}deg last={last:.3f}deg travel={travel:.3f}deg samples=4\n"
    "Camera zoom: first=4.000 last=4.000\n"
    "Per-axis overflow: maxEntries=500 maxDropped={drops} cap=8388608 samples={lane}\n"
    "--- Frame times (ms, in order) ---\n"
    "90.000 {avg:.3f} {avg:.3f} {tail:.3f}\n"
)
STAGES = "--- GPU stage timing ---\nvoxelStage1 1.000 0.500 2.000 8\n"


def write_round(
    output,
    name,
    index,
    avg,
    drops=0,
    binary="b",
    power="AC Power",
    envelope=5.0,
    visible=7.0,
    entities=1000177,
    stages=None,
    logged=None,
    yaw=None,
    travel=None,
    witnessed=True,
    steady=True,
):
    """One completed round; by default a well-formed run of the case the name says."""
    release = name.startswith("release-")
    # Four frames of the matrix's sweep: 0.6 degrees, then 1.2 degrees a frame.
    sweeping = name.endswith("-yawsweep")
    pose = 0.6 if sweeping else float(name.rsplit("-yaw", 1)[1])
    first = pose if yaw is None else yaw
    swept = 3.6 if sweeping else 0.0
    witness = WITNESS.format(
        yaw=first,
        last=first + swept,
        travel=swept if travel is None else travel,
        drops=drops,
        lane=3 if pose else 0,
        avg=avg,
        tail=avg + 2,
    )
    if stages is None:
        stages = "-profiling-on-" in name
    directory = output / name / f"round-{index}"
    directory.mkdir(parents=True)
    (directory / "run-1.txt").write_text(
        REPORT.format(
            avg=avg,
            p99=avg + 4,
            envelope=envelope,
            visible=visible,
            entities=entities,
            stages=STAGES if stages else "",
            witness=witness if witnessed else "",
            steady=STEADY if steady else "",
        )
    )
    (directory / "manifest.json").write_text(
        json.dumps(
            {
                "head": "0123456789abcdef",
                "build_type": "Release" if release else "Debug",
                "binary_sha256": binary,
                "shader_sha256": "s",
                "runtime_scripts_sha256": "r",
                "host_power": power,
                "host_cpus": 14,
                "runs": [
                    {
                        "index": 1,
                        "host_battery_percent": 90 - index,
                        "host_load_1m": 1.5 * index,
                        "engine_logged": (not release) if logged is None else logged,
                    }
                ],
            }
        )
    )


class CasesTest(unittest.TestCase):
    def test_every_build_gets_both_presets_at_every_pose_in_radians(self):
        selected = cases(["debug", "release"])
        self.assertEqual(len(selected), 12)
        self.assertEqual(
            selected["debug-profiling-on-yawsweep"][2:],
            ["--yaw", "0.010471976", "--yaw-step", "0.020943951"],
        )
        self.assertEqual(
            selected["release-profiling-off-yaw45"],
            ["--config-preset", PRESETS["off"], "--yaw", "0.785398163"],
        )
        self.assertEqual(selected["debug-profiling-on-yaw0"][-1], "0")


class SummaryTest(unittest.TestCase):
    def test_round_means_keep_drift_visible_and_the_worst_drop_is_reported(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            name = "debug-profiling-on-yaw45"
            for index, avg in ((1, 50.0), (2, 56.0), (10, 62.0)):
                write_round(output, name, index, avg, drops=7 * int(index == 2))
            summarize(output, {name: [], "debug-profiling-on-yaw0": []})
            text = (output / "summary.md").read_text()
            self.assertIn(
                "Power source: AC Power (battery 89% to 80%). "
                "Host load at run start 1.5 to 15.0 on 14 CPUs. Head: 012345678.",
                text.splitlines()[0],
            )
            row = text.splitlines()[4]
            self.assertIn("56.00 (50.00–62.00)", row)
            self.assertIn("50.00 / 56.00 / 62.00", row)
            self.assertIn("| 3.0 (3.0–3.0) | 45.000 | 500 / 7 |", row)
            self.assertEqual(len(text.splitlines()), 5)

    def test_the_tail_is_pooled_over_steady_frames_not_taken_from_startup(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            name = "release-profiling-off-yaw0"
            for index, avg in ((1, 30.0), (2, 40.0)):
                write_round(output, name, index, avg)
            summarize(output, {name: []})
            row = (output / "summary.md").read_text().splitlines()[4]
            # Six steady frames: 30, 30, 32, 40, 40, 42; the 90 ms first frames are excluded.
            self.assertIn("| 35.67 / 42.00 / 42.00 (6) | 34.00–44.00 |", row)

    def test_a_report_that_states_no_warm_up_pools_nothing(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            name = "release-profiling-off-yaw0"
            write_round(output, name, 1, 30.0, steady=False)
            summarize(output, {name: []})
            self.assertIn("| 30.00 | — | 34.00–34.00 |", (output / "summary.md").read_text())


class FingerprintTest(unittest.TestCase):
    def verify(self, **second):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            write_round(output, "debug-profiling-on-yaw0", 1, 30.0)
            write_round(output, "debug-profiling-on-yaw0", 2, 30.0, **second)
            write_round(output, "release-profiling-on-yaw0", 1, 30.0, binary="other tree")
            verify_artifacts(output, ["debug", "release"])

    def test_two_trees_may_differ_but_one_tree_may_not_change(self):
        self.verify()
        with self.assertRaisesRegex(ValueError, "binary changed"):
            self.verify(binary="rebuilt")

    def test_a_power_source_change_mid_matrix_is_refused(self):
        with self.assertRaisesRegex(ValueError, "power source"):
            self.verify(power="Battery Power")

    def test_builds_must_cull_one_pose_to_the_same_counts(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            write_round(output, "debug-profiling-on-yaw45", 1, 30.0)
            write_round(output, "release-profiling-on-yaw45", 1, 30.0, binary="r")
            write_round(output, "release-profiling-on-yaw0", 1, 30.0, binary="r", visible=3.0)
            verify_artifacts(output, ["debug", "release"])
            write_round(output, "release-profiling-on-yaw45", 2, 30.0, binary="r", visible=8.0)
            with self.assertRaisesRegex(ValueError, "yaw45: builds disagree"):
                verify_artifacts(output, ["debug", "release"])

    def test_a_case_that_is_not_the_scene_its_name_says_is_refused(self):
        faults = {
            "preset did not load": {"entities": 262321},
            "GPU stage rows present": {"name": "debug-profiling-off-yaw0", "stages": True},
            "GPU stage rows missing": {"stages": False},
            "a Debug build logged=False": {"logged": False},
            "a Release build logged=True": {"name": "release-profiling-on-yaw0", "logged": True},
            "it was at 0.785 deg": {
                "name": "release-profiling-off-yaw45", "yaw": 0.785,
            },
            "dropped up to 3 entries": {"name": "release-profiling-off-yaw45", "drops": 3},
            "witnessed no camera yaw": {"name": "release-profiling-off-yaw45", "witnessed": False},
            "yawed 1.200 deg over 4 frames": {
                "name": "release-profiling-off-yawsweep", "travel": 1.2,
            },
        }
        for message, fault in faults.items():
            with self.subTest(message=message), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary)
                name = fault.pop("name", "debug-profiling-on-yaw0")
                write_round(output, name, 1, 30.0, **fault)
                with self.assertRaisesRegex(ValueError, message):
                    verify_artifacts(output, ["debug", "release"])

    def test_a_sweep_arm_is_verified_from_its_travelled_arc(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            write_round(output, "release-profiling-off-yawsweep", 1, 40.0, binary="r")
            verify_artifacts(output, ["debug", "release"])
            summarize(output, {"release-profiling-off-yawsweep": []})
            self.assertIn("| 0.600 +3.6 | 500 / 0 |", (output / "summary.md").read_text())

    def test_an_unwitnessed_run_is_never_summarised_as_zero_drops(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            write_round(output, "release-profiling-on-yaw0", 1, 30.0, witnessed=False)
            summarize(output, {"release-profiling-on-yaw0": []})
            self.assertIn("| unwitnessed | unwitnessed |", (output / "summary.md").read_text())


class RoundOrderTest(unittest.TestCase):
    def test_rounds_alternate_and_each_case_gets_its_tree_and_runner_options(self):
        calls = []
        selected = {"a": ["--yaw", "0"], "b": ["--yaw", "1"]}
        environments = {"b": {"IRREDEN_BUILD_DIR": "/release"}}
        with (
            tempfile.TemporaryDirectory() as temporary,
            patch(
                "rotation_controls.subprocess.run",
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
