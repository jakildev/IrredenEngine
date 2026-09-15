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

    def test_other_sections_cannot_become_gpu_rows(self):
        rows = self.parse(
            "voxelStage1 1.250 0.500 3.750 299\n"
            "--- CPU phase timing ---\nClear 0.004 0.035 300"
        )
        self.assertEqual([row.name for row in rows], ["voxelStage1"])


if __name__ == "__main__":
    unittest.main()
