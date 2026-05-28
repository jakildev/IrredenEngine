"""Tests for resolve_blocked_by() in fleet-state-scout (T-1296).

Covers:
  (a) one closed ref → (none)
  (a) one still-open ref → unchanged bare #NNN
  (a) two refs, one closed → bare #NNN for the remaining one
  (a) both closed → (none)
  (a) merged-PR variant: issue open but claude/<N>-* PR in merged list → satisfied
  (a) free-text blocked_by not touched
  (a) (none) field not touched
  (b) integration with enrich_stackable_blocker_prs: multi-blocker reduces to
      single-blocker after resolution → stackable_blocker_pr written
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

resolve_blocked_by = _mod.resolve_blocked_by
enrich_stackable_blocker_prs = _mod.enrich_stackable_blocker_prs


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _state(engine_tasks=None, engine_prs=None, closed=None, merged_prs=None):
    return {
        "repos": {
            "engine": {
                "tasks": {"open": engine_tasks or []},
                "prs": engine_prs or [],
                "closed_fleet_queued": closed or [],
                "recent_merged_prs": merged_prs or [],
            }
        }
    }


def _task(id_, blocked_by):
    return {"id": id_, "blocked_by": blocked_by, "area": None}


def _pr(number, head_ref, author="bot"):
    return {"number": number, "headRefName": head_ref, "author": author, "labels": []}


def _closed_issue(number):
    return {"number": number, "title": "done", "labels": []}


def _merged_pr(head_ref):
    return {"number": 0, "headRefName": head_ref, "baseRefName": "master"}


def _gh_state_stub(state_map):
    """Return a callable that stubs subprocess.run for gh issue view --jq .state."""
    import subprocess

    def fake_run(cmd, **kwargs):
        if cmd[0] == "gh" and cmd[1] == "issue" and cmd[2] == "view":
            ref = cmd[3]
            state = state_map.get(ref, "OPEN")
            result = subprocess.CompletedProcess(cmd, 0, stdout=state + "\n", stderr="")
            return result
        result = subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")
        return result

    return fake_run


# ---------------------------------------------------------------------------
# Part (a) — resolve_blocked_by
# ---------------------------------------------------------------------------

class TestResolveBlockedBy(unittest.TestCase):

    def test_closed_ref_resolves_to_none(self):
        """Single ref closed via closed_fleet_queued → (none)."""
        tasks = [_task("#200", "#100")]
        state = _state(engine_tasks=tasks, closed=[_closed_issue(100)])
        resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "(none)")

    def test_open_ref_unchanged(self):
        """Single ref that is still open → bare #NNN."""
        tasks = [_task("#200", "#101")]
        with patch.object(_mod.subprocess, "run", _gh_state_stub({"101": "OPEN"})):
            state = _state(engine_tasks=tasks)
            resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "#101")

    def test_two_refs_one_closed_one_open(self):
        """Two refs; closed one filtered out → bare #NNN for remaining."""
        tasks = [_task("#200", "#100 (done), #101 (still open)")]
        state = _state(engine_tasks=tasks, closed=[_closed_issue(100)])
        with patch.object(_mod.subprocess, "run", _gh_state_stub({"101": "OPEN"})):
            resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "#101")

    def test_both_refs_closed_resolves_to_none(self):
        """Both refs closed → (none)."""
        tasks = [_task("#200", "#100, #102")]
        state = _state(engine_tasks=tasks, closed=[_closed_issue(100), _closed_issue(102)])
        resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "(none)")

    def test_merged_pr_variant_satisfies_open_issue(self):
        """Issue still open but claude/<N>-* PR merged → satisfied."""
        tasks = [_task("#200", "#101")]
        state = _state(
            engine_tasks=tasks,
            merged_prs=[_merged_pr("claude/101-rotation-fix")],
        )
        resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "(none)")

    def test_merged_pr_prefix_discrimination(self):
        """claude/1011-* does NOT satisfy #101 (requires trailing dash)."""
        tasks = [_task("#200", "#101")]
        state = _state(
            engine_tasks=tasks,
            merged_prs=[_merged_pr("claude/1011-unrelated")],
        )
        with patch.object(_mod.subprocess, "run", _gh_state_stub({"101": "OPEN"})):
            resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "#101")

    def test_free_text_blocked_by_not_touched(self):
        """Free-text (no #N refs) blocked_by is left unchanged."""
        tasks = [_task("#200", "pending design review")]
        state = _state(engine_tasks=tasks)
        resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "pending design review")

    def test_none_string_not_touched(self):
        """(none) field is not processed."""
        tasks = [_task("#200", "(none)")]
        state = _state(engine_tasks=tasks)
        resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "(none)")

    def test_none_value_not_touched(self):
        """None blocked_by is not processed."""
        tasks = [_task("#200", None)]
        state = _state(engine_tasks=tasks)
        resolve_blocked_by(state)
        self.assertIsNone(tasks[0]["blocked_by"])

    def test_two_unresolved_refs_stay_multi(self):
        """Two open refs → comma-joined bare format, still multi-blocker."""
        tasks = [_task("#200", "#101, #102")]
        with patch.object(_mod.subprocess, "run", _gh_state_stub({"101": "OPEN", "102": "OPEN"})):
            state = _state(engine_tasks=tasks)
            resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "#101, #102")

    def test_live_gh_fallback_closed(self):
        """Ref not in closed_fleet_queued: falls back to live gh issue view."""
        tasks = [_task("#200", "#999")]
        with patch.object(_mod.subprocess, "run", _gh_state_stub({"999": "CLOSED"})):
            state = _state(engine_tasks=tasks)
            resolve_blocked_by(state)
        self.assertEqual(tasks[0]["blocked_by"], "(none)")


# ---------------------------------------------------------------------------
# Part (b) — integration: resolve then enrich gives stackable_blocker_pr
# ---------------------------------------------------------------------------

class TestResolveAndEnrichIntegration(unittest.TestCase):

    def test_multi_blocker_reduces_to_stackable(self):
        """Two refs, one closed → single unresolved → enrich finds PR → stackable written."""
        tasks = [_task("#300", "#100, #101")]
        open_prs = [_pr(536, "claude/101-work-branch")]
        state = _state(
            engine_tasks=tasks,
            engine_prs=open_prs,
            closed=[_closed_issue(100)],
        )
        with patch.object(_mod.subprocess, "run", _gh_state_stub({"101": "OPEN"})):
            resolve_blocked_by(state)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertEqual(task["blocked_by"], "#101")
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 536)

    def test_all_resolved_no_stackable_written(self):
        """All refs resolved → (none) → enrich skips (no #NNN to match)."""
        tasks = [_task("#300", "#100, #102")]
        state = _state(
            engine_tasks=tasks,
            closed=[_closed_issue(100), _closed_issue(102)],
        )
        resolve_blocked_by(state)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertEqual(task["blocked_by"], "(none)")
        self.assertNotIn("stackable_blocker_pr", task)

    def test_two_unresolved_no_stackable(self):
        """Two open refs → multi-blocker → enrich does not write stackable_blocker_pr."""
        tasks = [_task("#300", "#101, #102")]
        open_prs = [
            _pr(536, "claude/101-work"),
            _pr(537, "claude/102-other"),
        ]
        with patch.object(_mod.subprocess, "run", _gh_state_stub({"101": "OPEN", "102": "OPEN"})):
            state = _state(engine_tasks=tasks, engine_prs=open_prs)
            resolve_blocked_by(state)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr", state["repos"]["engine"]["tasks"]["open"][0])


if __name__ == "__main__":
    unittest.main()
