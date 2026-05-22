"""Tests for project_sonnet_author / project_opus_worker / project_opus_reviewer
in fleet-state-scout.

The author/worker/reviewer hash-input projections MUST be stable across
labels their role's decision logic does NOT key on (merger-toggled labels
like fleet:merger-cooldown, smoke-gate labels like fleet:needs-linux-smoke,
authoring-host markers like fleet:authored-on-macos, recheck-cycle
fleet:changes-made on a still-flagged PR, etc.). Without this invariant,
every label flip on a PR that happens to also have a feedback label
re-triggers every role; the role iterations spin up, apply their actual
filters (lane ownership, branch lock, smoke filters), find nothing
actionable, and exit. Observed live 2026-05-22 on PR #1047: scout fired
sonnet-author + opus-worker four times in 5 minutes while reviewers and
the merger flipped labels around the PR, with each iteration exiting clean.

Mirrors test_merger_projection.py — the merger's projection was already
hardened against this; the author/worker/reviewer projections were not.
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
project_sonnet_author = _mod.project_sonnet_author
project_opus_worker = _mod.project_opus_worker
project_opus_reviewer = _mod.project_opus_reviewer
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


class SonnetAuthorStableAcrossIrrelevantLabels(unittest.TestCase):
    """Labels the author's decision logic does NOT key on must not flip
    the projection hash on a PR that's already in the projection."""

    def _hash(self, prs):
        return stable_hash(project_sonnet_author(_state(prs)))

    def test_merger_cooldown_does_not_flip_hash(self):
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:merger-cooldown",
        ])])
        self.assertEqual(before, after,
                         "merger-cooldown toggle must not re-fire sonnet-author")

    def test_semantic_conflict_does_not_flip_hash(self):
        # Sonnet-author explicitly does NOT handle semantic-conflict
        # (that's opus-worker's lane), so flipping this label on a PR
        # that's in the projection for some other reason should be a no-op.
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


class SonnetAuthorActionableTransitionsFlipHash(unittest.TestCase):
    """Real changes the author should react to must still re-fire."""

    def _hash(self, prs):
        return stable_hash(project_sonnet_author(_state(prs)))

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


class SonnetAuthorSkipLabelsDropPR(unittest.TestCase):
    """human:wip excludes a PR from the projection entirely."""

    def _hash(self, prs):
        return stable_hash(project_sonnet_author(_state(prs)))

    def test_human_wip_drops_pr(self):
        empty = self._hash([])
        with_wip = self._hash([_pr(101, labels=["fleet:needs-fix", "human:wip"])])
        self.assertEqual(empty, with_wip)


class OpusWorkerStableAcrossIrrelevantLabels(unittest.TestCase):
    """Same invariant as sonnet-author. opus-worker also keys on
    fleet:design-unblocked (DESIGN_RESUME_LABELS) — that label IS
    relevant and must flip the hash."""

    def _hash(self, prs):
        return stable_hash(project_opus_worker(_state(prs)))

    def test_merger_cooldown_does_not_flip_hash(self):
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:merger-cooldown",
        ])])
        self.assertEqual(before, after)

    def test_semantic_conflict_does_not_flip_hash_on_flagged_pr(self):
        # Opus-worker step 1c handles semantic-conflict, but the
        # current projector includes a PR via FEEDBACK_LABELS, not
        # semantic-conflict. The semantic-conflict label flipping
        # while the feedback label is unchanged should not re-fire
        # the worker — the relevant trigger set hasn't changed.
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

    def test_changes_made_on_still_flagged_pr_does_not_flip(self):
        before = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        after = self._hash([_pr(101, labels=[
            "fleet:needs-fix", "fleet:changes-made",
        ])])
        self.assertEqual(before, after)


class OpusWorkerActionableTransitionsFlipHash(unittest.TestCase):
    """Real changes must still re-fire opus-worker."""

    def _hash(self, prs):
        return stable_hash(project_opus_worker(_state(prs)))

    def test_design_unblocked_appears_flips_hash(self):
        before = self._hash([_pr(101, labels=[])])
        after = self._hash([_pr(101, labels=["fleet:design-unblocked"])])
        self.assertNotEqual(before, after)

    def test_needs_fix_appears_flips_hash(self):
        before = self._hash([_pr(101, labels=[])])
        after = self._hash([_pr(101, labels=["fleet:needs-fix"])])
        self.assertNotEqual(before, after)

    def test_needs_plan_issue_appears_flips_hash(self):
        before = stable_hash(project_opus_worker(_state([])))
        after = stable_hash(project_opus_worker(
            _state([], needs_plan=[{"number": 222}])
        ))
        self.assertNotEqual(before, after)


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


if __name__ == "__main__":
    unittest.main()
