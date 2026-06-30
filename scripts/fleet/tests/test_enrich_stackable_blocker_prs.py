"""Tests for enrich_stackable_blocker_prs() in fleet-state-scout.

Covers prefix-discrimination, multi-match safety, multi-blocker guard,
single-ref-with-prose pass-through, None/empty/free-text blocked_by guards,
cross-repo isolation, and author pass-through. Import the function via
importlib because the script has no .py extension.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

# ---------------------------------------------------------------------------
# Import the function under test
# ---------------------------------------------------------------------------
_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"

_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
enrich_stackable_blocker_prs = _mod.enrich_stackable_blocker_prs


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

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


def _task(id_, blocked_by):
    return {"id": id_, "blocked_by": blocked_by}


def _pr(number, head_ref, author="bot", labels=None, body=""):
    pr = {"number": number, "headRefName": head_ref, "author": author,
          "body": body}
    if labels is not None:
        pr["labels"] = labels
    return pr


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestEnrichStackableBlockerPrs(unittest.TestCase):

    def setUp(self):
        # Point PRS_DIR at an empty temp dir so `pr_cache_path.exists()` (the
        # empty-claim / known-files check, #1751) is deterministic and never
        # leaks the live ~/.fleet cache. Tests that exercise the diff path write
        # a fixture via _write_pr_cache; the rest leave it absent (files
        # "unknown" → not treated as empty).
        self._tmp = tempfile.TemporaryDirectory()
        self._orig_prs_dir = _mod.PRS_DIR
        _mod.PRS_DIR = Path(self._tmp.name)

    def tearDown(self):
        _mod.PRS_DIR = self._orig_prs_dir
        self._tmp.cleanup()

    def _write_pr_cache(self, repo, number, files):
        """Write a cached PR detail JSON (the shape `_blocker_pr_files` reads)
        so a test can give a blocker PR a known diff (empty list = empty
        claim-commit)."""
        repo_dir = Path(self._tmp.name) / repo
        repo_dir.mkdir(parents=True, exist_ok=True)
        (repo_dir / f"{number}.json").write_text(
            json.dumps({"files": [{"path": p} for p in files]})
        )

    def test_happy_path_single_match(self):
        """Exactly one PR matches the blocked_by prefix → field is written."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(536, "claude/1111-scout-stackable-blocker-pr", author="jakildev")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 536)
        self.assertEqual(task["stackable_blocker_pr"]["headRefName"],
                         "claude/1111-scout-stackable-blocker-pr")
        self.assertEqual(task["stackable_blocker_pr"]["author"], "jakildev")

    def test_queued_blocked_task_is_enriched(self):
        """#1527: a queued-blocked task (carries the fleet:blocked-derived
        `blocked: True` flag) now lives in tasks.open, so enrichment attaches
        the blocker's open PR — the autonomous "feed stacking" half. The
        `blocked` flag must not interfere with enrichment."""
        task = {"id": "#1112", "blocked_by": "#1111", "blocked": True}
        prs = [_pr(536, "claude/1111-scout-stackable-blocker-pr", author="jakildev")]
        state = _state(engine_tasks=[task], engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", out)
        self.assertEqual(out["stackable_blocker_pr"]["number"], 536)

    def test_zero_matches_no_field(self):
        """No open PR matches prefix → field absent."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(999, "claude/2000-unrelated")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_empty_prs_no_field(self):
        """No open PRs at all → field absent."""
        tasks = [_task("#1112", "#1111")]
        state = _state(engine_tasks=tasks, engine_prs=[])
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multi_match_no_field(self):
        """Two PRs share the same issue-number prefix (stale branch) → field absent."""
        tasks = [_task("#1112", "#1111")]
        prs = [
            _pr(536, "claude/1111-scout-stackable-blocker-pr"),
            _pr(537, "claude/1111-stale-bounced-branch"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multi_blocker_no_field(self):
        """blocked_by naming two issues is a multi-blocker → field absent."""
        tasks = [_task("#1115", "#1112, #1113")]
        prs = [_pr(540, "claude/1112-some-branch"), _pr(541, "claude/1113-other")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_two_refs_with_prose_no_field(self):
        """Two #refs is a multi-blocker regardless of surrounding prose
        ("#A and #B", "#A, #B (note)") → field absent. Guards the single-ref
        relaxation from over-matching genuine multi-blockers."""
        for value in ("#101 and #102", "#101, #102 (both must land)"):
            with self.subTest(blocked_by=value):
                tasks = [_task("#999", value)]
                prs = [_pr(1, "claude/101-x"), _pr(2, "claude/102-y")]
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_stackable_blocker_prs(state)
                self.assertNotIn(
                    "stackable_blocker_pr",
                    state["repos"]["engine"]["tasks"]["open"][0],
                )

    def test_none_blocked_by_no_field(self):
        """blocked_by is None → field absent."""
        tasks = [_task("#1112", None)]
        prs = [_pr(536, "claude/1111-x")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_explicit_none_string_no_field(self):
        """blocked_by is '(none)' → field absent."""
        tasks = [_task("#1112", "(none)")]
        prs = [_pr(536, "claude/1111-x")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination_no_match(self):
        """#101 prefix must not match a 1011 branch (trailing dash prevents it)."""
        tasks = [_task("#102", "#101")]
        prs = [_pr(100, "claude/1011-longer-id")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_prefix_discrimination_correct_match(self):
        """#101 with trailing dash matches 101-something but not 1011-something."""
        tasks = [_task("#102", "#101")]
        prs = [
            _pr(100, "claude/1011-longer-id"),
            _pr(101, "claude/101-real-branch"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 101)

    def test_cross_repo_isolation(self):
        """Engine task cannot match a game PR."""
        engine_tasks = [_task("#1112", "#1111")]
        game_prs = [_pr(536, "claude/1111-in-game-repo")]
        state = _state(engine_tasks=engine_tasks, engine_prs=[], game_prs=game_prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_multiple_tasks_independent(self):
        """Two tasks in the same repo with different blockers are enriched independently."""
        tasks = [
            _task("#1113", "#1111"),
            _task("#1114", "#1112"),
        ]
        prs = [
            _pr(536, "claude/1111-branch-a"),
            _pr(537, "claude/1112-branch-b", author="other"),
        ]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        open_tasks = state["repos"]["engine"]["tasks"]["open"]
        self.assertEqual(open_tasks[0]["stackable_blocker_pr"]["number"], 536)
        self.assertEqual(open_tasks[1]["stackable_blocker_pr"]["number"], 537)
        self.assertEqual(open_tasks[1]["stackable_blocker_pr"]["author"], "other")

    def test_free_text_blocked_by_no_field(self):
        """blocked_by values that reference zero issues (free text, a bare
        URL with no #ref, legacy T-NNN) → field absent."""
        values = [
            "pending design",
            "https://github.com/x/y/issues/1",
            "T-101",
        ]
        for value in values:
            with self.subTest(blocked_by=value):
                tasks = [_task("#999", value)]
                prs = [_pr(1, "claude/101-x")]
                state = _state(engine_tasks=tasks, engine_prs=prs)
                enrich_stackable_blocker_prs(state)
                self.assertNotIn(
                    "stackable_blocker_pr",
                    state["repos"]["engine"]["tasks"]["open"][0],
                )

    def test_single_ref_with_prose_enriched(self):
        """A single #ref followed by explanatory prose still stacks. The epic
        decomposer emits "#NNN (why)"; the old terse-only gate silently
        dropped the whole chain (#1309 blocked_by "#1308 (T1 must land
        first…)" never stacked on #1316, observed 2026-05-28)."""
        tasks = [_task("#1309", "#1308 (T1 must land first; stack on its PR)")]
        prs = [_pr(1316, "claude/1308-per-axis-trixel-canvas-infra",
                   author="jakildev")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 1316)
        self.assertEqual(task["stackable_blocker_pr"]["headRefName"],
                         "claude/1308-per-axis-trixel-canvas-infra")

    # ---- #1751: non-stackable base-state rejection (offer side) -----------

    def _assert_no_offer(self, labels=None, cached_files="unset"):
        """A single blocker PR with the given labels / cached diff yields NO
        stackable_blocker_pr field."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(536, "claude/1111-x", labels=labels)]
        if cached_files != "unset":
            self._write_pr_cache("engine", 536, cached_files)
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_wip_base_no_field(self):
        """fleet:wip base (head diff in flux) is not offered."""
        self._assert_no_offer(labels=["fleet:wip"])

    def test_human_wip_base_no_field(self):
        self._assert_no_offer(labels=["human:wip"])

    def test_design_unblocked_base_no_field(self):
        """A just-design-unblocked skeleton is not offered (#1751 / the
        2026-06-06 empty-skeleton hazard)."""
        self._assert_no_offer(labels=["fleet:design-unblocked"])

    def test_design_blocked_base_no_field(self):
        """Regression: the old filter (b) design-block rejection still holds
        through the unified predicate."""
        self._assert_no_offer(labels=["fleet:design-blocked"])

    def test_amending_base_no_field(self):
        """fleet:amending-<host>-<agent> (dynamic prefix) base is not offered."""
        self._assert_no_offer(labels=["fleet:amending-mac-worker-2"])

    def test_empty_claim_base_no_field(self):
        """An OPEN base whose cached diff is empty (claim-commit-only skeleton)
        is not offered — the 2026-06-06 empty-claim replay."""
        self._assert_no_offer(labels=None, cached_files=[])

    def test_clean_nonempty_open_base_enriched(self):
        """A clean OPEN base with a real (non-empty) diff and no reject labels
        is still offered."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(536, "claude/1111-x")]
        self._write_pr_cache("engine", 536, ["engine/render/x.cpp"])
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 536)

    def test_closes_n_body_match_offered(self):
        """A blocker PR on a non-standard branch (no claude/<N>-*) that Closes
        the blocker issue in its body is matched and offered — the scout offer
        now mirrors find-stackable-blockers' branch-OR-Closes union."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(540, "feature/manual-fix", body="Implements the thing.\nCloses #1111")]
        self._write_pr_cache("engine", 540, ["engine/render/x.cpp"])
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        task = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("stackable_blocker_pr", task)
        self.assertEqual(task["stackable_blocker_pr"]["number"], 540)

    def test_closes_n_unrelated_issue_not_matched(self):
        """A PR that Closes a DIFFERENT issue (#999) is not matched for #1111 —
        the closes-regex is anchored to the exact blocker number."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(541, "feature/other", body="Closes #999")]
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertNotIn("stackable_blocker_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_cache_miss_unknown_files_still_enriched(self):
        """No cached PR detail (files unknown) + clean labels → still offered;
        'unknown' is not treated as empty, so a not-yet-cached base isn't
        suppressed. The claim gate re-checks the diff live."""
        tasks = [_task("#1112", "#1111")]
        prs = [_pr(536, "claude/1111-x")]  # no cache fixture written
        state = _state(engine_tasks=tasks, engine_prs=prs)
        enrich_stackable_blocker_prs(state)
        self.assertIn("stackable_blocker_pr",
                      state["repos"]["engine"]["tasks"]["open"][0])


if __name__ == "__main__":
    unittest.main()
