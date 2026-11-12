"""Lost-tile set comparison and receiver-crop pixel counts, with negative controls."""

import gzip
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "perf"))
import sun_record_loss_stability as stability  # noqa: E402

HEADER = ("# active=1\n# face_records_requested=10\n# face_record_capacity=8\n"
          "cascade,x,y,count,complete,u_min,v_min,u_max,v_max,records_lost\n")


def write_probe(path, lost, count_of=lambda tile: 3):
    tiles = [(c, x, y) for c in range(2) for y in range(2) for x in range(2)]
    rows = "".join(f"{c},{x},{y},{count_of((c, x, y))},{int((c, x, y) not in lost)},"
                   f"0,0,1,1,{int((c, x, y) in lost)}\n" for c, x, y in tiles)
    with gzip.open(path, "wt") as stream:
        stream.write(HEADER + rows)


class LostTilesTest(unittest.TestCase):
    def test_reads_only_the_lost_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sun-face-index-0.csv.gz"
            write_probe(path, {(0, 1, 0), (1, 0, 1)})
            lost, metadata = stability.lost_tiles(path)
        self.assertEqual(lost, {(0, 1, 0), (1, 0, 1)})
        self.assertEqual(metadata["face_record_capacity"], "8")

    def test_window_keeps_only_overlapping_tiles(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sun-face-index-0.csv.gz"
            write_probe(path, {(0, 1, 0), (1, 0, 1)})
            self.assertEqual(stability.lost_tiles(path, (0.5, 0.5, 2.0, 2.0))[0],
                             {(0, 1, 0), (1, 0, 1)})
            self.assertEqual(stability.lost_tiles(path, (1.0, 0.0, 2.0, 1.0))[0], set())
            self.assertEqual(stability.lost_tiles(path, (-2.0, -2.0, 0.0, 0.0))[0], set())

    def test_a_retained_count_change_is_not_a_lost_set_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / "a.csv.gz", Path(tmp) / "b.csv.gz"
            write_probe(a, {(0, 0, 0)})
            write_probe(b, {(0, 0, 0)}, count_of=lambda tile: 7)
            self.assertEqual(stability.lost_tiles(a)[0], stability.lost_tiles(b)[0])


class SetSummaryTest(unittest.TestCase):
    def test_identical_sets_read_zero(self):
        same = {(0, 1, 1), (1, 2, 3)}
        summary = stability.set_summary({"a": set(same), "b": set(same), "c": set(same)})
        self.assertEqual(summary["distinct_sets"], 1)
        self.assertEqual(summary["tiles_differing_any"], 0)
        self.assertEqual(summary["pairwise_max"], 0)

    def test_one_flipped_tile_is_counted(self):
        base = {(0, 1, 1), (1, 2, 3)}
        summary = stability.set_summary({"a": base, "b": base | {(0, 5, 5)}, "c": base})
        self.assertEqual(summary["distinct_sets"], 2)
        self.assertEqual(summary["tiles_differing_any"], 1)
        self.assertEqual(summary["pairwise_max"], 1)
        self.assertEqual(summary["pairwise_min"], 0)
        self.assertEqual((summary["lost_min"], summary["lost_max"]), (2, 3))

    def test_tiles_differing_any_is_union_minus_intersection(self):
        summary = stability.set_summary({"a": {(0, 0, 0), (0, 0, 1)},
                                         "b": {(0, 0, 1), (0, 0, 2)},
                                         "c": {(0, 0, 1), (0, 0, 3)}})
        self.assertEqual(summary["tiles_differing_any"], 3)
        self.assertEqual(summary["pairwise_max"], 2)


class ChangedPixelsTest(unittest.TestCase):
    def setUp(self):
        from PIL import Image
        self.Image = Image
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def frame(self, name, changed=()):
        image = self.Image.new("RGB", (2560, 1440), (10, 20, 30))
        for point in changed:
            image.putpixel(point, (10, 20, 31))
        path = Path(self.tmp.name) / name
        image.save(path)
        return path

    def test_identical_frames_read_zero(self):
        self.assertEqual(stability.changed_pixels(self.frame("a.png"), self.frame("b.png"),
                                                  stability.RECEIVER_CROP), 0)

    def test_a_one_unit_change_inside_the_crop_counts(self):
        inside = (1200, 800)
        self.assertEqual(stability.changed_pixels(self.frame("a.png"),
                                                  self.frame("b.png", [inside]),
                                                  stability.RECEIVER_CROP), 1)

    def test_a_change_outside_the_crop_is_excluded(self):
        outside = [(1200, 600), (10, 10)]
        changed = self.frame("b.png", outside)
        self.assertEqual(stability.changed_pixels(self.frame("a.png"), changed,
                                                  stability.RECEIVER_CROP), 0)
        self.assertEqual(stability.changed_pixels(self.frame("a.png"), changed), 2)

    def test_a_1x_frame_scales_the_crop(self):
        small = self.Image.new("RGB", (1280, 720))
        moved = small.copy()
        moved.putpixel((600, 400), (1, 0, 0))
        a, b = Path(self.tmp.name) / "s.png", Path(self.tmp.name) / "t.png"
        small.save(a)
        moved.save(b)
        self.assertEqual(stability.changed_pixels(a, b, stability.RECEIVER_CROP), 1)

    def test_size_mismatch_refuses(self):
        a = self.frame("a.png")
        b = Path(self.tmp.name) / "small.png"
        self.Image.new("RGB", (1280, 720)).save(b)
        with self.assertRaises(ValueError):
            stability.changed_pixels(a, b)


if __name__ == "__main__":
    unittest.main()
