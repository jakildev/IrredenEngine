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
slice_worker = _mod.slice_worker
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


def _pr(num, *, labels=None, head="claude/feat", is_draft=False,
        base="master", mergeable="MERGEABLE"):
    return {
        "number": num,
        "title": f"T-{num}: feat",
        "headRefName": head,
        "baseRefName": base,
        "labels": sorted(labels or []),
        "isDraft": is_draft,
        "mergeable": mergeable,
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

    def test_stale_semantic_conflict_does_not_flip_hash(self):
        # A fleet:semantic-conflict label on a MERGEABLE PR is the #1654
        # fail-then-succeed race shape — stale, awaiting fleet-rebase's
        # cleanup sweep. It must not re-fire the worker; only a live
        # CONFLICTING PR is dispatch pressure (the SemanticConflictDispatch
        # tests). `_pr` defaults to mergeable=MERGEABLE.
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
    """human:wip / fleet:gated exclude a PR from the projection entirely."""

    def _hash(self, prs):
        return stable_hash(project_worker(_state(prs)))

    def test_human_wip_drops_pr(self):
        empty = self._hash([])
        with_wip = self._hash([_pr(101, labels=["fleet:needs-fix", "human:wip"])])
        self.assertEqual(empty, with_wip)

    def test_fleet_gated_drops_pr(self):
        # A gated, human-only PR must never dispatch a worker, even when it
        # also carries a normally-actionable feedback label (the #1990 case
        # carried fleet:semantic-conflict; a DEFER park can keep fleet:needs-fix
        # before the swap). fleet:gated wins unconditionally.
        empty = self._hash([])
        gated = self._hash([_pr(101, labels=["fleet:needs-fix", "fleet:gated"])])
        self.assertEqual(empty, gated)
        self.assertEqual(
            project_worker(_state([_pr(101, labels=["fleet:needs-fix", "fleet:gated"])])),
            [],
        )


def _sc_pr(num, **kwargs):
    kwargs.setdefault("labels", ["fleet:semantic-conflict"])
    kwargs.setdefault("mergeable", "CONFLICTING")
    return _pr(num, **kwargs)


class SemanticConflictDispatch(unittest.TestCase):
    """A claimable fleet:semantic-conflict PR IS worker dispatch pressure:
    it enters project_worker (so its appearance/resolution flips the hash and
    wakes the lane) and slice_worker's semantic_conflict_prs[] (so the class
    election counts it as one opus item). Its only consumer, role-worker step
    1c, runs exclusively in opus+-class iterations — without a projection
    item, no opus iteration ever launches when the opus queue is dry or
    host-locked, and conflicted PRs starve behind sonnet no-ops (engine
    #2417). Only a live CONFLICTING PR counts: the #1654 stale-label shape
    (label on a MERGEABLE PR) stays structurally excluded via the cached
    mergeable field, preserving what the old blanket label exclusion
    guaranteed."""

    def _items(self, prs):
        return [i for i in project_worker(_state(prs))
                if i["kind"] == "semantic_conflict"]

    def _slice(self, prs):
        return slice_worker(_state(prs))["semantic_conflict_prs"]

    def test_conflicting_pr_enters_projection_and_slice(self):
        prs = [_sc_pr(2417)]
        self.assertEqual(self._items(prs),
                         [{"kind": "semantic_conflict", "repo": "engine",
                           "pr": 2417}])
        slice_prs = self._slice(prs)
        self.assertEqual(len(slice_prs), 1)
        self.assertEqual(slice_prs[0]["number"], 2417)
        self.assertEqual(slice_prs[0]["repo"], "engine")

    def test_appearance_and_resolution_flip_hash(self):
        # Label lands (merger) -> wake; label cleared (worker resolved) ->
        # the item leaves. Both edges are real dispatch-relevant transitions.
        without = stable_hash(project_worker(_state([_pr(2417,
            mergeable="CONFLICTING")])))
        with_sc = stable_hash(project_worker(_state([_sc_pr(2417)])))
        self.assertNotEqual(without, with_sc)

    def test_stale_label_on_mergeable_pr_is_not_pressure(self):
        # #1654: fail-then-succeed race leaves the label on a MERGEABLE PR.
        # No projection item, no slice entry — the dispatch side must stay
        # blind to it until fleet-rebase's cleanup sweep clears the label.
        prs = [_sc_pr(2417, mergeable="MERGEABLE")]
        self.assertEqual(self._items(prs), [])
        self.assertEqual(self._slice(prs), [])

    def test_unknown_mergeable_fails_closed(self):
        # GitHub still computing mergeability -> not (yet) claimable; the
        # next scout tick picks it up once CONFLICTING is confirmed.
        prs = [_sc_pr(2417, mergeable="UNKNOWN")]
        self.assertEqual(self._items(prs), [])
        self.assertEqual(self._slice(prs), [])

    def test_resolving_claim_covers_the_pr(self):
        # A worker mid-resolution holds fleet:resolving-<host>-<agent>; the
        # PR is covered, not claimable — no second opus dispatch for it.
        prs = [_sc_pr(2417, labels=["fleet:semantic-conflict",
                                    "fleet:resolving-mac-worker-1"])]
        self.assertEqual(self._items(prs), [])
        self.assertEqual(self._slice(prs), [])

    def test_step1c_exclusion_labels_drop_the_pr(self):
        # role-worker step 1c's own pickup filter: wip/human-held or not yet
        # rebaseable against master. Mirror it exactly so a dispatched opus
        # worker never wakes to a conflict it would refuse on sight.
        for label in ("fleet:wip", "human:wip", "human:needs-fix",
                      "human:blocker", "fleet:awaiting-base",
                      "fleet:awaiting-upstream-review",
                      "fleet:fork-of-other-pr"):
            prs = [_sc_pr(2417, labels=["fleet:semantic-conflict", label])]
            self.assertEqual(self._items(prs), [], label)
            self.assertEqual(self._slice(prs), [], label)

    def test_gated_pr_stays_human_only(self):
        # fleet:gated wins unconditionally (loop-level exclusion shared with
        # the feedback path): the conflict sits in a file no agent can push.
        prs = [_sc_pr(2417, labels=["fleet:semantic-conflict", "fleet:gated"])]
        self.assertEqual(self._items(prs), [])
        self.assertEqual(self._slice(prs), [])

    def test_stacked_child_defers_to_conflicted_base(self):
        # Step 1c resolves the base first; the child is skipped until then,
        # and the base is the (single) item carrying the dispatch pressure.
        base = _sc_pr(2417, head="claude/base-feat")
        child = _sc_pr(2418, head="claude/child-feat", base="claude/base-feat")
        items = self._items([base, child])
        self.assertEqual([i["pr"] for i in items], [2417])
        self.assertEqual([p["number"] for p in self._slice([base, child])],
                         [2417])

    def test_stacked_child_with_clean_base_is_claimable(self):
        # A conflicted child whose base PR is NOT semantic-conflicted is
        # resolvable now — step 1c picks it up, so it counts.
        base = _pr(2417, head="claude/base-feat")
        child = _sc_pr(2418, head="claude/child-feat", base="claude/base-feat")
        self.assertEqual([i["pr"] for i in self._items([base, child])], [2418])

    def test_feedback_label_suppresses_conflict_pressure(self):
        # A conflicted PR that ALSO owes worker feedback/design-resume work
        # routes to the feedback lane ONLY — it must NOT also mint a
        # semantic_conflict item. The two lanes hold disjoint claim
        # namespaces (fleet:amending-* for the feedback fix vs
        # fleet:resolving-* for the rebase), so surfacing both lets a
        # (sonnet) nit-fix pane and an opus resolver pane force-push the same
        # head branch on one tick — a last-writer-wins race that silently
        # drops the conflict resolution or the feedback fix. Feedback comes
        # first (the PR's own stated priority); conflict pressure holds until
        # the feedback/design-resume label clears.
        for label in ("fleet:needs-fix", "fleet:has-nits",
                      "fleet:design-unblocked"):
            prs = [_sc_pr(2417, labels=["fleet:semantic-conflict", label])]
            # No semantic_conflict projection item or slice entry ...
            self.assertEqual(self._items(prs), [], label)
            self.assertEqual(self._slice(prs), [], label)
            # ... but the PR still surfaces in the feedback lane, so the fix
            # itself is not lost — only the parallel conflict dispatch is.
            kinds = sorted(i["kind"] for i in project_worker(_state(prs)))
            self.assertEqual(kinds, ["pr"], label)
            feedback = slice_worker(_state(prs))["feedback_prs"]
            self.assertEqual([p["number"] for p in feedback], [2417], label)


class SliceWorkerSkipLabelsDropPR(unittest.TestCase):
    """slice_worker is the dispatch slice a woken worker reads (distinct from
    project_worker, the hash-input that decides *whether* to wake). It must
    mirror project_worker's human:wip / fleet:gated exclusions so a woken
    worker never even surfaces a gated, human-only PR as a candidate."""

    def _feedback(self, prs):
        return slice_worker(_state(prs))["feedback_prs"]

    def test_human_wip_drops_pr(self):
        self.assertEqual(
            self._feedback([_pr(101, labels=["fleet:needs-fix", "human:wip"])]),
            [],
        )

    def test_fleet_gated_drops_pr(self):
        # Mirror of WorkerSkipLabelsDropPR.test_fleet_gated_drops_pr for the
        # slice: a gated PR carrying an otherwise-actionable feedback label
        # (#1990: fleet:gated alongside fleet:needs-fix) must not reach the
        # woken worker's candidate list. fleet:gated wins unconditionally.
        self.assertEqual(
            self._feedback([_pr(101, labels=["fleet:needs-fix", "fleet:gated"])]),
            [],
        )

    def test_plain_feedback_pr_still_surfaces(self):
        # Regression: a non-gated feedback PR must still reach the worker.
        result = self._feedback([_pr(101, labels=["fleet:needs-fix"])])
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["number"], 101)


class SliceWorkerSkipsGatedNeedsPlan(unittest.TestCase):
    """slice_worker drops human-gated needs-plan issues so the dispatcher's
    class resolver doesn't count them as plannable and spin a no-op worker
    (the needs-plan/no-plan limbo behind an idle 'worker found no work' fleet).
    A merely fleet:blocked needs-plan issue is still plannable and stays."""

    def _np(self, needs_plan):
        return [i["number"] for i in
                slice_worker(_state([], needs_plan=needs_plan))["needs_plan"]]

    def test_human_no_plan_dropped(self):
        self.assertEqual(
            self._np([{"number": 222,
                       "labels": ["fleet:needs-plan", "human:no-plan"]}]),
            [],
        )

    def test_human_owned_and_wip_dropped(self):
        self.assertEqual(
            self._np([
                {"number": 301, "labels": ["fleet:needs-plan", "human:owned"]},
                {"number": 302, "labels": ["fleet:needs-plan", "human:wip"]},
            ]),
            [],
        )

    def test_plain_needs_plan_surfaces(self):
        # Regression: a genuinely plannable issue must still reach the worker.
        self.assertEqual(
            self._np([{"number": 222, "labels": ["fleet:needs-plan"]}]),
            [222],
        )

    def test_blocked_needs_plan_still_plannable(self):
        # fleet:blocked is NOT a human gate — planning is independent of the
        # blocker, so a blocked needs-plan issue stays in the slice.
        self.assertEqual(
            self._np([{"number": 222,
                       "labels": ["fleet:needs-plan", "fleet:blocked"]}]),
            [222],
        )

    def test_mixed_set_keeps_only_plannable(self):
        self.assertEqual(
            self._np([
                {"number": 401, "labels": ["fleet:needs-plan", "human:no-plan"]},
                {"number": 402, "labels": ["fleet:needs-plan"]},
                {"number": 403, "labels": ["fleet:needs-plan", "human:owned"]},
            ]),
            [402],
        )


class ProjectWorkerSkipsGatedNeedsPlan(unittest.TestCase):
    """project_worker is the hash-input that decides *whether* to wake the
    dispatcher (distinct from slice_worker, the slice a woken worker reads).
    It must mirror slice_worker's human-gate skip (#2110 criterion 4): a
    needs-plan issue carrying human:no-plan / owned / wip must NOT flip the
    projection hash, or the late-opt-out window (human:no-plan added onto a
    still-needs-plan issue, before ingest strips the stale label) edge-triggers
    a phantom worker dispatch that slice_worker then filters to a no-op. The
    #2114 slice fix alone stopped the woken worker from claiming the issue but
    not the wake itself, since the dispatcher diffs this projection."""

    def _np(self, needs_plan):
        items = project_worker(_state([], needs_plan=needs_plan))
        return sorted(i["issue"] for i in items if i.get("kind") == "needs_plan")

    def test_human_no_plan_does_not_surface(self):
        self.assertEqual(
            self._np([{"number": 222,
                       "labels": ["fleet:needs-plan", "human:no-plan"]}]),
            [],
        )

    def test_human_owned_and_wip_do_not_surface(self):
        self.assertEqual(
            self._np([
                {"number": 301, "labels": ["fleet:needs-plan", "human:owned"]},
                {"number": 302, "labels": ["fleet:needs-plan", "human:wip"]},
            ]),
            [],
        )

    def test_plain_needs_plan_still_surfaces(self):
        self.assertEqual(
            self._np([{"number": 222, "labels": ["fleet:needs-plan"]}]),
            [222],
        )

    def test_blocked_needs_plan_still_surfaces(self):
        # fleet:blocked is NOT a human gate — planning is independent of the
        # blocker, so a blocked needs-plan issue still wakes the worker.
        self.assertEqual(
            self._np([{"number": 222,
                       "labels": ["fleet:needs-plan", "fleet:blocked"]}]),
            [222],
        )

    def test_gated_needs_plan_does_not_flip_hash(self):
        # The dispatch consequence: a gated needs-plan issue appearing must
        # leave the projection hash unchanged (no phantom wake), while a plain
        # one flips it (existing test_needs_plan_issue_appears_flips_hash).
        empty = stable_hash(project_worker(_state([])))
        gated = stable_hash(project_worker(_state([], needs_plan=[
            {"number": 222, "labels": ["fleet:needs-plan", "human:no-plan"]}])))
        self.assertEqual(empty, gated)

    def test_mixed_set_keeps_only_plannable(self):
        self.assertEqual(
            self._np([
                {"number": 401, "labels": ["fleet:needs-plan", "human:no-plan"]},
                {"number": 402, "labels": ["fleet:needs-plan"]},
                {"number": 403, "labels": ["fleet:needs-plan", "human:owned"]},
            ]),
            [402],
        )


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

    def test_gated_drops_from_sonnet_reviewer(self):
        result = self._sonnet([_pr(101, labels=["fleet:gated"])])
        self.assertEqual(result, [])

    def test_gated_drops_from_opus_reviewer(self):
        # Even with a reviewer-relevant flag label, a gated PR is human-only.
        result = self._opus([_pr(101, labels=[
            "fleet:has-nits", "fleet:gated",
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
        # Patch the REST seam (conditional_get), not run_capture: fetch_task_queue
        # migrated to _rest_list -> conditional_get, and a mock miss must not reach
        # the live API or the shared ~/.fleet ETag cache the scout uses (#2227).
        with patch.object(_mod, "conditional_get", return_value=(True, payload)):
            return _mod.fetch_task_queue("jakildev/IrredenEngine")

    def _issue(self, number, extra_labels=()):
        return {
            "number": number,
            "title": f"task #{number}",
            "labels": [
                {"name": "fleet:queued"},
                {"name": "fleet:sonnet"},
                {"name": "human:approved"},
            ] + [{"name": label} for label in extra_labels],
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


class NeedsGlHostAnnotation(unittest.TestCase):
    """fetch_task_queue stamps each task with needs_gl_host (#1998) so the
    dispatcher's claimability filter (fleet_task_class.py) can skip GL-only
    tasks on a Metal-only host. The flag flows into the worker slice via
    dict(task) and into state.json tasks.open[]."""

    def _fetch(self, issues_list):
        payload = json.dumps(issues_list)
        # Patch the REST seam (conditional_get), not run_capture: fetch_task_queue
        # migrated to _rest_list -> conditional_get, and a mock miss must not reach
        # the live API or the shared ~/.fleet ETag cache the scout uses (#2227).
        with patch.object(_mod, "conditional_get", return_value=(True, payload)):
            return _mod.fetch_task_queue("jakildev/IrredenEngine")

    def _issue(self, number, extra_labels=()):
        return {
            "number": number,
            "title": f"task #{number}",
            "labels": [
                {"name": "fleet:queued"},
                {"name": "fleet:opus"},
                {"name": "human:approved"},
            ] + [{"name": label} for label in extra_labels],
            "body": "**Model:** opus\n",
        }

    def test_needs_gl_host_true_when_labeled(self):
        result = self._fetch([self._issue(1937, extra_labels=["fleet:needs-gl-host"])])
        self.assertEqual(len(result["open"]), 1)
        self.assertTrue(result["open"][0]["needs_gl_host"])

    def test_needs_gl_host_false_when_absent(self):
        result = self._fetch([self._issue(1998)])
        self.assertEqual(len(result["open"]), 1)
        self.assertFalse(result["open"][0]["needs_gl_host"])

    def _issue_body(self, number, body):
        iss = self._issue(number)
        iss["body"] = body
        return iss

    def test_needs_gl_host_inferred_from_body_declaration(self):
        # #1969: body explicitly requires a Linux host but the label was
        # forgotten — inferred so a Metal pane skips it instead of churning.
        for phrase in (
            "This must run on a Linux host — references are per-backend.",
            "must be run on a Windows host to refresh windows-debug refs.",
            "Only runs on an OpenGL host.",
            "must run on a Linux/Windows host",
            # #2107: the "<GL|...>-host only" adjective form the verb pattern missed.
            "It is GL-host only and cannot be authored/validated on a macOS host.",
            "This is GL host only.",
            "OpenGL-host-only GTEST harness.",
            "Windows-host only — needs the native MSYS2 toolchain.",
        ):
            with self.subTest(phrase=phrase):
                result = self._fetch([self._issue_body(1969, f"**Model:** sonnet\n{phrase}")])
                self.assertTrue(result["open"][0]["needs_gl_host"],
                                f"expected inferred gl-host for: {phrase}")

    def test_needs_gl_host_not_inferred_from_passing_mention(self):
        # Precision guard: a passing mention or a macOS requirement must NOT
        # self-gate (over-gating would strand a mac-runnable task).
        for phrase in (
            "Tested on a Linux host previously.",
            "The Linux build is faster than macOS.",
            "must run on a macOS host",        # mac task — not a GL gate
            "compare against the linux-debug references",
            "the host only accepts one connection",   # "host only" without a GL prefix
            "macOS-host only",                         # mac adjective form — not a GL gate
        ):
            with self.subTest(phrase=phrase):
                result = self._fetch([self._issue_body(2000, f"**Model:** opus\n{phrase}")])
                self.assertFalse(result["open"][0]["needs_gl_host"],
                                 f"unexpected gl-host inference for: {phrase}")


if __name__ == "__main__":
    unittest.main()
