"""Tests for enrich_inflight_pr_tasks() in fleet-state-scout.

A queued task whose own issue already has an open implementation PR is
non-actionable off the queue — a parked design-blocked PR releases its
issue-side claim, so the task drops back into tasks.open looking free while a
fresh worker would refuse it. The enrichment tags it `inflight_pr` so the
dispatch resolver skips it (#1726, the #1640 / PR #1700 incident).

"Its own PR" is a union of two links, each sufficient alone: the
`claude/<N>-…` branch convention and a `Closes #N` in the PR body (#2672).
TestBodyClosesLink covers the body arm; the classes above it cover the branch
arm and must keep passing unmodified, which is what proves the widening
preserved the original behaviour.

Import the function via importlib because the script has no .py extension.
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
enrich_inflight_pr_tasks = _mod.enrich_inflight_pr_tasks

# The scout derives pr["closes_issues"] at fetch time with this exact call
# (fleet-state-scout:369). Fixtures below go through it rather than hardcoding
# an int list, so the arms actually exercise the closing-keyword grammar —
# `Fixes`/`Resolves`/lowercase spellings and the int element type included. A
# hardcoded [2578] would pass even if the parser only ever recognised "Closes".
body_closed_issue_numbers = _mod.body_closed_issue_numbers


def _state(engine_tasks=None, engine_prs=None, game_tasks=None, game_prs=None):
    return {
        "repos": {
            "engine": {
                "tasks": {"open": engine_tasks or []},
                "prs": engine_prs or [],
            },
            "game": {
                "tasks": {"open": game_tasks or []},
                "prs": game_prs or [],
            },
        }
    }


def _task(id_):
    # The enrichment keys on the task's OWN issue number; mirror the scout's
    # fetch_task_queue shape where `issue` == `id` == "#N".
    return {"id": id_, "issue": id_}


def _pr(number, head_ref, labels=None):
    # No `closes_issues` key: the pre-#2672 record shape, which also stands in
    # for a record the 304 fast path reused from an older schema. The
    # enrichment must treat its absence as "no body link", not crash.
    return {"number": number, "headRefName": head_ref, "labels": labels or []}


def _pr_body(number, head_ref, body, labels=None):
    """A PR record with `closes_issues` derived from `body` as the scout does."""
    pr = _pr(number, head_ref, labels)
    pr["closes_issues"] = sorted(set(body_closed_issue_numbers(body)))
    return pr


class TestEnrichInflightPrTasks(unittest.TestCase):

    def test_parked_design_blocked_pr_tags_inflight(self):
        # The #1640 case: open task whose own PR is fleet:wip + design-blocked.
        tasks = [_task("#1640")]
        prs = [_pr(1700, "claude/1640-metal-foreign-canvas-r32i-read",
                   labels=["fleet:wip", "fleet:design-blocked", "fleet:opus"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertEqual(out["inflight_pr"]["number"], 1700)
        self.assertEqual(out["inflight_pr"]["headRefName"],
                         "claude/1640-metal-foreign-canvas-r32i-read")
        self.assertTrue(out["inflight_pr"]["parked"])

    def test_active_wip_pr_tags_inflight_not_parked(self):
        # A plain fleet:wip PR (orphaned / mid-flight) is still in flight: a
        # fresh queue claim is the wrong action, so it's tagged — parked False.
        tasks = [_task("#1640")]
        prs = [_pr(1700, "claude/1640-metal-foreign-canvas-r32i-read",
                   labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertFalse(out["inflight_pr"]["parked"])

    def test_no_matching_pr_no_field(self):
        tasks = [_task("#1640")]
        prs = [_pr(1700, "claude/9999-unrelated", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_empty_prs_no_field(self):
        tasks = [_task("#1640")]
        state = _state(engine_tasks=tasks, engine_prs=[])
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination(self):
        # #164's task must not match a claude/1640- branch (trailing dash).
        tasks = [_task("#164")]
        prs = [_pr(1700, "claude/1640-metal", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_cross_repo_isolation(self):
        # An engine task must not match a same-numbered game PR.
        tasks = [_task("#101")]
        game_prs = [_pr(5, "claude/101-in-game-repo", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=[], game_prs=game_prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_game_legacy_branch_prefix_matches(self):
        # Game accepts both claude/<N>- and the legacy claude/game-<N>- form.
        tasks = [_task("#101")]
        game_prs = [_pr(5, "claude/game-101-unit-movement", labels=["fleet:wip"])]
        state = _state(game_tasks=tasks, game_prs=game_prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["game"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertEqual(out["inflight_pr"]["number"], 5)

    def test_multiple_tasks_independent(self):
        tasks = [_task("#1640"), _task("#1726")]
        prs = [_pr(1700, "claude/1640-metal", labels=["fleet:design-blocked"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        open_tasks = state["repos"]["engine"]["tasks"]["open"]
        self.assertIn("inflight_pr", open_tasks[0])
        # #1726 has no matching PR -> stays claimable.
        self.assertNotIn("inflight_pr", open_tasks[1])


class TestBodyClosesLink(unittest.TestCase):
    """An open PR's body `Closes #N` is its own sufficient link to issue N.

    Sufficient means the head branch need not also match the `claude/<N>-`
    convention: a PR whose branch names some other issue, or none, still puts
    its Closes-target in flight. See #2672.
    """

    def test_body_closes_without_branch_match_tags_inflight(self):
        tasks = [_task("#2578")]
        # Body-only link; the branch names a different issue entirely.
        prs = [_pr_body(2579, "claude/2419-fleet-branch-match-centralize",
                        "Refactor.\n\nCloses #2578\n\nAlso Closes #2578.",
                        labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertEqual(out["inflight_pr"]["number"], 2579)
        self.assertEqual(out["inflight_pr"]["headRefName"],
                         "claude/2419-fleet-branch-match-centralize")
        self.assertFalse(out["inflight_pr"]["parked"])

    def test_fixes_and_resolves_spellings_also_link(self):
        # The issue names three keywords; assert the two the primary arm above
        # does not reach, or "Closes-only" would pass every test in this class.
        for keyword in ("Fixes", "Resolves", "resolved"):
            with self.subTest(keyword=keyword):
                tasks = [_task("#2578")]
                prs = [_pr_body(2579, "claude/hand-named-branch",
                                f"{keyword} #2578", labels=["fleet:wip"])]
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_inflight_pr_tasks(state)
                self.assertIn(
                    "inflight_pr",
                    state["repos"]["engine"]["tasks"]["open"][0])

    def test_body_closes_other_issue_no_field(self):
        # No false positive: the PR closes a neighbouring issue, not this task.
        tasks = [_task("#2578")]
        prs = [_pr_body(2579, "claude/hand-named-branch", "Closes #2579",
                        labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_non_closing_reference_no_field(self):
        # `Part of #N` / a bare mention is not a claim to implement the issue.
        tasks = [_task("#2578")]
        prs = [_pr_body(2579, "claude/hand-named-branch",
                        "Part of #2578. See also #2578.", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_body_link_is_matched_as_int_not_string(self):
        # closes_issues holds ints and issue_num is a digit string, so dropping
        # the int() cast makes `"2578" in [2578]` silently False. This arm is
        # the cast's control: it fails, and only it fails, if the cast goes.
        prs = [_pr_body(2579, "claude/hand-named-branch", "Closes #2578")]
        self.assertEqual(prs[0]["closes_issues"], [2578])
        self.assertIsInstance(prs[0]["closes_issues"][0], int)
        state = _state(engine_tasks=[_task("#2578")], engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertIn("inflight_pr",
                      state["repos"]["engine"]["tasks"]["open"][0])

    def test_branch_arm_survives_a_body_naming_another_issue(self):
        # The union is `or`, not `and`: a branch-matched PR whose body closes
        # only some OTHER issue still tags this task. Turning the new clause
        # into an `and` passes every body-arm test above and fails here.
        tasks = [_task("#1640")]
        prs = [_pr_body(1700, "claude/1640-metal-foreign-canvas-r32i-read",
                        "Closes #9999", labels=["fleet:design-blocked"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertTrue(out["inflight_pr"]["parked"])

    def test_one_pr_closing_two_issues_tags_both_tasks(self):
        tasks = [_task("#1640"), _task("#1641")]
        prs = [_pr_body(1700, "claude/1640-metal",
                        "Closes #1640\nCloses #1641", labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        open_tasks = state["repos"]["engine"]["tasks"]["open"]
        # #1640 links by branch AND body; #1641 by body alone.
        self.assertEqual(open_tasks[0]["inflight_pr"]["number"], 1700)
        self.assertEqual(open_tasks[1]["inflight_pr"]["number"], 1700)

    def test_body_link_matches_a_later_pr_not_just_the_first(self):
        # next() scans the whole list: the predicate has to be evaluated per
        # PR, not short-circuited on prs[0]. Ordered as the scout emits them
        # (sorted by number ascending), with the non-matching PR first.
        tasks = [_task("#2578")]
        prs = [_pr(2400, "claude/2400-unrelated", labels=["fleet:wip"]),
               _pr_body(2579, "claude/hand-named-branch", "Closes #2578",
                        labels=["fleet:design-blocked"])]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertEqual(out["inflight_pr"]["number"], 2579)
        self.assertTrue(out["inflight_pr"]["parked"])

    def test_body_link_does_not_cross_repos(self):
        # `closes_issues` carries no repo namespace (unlike branch_matches_issue,
        # which takes repo_key), so per-repo isolation rests entirely on the
        # outer loop reading each repo's own prs[]. Flattening that loop would
        # pass every arm above and fail only here.
        tasks = [_task("#101")]
        game_prs = [_pr_body(5, "claude/game-hand-named", "Closes #101",
                             labels=["fleet:wip"])]
        state = _state(engine_tasks=tasks, engine_prs=[], game_prs=game_prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_missing_closes_issues_key_is_not_a_match(self):
        # A 304-reused record predating the field must degrade to branch-only,
        # not raise — the `or []` guard's control.
        tasks = [_task("#2578")]
        prs = [_pr(2579, "claude/hand-named-branch", labels=["fleet:wip"])]
        self.assertNotIn("closes_issues", prs[0])
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_null_closes_issues_is_not_a_match(self):
        tasks = [_task("#2578")]
        prs = [_pr(2579, "claude/hand-named-branch", labels=["fleet:wip"])]
        prs[0]["closes_issues"] = None
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertNotIn("inflight_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_closes_inside_an_unclosed_fence_leaves_the_task_claimable(self):
        # An opening fence with no closing fence is code through end-of-body in
        # CommonMark, so GitHub links nothing after it. Read as prose, the
        # quoted ref tags a task NOBODY is implementing — the scout drops it off
        # the queue and fleet-claim's duplicate-open-PR guard refuses the claim,
        # stranding it for as long as the quoting PR stays open.
        for fence in ("```", "~~~"):
            with self.subTest(fence=fence):
                tasks = [_task("#2578")]
                prs = [_pr_body(2579, "claude/hand-named-branch",
                                f"Do not write:\n\n{fence}\nCloses #2578\n",
                                labels=["fleet:wip"])]
                self.assertEqual(prs[0]["closes_issues"], [])
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_inflight_pr_tasks(state)
                self.assertNotIn(
                    "inflight_pr",
                    state["repos"]["engine"]["tasks"]["open"][0])

    def test_a_falsely_closed_fence_leaves_the_task_claimable(self):
        # Same consequence as the unclosed-fence arm above, reached the other
        # way: a run CommonMark does NOT accept as a closing fence (mixed
        # characters, or indented past three spaces) left the block open in
        # GitHub while this parser ends it, so the code after a false closer
        # reads as prose and tags a task nobody is implementing.
        for label, closer in (("mixed backtick/tilde", "```~~~"),
                              ("mixed tilde/backtick", "~~~```"),
                              ("indented four spaces", "    ```"),
                              ("indented with a tab", "\t```")):
            with self.subTest(closer=label):
                opener = "~~~" if closer.startswith("~") else "```"
                tasks = [_task("#2578")]
                prs = [_pr_body(2579, "claude/hand-named-branch",
                                f"{opener}\nexample\n{closer}\nCloses #2578\n",
                                labels=["fleet:wip"])]
                self.assertEqual(prs[0]["closes_issues"], [])
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_inflight_pr_tasks(state)
                self.assertNotIn(
                    "inflight_pr",
                    state["repos"]["engine"]["tasks"]["open"][0])

    def test_a_legally_closed_fence_still_tags_a_link_after_it(self):
        # Control against over-shooting the arm above: a real closing fence —
        # including one legally indented up to three spaces — must still end
        # its block, or tightening the closer silently drops live links and
        # every assertion above passes anyway.
        for closer in ("```", "   ```"):
            with self.subTest(closer=repr(closer)):
                tasks = [_task("#2578")]
                prs = [_pr_body(2579, "claude/hand-named-branch",
                                f"```\nCloses #9999\n{closer}\nCloses #2578\n",
                                labels=["fleet:wip"])]
                self.assertEqual(prs[0]["closes_issues"], [2578])
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_inflight_pr_tasks(state)
                self.assertEqual(
                    state["repos"]["engine"]["tasks"]["open"][0]
                    ["inflight_pr"]["number"], 2579)

    def test_a_real_link_above_an_unclosed_fence_still_tags_inflight(self):
        # Control for the arm above: the fix must strip what FOLLOWS the
        # unclosed fence, not silence every body that contains one.
        tasks = [_task("#2578")]
        prs = [_pr_body(2579, "claude/hand-named-branch",
                        "Closes #2578\n\n```\nCloses #9999\n",
                        labels=["fleet:wip"])]
        self.assertEqual(prs[0]["closes_issues"], [2578])
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_inflight_pr_tasks(state)
        self.assertEqual(
            state["repos"]["engine"]["tasks"]["open"][0]["inflight_pr"]["number"],
            2579)

    def test_game_task_body_link_tags_inflight(self):
        # The body arm is repo-agnostic by construction; assert it reaches the
        # game repo too rather than only the engine branch of the loop.
        tasks = [_task("#101")]
        game_prs = [_pr_body(5, "claude/hand-named-game-branch", "Closes #101",
                             labels=["fleet:wip"])]
        state = _state(game_tasks=tasks, game_prs=game_prs)
        enrich_inflight_pr_tasks(state)
        out = state["repos"]["game"]["tasks"]["open"][0]
        self.assertIn("inflight_pr", out)
        self.assertEqual(out["inflight_pr"]["number"], 5)


if __name__ == "__main__":
    unittest.main()
