"""Tests for fleet_task_class.resolve — the per-dispatch model-class picker.

The dispatcher launches each worker iteration with the model class of the
work it's serving (a claude process can't change model/effort after
launch), so this resolution IS the per-task model routing. The invariants
that matter:

  - pickup priority mirrors the role docs: feedback PRs, then
    semantic-conflict PRs, then unblocked open tasks (oldest first — slices
    arrive sorted), then stackable `blocked` tasks, then needs_plan;
  - a semantic-conflict PR is one opus claimable item (role-worker step 1c is
    opus+-only; the scout pre-filters the slice's semantic_conflict_prs[]),
    so a conflicted PR generates opus dispatch pressure even when the task
    queue is empty or host-locked — before this tier the label had no
    dispatch pressure at all and conflicts starved behind sonnet no-op
    iterations (engine #2417);
  - feedback class derives from review severity labels (fable opt-in via
    fleet:fable on the PR, blocking labels -> opus, nits-only -> sonnet);
  - the fable concurrency cap skips fable items rather than idling the
    lane, and yields ``defer`` (keep trigger, dispatch nothing) when ONLY
    cap-blocked fable work remains — an empty result would dispatch the
    lane default, which the claim gate then refuses, burning an iteration;
  - a `blocked` task is claimable ONLY as a stack on its blocker's PR: it
    carries `stackable_blocker_pr` (set by the scout) when that base is valid,
    and a blocked task WITHOUT it is terminally unclaimable like `inflight_pr`,
    so a queue of only such tasks defers instead of falling through to a
    lane-default no-op dispatch;
  - a GL-only task (``needs_gl_host``) is unclaimable on a Metal-only host —
    terminal like `inflight_pr`, and the lane defers when it's the only
    work (#1998, the GL-vs-Metal host-capability gate);
  - per-task **Effort:** overrides beat class defaults;
  - the output carries ``count`` = claimable items of the elected class,
    which the dispatcher uses to cap its idle-pane fan-out, and ``plan`` = 1
    when that count includes the class's needs-plan yield (the dispatcher's
    cue to pre-claim a specific issue via ``--plan-pick``, #2197);
  - ``plan_pick`` lists the class's planning candidates as ordered
    ``repo:number`` lines in slice order (engine-first, oldest-first);
  - an empty/unroutable slice falls through to the lane default (covers
    reservation resumes and missing slices).
"""
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from fleet_task_class import feedback_pr_class, plan_pick, resolve  # noqa: E402


def _task(issue, model=None, effort=None, owner="free", blocked=False,
          inflight_pr=None, needs_gl_host=False, stackable_blocker_pr=None):
    return {"issue": issue, "model": model, "effort": effort,
            "owner": owner, "blocked": blocked, "inflight_pr": inflight_pr,
            "needs_gl_host": needs_gl_host,
            "stackable_blocker_pr": stackable_blocker_pr}


class FeedbackClassFromLabels(unittest.TestCase):
    def test_nits_only_routes_sonnet(self):
        self.assertEqual(feedback_pr_class(["fleet:has-nits", "fleet:approved"]),
                         "sonnet")

    def test_blocking_labels_route_opus(self):
        for label in ("fleet:needs-fix", "human:needs-fix", "human:blocker"):
            self.assertEqual(feedback_pr_class([label]), "opus")

    def test_fable_label_routes_fable(self):
        self.assertEqual(feedback_pr_class(["fleet:needs-fix", "fleet:fable"]),
                         "fable")

    def test_design_unblocked_routes_opus(self):
        # Tier-4 resume is opus+-only (FLEET-FEEDBACK-HANDLING); routing it to
        # sonnet dispatches a worker that then skips the PR -> no-op forever
        # (engine #1885). design-unblocked alone -> opus.
        self.assertEqual(
            feedback_pr_class(["fleet:design-unblocked", "fleet:wip"]), "opus")

    def test_design_unblocked_plus_fable_routes_fable(self):
        # fable is opus+ (role-worker.md) and handles tier 4 too, so the rare
        # both-tagged PR stays fable rather than downgrading to opus —
        # fleet:fable is checked before fleet:design-unblocked.
        self.assertEqual(
            feedback_pr_class(["fleet:design-unblocked", "fleet:fable"]), "fable")


class TaskResolution(unittest.TestCase):
    def test_oldest_claimable_task_wins(self):
        out = resolve({"tasks_open": [_task("#10", "sonnet"),
                                      _task("#11", "fable")]},
                      "opus", fable_blocked=False)
        # fable still queued -> more=1; one claimable sonnet item -> count=1.
        self.assertEqual(out, "sonnet high 1 1 0")

    def test_effort_override_beats_class_default(self):
        out = resolve({"tasks_open": [_task("#10", "opus", effort="medium")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus medium 0 1 0")

    def test_untagged_task_uses_lane_default(self):
        out = resolve({"tasks_open": [_task("#10", None)]},
                      "sonnet", fable_blocked=False)
        self.assertEqual(out, "sonnet high 0 1 0")

    def test_count_reflects_claimable_items_of_class(self):
        # Three unblocked opus tasks + one sonnet: the elected opus class
        # reports count=3 (the dispatcher caps its fan-out at 3 opus workers),
        # and more=1 because the sonnet item is a different servable class.
        out = resolve({"tasks_open": [_task("#10", "opus"), _task("#11", "opus"),
                                      _task("#12", "opus"), _task("#13", "sonnet")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 1 3 0")

    def test_owned_and_blocked_tasks_skipped(self):
        # Owned/blocked tasks are invisible: they don't dispatch AND they
        # don't count toward `more` or `count` (an unclaimable class is no
        # reason to hold the trigger open or inflate the fan-out cap).
        out = resolve({"tasks_open": [_task("#10", "fable", owner="worker-1"),
                                      _task("#11", "opus", blocked=True),
                                      _task("#12", "opus")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_inflight_pr_task_skipped_fable_behind_dispatches(self):
        # #1726 / the #1640 incident: a head-of-queue opus task whose own issue
        # already has an open (parked design-blocked) PR is non-actionable. The
        # scout tags it `inflight_pr`; the resolver must skip it AND not count
        # it toward `more`, so the fable task queued behind it dispatches
        # instead of the lane churning no-op opus iterations on the parked head.
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
            _task("#1695", "fable"),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 0")

    def test_inflight_pr_only_candidate_defers(self):
        # The parked task is the ONLY queue item: nothing a fresh worker can
        # claim, so the lane goes quiet (defer) rather than churning a
        # lane-default no-op dispatch every tick (#1726 DoD: "goes quiet").
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "defer")

    def test_inflight_head_with_nonstackable_blocked_defers(self):
        # Mixed slice: an inflight head + a plain `blocked` task with NO valid
        # stackable base (scout left off `stackable_blocker_pr`). Both are
        # terminally unclaimable for a fresh worker, so the lane defers rather
        # than firing a lane-default worker whose only "work" is re-deriving the
        # not-stackable verdict the scout already reached (the idle-fleet churn
        # this fix targets — opus panes dispatched with nothing to claim).
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
            _task("#1641", "opus", blocked=True),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "defer")

    def test_stackable_blocked_task_is_elected(self):
        # A `blocked` task WITH a scout-confirmed stackable base is claimable
        # (stack it on the blocker's PR): elected as a candidate, with its own
        # class and a count of 1 — not dropped to the '' fallthrough.
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
            _task("#1641", "opus", blocked=True,
                  stackable_blocker_pr={"number": 1638}),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_unblocked_elected_before_stackable(self):
        # Pickup priority: an unblocked task outranks a stackable `blocked` one
        # for class election even when the blocked task appears first in the
        # slice. Both opus here, so the distinction shows in count (2 claimable).
        out = resolve({"tasks_open": [
            _task("#10", "opus", blocked=True, stackable_blocker_pr={"number": 9}),
            _task("#11", "opus"),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 2 0")

    def test_inflight_pr_head_with_capped_fable_defers(self):
        # Head parked (skipped), only remaining work is cap-blocked fable ->
        # defer (keep trigger, dispatch nothing) rather than electing the
        # parked opus head.
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
            _task("#1695", "fable"),
        ]}, "opus", fable_blocked=True)
        self.assertEqual(out, "defer")

    def test_feedback_beats_tasks(self):
        out = resolve({"feedback_prs": [{"labels": ["fleet:has-nits"]}],
                       "tasks_open": [_task("#10", "opus")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "sonnet high 1 1 0")

    def test_needs_plan_prefers_fable(self):
        # Planning is architect-tier design work: it elects fable while the
        # fable cap has headroom (the same reasoning that puts the architect
        # panes on the fable class).
        out = resolve({"needs_plan": [{"number": 99}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 1")

    def test_needs_plan_falls_back_to_opus_when_fable_capped(self):
        # A saturated fable cap must DOWNGRADE planning to opus, not defer it —
        # the downgrade happens at election time so planning never stalls
        # behind a long fable implementation iteration.
        out = resolve({"needs_plan": [{"number": 99}]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus xhigh 0 1 1")

    def test_needs_plan_counts_once_regardless_of_backlog(self):
        # The lane plans one issue at a time (planning-claim lock + the
        # comment-presence early-out make same-tick siblings a no-op):
        # multiple needs_plan issues still yield a single candidate ->
        # count=1 (no fan-out of colliding planners).
        out = resolve({"needs_plan": [{"number": 99}, {"number": 100},
                                      {"number": 101}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 1")

    def test_needs_plan_sonnet_tagged_routes_sonnet(self):
        # A `fleet:sonnet`-tagged needs-plan issue is MECHANICAL: the sonnet
        # lane light-plans it (PLANNING-PROTOCOL.md §"Lightweight plan") instead
        # of burning fable/opus. Sonnet default effort (high), count=1.
        out = resolve({"needs_plan": [{"number": 99, "labels": ["fleet:sonnet"]}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "sonnet high 0 1 1")

    def test_needs_plan_untagged_still_prefers_fable(self):
        # No fleet:sonnet tag -> architect-tier design planning, unchanged.
        out = resolve({"needs_plan": [{"number": 99, "labels": ["render"]}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 1")

    def test_needs_plan_mixed_classes_elects_oldest_and_flags_more(self):
        # Oldest plannable issue is the sonnet-tagged mechanical one, so the
        # lane elects sonnet; the untagged (design-tier) issue keeps a fable
        # candidate alive -> more=1 so the dispatcher's cross-class fan-out
        # serves it on the fable/opus lane the same tick.
        out = resolve({"needs_plan": [
            {"number": 99, "labels": ["fleet:sonnet"]},
            {"number": 100, "labels": ["render"]},
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "sonnet high 1 1 1")

    def test_needs_plan_sonnet_tagged_unaffected_by_fable_cap(self):
        # A saturated fable cap downgrades DESIGN-tier planning to opus, but a
        # mechanical sonnet light-plan is a sonnet-lane job regardless.
        out = resolve({"needs_plan": [{"number": 99, "labels": ["fleet:sonnet"]}]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "sonnet high 0 1 1")

    def test_design_unblocked_feedback_resolves_opus(self):
        # The engine #1885 shape: a design-unblocked PR is the top feedback
        # item, every open task is parked/blocked, and opus needs_plan sits
        # behind it. The lane must dispatch opus (and clear it for tier 4),
        # not sonnet — pre-fix this resolved "sonnet ... 1" and starved both.
        # count=1: the feedback fix is the only opus item; the plannable
        # issue now elects fable (planning prefers fable), so it rides the
        # `more` flag — the kept trigger serves the planning dispatch on a
        # following tick.
        out = resolve({
            "feedback_prs": [{"labels": ["fleet:design-unblocked", "fleet:wip"]}],
            "tasks_open": [_task("#1882", "opus",
                                 inflight_pr={"number": 1885, "parked": True})],
            "needs_plan": [{"number": 1887}],
        }, "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 1 1 0")


class FableCap(unittest.TestCase):
    def test_cap_skips_fable_to_next_class(self):
        # more=0: cap-blocked fable is not servable, so it must not hold
        # the trigger open (the fable iteration finishing re-fires the
        # scout; the periodic safety re-arm covers the quiescent corner).
        out = resolve({"tasks_open": [_task("#10", "fable"),
                                      _task("#11", "opus")]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_only_capped_fable_defers(self):
        out = resolve({"tasks_open": [_task("#10", "fable")]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "defer")

    def test_uncapped_fable_dispatches_xhigh(self):
        out = resolve({"tasks_open": [_task("#10", "fable")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 0")


class EmptySlice(unittest.TestCase):
    def test_empty_slice_falls_through_to_lane_default(self):
        self.assertEqual(resolve({}, "opus", fable_blocked=False), "")
        self.assertEqual(resolve({"tasks_open": [], "feedback_prs": []},
                                 "sonnet", fable_blocked=False), "")

    def test_only_owned_task_falls_through(self):
        # An owner-held task is non-terminal (a reservation resume may still
        # need a lane-default dispatch), so it keeps the slice on '' rather
        # than deferring — distinct from the terminal inflight/blocked cases.
        out = resolve({"tasks_open": [_task("#10", "opus", owner="worker-1")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "")


class GlHostGate(unittest.TestCase):
    """#1998: a `needs_gl_host` task can't be built/run/verified on a
    Metal-only (macOS) host. The dispatcher's claimability filter skips it
    there — so a mac slice whose only open work is GL-only defers (goes
    quiet) instead of churning a lane-default no-op, while a Linux/Windows
    slice claims it normally. Host comes from `_current_host()`, driven here
    via the FLEET_TEST_HOST seam (same seam fleet-claim's derive_host uses)."""

    def setUp(self):
        self._saved_host = os.environ.get("FLEET_TEST_HOST")

    def tearDown(self):
        if self._saved_host is None:
            os.environ.pop("FLEET_TEST_HOST", None)
        else:
            os.environ["FLEET_TEST_HOST"] = self._saved_host

    def _resolve_on(self, host, slice_data, fable_blocked=False):
        os.environ["FLEET_TEST_HOST"] = host
        return resolve(slice_data, "opus", fable_blocked=fable_blocked)

    def test_gl_only_task_alone_on_mac_defers(self):
        # The #1937 churn shape: a GL-backend task is the only open work and
        # the pane is Metal-only -> defer (go quiet), not a lane-default no-op.
        out = self._resolve_on(
            "mac", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "defer")

    def test_gl_only_task_claimable_on_linux(self):
        out = self._resolve_on(
            "linux", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_gl_only_task_claimable_on_windows(self):
        out = self._resolve_on(
            "windows", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_mixed_mac_slice_dispatches_claimable_no_churn(self):
        # GL-only #1937-shaped head + a claimable opus task: the mac pane skips
        # the GL task (not counted toward `more`) and dispatches the claimable
        # one — no defer, no churn.
        out = self._resolve_on("mac", {"tasks_open": [
            _task("#1937", "opus", needs_gl_host=True),
            _task("#1998", "opus"),
        ]})
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_gl_only_plus_inflight_only_on_mac_defers(self):
        # All-terminal mix on a mac pane: one GL-only (host-terminal) + one
        # inflight (PR-terminal). Nothing claimable -> defer.
        out = self._resolve_on("mac", {"tasks_open": [
            _task("#1937", "opus", needs_gl_host=True),
            _task("#1640", "opus", inflight_pr={"number": 1700}),
        ]})
        self.assertEqual(out, "defer")

    def test_gl_only_head_with_nonstackable_blocked_defers(self):
        # GL-only head (host-terminal on mac) + a plain `blocked` host-compatible
        # task with no stackable base. Both terminal -> defer (no lane-default
        # no-op for the not-stackable blocked task).
        out = self._resolve_on("mac", {"tasks_open": [
            _task("#1937", "opus", needs_gl_host=True),
            _task("#1941", "opus", blocked=True),
        ]})
        self.assertEqual(out, "defer")

    def test_gl_only_head_with_stackable_blocked_dispatches(self):
        # GL-only head (host-terminal on mac) + a `blocked` host-compatible task
        # WITH a stackable base -> the stackable task is claimable and elected.
        out = self._resolve_on("mac", {"tasks_open": [
            _task("#1937", "opus", needs_gl_host=True),
            _task("#1941", "opus", blocked=True,
                  stackable_blocker_pr={"number": 1900}),
        ]})
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_unknown_host_is_fail_closed(self):
        # Fail-closed: an unrecognized host is treated as not GL-capable, so a
        # GL-only task is skipped (real dispatch hosts are always mac/linux/win).
        out = self._resolve_on(
            "unknown", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "defer")


class SemanticConflictDispatchPressure(unittest.TestCase):
    """Semantic-conflict PRs are opus-class claimable work slotted between
    feedback and task pickup (role-worker step 1c, opus+-classes-only). This
    tier is what gives the label dispatch pressure at all: before it, a
    conflicted PR was only resolved as a ride-along when opus queue work
    happened to be flowing, and starved when the opus lane was dry or
    host-locked (engine #2417 sat unclaimed while sonnet iterations
    no-op'd). The scout pre-filters the slice (CONFLICTING-gated per #1654,
    step-1c exclusions, resolving-claims, stacked children), so the resolver
    counts every entry as-is."""

    def setUp(self):
        self._saved_host = os.environ.get("FLEET_TEST_HOST")

    def tearDown(self):
        if self._saved_host is None:
            os.environ.pop("FLEET_TEST_HOST", None)
        else:
            os.environ["FLEET_TEST_HOST"] = self._saved_host

    @staticmethod
    def _sc(num):
        return {"number": num, "repo": "engine",
                "labels": ["fleet:semantic-conflict"]}

    def test_conflict_alone_elects_opus(self):
        out = resolve({"semantic_conflict_prs": [self._sc(2417)]},
                      "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_conflict_dispatches_when_all_tasks_host_locked(self):
        # The #2417 starvation shape: every open task is GL-locked on a
        # Metal-only host, so tasks alone would defer and no opus iteration
        # ever launches — the conflict must still dispatch opus.
        os.environ["FLEET_TEST_HOST"] = "mac"
        out = resolve({
            "tasks_open": [_task("#1938", "opus", needs_gl_host=True)],
            "semantic_conflict_prs": [self._sc(2417)],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 0")

    def test_feedback_still_elected_before_conflict(self):
        # Pickup priority mirrors the worker loop: feedback (step 1) before
        # conflicts (step 1c). The conflict stays servable -> more=1 so the
        # next tick serves the opus lane.
        out = resolve({
            "feedback_prs": [{"number": 11, "labels": ["fleet:has-nits"]}],
            "semantic_conflict_prs": [self._sc(2417)],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "sonnet high 1 1 0")

    def test_conflict_elected_before_open_tasks(self):
        # Step 1c runs before task pickup (step 2), so the conflict outranks
        # a claimable sonnet task; the task holds more=1.
        out = resolve({
            "semantic_conflict_prs": [self._sc(2417)],
            "tasks_open": [_task("#10", "sonnet")],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 1 1 0")

    def test_conflicts_and_opus_feedback_share_the_count(self):
        # Each conflict is one claimable opus item alongside opus feedback:
        # the fan-out cap must cover both, one worker per item.
        out = resolve({
            "feedback_prs": [{"number": 11, "labels": ["fleet:needs-fix"]}],
            "semantic_conflict_prs": [self._sc(2417), self._sc(2420)],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 3 0")

    def test_exclude_opus_defers_not_lane_default(self):
        # Cap-covered opus with only conflict work left -> defer (real work
        # exists, none servable now), never '' — a lane-default dispatch
        # would launch a sonnet iteration that skips step 1c by design.
        out = resolve({"semantic_conflict_prs": [self._sc(2417)]},
                      "sonnet", fable_blocked=False, exclude=["opus"])
        self.assertEqual(out, "defer")

    def test_no_host_gate_on_conflicts(self):
        # Unlike needs_gl_host tasks, a conflict resolves on any host — step
        # 1c build-verifies IRShapeDebug, which every fleet host builds
        # natively. mac included.
        os.environ["FLEET_TEST_HOST"] = "mac"
        out = resolve({"semantic_conflict_prs": [self._sc(2417)]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 0")


class ExcludeClasses(unittest.TestCase):
    """The dispatcher's cross-class fan-out: when an elected class is fully
    cap-covered but another class is claimable, it re-resolves with the covered
    class excluded to serve the next one instead of deferring the whole tick."""

    def test_exclude_elects_the_next_class(self):
        slice_data = {"tasks_open": [_task("#10", "opus"), _task("#11", "sonnet")]}
        # No exclude: opus is the elected (oldest) class, sonnet is `more`.
        self.assertEqual(resolve(slice_data, "opus", False), "opus xhigh 1 1 0")
        # Exclude opus -> sonnet is elected, nothing else servable -> more=0.
        self.assertEqual(resolve(slice_data, "opus", False, exclude=["opus"]),
                         "sonnet high 0 1 0")

    def test_exclude_all_claimable_defers_not_empty(self):
        # Excluding every claimable class must DEFER (there is real work, just
        # none the caller can serve now), never '' — '' would wrongly fall the
        # dispatcher through to a lane-default no-op dispatch.
        slice_data = {"tasks_open": [_task("#10", "opus"), _task("#11", "sonnet")]}
        self.assertEqual(resolve(slice_data, "opus", False,
                                 exclude=["opus", "sonnet"]), "defer")

    def test_exclude_irrelevant_class_is_noop(self):
        # Excluding a class with no claimable work changes nothing.
        slice_data = {"tasks_open": [_task("#10", "opus")]}
        self.assertEqual(resolve(slice_data, "opus", False, exclude=["fable"]),
                         "opus xhigh 0 1 0")

    def test_exclude_on_empty_slice_stays_empty(self):
        # No claimable work at all + an exclude -> '' (lane-default), not defer:
        # nothing was excluded, so the reservation-resume fallthrough stands.
        self.assertEqual(resolve({"tasks_open": []}, "opus", False,
                                 exclude=["opus"]), "")


class PlanFlag(unittest.TestCase):
    """The trailing ``plan`` token: 1 iff the ELECTED class's claimable count
    includes its needs-plan yield — the dispatcher then pre-claims a specific
    issue and hands the assignment to the dispatch (#2197)."""

    def test_task_plus_same_class_plan_sets_flag_and_counts_both(self):
        # An opus task + an opus-degraded plan (fable capped): opus elected,
        # count=2 (task + the planning slot), plan=1.
        out = resolve({"tasks_open": [_task("#10", "opus")],
                       "needs_plan": [{"number": 99}]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus xhigh 0 2 1")

    def test_other_class_plan_does_not_set_flag(self):
        # The plan candidate routes to fable; the elected class is opus (the
        # task) -> plan=0, fable plan rides `more` for a later tick.
        out = resolve({"tasks_open": [_task("#10", "opus")],
                       "needs_plan": [{"number": 99}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 1 1 0")


class PlanPick(unittest.TestCase):
    """`plan_pick` — the ordered repo:number candidate lines the dispatcher
    walks with `fleet-claim planning-claim` until one is granted (#2197)."""

    SLICE = {"needs_plan": [
        {"number": 90, "repo": "engine", "labels": ["fleet:sonnet"]},
        {"number": 99, "repo": "engine", "labels": []},
        {"number": 120, "repo": "engine", "labels": ["render"]},
        {"number": 7, "repo": "game", "labels": []},
    ]}

    def test_fable_class_picks_design_tier_in_slice_order(self):
        # Engine-first / oldest-first is the slice composition order —
        # plan_pick preserves it; the sonnet-tagged mechanical issue is not a
        # fable candidate.
        self.assertEqual(plan_pick(self.SLICE, "fable", False),
                         ["engine:99", "engine:120", "game:7"])

    def test_sonnet_class_picks_only_mechanical(self):
        self.assertEqual(plan_pick(self.SLICE, "sonnet", False), ["engine:90"])

    def test_fable_cap_degrades_design_tier_to_opus(self):
        # fable_blocked routes design-tier planning to opus (same `_plan_class`
        # degrade `resolve` applies), so the opus pick set is the fable set.
        self.assertEqual(plan_pick(self.SLICE, "opus", True),
                         ["engine:99", "engine:120", "game:7"])
        self.assertEqual(plan_pick(self.SLICE, "opus", False), [])

    def test_missing_repo_defaults_engine_and_missing_number_skipped(self):
        s = {"needs_plan": [{"number": 5}, {"labels": []}]}
        self.assertEqual(plan_pick(s, "fable", False), ["engine:5"])

    def test_empty_slice_yields_no_picks(self):
        self.assertEqual(plan_pick({}, "fable", False), [])


if __name__ == "__main__":
    unittest.main()
