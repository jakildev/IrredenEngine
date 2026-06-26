"""Tests for _is_gated_self_config and _pr_all_gated_self_config (#1997).

Pins the gated-self-config dedup contract: a PR whose entire diff matches the
auto-mode self-edit gate (.claude/commands/role-*.md, .claude/agents/*,
.claude/skills/**/SKILL.md) is excluded from the worker dispatch trigger once it
carries fleet:human-deferred, so it cannot re-fan-out N panes on the next tick.

Imports via importlib because fleet-state-scout has no .py extension.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"

_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

_is_gated_self_config = _mod._is_gated_self_config
_pr_all_gated_self_config = _mod._pr_all_gated_self_config
project_worker = _mod.project_worker


class TestIsGatedSelfConfig(unittest.TestCase):

    # --- positive (gated) paths ---

    def test_role_command(self):
        self.assertTrue(_is_gated_self_config(".claude/commands/role-worker.md"))

    def test_role_command_other(self):
        self.assertTrue(_is_gated_self_config(".claude/commands/role-opus-reviewer.md"))

    def test_agents_file(self):
        self.assertTrue(_is_gated_self_config(".claude/agents/foo.md"))

    def test_agents_subdirectory(self):
        self.assertTrue(_is_gated_self_config(".claude/agents/bar/baz.txt"))

    def test_skill_md(self):
        self.assertTrue(_is_gated_self_config(".claude/skills/simplify/SKILL.md"))

    def test_skill_md_nested(self):
        self.assertTrue(_is_gated_self_config(".claude/skills/commit-and-push/SKILL.md"))

    # --- negative (not gated) paths ---

    def test_non_role_command(self):
        # Only role-*.md matches, not arbitrary commands
        self.assertFalse(_is_gated_self_config(".claude/commands/commit-and-push.md"))

    def test_non_skill_md(self):
        # Non-SKILL.md files under skills/ are not gated
        self.assertFalse(_is_gated_self_config(".claude/skills/simplify/notes.md"))

    def test_engine_source(self):
        self.assertFalse(_is_gated_self_config("engine/render/system_render.cpp"))

    def test_docs_file(self):
        self.assertFalse(_is_gated_self_config("docs/agents/FLEET.md"))

    def test_scripts_fleet(self):
        self.assertFalse(_is_gated_self_config("scripts/fleet/fleet-state-scout"))


class TestPrAllGatedSelfConfig(unittest.TestCase):

    def _make_pr_cache(self, tmpdir, repo_key, pr_number, paths):
        pr_dir = Path(tmpdir) / repo_key
        pr_dir.mkdir(parents=True, exist_ok=True)
        data = {"files": [{"path": p} for p in paths]}
        (pr_dir / f"{pr_number}.json").write_text(json.dumps(data))

    def test_all_gated_returns_true(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            self._make_pr_cache(tmpdir, "engine", 1990, [
                ".claude/commands/role-worker.md",
                ".claude/agents/foo.md",
                ".claude/skills/simplify/SKILL.md",
            ])
            with patch.object(_mod, "PRS_DIR", Path(tmpdir)):
                self.assertTrue(_pr_all_gated_self_config("engine", 1990))

    def test_mixed_returns_false(self):
        # One gated + one normal path → not all-gated → still surfaced
        with tempfile.TemporaryDirectory() as tmpdir:
            self._make_pr_cache(tmpdir, "engine", 42, [
                ".claude/commands/role-worker.md",
                "scripts/fleet/fleet-state-scout",
            ])
            with patch.object(_mod, "PRS_DIR", Path(tmpdir)):
                self.assertFalse(_pr_all_gated_self_config("engine", 42))

    def test_non_gated_only_returns_false(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            self._make_pr_cache(tmpdir, "engine", 99, [
                "docs/agents/FLEET.md",
                "engine/render/system.cpp",
            ])
            with patch.object(_mod, "PRS_DIR", Path(tmpdir)):
                self.assertFalse(_pr_all_gated_self_config("engine", 99))

    def test_empty_file_list_returns_false(self):
        # Cache miss / empty diff → safe degradation: never wrongly exclude
        with tempfile.TemporaryDirectory() as tmpdir:
            pr_dir = Path(tmpdir) / "engine"
            pr_dir.mkdir(parents=True)
            (pr_dir / "77.json").write_text(json.dumps({"files": []}))
            with patch.object(_mod, "PRS_DIR", Path(tmpdir)):
                self.assertFalse(_pr_all_gated_self_config("engine", 77))

    def test_cache_miss_returns_false(self):
        # No file at all → safe degradation
        with tempfile.TemporaryDirectory() as tmpdir:
            with patch.object(_mod, "PRS_DIR", Path(tmpdir)):
                self.assertFalse(_pr_all_gated_self_config("engine", 9999))


def _state(engine_prs=None, game_prs=None, engine_tasks=None, game_tasks=None):
    return {
        "repos": {
            "engine": {
                "prs": engine_prs or [],
                "tasks": {"open": engine_tasks or []},
                "needs_plan": [],
            },
            "game": {
                "prs": game_prs or [],
                "tasks": {"open": game_tasks or []},
                "needs_plan": [],
            },
        }
    }


def _pr(number, labels, paths=None, head="claude/test"):
    return {
        "number": number,
        "labels": labels,
        "headRefName": head,
        "files": [{"path": p} for p in (paths or [])],
    }


class TestProjectWorkerGatedFilter(unittest.TestCase):
    """project_worker excludes all-gated+human-deferred PRs from dispatch."""

    def _run_with_cache(self, tmpdir, state):
        with patch.object(_mod, "PRS_DIR", Path(tmpdir)):
            return project_worker(state)

    def _setup_cache(self, tmpdir, repo_key, pr_number, paths):
        pr_dir = Path(tmpdir) / repo_key
        pr_dir.mkdir(parents=True, exist_ok=True)
        data = {"files": [{"path": p} for p in paths]}
        (pr_dir / f"{pr_number}.json").write_text(json.dumps(data))

    def test_all_gated_and_human_deferred_excluded(self):
        # PR with fleet:needs-fix + fleet:human-deferred whose entire diff is
        # gated self-config must NOT appear in project_worker output.
        with tempfile.TemporaryDirectory() as tmpdir:
            self._setup_cache(tmpdir, "engine", 1990,
                              [".claude/commands/role-worker.md"])
            pr = _pr(1990,
                     ["fleet:needs-fix", "fleet:human-deferred"],
                     paths=[".claude/commands/role-worker.md"],
                     head="claude/1990-x")
            result = self._run_with_cache(tmpdir, _state(engine_prs=[pr]))
        numbers = [item["pr"] for item in result if item.get("kind") == "pr"]
        self.assertNotIn(1990, numbers)

    def test_all_gated_without_human_deferred_still_surfaced(self):
        # PR with fleet:needs-fix but NOT yet human-deferred → still the first
        # picker needs to see it so it can park it.
        with tempfile.TemporaryDirectory() as tmpdir:
            self._setup_cache(tmpdir, "engine", 1991,
                              [".claude/commands/role-worker.md"])
            pr = _pr(1991,
                     ["fleet:needs-fix"],
                     paths=[".claude/commands/role-worker.md"],
                     head="claude/1991-x")
            result = self._run_with_cache(tmpdir, _state(engine_prs=[pr]))
        numbers = [item["pr"] for item in result if item.get("kind") == "pr"]
        self.assertIn(1991, numbers)

    def test_partial_gated_human_deferred_not_excluded(self):
        # Mixed diff (some gated, some not) + human-deferred → NOT excluded
        # because the non-gated part may still need amendment.
        with tempfile.TemporaryDirectory() as tmpdir:
            self._setup_cache(tmpdir, "engine", 1992, [
                ".claude/commands/role-worker.md",
                "scripts/fleet/fleet-state-scout",
            ])
            pr = _pr(1992,
                     ["fleet:needs-fix", "fleet:human-deferred"],
                     paths=[".claude/commands/role-worker.md",
                             "scripts/fleet/fleet-state-scout"],
                     head="claude/1992-x")
            result = self._run_with_cache(tmpdir, _state(engine_prs=[pr]))
        numbers = [item["pr"] for item in result if item.get("kind") == "pr"]
        self.assertIn(1992, numbers)

    def test_non_gated_pr_unaffected(self):
        # Normal PR with fleet:needs-fix still shows up.
        with tempfile.TemporaryDirectory() as tmpdir:
            self._setup_cache(tmpdir, "engine", 1993,
                              ["engine/render/system.cpp"])
            pr = _pr(1993,
                     ["fleet:needs-fix"],
                     paths=["engine/render/system.cpp"],
                     head="claude/1993-x")
            result = self._run_with_cache(tmpdir, _state(engine_prs=[pr]))
        numbers = [item["pr"] for item in result if item.get("kind") == "pr"]
        self.assertIn(1993, numbers)


if __name__ == "__main__":
    unittest.main()
