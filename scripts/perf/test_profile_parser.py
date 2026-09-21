#!/usr/bin/env python3
"""Regression coverage for legacy and sampled GPU report columns."""

import tempfile
import unittest
from pathlib import Path

from compare_perf_runs import CellReport, parse_report, render_markdown


class GpuReportParserTest(unittest.TestCase):
    def parse(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.txt"
            path.write_text("--- GPU stage timing ---\n" + rows + "\n=== END REPORT ===\n")
            return parse_report(path, "fixture").gpu_stages

    def test_sampled_columns_preserve_min_max_and_count(self):
        rows = self.parse(
            "Stage Avg(ms) Min(ms) Max(ms) Samples\n"
            "voxelSunFaces 1.250 0.500 3.750 299\n"
            "computeSunShadow 0.000 0.000 0.000 299"
        )
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0].name, "voxelSunFaces")
        self.assertEqual((rows[0].avg_ms, rows[0].min_ms, rows[0].max_ms), (1.25, 0.5, 3.75))
        self.assertEqual(rows[0].samples, 299)
        self.assertEqual(rows[1].avg_ms, 0.0)

    def test_legacy_columns_do_not_invent_samples(self):
        rows = self.parse("Stage Avg(ms) Max(ms)\nvoxelStage1 1.250 3.750")
        self.assertEqual(len(rows), 1)
        self.assertEqual((rows[0].avg_ms, rows[0].max_ms), (1.25, 3.75))
        self.assertIsNone(rows[0].min_ms)
        self.assertIsNone(rows[0].samples)

    def test_mixed_cull_units_cannot_produce_a_delta(self):
        before = CellReport("fixture")
        after = CellReport("fixture")
        before.cull.samples = after.cull.samples = 1
        before.cull.ratio = 3.0
        after.cull.ratio = 1.0
        after.cull.avg_axis_entries = 30
        report = render_markdown(
            Path("before"), Path("after"), {"fixture": before},
            {"fixture": after}, 5, 5, False, False
        )
        self.assertIn("incompatible count units", report)
        self.assertNotIn("-200.0pp", report)

    def test_axis_work_is_separate_from_unique_survivors(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.txt"
            path.write_text(
                "--- Voxel cull stats ---\n"
                "Visible 5768.0 5768 12\nTotal 32768.0 32768 12\n"
                "AxisEntries 6144.0 6144 12\n"
            )
            cull = parse_report(path, "fixture").cull
            self.assertEqual(cull.avg_visible, 5768)
            self.assertEqual(cull.avg_axis_entries, 6144)
            self.assertEqual(cull.max_axis_entries, 6144)
            path.write_text("--- Voxel cull stats ---\nVisible 6144.0 6144 12\n")
            self.assertIsNone(parse_report(path, "legacy").cull.avg_axis_entries)

    def test_frame_gpu_is_separate_and_preserves_unavailability(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.txt"
            path.write_text(
                "--- GPU stage timing ---\ncell 1.0 0.5 2.0 8\n"
                "--- GPU frame timing ---\n"
                "Coverage: supported=1 attempted=10 valid=8 invalid=2 commandBuffers=16\n"
                "envelope 8.000 6.000 10.000 8\ncommandBufferSpans 5.000 4.000 7.000 8\n"
            )
            report = parse_report(path, "native")
            self.assertEqual([m.name for m in report.gpu_stages], ["cell"])
            self.assertEqual(report.gpu_frame.metrics[0].avg_ms, 8.0)
            self.assertEqual(report.gpu_frame.metrics[1].avg_ms, 5.0)
            self.assertEqual((report.gpu_frame.valid, report.gpu_frame.invalid,
                              report.gpu_frame.command_buffers), (8, 2, 16))
            path.write_text("--- GPU frame timing ---\n"
                            "Coverage: supported=0 attempted=0 valid=0 invalid=0 "
                            "commandBuffers=0\n")
            unavailable = parse_report(path, "unsupported").gpu_frame
            self.assertIs(unavailable.supported, False)
            self.assertEqual(unavailable.metrics, [])
            path.write_text("--- GPU stage timing ---\n")
            self.assertIsNone(parse_report(path, "legacy").gpu_frame.supported)

    def test_update_ticks_are_read_and_absence_is_not_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.txt"
            path.write_text("Update ticks: avg=3.1/frame  max=7\n")
            report = parse_report(path, "fixture")
            self.assertEqual((report.update_ticks_avg, report.update_ticks_max), (3.1, 7))
            path.write_text("--- GPU stage timing ---\n")
            self.assertIsNone(parse_report(path, "legacy").update_ticks_avg)

    def test_other_sections_cannot_become_gpu_rows(self):
        rows = self.parse(
            "voxelStage1 1.250 0.500 3.750 299\n"
            "--- CPU phase timing ---\nClear 0.004 0.035 300"
        )
        self.assertEqual([row.name for row in rows], ["voxelStage1"])


WITNESSED_REPORT = (
    "=== PROFILE REPORT (8 frames) ===\n"
    "Frame time:   avg=30.00ms   p50=19.00ms   p95=102.00ms   p99=102.00ms   "
    "min=18.00ms   max=102.00ms\n"
    "Steady frame time (first 2 of 8 frames excluded):   avg=19.17ms   p50=19.00ms   "
    "p95=21.00ms   p99=21.00ms   min=18.00ms   max=21.00ms\n"
    "Update ticks: avg=1.2/frame  max=6\n"
    "Entity count: 262321 (42 archetypes)\n"
    "--- GPU stage timing ---\n"
    "voxelStage1 1.000 0.500 2.000 8\n"
    "--- Run witness ---\n"
    "Camera yaw: first=-135.000deg last=-135.000deg travel=0.000deg samples=8\n"
    "Camera zoom: first=4.000 last=4.000\n"
    "Per-axis overflow: maxEntries=630842 maxDropped=7 cap=1048576 samples=8\n"
    "\n"
    "--- Frame times (ms, in order) ---\n"
    "102.000 32.000 21.000 20.000 19.000\n"
    "19.000 18.000 18.000\n"
    "\n"
    "--- Update ticks (per frame, in order) ---\n"
    "8 8 2 1 1\n"
    "1 1 0\n"
    "\n"
    "=== END REPORT ===\n"
)


class RunWitnessParserTest(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.txt"
            path.write_text(text)
            return parse_report(path, "fixture")

    def test_witness_steady_line_and_series_are_read(self):
        report = self.parse(WITNESSED_REPORT)
        witness = report.witness
        self.assertEqual(
            (witness.yaw_first_deg, witness.yaw_last_deg, witness.yaw_travel_deg),
            (-135.0, -135.0, 0.0),
        )
        self.assertEqual(
            (witness.pose_samples, witness.zoom_first, witness.zoom_last), (8, 4.0, 4.0)
        )
        self.assertEqual(
            (witness.overflow_max_entries, witness.overflow_max_dropped,
             witness.overflow_cap, witness.overflow_samples),
            (630842, 7, 1048576, 8),
        )
        self.assertEqual((report.frame.p99, report.steady_frame.p99), (102.0, 21.0))
        self.assertEqual(report.warmup_frames, 2)
        self.assertEqual(len(report.frame_times_ms), 8)
        self.assertEqual(report.frame_update_ticks, [8, 8, 2, 1, 1, 1, 1, 0])
        self.assertEqual(report.steady_frame_times_ms(), [21.0, 20.0, 19.0, 19.0, 18.0, 18.0])
        # The series must not leak into the stage table above it.
        self.assertEqual([stage.name for stage in report.gpu_stages], ["voxelStage1"])

    def test_a_report_without_the_sections_reads_absent_never_zero(self):
        report = self.parse(WITNESSED_REPORT.split("--- Run witness ---")[0].replace(
            WITNESSED_REPORT.splitlines()[2] + "\n", ""
        ))
        self.assertIsNone(report.steady_frame)
        self.assertIsNone(report.witness.yaw_first_deg)
        self.assertIsNone(report.witness.overflow_max_dropped)
        self.assertEqual(report.frame_times_ms, [])

    def test_a_series_without_a_stated_warm_up_has_no_steady_frames(self):
        report = self.parse(WITNESSED_REPORT.replace(WITNESSED_REPORT.splitlines()[2] + "\n", ""))
        self.assertEqual(len(report.frame_times_ms), 8)
        self.assertEqual(report.steady_frame_times_ms(), [])

    def test_the_parsed_lines_are_the_lines_the_engine_writes(self):
        # A reworded report line would read as an absent witness in every run.
        writer = (
            Path(__file__).resolve().parents[2] / "engine/profile/src/profile_report.cpp"
        ).read_text()
        for written in (
            '"--- Run witness ---\\n"',
            '"Camera yaw: first=%.3fdeg last=%.3fdeg travel=%.3fdeg samples=%u\\n"',
            '"Camera zoom: first=%.3f last=%.3f\\n"',
            '"Per-axis overflow: maxEntries=%u maxDropped=%u cap=%u samples=%u\\n"',
            '"Steady frame time (first %zu of %zu frames excluded):   avg=%.2fms   p50=%.2fms   "',
            '"--- Frame times (ms, in order) ---\\n"',
            '"--- Update ticks (per frame, in order) ---\\n"',
        ):
            self.assertIn(written, writer)


if __name__ == "__main__":
    unittest.main()
