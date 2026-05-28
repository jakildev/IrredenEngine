"""Tests for the genuine-ship predicate in fleet_scope_shipped.

Covers two false-positive classes:

* #1304 — fleet-queue-ingest's scope-shipped pre-flight used to trust prs[0]
  from a `gh pr list --search '#N'` query, but GitHub matches the bare token N
  anywhere (titles, bodies, comments, line numbers, relevance). Incidental
  hits with no literal #N must be rejected.
* #1260 / #1269 — a bare word-boundary #N in a PR *body* is also not proof:
  PRs cite issues they explicitly do NOT fix ("downstream issues (#1260)",
  "pre-existing #1269", "filed as #1269", "Refs #N"). A body ref counts only
  when a closing-action verb sits directly before it; a title ref is trusted
  as-is (the `#N: <desc>` PR-naming convention).

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

    def test_genuine_closing_verb_in_body(self):
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

    # --- mentioned-but-not-shipped body refs (#1260 / #1269) ---

    def test_downstream_body_mention_rejected(self):
        # The #1260 <- #1282 shape: PR names #1260 as explicitly-NOT-fixed work.
        self.assertFalse(pr_references_issue(
            "#1271: demo: IRShapeDebug --spin-yaw",
            "bug-fixing is downstream issues (#1256, #1260, etc). This PR ships "
            "the regression scaffolding.", 1260))

    def test_pre_existing_body_mention_rejected(self):
        # The #1269 <- #1265 shape: PR cites #1269 as a pre-existing failure.
        self.assertFalse(pr_references_issue(
            "#1258: render: camera pitch/roll",
            "MatchesStd140Packing fails on origin/master independently of this "
            "change (filed as #1269). 997/998 pass; the one failure is the "
            "pre-existing #1269.", 1269))

    def test_refs_body_mention_rejected(self):
        # "Refs #N" deliberately does not auto-close — not ship evidence.
        self.assertFalse(pr_references_issue("title", "Using Refs #1271 here", 1271))

    def test_closing_verb_does_not_bind_across_sentence(self):
        # A verb earlier in the body must not bind to a later unrelated #N.
        self.assertFalse(pr_references_issue(
            "", "Fixes #1258. Also see downstream #1260.", 1260))

    def test_title_ref_counts_even_with_unrelated_body_mention(self):
        # The same PR that mentions #1269 in its body genuinely ships #1258.
        self.assertTrue(pr_references_issue(
            "#1258: render: camera pitch/roll", "pre-existing #1269 still fails", 1258))

    def test_closing_verb_variants_in_body(self):
        for body in ("fixed #1300", "resolves #1300", "Resolved: #1300",
                     "implements #1300", "shipped #1300", "supersedes #1300",
                     "closes #1300", "Fixes #1300"):
            self.assertTrue(pr_references_issue("", body, 1300), body)


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

    def test_real_world_1260_not_shipped_by_1282(self):
        # #1282 only mentions #1260 as downstream-not-fixed -> no ship.
        prs = [_pr(1282, "#1271: demo: IRShapeDebug --spin-yaw",
                   "bug-fixing is downstream issues (#1256, #1260, etc)")]
        self.assertIsNone(select_shipped_pr(prs, 1260))

    def test_real_world_1265_ships_1258_not_1269(self):
        # #1265 closes #1258 but only cites #1269 as a pre-existing failure.
        pr = _pr(1265, "#1258: render: camera pitch/roll",
                 "Closes #1258. The one failure is the pre-existing #1269.")
        self.assertEqual(select_shipped_pr([pr], 1258)["number"], 1265)
        self.assertIsNone(select_shipped_pr([pr], 1269))


if __name__ == "__main__":
    unittest.main()
