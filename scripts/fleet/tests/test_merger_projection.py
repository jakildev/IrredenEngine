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


class SignalSemantics(unittest.TestCase):
    """Verify the action signal categorization matches the role doc."""

    def test_approved_mergeable_master_is_merge_ready(self):
        items = project_merger(_state([_pr(101, labels=["fleet:approved"])]))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["signal"], "merge-ready")

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
