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
import io
import os
import subprocess
import sys
import tempfile
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


class CheckGitVersionArm(unittest.TestCase):
    """`--check-git-version`: the floor check setup-windows.sh feeds a pane's
    captured `git --version` into."""

    def check(self, text):
        with patch("sys.stdout", new_callable=io.StringIO) as out:
            rc = _mod.check_git_version(text)
        return rc, out.getvalue()

    def test_incident_host_git_is_too_old(self):
        rc, out = self.check("git version 2.34.1.windows.1\n")
        self.assertEqual(rc, _mod.EXIT_GIT_TOO_OLD)
        self.assertIn("2.34.1.windows.1", out)
        self.assertIn(f">= {_mod.MERGE_TREE_MIN_VERSION}", out)

    def test_floor_and_vendor_suffixes_pass(self):
        for text in ("git version 2.38.0.windows.1", "git version 2.38",
                     "git version 2.39.3 (Apple Git-146)", "git version 3.0.0\r\n"):
            with self.subTest(text=text):
                self.assertEqual(self.check(text)[0], 0)

    def test_unreadable_is_distinct_from_too_old(self):
        for text in ("", "bash: git: command not found", "git version\n",
                     "git version 2.40.0\ngit version 2.30.0"):
            with self.subTest(text=text):
                self.assertEqual(self.check(text)[0], _mod.EXIT_GIT_UNREADABLE)

    def test_omitted_text_reads_this_process_git(self):
        with patch.object(_mod, "git_status", _stub("git version 2.37.9")):
            rc, out = self.check(None)
        self.assertEqual(rc, _mod.EXIT_GIT_TOO_OLD)
        self.assertIn("2.37.9", out)

    def test_cli_arm_needs_no_repository_or_network(self):
        with tempfile.TemporaryDirectory() as tmp:
            cases = (("git version 2.34.1.windows.1", 1),
                     ("git version 2.38.0.windows.1", 0), ("", 2))
            for text, want in cases:
                with self.subTest(text=text):
                    proc = subprocess.run(
                        [sys.executable, str(_SCRIPT), "--check-git-version", text],
                        cwd=tmp, capture_output=True, text=True, timeout=60,
                        env={**os.environ, "GH_TOKEN": "hermetic-test-invalid",
                             "GH_HOST": "invalid.invalid"},
                    )
                    self.assertEqual(proc.returncode, want, proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
