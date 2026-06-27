"""Tests for project_opus_reviewer() / slice_opus_reviewer() in fleet-state-scout.

The opus-reviewer pane is woken purely by the scout writing a trigger when
this projection's hash flips. The regression these tests lock in:

  fleet:needs-opus-recheck — the explicit escalation the sonnet-reviewer
  stamps on an "approve + Opus recheck required" first pass — MUST be a
  trigger signal. Before it was added (PR #1473), the projection keyed only
  on fleet:has-nits / fleet:needs-fix, so an approve-and-escalate PR carried
  no flag label, the hash never flipped, and the opus pane never woke for it
  (it only fired coincidentally on some OTHER PR's has-nits/needs-fix
  transition).

This harness pins:
  - needs-opus-recheck appearing flips the hash (wakes the pane).
  - needs-opus-recheck on a PR with no other flag label still appears.
  - has-nits / needs-fix remain trigger signals (no regression).
  - skip labels (semantic-conflict, wip, amending-*) drop the PR entirely,
    so the escalation lies dormant until the PR is reviewable again.
  - removing the label (opus consumed it) drops the PR from the projection.
  - the slice tags each flagged PR with its repo, across both repos.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
project_opus_reviewer = _mod.project_opus_reviewer
slice_opus_reviewer = _mod.slice_opus_reviewer
stable_hash = _mod.stable_hash


def _state(prs):
    return {"repos": {"engine": {"prs": prs}}}


def _state_eng_game(engine_prs, game_prs):
    return {"repos": {"engine": {"prs": engine_prs},
                      "game": {"prs": game_prs}}}


def _pr(num, *, labels=None, mergeable="MERGEABLE", base="master",
        head="claude/feat", author="bot"):
    return {
        "number": num,
        "title": f"#{num}: feat",
        "headRefName": head,
        "baseRefName": base,
        "labels": sorted(labels or []),
        "mergeable": mergeable,
        "author": author,
    }


def _hash(state):
    return stable_hash(project_opus_reviewer(state))


class RecheckLabelWakesPane(unittest.TestCase):
    """The core fix: fleet:needs-opus-recheck must be a trigger signal."""

    def test_recheck_label_appears_flips_hash(self):
        before = _state([_pr(101, labels=[])])
        after = _state([_pr(101, labels=["fleet:needs-opus-recheck"])])
        self.assertNotEqual(_hash(before), _hash(after),
                            "stamping fleet:needs-opus-recheck must wake the opus pane")

    def test_recheck_only_pr_is_in_projection(self):
        # The approve+recheck case: no verdict label, no nits — only the
        # escalation. This is exactly the state PR #1473 was in.
        items = project_opus_reviewer(_state([
            _pr(101, labels=["fleet:needs-opus-recheck"]),
        ]))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["pr"], 101)
        self.assertEqual(items[0]["labels"], ["fleet:needs-opus-recheck"])

    def test_recheck_with_nits_is_in_projection(self):
        items = project_opus_reviewer(_state([
            _pr(101, labels=["fleet:has-nits", "fleet:needs-opus-recheck"]),
        ]))
        self.assertEqual(len(items), 1)
        self.assertEqual(
            items[0]["labels"],
            ["fleet:has-nits", "fleet:needs-opus-recheck"],
        )

    def test_recheck_removed_drops_from_projection(self):
        # opus-reviewer consumed the escalation and posted fleet:approved.
        # The PR must drop out so the pane isn't re-woken for it.
        items = project_opus_reviewer(_state([
            _pr(101, labels=["fleet:approved"]),
        ]))
        self.assertEqual(items, [])


class ExistingSignalsUnchanged(unittest.TestCase):
    """has-nits / needs-fix must remain trigger signals (no regression)."""

    def test_has_nits_still_triggers(self):
        items = project_opus_reviewer(_state([
            _pr(101, labels=["fleet:has-nits"]),
        ]))
        self.assertEqual(len(items), 1)

    def test_needs_fix_still_triggers(self):
        items = project_opus_reviewer(_state([
            _pr(101, labels=["fleet:needs-fix"]),
        ]))
        self.assertEqual(len(items), 1)


class SkipLabelsGateTheEscalation(unittest.TestCase):
    """Skip labels drop the PR entirely so the recheck escalation lies
    dormant while the PR is not reviewable, then re-activates."""

    def test_semantic_conflict_drops_recheck_pr(self):
        # PR #1473's live state: needs-opus-recheck would be set, but the
        # merger re-applied fleet:semantic-conflict. The PR must NOT be in
        # the opus projection until the conflict clears.
        empty = _state([])
        conflicted = _state([_pr(101, labels=[
            "fleet:needs-opus-recheck", "fleet:semantic-conflict",
        ])])
        self.assertEqual(_hash(empty), _hash(conflicted),
                        "a conflicted PR must be invisible to the opus pane")

    def test_wip_drops_recheck_pr(self):
        empty = _state([])
        wip = _state([_pr(101, labels=[
            "fleet:needs-opus-recheck", "fleet:wip",
        ])])
        self.assertEqual(_hash(empty), _hash(wip))

    def test_amending_prefix_drops_recheck_pr(self):
        empty = _state([])
        amending = _state([_pr(101, labels=[
            "fleet:needs-opus-recheck", "fleet:amending-mac-opus-worker-1",
        ])])
        self.assertEqual(_hash(empty), _hash(amending))


class SliceTagsRepo(unittest.TestCase):
    """The slice carries full PR records tagged with their repo."""

    def test_slice_includes_recheck_pr_both_repos(self):
        out = slice_opus_reviewer(_state_eng_game(
            engine_prs=[_pr(101, labels=["fleet:needs-opus-recheck"])],
            game_prs=[_pr(99, labels=["fleet:needs-opus-recheck"])],
        ))
        repos = sorted(pr["repo"] for pr in out["flagged_prs"])
        self.assertEqual(repos, ["engine", "game"])

    def test_slice_excludes_approved_pr(self):
        out = slice_opus_reviewer(_state([_pr(101, labels=["fleet:approved"])]))
        self.assertEqual(out["flagged_prs"], [])


def _issue(num, *, labels=None):
    return {"number": num, "title": f"#{num}: task", "labels": sorted(labels or [])}


def _state_pr_plan(prs=None, plan_review=None):
    return {"repos": {"engine": {"prs": prs or [],
                                 "plan_review": plan_review or []}}}


class PlanReviewWakesPane(unittest.TestCase):
    """#1932: a posted ## Plan (fleet:plan-review issue) must wake the reviewer.

    Before this, project_opus_reviewer keyed only on PRs, so nothing triggered
    the pane on issue state — plans piled up in fleet:plan-review until some
    unrelated PR coincidentally woke the reviewer.
    """

    def test_plan_review_issue_flips_hash(self):
        before = _state_pr_plan(plan_review=[])
        after = _state_pr_plan(plan_review=[_issue(50, labels=["fleet:plan-review"])])
        self.assertNotEqual(_hash(before), _hash(after),
                            "a plan-review issue must wake the opus pane")

    def test_plan_review_issue_in_projection(self):
        items = project_opus_reviewer(_state_pr_plan(
            plan_review=[_issue(50, labels=["fleet:plan-review"])]))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["kind"], "plan_review")
        self.assertEqual(items[0]["issue"], 50)

    def test_human_held_plan_review_skipped(self):
        for held in ("human:owned", "human:wip", "human:no-plan"):
            empty = _state_pr_plan(plan_review=[])
            held_state = _state_pr_plan(
                plan_review=[_issue(50, labels=["fleet:plan-review", held])])
            self.assertEqual(
                _hash(empty), _hash(held_state),
                f"{held} plan-review issue must be invisible to the pane")

    def test_cleared_plan_review_drops(self):
        # The reviewer verdict removed fleet:plan-review -> the issue leaves the
        # set (terminal), so the pane isn't re-woken for it.
        self.assertEqual(project_opus_reviewer(_state_pr_plan(plan_review=[])), [])

    def test_slice_includes_plan_review(self):
        out = slice_opus_reviewer(_state_pr_plan(
            plan_review=[_issue(50, labels=["fleet:plan-review"])]))
        self.assertEqual(len(out["plan_review"]), 1)
        self.assertEqual(out["plan_review"][0]["number"], 50)
        self.assertEqual(out["plan_review"][0]["repo"], "engine")

    def test_pr_and_plan_review_coexist(self):
        items = project_opus_reviewer(_state_pr_plan(
            prs=[_pr(101, labels=["fleet:needs-opus-recheck"])],
            plan_review=[_issue(50, labels=["fleet:plan-review"])]))
        kinds = sorted(it.get("kind", "pr") for it in items)
        self.assertEqual(kinds, ["plan_review", "pr"])


if __name__ == "__main__":
    unittest.main()
