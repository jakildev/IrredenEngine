"""GPU hierarchy input layout and readback validation, including corruption controls."""

import gzip
import hashlib
import io
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from fractions import Fraction
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "perf"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sun_hierarchy_probe as probe  # noqa: E402
from shader_contract_helpers import extract_block  # noqa: E402
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
        self.replay_evidence(probe.ROOT / "docs/perf/sun-hierarchy-gpu")

    def test_committed_spatial_outputs_bounds_and_order_replay(self):
        self.replay_evidence(probe.ROOT / "docs/perf/sun-spatial-hierarchy", spatial=True)

    def test_retained_measurements_preserve_results_and_failed_qualification(self):
        evidence = probe.ROOT / "docs/perf/sun-spatial-hierarchy"
        folder = evidence / "measurements"
        runs = json.loads((folder / "runs.json").read_text())
        self.assertEqual(len(runs["runs"]), 18)
        self.assertEqual(runs["guarded_retry"]["status"], "not-run")
        self.assertEqual((folder / "batched-breach.quiet").read_text().splitlines()[0], "BREACH")
        for run in runs["runs"]:
            raw = gzip.decompress((folder / run["file"]).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(), run["uncompressed_sha256"])
            report = json.loads(raw)
            scene = Path(report["capture"]).parent.name
            validated = json.loads((evidence / scene / "results.json").read_text())
            for order, case in report["cases"].items():
                for mode in ("fast", "precise"):
                    result = case["results"][mode]
                    expected = validated["cases"][order]["results"][mode]["coverage"]
                    self.assertEqual(result["coverage"], expected * report["repeat_queries"])
                    self.assertEqual(result["mismatches"], 0)
                    timing = case["timing"][mode]
                    self.assertGreaterEqual(timing["warmup_rounds"], 2)
                    self.assertEqual(len(timing["samples"]), 7)
                    for sample in timing["samples"]:
                        self.assertGreater(sample["build_gpu_ms"], 0)
                        self.assertGreater(sample["query_gpu_ms"], 0)
            batched = run["file"].startswith("batched-")
            self.assertEqual(run["wrapper_exit"], 76 if batched else 0)
            self.assertEqual(run["status"], "rejected-quiet-breach" if batched
                             else "exploratory-no-retained-quiet-report")

    def replay_evidence(self, evidence, spatial=False):
        manifest = json.loads((evidence / "manifest.json").read_text())
        for name, digest in manifest.items():
            self.assertEqual(hashlib.sha256((evidence / name).read_bytes()).hexdigest(), digest)
        controls = {name: (quads, points, expected)
                    for name, quads, points, expected in probe.control_cases(spatial)}
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
                    order = None
                    if spatial:
                        order = probe.validate_order(gzip.decompress(
                            (folder / f"{name}.{mode}.bin.order.gz").read_bytes()), len(faces))
                        if name == "spatial-octants":
                            probe.validate_octant_order(order)
                    actual = probe.validate_output(raw, bounds, faces, queries, leaves, order)
                    with self.subTest(scene=folder.name, order=name, mode=mode):
                        self.assertEqual(actual, case["results"][mode])
                        self.assertEqual(actual["coverage"], expected)

    def test_native_controls_have_independent_expected_visibility(self):
        for name, quads, points, expected in probe.control_cases(spatial=True):
            # Repeated quads do not change the independent geometric union.
            unique = set(quads)
            actual = [any(face.blocks(point, Fraction(1, 256), 64) for face in unique)
                      for point in points]
            with self.subTest(case=name):
                self.assertEqual(actual, expected)

    def test_spatial_order_rejects_lost_records_unsorted_keys_and_bad_padding(self):
        def packed(rows, header=None):
            padding = [(0xffffffff,) * 3] * (2048 - len(rows))
            return (struct.pack("<2I", *(header or (0, len(rows))))
                    + b"".join(struct.pack("<3I", *row) for row in rows + padding))

        self.assertEqual(probe.validate_order(packed([]), 0), [])
        valid = [(3, 0, 2), (4, 0, 0), (4, 0, 1)]
        self.assertEqual(probe.validate_order(packed(valid), 3), [2, 0, 1])
        for rows in ([(3, 0, 2), (4, 0, 0), (4, 0, 0)],
                     [(3, 0, 3), (4, 0, 0), (4, 0, 1)],
                     [(4, 0, 0), (3, 0, 2), (4, 0, 1)],
                     [(3, 0, 2), (4, 0, 1), (4, 0, 0)],
                     [(1 << 30, 0, 2), (4, 0, 0), (4, 0, 1)],
                     [(3, 1, 2), (4, 0, 0), (4, 0, 1)]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                probe.validate_order(packed(rows), 3)
        raw = packed(valid)
        for corrupt in (raw[:-4], packed(valid, (1, 3)), packed(valid, (0, 2)),
                        raw[:-4] + b"\0" * 4):
            with self.assertRaises(ValueError):
                probe.validate_order(corrupt, 3)

    def test_spatial_control_rejects_all_zero_keys(self):
        raw = struct.pack("<2I", 0, 8)
        raw += b"".join(struct.pack("<3I", 0, 0, i) for i in range(8))
        raw += struct.pack("<3I", *((0xffffffff,) * 3)) * (2048 - 8)
        order = probe.validate_order(raw, 8)
        with self.assertRaisesRegex(ValueError, "XYZ Morton order"):
            probe.validate_octant_order(order)
        probe.validate_octant_order([1, 5, 3, 7, 6, 2, 4, 0])

    def test_measurement_requires_benchmark_hold(self):
        with (patch.object(sys, "argv", ["probe", "unused.csv.gz", "--measure",
                                        "--output", "unused"]),
              patch.object(probe.platform, "system", return_value="Darwin"),
              patch.dict(probe.os.environ, {"IR_RUN_LOCK_TOKEN": "token",
                                           "IR_ACQUIRE_HELD_VERB": "gpu"}, clear=True),
              redirect_stderr(io.StringIO()) as errors,
              self.assertRaises(SystemExit) as result):
            probe.main()
        self.assertEqual(result.exception.code, 2)
        self.assertIn("run under ir-acquire benchmark", errors.getvalue())

    def test_ordered_leaf_validation_uses_original_record_ids(self):
        far = Quad((Fraction(10),) * 3, self.face.edge_u, self.face.edge_v)
        faces = [far] + [self.face] * 8
        rows = [(0,) * 8, (0, 0, 0, 0, 11, 11, 10, 0),
                (0, 0, 0, 0, 1, 1, 0, 0), (10, 10, 10, 0, 11, 11, 10, 0)]
        raw = struct.pack("<4I", 1, 1, 2, 1)
        bounds = self.bounds(rows)
        self.assertEqual(probe.validate_output(raw, bounds, faces, self.points, 2,
                                               list(range(1, 9)) + [0])["blocked"], 1)
        with self.assertRaisesRegex(ValueError, "enclose exact quad"):
            probe.validate_output(raw, bounds, faces, self.points, 2)

    @unittest.skipUnless(shutil.which("c++"), "C preprocessor required")
    def test_timed_query_compiles_out_reference_and_counters(self):
        def preprocess(diagnostic):
            source = f"#define IR_HIERARCHY_DIAGNOSTIC {int(diagnostic)}\n" + probe.KERNEL
            return subprocess.check_output(["c++", "-E", "-P", "-x", "c++", "-"],
                                           input=source, text=True)

        timed, diagnostic = preprocess(False), preprocess(True)
        for expression in ("reference=true", "++visited", "++tested"):
            self.assertIn(expression, diagnostic)
            self.assertNotIn(expression, timed)
        self.assertIn("output[id]=uint4(actual,0,0,0)", timed)
        self.assertIn("blocks(records,recordId,point,64.0)", timed)

    @unittest.skipUnless(shutil.which("c++"), "C++ compiler required")
    def test_probe_merge_schedule_matches_production(self):
        folder = probe.ROOT / "engine/prefabs/irreden/render"
        system = (folder / "systems/system_voxel_to_trixel.hpp").read_text()
        component = (folder / "components/component_per_axis_trixel_canvases.hpp").read_text()
        bits = re.search(r"kOverflowSortBlockBits = (\d+);", component).group(1)
        production = extract_block(system, "inline void forEachOverflowSortMergeStep")
        production = production.replace("C_PerAxisTrixelCanvases::kOverflowSortBlockBits", bits)
        production = production.replace("IRMath::min", "std::min")
        schedule = extract_block(probe.LAUNCHER, "for(uint32_t stage=12;")
        shader = (probe.ROOT / "engine/render/src/shaders/metal/"
                  "c_per_axis_overflow_sort.metal").read_text()
        self.assertRegex(shader, r"kSortThreads = 256u;")
        self.assertRegex(shader, r"kSortBlockBits = 11u;")
        self.assertIn("const uint32_t sortSpan=MAX(2048,cfg[1]*8);", probe.LAUNCHER)
        self.assertIn("sortSpan/(mode==0?256:2048)", probe.LAUNCHER)
        self.assertIn("threadsPerThreadgroup:MTLSizeMake(256,1,1)", probe.LAUNCHER)
        source = r"""
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#define MIN std::min
template <typename Callback>
PRODUCTION
int main(){
    for(uint32_t sortSpan=2048;sortSpan<=65536;sortSpan*=2){
        std::vector<std::array<uint32_t,4>> expected,actual;
        forEachOverflowSortMergeStep(sortSpan,[&](auto k,auto low,auto high,auto){
            expected.push_back({2,k,low,high});
        });
        auto sort=[&](uint32_t mode,auto k,auto low,auto high){
            actual.push_back({mode,k,low,high});
        };
        SCHEDULE
        if(actual!=expected)return 1;
    }
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            for name, candidate, expected in (
                    ("matching", schedule, 0),
                    ("missing_stage", schedule.replace("stage=12", "stage=13"), 1),
                    ("wrong_mode", schedule.replace("sort(2,", "sort(1,"), 1)):
                code = source.replace("PRODUCTION", production).replace("SCHEDULE", candidate)
                (path / "test.cpp").write_text(code)
                subprocess.run(["c++", "-std=c++17", str(path / "test.cpp"),
                                "-o", str(path / "test")], check=True, capture_output=True)
                with self.subTest(variant=name):
                    self.assertEqual(subprocess.run([str(path / "test")]).returncode, expected)


if __name__ == "__main__":
    unittest.main()
