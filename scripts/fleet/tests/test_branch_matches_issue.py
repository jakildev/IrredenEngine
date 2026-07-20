"""Tests for the shared branch<->issue matcher (fleet_branch_match.py, #1425).

Pins the contract both fleet-claim and fleet-state-scout rely on:
  - engine accepts only `claude/<N>-`
  - game accepts both `claude/<N>-` and `claude/game-<N>-`
  - cross-repo and wrong-issue branches do not match
  - issue_from_branch is the repo-agnostic inverse (strips optional game-)
  - (#2419) an improvised `issue-<N>` token branch, or a `Closes #N` body,
    resolves to the issue so a live PR is not swept into a duplicate claim
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from fleet_branch_match import (
    _is_game,
    body_closes_issue,
    branch_matches_issue,
    issue_branch_prefixes,
    issue_from_branch,
    issue_pr_state,
)


class BranchMatchesIssue(unittest.TestCase):
    # --- acceptance criteria (#1425) -------------------------------------
    def test_game_legacy_prefix_matches_game(self):
        self.assertTrue(branch_matches_issue("claude/game-105-x", 105, "game"))

    def test_engine_prefix_matches_engine(self):
        self.assertTrue(branch_matches_issue("claude/105-x", 105, "engine"))

    def test_cross_repo_negative(self):
        # A game-shaped branch is not an engine branch.
        self.assertFalse(branch_matches_issue("claude/game-105-x", 105, "engine"))

    def test_wrong_issue_negative(self):
        self.assertFalse(branch_matches_issue("claude/106-x", 105, "engine"))
        self.assertFalse(branch_matches_issue("claude/game-106-x", 105, "game"))

    # --- game also accepts the new prefix-less form ----------------------
    def test_game_accepts_engine_form(self):
        # New game branches drop the game- prefix; the matcher must still
        # recognize them so the convention can migrate.
        self.assertTrue(branch_matches_issue("claude/105-x", 105, "game"))

    # --- the trailing '-' guards the issue-number boundary ---------------
    def test_issue_number_is_not_a_prefix_of_another(self):
        self.assertFalse(branch_matches_issue("claude/1050-x", 105, "engine"))
        self.assertFalse(branch_matches_issue("claude/1050-x", 105, "game"))
        self.assertFalse(branch_matches_issue("claude/game-1050-x", 105, "game"))

    # --- repo identifiers the call sites actually pass -------------------
    def test_repo_identifier_forms(self):
        # owner/repo paths (fleet-claim's $repo)
        self.assertTrue(branch_matches_issue("claude/game-105-x", 105, "jakildev/irreden"))
        self.assertFalse(branch_matches_issue("claude/game-105-x", 105, "jakildev/IrredenEngine"))
        # --repo namespace token (empty == engine)
        self.assertTrue(branch_matches_issue("claude/105-x", 105, ""))
        self.assertFalse(branch_matches_issue("claude/game-105-x", 105, ""))

    def test_is_game(self):
        for game in ("game", "irreden", "jakildev/irreden", "JAKILDEV/IRREDEN"):
            self.assertTrue(_is_game(game), game)
        for engine in ("", "engine", "jakildev/IrredenEngine", "jakildev/irredenengine", None):
            self.assertFalse(_is_game(engine), engine)

    # --- issue arg accepts int or str (with or without '#') --------------
    def test_issue_arg_int_str_hash(self):
        for issue in (105, "105", "#105"):
            self.assertTrue(branch_matches_issue("claude/105-x", issue, "engine"), issue)

    def test_issue_branch_prefixes(self):
        self.assertEqual(issue_branch_prefixes("engine", 105), ["claude/105-"])
        self.assertEqual(
            issue_branch_prefixes("game", 105),
            ["claude/105-", "claude/game-105-"],
        )

    # --- #2419: the improvised `issue-<N>` token form --------------------
    def test_issue_token_form_matches(self):
        # The exact branch shapes from the #2419 duplicate-PR incident.
        self.assertTrue(
            branch_matches_issue("claude/game-worker-3-issue-255", 255, "game"))
        self.assertTrue(
            branch_matches_issue("claude/worker-4-issue-255", 255, "engine"))
        # game token branch is repo-agnostic for the token, but a game- infix
        # branch checked as engine still resolves via the token (the number is
        # what the sweep needs; the head-prefix cross-repo guard is separate).
        self.assertTrue(
            branch_matches_issue("claude/game-worker-3-issue-255", 255, "engine"))

    def test_issue_token_word_bounded(self):
        # `issue-25` must NOT satisfy #255 (word-bounded digit run), and the
        # right boundary rejects a longer number too.
        self.assertFalse(branch_matches_issue("claude/w-3-issue-25", 255, "engine"))
        self.assertTrue(branch_matches_issue("claude/w-3-issue-25", 25, "engine"))
        self.assertFalse(branch_matches_issue("claude/w-3-issue-2550", 255, "engine"))

    def test_issue_token_left_boundary(self):
        # `reissue-255` is not an `issue-<N>` token (must open a -/ segment).
        self.assertFalse(branch_matches_issue("claude/reissue-255", 255, "engine"))
        # `claude/issue-255-topic` (token right after the slash) does match.
        self.assertTrue(branch_matches_issue("claude/issue-255-topic", 255, "engine"))

    def test_issue_token_requires_claude_prefix(self):
        # A non-fleet branch with an issue- token is not a working branch.
        self.assertFalse(branch_matches_issue("feature/issue-255", 255, "engine"))
        self.assertFalse(branch_matches_issue("issue-255", 255, "engine"))

    def test_number_first_form_still_wins(self):
        # Regression guard: widening the matcher didn't break the primary form.
        self.assertTrue(branch_matches_issue("claude/255-topic", 255, "engine"))
        self.assertFalse(branch_matches_issue("claude/1050-x", 105, "engine"))


class IssueFromBranch(unittest.TestCase):
    def test_engine_branch(self):
        self.assertEqual(issue_from_branch("claude/105-gear-foo"), 105)

    def test_legacy_game_branch(self):
        # The reconcile head_issue bug: this used to return None.
        self.assertEqual(issue_from_branch("claude/game-105-gear-foo"), 105)

    def test_non_claude_branch(self):
        self.assertIsNone(issue_from_branch("master"))
        self.assertIsNone(issue_from_branch("feature/105-x"))

    def test_no_digits(self):
        self.assertIsNone(issue_from_branch("claude/game-scratch"))
        self.assertIsNone(issue_from_branch("claude/topic-only"))

    def test_empty_and_none(self):
        self.assertIsNone(issue_from_branch(""))
        self.assertIsNone(issue_from_branch(None))

    # --- #2419: fall back to an `issue-<N>` token ------------------------
    def test_issue_token_fallback(self):
        self.assertEqual(
            issue_from_branch("claude/game-worker-3-issue-255"), 255)
        self.assertEqual(issue_from_branch("claude/worker-4-issue-255"), 255)
        self.assertEqual(issue_from_branch("claude/issue-42-topic"), 42)

    def test_number_first_wins_over_token(self):
        # A number-first branch whose topic merely contains 'issue-<M>' reads
        # the leading number, not the token.
        self.assertEqual(issue_from_branch("claude/255-issue-tracker"), 255)
        self.assertEqual(issue_from_branch("claude/game-105-issue-99-fix"), 105)


class IssuePrState(unittest.TestCase):
    """Classifier behind the #1488 claim-lifecycle fixes: a matching PR that
    is design-blocked/-unblocked is PARKED (awaiting resume), not active, so
    its issue-side claim labels are sweepable like a no-PR abandon."""

    def _pr(self, head, *labels):
        return {"headRefName": head, "labels": [{"name": n} for n in labels]}

    def test_active_when_matching_pr_not_parked(self):
        prs = [self._pr("claude/1488-x", "fleet:wip", "fleet:approved")]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "active")

    def test_parked_on_design_blocked(self):
        prs = [self._pr("claude/1488-x", "fleet:wip", "fleet:design-blocked")]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "parked")

    def test_parked_on_design_unblocked(self):
        prs = [self._pr("claude/1488-x", "fleet:design-unblocked")]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "parked")

    def test_none_when_no_branch_matches(self):
        prs = [self._pr("claude/9999-x", "fleet:wip")]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "none")
        self.assertEqual(issue_pr_state([], 1488, "engine"), "none")

    def test_active_wins_over_parked_dup(self):
        # Two PRs match the same issue; one parked, one active. Any active
        # matching PR means the claim is still live work -> "active".
        prs = [
            self._pr("claude/1488-parked", "fleet:design-blocked"),
            self._pr("claude/1488-live", "fleet:wip"),
        ]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "active")

    def test_game_legacy_branch_parked(self):
        # The matcher must recognize the legacy game- prefix here too.
        prs = [self._pr("claude/game-45-x", "fleet:design-blocked")]
        self.assertEqual(issue_pr_state(prs, 45, "game"), "parked")
        # A game-shaped branch is not an engine branch -> no match.
        self.assertEqual(issue_pr_state(prs, 45, "engine"), "none")

    def test_malformed_records_are_ignored(self):
        # Defensive: missing/None labels and non-dict label entries.
        prs = [
            {"headRefName": "claude/1488-x"},                       # no labels key
            {"headRefName": "claude/1488-y", "labels": None},
            {"headRefName": "claude/1488-z", "labels": ["junk", None]},
        ]
        # None are parked, all match -> first match is active.
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "active")

    # --- #2419: an `issue-<N>` token branch survives the sweep -----------
    def test_active_via_issue_token_branch(self):
        # Acceptance (b): the exact incident branch is live work, so the
        # cleanup sweep (which keeps a claim only on "active") won't free it.
        prs = [{"headRefName": "claude/game-worker-3-issue-255",
                "labels": [{"name": "fleet:wip"}]}]
        self.assertEqual(issue_pr_state(prs, 255, "game"), "active")

    # --- #2419: body `Closes #N` is a second liveness signal -------------
    def test_active_via_closes_body_untrackable_branch(self):
        # Branch name the matcher can't tie to #1488, but the body closes it:
        # still live work, so the claim must stay (not swept -> no duplicate).
        prs = [{"headRefName": "claude/some-weird-branch",
                "body": "Closes #1488", "labels": []}]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "active")

    def test_parked_via_closes_body(self):
        # The body signal is classified the same as a branch match: a parked
        # PR that closes the issue is parked, not active.
        prs = [{"headRefName": "claude/weird",
                "body": "Fixes #1488", "labels": [{"name": "fleet:design-blocked"}]}]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "parked")

    def test_closes_body_word_bounded(self):
        # `Closes #14` must not keep the claim on #1488.
        prs = [{"headRefName": "claude/weird", "body": "Closes #14", "labels": []}]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "none")

    def test_missing_body_is_backward_compatible(self):
        # A caller that fetches only headRefName+labels (no body) simply
        # forgoes the second signal — an untrackable branch is "none".
        prs = [{"headRefName": "claude/weird", "labels": [{"name": "fleet:wip"}]}]
        self.assertEqual(issue_pr_state(prs, 1488, "engine"), "none")


class BodyClosesIssue(unittest.TestCase):
    def test_close_keywords(self):
        for verb in ("Closes", "close", "closed", "Fixes", "fix", "fixed",
                     "Resolves", "resolve", "resolved"):
            self.assertTrue(body_closes_issue("%s #255" % verb, 255), verb)

    def test_case_insensitive_and_embedded(self):
        self.assertTrue(body_closes_issue("...work here.\n\nCLOSES #255\n", 255))
        self.assertTrue(body_closes_issue("this fixes #255 fully", 255))

    def test_word_bounded_number(self):
        self.assertFalse(body_closes_issue("Closes #2550", 255))
        self.assertFalse(body_closes_issue("Closes #25", 255))

    def test_requires_a_close_keyword(self):
        self.assertFalse(body_closes_issue("see #255", 255))
        self.assertFalse(body_closes_issue("relates to #255", 255))

    def test_issue_arg_forms_and_empty(self):
        for issue in (255, "255", "#255"):
            self.assertTrue(body_closes_issue("Closes #255", issue), issue)
        self.assertFalse(body_closes_issue("", 255))
        self.assertFalse(body_closes_issue(None, 255))


if __name__ == "__main__":
    unittest.main()
