"""Tests for fleet-stale-sweep — stale PR detection + epic auto-close.

Imports the script via importlib because it has no .py extension,
mirroring test_queue_manager_projection.py and friends.
"""
import datetime as dt
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-stale-sweep"

_loader = importlib.machinery.SourceFileLoader("fleet_stale_sweep", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_stale_sweep", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

extract_child_issue_numbers = _mod.extract_child_issue_numbers
parse_iso8601 = _mod.parse_iso8601
sweep_stale_prs = _mod.sweep_stale_prs


class ExtractChildren(unittest.TestCase):
    """The epic-children parser is the load-bearing bit of the auto-close
    path. False positives reap epics prematurely; false negatives just
    delay closure. So the bias is conservative."""

    def test_unchecked_checkbox(self):
        self.assertEqual(extract_child_issue_numbers("- [ ] #123"), [123])

    def test_checked_checkbox(self):
        self.assertEqual(extract_child_issue_numbers("- [x] #123"), [123])

    def test_capital_x(self):
        self.assertEqual(extract_child_issue_numbers("- [X] #123"), [123])

    def test_multiple_children_preserves_order(self):
        body = (
            "Phase 1\n"
            "- [ ] #100\n"
            "- [x] #101 — done\n"
            "Phase 2\n"
            "- [ ] #102 — pending\n"
        )
        self.assertEqual(extract_child_issue_numbers(body), [100, 101, 102])

    def test_dedupes_repeated_refs(self):
        body = "- [ ] #50\n- [ ] #50 — listed twice\n"
        self.assertEqual(extract_child_issue_numbers(body), [50])

    def test_skips_prose_references(self):
        # Critical: an epic that says "see also #99" in prose must NOT count
        # #99 as a child. The checklist form is the contract.
        body = (
            "## Background\n"
            "See also #99 for context.\n"
            "Filed by @user as a follow-up to #88.\n"
            "\n"
            "## Children\n"
            "- [ ] #100\n"
        )
        self.assertEqual(extract_child_issue_numbers(body), [100])

    def test_skips_non_checklist_bullets(self):
        body = (
            "- #50 is the parent  \n"
            "- not a checklist item\n"
            "- [ ] #51 — real child\n"
        )
        self.assertEqual(extract_child_issue_numbers(body), [51])

    def test_empty_body(self):
        self.assertEqual(extract_child_issue_numbers(""), [])
        self.assertEqual(extract_child_issue_numbers(None), [])

    def test_indented_checklist(self):
        # Nested under a parent bullet — still counts.
        body = "## Phase A\n  - [ ] #200\n  - [x] #201\n"
        self.assertEqual(extract_child_issue_numbers(body), [200, 201])


class ParseIso(unittest.TestCase):
    def test_z_suffix(self):
        ts = parse_iso8601("2026-05-21T22:25:34Z")
        self.assertIsNotNone(ts)
        self.assertEqual(ts.year, 2026)
        self.assertEqual(ts.tzinfo, dt.timezone.utc)

    def test_empty_returns_none(self):
        self.assertIsNone(parse_iso8601(""))
        self.assertIsNone(parse_iso8601(None))

    def test_malformed_returns_none(self):
        self.assertIsNone(parse_iso8601("not a date"))


class StalePrSweep(unittest.TestCase):
    """sweep_stale_prs filters carefully — only fleet:wip, only stale,
    only un-flagged. Anything else is left for other reapers to handle."""

    def _state(self, prs):
        return {"repos": {"engine": {"prs": prs}}}

    def _pr(self, *, number, updated_days_ago, labels):
        ts = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=updated_days_ago)
        return {
            "number": number,
            "labels": labels,
            "updatedAt": ts.strftime("%Y-%m-%dT%H:%M:%SZ"),
        }

    def test_fresh_wip_pr_not_flagged(self):
        state = self._state([
            self._pr(number=1, updated_days_ago=2, labels=["fleet:wip"]),
        ])
        n = sweep_stale_prs(state, stale_days=7, dry_run=True)
        self.assertEqual(n, 0)

    def test_stale_wip_pr_flagged(self):
        state = self._state([
            self._pr(number=1, updated_days_ago=10, labels=["fleet:wip"]),
        ])
        n = sweep_stale_prs(state, stale_days=7, dry_run=True)
        self.assertEqual(n, 1)

    def test_stale_but_already_stalled_skipped(self):
        # Idempotent: if we already flagged it, don't re-flag.
        state = self._state([
            self._pr(number=1, updated_days_ago=20,
                     labels=["fleet:wip", "fleet:stalled"]),
        ])
        n = sweep_stale_prs(state, stale_days=7, dry_run=True)
        self.assertEqual(n, 0)

    def test_stale_non_wip_pr_skipped(self):
        # A stale PR that's in the review cycle (not fleet:wip) is handled
        # by a different reaper; stay out of its way.
        state = self._state([
            self._pr(number=1, updated_days_ago=20,
                     labels=["fleet:approved", "fleet:has-nits"]),
        ])
        n = sweep_stale_prs(state, stale_days=7, dry_run=True)
        self.assertEqual(n, 0)

    def test_design_blocked_wip_pr_flagged_after_threshold(self):
        # fleet:design-blocked + fleet:wip — if the architect doesn't reply
        # for 7+ days, the human should still see it surface. Architect
        # wake-up is a separate (manual) loop per the audit decision.
        state = self._state([
            self._pr(number=1, updated_days_ago=10,
                     labels=["fleet:wip", "fleet:design-blocked"]),
        ])
        n = sweep_stale_prs(state, stale_days=7, dry_run=True)
        self.assertEqual(n, 1)

    def test_pr_missing_updated_at_skipped(self):
        # Defensive: a record from a partial fetch shouldn't crash the
        # sweep — just skip and let next tick try again.
        state = self._state([{"number": 1, "labels": ["fleet:wip"]}])
        n = sweep_stale_prs(state, stale_days=7, dry_run=True)
        self.assertEqual(n, 0)


if __name__ == "__main__":
    unittest.main()
