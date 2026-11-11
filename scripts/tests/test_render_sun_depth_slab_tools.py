import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "perf"))

from sun_depth_slab_census import summarize_probe  # noqa: E402
from sun_depth_slab_timing import BAKE_ROW, verdict  # noqa: E402


class SunDepthSlabToolsTest(unittest.TestCase):
    def summarize(self, mode, rows):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.csv"
            path.write_text(
                f"# slab_mode={mode}\n# slab_slots=3\n"
                "cascade,x,y,nearest,slab0_min,slab0_max,slab1_min,slab1_max,"
                "slab2_min,slab2_max,overflow,collision\n" + rows)
            return summarize_probe(path)["cascades"]["0"]

    def test_associative_census_separates_incomplete_write_sets(self):
        # Slabs 1 and 4 collide modulo three; an overflowed row is incomplete.
        row = "0,0,0,16777216,16777216,16777217,67108864,67108865,4294967295,0,"
        result = self.summarize("associative", row + "0,0\n" + row + "1,0\n")
        self.assertEqual(result["complete_rows"], 1)
        self.assertEqual(result["complete_modulo_collisions"], 1)
        self.assertEqual(result["overflowed_rows"], 1)
        self.assertEqual(result["overflowed_retained_modulo_collisions"], 1)
        self.assertEqual(result["direct_collisions"], 0)

    def test_direct_census_recomputes_collisions_across_sparse_slots(self):
        row = "0,0,0,16777216,4294967295,0,16777216,67108864,4294967295,0,0,"
        result = self.summarize("direct", row + "1\n")
        self.assertEqual(result["direct_collisions"], 1)
        self.assertEqual(result["complete_rows"], 0)
        with self.assertRaisesRegex(ValueError, "disagrees with raw extrema"):
            self.summarize("direct", row + "0\n")

    def test_budget_bound_cannot_excuse_a_frame_regression(self):
        pair = {"mean": 0.07, "lower95": 0.06}
        self.assertEqual(verdict("canvas-lower", BAKE_ROW, 1.12, pair)[1], "pass (ii)")
        self.assertEqual(verdict("canvas-lower", BAKE_ROW, 1.12, pair, False)[1], "FAIL")
        self.assertEqual(verdict("canvas-stacked-128", BAKE_ROW, 1.12, pair)[1], "FAIL")
        self.assertEqual(verdict("canvas-lower", BAKE_ROW, 1.16, pair)[1], "FAIL")


if __name__ == "__main__":
    unittest.main()
