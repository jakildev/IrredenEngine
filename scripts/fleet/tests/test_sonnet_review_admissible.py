"""Tests for the reviewer-lane admissibility predicates in fleet-state-scout.

The sonnet lane admits a PR with no verdict label OR a re-review trigger
(`RECHECK_LABELS`). `fleet:needs-opus-recheck` deliberately sets no verdict
label — the opus verdict swap is what does — so before this guard an
escalated PR was re-elected by the sonnet lane on every tick: a full pass on
the same head, the same `Opus recheck required:` ending, the same escalation
edge, and the projection unchanged. Both sonnet slots spent the afternoon
confirming one PR while the unreviewed queue waited.

The control that inverts is the escalated-with-no-trigger PR: the sonnet
lane must not admit it, the opus lane must, and an author push (a RECHECK
label) must bring the sonnet lane back. Asserting through both the
predicate and the slice keeps the dispatcher's pick list — what actually
launches a pane — in the assertion.

Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_scout = importlib.util.module_from_spec(_spec)
_loader.exec_module(_scout)

sonnet_review_admissible = _scout.sonnet_review_admissible
opus_review_admissible = _scout.opus_review_admissible
slice_sonnet_reviewer = _scout.slice_sonnet_reviewer
slice_opus_reviewer = _scout.slice_opus_reviewer


def _pr(number, labels):
    return {"number": number, "title": f"pr {number}", "headRefName": f"claude/{number}-x",
            "labels": list(labels), "mergeable": "MERGEABLE", "isDraft": False,
            "updatedAt": "2026-09-14T22:00:00Z"}


def _state(*prs):
    return {"repos": {"engine": {"prs": list(prs), "plan_review": []}}}


def _sonnet_numbers(*prs):
    return [p["number"] for p in slice_sonnet_reviewer(_state(*prs))["candidate_prs"]]


def _opus_numbers(*prs):
    return [p["number"] for p in slice_opus_reviewer(_state(*prs))["flagged_prs"]]


class EscalatedPrBelongsToTheOpusLane(unittest.TestCase):
    ESCALATED = ["fleet:needs-opus-recheck", "fleet:author-claude"]

    def test_escalated_pr_with_no_trigger_is_not_a_sonnet_candidate(self):
        pr = _pr(3202, self.ESCALATED)
        self.assertFalse(sonnet_review_admissible(pr, set(pr["labels"])))
        self.assertEqual(_sonnet_numbers(pr), [])

    def test_escalated_pr_is_an_opus_candidate(self):
        pr = _pr(3202, self.ESCALATED)
        self.assertTrue(opus_review_admissible(pr, set(pr["labels"])))
        self.assertEqual(_opus_numbers(pr), [3202])

    def test_has_nits_beside_the_escalation_is_not_a_verdict(self):
        # REVIEWER-PROTOCOL: has-nits rides with the escalation when sonnet
        # had nits; it is not a verdict and must not re-admit the PR.
        pr = _pr(3202, self.ESCALATED + ["fleet:has-nits"])
        self.assertFalse(sonnet_review_admissible(pr, set(pr["labels"])))

    def test_author_push_brings_the_sonnet_lane_back(self):
        for trigger in ("fleet:changes-made", "human:re-review"):
            with self.subTest(trigger=trigger):
                pr = _pr(3202, self.ESCALATED + [trigger])
                self.assertTrue(sonnet_review_admissible(pr, set(pr["labels"])))
                self.assertEqual(_sonnet_numbers(pr), [3202])
                # The opus lane still sees it; the two are not exclusive.
                self.assertEqual(_opus_numbers(pr), [3202])


class UnchangedSonnetAdmission(unittest.TestCase):
    """The pre-existing admission rules, pinned so the guard adds only the
    escalation case."""

    def test_unreviewed_pr_is_a_candidate(self):
        pr = _pr(3395, ["fleet:author-codex"])
        self.assertEqual(_sonnet_numbers(pr), [3395])

    def test_verdicted_pr_without_a_trigger_is_not(self):
        for verdict in ("fleet:approved", "fleet:needs-fix", "fleet:has-nits", "fleet:blocker"):
            with self.subTest(verdict=verdict):
                pr = _pr(3362, [verdict])
                self.assertEqual(_sonnet_numbers(pr), [])

    def test_verdicted_pr_with_a_trigger_is(self):
        pr = _pr(3362, ["fleet:approved", "fleet:changes-made"])
        self.assertEqual(_sonnet_numbers(pr), [3362])

    def test_skip_labels_still_win_over_a_trigger(self):
        pr = _pr(3215, ["fleet:changes-made", "fleet:amending-mac-pool-4"])
        self.assertEqual(_sonnet_numbers(pr), [])
        pr = _pr(3215, ["fleet:needs-opus-recheck", "fleet:changes-made",
                        "fleet:semantic-conflict"])
        self.assertEqual(_sonnet_numbers(pr), [])
        self.assertEqual(_opus_numbers(pr), [])

    def test_slice_order_is_preserved(self):
        prs = [_pr(3390, []), _pr(3202, ["fleet:needs-opus-recheck"]), _pr(3395, [])]
        self.assertEqual(_sonnet_numbers(*prs), [3390, 3395])


if __name__ == "__main__":
    unittest.main()
