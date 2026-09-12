"""Tests for enrich_shadow_merged_pr_tasks() in fleet-state-scout.

A PR merged to master under a `Ref` reference instead of a closing keyword does
not close its issue, so the row stays fleet:queued and owner-free while part of
its work is already on master — and no in-flight leg can see it (fetch_prs is
open-only; the worker's merged leg needs a non-master base plus a closing
keyword). The enrichment tags the row with the shadowing merged PR so the
worker reads it before branching.

The field is ADVISORY, and the arms below pin both halves: it is stamped, and
it does NOT make the task unclaimable. A `Ref` reference marks PARTIAL work, so
the residual is real queued work, and unlike an open PR a merged master commit
never clears — a refusal keyed on it would be unclearable by anything the fleet
can do.

Hermetic: the state dict is built by hand, so no GitHub and no ~/.fleet. Import
the function via importlib because the script has no .py extension.
"""
import importlib.machinery
import importlib.util
import sys
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"

_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


def _subject(name):
    """Bind a scout function without aborting the module when it is absent.

    A missing subject has to make the ARMS red, not the import: an import-time
    AttributeError prints no tally at all, and fleet-positive-control reports a
    tally-less run as a setup failure rather than a verdict — so a suite that
    binds its subject eagerly can never be controlled against a ref that
    predates it.
    """
    fn = getattr(_mod, name, None)
    if fn is not None:
        return fn

    def _missing(*_args, **_kwargs):
        raise AssertionError(f"fleet-state-scout defines no {name}()")

    return _missing


enrich_shadow_merged_pr_tasks = _subject("enrich_shadow_merged_pr_tasks")
enrich_inflight_pr_tasks = _subject("enrich_inflight_pr_tasks")

sys.path.insert(0, str(Path(__file__).parent.parent))
import fleet_task_class  # noqa: E402


def _state(engine_tasks=None, engine_merged=None,
           game_tasks=None, game_merged=None, engine_prs=None):
    return {
        "repos": {
            "engine": {
                "tasks": {"open": engine_tasks or []},
                "prs": engine_prs or [],
                "recent_merged_prs": engine_merged or [],
            },
            "game": {
                "tasks": {"open": game_tasks or []},
                "prs": [],
                "recent_merged_prs": game_merged or [],
            },
        }
    }


def _task(id_):
    # The enrichment keys on the task's OWN issue number; mirror the scout's
    # fetch_task_queue shape where `issue` == `id` == "#N".
    return {"id": id_, "issue": id_, "owner": "free", "blocked_by": "(none)"}


def _merged(number, head_ref, merged_at="2026-08-22T22:23:03Z",
            base="master", title=""):
    return {
        "number": number,
        "title": title or f"PR {number}",
        "headRefName": head_ref,
        "baseRefName": base,
        "mergedAt": merged_at,
    }


# The reference fixture, transcribed from the incident this enricher exists
# for: a PR merged to master off a `claude/<issue>-…` branch under a `Ref`
# reference, leaving the issue queued and owner-free.
_PR_2475 = _merged(2475, "claude/2298-occlusion-cull-feeder-domain",
                   merged_at="2026-08-22T22:23:03Z",
                   title="engine/render: occlusion cull feeder domain")


class ShadowMergedPrFires(unittest.TestCase):
    """The shadowed row is tagged, with the whole record the reader needs."""

    def test_shadowed_row_is_tagged(self):
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[_PR_2475])
        enrich_shadow_merged_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("shadow_merged_pr", out)
        self.assertEqual(out["shadow_merged_pr"]["number"], 2475)
        self.assertEqual(out["shadow_merged_pr"]["headRefName"],
                         "claude/2298-occlusion-cull-feeder-domain")
        self.assertEqual(out["shadow_merged_pr"]["mergedAt"],
                         "2026-08-22T22:23:03Z")
        self.assertEqual(out["shadow_merged_pr"]["title"],
                         "engine/render: occlusion cull feeder domain")

    def test_base_ref_is_carried_and_not_filtered_on(self):
        # A merged non-master-base PR whose head names the issue is equally
        # worth reading before branching, so base is carried, never filtered on;
        # the reader needs it to tell a master merge from a stack landing.
        pr = _merged(2475, "claude/2298-occlusion-cull-feeder-domain",
                     base="claude/2200-base")
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[pr])
        enrich_shadow_merged_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertIn("shadow_merged_pr", out)
        self.assertEqual(out["shadow_merged_pr"]["baseRefName"],
                         "claude/2200-base")

    def test_newest_match_in_the_window_wins(self):
        # Several merged PRs in the window may name one issue (a multi-PR
        # issue shipping in slices). The latest is the most current statement
        # of what has already landed, so it is the one the worker must read.
        older = _merged(2475, "claude/2298-occlusion-cull-feeder-domain",
                        merged_at="2026-08-22T22:23:03Z")
        newer = _merged(3041, "claude/2298-widen-cull-domain",
                        merged_at="2026-08-23T06:23:11Z")
        state = _state(engine_tasks=[_task("#2298")],
                       engine_merged=[older, newer])
        enrich_shadow_merged_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertEqual(out["shadow_merged_pr"]["number"], 3041)

    def test_multiple_tasks_independent(self):
        state = _state(engine_tasks=[_task("#2298"), _task("#9001")],
                       engine_merged=[_PR_2475])
        enrich_shadow_merged_pr_tasks(state)
        rows = state["repos"]["engine"]["tasks"]["open"]
        self.assertIn("shadow_merged_pr", rows[0])
        self.assertNotIn("shadow_merged_pr", rows[1])

    def test_game_rows_are_enriched_too(self):
        pr = _merged(77, "claude/game-101-unit-movement")
        state = _state(game_tasks=[_task("#101")], game_merged=[pr])
        enrich_shadow_merged_pr_tasks(state)
        out = state["repos"]["game"]["tasks"]["open"][0]
        self.assertIn("shadow_merged_pr", out)
        self.assertEqual(out["shadow_merged_pr"]["number"], 77)

    def test_game_prefixless_branch_form_matches(self):
        # branch_matches_issue accepts both game spellings; pass repo_key so the
        # legacy `claude/game-<N>-` arm is reachable at all.
        pr = _merged(78, "claude/101-unit-movement")
        state = _state(game_tasks=[_task("#101")], game_merged=[pr])
        enrich_shadow_merged_pr_tasks(state)
        self.assertIn("shadow_merged_pr",
                      state["repos"]["game"]["tasks"]["open"][0])

    def test_both_repos_in_one_pass(self):
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[_PR_2475],
                       game_tasks=[_task("#101")],
                       game_merged=[_merged(77, "claude/game-101-unit-movement")])
        enrich_shadow_merged_pr_tasks(state)
        self.assertIn("shadow_merged_pr",
                      state["repos"]["engine"]["tasks"]["open"][0])
        self.assertIn("shadow_merged_pr",
                      state["repos"]["game"]["tasks"]["open"][0])


class ShadowMergedPrStaysSilent(unittest.TestCase):
    """The negative controls: the enricher stays silent."""

    def test_word_boundary_longer_number_does_not_match(self):
        # A branch whose issue number merely CONTAINS the task's must not match.
        # branch_matches_issue's trailing dash gives this, but it is the arm a
        # hand-rolled number grep gets wrong, so it is pinned here.
        pr = _merged(9001, "claude/12298-unrelated")
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[pr])
        enrich_shadow_merged_pr_tasks(state)
        self.assertNotIn("shadow_merged_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_word_boundary_task_number_is_a_prefix_of_the_branch(self):
        # The direction the trailing dash exists for: task 229 must not be
        # shadowed by a `claude/2298-…` merge. Dropping the dash from
        # issue_branch_prefixes leaves every other arm here green.
        pr = _merged(9002, "claude/2298-occlusion-cull-feeder-domain")
        state = _state(engine_tasks=[_task("#229")], engine_merged=[pr])
        enrich_shadow_merged_pr_tasks(state)
        self.assertNotIn("shadow_merged_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_empty_window_leaves_every_row_clean(self):
        state = _state(engine_tasks=[_task("#2298"), _task("#9001")],
                       engine_merged=[])
        enrich_shadow_merged_pr_tasks(state)
        for row in state["repos"]["engine"]["tasks"]["open"]:
            self.assertNotIn("shadow_merged_pr", row)

    def test_missing_key_is_not_an_error(self):
        # A degraded tick can leave recent_merged_prs unset entirely.
        state = {"repos": {"engine": {"tasks": {"open": [_task("#2298")]}}}}
        enrich_shadow_merged_pr_tasks(state)
        self.assertNotIn("shadow_merged_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_unrelated_branch_does_not_match(self):
        pr = _merged(9003, "claude/9999-something-else")
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[pr])
        enrich_shadow_merged_pr_tasks(state)
        self.assertNotIn("shadow_merged_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_cross_repo_isolation(self):
        # An engine task must not be shadowed by a same-numbered game merge.
        state = _state(engine_tasks=[_task("#101")], engine_merged=[],
                       game_merged=[_merged(77, "claude/game-101-unit-movement")])
        enrich_shadow_merged_pr_tasks(state)
        self.assertNotIn("shadow_merged_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_row_without_an_issue_number_is_skipped(self):
        state = _state(engine_tasks=[{"id": "", "issue": ""}],
                       engine_merged=[_PR_2475])
        enrich_shadow_merged_pr_tasks(state)
        self.assertNotIn("shadow_merged_pr",
                         state["repos"]["engine"]["tasks"]["open"][0])


class ShadowMergedPrIsAdvisory(unittest.TestCase):
    """The field must never gate claimability.

    This is the arm that goes red if someone later "tightens" the predicate to
    read it — which would be a permanent strand, not a stricter gate, since a
    merged master commit never clears.
    """

    def _shadowed_task(self):
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[_PR_2475])
        enrich_shadow_merged_pr_tasks(state)
        return state["repos"]["engine"]["tasks"]["open"][0]

    def test_task_claimable_is_true(self):
        task = self._shadowed_task()
        self.assertIn("shadow_merged_pr", task)
        self.assertIs(fleet_task_class._task_claimable(task, "mac"), True)
        self.assertIs(fleet_task_class._task_claimable(task, "linux"), True)

    def test_task_is_not_terminally_unclaimable(self):
        task = self._shadowed_task()
        self.assertFalse(fleet_task_class._terminally_unclaimable(task, "mac"))

    def test_inflight_pr_still_refuses(self):
        # The control for the two arms above: the sibling field DOES gate, so a
        # True verdict there would mean the predicate had stopped reading
        # anything rather than that it correctly ignores shadow_merged_pr.
        task = self._shadowed_task()
        task["inflight_pr"] = {"number": 1, "headRefName": "claude/2298-x",
                               "parked": False}
        self.assertIs(fleet_task_class._task_claimable(task, "mac"), False)


class ShadowMergedPrCoexistsWithInflight(unittest.TestCase):
    """Both enrichers may fire on one row; neither disturbs the other."""

    def test_both_fields_are_set_independently(self):
        open_pr = {"number": 3400, "headRefName": "claude/2298-resume",
                   "labels": ["fleet:wip"]}
        state = _state(engine_tasks=[_task("#2298")],
                       engine_prs=[open_pr],
                       engine_merged=[_PR_2475])
        enrich_inflight_pr_tasks(state)
        enrich_shadow_merged_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertEqual(out["inflight_pr"]["number"], 3400)
        self.assertEqual(out["shadow_merged_pr"]["number"], 2475)

    def test_shadow_pass_does_not_clear_inflight(self):
        open_pr = {"number": 3400, "headRefName": "claude/2298-resume",
                   "labels": ["fleet:design-blocked"]}
        state = _state(engine_tasks=[_task("#2298")],
                       engine_prs=[open_pr], engine_merged=[])
        enrich_inflight_pr_tasks(state)
        enrich_shadow_merged_pr_tasks(state)
        out = state["repos"]["engine"]["tasks"]["open"][0]
        self.assertTrue(out["inflight_pr"]["parked"])
        self.assertNotIn("shadow_merged_pr", out)


class ShadowMergedPrLogging(unittest.TestCase):
    """The stamp is traced, once, and never for a row the open-PR enricher
    already reported."""

    def _lines(self, state):
        seen = []
        original = _mod.log
        _mod.log = seen.append
        try:
            enrich_shadow_merged_pr_tasks(state)
        finally:
            _mod.log = original
        return seen

    def test_stamp_is_logged_once(self):
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[_PR_2475])
        lines = self._lines(state)
        self.assertEqual(len(lines), 1)
        self.assertIn("#2298", lines[0])
        self.assertIn("2475", lines[0])

    def test_row_already_reported_as_inflight_is_not_double_logged(self):
        task = _task("#2298")
        task["inflight_pr"] = {"number": 3400,
                               "headRefName": "claude/2298-resume",
                               "parked": False}
        state = _state(engine_tasks=[task], engine_merged=[_PR_2475])
        lines = self._lines(state)
        self.assertEqual(lines, [])
        # ...but the field is still stamped — only the log line is suppressed.
        self.assertIn("shadow_merged_pr",
                      state["repos"]["engine"]["tasks"]["open"][0])

    def test_clean_window_logs_nothing(self):
        state = _state(engine_tasks=[_task("#2298")], engine_merged=[])
        self.assertEqual(self._lines(state), [])


if __name__ == "__main__":
    unittest.main()
