"""Tests for project_merger() in fleet-state-scout.

The merger's hash-input projection MUST be stable across the merger's
own self-toggled labels (fleet:merger-cooldown, fleet:awaiting-base,
fleet:needs-base-update, etc.). Without this invariant, the merger
self-triggers on every iteration: it toggles its cooldown label, scout
sees the projection hash flip, scout fires the merger again, repeat.

Observed live 2026-05-09: ~$0.30/iteration × 12/hour idle-fleet burn.

This harness locks in the behavior:
  - Same durable state -> same hash, regardless of cooldown labels.
  - The one action-relevant transition (a conflict arising on an
    approved PR, or such a PR appearing) DOES change the hash.
  - Merge-ready churn does not: an approved MERGEABLE PR appearing, or
    leaving the list when the human merges it, is not merger work and
    must never arm the lane (39 no-op iterations in one day, measured
    2026-09-17, came from exactly that).
  - Skip-labels (wip, blocker, needs-linux-smoke, etc.) drop a PR
    from the projection entirely so the merger isn't woken to find
    nothing to do.
  - The slice's `merger_candidates` names every PR tier-0 could hand to
    the LLM pass as a `merge:<repo>:<N>` target.
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
_merger_action_signal = _mod._merger_action_signal


def _signal(pr):
    return _merger_action_signal(set(pr["labels"]), pr.get("mergeable"),
                                 pr.get("baseRefName", "master"))


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
    """The core invariant: merger-toggled labels must NOT change the hash.
    Every pair below is a live projection row (a needs-resolve PR) on both
    sides, so the equality is about the label, not an empty projection."""

    def test_cooldown_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:merger-cooldown",
        ], mergeable="CONFLICTING")])
        self.assertEqual(_hash(before), _hash(after),
                         "cooldown toggle must not re-trigger merger")
        self.assertEqual(len(project_merger(after)), 1)

    def test_awaiting_base_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"], base="claude/parent")])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:awaiting-base",
        ], base="claude/parent")])
        self.assertEqual(_hash(before), _hash(after))

    def test_stacked_rebase_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:stacked-rebase",
            "fleet:changes-made",
        ], mergeable="CONFLICTING")])
        self.assertEqual(_hash(before), _hash(after))
        self.assertEqual(len(project_merger(after)), 1)

    def test_semantic_conflict_label_does_not_flip_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"],
                             mergeable="CONFLICTING")])
        after = _state([_pr(101, labels=[
            "fleet:approved", "fleet:semantic-conflict",
        ], mergeable="CONFLICTING")])
        self.assertEqual(_hash(before), _hash(after))


class ActionableTransitionsFlipHash(unittest.TestCase):
    """The one real merger transition — a conflict on an approved PR — must
    still trigger the merger (the positive fire)."""

    def test_approval_appears_on_conflicting_pr_flips_hash(self):
        before = _state([_pr(101, labels=[], mergeable="CONFLICTING")])
        after = _state([_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")])
        self.assertNotEqual(_hash(before), _hash(after))

    def test_mergeable_to_conflicting_flips_hash(self):
        before = _state([_pr(101, labels=["fleet:approved"], mergeable="MERGEABLE")])
        after = _state([_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")])
        self.assertNotEqual(_hash(before), _hash(after))

    def test_new_needs_resolve_pr_flips_hash(self):
        before = _state([])
        after = _state([_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")])
        self.assertNotEqual(_hash(before), _hash(after))
        self.assertEqual(project_merger(after)[0]["signal"], "needs-resolve")


class MergeReadyChurnDoesNotArm(unittest.TestCase):
    """An approved MERGEABLE PR is on the human's merge click, not merger
    work: its appearance and its departure leave the hash alone, so a
    human merge never launches a merger iteration that finds nothing."""

    def test_merge_ready_pr_appearing_does_not_flip_hash(self):
        before = _state([])
        after = _state([_pr(101, labels=["fleet:approved"])])
        self.assertEqual(_hash(before), _hash(after))
        self.assertEqual(project_merger(after), [])

    def test_merge_ready_pr_merging_does_not_flip_hash(self):
        # The human merges the merge-ready PR while the other stays needs-resolve: the
        # projection is the same one-row set before and after.
        before = _state([_pr(101, labels=["fleet:approved"]),
                         _pr(102, labels=["fleet:approved"], mergeable="CONFLICTING")])
        after = _state([_pr(102, labels=["fleet:approved"], mergeable="CONFLICTING")])
        self.assertEqual(_hash(before), _hash(after))

    def test_approval_appearing_on_mergeable_pr_does_not_flip_hash(self):
        before = _state([_pr(101, labels=[])])
        after = _state([_pr(101, labels=["fleet:approved"])])
        self.assertEqual(_hash(before), _hash(after))

    def test_merge_ready_stays_in_the_slice(self):
        # The role still reads it (its cooldown sweep, step 1); only the
        # trigger hash excludes it.
        out = slice_merger(_state([_pr(101, labels=["fleet:approved"])]))
        self.assertEqual([pr["number"] for pr in out["prs"]], [101])
        self.assertEqual(out["merger_candidates"], [])


class SkipLabelsRemovedFromProjection(unittest.TestCase):
    """Skip labels exclude a PR entirely so the merger isn't woken
    to find nothing to do. Each PR is CONFLICTING — a row the projection
    would carry without the label — so the label is what drops it."""

    def _dropped(self, label):
        empty = _state([])
        flagged = _state([_pr(101, labels=["fleet:approved", label],
                              mergeable="CONFLICTING")])
        self.assertEqual(_hash(empty), _hash(flagged),
                         f"{label} PRs should be invisible to merger")
        self.assertEqual(project_merger(flagged), [])

    def test_needs_linux_smoke_dropped(self):
        # PR with fleet:approved + fleet:needs-linux-smoke is a smoke-runner's
        # job, not the merger's. Should NOT appear in the projection.
        self._dropped("fleet:needs-linux-smoke")

    def test_needs_macos_smoke_dropped(self):
        self._dropped("fleet:needs-macos-smoke")

    def test_needs_windows_smoke_dropped(self):
        # The third smoke label must behave identically to linux/macos.
        self._dropped("fleet:needs-windows-smoke")

    def test_wip_dropped(self):
        self._dropped("fleet:wip")

    def test_blocker_dropped(self):
        self._dropped("fleet:blocker")


class HumanOwesFixLabelsDropped(unittest.TestCase):
    """An approved PR that also carries a human-owes-a-fix label must drop
    out of the projection so the merger isn't woken every scout tick to
    find nothing to do. These labels mirror role-merger.md step 3's skip
    list, keeping the projection consistent with the merge gate (#1533;
    observed live on game #144: fleet:approved + human:needs-fix projected
    merge-ready forever)."""

    def _dropped(self, *extra_labels, mergeable="CONFLICTING"):
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

    def test_fleet_gated_dropped(self):
        # A gated, human-only PR must never wake or be acted on by the merger.
        self._dropped("fleet:gated")

    def test_fleet_gated_conflicting_dropped(self):
        # Even CONFLICTING (the #1990 case): the conflict is in a gated file
        # the merger can't push, so it must skip rather than re-flag
        # semantic-conflict — that was the 11-pass thrash.
        self._dropped("fleet:gated", mergeable="CONFLICTING")

    def test_repo_agnostic_game_human_needs_fix_dropped(self):
        # The projection loops both repos; the skip must hold for game too
        # (game #144 was the live offender).
        empty = _state_eng_game(engine_prs=[], game_prs=[])
        flagged = _state_eng_game(
            engine_prs=[],
            game_prs=[_pr(144, labels=[
                "fleet:approved", "human:needs-fix",
            ], mergeable="CONFLICTING")],
        )
        self.assertEqual(_hash(empty), _hash(flagged))
        self.assertEqual(project_merger(flagged), [])


class SignalSemantics(unittest.TestCase):
    """Verify the action signal categorization matches the role doc."""

    def test_approved_mergeable_master_is_merge_ready(self):
        pr = _pr(101, labels=["fleet:approved"])
        self.assertEqual(_signal(pr), "merge-ready")
        # …and merge-ready is not a projection row.
        self.assertEqual(project_merger(_state([pr])), [])

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

    def test_approved_stacked_projects_nothing(self):
        # Native-stacked-PRs migration: an approved MERGEABLE PR whose base
        # is a feature branch is a native-stack child — GitHub owns its base
        # management and the human merges it from the stack UI, so the
        # merger has no action (the legacy "stacked-pending" signal retired).
        items = project_merger(_state([_pr(101, labels=[
            "fleet:approved",
        ], base="claude/parent")]))
        self.assertEqual(items, [])

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
        pr = _pr(101, labels=["fleet:approved", "fleet:human-deferred"],
                 mergeable="MERGEABLE")
        self.assertEqual(_signal(pr), "merge-ready")
        self.assertEqual(project_merger(_state([pr])), [])

    def test_gated_is_the_inverse_of_human_deferred(self):
        # The whole point of fleet:gated: where a CONFLICTING human-deferred PR
        # projects needs-resolve (merger acts), the same PR labeled fleet:gated
        # projects NOTHING — the merger can't push the gated conflict, so it
        # must stay hands-off (breaks the #1990 thrash at the source).
        deferred = project_merger(_state([_pr(101, labels=[
            "fleet:approved", "fleet:human-deferred",
        ], mergeable="CONFLICTING")]))
        gated = project_merger(_state([_pr(101, labels=[
            "fleet:approved", "fleet:gated",
        ], mergeable="CONFLICTING")]))
        self.assertEqual(deferred[0]["signal"], "needs-resolve")
        self.assertEqual(gated, [])


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
        # merge-ready — NOT as needs-resolve — so it is not a projection row.
        pr = _pr(101, labels=[
            "fleet:approved", "fleet:semantic-conflict",
            "fleet:stacked-rebase", "fleet:merger-cooldown",
        ], mergeable="MERGEABLE", base="master")
        self.assertEqual(_signal(pr), "merge-ready",
                         "stale fleet:semantic-conflict must not push a MERGEABLE "
                         "PR from merge-ready to needs-resolve")
        self.assertEqual(project_merger(_state([pr])), [])

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
        self.assertEqual(out["merger_candidates"], [])


class MergerCandidates(unittest.TestCase):
    """`merger_candidates` is the record table a `merge:<repo>:<N>` target
    resolves against: every PR tier-0 could hand to the LLM pass, tagged
    with why, and nothing another lane owns."""

    def _candidates(self, *prs, game=()):
        out = slice_merger(_state_eng_game(engine_prs=list(prs), game_prs=list(game)))
        return {(c["repo"], c["number"]): c["signal"] for c in out["merger_candidates"]}

    def test_needs_resolve_row_is_a_candidate(self):
        self.assertEqual(
            self._candidates(_pr(101, labels=["fleet:approved"], mergeable="CONFLICTING")),
            {("engine", 101): "needs-resolve"})

    def test_unapproved_conflict_is_an_llm_candidate(self):
        # role-merger.md step 3 admits it, so tier-0 can name it.
        self.assertEqual(self._candidates(_pr(101, labels=[], mergeable="CONFLICTING")),
                         {("engine", 101): "llm"})

    def test_unknown_row_is_an_llm_candidate(self):
        # The slice is one tick old: tier-0 may confirm it CONFLICTING and
        # name it before the scout sees the settled value.
        self.assertEqual(self._candidates(_pr(101, labels=[], mergeable="UNKNOWN")),
                         {("engine", 101): "llm"})

    def test_approved_stacked_child_is_a_candidate(self):
        self.assertEqual(
            self._candidates(_pr(101, labels=["fleet:approved"], base="claude/parent")),
            {("engine", 101): "stacked"})

    def test_merge_ready_and_unapproved_clean_are_not_candidates(self):
        self.assertEqual(self._candidates(_pr(101, labels=["fleet:approved"]),
                                          _pr(102, labels=[])), {})

    def test_other_lanes_prs_are_not_candidates(self):
        # Worker-owned (semantic conflict, amending), human-owned, parked.
        for labels in (["fleet:approved", "fleet:semantic-conflict"],
                       ["fleet:approved", "fleet:amending-mac-pool-2"],
                       ["fleet:approved", "human:needs-fix"],
                       ["fleet:approved", "fleet:gated"],
                       ["fleet:approved", "fleet:wip"]):
            with self.subTest(labels=labels):
                self.assertEqual(
                    self._candidates(_pr(101, labels=labels, mergeable="CONFLICTING")), {})

    def test_candidates_carry_repo_and_the_routing_labels(self):
        out = slice_merger(_state_eng_game(
            engine_prs=[],
            game_prs=[_pr(99, labels=["fleet:approved", "fleet:author-codex"],
                          mergeable="CONFLICTING")]))
        (record,) = out["merger_candidates"]
        self.assertEqual((record["repo"], record["number"]), ("game", 99))
        self.assertIn("fleet:author-codex", record["labels"])
        # An unrecognised state the slice would otherwise drop still rides
        # `prs` when it is a candidate.
        self.assertEqual([pr["number"] for pr in out["prs"]], [99])


if __name__ == "__main__":
    unittest.main()
