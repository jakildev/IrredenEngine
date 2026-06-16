"""Tests for project_merger() in fleet-state-scout.

The merger's hash-input projection MUST be stable across the merger's
own self-toggled labels (fleet:merger-cooldown, fleet:awaiting-base,
fleet:needs-base-update, etc.). Without this invariant, the merger
self-triggers on every iteration: it toggles its cooldown label, scout
sees the projection hash flip, scout fires the merger again, repeat.

Observed live 2026-05-09: ~$0.30/iteration × 12/hour idle-fleet burn.

This harness locks in the behavior:
  - Same durable state -> same hash, regardless of cooldown labels.
  - Action-relevant transitions (approved becomes mergeable, conflict
    arises) DO change the hash.
  - Skip-labels (wip, blocker, needs-linux-smoke, etc.) drop a PR
    from the projection entirely so the merger isn't woken to find
    nothing to do.
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
project_merger = _mod.project_merger
project_worker = _mod.project_worker
slice_merger = _mod.slice_merger
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
        "title": f"T-{num}: feat",
        "headRefName": head,
        "baseRefName": base,
        "labels": sorted(labels or []),
        "mergeable": mergeable,
        "author": author,
    }


def _hash(state):
    return stable_hash(project_merger(state))


class StableAcrossSelfToggledLabels(unittest.TestCase):
    """The core invariant: merger-toggled labels must NOT change the hash."""

    def test_cooldown_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"])])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:merger-cooldown",
        ])])
        self.assertEqual(_hash(before), _hash(after),
                         "cooldown toggle must not re-trigger merger")

    def test_awaiting_base_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"], base="claude/parent")])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:awaiting-base",
        ], base="claude/parent")])
        self.assertEqual(_hash(before), _hash(after))

    def test_stacked_rebase_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"])])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:stacked-rebase",
            "fleet:changes-made",
        ])])
        self.assertEqual(_hash(before), _hash(after))

    def test_semantic_conflict_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"],
                             mergeable="CONFLICTING")])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:semantic-conflict",
        ], mergeable="CONFLICTING")])
        self.assertEqual(_hash(before), _hash(after))


class ActionableTransitionsFlipHash(unittest.TestCase):
    """Real state changes must still trigger the merger."""

    def test_approval_appears_flips_hash(self):
        before = _state([_pr(101, labels=[])])
        after = _state([_pr(101, labels=["fleet:approved"])])
        self.assertNotEqual(_hash(before), _hash(after))

    def test_mergeable_to_conflicting_flips_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"], mergeable="MERGEABLE")])
        after = _state([_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")])
        self.assertNotEqual(_hash(before), _hash(after))

    def test_new_approved_pr_flips_hash(self):
        before = _state([])
        after = _state([_pr(101, labels=["fleet:approved"])])
        self.assertNotEqual(_hash(before), _hash(after))


class SkipLabelsRemovedFromProjection(unittest.TestCase):
    """Skip labels exclude a PR entirely so the merger isn't woken
    to find nothing to do."""

    def test_needs_linux_smoke_dropped(self):
        # PR with fleet:approved + fleet:needs-linux-smoke is a smoke-runner's
        # job, not the merger's. Should NOT appear in the projection.
        empty = _state([])
        with_smoke = _state([_pr(101, labels=[
            "fleet:approved", "fleet:needs-linux-smoke",
        ])])
        self.assertEqual(_hash(empty), _hash(with_smoke),
                         "needs-linux-smoke PRs should be invisible to merger")

    def test_needs_macos_smoke_dropped(self):
        empty = _state([])
        with_smoke = _state([_pr(101, labels=[
            "fleet:approved", "fleet:needs-macos-smoke",
        ])])
        self.assertEqual(_hash(empty), _hash(with_smoke))

    def test_wip_dropped(self):
        empty = _state([])
        with_wip = _state([_pr(101, labels=[
            "fleet:approved", "fleet:wip",
        ])])
        self.assertEqual(_hash(empty), _hash(with_wip))

    def test_blocker_dropped(self):
        empty = _state([])
        with_blocker = _state([_pr(101, labels=[
            "fleet:approved", "fleet:blocker",
        ])])
        self.assertEqual(_hash(empty), _hash(with_blocker))


class HumanOwesFixLabelsDropped(unittest.TestCase):
    """An approved PR that also carries a human-owes-a-fix label must drop
    out of the projection so the merger isn't woken every scout tick to
    find nothing to do. These labels mirror role-merger.md step 3's skip
    list, keeping the projection consistent with the merge gate (#1533;
    observed live on game #144: fleet:approved + human:needs-fix projected
    merge-ready forever)."""

    def _dropped(self, *extra_labels, mergeable="MERGEABLE"):
        empty = _state([])
        flagged = _state([_pr(101, labels=[
            "fleet:approved", *extra_labels,
        ], mergeable=mergeable)])
        self.assertEqual(_hash(empty), _hash(flagged))
        self.assertEqual(project_merger(flagged), [])

    def test_human_needs_fix_dropped(self):
        self._dropped("human:needs-fix")

    def test_human_needs_fix_conflicting_dropped(self):
        # Even CONFLICTING: the human owes a fix, so the merger must not
        # try to resolve — the author will force-push when addressing it,
        # invalidating any rebase the merger does now.
        self._dropped("human:needs-fix", mergeable="CONFLICTING")

    def test_human_blocker_dropped(self):
        self._dropped("human:blocker")

    def test_fleet_needs_fix_dropped(self):
        self._dropped("fleet:needs-fix")

    def test_human_re_review_dropped(self):
        self._dropped("human:re-review")

    def test_repo_agnostic_game_human_needs_fix_dropped(self):
        # The projection loops both repos; the skip must hold for game too
        # (game #144 was the live offender).
        empty = _state_eng_game(engine_prs=[], game_prs=[])
        flagged = _state_eng_game(
            engine_prs=[],
            game_prs=[_pr(144, labels=[
                "fleet:approved", "human:needs-fix",
            ])],
        )
        self.assertEqual(_hash(empty), _hash(flagged))
        self.assertEqual(project_merger(flagged), [])


class SignalSemantics(unittest.TestCase):
    """Verify the action signal categorization matches the role doc."""

    def test_approved_mergeable_master_is_merge_ready(self):
        items = project_merger(_state([_pr(101, labels=["fleet:approved"])]))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["signal"], "merge-ready")

    def test_approved_needs_fix_not_merge_ready(self):
        # Acceptance criterion #1533: fleet:approved + human:needs-fix must
        # yield no signal (not merge-ready) so it drops out of the merger.
        items = project_merger(_state([_pr(101, labels=[
            "fleet:approved", "human:needs-fix",
        ])]))
        self.assertEqual(items, [])

    def test_approved_conflicting_is_needs_resolve(self):
        items = project_merger(_state([_pr(101, labels=["fleet:approved"],
                                            mergeable="CONFLICTING")]))
        self.assertEqual(items[0]["signal"], "needs-resolve")

    def test_approved_stacked_is_stacked_pending(self):
        items = project_merger(_state([_pr(101, labels=[
            "fleet:approved", "fleet:stacked",
        ], base="claude/parent")]))
        self.assertEqual(items[0]["signal"], "stacked-pending")

    def test_unapproved_is_dropped(self):
        items = project_merger(_state([_pr(101, labels=[])]))
        self.assertEqual(items, [])

    def test_human_deferred_conflicting_is_needs_resolve(self):
        # Re-scoped (PR #1712): fleet:human-deferred marks a deferred
        # *review concern* tracked in a follow-up issue, NOT a conflict
        # handoff. A conflicting deferred PR is flagged like any approved
        # PR; the opus-worker resolves it and drops the label (new commits
        # invalidate the deferral). The old "invisible to merger" behavior
        # stranded the PR's conflict from resolution.
        items = project_merger(_state([_pr(101, labels=[
            "fleet:approved", "fleet:human-deferred",
        ], mergeable="CONFLICTING")]))
        self.assertEqual(items[0]["signal"], "needs-resolve")

    def test_human_deferred_mergeable_is_merge_ready(self):
        # A MERGEABLE deferred PR is just an approved PR awaiting the
        # human's merge/re-flag decision — treated like any approved PR.
        items = project_merger(_state([_pr(101, labels=[
            "fleet:approved", "fleet:human-deferred",
        ], mergeable="MERGEABLE")]))
        self.assertEqual(items[0]["signal"], "merge-ready")


class FailThenSucceedStackedRebase(unittest.TestCase):
    """After a stacked-rebase retry that succeeds (#1654), the failed first
    pass's fleet:semantic-conflict label may still be present if the success
    path hasn't cleaned it up yet.

    Invariants the fix relies on:
    1. A stale fleet:semantic-conflict on an otherwise-MERGEABLE PR must still
       project as merge-ready, not needs-resolve — the merger must NOT loop
       back into conflict-resolution mode for an already-clean PR.
    2. Removing the stale label (once the success path cleans it up) must NOT
       retrigger the merger — fleet:semantic-conflict is intentionally excluded
       from the projection hash.
    3. The stale label must NOT surface in the opus-worker projection, which
       would trigger a false step-1c resolution pass on an already-clean PR.
    """

    def test_stale_semantic_conflict_on_mergeable_projects_merge_ready(self):
        # fail-then-succeed: PR has fleet:stacked-rebase (set by the successful
        # second pass) but also stale fleet:semantic-conflict (set by the failed
        # first pass, not yet cleared). The merger must still classify it as
        # merge-ready — NOT as needs-resolve.
        pr = _pr(101, labels=[
            "fleet:approved", "fleet:semantic-conflict",
            "fleet:stacked-rebase", "fleet:merger-cooldown",
        ], mergeable="MERGEABLE", base="master")
        items = project_merger(_state([pr]))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["signal"], "merge-ready",
                         "stale fleet:semantic-conflict must not push a MERGEABLE "
                         "PR from merge-ready to needs-resolve")

    def test_removing_stale_semantic_conflict_does_not_flip_hash(self):
        # Once the merger's success path removes the stale label (per #1654
        # fix), the merger must NOT re-dispatch — removing fleet:semantic-conflict
        # from an otherwise-stable PR must not change the projection hash.
        with_stale = _state([_pr(101, labels=[
            "fleet:approved", "fleet:stacked-rebase", "fleet:semantic-conflict",
        ])])
        without_stale = _state([_pr(101, labels=[
            "fleet:approved", "fleet:stacked-rebase",
        ])])
        self.assertEqual(
            _hash(with_stale), _hash(without_stale),
            "removing stale fleet:semantic-conflict must not retrigger the merger",
        )

    def test_stale_semantic_conflict_not_flagged_to_worker(self):
        # A stale fleet:semantic-conflict on a clean MERGEABLE PR must not
        # surface in the worker projection — worker step 1c looks
        # for fleet:semantic-conflict PRs to resolve, but this one has no
        # real conflict. The label is NOT in _WORKER_RELEVANT_LABELS so
        # this is a structural guarantee, not just current behavior.
        pr = _pr(101, labels=[
            "fleet:approved", "fleet:semantic-conflict", "fleet:stacked-rebase",
        ], mergeable="MERGEABLE", base="master")
        state = _state([pr])
        items = project_worker(state)
        pr_items = [i for i in items if i.get("kind") == "pr"]
        self.assertEqual(pr_items, [],
                         "stale fleet:semantic-conflict must not appear in "
                         "worker projection (would trigger false step-1c "
                         "resolution pass on an already-clean PR)")


class MergerCoversBothRepos(unittest.TestCase):
    """The merger handles engine AND game PRs (the game pass added after the
    engine-only v1). A CONFLICTING approved game PR must be visible to the
    merger — the gap that left game #99 rotting with no actor."""

    def test_game_conflict_in_projection(self):
        items = project_merger(_state_eng_game(
            engine_prs=[],
            game_prs=[_pr(99, labels=["fleet:approved"], mergeable="CONFLICTING")],
        ))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["repo"], "game")
        self.assertEqual(items[0]["signal"], "needs-resolve")

    def test_game_conflict_in_slice_tagged_repo(self):
        out = slice_merger(_state_eng_game(
            engine_prs=[_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")],
            game_prs=[_pr(99, labels=["fleet:approved"], mergeable="CONFLICTING")],
        ))
        repos = sorted(pr["repo"] for pr in out["prs"])
        self.assertEqual(repos, ["engine", "game"])

    def test_slice_excludes_clean_unapproved_game_pr(self):
        # Same filter as engine: a MERGEABLE, unapproved game PR is not the
        # merger's business and must not bloat the slice.
        out = slice_merger(_state_eng_game(
            engine_prs=[],
            game_prs=[_pr(99, labels=[], mergeable="MERGEABLE")],
        ))
        self.assertEqual(out["prs"], [])


if __name__ == "__main__":
    unittest.main()
