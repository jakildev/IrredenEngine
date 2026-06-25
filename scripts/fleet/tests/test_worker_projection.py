"""Tests for project_worker / project_opus_reviewer / project_sonnet_reviewer
in fleet-state-scout.

The worker / reviewer hash-input projections MUST be stable across labels
their role's decision logic does NOT key on (merger-toggled labels like
fleet:merger-cooldown, smoke-gate labels like fleet:needs-linux-smoke,
authoring-host markers like fleet:authored-on-macos, recheck-cycle
fleet:changes-made on a still-flagged PR, etc.). Without this invariant,
every label flip on a PR that happens to also have a feedback label
re-triggers every role; the role iterations spin up, apply their actual
filters (lane ownership, branch lock, smoke filters), find nothing
actionable, and exit. Observed live 2026-05-22 on PR #1047: scout fired
sonnet-author + opus-worker four times in 5 minutes while reviewers and
the merger flipped labels around the PR, with each iteration exiting clean.

Mirrors test_merger_projection.py — the merger's projection was already
hardened against this; the worker/reviewer projections were not.

P2: the per-class project_sonnet_author / project_opus_worker pair was
unified into a single project_worker (every task class + needs_plan +
feedback/design-resume PRs), so the worker invariants below are tested
against that one projector.
"""
import importlib.machinery
import importlib.util
import json
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
project_worker = _mod.project_worker
project_opus_reviewer = _mod.project_opus_reviewer
project_sonnet_reviewer = _mod.project_sonnet_reviewer
stable_hash = _mod.stable_hash


def _state(prs, tasks=None, needs_plan=None):
    return {
        "repos": {
            "engine": {
                "prs": prs,
                "tasks": tasks or {"open": [], "in_progress": [], "done": []},
                "needs_plan": needs_plan or [],
            }
        }
    }


def _pr(num, *, labels=None, head="claude/feat", is_draft=False):
    return {
        "number": num,
        "title": f"T-{num}: feat",
        "headRefName": head,
        "baseRefName": "master",
        "labels": sorted(labels or []),
        "isDraft": is_draft,
        "mergeable": "MERGEABLE",
        "author": "bot",
    }


def _task(tid, *, model, owner="free", blocked_by="(none)"):
    return {"id": tid, "model": model, "owner": owner, "blocked_by": blocked_by}


class WorkerStableAcrossIrrelevantLabels(unittest.TestCase):
    """Labels the worker's decision logic does NOT key on must not flip
    the projection hash on a PR that's already in the projection."""

    def _hash(self, prs):
        return stable_hash(project_worker(_state(prs)))

    def test_merger_cooldown_does_not_flip_hash(self):
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:merger-cooldown",
        ])])
        self.assertEqual(before, after,
                         "merger-cooldown toggle must not re-fire the worker")

    def test_semantic_conflict_does_not_flip_hash(self):
        # The worker handles semantic-conflict (step 1c), but the projector
        # includes a PR via FEEDBACK_LABELS, not semantic-conflict. Flipping
        # that label while the feedback label is unchanged must not re-fire.
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:semantic-conflict",
        ])])
        self.assertEqual(before, after)

    def test_needs_linux_smoke_does_not_flip_hash(self):
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:needs-linux-smoke",
        ])])
        self.assertEqual(before, after)

    def test_authored_on_macos_does_not_flip_hash(self):
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:authored-on-macos",
        ])])
        self.assertEqual(before, after)

    def test_changes_made_on_still_flagged_pr_does_not_flip(self):
        # While the worker is amending a flagged PR, fleet:changes-made
        # is toggled on each push. The feedback label is still set, so
        # the PR stays in the projection — the toggle alone must not
        # re-fire the worker.
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:changes-made",
        ])])
        self.assertEqual(before, after)


class WorkerActionableTransitionsFlipHash(unittest.TestCase):
    """Real changes the worker should react to must still re-fire. The
    unified lane keys on FEEDBACK_LABELS, DESIGN_RESUME_LABELS, every task
    class, and needs_plan issues."""

    def _hash(self, prs, tasks=None, needs_plan=None):
        return stable_hash(project_worker(_state(prs, tasks, needs_plan)))

    def test_needs_fix_appears_flips_hash(self):
        before = self._hash([_pr(101, labels=[])])
        after = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        self.assertNotEqual(before, after)

    def test_needs_fix_clears_flips_hash(self):
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[])])
        self.assertNotEqual(before, after)

    def test_human_needs_fix_appears_flips_hash(self):
        before = self._hash([_pr(101, labels=[])])
        after = self._hash([_pr(101, labels=["human:needs-fix"])])
        self.assertNotEqual(before, after)

    def test_needs_fix_to_has_nits_flips_hash(self):
        # Both are FEEDBACK_LABELS but the verdict is different — the
        # role should re-evaluate.
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=["fleet:has-nits"])])
        self.assertNotEqual(before, after)

    def test_design_unblocked_appears_flips_hash(self):
        before = self._hash([_pr(101, labels=[])])
        after = self._hash([_pr(101, labels=["fleet:design-unblocked"])])
        self.assertNotEqual(before, after)

    def test_needs_plan_issue_appears_flips_hash(self):
        before = self._hash([])
        after = self._hash([], needs_plan=[{"number": 222}])
        self.assertNotEqual(before, after)


class WorkerCoversEveryTaskClass(unittest.TestCase):
    """The unified lane is the union of the old per-class lanes — a task of
    any class (fable / opus / sonnet) appears in the projection, and a new
    task flips the hash regardless of its class."""

    def _hash(self, tasks):
        return stable_hash(project_worker(_state([], tasks=tasks)))

    def test_each_class_appears(self):
        empty = self._hash({"open": []})
        for cls in ("fable", "opus", "sonnet"):
            with_task = self._hash({"open": [_task(f"#{cls}", model=cls)]})
            self.assertNotEqual(empty, with_task,
                                f"a {cls}-class task must enter the worker projection")

    def test_class_retag_does_not_flip_hash(self):
        # The projection item keys on task identity (id + blocked_by), not
        # model class — the same as the pre-P2 projectors. A re-tag
        # (sonnet->opus) doesn't change which lane must act (one unified
        # lane) and the dispatcher re-resolves the class per dispatch from
        # the slice, so it must NOT re-fire the worker.
        opus = self._hash({"open": [_task("#10", model="opus")]})
        sonnet = self._hash({"open": [_task("#10", model="sonnet")]})
        self.assertEqual(opus, sonnet)


class WorkerSkipLabelsDropPR(unittest.TestCase):
    """human:wip excludes a PR from the projection entirely."""

    def _hash(self, prs):
        return stable_hash(project_worker(_state(prs)))

    def test_human_wip_drops_pr(self):
        empty = self._hash([])
        with_wip = self._hash([_pr(101, labels=["fleet:needs-fix", "human:wip"])])
        self.assertEqual(empty, with_wip)


class OpusReviewerStableAcrossIrrelevantLabels(unittest.TestCase):
    """Opus-reviewer keys on fleet:has-nits / fleet:needs-fix (flag_labels).
    Other labels are either gating (REVIEW_SKIP_LABELS, removed from the
    projection entirely) or irrelevant — neither should re-flip the hash
    while the PR's flag_labels subset is unchanged."""

    def _hash(self, prs):
        return stable_hash(project_opus_reviewer(_state(prs)))

    def test_changes_made_does_not_flip_hash(self):
        # The worker pushes a fix, toggles fleet:changes-made on the
        # still-flagged PR. The reviewer's verdict label hasn't changed,
        # so the recheck shouldn't fire from this transition alone.
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:changes-made",
        ])])
        self.assertEqual(before, after)

    def test_authored_on_macos_does_not_flip_hash(self):
        before = self._hash([_pr(101, labels=["fleet:has-nits"])])
        after = self._hash([_pr(101, labels=[
            "fleet:has-nits", "fleet:authored-on-macos",
        ])])
        self.assertEqual(before, after)


class OpusReviewerActionableTransitionsFlipHash(unittest.TestCase):

    def _hash(self, prs):
        return stable_hash(project_opus_reviewer(_state(prs)))

    def test_has_nits_to_needs_fix_flips_hash(self):
        # Verdict escalation — reviewer should re-evaluate.
        before = self._hash([_pr(101, labels=["fleet:has-nits"])])
        after = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        self.assertNotEqual(before, after)

    def test_skip_label_added_flips_hash(self):
        # PR drops from the projection (REVIEW_SKIP_LABELS).
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:merger-cooldown",
        ])])
        self.assertNotEqual(before, after)


class AmendingClaimBarsReviewerPickup(unittest.TestCase):
    """A worker amending a fleet:needs-fix PR holds a host-suffixed
    fleet:amending-<host>-<agent> claim for the whole fix. Reviewers must
    not review a diff mid-amend; the claim is a REVIEW_SKIP_PREFIXES match.
    Closes the gap observed on PR #1316 (2026-05-28): the worker cleared
    fleet:needs-fix to start fixing, leaving the PR with no skip label, so
    a reviewer poll claimed and reviewed it mid-rewrite."""

    def _sonnet(self, prs):
        return project_sonnet_reviewer(_state(prs))

    def _opus(self, prs):
        return project_opus_reviewer(_state(prs))

    def test_amending_drops_pr_from_sonnet_reviewer(self):
        # needs-fix cleared, amend claim held -> not reviewable.
        held = self._sonnet([_pr(101, labels=["fleet:amending-mac-sonnet-fleet-1"])])
        self.assertEqual(held, [])

    def test_amending_plus_needs_fix_drops_pr_from_opus_reviewer(self):
        # Brief window before the worker removes needs-fix: still skipped.
        held = self._opus([_pr(101, labels=[
            "fleet:needs-fix", "fleet:amending-mac-opus-worker-1",
        ])])
        self.assertEqual(held, [])

    def test_release_then_changes_made_re_enters_sonnet_reviewer(self):
        # amend claim released, fleet:changes-made added -> recheck fires.
        before = stable_hash(self._sonnet([
            _pr(101, labels=["fleet:amending-mac-sonnet-fleet-1"])]))
        after = stable_hash(self._sonnet([
            _pr(101, labels=["fleet:changes-made"])]))
        self.assertNotEqual(before, after)
        self.assertEqual(len(self._sonnet([
            _pr(101, labels=["fleet:changes-made"])])), 1)

    def test_plain_needs_fix_unaffected(self):
        # No amend claim -> opus-reviewer still sees the flagged PR.
        self.assertEqual(len(self._opus([_pr(101, labels=["fleet:needs-fix"])])), 1)


class HumanDeferredDropsFromReviewers(unittest.TestCase):
    """fleet:human-deferred and fleet:needs-human PRs must not surface in
    reviewer projections (#1996 Gap 2).

    Without fleet:human-deferred in REVIEW_SKIP_LABELS, a no-verdict deferred
    PR re-enters the sonnet-reviewer pool, gets re-flagged fleet:needs-fix,
    and the worker re-escalates back to fleet:human-deferred indefinitely.
    """

    def _sonnet(self, prs):
        return project_sonnet_reviewer(_state(prs))

    def _opus(self, prs):
        return project_opus_reviewer(_state(prs))

    def test_human_deferred_drops_from_sonnet_reviewer(self):
        result = self._sonnet([_pr(101, labels=["fleet:human-deferred"])])
        self.assertEqual(result, [])

    def test_human_deferred_drops_from_opus_reviewer(self):
        result = self._opus([_pr(101, labels=[
            "fleet:needs-fix", "fleet:human-deferred",
        ])])
        self.assertEqual(result, [])

    def test_needs_human_drops_from_sonnet_reviewer(self):
        result = self._sonnet([_pr(101, labels=["fleet:needs-human"])])
        self.assertEqual(result, [])

    def test_needs_human_drops_from_opus_reviewer(self):
        result = self._opus([_pr(101, labels=[
            "fleet:needs-fix", "fleet:needs-human",
        ])])
        self.assertEqual(result, [])

    def test_plain_pr_without_defer_still_reviewed(self):
        # Regression: PRs without a skip label still surface normally.
        result = self._sonnet([_pr(101, labels=["fleet:changes-made"])])
        self.assertEqual(len(result), 1)


class PlanReviewExcludedFromTasksOpen(unittest.TestCase):
    """fetch_task_queue must skip fleet:plan-review issues so they don't
    appear in tasks.open[] as pickable by autonomous workers (#1996 Gap 1).

    A plan-review task has its plan posted and awaits human/architect
    approval — it must not trigger an autonomous dispatch that immediately
    no-ops because the issue isn't claimable.
    """

    def _fetch(self, issues_list):
        payload = json.dumps(issues_list)
        with patch.object(_mod, "run_capture", return_value=payload):
            return _mod.fetch_task_queue("jakildev/IrredenEngine")

    def _issue(self, number, extra_labels=()):
        return {
            "number": number,
            "title": f"task #{number}",
            "labels": [
                {"name": "fleet:queued"},
                {"name": "fleet:sonnet"},
                {"name": "human:approved"},
            ] + [{"name": l} for l in extra_labels],
            "body": "**Model:** sonnet\n",
        }

    def test_plan_review_task_not_in_open(self):
        result = self._fetch([self._issue(42, extra_labels=["fleet:plan-review"])])
        self.assertEqual(result["open"], [],
                         "fleet:plan-review task must not enter tasks.open[]")
        self.assertEqual(result["in_progress"], [])

    def test_needs_human_task_still_excluded(self):
        # Regression: existing fleet:needs-human skip must still work.
        result = self._fetch([self._issue(99, extra_labels=["fleet:needs-human"])])
        self.assertEqual(result["open"], [])

    def test_normal_task_still_enters_open(self):
        # Regression: ordinary sonnet task with no skip labels still projects.
        result = self._fetch([self._issue(55)])
        self.assertEqual(len(result["open"]), 1)
        self.assertEqual(result["open"][0]["id"], "#55")


if __name__ == "__main__":
    unittest.main()
