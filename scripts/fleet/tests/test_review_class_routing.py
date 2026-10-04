"""Review class routing in fleet-state-scout.

The sonnet lane's slice stamps each candidate PR with `review_class` from
its cached changed-file list: "opus" when a path is under a core-area
directory or is a public `ir_*.hpp`, "sonnet" otherwise, and no stamp on a
detail-cache miss (the router keeps the lane's class). fleet_runtime.route
reads the stamp, so a core PR gets one opus-class review that is final
instead of a sonnet pass plus an opus recheck of the same surface.

Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout_review_class", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout_review_class", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class ReviewClassForPaths(unittest.TestCase):
    # Fixture paths are untracked siblings on purpose: fleet_test_subjects.py
    # reads every tracked path literal in a suite as an out-of-tree test
    # subject, and the classifier only looks at the directory and `ir_` prefix.
    def test_core_paths_and_public_headers_read_opus(self):
        for path in ("engine/render/src/shaders/c_zz_routing_fixture.glsl",
                     "engine/entity/include/irreden/entity/zz_routing_fixture.hpp",
                     "engine/system/src/zz_routing_fixture.cpp",
                     "engine/world/include/irreden/world/zz_routing_fixture.hpp",
                     "engine/audio/src/zz_routing_fixture.cpp",
                     "engine/video/include/irreden/video/zz_routing_fixture.hpp",
                     "engine/math/include/irreden/math/ir_math_core.hpp",
                     "engine/input/include/irreden/ir_zz_routing_fixture.hpp",
                     "engine/include/irreden/ir_zz_routing_fixture.hpp",
                     "engine/profile/include/irreden/profile/ir_zz_routing_fixture.hpp",
                     "engine/input/include/irreden/input/ir_zz_routing_fixture.hpp"):
            self.assertEqual(_mod.review_class_for_paths(["docs/x.md", path]), "opus", path)

    def test_everything_else_reads_sonnet(self):
        for paths in (["docs/agents/FLEET.md"],
                      ["creations/demos/fog_demo/zz_routing_fixture.cpp", "scripts/fleet/fleet-up"],
                      ["engine/prefabs/irreden/render/zz_routing_fixture.hpp"],
                      ["engine/input/src/zz_routing_fixture.cpp"],
                      ["engine/script/include/irreden/script/zz_routing_fixture.hpp"],
                      ["engine/input/src/ir_input_impl.hpp"],
                      []):
            self.assertEqual(_mod.review_class_for_paths(paths), "sonnet", paths)


def _pr(number, labels=()):
    return {"number": number, "headRefName": f"claude/{number}-x", "headRefOid": "a" * 40,
            "labels": list(labels), "mergeable": "MERGEABLE", "isDraft": False,
            "reviews": [], "schema": _mod.PR_RECORD_SCHEMA, "closes_issues": []}


class SliceStampsReviewClass(unittest.TestCase):
    def test_slice_stamps_from_the_detail_cache(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(_mod, "PRS_DIR", Path(tmp)):
            (Path(tmp) / "engine").mkdir()
            (Path(tmp) / "engine" / "11.json").write_text(json.dumps(
                {"files": [{"path": "engine/render/src/ir_zz_routing_fixture.cpp"}]}))
            (Path(tmp) / "engine" / "12.json").write_text(json.dumps(
                {"files": [{"path": "scripts/fleet/fleet-up"}]}))
            state = {"repos": {"engine": {"prs": [_pr(11), _pr(12), _pr(13)]}}}
            out = _mod.slice_sonnet_reviewer(state)
            by_number = {pr["number"]: pr for pr in out["candidate_prs"]}
            self.assertEqual(by_number[11]["review_class"], "opus")
            self.assertEqual(by_number[12]["review_class"], "sonnet")
            self.assertNotIn("review_class", by_number[13], "a cache miss leaves no stamp")

    def test_opus_lane_slice_is_untouched(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(_mod, "PRS_DIR", Path(tmp)):
            state = {"repos": {"engine": {"prs": [_pr(11, ["fleet:needs-opus-recheck"])]}}}
            out = _mod.slice_opus_reviewer(state)
            self.assertEqual([pr["number"] for pr in out["flagged_prs"]], [11])
            self.assertNotIn("review_class", out["flagged_prs"][0])


if __name__ == "__main__":
    unittest.main()
