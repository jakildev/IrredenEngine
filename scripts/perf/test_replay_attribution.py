"""The attribution replay reads perf-gate comments the way the gate wrote them."""

import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from replay_attribution import fires, parse_comment, visible_view

SLUG = "linux-x86_64-epyc-7763-64-unknown"
CAPTURE = "a" * 40


def comment(frame_rows, *, ref_ms=("100.00", "100.00"), capture=True, extra=""):
    lines = [
        "<!-- perf-gate -->",
        "## Perf gate",
        f"# perf comparison: {'b' * 40} → 24eaf5dfd-ci",
        f"baseline: `/home/runner/work/_temp/perf-baseline/history/{SLUG}/{CAPTURE}`",
        f"- ref_ms: head {ref_ms[0]} vs baseline {ref_ms[1]} (load_factor 1.00×)",
    ]
    if capture:
        lines.append("- baseline capture: `perf-baseline@448808c23` (branch tip); "
                     "master `00d0d549d`, finished 2026-10-14T23:32:13Z")
    lines += ["## frame timing (ms)", "", "| cell | avg | p99 |", "|------|-----|-----|",
              *frame_rows, "", "## GPU stage timing", "| `zoom=1` | 1.00 → 0.10 (-90.0%) |",
              extra]
    return {"url": "https://api.github.com/repos/o/r/issues/4450",
            "created": "2026-10-15T00:50:38Z", "body": "\n".join(lines)}


class ParseCommentTest(unittest.TestCase):
    def test_reads_both_row_formats_and_the_load_flag(self):
        row = parse_comment(comment([
            "| `zoom=1` | 571.02 → 588.83 (+3.1%) | 8806.37 → 8823.20 (+0.2%) |",
            "| `zoom=4` (load-flagged) | 1696.39 → 1707.29 (+0.6% raw, -1.0% scaled) | x |",
        ]))
        self.assertEqual(row["pr"], 4450)
        self.assertEqual(row["head"], "24eaf5dfd")
        self.assertEqual((row["slug"], row["capture_commit"]), (SLUG, CAPTURE))
        self.assertEqual(row["capture_master"], "00d0d549d")
        self.assertEqual(row["frames"], [("zoom=1", 571.02, 588.83, False),
                                         ("zoom=4", 1696.39, 1707.29, True)])
        self.assertFalse(row["regressed"])

    def test_a_comment_without_a_class_matched_capture_is_skipped(self):
        self.assertIsNone(parse_comment(comment([], capture=False)))


class FiresTest(unittest.TestCase):
    def test_raw_and_calibrated_improvement_fires(self):
        self.assertTrue(fires(parse_comment(comment(["| `z` | 100.00 → 60.00 (-40.0%) |"])), 5.0))

    def test_a_regression_or_a_load_flagged_cell_does_not_fire(self):
        regressed = comment(["| `z` | 100.00 → 60.00 (-40.0%) |"],
                            extra=":warning: **Regression detected** — at least one cell")
        self.assertFalse(fires(parse_comment(regressed), 5.0))
        flagged = comment(["| `z` (load-flagged) | 100.00 → 60.00 (-40.0%) |"])
        self.assertFalse(fires(parse_comment(flagged), 5.0))

    def test_an_improvement_the_calibration_ratio_explains_does_not_fire(self):
        confounded = comment(["| `z` | 100.00 → 85.00 (-15.0%) |"], ref_ms=("85.00", "100.00"))
        self.assertFalse(fires(parse_comment(confounded), 5.0))


class VisibleViewTest(unittest.TestCase):
    def test_keeps_only_captures_committed_by_the_comment(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            times = {(SLUG, name): datetime(2026, 10, 14, hour, tzinfo=timezone.utc)
                     for name, hour in (("early", 1), ("same", 2), ("late", 3))}
            for slug, commit in times:
                (work / "history" / slug / commit).mkdir(parents=True)
            root = visible_view(times, "2026-10-14T02:00:00Z", work / "history", work / "views")
            self.assertEqual(sorted(p.name for p in (root / SLUG).iterdir()), ["early", "same"])
            self.assertEqual(visible_view(times, "2026-10-14T02:00:00Z", work / "history",
                                          work / "views"), root)


if __name__ == "__main__":
    unittest.main()
