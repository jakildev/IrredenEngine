#!/usr/bin/env python3
"""Hermetic controls for run_matrix.py: no build, GPU or engine run."""

import json
import math
import statistics
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
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

    def test_every_rgb_difference_counts_and_the_crop_bounds_it(self):
        lit, shadow, caster = (150, 152, 160), (45, 46, 48), (24, 36, 72)
        reference = [lit, lit, shadow, caster, (0, 0, 0), lit]
        actual = [lit, (150, 152, 161), lit, lit, (80, 120, 240), lit]

        def frame(row):
            return 3, 2, bytes(v for p in row for v in p)

        self.assertEqual(rm.changed_pixels(frame(actual), frame(reference)), {1, 2, 3, 4})
        self.assertEqual(rm.changed_pixels(frame(actual), frame(reference), (0, 1, 3, 2)), {3, 4})
        self.assertEqual(rm.changed_pixels(frame(actual), frame(actual)), set())
        with self.assertRaises(ValueError):
            rm.changed_pixels((2, 3, frame(actual)[2]), frame(reference))

    def test_receiver_and_stack_crops_split_the_receiver_column(self):
        self.assertEqual(rm.STACK_CROP[3], rm.RECEIVER_CROP[1])
        self.assertEqual((rm.STACK_CROP[0], rm.STACK_CROP[2]),
                         (rm.RECEIVER_CROP[0], rm.RECEIVER_CROP[2]))
        self.assertEqual(rm.scaled_crop(rm.RECEIVER_CROP, 1280), (550, 356, 730, 420))
        self.assertEqual(rm.scaled_crop(rm.RECEIVER_CROP, 2560), rm.RECEIVER_CROP)
        with self.assertRaises(ValueError):
            rm.scaled_crop(rm.RECEIVER_CROP, 1000)

    def test_scoring_refuses_a_frame_that_no_longer_matches_its_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            entry = {}
            for label in ("oracle", *rm.DIAGNOSTIC_VARIANTS, "production"):
                path = Path(tmp) / label / "shot.png"
                path.parent.mkdir()
                rm.RENDER_COMPARE.write_png(str(path), 1280, 720, bytes(1280 * 720 * 3), 3)
                entry[label] = {"frame": path.name, "frame_sha256": rm.sha256(path)}
            entry["oracle"]["index_witness"] = {"hierarchy_ready": False,
                                                "receiver_tiles": [{"route": "sampled-fallback"}]}
            rm.score_case(Path(tmp), entry)
            self.assertEqual(entry["receiver_crop"], (550, 356, 730, 420))
            self.assertTrue(entry["verdict"]["production_matches_oracle"])
            (Path(tmp) / "production" / "shot.png").write_bytes(b"tampered")
            with self.assertRaises(rm.ProvenanceError):
                rm.score_case(Path(tmp), entry)

    def test_a_pre_tree_capture_is_compared_with_the_closing_production(self):
        with tempfile.TemporaryDirectory() as tmp:
            entry = {}
            receiver = rm.scaled_crop(rm.RECEIVER_CROP, 1280)
            for label in ("oracle", *rm.DIAGNOSTIC_VARIANTS, "production", rm.PRE_PRODUCTION):
                pixels = bytearray(1280 * 720 * 3)
                if label == rm.PRE_PRODUCTION:
                    pixels[(receiver[1] * 1280 + receiver[0]) * 3] = 7
                path = Path(tmp) / label / "shot.png"
                path.parent.mkdir()
                rm.RENDER_COMPARE.write_png(str(path), 1280, 720, bytes(pixels), 3)
                entry[label] = {"frame": path.name, "frame_sha256": rm.sha256(path)}
            entry["oracle"]["index_witness"] = {"hierarchy_ready": False,
                                                "receiver_tiles": [{"route": "sampled-fallback"}]}
            rm.score_case(Path(tmp), entry)
            self.assertEqual(entry[rm.PRE_PRODUCTION]["receiver_changed_vs_production"], 1)
            self.assertFalse(entry["verdict"]["production_matches_pre_tree"])
            self.assertTrue(entry["verdict"]["production_matches_oracle"])
            del entry[rm.PRE_PRODUCTION]
            rm.score_case(Path(tmp), entry)
            self.assertNotIn("production_matches_pre_tree", entry["verdict"])

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


class PreTreeProvenance(unittest.TestCase):
    DIFF = "diff --git a/engine/x.cpp b/engine/x.cpp\n-old\n+new"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.build = Path(self.tmp.name) / "build"
        demo = self.build / "creations/demos" / rm.DEMO
        sampler = demo / "shaders" / rm.SUBDIR / rm.SAMPLER
        sampler.parent.mkdir(parents=True)
        sampler.write_bytes(rm.SOURCE_SAMPLER.read_bytes())
        (demo / "shaders/other.glsl").write_bytes(b"void main() {}\n")
        (demo / "scripts").mkdir()
        (demo / rm.TARGET).write_bytes(b"binary")
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
        self.count = 0

    def tearDown(self):
        self.tmp.cleanup()

    def snapshot(self):
        """A fresh `snapshot` of the fake Debug build, taken over a non-empty revert diff."""
        self.count += 1
        dest = Path(self.tmp.name) / f"pre-{self.count}"
        binary = self.build / "creations/demos" / rm.DEMO / rm.TARGET
        paths = (self.build, "Debug", binary, binary.parent / "shaders")
        args = SimpleNamespace(build="debug", dest=str(dest), label="pre", reverts="r" * 40)
        with mock.patch.object(rm, "build_paths", return_value=paths), \
                mock.patch.object(rm, "git", side_effect=lambda *a: self.DIFF
                                  if a[0] == "diff" else "c" * 40), \
                mock.patch("builtins.print"):
            rm.snapshot(args)
        return dest

    def test_a_snapshot_reopens_with_its_recorded_diff(self):
        dest = self.snapshot()
        record = rm.tree_provenance("debug", "pre", dest)
        written = (dest / "tree.diff").read_bytes()
        self.assertEqual(written, (self.DIFF + "\n").encode())
        self.assertEqual(record["tree"]["diff_sha256"], rm.sha256_bytes(written))

    def test_each_mismatch_is_refused(self):
        demo = Path(rm.DEMO)
        mutations = {
            "binary": ("debug", lambda d: (d / "creations/demos" / demo / rm.TARGET)
                       .write_bytes(b"other")),
            "diff": ("debug", lambda d: (d / "tree.diff").write_bytes(b"changed\n")),
            "shader dir": ("debug", lambda d: (d / "creations/demos" / demo / "shaders/new.glsl")
                           .write_bytes(b"\n")),
            "build type": ("release", lambda d: None),
        }
        for name, (build, mutate) in mutations.items():
            dest = self.snapshot()
            mutate(dest)
            with self.subTest(name=name), self.assertRaises(rm.ProvenanceError):
                rm.tree_provenance(build, "pre", dest)

    def test_retained_records_pin_their_committed_diffs(self):
        here = Path(__file__).resolve().parent
        for retained in (here / "windows-opengl-mechanism", here / "windows-opengl-early-tests"):
            records = sorted(retained.glob("*/pre-tree/tree.json"))
            self.assertEqual(len(records), len(rm.BUILDS), retained.name)
            self.check_retained(records)

    def check_retained(self, records):
        for path in records:
            with self.subTest(record=f"{path.parents[2].name}/{path.parent.parent.name}"):
                tree = json.loads(path.read_text())
                self.assertEqual(tree["build_type"], rm.BUILDS[path.parent.parent.name][1])
                self.assertEqual(tree["diff_sha256"], rm.sha256(path.parent / "tree.diff"))


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


class BinaryAB(unittest.TestCase):
    def test_every_pair_runs_both_trees_in_both_variants_in_mirrored_order(self):
        arms = rm.ab_plan(4)
        self.assertEqual(len(arms), 4 * len(rm.CASES) * 4)
        for pair in range(1, 5):
            for case in rm.CASES:
                order = [(a["tree"], a["variant"]) for a in arms
                         if (a["pair"], a["case"]) == (pair, case)]
                self.assertEqual(tuple(order), rm.ab_order(pair))
        self.assertEqual(rm.ab_order(2), tuple(reversed(rm.ab_order(1))))
        # Each tree leads the production comparison in half the pairs.
        leads = [next(tree for tree, variant in rm.ab_order(p) if variant == rm.PRODUCTION)
                 for p in range(1, 5)]
        self.assertEqual(leads.count("pre"), leads.count("post"))
        self.assertEqual(len({rm.ab_arm_dir("out", arm) for arm in arms}), len(arms))

    def test_a_single_tree_probe_alternates_its_variants(self):
        variants = (rm.PRODUCTION, rm.EARLY_TESTS)
        self.assertEqual(rm.ab_order(1, ("post",), variants),
                         (("post", rm.PRODUCTION), ("post", rm.EARLY_TESTS)))
        self.assertEqual(rm.ab_order(2, ("post",), variants),
                         (("post", rm.EARLY_TESTS), ("post", rm.PRODUCTION)))

    def test_early_tests_control_forces_early_tests_on_the_early_route_only(self):
        pristine = (SHADERS / rm.EARLY_FRAGMENT).read_bytes()
        staged = rm.early_tests_bytes(pristine).decode()
        for backend in ("opengl", "metal"):
            with self.subTest(backend=backend), mock.patch.object(rm, "BACKEND", backend):
                self.assertEqual(rm.early_tests_bytes(pristine).decode(), staged)
        self.assertTrue(staged.startswith("#version 450 core\n" + rm.EARLY_TESTS_LINE))
        self.assertEqual(staged.replace(rm.EARLY_TESTS_LINE, "", 1), pristine.decode())
        self.assertIn("#define IR_SOURCE_FACE_EARLY_DEPTH 1", staged)
        with self.assertRaises(rm.StageError):
            rm.early_tests_bytes(staged.encode())
        sampler = TWINS[0].read_text()
        self.assertEqual(rm.transform(sampler, rm.EARLY_TESTS), sampler)

    def dispatch(self, backend, command):
        """main() on an early-tests argv with the backend forced and the runners stubbed;
        returns (exit code, the stubbed runner's call count)."""
        runner = "ab_timing" if command == "ab-timing" else "witness"
        argv = ["run_matrix.py", command, "--build", "debug", "--output", "out",
                "--variants", rm.PRODUCTION, rm.EARLY_TESTS]
        with mock.patch.object(rm, "BACKEND", backend), mock.patch.object(sys, "argv", argv), \
                mock.patch.object(rm, runner) as stub, mock.patch("sys.stderr"):
            try:
                code = rm.main()
            except SystemExit as exit_:
                code = exit_.code
        return code, stub.call_count

    def test_early_tests_variant_is_refused_off_opengl(self):
        for command in ("ab-timing", "witness"):
            with self.subTest(command=command):
                self.assertEqual(self.dispatch("metal", command), (2, 0))
                self.assertEqual(self.dispatch("opengl", command), (0, 1))

    def test_explicit_tree_command_targets_the_snapshot(self):
        command = rm.ab_profile_command(Path("snap"), Path("arm"), ["--x"], 7)
        self.assertEqual(command[command.index("--build-dir") + 1], "snap")
        self.assertNotIn("--release", command)
        self.assertEqual(command[-2:], ["--", "--x"])

    def test_cut_share_is_against_the_before_mean(self):
        stats = rm.paired_difference([30.0, 32.0, 31.0], [60.0, 62.0, 64.0])
        self.assertAlmostEqual(stats["delta_mean"], -31.0)
        self.assertAlmostEqual(stats["change_share"], -31.0 / 62.0)
        self.assertAlmostEqual(stats["upper_share"], stats["delta_upper_95"] / 62.0)

    def test_witness_fixture_drops_the_profile_and_exits_after_the_window(self):
        self.assertNotIn("--auto-profile", rm.WITNESS_COMMON)
        self.assertNotIn("--profile-frames", rm.WITNESS_COMMON)
        self.assertEqual(rm.WITNESS_COMMON[-2:], ["--exit-after-frames", str(rm.WITNESS_EXIT_FRAME)])
        self.assertGreater(rm.WITNESS_EXIT_FRAME, rm.WITNESS_FIRST_FRAME + rm.WITNESS_FRAMES)
        stripped = [v for v in rm.COMMON if v not in ("--auto-profile", "--profile-frames", "360")]
        self.assertEqual(rm.WITNESS_COMMON[:-2], stripped)


def profile_report(composite_row):
    """A minimal exclusive+window profile report with the given composite stage row."""
    return "\n".join((
        "=== PROFILE REPORT (360 frames) ===",
        "Frame time:   avg=30.00ms   p50=30.00ms   p95=31.00ms   p99=32.00ms   min=29.00ms"
        "   max=33.00ms",
        "Steady frame time (first 90 of 360 frames excluded):   avg=29.50ms   p50=29.50ms"
        "   p95=30.00ms   p99=31.00ms   min=29.00ms   max=32.00ms",
        "", "--- GPU frame timing ---",
        "Coverage: supported=1 attempted=360 valid=360 invalid=0 commandBuffers=360",
        "Metric Avg(ms) Min(ms) Max(ms) Samples",
        "commandBufferSpans 27.210 26.900 27.600 360",
        "", "--- GPU stage timing ---", "Format: exclusive+window",
        "Stage                                  Avg(ms)   Min(ms)   Max(ms) Samples Window(ms)"
        " WindowOnly",
        "voxelCompact                             0.100     0.079     0.297     360      0.101"
        "          0",
        composite_row,
        "Exclusive sum: 10.359 ms; commandBufferSpans: 27.210 ms (38.1%)",
        "=== END REPORT ===", ""))


SAMPLED_COMPOSITE = ("entityCanvasToFb                        47.950    47.000    49.000"
                     "     360     47.990          0")
WINDOW_ONLY_COMPOSITE = "entityCanvasToFb                             -         -         -" \
                        "       0      5.800        360 window-only"


class CompositeRow(unittest.TestCase):
    def metrics(self, row, backend):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "arm" / "run-1.txt"
            path.parent.mkdir()
            path.write_text(profile_report(row))
            return rm.report_metrics(path, "counters", backend)

    def test_a_sampled_row_is_read_on_both_backends(self):
        for backend in ("opengl", "metal"):
            with self.subTest(backend=backend):
                metrics = self.metrics(SAMPLED_COMPOSITE, backend)
                self.assertEqual((metrics["entityCanvasToFb"], metrics["entityCanvasToFb_samples"],
                                  metrics["entityCanvasToFb_row"]), (47.95, 360, "sampled"))
                self.assertEqual(metrics["gpu_frame"]["commandBufferSpans"], 27.21)

    def test_metal_reads_its_window_only_composite(self):
        metrics = self.metrics(WINDOW_ONLY_COMPOSITE, "metal")
        self.assertEqual((metrics["entityCanvasToFb"], metrics["entityCanvasToFb_samples"],
                          metrics["entityCanvasToFb_row"]), (5.8, 360, "window-only"))

    def test_opengl_refuses_a_window_only_composite(self):
        with self.assertRaises(rm.ProvenanceError):
            self.metrics(WINDOW_ONLY_COMPOSITE, "opengl")

    def test_a_report_with_no_composite_row_is_refused_on_both_backends(self):
        for backend in ("opengl", "metal"):
            with self.subTest(backend=backend), self.assertRaises(rm.ProvenanceError):
                self.metrics("perAxisScatter 0.649 0.575 0.761 360 0.649 0", backend)

    def test_metal_guard_compares_post_and_pre_means(self):
        def stats(post, pre):
            return {"post vs pre, production": {"production_mean": post, "ablation_mean": pre}}

        contrasts = {"entityCanvasToFb": stats(5.71, 5.80),
                     "gpu_frame:commandBufferSpans": stats(27.03, 27.21)}
        guard = rm.metal_guard(contrasts)
        self.assertTrue(guard["met"])
        self.assertAlmostEqual(guard["entityCanvasToFb"]["ratio"], 5.71 / 5.80)
        contrasts["gpu_frame:commandBufferSpans"] = stats(27.21 * 1.03, 27.21)
        self.assertFalse(rm.metal_guard(contrasts)["met"])
        self.assertIsNone(rm.metal_guard({"entityCanvasToFb": stats(5.71, 5.80)}))
        self.assertEqual(rm.ab_value({"gpu_frame": {"commandBufferSpans": 2.0}},
                                     "gpu_frame:commandBufferSpans"), 2.0)


class ShippedLeverRefusesTheControl(unittest.TestCase):
    def test_early_tests_control_is_refused_once_the_body_forces_early_tests(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "shaders"
            sampler = root / rm.SUBDIR / rm.SAMPLER
            sampler.parent.mkdir(parents=True, exist_ok=True)
            sampler.write_bytes(TWINS[0].read_bytes())
            wrapper = (SHADERS / rm.EARLY_FRAGMENT).read_bytes()
            (root / rm.EARLY_FRAGMENT).write_bytes(wrapper)
            body = root / rm.EARLY_FRAGMENT_BODY
            shipped = (SHADERS / rm.EARLY_FRAGMENT_BODY).read_text()
            self.assertIn(rm.EARLY_TESTS_LINE.strip(), shipped)
            for text, refused in ((shipped, True),
                                  (shipped.replace(rm.EARLY_TESTS_LINE.strip(), ""), False)):
                body.write_text(text)
                hash_, digest = rm.sha256(sampler), rm.directory_digest(root)
                with self.subTest(refused=refused):
                    if refused:
                        with self.assertRaises(rm.StageError):
                            with rm.staged(root, rm.EARLY_TESTS, hash_, digest):
                                self.fail("staged the control over the shipped lever")
                    else:
                        with rm.staged(root, rm.EARLY_TESTS, hash_, digest):
                            self.assertIn(rm.EARLY_TESTS_LINE.encode(),
                                          (root / rm.EARLY_FRAGMENT).read_bytes())
                    self.assertEqual((root / rm.EARLY_FRAGMENT).read_bytes(), wrapper)
                    self.assertEqual(rm.directory_digest(root), digest)


def witness_row(instance, kind, invocations, owned, gpu_ms=1.0, center=(10.0, 20.0)):
    return {"frame": 60, "draw": 0, "instance": instance, "entity": instance, "kind": kind,
            "center_x": center[0], "center_y": center[1], "extent_x": 4.0, "extent_y": 4.0,
            "distance_offset": 0, "fragment_invocations": invocations, "owned_samples": owned,
            "gpu_ms": gpu_ms}


class DrawWitness(unittest.TestCase):
    def test_coincident_canvases_split_hidden_work_into_front_and_cross_canvas(self):
        rows = [witness_row(0, "source-early", 300, 100), witness_row(0, "source-fallback", 0, 0),
                witness_row(1, "source-early", 250, 0), witness_row(2, "source-early", 250, 0),
                witness_row(0, "texture-layer", 50, 0),
                witness_row(3, "source-early", 40, 40, center=(99.0, 0.0))]
        groups = rm.witness_frame(rows)
        stack = groups[rm.canvas_signature(rows[0])]
        self.assertEqual(stack["canvases"], 3)
        self.assertEqual((stack["source_invocations"], stack["source_owned"]), (800, 100))
        self.assertAlmostEqual(stack["source_invocations_per_owned"], 8.0)
        self.assertEqual(stack["front_canvas"], 0)
        self.assertEqual((stack["front_canvas_hidden"], stack["cross_canvas_hidden"]), (200, 500))
        self.assertAlmostEqual(stack["cross_canvas_share"], 500 / 700)
        self.assertEqual(stack["kinds"]["texture-layer"]["invocations"], 50)
        lone = groups[rm.canvas_signature(rows[-1])]
        self.assertEqual((lone["canvases"], lone["cross_canvas_hidden"]), (1, 0))
        self.assertIsNone(lone["cross_canvas_share"])

    def test_unsupported_or_empty_witness_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "witness.csv"
            for text in ("unsupported\n", ",".join(rm.WITNESS_COLUMNS) + "\n", ""):
                path.write_text(text)
                with self.subTest(text=text), self.assertRaises(rm.ChildFailure):
                    rm.read_witness(path)
            path.write_text(",".join(rm.WITNESS_COLUMNS) + "\n"
                            "60,0,0,7,source-early,1.0,2.0,3.0,4.0,-5,90,30,0.25\n")
            row = rm.read_witness(path)[0]
            self.assertEqual((row["fragment_invocations"], row["owned_samples"]), (90, 30))
            self.assertEqual((row["distance_offset"], row["gpu_ms"]), (-5, 0.25))


if __name__ == "__main__":
    unittest.main()
