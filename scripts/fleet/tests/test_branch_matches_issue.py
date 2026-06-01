"""Tests for the shared branch<->issue matcher (fleet_branch_match.py, #1425).

Pins the contract both fleet-claim and fleet-state-scout rely on:
  - engine accepts only `claude/<N>-`
  - game accepts both `claude/<N>-` and `claude/game-<N>-`
  - cross-repo and wrong-issue branches do not match
  - issue_from_branch is the repo-agnostic inverse (strips optional game-)
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from fleet_branch_match import (
    branch_matches_issue,
    issue_branch_prefixes,
    issue_from_branch,
    _is_game,
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


if __name__ == "__main__":
    unittest.main()
