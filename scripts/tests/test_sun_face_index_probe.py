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
    def evaluate(self, rows, *, metadata=None, bounds=False, world_point=None):
        values = {"active": "1", "tile_capacity": "64", "tiles_per_axis": "2",
                  "cascade_count": "1", "face_records_requested": "70000",
                  "face_record_capacity": "65536", "basis_u": "1,0,0", "basis_v": "0,1,0"}
        if metadata is not None:
            values.update(metadata)
        columns = "cascade,x,y,count,complete"
        if bounds:
            columns += ",u_min,v_min,u_max,v_max"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiles.csv"
            path.write_text("".join(f"# {key}={value}\n" for key, value in values.items())
                            + columns + "\n" + rows)
            return PROBE.summarize(path, world_point)

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

    def test_invalid_metadata_and_incomplete_rows(self):
        for metadata in ({"tile_capacity": "0"}, {"cascade_count": "-1"},
                         {"face_record_capacity": "0"}, {"face_records_requested": "-1"}):
            with self.subTest(metadata=metadata), self.assertRaisesRegex(ValueError, "dimensions"):
                self.evaluate("", metadata=metadata)
        for rows in ("0,0,0,0\n", "0,0,0,0,1,extra\n"):
            with self.subTest(rows=rows), self.assertRaisesRegex(ValueError, "tile row"):
                self.evaluate(rows)

    def test_missing_and_duplicate_metadata_or_columns(self):
        cases = (("# active=1\n", "missing capture metadata"),
                 ("# active=1\n# active=1\n", "duplicate capture metadata"),
                 ("# active=1\n# broken\n", "invalid.*metadata"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiles.csv"
            for text, message in cases:
                path.write_text(text)
                with self.subTest(text=text), self.assertRaisesRegex(ValueError, message):
                    PROBE.summarize(path)
        with self.assertRaisesRegex(ValueError, "missing tile bounds"):
            self.evaluate("", world_point=(0, 0, 0))

    def test_world_point_and_basis_must_be_finite_three_vectors(self):
        for point in ((0, 0), (0, 0, 0, 0), (float("nan"), 0, 0), (0, float("inf"), 0)):
            with self.subTest(point=point), self.assertRaisesRegex(ValueError, "world point"):
                self.evaluate("", bounds=True, world_point=point)
        for axis in ("1,0", "1,0,0,0", "nan,0,0", "1,inf,0"):
            with self.subTest(axis=axis), self.assertRaisesRegex(ValueError, "sun basis"):
                self.evaluate("", metadata={"basis_u": axis}, bounds=True, world_point=(0, 0, 0))

    def test_duplicate_count_column_cannot_hide_incomplete_tile(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiles.csv"
            path.write_text("# active=1\n# tile_capacity=64\n# tiles_per_axis=1\n"
                            "# cascade_count=1\n# face_records_requested=65\n"
                            "# face_record_capacity=65536\n"
                            "cascade,x,y,count,complete,count\n0,0,0,65,1,0\n")
            with self.assertRaisesRegex(ValueError, "duplicate tile columns"):
                PROBE.summarize(path)

    def test_rejects_invalid_bounds_even_without_world_query(self):
        for bounds in ("nan,0,1,1", "0,0,inf,1", "1,0,0,1", "0,0,1,0"):
            with self.subTest(bounds=bounds), self.assertRaisesRegex(ValueError, "tile bounds"):
                self.evaluate(f"0,0,0,0,1,{bounds}\n", bounds=True)

    def test_rejects_ambiguous_world_point(self):
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.evaluate("0,0,0,0,1,0,0,2,2\n0,1,0,0,1,1,0,3,2\n",
                          bounds=True, world_point=(1.5, 0.5, 0))

    def test_half_open_bounds_and_distinct_cascades(self):
        rows = "".join(f"{cascade},{x},{y},0,1,{x},{y},{x + 1},{y + 1}\n"
                       for cascade in range(2) for y in range(2) for x in range(2))
        for point, tile in (((0, 0, 0), (0, 0)), ((1, 1, 0), (1, 1))):
            with self.subTest(point=point):
                result = self.evaluate(rows, metadata={"cascade_count": "2"},
                                       bounds=True, world_point=point)
                self.assertEqual(result["point_tiles"], [
                    {"cascade": cascade, "x": tile[0], "y": tile[1],
                     "counter": 0, "complete": True} for cascade in range(2)])
        for point in ((-0.001, 0, 0), (2, 0, 0), (0, 2, 0)):
            with self.subTest(point=point):
                result = self.evaluate(rows, metadata={"cascade_count": "2"},
                                       bounds=True, world_point=point)
                self.assertEqual(result["point_tiles"], [])


if __name__ == "__main__":
    unittest.main()
