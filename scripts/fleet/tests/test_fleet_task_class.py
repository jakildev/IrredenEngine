"""Tests for fleet_task_class.resolve — the per-dispatch model-class picker.

The dispatcher launches each worker iteration with the model class of the
work it's serving (a claude process can't change model/effort after
launch), so this resolution IS the per-task model routing. The invariants
that matter:

  - pickup priority mirrors the role docs: feedback PRs, then open tasks
    (oldest first — slices arrive sorted), then needs_plan;
  - feedback class derives from review severity labels (fable opt-in via
    fleet:fable on the PR, blocking labels -> opus, nits-only -> sonnet);
  - the fable concurrency cap skips fable items rather than idling the
    lane, and yields ``defer`` (keep trigger, dispatch nothing) when ONLY
    cap-blocked fable work remains — an empty result would dispatch the
    lane default, which the claim gate then refuses, burning an iteration;
  - per-task **Effort:** overrides beat class defaults;
  - an empty/unroutable slice falls through to the lane default (covers
    reservation resumes and missing slices).
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from fleet_task_class import resolve, feedback_pr_class  # noqa: E402


def _task(issue, model=None, effort=None, owner="free", blocked=False,
          inflight_pr=None):
    return {"issue": issue, "model": model, "effort": effort,
            "owner": owner, "blocked": blocked, "inflight_pr": inflight_pr}


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


class TaskResolution(unittest.TestCase):
    def test_oldest_claimable_task_wins(self):
        out = resolve({"tasks_open": [_task("#10", "sonnet"),
                                      _task("#11", "fable")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "sonnet high 1")  # fable still queued -> more=1

    def test_effort_override_beats_class_default(self):
        out = resolve({"tasks_open": [_task("#10", "opus", effort="medium")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus medium 0")

    def test_untagged_task_uses_lane_default(self):
        out = resolve({"tasks_open": [_task("#10", None)]},
                      "sonnet", fable_blocked=False)
        self.assertEqual(out, "sonnet high 0")

    def test_owned_and_blocked_tasks_skipped(self):
        # Owned/blocked tasks are invisible: they don't dispatch AND they
        # don't count toward `more` (an unclaimable class is no reason to
        # hold the trigger open).
        out = resolve({"tasks_open": [_task("#10", "fable", owner="worker-1"),
                                      _task("#11", "opus", blocked=True),
                                      _task("#12", "opus")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0")

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
        self.assertEqual(out, "fable xhigh 0")

    def test_inflight_pr_only_candidate_defers(self):
        # The parked task is the ONLY queue item: nothing a fresh worker can
        # claim, so the lane goes quiet (defer) rather than churning a
        # lane-default no-op dispatch every tick (#1726 DoD: "goes quiet").
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "defer")

    def test_inflight_head_with_blocked_stackable_falls_through(self):
        # Mixed slice: inflight head + a plain `blocked` task that may still be
        # stackable. Must NOT defer — '' lets the dispatcher's lane-default
        # fallthrough reach the worker's stackable tier for the blocked task.
        out = resolve({"tasks_open": [
            _task("#1640", "opus", inflight_pr={"number": 1700, "parked": True}),
            _task("#1641", "opus", blocked=True),
        ]}, "opus", fable_blocked=False)
        self.assertEqual(out, "")

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
        self.assertEqual(out, "sonnet high 1")

    def test_needs_plan_runs_opus(self):
        out = resolve({"needs_plan": [{"number": 99}]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "opus xhigh 0")


class FableCap(unittest.TestCase):
    def test_cap_skips_fable_to_next_class(self):
        # more=0: cap-blocked fable is not servable, so it must not hold
        # the trigger open (the fable iteration finishing re-fires the
        # scout; the periodic safety re-arm covers the quiescent corner).
        out = resolve({"tasks_open": [_task("#10", "fable"),
                                      _task("#11", "opus")]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "opus xhigh 0")

    def test_only_capped_fable_defers(self):
        out = resolve({"tasks_open": [_task("#10", "fable")]},
                      "opus", fable_blocked=True)
        self.assertEqual(out, "defer")

    def test_uncapped_fable_dispatches_xhigh(self):
        out = resolve({"tasks_open": [_task("#10", "fable")]},
                      "opus", fable_blocked=False)
        self.assertEqual(out, "fable xhigh 0")


class EmptySlice(unittest.TestCase):
    def test_empty_slice_falls_through_to_lane_default(self):
        self.assertEqual(resolve({}, "opus", fable_blocked=False), "")
        self.assertEqual(resolve({"tasks_open": [], "feedback_prs": []},
                                 "sonnet", fable_blocked=False), "")


if __name__ == "__main__":
    unittest.main()
