"""Validate probe completeness at capacity and reject corrupt tile tables."""

import gzip
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "sun_face_index_probe", ROOT / "scripts/perf/sun_face_index_probe.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class SourceFaceIndexProbeTest(unittest.TestCase):
    def evaluate(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiles.csv"
            path.write_text("# active=1\n# tile_capacity=64\n"
                            "# tiles_per_axis=2\n# cascade_count=1\n"
                            "# face_records_requested=70000\n# face_record_capacity=65536\n"
                            "cascade,x,y,count,complete\n" + rows)
            return PROBE.summarize(path)

    def test_closed_capacity_and_exhausted_record_pool(self):
        result = self.evaluate("0,0,0,0,1\n0,1,0,64,1\n0,0,1,65,0\n0,1,1,70000,0\n")
        self.assertEqual(result["face_records_requested"], 70000)
        self.assertEqual(result["cascades"], [{"occupied_tiles": 3,
                                             "incomplete_tiles": 2, "max_counter": 70000}])

    def test_world_point_on_tile_boundary_and_compressed_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiles.csv.gz"
            with gzip.open(path, "wt") as stream:
                stream.write("# active=1\n# tile_capacity=64\n# tiles_per_axis=2\n"
                             "# cascade_count=1\n# face_records_requested=65\n"
                             "# face_record_capacity=65536\n# basis_u=0,1,0\n"
                             "# basis_v=-1,0,0\n"
                             "cascade,x,y,count,complete,u_min,v_min,u_max,v_max\n"
                             "0,0,0,0,1,0,0,1,1\n0,1,0,64,1,1,0,2,1\n"
                             "0,0,1,65,0,0,1,1,2\n0,1,1,1,1,1,1,2,2\n")
            result = PROBE.summarize(path, (-0.5, 1, 0))
            self.assertEqual(result["sun_uv"], [1, 0.5])
            self.assertEqual(result["point_tiles"], [{"cascade": 0, "x": 1, "y": 0,
                                                     "counter": 64, "complete": True}])

    def test_inactive_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiles.csv"
            path.write_text("# active=0\n")
            with self.assertRaisesRegex(ValueError, "inactive"):
                PROBE.summarize(path)

    def test_capacity_boundary_misclassified(self):
        with self.assertRaisesRegex(ValueError, "completeness"):
            self.evaluate("0,0,0,64,0\n")

    def test_duplicate_missing_and_out_of_range_tiles(self):
        for rows in ("0,0,0,0,1\n0,0,0,0,1\n", "0,0,0,0,1\n", "1,0,0,0,1\n"):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                self.evaluate(rows)


if __name__ == "__main__":
    unittest.main()
