"""Tests for fleet_task_class.resolve — the per-dispatch model-class picker.

The dispatcher launches each worker iteration with the model class of the
work it's serving (a claude process can't change model/effort after
launch), so this resolution IS the per-task model routing. The invariants
that matter:

  - dispatch priority puts conflicts first: semantic-conflict PRs, then
    feedback PRs, then unblocked open tasks (oldest first — slices arrive
    sorted), then stackable `blocked` tasks, then needs_plan. This is the
    dispatch order, not the role doc's step numbering — a worker handed a
    target works it and skips the scans, so role-worker.md running step 1c
    after step 1 does not make feedback outrank conflicts here;
  - a semantic-conflict PR is one opus claimable item (role-worker step 1c is
    opus+-only; the scout pre-filters the slice's semantic_conflict_prs[]),
    so a conflicted PR generates opus dispatch pressure even when the task
    queue is empty or host-locked;
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
    work (the GL-vs-Metal host-capability gate);
  - that same host gate covers **feedback PRs** (matched on the raw
    ``fleet:needs-gl-host`` label, since PR records carry no derived field):
    role-worker step 1 skips them on a Metal-only host and `amending-claim`
    refuses the claim, so counting one would inflate the elected class on
    exactly the hosts that can't serve it — and via the design-unblocked→opus
    route that phantom item would win the election every tick and starve the
    other lanes behind the concurrency cap. The quiet path must widen with it,
    or the gate only relocates the no-op it removes;
  - a feedback PR inherits its closed issue's ``**Host:**`` pin: the scout's
    slice_worker stamps ``needs_host`` on the slice record from the same-repo
    task it closes, so a macOS-only residual is not elected on windows/linux;
  - per-task **Effort:** overrides beat class defaults; work dispatches
    default to effort ``high`` for every class, while planning yields carry
    ``xhigh`` (``PLAN_EFFORT`` — plans are the fleet's design surface);
  - the output carries ``count`` = claimable items of the elected class,
    which the dispatcher uses to cap its idle-pane fan-out, and ``plan`` = 1
    when that count includes the class's needs-plan yield;
  - ``pick`` / ``pick_role`` list a lane's ordered dispatch targets
    (``<kind>:<repo>:<N>[:<extra>]``) for the dispatcher's claim walk, and
    ``plan_pick`` the class's planning targets in slice order (engine-first,
    oldest-first);
  - an empty/unroutable slice falls through to the lane default (covers
    reservation resumes and missing slices).
"""
import importlib.machinery
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
import fleet_task_class  # noqa: E402
from fleet_task_class import _host_incompatible, feedback_pr_class, plan_pick, resolve  # noqa: E402


def pick(*args, **kwargs):
    """`fleet_task_class.pick`, resolved lazily — a module-level import
    raises ImportError against a pre-target resolver and kills the suite
    before unittest prints its `Ran N tests` line, which is exactly the line
    `fleet-positive-control` scores. Deferred, a missing function is an
    ordinary test error the control can count."""
    impl = getattr(fleet_task_class, "pick", None)
    if impl is None:
        raise AssertionError("fleet_task_class has no pick (pre-target tree)")
    return impl(*args, **kwargs)


def pick_role(*args, **kwargs):
    impl = getattr(fleet_task_class, "pick_role", None)
    if impl is None:
        raise AssertionError("fleet_task_class has no pick_role (pre-target tree)")
    return impl(*args, **kwargs)

# The scout's slice_worker is the pre-filter that feeds resolve(). Loaded here
# (not only in test_worker_projection.py) to assert the cross-module contract:
# a fleet:semantic-conflict PR that ALSO carries a worker feedback label is
# dropped from semantic_conflict_prs[] so it reaches resolve() as feedback-only.
_SCOUT = Path(__file__).parent.parent / "fleet-state-scout"
_scout_loader = importlib.machinery.SourceFileLoader(
    "fleet_state_scout", str(_SCOUT))
_scout_spec = importlib.util.spec_from_loader("fleet_state_scout", _scout_loader)
_scout_mod = importlib.util.module_from_spec(_scout_spec)
_scout_loader.exec_module(_scout_mod)
slice_worker = _scout_mod.slice_worker


def _task(issue, model=None, effort=None, owner="free", blocked=False,
          inflight_pr=None, needs_gl_host=False, stackable_blocker_pr=None,
          backend_symmetric=False, needs_host=None):
    return {"issue": issue, "model": model, "effort": effort,
            "owner": owner, "blocked": blocked, "inflight_pr": inflight_pr,
            "needs_gl_host": needs_gl_host,
            "needs_host": needs_host,
            "backend_symmetric": backend_symmetric,
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
        # sonnet dispatches a worker that then skips the PR -> no-op forever.
        # design-unblocked alone -> opus.
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
        self.assertEqual(out, "opus high 1 3 0")

    def test_owned_and_blocked_tasks_skipped(self):
        # Owned/blocked tasks are invisible: they don't dispatch AND they
        # don't count toward `more` or `count` (an unclaimable class is no
        # reason to hold the trigger open or inflate the fan-out cap).
        out = resolve({"tasks_open": [_task("#10", "fable", owner="worker-1"),
                                      _task("#11", "opus", blocked=True),
                                      _task("#12", "opus")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus high 0 1 0")

    def test_inflight_pr_task_skipped_fable_behind_dispatches(self):
        # A head-of-queue opus task whose own issue already has an open
        # (parked design-blocked) PR is non-actionable. The scout tags it
        # `inflight_pr`; the resolver must skip it AND not count it toward
        # `more`, so the fable task queued behind it dispatches instead of the
        # lane churning no-op opus iterations on the parked head.
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
            _task("#1695", "fable"),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "fable high 0 1 0")

    def test_inflight_pr_only_candidate_defers(self):
        # The parked task is the ONLY queue item: nothing a fresh worker can
        # claim, so the lane goes quiet (defer) rather than churning a
        # lane-default no-op dispatch every tick.
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "defer")

    def test_inflight_head_with_nonstackable_blocked_defers(self):
        # Mixed slice: an inflight head + a plain `blocked` task with NO valid
        # stackable base (scout left off `stackable_blocker_pr`). Both are
        # terminally unclaimable for a fresh worker, so the lane defers rather
        # than firing a lane-default worker whose only "work" is re-deriving the
        # not-stackable verdict the scout already reached (opus panes dispatched
        # with nothing to claim).
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
        self.assertEqual(out, "opus high 0 1 0")

    def test_unblocked_elected_before_stackable(self):
        # Pickup priority: an unblocked task outranks a stackable `blocked` one
        # for class election even when the blocked task appears first in the
        # slice. Both opus here, so the distinction shows in count (2 claimable).
        out = resolve({"tasks_open": [
            _task("#10", "opus", blocked=True, stackable_blocker_pr={"number": 9}),
            _task("#11", "opus"),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "opus high 0 2 0")

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

    def test_needs_plan_untagged_plans_at_opus(self):
        # No class label and no `**Model:**` class (`model` unset) resolves to
        # opus, the same default a task gets — NOT fable. Planning inherits the
        # task's declared class; only a fable declaration buys an
        # architect-tier plan. PLAN_EFFORT keeps xhigh for the opus lane.
        out = resolve({"needs_plan": [{"number": 99}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 1")

    def test_needs_plan_fable_tagged_routes_fable(self):
        # The explicit high-stakes signal: a fleet:fable needs-plan issue is
        # the one shape that still elects architect-tier planning.
        out = resolve({"needs_plan": [{"number": 99, "labels": ["fleet:fable"]}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 1")

    def test_needs_plan_falls_back_to_opus_when_fable_capped(self):
        # A saturated fable cap must DOWNGRADE fable-tagged planning to opus,
        # not defer it — the downgrade happens at election time so planning
        # never stalls behind a long fable implementation iteration.
        out = resolve({"needs_plan": [{"number": 99, "labels": ["fleet:fable"]}]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus xhigh 0 1 1")

    def test_needs_plan_model_field_fable_routes_fable(self):
        # The usual fable opt-in: `**Model:** fable` in the body (stamped as
        # `model` by the scout) with NO class label — ingest bounced the issue
        # to needs-plan before it could stamp one.
        out = resolve({"needs_plan": [{"number": 3661, "model": "fable"}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 1")

    def test_needs_plan_model_field_fable_degrades_when_capped(self):
        out = resolve({"needs_plan": [{"number": 3661, "model": "fable"}]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus xhigh 0 1 1")

    def test_needs_plan_model_field_opus_plans_at_opus(self):
        out = resolve({"needs_plan": [{"number": 99, "model": "opus"}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 1")

    def test_needs_plan_model_field_sonnet_routes_sonnet(self):
        out = resolve({"needs_plan": [{"number": 99, "model": "sonnet"}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "sonnet high 0 1 1")

    def test_needs_plan_class_label_beats_model_field(self):
        # Label first, body second — the order the scout resolves a task's
        # class in and `fleet-claim` checks it in.
        out = resolve({"needs_plan": [
            {"number": 99, "labels": ["fleet:opus"], "model": "fable"}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 1")
        out = resolve({"needs_plan": [
            {"number": 99, "labels": ["fleet:fable"], "model": "sonnet"}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0 1 1")

    def test_needs_plan_sonnet_label_beats_fable_label(self):
        # Both class labels: the mechanical light-plan wins.
        out = resolve({"needs_plan": [
            {"number": 99, "labels": ["fleet:fable", "fleet:sonnet"]}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "sonnet high 0 1 1")

    def test_needs_plan_counts_once_regardless_of_backlog(self):
        # The lane plans one issue at a time (planning-claim lock + the
        # comment-presence early-out make same-tick siblings a no-op):
        # multiple needs_plan issues still yield a single candidate ->
        # count=1 (no fan-out of colliding planners).
        out = resolve({"needs_plan": [{"number": 99}, {"number": 100},
                                      {"number": 101}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 1")

    def test_needs_plan_sonnet_tagged_routes_sonnet(self):
        # A `fleet:sonnet`-tagged needs-plan issue is MECHANICAL: the sonnet
        # lane light-plans it (PLANNING-PROTOCOL.md §"Lightweight plan") instead
        # of burning fable/opus. Sonnet default effort (high), count=1.
        out = resolve({"needs_plan": [{"number": 99, "labels": ["fleet:sonnet"]}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "sonnet high 0 1 1")

    def test_needs_plan_untagged_plans_at_opus_not_fable(self):
        # A non-class label and no `**Model:**` class declare nothing, so this
        # plans at opus; only a fable declaration buys architect-tier.
        out = resolve({"needs_plan": [{"number": 99, "labels": ["render"]}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0 1 1")

    def test_needs_plan_mixed_classes_elects_oldest_and_flags_more(self):
        # Oldest plannable issue is the sonnet-tagged mechanical one, so the
        # lane elects sonnet; the untagged issue keeps an OPUS planning
        # candidate alive -> more=1 so the dispatcher's cross-class fan-out
        # serves it on the opus lane the same tick.
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
        # A design-unblocked PR is the top feedback item, every open task is
        # parked/blocked, and a fable needs_plan sits behind it. The lane must
        # dispatch opus (and clear it for tier 4), not sonnet.
        # count=1: the feedback fix is the only opus item; the plannable issue
        # is fleet:fable-tagged so it elects fable, a different class than the
        # elected opus, and rides the `more` flag — the kept trigger serves the
        # planning dispatch on a following tick.
        out = resolve({
            "feedback_prs": [{"labels": ["fleet:design-unblocked", "fleet:wip"]}],
            "tasks_open": [_task("#1882", "opus",
                                 inflight_pr={"number": 1885, "parked": True})],
            "needs_plan": [{"number": 1887, "labels": ["fleet:fable"]}],
        }, "opus", fable_blocked=False)
        self.assertEqual(out, "opus high 1 1 0")


class FableCap(unittest.TestCase):
    def test_cap_skips_fable_to_next_class(self):
        # more=0: cap-blocked fable is not servable, so it must not hold
        # the trigger open (the fable iteration finishing re-fires the
        # scout; the periodic safety re-arm covers the quiescent corner).
        out = resolve({"tasks_open": [_task("#10", "fable"),
                                      _task("#11", "opus")]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus high 0 1 0")

    def test_only_capped_fable_defers(self):
        out = resolve({"tasks_open": [_task("#10", "fable")]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "defer")

    def test_uncapped_fable_dispatches(self):
        out = resolve({"tasks_open": [_task("#10", "fable")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable high 0 1 0")


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


class HostSeamCase(unittest.TestCase):
    """Base for every host-gated case: saves/restores the FLEET_TEST_HOST seam
    (the same seam fleet-claim's derive_host uses) and resolves a slice as
    seen from a given host."""

    def setUp(self):
        self._saved_host = os.environ.get("FLEET_TEST_HOST")

    def tearDown(self):
        if self._saved_host is None:
            os.environ.pop("FLEET_TEST_HOST", None)
        else:
            os.environ["FLEET_TEST_HOST"] = self._saved_host

    def _resolve_on(self, host, slice_data, lane_default="opus",
                    fable_blocked=False):
        os.environ["FLEET_TEST_HOST"] = host
        return resolve(slice_data, lane_default, fable_blocked=fable_blocked)


class GlHostGate(HostSeamCase):
    """A `needs_gl_host` task can't be built/run/verified on a Metal-only
    (macOS) host. The dispatcher's claimability filter skips it there — so a
    mac slice whose only open work is GL-only defers (goes quiet) instead of
    churning a lane-default no-op, while a Linux/Windows slice claims it
    normally."""

    def test_gl_only_task_alone_on_mac_defers(self):
        # A GL-backend task is the only open work and the pane is Metal-only
        # -> defer (go quiet), not a lane-default no-op.
        out = self._resolve_on(
            "mac", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "defer")

    # --- the backend-symmetric narrowing ---------------------------------

    def test_backend_symmetric_task_is_claimable_on_mac(self):
        # The task's Metal half is natively verifiable here, so the pane
        # dispatches instead of deferring.
        out = self._resolve_on("mac", {"tasks_open": [
            _task("#2816", "opus", needs_gl_host=True, backend_symmetric=True)]})
        self.assertEqual(out, "opus high 0 1 0")

    def test_gl_only_task_still_defers_on_mac(self):
        # Without the discriminator, nothing changes for a GL-only task.
        out = self._resolve_on("mac", {"tasks_open": [
            _task("#1938", "opus", needs_gl_host=True, backend_symmetric=False)]})
        self.assertEqual(out, "defer")

    def test_unknown_host_refuses_despite_backend_symmetric(self):
        # The discriminator opens ONLY the mac door. Fail-closed survives the
        # narrowing — asserted, not left to METAL_CAPABLE_HOSTS' comment.
        out = self._resolve_on("unknown", {"tasks_open": [
            _task("#2816", "opus", needs_gl_host=True, backend_symmetric=True)]})
        self.assertEqual(out, "defer")

    def test_feedback_pr_dimension_ignores_a_backend_symmetric_label(self):
        # The PR path is deliberately NOT narrowed: `_host_incompatible` reads
        # the scout's task FIELD and never a PR's labels, because on a PR
        # `fleet:needs-gl-host` describes the RESIDUAL, not the task's
        # symmetry. A PR carrying both labels must still be host-incompatible
        # on mac — narrowing here would re-inflate the phantom counts.
        pr = {"number": 2475,
              "labels": ["fleet:needs-gl-host", "fleet:backend-symmetric",
                         "fleet:needs-fix"]}
        self.assertTrue(_host_incompatible(pr, "mac"))
        self.assertFalse(_host_incompatible(pr, "linux"))

    def test_gl_only_task_claimable_on_linux(self):
        out = self._resolve_on(
            "linux", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "opus high 0 1 0")

    def test_gl_only_task_claimable_on_windows(self):
        out = self._resolve_on(
            "windows", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "opus high 0 1 0")

    def test_mixed_mac_slice_dispatches_claimable_no_churn(self):
        # A GL-only head + a claimable opus task: the mac pane skips the GL
        # task (not counted toward `more`) and dispatches the claimable one —
        # no defer, no churn.
        out = self._resolve_on("mac", {"tasks_open": [
            _task("#1937", "opus", needs_gl_host=True),
            _task("#1998", "opus"),
        ]})
        self.assertEqual(out, "opus high 0 1 0")

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
        self.assertEqual(out, "opus high 0 1 0")

    def test_unknown_host_is_fail_closed(self):
        # Fail-closed: an unrecognized host is treated as not GL-capable, so a
        # GL-only task is skipped (real dispatch hosts are always mac/linux/win).
        out = self._resolve_on(
            "unknown", {"tasks_open": [_task("#1937", "opus", needs_gl_host=True)]})
        self.assertEqual(out, "defer")


class RequiredHostGate(HostSeamCase):
    """A task whose body pins it to ONE OS (`needs_host`, scout-derived) is
    claimable on that host only. A task whose body says "must run on a Linux
    host" in prose sets `needs_gl_host` (linux is GL-capable) but not
    `needs_host`, so the GL gate alone would pass it on a Windows pane and the
    dispatcher would elect it every tick, each worker re-reading the body and
    refusing."""

    def test_linux_only_task_defers_on_windows(self):
        out = self._resolve_on("windows", {"tasks_open": [
            _task("#1969", "sonnet", needs_gl_host=True, needs_host="linux")]})
        self.assertEqual(out, "defer")

    def test_linux_only_task_dispatches_on_linux(self):
        out = self._resolve_on("linux", {"tasks_open": [
            _task("#1969", "sonnet", needs_gl_host=True, needs_host="linux")]})
        self.assertEqual(out, "sonnet high 0 1 0")

    def test_windows_only_task_dispatches_on_windows_only(self):
        slice_data = {"tasks_open": [_task("#1", "opus", needs_host="windows")]}
        self.assertEqual(self._resolve_on("windows", slice_data), "opus high 0 1 0")
        self.assertEqual(self._resolve_on("linux", slice_data), "defer")

    def test_mac_only_task_gates_off_linux_and_windows(self):
        # The symmetric half the GL gate deliberately lacks: a mac-pinned task
        # is not a GL task, so `needs_gl_host` is False and only `needs_host`
        # keeps it off the GL hosts.
        slice_data = {"tasks_open": [_task("#2", "opus", needs_host="mac")]}
        self.assertEqual(self._resolve_on("mac", slice_data), "opus high 0 1 0")
        self.assertEqual(self._resolve_on("linux", slice_data), "defer")
        self.assertEqual(self._resolve_on("windows", slice_data), "defer")

    def test_unknown_host_is_fail_closed(self):
        out = self._resolve_on("unknown", {"tasks_open": [
            _task("#1969", "sonnet", needs_host="linux")]})
        self.assertEqual(out, "defer")

    def test_unpinned_task_is_unaffected(self):
        # `needs_host` absent (slices without the field) or None: today's
        # behavior.
        task = _task("#3", "opus")
        del task["needs_host"]
        self.assertEqual(self._resolve_on("windows", {"tasks_open": [task]}),
                         "opus high 0 1 0")
        self.assertFalse(_host_incompatible(_task("#4", "opus"), "mac"))

    def test_host_pinned_head_does_not_starve_claimable_work(self):
        # Two linux-pinned sonnet tasks at the head, with a claimable sibling
        # behind them: the sibling must still be elected, and the pinned pair
        # must not inflate its count.
        out = self._resolve_on("windows", {"tasks_open": [
            _task("#1969", "sonnet", needs_gl_host=True, needs_host="linux"),
            _task("#2158", "sonnet", needs_gl_host=True, needs_host="linux"),
            _task("#2200", "sonnet")]})
        self.assertEqual(out, "sonnet high 0 1 0")


class DispatchTargets(HostSeamCase):
    """`pick` / `pick_role`: the ordered `<kind>:<repo>:<N>[:<extra>]` lines
    the dispatcher walks, taking each item's lane claim until one is granted
    (assign_for_pane). Order is the dispatch priority — conflict,
    feedback, task, stack, then the class's planning candidates — filtered
    to the elected class and host-gated exactly as `resolve` counts them,
    so what is elected is what gets assigned."""

    def _pick_on(self, host, slice_data, cls, fable_blocked=False):
        os.environ["FLEET_TEST_HOST"] = host
        return pick(slice_data, cls, fable_blocked)

    def test_worker_pickup_order_and_kinds(self):
        slice_data = {
            "feedback_prs": [{"number": 50, "repo": "engine",
                              "labels": ["fleet:approved", "fleet:has-nits"]}],
            "semantic_conflict_prs": [{"number": 2417, "repo": "engine",
                                       "labels": ["fleet:semantic-conflict"]}],
            "tasks_open": [
                _task("#344", "sonnet", blocked=True,
                      stackable_blocker_pr={"number": 397, "headRefName": "x"}),
                _task("#10", "opus"),
                _task("#13", "sonnet"),
            ],
            "needs_plan": [{"number": 99, "repo": "engine", "labels": ["fleet:sonnet"]}],
        }
        for task in slice_data["tasks_open"]:
            task["repo"] = "engine"
        self.assertEqual(self._pick_on("linux", slice_data, "sonnet"), [
            "feedback:engine:50", "task:engine:13", "stack:engine:344:397",
            "plan:engine:99"])
        self.assertEqual(self._pick_on("linux", slice_data, "opus"),
                         ["conflict:engine:2417", "task:engine:10"])
        self.assertEqual(self._pick_on("linux", slice_data, "fable"), [])

    def test_host_gate_and_owner_filter_apply(self):
        # A pinned or owned item never becomes a target on the wrong host —
        # the same predicate that keeps it out of the class election.
        tasks = [_task("#1969", "sonnet", needs_gl_host=True, needs_host="linux"),
                 _task("#1", "sonnet", owner="pool-4"),
                 _task("#2", "sonnet", inflight_pr={"number": 5}),
                 _task("#3", "sonnet")]
        for task in tasks:
            task["repo"] = "game"
        self.assertEqual(self._pick_on("windows", {"tasks_open": tasks}, "sonnet"),
                         ["task:game:3"])
        self.assertEqual(self._pick_on("linux", {"tasks_open": tasks}, "sonnet"),
                         ["task:game:1969", "task:game:3"])

    def test_untagged_task_follows_the_lane_default(self):
        task = _task("#7")
        task["repo"] = "engine"
        os.environ["FLEET_TEST_HOST"] = "linux"
        self.assertEqual(pick({"tasks_open": [task]}, "opus", False, "opus"),
                         ["task:engine:7"])
        self.assertEqual(pick({"tasks_open": [task]}, "sonnet", False, "sonnet"),
                         ["task:engine:7"])
        self.assertEqual(pick({"tasks_open": [task]}, "sonnet", False, "opus"), [])

    def test_capped_fable_plan_routes_to_opus(self):
        slice_data = {"needs_plan": [
            {"number": 99, "repo": "engine", "labels": ["fleet:fable"]}]}
        self.assertEqual(self._pick_on("linux", slice_data, "fable"), ["plan:engine:99"])
        self.assertEqual(self._pick_on("linux", slice_data, "opus"), [])
        self.assertEqual(self._pick_on("linux", slice_data, "opus", fable_blocked=True),
                         ["plan:engine:99"])

    def test_reviewer_and_smoke_lanes(self):
        self.assertEqual(pick_role({"candidate_prs": [
            {"number": 3074, "repo": "engine"}, {"number": 12, "repo": "game"}]},
            "sonnet-reviewer"), ["review:engine:3074", "review:game:12"])
        self.assertEqual(pick_role({
            "flagged_prs": [{"number": 3080, "repo": "engine"}],
            "plan_review": [{"number": 605, "repo": "engine"}]},
            "opus-reviewer"), ["review:engine:3080", "planreview:engine:605"])
        os.environ["FLEET_TEST_HOST"] = "windows"
        smoke = {"smoke_pending_prs": [
            {"number": 3087, "labels": ["fleet:needs-windows-smoke"]},
            {"number": 3082, "labels": ["fleet:needs-macos-smoke"]},
            {"number": 41, "repo": "game", "labels": ["fleet:needs-windows-smoke"]},
            {"number": 42, "repo": "game", "labels": ["fleet:needs-macos-smoke"]}]}
        # Slice order is kept (engine first); a record without `repo` is an
        # engine record; a game record targets `smoke:game:<N>`.
        self.assertEqual(pick_role(smoke, "smoke-worker"),
                         ["smoke:engine:3087", "smoke:game:41"])
        self.assertEqual(pick_role(smoke, "merger"), [])

    def test_record_without_a_number_is_dropped(self):
        self.assertEqual(pick_role({"candidate_prs": [{"repo": "engine"}]},
                                   "sonnet-reviewer"), [])
        task = _task("#", "opus")
        task["repo"] = "engine"
        self.assertEqual(self._pick_on("linux", {"tasks_open": [task]}, "opus"), [])

    def test_declined_target_is_skipped_until_the_item_changes(self):
        # When the claim is granted, the worker reads the body, refuses,
        # `fleet-claim decline` posts the record and the dispatcher's exit fold
        # remembers the item's post-release updatedAt. A record not newer than
        # the stamp -> not offered (and not counted) — including one the scout
        # has not refreshed since the release; a newer stamp -> offered again;
        # no stamp -> offered.
        with tempfile.TemporaryDirectory() as state_dir:
            os.environ["FLEET_STATE_DIR"] = state_dir
            try:
                os.makedirs(os.path.join(state_dir, "declined"))
                task = _task("#1969", "sonnet")
                task["repo"] = "engine"
                task["updatedAt"] = "2026-09-06T12:00:00Z"
                with open(os.path.join(state_dir, "declined", "task-engine-1969"), "w") as handle:
                    handle.write("2026-09-06T12:00:00Z\nneeds a linux host\n")
                self.assertEqual(self._pick_on("linux", {"tasks_open": [task]}, "sonnet"), [])
                self.assertEqual(self._resolve_on("linux", {"tasks_open": [task]}, "sonnet"),
                                 "")
                task["updatedAt"] = "2026-09-06T11:00:00Z"   # stale projection
                self.assertEqual(self._pick_on("linux", {"tasks_open": [task]}, "sonnet"), [])
                task["updatedAt"] = "2026-09-06T13:00:00Z"
                self.assertEqual(self._pick_on("linux", {"tasks_open": [task]}, "sonnet"),
                                 ["task:engine:1969"])
                task["updatedAt"] = ""
                self.assertEqual(self._pick_on("linux", {"tasks_open": [task]}, "sonnet"),
                                 ["task:engine:1969"])
                # Other kinds and repos are separate memories.
                pr = {"number": 1969, "repo": "engine", "updatedAt": "2026-09-06T12:00:00Z"}
                self.assertEqual(pick_role({"candidate_prs": [pr]}, "sonnet-reviewer"),
                                 ["review:engine:1969"])
                with open(os.path.join(state_dir, "declined", "review-engine-1969"), "w") as handle:
                    handle.write("2026-09-06T12:00:00Z\nverdict already standing\n")
                self.assertEqual(pick_role({"candidate_prs": [pr]}, "sonnet-reviewer"), [])
            finally:
                os.environ.pop("FLEET_STATE_DIR", None)

    def test_decline_memory_is_scoped_to_the_declining_role(self):
        # The sonnet and opus reviewers share the `review` kind. A sonnet
        # decline ("fresh approval already posted; the escalation is
        # standing") is that lane's judgement, not the opus lane's: the PR
        # was escalated TO opus, and an unscoped record would hide it there
        # for as long as nothing touched the PR. Line 3 of the record is the
        # role; a record without one (written before the role was recorded)
        # keeps its old reach.
        with tempfile.TemporaryDirectory() as state_dir:
            os.environ["FLEET_STATE_DIR"] = state_dir
            try:
                os.makedirs(os.path.join(state_dir, "declined"))
                pr = {"number": 3286, "repo": "engine", "updatedAt": "2026-09-14T20:17:33Z",
                      "labels": ["fleet:needs-opus-recheck"]}
                sonnet = {"candidate_prs": [pr]}
                opus = {"flagged_prs": [pr]}
                path = os.path.join(state_dir, "declined", "review-engine-3286")
                with open(path, "w") as handle:
                    handle.write("2026-09-14T20:17:33Z\nescalation standing\nsonnet-reviewer\n")
                self.assertEqual(pick_role(sonnet, "sonnet-reviewer"), [])
                self.assertEqual(pick_role(opus, "opus-reviewer"), ["review:engine:3286"])
                with open(path, "w") as handle:
                    handle.write("2026-09-14T20:17:33Z\nnot mine\nopus-reviewer\n")
                self.assertEqual(pick_role(sonnet, "sonnet-reviewer"), ["review:engine:3286"])
                self.assertEqual(pick_role(opus, "opus-reviewer"), [])
                with open(path, "w") as handle:
                    handle.write("2026-09-14T20:17:33Z\nlegacy record, no role line\n")
                self.assertEqual(pick_role(sonnet, "sonnet-reviewer"), [])
                self.assertEqual(pick_role(opus, "opus-reviewer"), [])
                # The worker lane's kinds are one role: any worker's decline
                # of a task suppresses every worker class, as before.
                task = _task("#1969", "sonnet")
                task["repo"] = "engine"
                task["updatedAt"] = "2026-09-06T12:00:00Z"
                with open(os.path.join(state_dir, "declined", "task-engine-1969"), "w") as handle:
                    handle.write("2026-09-06T12:00:00Z\nneeds a linux host\nworker\n")
                self.assertEqual(self._pick_on("linux", {"tasks_open": [task]}, "sonnet"), [])
                self.assertEqual(self._pick_on("linux", {"tasks_open": [task]}, "opus"), [])
            finally:
                os.environ.pop("FLEET_STATE_DIR", None)

    def test_prs_under_another_review_claim_are_not_targets(self):
        # The reviewers skip these from cached labels at zero cost; the
        # dispatcher's walk must too, or each costs a real review-claim round
        # trip before being refused.
        held = {"number": 3074, "repo": "engine", "labels": ["fleet:reviewing-mac-pool-2"]}
        free = {"number": 3080, "repo": "engine", "labels": ["fleet:changes-made"]}
        self.assertEqual(pick_role({"candidate_prs": [held, free]}, "sonnet-reviewer"),
                         ["review:engine:3080"])
        self.assertEqual(pick_role({"flagged_prs": [held], "plan_review": [
            {"number": 605, "repo": "engine", "labels": ["fleet:reviewing-linux-pool-1"]}]},
            "opus-reviewer"), [])


class FeedbackPrHostGate(HostSeamCase):
    """The host gate applies to feedback PRs too, not just tasks.

    A `fleet:needs-gl-host` feedback PR has GL-only work left, so role-worker
    step 1 skips it on a Metal-only host and `amending-claim` refuses the
    claim. Counting it anyway would inflate the elected class's claimable
    count on hosts that can't serve it, and because `feedback_pr_class` routes
    `fleet:design-unblocked` to opus, that phantom item would win the class
    election every tick and starve the other lanes behind the concurrency cap.
    """

    @staticmethod
    def _fb(number, labels):
        return {"number": number, "repo": "engine", "labels": labels}

    # -- the gate itself -------------------------------------------------
    def test_gl_gated_feedback_pr_not_counted_on_mac(self):
        # design-unblocked (opus-routed) AND GL-gated. On mac it must
        # contribute nothing; the claimable sonnet task is what gets elected,
        # so the count reflects only work this host can serve.
        out = self._resolve_on("mac", {
            "feedback_prs": [self._fb(2475, ["fleet:design-unblocked",
                                             "fleet:needs-gl-host"])],
            "tasks_open": [_task("#10", "sonnet")],
        }, lane_default="sonnet")
        self.assertEqual(out, "sonnet high 0 1 0")

    def test_gl_gated_feedback_pr_counted_on_linux(self):
        out = self._resolve_on("linux", {
            "feedback_prs": [self._fb(2475, ["fleet:design-unblocked",
                                             "fleet:needs-gl-host"])],
        })
        self.assertEqual(out, "opus high 0 1 0")

    def test_gl_gated_feedback_pr_counted_on_windows(self):
        out = self._resolve_on("windows", {
            "feedback_prs": [self._fb(2475, ["fleet:design-unblocked",
                                             "fleet:needs-gl-host"])],
        })
        self.assertEqual(out, "opus high 0 1 0")

    def test_ungated_feedback_pr_unaffected_on_mac(self):
        # Only the GL label is gated — an ordinary feedback PR still counts on
        # every host.
        out = self._resolve_on("mac", {
            "feedback_prs": [self._fb(2393, ["fleet:design-unblocked",
                                             "fleet:wip"])],
        })
        self.assertEqual(out, "opus high 0 1 0")

    def test_unknown_host_is_fail_closed(self):
        out = self._resolve_on("unknown", {
            "feedback_prs": [self._fb(2475, ["fleet:needs-fix",
                                             "fleet:needs-gl-host"])],
        })
        self.assertEqual(out, "defer")

    # -- the quiet path must follow the gate -----------------------------
    def test_gl_gated_feedback_pr_alone_on_mac_defers(self):
        # Gating `_candidates` alone would only relocate the churn: with no
        # candidate yielded, a tasks-only quiet check reports
        # nothing-unclaimable and falls through to '' -> a lane-default no-op
        # dispatch. The slice holds real work no host-compatible worker can
        # claim -> defer.
        out = self._resolve_on("mac", {
            "feedback_prs": [self._fb(2475, ["fleet:design-unblocked",
                                             "fleet:needs-gl-host"])],
        })
        self.assertEqual(out, "defer")

    def test_gl_gated_feedback_plus_terminal_tasks_on_mac_defers(self):
        out = self._resolve_on("mac", {
            "feedback_prs": [self._fb(2475, ["fleet:design-unblocked",
                                             "fleet:needs-gl-host"])],
            "tasks_open": [_task("#1640", "opus", inflight_pr={"number": 1700})],
        })
        self.assertEqual(out, "defer")

    def test_gl_gated_feedback_plus_owned_task_still_falls_through(self):
        # An owner-held task is NOT terminal, so the '' fallthrough that covers
        # reservation resumes survives the widened quiet check.
        out = self._resolve_on("mac", {
            "feedback_prs": [self._fb(2475, ["fleet:design-unblocked",
                                             "fleet:needs-gl-host"])],
            "tasks_open": [_task("#10", "opus", owner="worker-1")],
        })
        self.assertEqual(out, "")

    def test_conflict_still_dispatches_when_feedback_host_locked(self):
        # Step 1c is host-agnostic, so a conflict must still elect opus even
        # when the only feedback item is GL-locked on this host.
        out = self._resolve_on("mac", {
            "feedback_prs": [self._fb(2475, ["fleet:design-unblocked",
                                             "fleet:needs-gl-host"])],
            "semantic_conflict_prs": [{"number": 2417, "repo": "engine",
                                       "labels": ["fleet:semantic-conflict"]}],
        })
        self.assertEqual(out, "opus high 0 1 0")


class FeedbackPrInheritsIssueHostPin(HostSeamCase):
    """End-to-end (slice_worker -> resolve / pick): a feedback PR whose
    `Closes #N` issue body pins `**Host:** macos` carries that pin into the
    worker slice, so the dispatcher elects it on mac only: a Metal re-capture
    residual is no more actionable on a Windows pane than the task was."""

    def setUp(self):
        super().setUp()
        self._saved_state_dir = os.environ.get("FLEET_STATE_DIR")
        self._tmp = tempfile.TemporaryDirectory()
        os.environ["FLEET_STATE_DIR"] = self._tmp.name

    def tearDown(self):
        if self._saved_state_dir is None:
            os.environ.pop("FLEET_STATE_DIR", None)
        else:
            os.environ["FLEET_STATE_DIR"] = self._saved_state_dir
        self._tmp.cleanup()
        super().tearDown()

    @staticmethod
    def _pr(number, closes, labels=("fleet:needs-fix",)):
        return {"number": number, "title": "T: fix",
                "headRefName": f"claude/{number}", "baseRefName": "master",
                "labels": sorted(labels), "isDraft": False,
                "mergeable": "MERGEABLE", "author": "bot",
                "closes_issues": list(closes), "closes_cross_repo": []}

    @staticmethod
    def _state(prs, in_progress=(), open_tasks=(), game_tasks=()):
        empty = {"prs": [], "tasks": {"open": [], "in_progress": []},
                 "needs_plan": []}
        return {"repos": {
            "engine": {"prs": list(prs), "needs_plan": [],
                       "tasks": {"open": list(open_tasks),
                                 "in_progress": list(in_progress)}},
            "game": dict(empty, tasks={"open": [],
                                       "in_progress": list(game_tasks)}),
        }}

    def _on(self, host, state):
        """(resolve verdict, opus-lane dispatch targets) as seen from `host`."""
        slice_data = slice_worker(state)
        out = self._resolve_on(host, slice_data)
        return out, pick(slice_data, "opus", False)

    def test_mac_pinned_issue_gates_feedback_pr_off_windows_and_linux(self):
        state = self._state([self._pr(3768, [3757])],
                            in_progress=[_task("#3757", "opus", needs_host="mac")])
        self.assertEqual(slice_worker(state)["feedback_prs"][0]["needs_host"],
                         "mac")
        for host in ("windows", "linux"):
            self.assertEqual(self._on(host, state), ("defer", []), host)
        self.assertEqual(self._on("mac", state),
                         ("opus high 0 1 0", ["feedback:engine:3768"]))

    def test_pin_read_from_an_open_task_too(self):
        # An unclaimed task with an open PR stays in tasks.open.
        state = self._state([self._pr(3768, [3757])],
                            open_tasks=[_task("#3757", "opus", needs_host="mac",
                                              inflight_pr={"number": 3768})])
        self.assertEqual(self._on("windows", state), ("defer", []))

    def test_unpinned_issue_elects_everywhere(self):
        # Regression: no Host pin -> no `needs_host` on the record and the
        # same election as before on every host.
        state = self._state([self._pr(3768, [3757])],
                            in_progress=[_task("#3757", "opus")])
        self.assertNotIn("needs_host", slice_worker(state)["feedback_prs"][0])
        for host in ("mac", "windows", "linux"):
            self.assertEqual(self._on(host, state),
                             ("opus high 0 1 0", ["feedback:engine:3768"]),
                             host)

    def test_unpinned_link_does_not_veto_a_pinned_one(self):
        state = self._state([self._pr(3768, [3757, 3758])],
                            in_progress=[_task("#3757", "opus", needs_host="mac"),
                                         _task("#3758", "opus")])
        self.assertEqual(self._on("windows", state), ("defer", []))

    def test_disagreeing_pins_leave_the_pr_ungated(self):
        state = self._state([self._pr(3768, [3757, 3758])],
                            in_progress=[_task("#3757", "opus", needs_host="mac"),
                                         _task("#3758", "opus",
                                               needs_host="windows")])
        self.assertNotIn("needs_host", slice_worker(state)["feedback_prs"][0])
        self.assertEqual(self._on("linux", state)[0], "opus high 0 1 0")

    def test_other_repo_task_with_the_same_number_is_ignored(self):
        # `closes_issues` is same-repo: a pinned game issue with the same
        # number says nothing about the engine issue the PR closes.
        state = self._state([self._pr(3768, [3757])],
                            game_tasks=[_task("#3757", "opus", needs_host="mac")])
        self.assertEqual(self._on("windows", state)[0], "opus high 0 1 0")


class SemanticConflictDispatchPressure(HostSeamCase):
    """Semantic-conflict PRs are opus-class claimable work dispatched ahead of
    feedback and tasks (role-worker step 1c, opus+-classes-only). This tier
    gives the label dispatch pressure: without it, a conflicted PR is
    resolved only as a ride-along when opus queue work happens to be flowing,
    and starves when the opus lane is dry or host-locked. The scout
    pre-filters the slice (CONFLICTING-gated, step-1c exclusions,
    resolving-claims, stacked children), so the resolver counts every entry
    as-is."""

    @staticmethod
    def _sc(num):
        return {"number": num, "repo": "engine",
                "labels": ["fleet:semantic-conflict"]}

    def test_conflict_alone_elects_opus(self):
        out = resolve({"semantic_conflict_prs": [self._sc(2417)]},
                      "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus high 0 1 0")

    def test_conflict_dispatches_when_all_tasks_host_locked(self):
        # Every open task is GL-locked on a Metal-only host, so tasks alone
        # would defer and no opus iteration ever launches — the conflict must
        # still dispatch opus.
        os.environ["FLEET_TEST_HOST"] = "mac"
        out = resolve({
            "tasks_open": [_task("#1938", "opus", needs_gl_host=True)],
            "semantic_conflict_prs": [self._sc(2417)],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus high 0 1 0")

    def test_conflict_elected_before_feedback(self):
        # A conflicted PR holds up the merge flow, so it outranks feedback.
        # The sonnet feedback stays servable -> more=1 so the next tick
        # serves the sonnet lane.
        out = resolve({
            "feedback_prs": [{"number": 11, "labels": ["fleet:has-nits"]}],
            "semantic_conflict_prs": [self._sc(2417)],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus high 1 1 0")

    def test_conflict_precedes_feedback_in_pick_order(self):
        os.environ["FLEET_TEST_HOST"] = "linux"
        out = pick({
            "feedback_prs": [{"number": 11, "repo": "engine",
                              "labels": ["fleet:needs-fix"]}],
            "semantic_conflict_prs": [self._sc(2417)],
        }, "opus", False)
        self.assertEqual(out, ["conflict:engine:2417", "feedback:engine:11"])

    def test_conflict_elected_before_open_tasks(self):
        # Conflicts dispatch ahead of task pickup, so the conflict outranks
        # a claimable sonnet task; the task holds more=1.
        out = resolve({
            "semantic_conflict_prs": [self._sc(2417)],
            "tasks_open": [_task("#10", "sonnet")],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus high 1 1 0")

    def test_conflicts_and_opus_feedback_share_the_count(self):
        # Each conflict is one claimable opus item alongside opus feedback:
        # the fan-out cap must cover both, one worker per item.
        out = resolve({
            "feedback_prs": [{"number": 11, "labels": ["fleet:needs-fix"]}],
            "semantic_conflict_prs": [self._sc(2417), self._sc(2420)],
        }, "sonnet", fable_blocked=False)
        self.assertEqual(out, "opus high 0 3 0")

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
        self.assertEqual(out, "opus high 0 1 0")


class ExcludeClasses(unittest.TestCase):
    """The dispatcher's cross-class fan-out: when an elected class is fully
    cap-covered but another class is claimable, it re-resolves with the covered
    class excluded to serve the next one instead of deferring the whole tick."""

    def test_exclude_elects_the_next_class(self):
        slice_data = {"tasks_open": [_task("#10", "opus"), _task("#11", "sonnet")]}
        # No exclude: opus is the elected (oldest) class, sonnet is `more`.
        self.assertEqual(resolve(slice_data, "opus", False), "opus high 1 1 0")
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
                         "opus high 0 1 0")

    def test_exclude_on_empty_slice_stays_empty(self):
        # No claimable work at all + an exclude -> '' (lane-default), not defer:
        # nothing was excluded, so the reservation-resume fallthrough stands.
        self.assertEqual(resolve({"tasks_open": []}, "opus", False,
                                 exclude=["opus"]), "")


class PlanFlag(unittest.TestCase):
    """The trailing ``plan`` token: 1 iff the ELECTED class's claimable count
    includes its needs-plan yield — the dispatcher then pre-claims a specific
    issue and hands the assignment to the dispatch."""

    def test_task_plus_same_class_plan_sets_flag_and_counts_both(self):
        # An opus task + an opus-degraded plan (fable capped): opus elected,
        # count=2 (task + the planning slot), plan=1.
        out = resolve({"tasks_open": [_task("#10", "opus")],
                       "needs_plan": [{"number": 99}]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus high 0 2 1")

    def test_other_class_plan_does_not_set_flag(self):
        # The plan candidate routes to fable (explicitly tagged); the elected
        # class is opus (the task) -> plan=0, fable plan rides `more` for a
        # later tick.
        out = resolve({"tasks_open": [_task("#10", "opus")],
                       "needs_plan": [{"number": 99, "labels": ["fleet:fable"]}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus high 1 1 0")


class PlanPick(unittest.TestCase):
    """`plan_pick` — the ordered `plan:<repo>:<N>` targets the dispatcher
    walks with `fleet-claim planning-claim` until one is granted."""

    SLICE = {"needs_plan": [
        {"number": 90, "repo": "engine", "labels": ["fleet:sonnet"]},
        {"number": 99, "repo": "engine", "labels": ["fleet:fable"]},
        {"number": 120, "repo": "engine", "labels": ["render", "fleet:fable"]},
        {"number": 7, "repo": "game", "labels": ["fleet:fable"]},
        # untagged -> ordinary opus planning, never a fable candidate
        {"number": 130, "repo": "engine", "labels": []},
    ]}

    def test_fable_class_picks_design_tier_in_slice_order(self):
        # Engine-first / oldest-first is the slice composition order —
        # plan_pick preserves it; the sonnet-tagged mechanical issue is not a
        # fable candidate.
        self.assertEqual(plan_pick(self.SLICE, "fable", False),
                         ["plan:engine:99", "plan:engine:120", "plan:game:7"])

    def test_sonnet_class_picks_only_mechanical(self):
        self.assertEqual(plan_pick(self.SLICE, "sonnet", False), ["plan:engine:90"])

    def test_fable_cap_degrades_design_tier_to_opus(self):
        # fable_blocked degrades the fable-tagged design tier to opus (same
        # `_plan_class` degrade `resolve` applies), so opus then owns the fable
        # set PLUS the untagged row it always owned, in slice order.
        self.assertEqual(plan_pick(self.SLICE, "opus", True),
                         ["plan:engine:99", "plan:engine:120", "plan:game:7",
                          "plan:engine:130"])
        # With fable free, opus owns only the untagged row.
        self.assertEqual(plan_pick(self.SLICE, "opus", False), ["plan:engine:130"])

    def test_missing_repo_defaults_engine_and_missing_number_skipped(self):
        # Untagged issues plan at opus, so this reads the opus pick set; the
        # assertion is about the missing `repo` defaulting to engine and the
        # entry with no `number` being skipped.
        s = {"needs_plan": [{"number": 5}, {"labels": []}]}
        self.assertEqual(plan_pick(s, "opus", False), ["plan:engine:5"])

    def test_empty_slice_yields_no_picks(self):
        self.assertEqual(plan_pick({}, "fable", False), [])


class SemanticConflictFeedbackExclusionIntegration(unittest.TestCase):
    """End-to-end (slice_worker -> resolve): a fleet:semantic-conflict PR that
    ALSO owes a worker feedback fix must reach resolve() as feedback-only. The
    scout drops it from semantic_conflict_prs[], so no parallel opus conflict
    item is minted alongside the feedback item. Without this, a feedback pane
    and an opus resolver pane can force-push the same head branch on one tick
    (disjoint fleet:amending-* vs fleet:resolving-* claims give no mutual
    exclusion), silently dropping the conflict resolution or the fix."""

    @staticmethod
    def _state(pr):
        return {"repos": {"engine": {
            "prs": [pr], "tasks": {"open": []}, "needs_plan": []}}}

    @staticmethod
    def _pr(labels, mergeable):
        return {"number": 2422, "title": "T: feat",
                "headRefName": "claude/feat", "baseRefName": "master",
                "labels": sorted(labels), "isDraft": False,
                "mergeable": mergeable, "author": "bot"}

    def test_conflict_plus_feedback_dispatches_feedback_only(self):
        # Every worker feedback / design-resume label suppresses the parallel
        # conflict item, regardless of which class the feedback itself routes
        # to (nits -> sonnet, needs-fix / design-unblocked -> opus). Assert the
        # dispatch is identical to the same PR with no conflict label at all —
        # the conflict adds zero pressure while the feedback is pending.
        for label in ("fleet:has-nits", "fleet:needs-fix",
                      "fleet:design-unblocked"):
            combined = slice_worker(self._state(self._pr(
                ["fleet:semantic-conflict", label], "CONFLICTING")))
            self.assertEqual(combined["semantic_conflict_prs"], [], label)
            self.assertEqual(len(combined["feedback_prs"]), 1, label)
            plain = slice_worker(self._state(self._pr([label], "MERGEABLE")))
            self.assertEqual(resolve(combined, "sonnet", False),
                             resolve(plain, "sonnet", False), label)


class StackOfferDispatch(unittest.TestCase):
    """Stackable-offer → dispatch integration. A blocked task whose sole
    blocker has a safe open PR gets `stackable_blocker_pr` and the dispatcher
    dispatches on it. A base missing a merged ancestor self-heals via
    `gh stack sync`, so the offer is not withheld on containment.
    Hermetic: PRS dir redirects into a TemporaryDirectory
    (scripts/fleet/CLAUDE.md)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self._orig_prs_dir = _scout_mod.PRS_DIR
        _scout_mod.PRS_DIR = Path(self._tmp.name) / "prs"
        self._orig_log = _scout_mod.log
        _scout_mod.log = lambda _m: None

    def tearDown(self):
        _scout_mod.PRS_DIR = self._orig_prs_dir
        _scout_mod.log = self._orig_log
        self._tmp.cleanup()

    def _enriched_slice(self):
        """Build the forensics-shaped state (candidate #40 → base #30's PR;
        #30 blocked by merged #10 + open #20), enrich it, and return the
        resolve() slice."""
        state = {
            "repos": {
                "engine": {
                    "tasks": {
                        "open": [{"id": "#40", "issue": "#40",
                                  "blocked_by": "#30", "area": None,
                                  "model": "opus", "owner": "free",
                                  "blocked": True}],
                        "in_progress": [
                            {"id": "#30", "issue": "#30",
                             "blocked_by": "#10, #20"},
                            {"id": "#20", "issue": "#20",
                             "blocked_by": "(none)"},
                        ],
                    },
                    "prs": [{"number": 300, "headRefName": "claude/30-base",
                             "headRefOid": "oidbase", "author": "bot",
                             "labels": ["fleet:approved"]}],
                    "recent_merged_prs": [
                        {"number": 100, "headRefName": "claude/10-m",
                         "mergedAt": "t"}],
                    "closed_fleet_queued": [],
                },
                "game": {"tasks": {"open": [], "in_progress": []}, "prs": [],
                         "recent_merged_prs": [], "closed_fleet_queued": []},
            }
        }
        _scout_mod.enrich_stackable_blocker_prs(state)
        return {"tasks_open": state["repos"]["engine"]["tasks"]["open"]}

    def test_safe_base_offers_and_dispatches(self):
        """Safe single-blocker base → offer stands → the blocked task is
        claimable as a stack → resolve dispatches opus."""
        slice_data = self._enriched_slice()
        self.assertIn("stackable_blocker_pr", slice_data["tasks_open"][0])
        out = resolve(slice_data, "opus", fable_blocked=False)
        self.assertTrue(out.startswith("opus "), out)


if __name__ == "__main__":
    unittest.main()
