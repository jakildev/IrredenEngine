"""Lossless capture loading and exact hierarchy pruning, with independent triangle controls."""

import random
import struct
import sys
import tempfile
import unittest
from fractions import Fraction as F
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from perf import sun_geometry_probe as probe  # noqa: E402


def quad(corner=(0, 0, 0), edge_u=(1, 0, 0), edge_v=(0, 1, 0)):
    return probe.Quad(*(tuple(map(F, values)) for values in (corner, edge_u, edge_v)))


def triangle_blocks(face, point, bias, throw):
    """Moller-Trumbore ray/triangle oracle; no projected-quad inverse or hierarchy bounds."""
    def sub(a, b):
        return tuple(x - y for x, y in zip(a, b))

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0])

    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    c, u, v = face.corner, face.edge_u, face.edge_v
    vertices = (c, tuple(x + y for x, y in zip(c, u)),
                tuple(x + y + z for x, y, z in zip(c, u, v)),
                tuple(x + y for x, y in zip(c, v)))
    direction = (0, 0, -1)
    for a, b, c in ((vertices[0], vertices[1], vertices[2]),
                    (vertices[0], vertices[2], vertices[3])):
        ab, ac = sub(b, a), sub(c, a)
        p = cross(direction, ac)
        det = dot(ab, p)
        if det == 0:
            continue
        offset = sub(point, a)
        bary_u = dot(offset, p) / det
        q = cross(offset, ab)
        bary_v = dot(direction, q) / det
        distance = dot(ac, q) / det
        if 0 <= bary_u and 0 <= bary_v and bary_u + bary_v <= 1 and bias < distance < throw:
            return True
    return False


class GeometryProbeTest(unittest.TestCase):
    def capture(self, *, records=None, metadata=None):
        records = records if records is not None else [[0] * 10]
        values = {"active": "1", "tile_capacity": "256", "tiles_per_axis": "1",
                  "cascade_count": "1", "face_records_requested": str(len(records)),
                  "face_record_capacity": "65536", "geometry_version": "1",
                  "geometry_record_words": "10", "geometry_records_captured": str(len(records))}
        values.update({f"geometry_record_{i}": ",".join(map(str, words))
                       for i, words in enumerate(records)})
        values.update(metadata or {})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.csv"
            path.write_text("".join(f"# {key}={value}\n" for key, value in values.items())
                            + "cascade,x,y,count,complete\n0,0,0,392,0\n")
            return probe.load_quads(path)

    def test_roundtrip_f32_payload_with_incomplete_tile(self):
        words = [0x80000000, 1, 0x7f7fffff, 0x3f800000, 0, 0, 0, 0x3f800000, 0, 0]
        faces, index = self.capture(records=[words])
        expected = struct.unpack("<9f", struct.pack("<9I", *words[:9]))
        self.assertEqual(faces[0].corner, tuple(map(F, expected[:3])))
        self.assertEqual(index["cascades"][0]["incomplete_tiles"], 1)

    def test_reject_partial_corrupt_and_unsupported_capture(self):
        for metadata in ({"geometry_version": "2"}, {"geometry_record_words": "9"},
                         {"geometry_records_captured": "0"},
                         {"geometry_record_1": ",".join(["0"] * 10)},
                         {"geometry_record_0": "0,1"},
                         {"face_records_requested": "2", "face_record_capacity": "1"}):
            with self.subTest(metadata=metadata), self.assertRaises(ValueError):
                self.capture(metadata=metadata)
        for words in ([0] * 9 + [3], [-1] + [0] * 9, [0xffffffff + 1] + [0] * 9,
                      [0x7f800000] + [0] * 9, [0x7fc00000] + [0] * 9):
            with self.subTest(words=words), self.assertRaises(ValueError):
                self.capture(records=[words])
        self.assertEqual(self.capture(records=[])[0], [])

    def test_exact_throw_bias_and_closed_footprint(self):
        face = quad()
        for point, bias, throw, expected in (((F(0), F(0), F(1)), 0, 2, True),
                                            ((F(1), F(1), F(1)), 0, 2, True),
                                            ((F(1, 2), F(1, 2), F(64)), 0, 64, False),
                                            ((F(1, 2), F(1, 2), F(1, 256)), F(1, 256), 64, False),
                                            ((F(1001, 1000), F(1, 2), F(1)), 0, 64, False)):
            with self.subTest(point=point):
                self.assertEqual(face.blocks(point, bias, throw), expected)
                self.assertEqual(triangle_blocks(face, point, bias, throw), expected)
                self.assertEqual(probe.Hierarchy([face]).query(point, bias, throw)[0], expected)

    def test_depth_extrema_are_insufficient_for_finite_footprints(self):
        faces = [quad((4, 4, 1)), quad((0, 0, 2)), quad((4, 4, 3))]
        point = (F(1, 2), F(1, 2), F(10))
        self.assertFalse(any(f.blocks(point, 0, 100) for f in (faces[0], faces[2])))
        self.assertTrue(probe.Hierarchy(faces, 1).query(point, 0, 100)[0])

    def test_distant_casters_duplicates_and_order_preserve_blocker(self):
        lower = quad((0, 0, 2))
        distant = quad((0, 0, -120))
        point = (F(1, 2), F(1, 2), F(10))
        self.assertFalse(distant.blocks(point, 0, 100))
        randomizer = random.Random(873)
        faces = [lower] + [distant] * 512
        for _ in range(4):
            randomizer.shuffle(faces)
            tree = probe.Hierarchy(faces)
            blocked, _, tests = tree.query(point, 0, 100)
            self.assertTrue(blocked)
            self.assertLess(tests, 9)
        self.assertFalse(probe.Hierarchy([distant] * 512).query(point, 0, 100)[0])

    def test_rejection_heavy_worst_case_still_visits_every_record(self):
        faces = [quad((0, 0, 0), (10, 10, 0), (1, 0, 0))] * 512
        blocked, _, tested = probe.Hierarchy(faces).query((F(1), F(8), F(10)), 0, 64)
        self.assertFalse(blocked)
        self.assertEqual(tested, len(faces))

    def test_random_oblique_quads_match_independent_triangles(self):
        randomizer = random.Random(3566)
        faces = [quad(*(tuple(randomizer.randint(-8, 8) for _ in range(3))
                        for _ in range(3))) for _ in range(60)]
        points = [tuple(F(randomizer.randint(-20, 20), 2) for _ in range(3))
                  for _ in range(80)]
        for face in faces:
            for point in points:
                self.assertEqual(face.blocks(point, F(1, 256), 16),
                                 triangle_blocks(face, point, F(1, 256), 16))
        expected = [any(triangle_blocks(face, point, F(1, 256), 16) for face in faces)
                    for point in points]
        for leaf_size in (1, 8, 100):
            result = probe.evaluate(faces, points, throw=16, leaf_size=leaf_size)
            self.assertEqual(result["coverage"], expected)
            self.assertEqual(result["mismatches"], 0)

    def test_native_crowded_records_retain_the_missing_center_blocker(self):
        root = (Path(__file__).resolve().parents[2]
                / "docs/pr-screenshots/codex/sun-geometry-capture")
        lower, _ = probe.load_quads(root / "lower/index.csv.gz")
        crowded, index = probe.load_quads(root / "far-128/index.csv.gz")
        self.assertTrue(set(lower) <= set(crowded))
        self.assertGreater(sum(c["incomplete_tiles"] for c in index["cascades"]), 0)
        point = (F(0), F(0), F(2))
        for faces in (lower, crowded):
            self.assertTrue(probe.Hierarchy(faces).query(point, F(1, 256), 64)[0])
            self.assertTrue(any(triangle_blocks(face, point, F(1, 256), 64)
                                for face in faces))

    def test_empty_and_invalid_query_contract(self):
        result = probe.evaluate([], [(F(0), F(0), F(0))])
        self.assertEqual(result["quad_tests"]["max"], 0)
        self.assertEqual(result["blocked"], 0)
        for args in ({"bias": -1}, {"throw": 0}, {"leaf_size": 0}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                probe.evaluate([], [(0, 0, 0)], **args)
        with self.assertRaisesRegex(ValueError, "no receiver"):
            probe.evaluate([], [])


if __name__ == "__main__":
    unittest.main()
