"""Tests for the shared branch<->issue matcher (fleet_branch_match.py, #1425).

Pins the contract both fleet-claim and fleet-state-scout rely on:
  - engine accepts only `claude/<N>-`
  - game accepts both `claude/<N>-` and `claude/game-<N>-`
  - cross-repo and wrong-issue branches do not match
  - issue_from_branch is the repo-agnostic inverse (strips optional game-)
  - (#2419) an improvised `issue-<N>` token branch, or a `Closes #N` body,
    resolves to the issue so a live PR is not swept into a duplicate claim
  - the token is a *fallback*: a leading-number form suppresses it, so a
    branch naming `issue-<M>` in its topic never dual-attributes to #M
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from fleet_branch_match import (
    _is_game,
    body_closed_issue_numbers,
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
        # right boundary rejects a longer number too — in both directions:
        # `issue-255` must not satisfy #25 either.
        self.assertFalse(branch_matches_issue("claude/w-3-issue-25", 255, "engine"))
        self.assertTrue(branch_matches_issue("claude/w-3-issue-25", 25, "engine"))
        self.assertFalse(branch_matches_issue("claude/w-3-issue-2550", 255, "engine"))
        self.assertFalse(branch_matches_issue("claude/w-3-issue-255", 25, "engine"))

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

    def test_leading_number_is_authoritative_over_token(self):
        # The dual-attribution lock: a branch carrying BOTH a leading number
        # and an `issue-<N>` token resolves to the leading number ONLY — the
        # token arm is a fallback, not an additional match. Without the gate,
        # `cmd_claim`'s open-PR guard falsely refuses a fresh claim on #1425
        # because an unrelated branch names `issue-1425` in its topic.
        b = "claude/2419-fix-issue-1425-recurrence"
        self.assertTrue(branch_matches_issue(b, 2419, "engine"))
        self.assertFalse(branch_matches_issue(b, 1425, "engine"))


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
        self.assertEqual(
            issue_from_branch("claude/2419-fix-issue-1425-recurrence"), 2419)


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

    def test_closed_issue_numbers_extraction(self):
        # The all-refs form the scout's closes_issues derivation rides on:
        # keyword-gated (a bare `#99` mention is not a close) and int-typed.
        body = "Closes #10, fixes #20 and resolves #255. Mentions #99 too."
        self.assertEqual(sorted(body_closed_issue_numbers(body)), [10, 20, 255])
        self.assertEqual(body_closed_issue_numbers(""), [])
        self.assertEqual(body_closed_issue_numbers(None), [])


class ClosingKeywordInsideCode(unittest.TestCase):
    """A closing keyword inside markdown code is not a link, so is not a match.

    Ground truth for every arm is GitHub's own `closingIssuesReferences` — the
    field it auto-closes from — sampled from two real bodies that bracket the
    rule: one whose only `Closes #N` sits in a code span while arguing AGAINST
    closing (GitHub: closes nothing), and one carrying bare refs (GitHub:
    closes both). The two must land on opposite sides. See #2672.
    """

    # A body arguing against the very reference it quotes.
    NEGATED = ('The stamp is **measured false**. The matched PR #3020\'s entire '
               'diff is one documentation\nfile, and its body reads *"No '
               '`Closes #2091` \u2014 deliberate \u2026 Auto-closing on merge '
               'would\nstrand it."*\n')

    def test_quoted_negated_reference_is_not_a_close(self):
        self.assertFalse(body_closes_issue(self.NEGATED, 2091))
        self.assertEqual(body_closed_issue_numbers(self.NEGATED), [])

    def test_bare_references_still_close(self):
        body = "Reconcile R9.\n\nCloses #2939\nCloses #3205\n"
        self.assertEqual(sorted(body_closed_issue_numbers(body)), [2939, 3205])
        self.assertTrue(body_closes_issue(body, 3205))

    def test_fenced_block_is_not_a_close(self):
        body = "Example of what NOT to write:\n\n```\nCloses #255\n```\n"
        self.assertFalse(body_closes_issue(body, 255))
        self.assertEqual(body_closed_issue_numbers(body), [])

    def test_tilde_fence_is_not_a_close(self):
        body = "~~~\nCloses #255\n~~~\n"
        self.assertEqual(body_closed_issue_numbers(body), [])

    def test_backticks_inside_a_fence_do_not_leak(self):
        # Fences are stripped before spans precisely so a fence's own backtick
        # runs can't be read as span delimiters and re-expose what follows.
        body = "```\nrun `x`\nCloses #255\n```\n\nCloses #10\n"
        self.assertEqual(body_closed_issue_numbers(body), [10])

    def test_live_ref_outside_a_fence_still_matches(self):
        # The rule must not turn "body contains any code" into "body closes
        # nothing" — the failure mode that would strand every fleet PR, since
        # commit-and-push bodies routinely carry both.
        body = "Closes #255\n\n```\nsome code\n```\n"
        self.assertEqual(body_closed_issue_numbers(body), [255])
        self.assertTrue(body_closes_issue(body, 255))

    def test_span_and_live_ref_in_the_same_line(self):
        body = "Not `Closes #10`, but really Closes #20."
        self.assertEqual(body_closed_issue_numbers(body), [20])

    def test_two_spans_do_not_merge_and_eat_the_prose_between(self):
        # A greedy span regex pairs the FIRST and LAST backtick, blanking the
        # live ref sitting between two unrelated spans.
        body = "See `foo` — Closes #20 — and `bar`."
        self.assertEqual(body_closed_issue_numbers(body), [20])

    def test_double_backtick_span_is_stripped(self):
        self.assertEqual(body_closed_issue_numbers("``Closes #255``"), [])

    def test_stripping_cannot_splice_a_new_reference(self):
        # Neither deletion nor whitespace is safe here: the keyword's separator
        # is `\s+`, so blanking a span to " " still lets a keyword and a number
        # that were never adjacent form a reference. The sentinel must break it.
        self.assertEqual(body_closed_issue_numbers("Closes`x``y`#5"), [])
        self.assertEqual(body_closed_issue_numbers("Closes `foo` #5"), [])

    def test_a_fence_between_keyword_and_number_does_not_splice(self):
        body = "Closes\n\n```\ncode\n```\n\n#5\n"
        self.assertEqual(body_closed_issue_numbers(body), [])

    def test_a_longer_fence_quoting_a_shorter_one_is_fully_stripped(self):
        # CommonMark fences are 3-OR-MORE, closed by a run of the same char at
        # least that long. A body quoting fence syntax must open longer than the
        # sample; an exactly-3 matcher closes on the SAMPLE's fence and leaks
        # the rest (inherited from fleet-plan-lint's grammar).
        body = "````\nBad example:\n```\nCloses #255\n```\n````\n"
        self.assertEqual(body_closed_issue_numbers(body), [])

    def test_unclosed_backtick_fence_runs_to_end_of_body(self):
        # CommonMark: an opening fence with no closing fence encloses every line
        # "until the end of the containing block (or document)", so GitHub never
        # links what follows it. A matcher that requires a closing fence reads
        # the whole tail as prose and invents the link — the direction this
        # grammar exists to avoid, and the one a truncated or mid-edit body
        # produces most often.
        body = "Example of what NOT to write:\n\n```\nCloses #255\n"
        self.assertFalse(body_closes_issue(body, 255))
        self.assertEqual(body_closed_issue_numbers(body), [])

    def test_unclosed_tilde_fence_runs_to_end_of_body(self):
        body = "~~~\nFixes #256\n"
        self.assertFalse(body_closes_issue(body, 256))
        self.assertEqual(body_closed_issue_numbers(body), [])

    def test_an_unclosed_fence_only_swallows_what_follows_it(self):
        # The EOF arm must not become "a body containing an unclosed fence
        # closes nothing" — a real reference ABOVE the fence is still prose to
        # GitHub, and dropping it strands a merged PR's issue open.
        body = "Closes #99\n\n```\nCloses #255\n"
        self.assertEqual(body_closed_issue_numbers(body), [99])
        self.assertTrue(body_closes_issue(body, 99))
        self.assertFalse(body_closes_issue(body, 255))

    def test_a_closed_fence_still_ends_at_its_closing_fence(self):
        # Control for the arm order: with the EOF alternative tried first, a
        # well-formed block would swallow the live reference after it and every
        # assertion above would still pass.
        body = "```\nCloses #255\n```\n\nCloses #10\n"
        self.assertEqual(body_closed_issue_numbers(body), [10])

    def test_a_mixed_character_run_does_not_close_a_fence(self):
        # CommonMark closes a fence only with a run of the OPENER's character.
        # A laxer `[`~]*` surplus let ```` ```~~~ ```` close a backtick opener,
        # ending the block early and reading the code after it as prose — an
        # invented link that suppresses a claimable task.
        for body, num in (("```\nexample\n```~~~\nCloses #10\n", 10),
                          ("~~~\nexample\n~~~```\nCloses #11\n", 11),
                          ("```\nexample\n~~~\nCloses #12\n", 12)):
            with self.subTest(body=body[:24]):
                self.assertEqual(body_closed_issue_numbers(body), [])
                self.assertFalse(body_closes_issue(body, num))

    def test_an_over_indented_run_does_not_close_a_fence(self):
        # Same class, same direction: a closing fence may be indented at most
        # three spaces, and a tab is four columns. Accepting either keeps the
        # block open in GitHub while this parser ends it and invents the link.
        for body, num in (("```\nexample\n    ```\nCloses #13\n", 13),
                          ("```\nexample\n\t```\nCloses #14\n", 14)):
            with self.subTest(body=body[:24]):
                self.assertEqual(body_closed_issue_numbers(body), [])
                self.assertFalse(body_closes_issue(body, num))

    def test_a_legally_indented_closer_still_ends_its_block(self):
        # Control against over-shooting the two arms above: tightening the
        # closer must not swallow well-formed blocks. Without this, a closer
        # arm that rejected ALL indentation — or the EOF arm winning outright —
        # passes every assertion above while dropping real links.
        for body, want in (("```\nexample\n   ```\nCloses #23\n", [23]),
                           ("```\nexample\n``` \t\nCloses #24\n", [24]),
                           ("  ```\nexample\n  ```\nCloses #25\n", [25]),
                           ("```\nexample\n`````\nCloses #22\n", [22])):
            with self.subTest(body=body[:24]):
                self.assertEqual(body_closed_issue_numbers(body), want)

    def test_an_over_indented_opener_is_not_a_fence(self):
        # The opener obeys the same three-space rule as the closer, and for a
        # sharper reason: four columns of indentation (a tab is four) opens an
        # INDENTED code block, so the backticks are literal and no fenced block
        # exists to run to end-of-body. Read as a fence, the matcher strips the
        # rest of the body and drops the live reference below the sample.
        for body, want in (("    ```\n    x\nCloses #40\n", [40]),
                           ("\t```\n\tx\nCloses #41\n", [41]),
                           ("      ~~~\n      x\nFixes #42\n", [42])):
            with self.subTest(body=body[:24]):
                self.assertEqual(body_closed_issue_numbers(body), want)

    def test_a_legally_indented_opener_still_opens_a_fence(self):
        # Control against over-shooting: up to three spaces is still a fence,
        # so tightening the opener must not re-expose what a well-formed (or
        # unclosed) indented block quotes.
        for body in ("   ```\n   Closes #43\n   ```\n",
                     "   ```\nCloses #44\n",
                     "  ~~~\nCloses #45\n  ~~~\n"):
            with self.subTest(body=body[:24]):
                self.assertEqual(body_closed_issue_numbers(body), [])

    def test_an_indented_code_block_is_not_a_close(self):
        # The block form the opener no longer swallows still has to be code:
        # a run indented four columns after a blank line renders inside
        # <pre><code>, exactly like a fenced block, so GitHub links nothing in
        # it. Without this the tightened opener would trade a dropped link for
        # an invented one — the costlier direction.
        for body in ("Example of what NOT to write:\n\n    Closes #46\n",
                     "    Closes #47\n",
                     "Text.\n\n\tFixes #48\n",
                     "    ```\n    x\n    Closes #49\n"):
            with self.subTest(body=body[:28]):
                self.assertEqual(body_closed_issue_numbers(body), [])

    def test_an_indented_continuation_line_still_closes(self):
        # Four columns is only code OUTSIDE a list: under a bullet it is the
        # item's own continuation text, and GitHub links it. This is measured,
        # not deduced — the first body is the shape of a merged engine PR whose
        # closingIssuesReferences lists the issue its six-space continuation
        # line closes. Stripping on indentation alone drops that link.
        for body, want in (
                ("- [x] Citations resolved: refs above\n"
                 "      MERGED (closes #50), and the rest\n", [50]),
                ("- item\n\n    closes #51\n", [51]),
                ("- item\nlazily continued\n\n    closes #52\n", [52]),
                ("1. item\n\n    closes #53\n", [53]),
                ("A wrapped paragraph line\n    closes #54\n", [54])):
            with self.subTest(body=body[:28]):
                self.assertEqual(body_closed_issue_numbers(body), want)

    def test_indentation_short_of_a_code_block_still_closes(self):
        # The threshold is the fourth column, and it is the same one the fence
        # arms use. Three spaces is ordinary prose indentation — a matcher that
        # strips at three drops the link while GitHub keeps it.
        for body, want in (("Text:\n\n   closes #55\n", [55]),
                           ("   closes #56\n", [56]),
                           ("Text:\n\n  \tcloses #57\n", [])):
            with self.subTest(body=body[:24]):
                self.assertEqual(body_closed_issue_numbers(body), want)

    def test_an_unbalanced_backtick_cannot_blank_a_later_paragraph(self):
        # A span is bounded to one paragraph, so a stray backtick pairs with the
        # next stray one only within its own. Unbounded, these two would pair
        # across the blank lines and swallow the live reference between them —
        # dropping a real close link, the costlier direction.
        body = "A stray ` backtick.\n\nCloses #255\n\nAnd another ` here.\n"
        self.assertEqual(body_closed_issue_numbers(body), [255])

    def test_both_forms_agree_on_every_arm(self):
        # The singular and all-refs forms share the keyword AND the stripping;
        # this is the no-drift assertion that centralization exists for.
        for body in (self.NEGATED, "Closes #2091", "```\nCloses #2091\n```",
                     "```\nCloses #2091\n", "~~~\nCloses #2091\n",
                     "```\nx\n```~~~\nCloses #2091\n",
                     "~~~\nx\n~~~```\nCloses #2091\n",
                     "```\nx\n    ```\nCloses #2091\n",
                     "```\nx\n\t```\nCloses #2091\n",
                     "```\nx\n   ```\nCloses #2091\n",
                     "    ```\n    x\nCloses #2091\n",
                     "Prose:\n\n    Closes #2091\n",
                     "- item\n\n    Closes #2091\n",
                     "Not `Closes #2091` but Closes #2091 really"):
            with self.subTest(body=body[:40]):
                nums = body_closed_issue_numbers(body)
                self.assertEqual(body_closes_issue(body, 2091), 2091 in nums)


if __name__ == "__main__":
    unittest.main()
