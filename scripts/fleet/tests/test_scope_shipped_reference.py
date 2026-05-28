"""Tests for the genuine-reference predicate in fleet_scope_shipped.

Covers the #1304 false-positives: fleet-queue-ingest's scope-shipped
pre-flight used to trust prs[0] from a `gh pr list --search '#N'` query, but
GitHub matches the bare token N anywhere (titles, bodies, comments, line
numbers, relevance). select_shipped_pr() must reject those incidental hits and
only flag a PR that carries a genuine word-boundary #N cross-reference.

The module is a real .py, but it is loaded via importlib (mirroring
test_enrich_stackable_blocker_prs.py) so the test runs regardless of cwd.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_MODULE = Path(__file__).parent.parent / "fleet_scope_shipped.py"

_loader = importlib.machinery.SourceFileLoader("fleet_scope_shipped", str(_MODULE))
_spec = importlib.util.spec_from_loader("fleet_scope_shipped", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
pr_references_issue = _mod.pr_references_issue
select_shipped_pr = _mod.select_shipped_pr


def _pr(number, title="", body=""):
    return {"number": number, "title": title, "body": body, "url": "", "mergedAt": ""}


class PrReferencesIssue(unittest.TestCase):
    def test_genuine_closes_in_body(self):
        self.assertTrue(pr_references_issue("some PR", "Closes #1300.", 1300))

    def test_genuine_hash_ref_in_body(self):
        self.assertTrue(pr_references_issue("title", "supersedes #1284 here", 1284))

    def test_genuine_ref_in_title(self):
        self.assertTrue(pr_references_issue("#1300: render fix", "", 1300))

    def test_bare_number_no_hash_is_not_a_reference(self):
        # The #1243 -> #1300 shape: "1300" appears as a line number, no '#'.
        self.assertFalse(
            pr_references_issue("delete orphaned queue scripts",
                                "touches lines 71, 176, 1300, 3619", 1300))

    def test_no_literal_number_at_all(self):
        # The #1160 -> #1284 shape: relevance-only hit, number absent entirely.
        self.assertFalse(
            pr_references_issue("codegen: emit per-component tick", "body text", 1284))

    def test_longer_number_does_not_match_prefix(self):
        # #13000 must not satisfy n=1300.
        self.assertFalse(pr_references_issue("", "see #13000", 1300))

    def test_longer_number_does_not_match_suffix(self):
        # #1300 query must not be satisfied by a leading-digit token.
        self.assertFalse(pr_references_issue("", "see #21300", 1300))

    def test_hash_ref_with_adjacent_punctuation(self):
        self.assertTrue(pr_references_issue("", "fixes (#1300) finally", 1300))

    def test_falsy_issue_number(self):
        self.assertFalse(pr_references_issue("#0 ref", "#0 ref", 0))

    def test_alphanumeric_before_hash_is_not_a_reference(self):
        # "abc#1300" — '#' immediately preceded by a word char must be rejected.
        self.assertFalse(pr_references_issue("", "abc#1300 fix", 1300))

    def test_non_numeric_truthy_n_returns_false(self):
        # int(n) raises ValueError for a truthy non-numeric string.
        self.assertFalse(pr_references_issue("", "Closes #1300", "abc"))


class SelectShippedPr(unittest.TestCase):
    def test_empty_candidates(self):
        self.assertIsNone(select_shipped_pr([], 1300))

    def test_no_genuine_reference_returns_none(self):
        prs = [
            _pr(1243, "delete orphaned queue scripts", "lines 71, 176, 1300"),
            _pr(1160, "codegen: emit per-component tick", "unrelated"),
        ]
        self.assertIsNone(select_shipped_pr(prs, 1300))

    def test_picks_first_genuine_reference(self):
        prs = [
            _pr(1243, "delete orphaned queue scripts", "lines 71, 176, 1300"),
            _pr(900, "real shipper", "Closes #1300"),
        ]
        best = select_shipped_pr(prs, 1300)
        self.assertIsNotNone(best)
        self.assertEqual(best["number"], 900)

    def test_missing_title_body_keys(self):
        # Defensive: a candidate dict without title/body must not raise.
        self.assertIsNone(select_shipped_pr([{"number": 5}], 1300))


if __name__ == "__main__":
    unittest.main()
