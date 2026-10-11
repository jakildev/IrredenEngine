"""resolve_needs_plan_blocked_by resolves a needs-plan issue's
`**Blocked by:** #N` against a live `_resolve_ref_satisfied` fallback when
the blocker is absent from both in-memory sources (closed_fleet_queued and
recent merged `claude/<N>-*` heads), matching its sibling resolve_blocked_by.

Without that fallback, a blocker that closed outside those two sources pins
the issue `blocked: True` forever — a hard gate in project_worker (never
wakes a planner dispatch) and in the role slice (can never be assigned as
FLEET_PLAN_ISSUE). A blocker carrying no labels is absent from a
`labels=fleet:queued` closed set at any window size, so widening that window
does not reach this lane.

The REST stub models `conditional_get(<slug>, issues/<N>)`
argument-for-argument and raises on any route it does not model, per
scripts/fleet/CLAUDE.md (mock at a seam that fails closed). Test fidelity pins
that rejection so a later stub rewrite cannot silently reopen the hole.

Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet.
"""
import copy
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

resolve_needs_plan_blocked_by = _mod.resolve_needs_plan_blocked_by
resolve_blocked_by = _mod.resolve_blocked_by
resolve_epic_children = _mod.resolve_epic_children

_BLOCKED_ISSUE = 2330
_BLOCKER = "2310"

# Synthetic game-repo numbers, deliberately far from any real issue so the
# fixture can't be mistaken for a live one; proves the resolver is
# repo-generic, not engine-only.
_GAME_BLOCKED_ISSUE = 91
_GAME_BLOCKER = "77"


class _RestStub:
    """Emulates conditional GETs of `repos/<slug>/issues/<ref>`.

    `states` maps ref -> the REST state string. A ref absent from it is a
    fixture error, not a pass. `changed` may model either a 200 or a 304; both
    carry the body that the production conditional cache returns.
    """

    def __init__(self, states, changed=True, failure=False):
        self.states = states
        self.changed = changed
        self.failure = failure
        self.calls = []

    def __call__(self, repo_slug, path, **kwargs):
        if not path.startswith("issues/") or path.count("/") != 1:
            raise AssertionError(f"unmodeled REST route: {repo_slug} {path}")
        if kwargs.get("timeout") != 10 or "cache_dir" not in kwargs:
            raise AssertionError(f"unmodeled conditional-get options: {kwargs!r}")
        ref = path.split("/", 1)[1]
        self.calls.append((repo_slug, ref, self.changed))
        if self.failure:
            return True, None
        if ref not in self.states:
            raise AssertionError(
                f"fixture gap: no state planted for #{ref} (calls={self.calls})")
        state = self.states[ref]
        if state == "MERGED":
            state = "closed"
        return self.changed, json.dumps({"state": state.lower()})


def _state(blocked_by="#" + _BLOCKER, closed=(), merged_heads=()):
    return {"repos": {"engine": {
        "closed_fleet_queued": [{"number": n} for n in closed],
        "recent_merged_prs": [{"headRefName": h} for h in merged_heads],
        "needs_plan": [{
            "number": _BLOCKED_ISSUE,
            "labels": ["human:approved", "fleet:needs-plan"],
            "body": f"**Area:** render\n**Blocked by:** {blocked_by}\n",
        }],
    }}}


def _two_repo_state():
    """Both engine and game carry a needs-plan issue whose blocker is CLOSED
    live only — neither in-memory source covers it. The game repo's lane
    must resolve exactly like engine's, not stay pinned blocked=True because
    the fixture never exercised a non-engine repo_key.
    """
    state = _state()
    state["repos"]["game"] = {
        "closed_fleet_queued": [],
        "recent_merged_prs": [],
        "needs_plan": [{
            "number": _GAME_BLOCKED_ISSUE,
            "labels": ["human:approved", "fleet:needs-plan"],
            "body": f"**Area:** gameplay\n**Blocked by:** #{_GAME_BLOCKER}\n",
        }],
    }
    return state


def _run(state, stub):
    with patch.object(_mod, "conditional_get", stub):
        resolve_needs_plan_blocked_by(state)
    return state["repos"]["engine"]["needs_plan"][0]["blocked"]


class NeedsPlanLiveFallback(unittest.TestCase):

    def test_closed_blocker_outside_inmemory_sources_unblocks(self):
        stub = _RestStub({_BLOCKER: "CLOSED"})
        self.assertFalse(_run(_state(), stub))
        self.assertEqual(stub.calls, [("jakildev/IrredenEngine", _BLOCKER, True)])

    def test_merged_blocker_unblocks(self):
        """A `Blocked by:` naming a PR number that MERGED is satisfied too."""
        self.assertFalse(_run(_state(), _RestStub({_BLOCKER: "MERGED"})))

    def test_game_repo_closed_blocker_outside_inmemory_sources_unblocks(self):
        """With an explicit fixture rather than relying on the resolver being
        provably repo-generic: the live fallback fires for the game repo
        too, resolving against its own slug (jakildev/irreden), and engine's
        resolution in the same tick is unaffected.
        """
        state = _two_repo_state()
        stub = _RestStub({_BLOCKER: "CLOSED", _GAME_BLOCKER: "CLOSED"})
        with patch.object(_mod, "conditional_get", stub):
            resolve_needs_plan_blocked_by(state)
        self.assertFalse(state["repos"]["engine"]["needs_plan"][0]["blocked"])
        self.assertFalse(state["repos"]["game"]["needs_plan"][0]["blocked"])
        self.assertEqual(sorted(stub.calls), sorted([
            ("jakildev/IrredenEngine", _BLOCKER, True),
            ("jakildev/irreden", _GAME_BLOCKER, True),
        ]))

    def test_open_blocker_still_blocks_with_or_without_merged_head(self):
        """NEGATIVE CONTROL — the fallback must not be a gate that cannot fire.

        Same code path, only the live verdict differs. A matching merged branch
        head is not itself proof that the blocker issue is closed.
        """
        cases = ([], [f"claude/{_BLOCKER}-light-volume-continuity"])
        for merged_heads in cases:
            with self.subTest(merged_heads=merged_heads):
                stub = _RestStub({_BLOCKER: "OPEN"})
                self.assertTrue(_run(_state(merged_heads=merged_heads), stub))
                self.assertEqual(len(stub.calls), 1)

    def test_rest_failure_fails_closed(self):
        """A flaky/timing-out REST read leaves the issue blocked — the conservative
        direction: an unreachable check fails closed rather than unblocking."""
        self.assertTrue(_run(_state(), _RestStub({}, failure=True)))

    def test_invalid_or_incomplete_rest_body_fails_closed(self):
        cases = (
            (False, None),
            (False, "{"),
            (False, "[]"),
            (False, '{"number": 2310}'),
        )
        for response in cases:
            with self.subTest(response=response):
                with patch.object(_mod, "conditional_get", return_value=response):
                    state = _state()
                    resolve_needs_plan_blocked_by(state)
                self.assertTrue(state["repos"]["engine"]["needs_plan"][0]["blocked"])

    def test_inmemory_hit_makes_no_live_call(self):
        """The common path must not acquire a per-tick network cost: a blocker
        already in closed_fleet_queued short-circuits before the fallback."""
        stub = _RestStub({})  # any call at all is a fixture gap -> raises
        self.assertFalse(_run(_state(closed=[int(_BLOCKER)]), stub))
        self.assertEqual(stub.calls, [])

    def test_cross_repo_ref_makes_no_live_call(self):
        """A qualified cross-repo ref is deferred to the ingest's own check;
        it must not be resolved (or blocked on) from this repo's lane."""
        stub = _RestStub({})
        self.assertFalse(_run(_state(blocked_by="Irreden#41"), stub))
        self.assertEqual(stub.calls, [])

    def test_ref_cache_is_shared_across_lanes(self):
        """One live call per novel ref per tick, not one per lane."""
        cache = {}
        stub = _RestStub({_BLOCKER: "CLOSED"})
        with patch.object(_mod, "conditional_get", stub):
            resolve_needs_plan_blocked_by(_state(), cache)
            resolve_needs_plan_blocked_by(_state(), cache)
        self.assertEqual(len(stub.calls), 1)

    def test_one_fetch_is_shared_by_pickup_planner_and_epic_passes(self):
        state = _state()
        repo = state["repos"]["engine"]
        repo["tasks"] = {
            "open": [{"id": "#2400", "blocked_by": f"#{_BLOCKER}", "area": None}],
            "in_progress": [],
        }
        repo["epics"] = [{
            "number": 2500,
            "checklist": [{"number": int(_BLOCKER), "checked": False}],
        }]
        cache = {}
        stub = _RestStub({_BLOCKER: "CLOSED"})
        with patch.object(_mod, "conditional_get", stub):
            resolve_blocked_by(state, cache)
            resolve_needs_plan_blocked_by(state, cache)
            resolve_epic_children(state, cache)
        self.assertEqual(len(stub.calls), 1)
        self.assertEqual(repo["tasks"]["open"][0]["blocked_by"], "(none)")
        self.assertFalse(repo["needs_plan"][0]["blocked"])
        self.assertTrue(repo["epics"][0]["checklist"][0]["closed"])

    def test_reference_cache_prunes_entries_not_touched_this_tick(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = {}
            stub = _RestStub({_BLOCKER: "CLOSED"})
            with patch.object(_mod, "REF_STATE_ETAG_DIR", root), \
                    patch.object(_mod, "conditional_get", stub):
                _mod._resolve_ref_satisfied(
                    "jakildev/IrredenEngine", _BLOCKER, cache)
                touched = _mod.cache_entry_path(
                    "jakildev/IrredenEngine", f"issues/{_BLOCKER}",
                    cache_dir=root)
                touched.parent.mkdir(parents=True)
                touched.write_text("{}")
                stale = root / "stale.json"
                stale.write_text("{}")
                _mod._prune_ref_state_cache(cache)
            self.assertTrue(touched.exists())
            self.assertFalse(stale.exists())

    def test_following_tick_parses_304_cached_body(self):
        stub = _RestStub({_BLOCKER: "CLOSED"}, changed=False)
        self.assertFalse(_run(_state(), stub))
        self.assertEqual(stub.calls, [("jakildev/IrredenEngine", _BLOCKER, False)])

    def test_stub_fidelity(self):
        """The stub rejects the shapes it claims to reject — otherwise the
        arms above certify a route the real endpoint would never accept."""
        stub = _RestStub({_BLOCKER: "CLOSED"})
        with self.assertRaises(AssertionError):
            stub("jakildev/IrredenEngine", f"pulls/{_BLOCKER}",
                 cache_dir=Path("cache"), timeout=10)
        with self.assertRaises(AssertionError):
            stub("jakildev/IrredenEngine", f"issues/{_BLOCKER}/comments",
                 cache_dir=Path("cache"), timeout=10)
        with self.assertRaises(AssertionError):
            stub("jakildev/IrredenEngine", f"issues/{_BLOCKER}", timeout=10)


class ReferenceTransportParity(unittest.TestCase):

    @staticmethod
    def _fixture():
        return {
            "generated_at": "2026-01-01T00:00:00Z",
            "repos": {"engine": {
                "path": "/synthetic/engine",
                "prs": [],
                "needs_plan": [{
                    "number": 302,
                    "title": "plan residual",
                    "labels": ["human:approved", "fleet:needs-plan"],
                    "body": f"**Model:** opus\n**Blocked by:** #{_BLOCKER}\n",
                }],
                "plan_review": [],
                "human_approved": [],
                "closed_fleet_queued": [],
                "recent_merged_prs": [],
                "tasks": {
                    "open": [
                        {"id": "#300", "issue": "#300", "title": "ordinary", "status": " ",
                         "model": "opus", "owner": "free", "blocked": False,
                         "blocked_by": "(none)", "labels": ["fleet:queued"],
                         "area": "fleet"},
                        {"id": "#301", "issue": "#301", "title": "residual", "status": " ",
                         "model": "opus", "owner": "free", "blocked": True,
                         "blocked_by": f"#{_BLOCKER}", "labels": ["fleet:queued"],
                         "area": "fleet"},
                    ],
                    "in_progress": [],
                    "done": [],
                    "plan_gated": [],
                },
                "epics": [{
                    "number": 303,
                    "title": "synthetic epic",
                    "labels": ["fleet:epic"],
                    "checklist": [{"number": int(_BLOCKER), "checked": False}],
                }],
                "epic_backrefs": [],
            }},
        }

    @staticmethod
    def _run_pipeline(state, resolver=None, rest=None):
        cache = {}
        resolver_patch = patch.object(
            _mod, "_resolve_ref_satisfied", resolver) if resolver else None
        rest_patch = patch.object(_mod, "conditional_get", rest) if rest else None
        if resolver_patch:
            resolver_patch.start()
        if rest_patch:
            rest_patch.start()
        try:
            resolve_blocked_by(state, cache)
            _mod.resolve_human_approved_blockers(state)
            resolve_needs_plan_blocked_by(state, cache)
            resolve_epic_children(state, cache)
            _mod.enrich_inflight_pr_tasks(state)
            _mod.enrich_stackable_blocker_prs(state)
            _mod.enrich_shadow_merged_pr_tasks(state)
        finally:
            if rest_patch:
                rest_patch.stop()
            if resolver_patch:
                resolver_patch.stop()
        projections = {
            f"projector:{name}": fn(state)
            for name, fn in _mod.PROJECTORS.items()
        }
        projections.update({
            f"slice:{name}": fn(state)
            for name, fn in _mod.SLICERS.items()
        })
        return state, projections

    def test_state_and_all_role_projections_match_legacy_transport(self):
        legacy_trace = []

        def legacy(repo_slug, ref, cache, requires_merge=False):
            key = (repo_slug, ref, requires_merge)
            if key in cache:
                return cache[key]
            legacy_trace.append((repo_slug, ref, requires_merge))
            result = True
            cache[key] = result
            return result

        before, before_projections = self._run_pipeline(
            copy.deepcopy(self._fixture()), resolver=legacy)
        rest = _RestStub({_BLOCKER: "CLOSED"})
        after, after_projections = self._run_pipeline(
            copy.deepcopy(self._fixture()), rest=rest)

        def encode(value):
            return json.dumps(value, sort_keys=True, separators=(",", ":"))

        self.assertEqual(encode(before), encode(after))
        self.assertEqual(encode(before_projections), encode(after_projections))
        self.assertEqual(len(legacy_trace), 1)
        self.assertEqual(len(rest.calls), 1)

        open_after, _ = self._run_pipeline(
            copy.deepcopy(self._fixture()), rest=_RestStub({_BLOCKER: "OPEN"}))
        self.assertNotEqual(encode(before), encode(open_after))

        mutated_projection = copy.deepcopy(after_projections)
        mutated_projection["projector:worker"].append({"positive_control": True})
        self.assertNotEqual(encode(before_projections), encode(mutated_projection))


if __name__ == "__main__":
    unittest.main()
