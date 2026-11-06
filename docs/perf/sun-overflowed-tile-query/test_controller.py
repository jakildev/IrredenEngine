#!/usr/bin/env python3
"""Hermetic controls for run_matrix.py: no build, GPU or engine run."""

import json
import math
import statistics
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_matrix as rm  # noqa: E402

SHADERS = rm.ROOT / "engine/render/src/shaders"
TWINS = (SHADERS / "ir_sun_shadow_sample.glsl", SHADERS / "metal/ir_sun_shadow_sample.metal")


class UniqueAblationMatching(unittest.TestCase):
    def test_both_sampler_twins_carry_one_decision_and_one_hierarchy_hit(self):
        for twin in TWINS:
            text = twin.read_text()
            with self.subTest(twin=twin.name):
                ablated = rm.transform(text, rm.ABLATION)
                self.assertEqual(ablated.count("const bool queryHierarchy = false;"), 1)
                self.assertNotIn("sourceSunHierarchyLeafCount(retainedFaces);", ablated)
                self.assertEqual(len(ablated), len(text) - len(rm.DECISION.search(text)[0])
                                 + len("const bool queryHierarchy = false;"))
                for variant, marker in (("entry-shadow", "if (queryHierarchy) return 1.0;"),
                                        ("entry-lit", "if (queryHierarchy) return 0.0;")):
                    staged = rm.transform(text, variant)
                    self.assertEqual(staged.count(marker), 1)
                    self.assertEqual(staged.replace(" " + marker, ""), text)
                cleared = rm.transform(text, "hit-cleared")
                self.assertEqual(len(rm.HIERARCHY_HIT.findall(cleared)), 0)
                self.assertEqual(cleared.count(")) return 0.0;"), text.count(")) return 0.0;") + 1)
                self.assertEqual(rm.transform(text, rm.PRODUCTION), text)

    def test_absent_or_repeated_decision_is_refused(self):
        text = TWINS[0].read_text()
        decision = rm.DECISION.search(text)[0]
        for broken in (text.replace(decision, "const bool exact = true;"),
                       text.replace(decision, decision + "\n" + decision)):
            for variant in (rm.ABLATION, "entry-shadow", "entry-lit", "hit-cleared"):
                with self.subTest(variant=variant), self.assertRaises(rm.StageError):
                    rm.transform(broken, variant)

    def test_repeated_hierarchy_hit_is_refused(self):
        text = TWINS[0].read_text()
        hit = rm.HIERARCHY_HIT.search(text)[0]
        with self.assertRaises(rm.StageError):
            rm.transform(text.replace(hit, hit + "\n" + hit), "hit-cleared")


class AlternatingOrder(unittest.TestCase):
    def test_arm_order_alternates_by_pair(self):
        self.assertEqual(rm.arm_order(1), (rm.PRODUCTION, rm.ABLATION))
        self.assertEqual(rm.arm_order(2), (rm.ABLATION, rm.PRODUCTION))
        self.assertEqual(rm.arm_order(5), (rm.PRODUCTION, rm.ABLATION))

    def test_plan_is_pair_major_with_every_case_and_the_synchronized_control(self):
        arms = rm.plan(5)
        per_pair = (len(rm.CASES) + 1) * 2
        self.assertEqual(len(arms), 5 * per_pair)
        for pair in range(1, 6):
            in_pair = [a for a in arms if a["pair"] == pair]
            self.assertEqual(in_pair, arms[(pair - 1) * per_pair:pair * per_pair])
            for mode, case in [("counters", c) for c in rm.CASES] + [("synchronized", "far-128")]:
                variants = [a["variant"] for a in in_pair if (a["mode"], a["case"]) == (mode, case)]
                self.assertEqual(tuple(variants), rm.arm_order(pair))
        directories = {rm.arm_dir("out", arm) for arm in arms}
        self.assertEqual(len(directories), len(arms))

    def test_synchronized_arms_alone_take_the_preset(self):
        arm = {"mode": "synchronized", "case": "far-128", "pair": 1, "variant": rm.ABLATION}
        self.assertEqual(rm.fixture_args(arm, "p.lua")[-2:], ["--config-preset", "p.lua"])
        arm["mode"] = "counters"
        self.assertNotIn("--config-preset", rm.fixture_args(arm, "p.lua"))
        self.assertEqual(rm.fixture_args(arm, "p.lua"), rm.COMMON)


class PairedSummaryMath(unittest.TestCase):
    def test_paired_delta_bounds_and_share(self):
        production, ablation = [10.0, 12.0, 11.0, 13.0, 14.0], [2.0, 2.5, 2.0, 3.0, 2.5]
        stats = rm.paired(production, ablation)
        deltas = [8.0, 9.5, 9.0, 10.0, 11.5]
        self.assertEqual(stats["deltas"], deltas)
        self.assertAlmostEqual(stats["delta_mean"], 9.6)
        sd = statistics.stdev(deltas)
        self.assertAlmostEqual(stats["delta_sd"], sd)
        half = 2.132 * sd / math.sqrt(5)
        self.assertAlmostEqual(stats["delta_lower_95"], 9.6 - half)
        self.assertAlmostEqual(stats["delta_upper_95"], 9.6 + half)
        self.assertAlmostEqual(stats["share"], 9.6 / 12.0)
        self.assertEqual((stats["production_min"], stats["ablation_max"]), (10.0, 3.0))

    def test_unequal_or_singleton_arms(self):
        with self.assertRaises(ValueError):
            rm.paired([1.0, 2.0], [1.0])
        self.assertIsNone(rm.paired([2.0], [1.0])["delta_lower_95"])

    def test_hierarchy_leaf_count_rounds_up_in_power_of_two_groups(self):
        self.assertEqual([rm.leaf_count(n) for n in (1, 8, 9, 16, 17, 5164, 65536)],
                         [1, 1, 2, 2, 4, 1024, 8192])


def write_index(path, requested, ready, tiles):
    header = ["# active=1", f"# face_records_requested={requested}",
              "# face_record_capacity=65536", f"# hierarchy_ready_leaves={ready}",
              "# hierarchy_needed=1", "# tile_capacity=256", "# tiles_per_axis=2",
              "# cascade_count=1", "# basis_u=1,-0,0", "# basis_v=0,-1,-0",
              "cascade,x,y,count,complete,u_min,v_min,u_max,v_max,records_lost"]
    rows = [",".join(map(str, tile)) for tile in tiles]
    Path(path).write_text("\n".join(header + rows) + "\n")


class PositiveFireWitness(unittest.TestCase):
    TILES = [(0, 0, 0, 300, 0, -1, -1, 1, 1, 0), (0, 1, 0, 400, 0, 1, -1, 3, 1, 1),
             (0, 0, 1, 10, 1, -1, 1, 1, 3, 0), (0, 1, 1, 0, 1, 1, 1, 3, 3, 0)]

    def witness(self, requested, ready, tiles=TILES):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "index.csv"
            write_index(path, requested, ready, tiles)
            return rm.index_witness(path)

    def test_receiver_tile_takes_the_exact_hierarchy_only_when_its_leaves_are_ready(self):
        witness = self.witness(5164, 1024)
        self.assertTrue(witness["hierarchy_ready"])
        self.assertEqual(witness["retained_records"], 5164)
        self.assertEqual([(t["x"], t["y"], t["route"]) for t in witness["receiver_tiles"]],
                         [(0, 0, "exact-hierarchy")])
        self.assertEqual((witness["overflowed_retaining_tiles"], witness["record_lost_tiles"]),
                         (1, 1))
        unready = self.witness(5164, 645)
        self.assertFalse(unready["hierarchy_ready"])
        self.assertEqual(unready["receiver_tiles"][0]["route"], "sampled-fallback")

    def test_a_receiver_tile_that_lost_records_answers_from_the_sampled_layers(self):
        tiles = [(0, 0, 0, 300, 0, -1, -1, 1, 1, 1), *self.TILES[1:]]
        self.assertEqual(self.witness(71470, 8192, tiles)["receiver_tiles"][0]["route"],
                         "sampled-fallback")

    def test_only_floor_to_floor_changes_measure_the_receiver(self):
        lit, shadow, caster, black = (150, 152, 160), (45, 46, 48), (24, 36, 72), (0, 0, 0)
        self.assertTrue(rm.floor_pixel(bytes(lit)) and rm.floor_pixel(bytes(shadow)))
        self.assertFalse(rm.floor_pixel(bytes(caster)) or rm.floor_pixel(bytes(black)))
        reference = [lit, lit, shadow, caster, black, lit]
        actual = [lit, shadow, lit, lit, (80, 120, 240), lit]
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for name, row in (("a.png", actual), ("r.png", reference)):
                path = Path(tmp) / name
                rm.RENDER_COMPARE.write_png(str(path), 3, 2, bytes(v for p in row for v in p), 3)
                paths.append(path)
            floor, other = rm.changed_pixels(*paths)
            self.assertEqual((floor, other), ({1, 2}, {3, 4}))
            self.assertEqual(rm.changed_pixels(*paths, crop=(0, 1, 3, 2)), (set(), {3, 4}))

    def test_verdict(self):
        def entry(route, entered, hit, production=0):
            tile = {"route": route}
            return {"oracle": {"index_witness": {"hierarchy_ready": True,
                                                 "receiver_tiles": [tile]}},
                    "entry_receiver_px": entered,
                    "hit-cleared": {"receiver_changed_vs_oracle": hit},
                    "production": {"receiver_changed_vs_oracle": production}}

        fired = rm.case_verdict(entry("exact-hierarchy", 900, 400))
        self.assertTrue(fired["positive_fire"] and fired["production_matches_oracle"])
        self.assertFalse(fired["sampled_fallback"])
        self.assertFalse(rm.case_verdict(entry("exact-hierarchy", 900, 0))["positive_fire"])
        control = rm.case_verdict(entry("sampled-fallback", 0, 0))
        self.assertTrue(control["sampled_fallback"] and not control["positive_fire"])
        self.assertFalse(rm.case_verdict(entry("exact-hierarchy", 9, 4, 1))
                         ["production_matches_oracle"])


def manifest(expected):
    return {"build_type": expected["build_type"], "binary_sha256": expected["binary_sha256"],
            "shader_sha256": expected["shader_dir_sha256"],
            "runtime_scripts_sha256": expected["runtime_scripts_sha256"],
            "head": expected["source_revision"],
            "command": ["fleet-run", "--timeout", "180", "IRCanvasStress",
                        *expected["fixture_args"]],
            "runs": [{"clean": True, "fresh_report": True, "quiet": "guarded"}]}


class ProvenanceRefusal(unittest.TestCase):
    EXPECTED = {"build_type": "Debug", "binary_sha256": "b" * 64, "shader_dir_sha256": "s" * 64,
                "runtime_scripts_sha256": "r" * 64, "source_revision": "c" * 40,
                "fixture_args": [*rm.COMMON]}

    def test_matching_manifest_is_accepted(self):
        rm.validate_arm(manifest(self.EXPECTED), self.EXPECTED)

    def test_each_mismatch_is_refused(self):
        mutations = {
            "build type": lambda m: m.update(build_type="Release"),
            "binary": lambda m: m.update(binary_sha256="x" * 64),
            "shader set": lambda m: m.update(shader_sha256="x" * 64),
            "runtime scripts": lambda m: m.update(runtime_scripts_sha256="x" * 64),
            "source revision": lambda m: m.update(head="d" * 40),
            "fixture": lambda m: m.update(command=m["command"][:-1] + ["-61"]),
            "unclean": lambda m: m["runs"][0].update(clean=False),
            "stale report": lambda m: m["runs"][0].update(fresh_report=False),
            "unguarded": lambda m: m["runs"][0].update(quiet="unguarded"),
            "two runs": lambda m: m["runs"].append(dict(m["runs"][0])),
        }
        for name, mutate in mutations.items():
            data = manifest(self.EXPECTED)
            mutate(data)
            with self.subTest(name=name), self.assertRaises(rm.ProvenanceError):
                rm.validate_arm(data, self.EXPECTED)

    def test_recorded_build_provenance_refuses_a_different_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            current = {"source_revision": "a", "binary_sha256": "b", "host_gpu": [1]}
            rm.check_provenance(tmp, current)
            rm.check_provenance(tmp, {**current, "host_gpu": [2]})
            with self.assertRaises(rm.ProvenanceError):
                rm.check_provenance(tmp, {**current, "binary_sha256": "c"})


class ByteForByteRestoration(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "shaders"
        sampler = self.root / rm.SUBDIR / rm.SAMPLER
        sampler.parent.mkdir(parents=True)
        self.pristine = TWINS[0].read_bytes().replace(b"\n", b"\r\n")
        sampler.write_bytes(self.pristine)
        (self.root / "other.glsl").write_bytes(b"void main() {}\n")
        self.sampler = sampler
        self.hash = rm.sha256_bytes(self.pristine)
        self.digest = rm.directory_digest(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_success_stages_then_restores_exact_bytes(self):
        with rm.staged(self.root, rm.ABLATION, self.hash, self.digest) as stage:
            data = self.sampler.read_bytes()
            self.assertIn(b"const bool queryHierarchy = false;", data)
            self.assertNotIn(b"\n", data.replace(b"\r\n", b""))
            self.assertEqual(stage["sampler_sha256"], rm.sha256_bytes(data))
            self.assertNotEqual(stage["shader_dir_sha256"], self.digest)
        self.assertEqual(self.sampler.read_bytes(), self.pristine)
        self.assertEqual(rm.directory_digest(self.root), self.digest)

    def test_injected_child_failure_restores_and_propagates(self):
        def failing_runner(command):
            self.assertIn(b"queryHierarchy = false", self.sampler.read_bytes())
            out = Path(command[command.index("--output") + 1])
            out.mkdir(parents=True)
            (out / "run-1.log").write_text("ir-run: RESULT=CRASH exit=139\n")
            return 1

        with self.assertRaises(rm.ChildFailure):
            with rm.staged(self.root, rm.ABLATION, self.hash, self.digest):
                rm.run_profile_arm("debug", Path(self.tmp.name) / "arm", [], 1,
                                   runner=failing_runner)
        self.assertEqual(self.sampler.read_bytes(), self.pristine)
        self.assertEqual(rm.directory_digest(self.root), self.digest)

    def test_interrupt_restores(self):
        with self.assertRaises(KeyboardInterrupt):
            with rm.staged(self.root, "hit-cleared", self.hash, self.digest):
                raise KeyboardInterrupt
        self.assertEqual(self.sampler.read_bytes(), self.pristine)

    def test_a_non_pristine_starting_set_is_refused_untouched(self):
        (self.root / "other.glsl").write_bytes(b"changed\n")
        with self.assertRaises(rm.ProvenanceError):
            with rm.staged(self.root, rm.ABLATION, self.hash, self.digest):
                self.fail("staged a non-pristine set")
        self.assertEqual(self.sampler.read_bytes(), self.pristine)

    def test_quiet_breach_is_retried_and_counted(self):
        attempts = []

        def runner(command):
            out = Path(command[command.index("--output") + 1])
            out.mkdir(parents=True)
            attempts.append(out)
            quiet = "breach" if len(attempts) < 3 else "guarded"
            (out / "run-1.log").write_text(f"ir-run: QUIET={quiet}\n")
            (out / "manifest.json").write_text(json.dumps(
                {"runs": [{"clean": True, "quiet": quiet}]}))
            return 0

        discarded = rm.run_profile_arm("debug", Path(self.tmp.name) / "arm", [], 1, runner=runner)
        self.assertEqual((discarded, len(attempts)), (2, 3))


if __name__ == "__main__":
    unittest.main()
