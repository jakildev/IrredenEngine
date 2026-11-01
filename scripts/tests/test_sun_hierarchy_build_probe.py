"""Portable checks for production source expansion and the native readback oracle."""

import io
import struct
import sys
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "perf"))
import sun_hierarchy_build_probe as probe  # noqa: E402


class HierarchyBuildProbeTest(unittest.TestCase):
    def setUp(self):
        self.layout = {
            "kSourceFaceCapacity": 65536,
            "kSourceFaceHierarchyLeafSize": 8,
            "kSourceFaceHierarchyMaxLeaves": 8192,
        }

    def result(self, count=17, analytic=False):
        leaves = probe.leaf_count(count)
        record = probe.RECORD.pack(0, 0, 0, 1, 0, 0, 0, 1, 0, int(analytic))
        empty = (0, 0, 0, -1, 0, 0, 0, 0)
        occupied = (0, 0, 0, 1, 0, 0, 0, 0) if analytic else (0, 0, 0, 0, 1, 1, 0, 0)
        nodes = [empty] * (2 * self.layout["kSourceFaceHierarchyMaxLeaves"])
        for leaf in range((count + 7) // 8):
            nodes[leaves + leaf] = occupied
        for node in range(leaves - 1, 0, -1):
            nodes[node] = (
                occupied
                if any(child[3] >= 0 for child in nodes[2 * node : 2 * node + 2])
                else empty
            )
        return bytearray(
            struct.pack("<I", leaves) + b"".join(probe.NODE.pack(*node) for node in nodes)
        ), record * count

    def validate(self, data, records, count=17):
        return probe.validate_tree(data, records, count, True, self.layout)

    def test_actual_layout_preserves_32_byte_nodes_and_40_byte_records(self):
        layout = probe.parse_layout((probe.ROOT / probe.SHADERS / probe.LAYOUT).read_text())
        self.assertEqual(layout["kSourceFaceHierarchyNodeWords"] * 4, probe.NODE.size)
        self.assertEqual(layout["kSourceFaceRecordWords"] * 4, probe.RECORD.size)
        self.assertEqual(
            layout["kSourceFaceHierarchyOffset"], layout["kSourceFaceHierarchyReadyOffset"] + 1
        )
        self.assertEqual(
            layout["kSourceFaceHierarchyOrderOffset"],
            layout["kSourceFaceHierarchyOffset"] + 2 * 8192 * 8,
        )
        self.assertEqual(
            layout["kSourceFaceBufferWords"],
            layout["kSourceFaceHierarchyOrderEntriesOffset"] + layout["kSourceFaceCapacity"],
        )

    def test_expansion_uses_exact_included_source_and_hashes(self):
        sources = {
            "root.metal": '#include <metal_stdlib>\n#include "leaf.metal"\nvoid kernel() {}\n',
            "leaf.metal": "constant uint value = 9u;\n",
        }
        dependencies = {}
        expanded = probe.expand_shader(sources.__getitem__, "root.metal", dependencies)
        self.assertIn("#include <metal_stdlib>", expanded)
        self.assertIn(sources["leaf.metal"], expanded)
        self.assertIn("void kernel() {}", expanded)
        self.assertEqual(
            dependencies, {name: probe.digest(source.encode()) for name, source in sources.items()}
        )
        with self.assertRaisesRegex(ValueError, "cyclic"):
            probe.expand_shader(lambda _: '#include "root.metal"', "root.metal")

    def test_constant_parser_rejects_noninteger_or_executable_expressions(self):
        self.assertEqual(
            probe.parse_layout("constant uint a=8u; constant uint b=a * 4u / 2u + 1u;"),
            {"a": 8, "b": 17},
        )
        for value in ("3 / 2", "__import__('os')", "2 ** 8"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                probe.parse_layout(f"constant uint a={value};")

    def test_dispatches_keep_all_fourteen_base_passes_and_two_candidate_passes(self):
        base = {**self.layout, "kSourceFaceRecordWords": 10, "kSourceFaceHierarchyNodeWords": 8}
        head = {
            **base,
            "kSourceFaceHierarchyWorkgroupSize": 128,
            "kSourceFaceHierarchyBuildPasses": 2,
        }
        plans = probe.dispatch_plans(base, head)
        self.assertEqual(len(plans["base"]), 14)
        self.assertEqual(plans["base"][0], {"argument": 0, "groups": 128, "threads": 64})
        self.assertEqual(plans["base"][1], {"argument": 4096, "groups": 64, "threads": 64})
        self.assertEqual(plans["base"][-1], {"argument": 1, "groups": 1, "threads": 64})
        self.assertEqual(
            plans["head"],
            [
                {"argument": 0, "groups": 64, "threads": 128},
                {"argument": 1, "groups": 1, "threads": 128},
            ],
        )
        with self.assertRaisesRegex(ValueError, "layout changed"):
            probe.dispatch_plans(base, {**head, "kSourceFaceCapacity": 131072})
        with self.assertRaisesRegex(ValueError, "base must use"):
            probe.dispatch_plans(head, head)

    def test_leaf_boundaries_and_reversed_record_order(self):
        for count, leaves in (
            (0, 1),
            (1, 1),
            (8, 1),
            (9, 2),
            (1023, 128),
            (1024, 128),
            (1025, 256),
            (65536, 8192),
        ):
            self.assertEqual(probe.leaf_count(count), leaves)
        records = list(probe.RECORD.iter_unpack(probe.make_records(35, mixed=True)))
        reverse = list(probe.RECORD.iter_unpack(probe.make_records(35, mixed=True, reverse=True)))
        self.assertEqual(reverse, records[::-1])
        self.assertEqual({record[-1] for record in records}, {0, 1, 2})

    def test_oracle_accepts_enclosure_and_empty_tail(self):
        data, records = self.result()
        self.assertEqual(self.validate(data, records), 7)
        data, records = self.result(analytic=True)
        self.assertEqual(self.validate(data, records), 7)

    def test_oracle_rejects_clipped_leaf_and_wrong_parent_union(self):
        data, records = self.result()
        struct.pack_into("<f", data, 4 + 4 * probe.NODE.size, 0.125)
        with self.assertRaisesRegex(ValueError, "clip"):
            self.validate(data, records)
        data, records = self.result()
        struct.pack_into("<f", data, 4 + probe.NODE.size, -1)
        with self.assertRaisesRegex(ValueError, "merge"):
            self.validate(data, records)

    def test_oracle_rejects_lost_unbounded_flag_stale_empty_and_readiness(self):
        for node, word, value, message in ((1, 3, 0, "state"), (7, 0, 1, "canonical")):
            data, records = self.result(analytic=True)
            struct.pack_into("<f", data, 4 + node * probe.NODE.size + word * 4, value)
            with self.subTest(node=node), self.assertRaisesRegex(ValueError, message):
                self.validate(data, records)
        data, records = self.result()
        struct.pack_into("<I", data, 0, 0)
        with self.assertRaisesRegex(ValueError, "readiness"):
            self.validate(data, records)

    def test_oracle_rejects_truncation_and_incorrect_input_count(self):
        data, records = self.result()
        with self.assertRaisesRegex(ValueError, "full production"):
            self.validate(data[:-1], records)
        with self.assertRaisesRegex(ValueError, "packed input"):
            self.validate(data, records[: -probe.RECORD.size])

    def test_measurement_requires_benchmark_lease_before_native_work(self):
        for environment, measure in (
            ({}, False),
            ({"IR_RUN_LOCK_TOKEN": "token", "IR_ACQUIRE_HELD_VERB": "gpu"}, True),
        ):
            with (
                patch.dict(probe.os.environ, environment, clear=True),
                redirect_stderr(io.StringIO()),
            ):
                with patch.object(probe.platform, "system") as host, self.assertRaises(SystemExit):
                    probe.main(["--output", "/unused", *(["--measure"] if measure else [])])
                host.assert_not_called()


if __name__ == "__main__":
    unittest.main()
