"""Tests for fetch_task_queue's `plan_gated` capture in fleet-state-scout (#2740).

This is the reachability half of the retract round-trip, and the reason the
candidate list is NOT derived from `tasks.open`:

  - `fleet:plan-review` rows are dropped by a `continue` BEFORE the task dict is
    built, so they never appear in any section. A tasks.open-derived detector
    would cover only the `fleet:needs-plan` arm — the same shape as the defect
    itself (the input-set builder filters the target before the detector runs).
  - A CLAIMED issue routes to `tasks.in_progress`, not `tasks.open`. A gate
    landing on a claimed issue is the #2734 incident shape exactly.

So the capture sits inside the loop above the section filters, and these tests
pin it there: each case asserts the issue is in `plan_gated` AND states which
section (if any) it reached, so a future refactor that moves the capture below a
`continue` fails here rather than going quietly half-blind.

The parked labels (`fleet:needs-human`, `fleet:gated`) are excluded on purpose:
those issues are already out of `tasks.open` unconditionally, so there is no
pickup defect to retract and their handling stays literally unchanged.

`human:review-plan` was retired by PR #3112 and must NOT re-arm as a gate.
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


def _issue(number, labels, body="**Model:** opus\n**Blocked by:** (none)"):
    return {"number": number, "title": f"task {number}", "body": body,
            "labels": [{"name": n} for n in labels],
            "updated_at": "2026-09-10T00:00:00Z"}


def _run(issues):
    """Call fetch_task_queue with the REST layer stubbed to `issues`."""
    original = _mod._rest_list
    _mod._rest_list = lambda *a, **k: issues
    try:
        return _mod.fetch_task_queue("jakildev/IrredenEngine")
    finally:
        _mod._rest_list = original


class PlanGatedCapture(unittest.TestCase):
    def test_needs_plan_free_row(self):
        s = _run([_issue(700, ["fleet:queued", "fleet:opus", "fleet:needs-plan"])])
        self.assertEqual(s["plan_gated"], [700])
        # This arm DOES reach tasks.open — which is why it was the one a
        # tasks.open-derived detector would have covered.
        self.assertEqual([t["issue"] for t in s["open"]], ["#700"])

    def test_plan_review_row_is_captured_though_it_reaches_no_section(self):
        s = _run([_issue(701, ["fleet:queued", "fleet:opus", "fleet:plan-review"])])
        self.assertEqual(s["plan_gated"], [701],
                         "plan-review must be captured above the `continue`")
        self.assertEqual(s["open"], [])
        self.assertEqual(s["in_progress"], [],
                         "the row is dropped from every section — a section-derived "
                         "candidate source cannot see this arm at all")

    def test_claimed_gated_row_is_captured(self):
        s = _run([_issue(702, ["fleet:queued", "fleet:opus", "fleet:needs-plan",
                               "fleet:claim-mac-pool-3"])])
        self.assertEqual(s["plan_gated"], [702])
        self.assertEqual([t["issue"] for t in s["in_progress"]], ["#702"],
                         "a claimed row routes to in_progress, not open")

    def test_gate_free_row_is_not_captured(self):
        s = _run([_issue(703, ["fleet:queued", "fleet:opus"])])
        self.assertEqual(s["plan_gated"], [],
                         "negative control: an ordinary queued task is never retracted")
        self.assertEqual([t["issue"] for t in s["open"]], ["#703"])

    def test_retired_human_review_plan_is_inert(self):
        s = _run([_issue(704, ["fleet:queued", "fleet:opus", "human:review-plan"])])
        self.assertEqual(s["plan_gated"], [],
                         "human:review-plan was retired (#3112); honoring it here "
                         "would re-arm a dead gate")

    def test_parked_issues_are_excluded(self):
        for park in ("fleet:needs-human", "fleet:gated"):
            with self.subTest(park=park):
                s = _run([_issue(705, ["fleet:queued", "fleet:opus",
                                       "fleet:plan-review", park])])
                self.assertEqual(s["plan_gated"], [],
                                 f"{park} already holds the issue out of tasks.open")

    def test_multiple_candidates_are_sorted(self):
        s = _run([
            _issue(720, ["fleet:queued", "fleet:plan-review"]),
            _issue(710, ["fleet:queued", "fleet:needs-plan"]),
            _issue(715, ["fleet:queued", "fleet:opus"]),
        ])
        self.assertEqual(s["plan_gated"], [710, 720])

    def test_pull_requests_never_reach_the_candidate_list(self):
        pr = _issue(730, ["fleet:queued", "fleet:needs-plan"])
        pr["pull_request"] = {"url": "https://example/pr/730"}
        s = _run([pr])
        self.assertEqual(s["plan_gated"], [])


class GateSetDriftGuard(unittest.TestCase):
    """The retract predicate and the ingest skip set are deliberately separate
    constants — coupling them would make any future addition to the skip set
    silently become a retract gate. But the containment must hold in one
    direction: a planning gate that holds ingest and is NOT retractable
    reproduces #2740 exactly (the gate blocks queuing but not pickup)."""

    def test_every_retract_gate_also_holds_ingest(self):
        self.assertLessEqual(
            set(_mod._PLAN_GATE_LABELS), set(_mod._INGEST_SKIP_LABELS),
            "a retract gate that does not hold the ingest add path would be "
            "retracted and immediately re-stamped — a treadmill")

    def test_parks_are_the_labels_fetch_task_queue_skips(self):
        # _RETRACT_PARK_LABELS is excluded from the candidate set on the
        # grounds that fetch_task_queue already drops those rows. If that
        # stops being true the exclusion silently becomes a live gap.
        for park in _mod._RETRACT_PARK_LABELS:
            with self.subTest(park=park):
                s = _run([_issue(900, ["fleet:queued", "fleet:opus", park])])
                self.assertEqual(s["open"], [], f"{park} must drop from open")
                self.assertEqual(s["in_progress"], [],
                                 f"{park} must drop from in_progress")

    def test_retired_gate_is_not_in_the_predicate(self):
        self.assertNotIn("human:review-plan", _mod._PLAN_GATE_LABELS,
                         "PR #3112 retired this gate; re-arming it here would "
                         "contradict test_fleet_queue_ingest_review_plan_inert.sh")


if __name__ == "__main__":
    unittest.main()
