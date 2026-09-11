"""Tests for fleet-up's Opus reviewer bootstrap predicate."""

import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_SCOUT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCOUT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_scout = importlib.util.module_from_spec(_spec)
_loader.exec_module(_scout)
slice_opus_reviewer = _scout.slice_opus_reviewer


def _opus_reviewer_actionable():
    """Lift the bootstrap predicate without executing fleet-up's heredoc."""
    source = (Path(__file__).parent.parent / "fleet-up").read_text().splitlines()
    start = next(i for i, line in enumerate(source)
                 if line.startswith("def opus_reviewer_actionable(p):"))
    end = next(i for i, line in enumerate(source[start:], start)
               if line.startswith("# Host key + smoke-label map"))
    namespace = {}
    exec("\n".join(source[start:end]), namespace)  # noqa: S102 - defs only
    return namespace["opus_reviewer_actionable"]


def _slice(*, prs=None, plan_review=None):
    state = {"repos": {"engine": {
        "prs": prs or [], "plan_review": plan_review or [],
    }}}
    return slice_opus_reviewer(state)


def _pr(labels):
    return {"number": 1, "title": "test", "labels": labels}


def _issue(labels):
    return {"number": 2, "title": "test", "labels": labels}


class OpusReviewerBootstrapActionable(unittest.TestCase):

    def setUp(self):
        self.actionable = _opus_reviewer_actionable()

    def test_plan_review_only_is_actionable(self):
        projection = _slice(plan_review=[_issue(["fleet:plan-review"])])
        self.assertTrue(self.actionable(projection))

    def test_flagged_pr_only_is_actionable(self):
        projection = _slice(prs=[_pr(["fleet:needs-opus-recheck"])])
        self.assertTrue(self.actionable(projection))

    def test_empty_projection_is_not_actionable(self):
        self.assertFalse(self.actionable(_slice()))


if __name__ == "__main__":
    unittest.main()
