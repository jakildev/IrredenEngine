"""Only complete interleaved pairs may enter a lane's statistics."""

import contextlib
import io
import json
import statistics
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import summarize_pairs  # noqa: E402

REPORT = """Frame time:   avg={frame:.2f}ms   p50=1.00ms
Steady frame time (first 75 of 300 frames excluded):   avg={steady:.2f}ms   p50=1.00ms

--- GPU stage timing ---
stage                                    avg       min       max       n
voxelCompact                             {compact:.3f}     0.036     0.735     299
voxelStage1                              {stage1:.3f}     0.189     1.875     299

--- CPU phase timing ---
phase                                    avg       max       n
voxelStage1                              1.400     4.642     300

--- Run witness ---
Camera yaw: first=0.000deg last=0.000deg travel=0.000deg samples=300
Per-axis overflow: maxEntries=0 maxDropped=0 cap=0 samples=0

--- Frame times (ms, in order) ---
1.0
"""


def write_arm(root, arm, lane, pair, compact, clean=True, frame=10.0, stage1=0.5):
    arm_dir = root / f"{arm}-{lane}-{pair}"
    arm_dir.mkdir()
    (arm_dir / "run-1.txt").write_text(
        REPORT.format(frame=frame, steady=frame, compact=compact, stage1=stage1))
    run = {"clean": clean, "quiet": "guarded" if clean else "breach",
           "exit_code": 0 if clean else 76}
    (arm_dir / "manifest.json").write_text(json.dumps({"runs": [run]}))


def summarize(root):
    out = io.StringIO()
    with patch.object(sys, "argv", ["summarize_pairs.py", str(root)]), \
            contextlib.redirect_stdout(out):
        code = summarize_pairs.main()
    comparison = json.loads((root / "comparison.json").read_text())
    return code, out.getvalue(), comparison


def row(comparison, lane, metric):
    return next(r for r in comparison[lane] if r["metric"] == metric)


class SummarizePairsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def test_unmatched_arms_enter_no_statistic(self):
        # Base pair 1 is contaminated and head pair 7 has no base arm; the
        # outlier values on both unmatched arms would move every mean.
        write_arm(self.root, "base", "identity", 1, compact=9.0, clean=False)
        write_arm(self.root, "head", "identity", 1, compact=0.40)
        for pair in range(2, 7):
            write_arm(self.root, "base", "identity", pair, compact=0.30 + pair / 1000)
            write_arm(self.root, "head", "identity", pair, compact=0.31 + pair / 1000)
        write_arm(self.root, "head", "identity", 7, compact=9.0)

        code, out, comparison = summarize(self.root)

        self.assertEqual(code, 0)
        compact = row(comparison, "identity", "GPU voxelCompact")
        self.assertEqual(compact["pairs"], [2, 3, 4, 5, 6])
        self.assertEqual(compact["n"], 5)
        self.assertTrue(compact["pairs_sufficient"])
        expected_base = [0.30 + p / 1000 for p in range(2, 7)]
        expected_head = [0.31 + p / 1000 for p in range(2, 7)]
        self.assertEqual(compact["base_runs"], expected_base)
        self.assertEqual(compact["head_runs"], expected_head)
        self.assertAlmostEqual(compact["base_ms"], statistics.fmean(expected_base))
        self.assertAlmostEqual(compact["head_ms"], statistics.fmean(expected_head))
        self.assertAlmostEqual(compact["delta_us"], statistics.fmean(compact["pair_deltas_us"]))
        self.assertEqual(len(compact["pair_deltas_us"]), 5)
        self.assertIn("excluded base-identity-1:", out)
        self.assertIn("unmatched head-identity-1: no clean partner arm", out)
        self.assertIn("unmatched head-identity-7: no clean partner arm", out)
        self.assertIn("5 complete pairs from 5 base / 7 head clean arms", out)

    def test_fewer_than_five_complete_pairs_fails_without_the_bound(self):
        # Four complete pairs whose compact row fails (i) with a paired
        # spread that would pass (ii) if the bound were available.
        for pair, delta in zip(range(1, 5), (0.06, -0.02, 0.06, -0.02)):
            write_arm(self.root, "base", "identity", pair, compact=0.30)
            write_arm(self.root, "head", "identity", pair, compact=0.30 + delta)
        write_arm(self.root, "base", "identity", 5, compact=0.30)
        write_arm(self.root, "head", "identity", 5, compact=0.30, clean=False)

        code, out, comparison = summarize(self.root)

        self.assertEqual(code, 1)
        compact = row(comparison, "identity", "GPU voxelCompact")
        self.assertEqual(compact["n"], 4)
        self.assertFalse(compact["pairs_sufficient"])
        self.assertIsNone(compact["sd_us"])
        self.assertIsNone(compact["lower_bound_us"])
        self.assertFalse(compact["pass_written"])
        self.assertFalse(compact["pass_bound"])
        self.assertFalse(compact["pass"])
        frame = row(comparison, "identity", "frame")
        self.assertTrue(frame["pass_written"])
        self.assertFalse(frame["pass"])
        self.assertIn("INSUFFICIENT: 4 complete pairs, below the minimum of 5", out)

    def test_five_complete_pairs_grade_a_noisy_row_by_the_bound(self):
        for pair, delta in zip(range(1, 6), (0.06, -0.02, 0.06, -0.02, 0.06)):
            write_arm(self.root, "base", "identity", pair, compact=0.30)
            write_arm(self.root, "head", "identity", pair, compact=0.30 + delta)

        code, out, comparison = summarize(self.root)

        self.assertEqual(code, 0)
        compact = row(comparison, "identity", "GPU voxelCompact")
        self.assertEqual(compact["n"], 5)
        self.assertFalse(compact["pass_written"])
        self.assertTrue(compact["pass_bound"])
        self.assertTrue(compact["pass"])
        deltas = [d * 1000 for d in (0.06, -0.02, 0.06, -0.02, 0.06)]
        expected_bound = (statistics.fmean(deltas)
                          - 2.132 * statistics.stdev(deltas) / 5 ** 0.5)
        self.assertAlmostEqual(compact["lower_bound_us"], expected_bound, places=6)
        self.assertIn("yes (bound)", out)

    def test_a_lane_with_no_complete_pair_fails(self):
        write_arm(self.root, "base", "identity", 1, compact=0.30)
        write_arm(self.root, "head", "identity", 2, compact=0.30)

        code, out, comparison = summarize(self.root)

        self.assertEqual(code, 1)
        self.assertEqual(comparison, {})
        self.assertIn("identity: no complete pairs (1 base, 1 head)", out)


if __name__ == "__main__":
    unittest.main()
