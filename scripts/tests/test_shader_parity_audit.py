"""Pin shader_parity_audit: pairing rules, allowlist rules, and the live tree."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

import shader_parity_audit as audit  # noqa: E402


class Tree:
    """A throwaway shader tree with an optional allowlist."""

    def __init__(self, glsl=(), metal=(), entries=None):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name) / "shaders"
        (self.root / "metal").mkdir(parents=True)
        for name in glsl:
            (self.root / f"{name}.glsl").write_text("")
        for name in metal:
            (self.root / "metal" / f"{name}.metal").write_text("")
        self.allowlist = Path(self._tmp.name) / "allow.json"
        self.allowlist.write_text(json.dumps({"entries": entries or []}))

    def run(self):
        return audit.audit(self.root, self.allowlist)[0]

    def close(self):
        self._tmp.cleanup()


def entry(stem, side, kind="by-design", reason="bundled elsewhere", **extra):
    return {"stem": stem, "side": side, "kind": kind, "reason": reason, **extra}


class AuditCase(unittest.TestCase):
    def check(self, tree, expected_substrings):
        self.addCleanup(tree.close)
        failures = tree.run()
        self.assertEqual(len(failures), len(expected_substrings), failures)
        for fragment in expected_substrings:
            self.assertTrue(any(fragment in f for f in failures), (fragment, failures))


class PairingTest(AuditCase):
    def test_paired_tree_passes(self):
        self.check(Tree(glsl=["c_a", "ir_b"], metal=["c_a", "ir_b"]), [])

    def test_unpaired_glsl_names_the_expected_metal_file(self):
        self.check(Tree(glsl=["c_probe"]), ["c_probe.glsl -> metal/c_probe.metal"])

    def test_unpaired_metal_names_the_expected_glsl_file(self):
        self.check(Tree(metal=["ir_orphan"]), ["metal/ir_orphan.metal -> ir_orphan.glsl"])

    def test_raster_pair_maps_to_one_bare_metal_file(self):
        self.check(Tree(glsl=["v_blit", "f_blit"], metal=["blit"]), [])

    def test_lone_fragment_stage_is_a_valid_raster_pair(self):
        self.check(Tree(glsl=["f_stats"], metal=["stats"]), [])

    def test_raster_without_bare_metal_fails(self):
        self.check(Tree(glsl=["v_blit", "f_blit"]), ["v_blit.glsl", "f_blit.glsl"])

    def test_bare_metal_without_any_raster_stage_fails(self):
        self.check(Tree(metal=["blit"]), ["metal/blit.metal"])

    def test_c_prefix_is_kept_on_the_metal_side(self):
        self.check(Tree(glsl=["c_a"], metal=["a"]), ["c_a.glsl", "metal/a.metal"])

    def test_unrecognised_prefix_fails(self):
        self.check(Tree(glsl=["x_thing"]), ["unrecognised shader name x_thing.glsl"])


class AllowlistTest(AuditCase):
    def test_allowlisted_glsl_passes(self):
        self.check(Tree(glsl=["ir_x"], entries=[entry("ir_x", "glsl")]), [])

    def test_allowlisted_metal_passes(self):
        self.check(Tree(metal=["ir_x"], entries=[entry("ir_x", "metal")]), [])

    def test_entry_for_the_wrong_side_does_not_excuse(self):
        self.check(
            Tree(glsl=["ir_x"], entries=[entry("ir_x", "metal")]),
            ["missing Metal counterpart: ir_x.glsl", "stale allowlist entry metal:ir_x"],
        )

    def test_deferral_with_follow_up_passes(self):
        self.check(
            Tree(glsl=["c_new"], entries=[entry("c_new", "glsl", "deferred", follow_up="#123")]),
            [],
        )

    def test_deferral_without_follow_up_fails(self):
        self.check(
            Tree(glsl=["c_new"], entries=[entry("c_new", "glsl", "deferred")]),
            ["a deferral needs follow_up"],
        )

    def test_deferral_with_prose_follow_up_fails(self):
        self.check(
            Tree(glsl=["c_new"], entries=[entry("c_new", "glsl", "deferred", follow_up="later")]),
            ["a deferral needs follow_up"],
        )

    def test_entry_for_a_paired_file_is_stale(self):
        self.check(
            Tree(glsl=["c_a"], metal=["c_a"], entries=[entry("c_a", "glsl")]),
            ["stale allowlist entry glsl:c_a"],
        )

    def test_entry_without_a_reason_fails(self):
        self.check(
            Tree(glsl=["c_a"], entries=[entry("c_a", "glsl", reason=" ")]),
            ["reason is empty"],
        )

    def test_unknown_kind_fails(self):
        self.check(
            Tree(glsl=["c_a"], entries=[entry("c_a", "glsl", kind="whenever")]),
            ["kind must be one of"],
        )

    def test_duplicate_entry_fails(self):
        self.check(
            Tree(glsl=["c_a"], entries=[entry("c_a", "glsl"), entry("c_a", "glsl")]),
            ["duplicate entry"],
        )

    def test_malformed_allowlist_fails(self):
        tree = Tree(glsl=["c_a"], metal=["c_a"])
        self.addCleanup(tree.close)
        tree.allowlist.write_text("[]")
        self.assertTrue(any("top level must be" in f for f in tree.run()))


class CommandLineTest(unittest.TestCase):
    def test_exit_codes(self):
        clean = Tree(glsl=["c_a"], metal=["c_a"])
        broken = Tree(glsl=["c_probe"])
        self.addCleanup(clean.close)
        self.addCleanup(broken.close)

        def run(tree):
            return audit.main(
                ["--shader-root", str(tree.root), "--allowlist", str(tree.allowlist)]
            )

        self.assertEqual(run(clean), 0)
        self.assertEqual(run(broken), 1)
        self.assertEqual(
            audit.main(["--shader-root", "/nonexistent", "--allowlist", "/nonexistent"]), 2
        )


class CheckedInTreeTest(unittest.TestCase):
    def test_master_tree_is_clean(self):
        failures, n_glsl, n_metal, _ = audit.audit(audit.DEFAULT_ROOT, audit.DEFAULT_ALLOWLIST)
        self.assertEqual(failures, [])
        self.assertGreater(n_glsl, 0)
        self.assertGreater(n_metal, 0)


if __name__ == "__main__":
    unittest.main()
