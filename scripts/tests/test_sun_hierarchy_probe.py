"""GPU hierarchy input layout and readback validation, including corruption controls."""

import gzip
import hashlib
import json
import struct
import sys
import unittest
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "perf"))
import sun_hierarchy_probe as probe  # noqa: E402
from sun_geometry_probe import Quad  # noqa: E402


class HierarchyProbeTest(unittest.TestCase):
    def setUp(self):
        self.face = Quad((Fraction(0),) * 3, (Fraction(1), Fraction(0), Fraction(0)),
                         (Fraction(0), Fraction(1), Fraction(0)))
        self.points = [(Fraction(1, 2), Fraction(1, 2), Fraction(2))]

    def test_record_and_query_packing_with_power_of_two_leaf_capacity(self):
        for count, expected in ((0, 1), (1, 1), (8, 1), (9, 2), (17, 4), (65536, 8192)):
            data, leaves = probe.pack_input([self.face] * count, self.points)
            self.assertEqual(leaves, expected)
            self.assertEqual(struct.unpack_from("<4I", data), (count, leaves, 0, 1))
            self.assertEqual(len(data), 16 + 40 * count + 16)
            if count:
                self.assertEqual(struct.unpack_from("<9fI", data, 16),
                                 (0, 0, 0, 1, 0, 0, 0, 1, 0, 0))
            self.assertEqual(struct.unpack_from("<4f", data, len(data) - 16),
                             (0.5, 0.5, 2, 1 / 256))
        with self.assertRaises(ValueError):
            probe.pack_input([self.face] * 65537, self.points)
        with self.assertRaises(ValueError):
            probe.pack_input([], [])

    def bounds(self, rows):
        return b"".join(struct.pack("<8f", *row) for row in rows)

    def check(self, *, rows=None, result=(1, 1, 1, 1), quads=None, leaves=1):
        rows = rows or [(0,) * 8, (0, 0, 0, 0, 1, 1, 0, 0)]
        return probe.validate_output(struct.pack("<4I", *result), self.bounds(rows),
                                     [self.face] if quads is None else quads,
                                     self.points, leaves)

    def test_leaf_encloses_exact_geometry(self):
        self.assertEqual(self.check()["coverage"], [True])
        for row in ((0.1, 0, 0, 0, 1, 1, 0, 0),
                    (0, 0, 0, 0, 0.9, 1, 0, 0),
                    (0, 0, 0, -1, 1, 1, 0, 0),
                    (0, 0, 0, 5, 1, 1, 0, 0),
                    (float("nan"), 0, 0, 0, 1, 1, 0, 0),
                    (0, 0, 0, 0, float("inf"), 1, 0, 0)):
            with self.subTest(row=row), self.assertRaises(ValueError):
                self.check(rows=[(0,) * 8, row])

    def test_query_mismatch_and_invalid_counters_are_rejected(self):
        for result in ((0, 1, 1, 1), (2, 2, 1, 1), (1, 1, 0, 1),
                       (1, 1, 2, 1), (1, 1, 1, 2)):
            with self.subTest(result=result), self.assertRaises(ValueError):
                self.check(result=result)
        with self.assertRaisesRegex(ValueError, "incomplete"):
            probe.validate_output(b"", b"", [self.face], self.points, 1)

    def test_empty_and_unbounded_nodes(self):
        empty = (0, 0, 0, -1, 0, 0, 0, 0)
        unbounded = (0, 0, 0, 1, 0, 0, 0, 0)
        self.assertEqual(self.check(rows=[(0,) * 8, empty], result=(0, 0, 1, 0),
                                    quads=[])["blocked"], 0)
        self.assertEqual(self.check(rows=[(0,) * 8, unbounded])["blocked"], 1)
        with self.assertRaisesRegex(ValueError, "empty leaf"):
            self.check(quads=[])

    def test_parent_contains_children_and_propagates_unbounded(self):
        bounded = (0, 0, 0, 0, 1, 1, 0, 0)
        empty = (0, 0, 0, -1, 0, 0, 0, 0)
        unbounded = (0, 0, 0, 1, 0, 0, 0, 0)
        rows = [(0,) * 8, bounded, bounded, empty]
        self.check(rows=rows, result=(1, 1, 2, 1), leaves=2)
        for parent, child in ((empty, bounded), (bounded, unbounded),
                              ((0.1, 0, 0, 0, 1, 1, 0, 0), bounded)):
            with self.subTest(parent=parent, child=child), self.assertRaises(ValueError):
                self.check(rows=[(0,) * 8, parent, child, empty],
                           result=(1, 1, 2, 1), leaves=2)

    def test_committed_native_outputs_and_bounds_replay(self):
        evidence = probe.ROOT / "docs/perf/sun-hierarchy-gpu"
        manifest = json.loads((evidence / "manifest.json").read_text())
        for name, digest in manifest.items():
            self.assertEqual(hashlib.sha256((evidence / name).read_bytes()).hexdigest(), digest)
        controls = {name: (quads, points, expected)
                    for name, quads, points, expected in probe.control_cases()}
        reports = sorted(evidence.glob("*/results.json"))
        self.assertEqual({p.parent.name for p in reports},
                         {"controls", "lower", "far-8", "far-16", "far-128", "near-128",
                          "oblique-lower", "oblique-far-128"})
        for report_path in reports:
            report = json.loads(report_path.read_text())
            folder = report_path.parent
            is_control = folder.name == "controls"
            if not is_control:
                captured = (probe.ROOT / "docs/pr-screenshots/codex/sun-geometry-capture"
                            / folder.name)
                capture = captured / "index.csv.gz"
                self.assertEqual(hashlib.sha256(capture.read_bytes()).hexdigest(),
                                 report["capture_sha256"])
                quads, _ = probe.load_quads(capture)
                exact = json.loads((captured / "hierarchy.json").read_text())["coverage"]
                points = [(Fraction(-12) + Fraction(3, 2) * x,
                           Fraction(-12) + Fraction(3, 2) * y, Fraction(2))
                          for y in range(17) for x in range(17)]
            self.assertEqual(set(report["cases"]),
                             set(controls) if is_control else {"captured", "reversed"})
            for name, case in report["cases"].items():
                if is_control:
                    faces, queries, expected = controls[name]
                else:
                    faces = list(reversed(quads)) if name == "reversed" else quads
                    queries, expected = points, exact
                data, leaves = probe.pack_input(faces, queries)
                self.assertEqual(hashlib.sha256(data).hexdigest(), case["input_sha256"])
                for mode in ("fast", "precise"):
                    raw = gzip.decompress((folder / f"{name}.{mode}.bin.gz").read_bytes())
                    bounds = gzip.decompress(
                        (folder / f"{name}.{mode}.bin.bounds.gz").read_bytes())
                    actual = probe.validate_output(raw, bounds, faces, queries, leaves)
                    with self.subTest(scene=folder.name, order=name, mode=mode):
                        self.assertEqual(actual, case["results"][mode])
                        self.assertEqual(actual["coverage"], expected)

    def test_native_controls_have_independent_expected_visibility(self):
        for name, quads, points, expected in probe.control_cases():
            # Repeated quads do not change the independent geometric union.
            unique = set(quads)
            actual = [any(face.blocks(point, Fraction(1, 256), 64) for face in unique)
                      for point in points]
            with self.subTest(case=name):
                self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
