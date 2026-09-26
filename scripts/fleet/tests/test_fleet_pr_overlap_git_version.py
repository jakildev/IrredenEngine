"""Tests for fleet-pr-overlap's git-version diagnostic on pre-2.38 git.

git merge-tree --write-tree was added in git 2.38; a pre-2.38 git (measured
2.34.1 on the Windows fleet host) hits the old three-positional usage error
instead, which conflict_paths must turn into an actionable message naming
the version requirement rather than surfacing the raw exit-129 usage line as
an opaque OverlapError.
Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-pr-overlap"
_loader = importlib.machinery.SourceFileLoader("fleet_pr_overlap", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_pr_overlap", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

_USAGE_ERR = "usage: git merge-tree <base-tree> <branch1> <branch2>"
_HEAD = "a" * 40
_OTHER = "b" * 40


def _stub(version_out="git version 2.34.1.windows.1"):
    def fake(*args, cwd=None):
        if args[:1] == ("--version",):
            return (0, version_out, "")
        if args[0] == "merge-tree":
            return (129, "", _USAGE_ERR)
        raise AssertionError(f"unexpected git_status call: {args}")
    return fake


class MergeTreePreVersionFloor(unittest.TestCase):
    def test_pre_2_38_usage_error_names_the_version_requirement(self):
        with patch.object(_mod, "git_status", _stub()):
            with self.assertRaises(_mod.OverlapError) as ctx:
                _mod.conflict_paths(1, _HEAD, _OTHER, {"path"})
        message = str(ctx.exception)
        self.assertIn(_mod.MERGE_TREE_MIN_VERSION, message)
        self.assertIn("2.34.1.windows.1", message)

    def test_unparseable_version_output_still_names_the_requirement(self):
        with patch.object(_mod, "git_status", _stub(version_out="")):
            with self.assertRaises(_mod.OverlapError) as ctx:
                _mod.conflict_paths(1, _HEAD, _OTHER, {"path"})
        self.assertIn(_mod.MERGE_TREE_MIN_VERSION, str(ctx.exception))

    def test_other_merge_tree_failures_are_unaffected(self):
        def fake(*args, cwd=None):
            if args[0] == "merge-tree":
                return (128, "", "fatal: not a valid object name")
            raise AssertionError(f"unexpected git_status call: {args}")
        with patch.object(_mod, "git_status", fake):
            with self.assertRaises(_mod.OverlapError) as ctx:
                _mod.conflict_paths(1, _HEAD, _OTHER, {"path"})
        message = str(ctx.exception)
        self.assertNotIn(_mod.MERGE_TREE_MIN_VERSION, message)
        self.assertIn("exited 128", message)


if __name__ == "__main__":
    unittest.main()
