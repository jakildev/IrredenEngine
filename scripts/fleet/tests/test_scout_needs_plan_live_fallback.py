"""Regression test for #2986: resolve_needs_plan_blocked_by resolved a
needs-plan issue's `**Blocked by:** #N` against in-memory sources only
(closed_fleet_queued + recent merged `claude/<N>-*` heads), with none of the
live `_resolve_ref_satisfied` fallback its sibling resolve_blocked_by uses.

Any blocker that closed outside those two sources therefore pinned the issue
`blocked: True` forever — a hard gate in project_worker (never wakes a planner
dispatch) and in the role slice (can never be assigned as FLEET_PLAN_ISSUE), so
the false positive is permanent and silent.

The live incident: engine #2330, `Blocked by: #2310`, with #2310 CLOSED since
2026-07-09. #2310 carries no labels, so it is absent from the
`labels=fleet:queued` closed set at *any* window size — widening the window
(what #2856 did for fetch_human_approved) does not reach this lane.

The `gh` stub models `gh issue view <N> --repo <slug> --json state --jq .state`
argument-for-argument and raises on any call shape it does not model, per
scripts/fleet/CLAUDE.md ("a CLI stub models the tool's argument parsing, not
just its endpoint" / mock at a seam that fails closed). test_stub_fidelity
pins that rejection so a later stub rewrite cannot silently reopen the hole.

Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet.
"""
import importlib.machinery
import importlib.util
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

resolve_needs_plan_blocked_by = _mod.resolve_needs_plan_blocked_by

# The incident's real numbers, so a reader can line the fixture up with #2986.
_BLOCKED_ISSUE = 2330
_BLOCKER = "2310"


class _GhStub:
    """Emulates `gh issue view <ref> --repo <slug> --json state --jq .state`.

    `states` maps ref -> the state string the real gh would print. A ref absent
    from it is a fixture error, not a pass — the stub raises rather than
    returning an empty stdout that the subject would read as "not satisfied".
    """

    def __init__(self, states, raises=False):
        self.states = states
        self.raises = raises
        self.calls = []

    def __call__(self, argv, **kwargs):
        # Model the real argument vector; anything else is an unmodeled call.
        # gh issue view <ref> --repo <slug> --json state --jq .state  == 10 args
        if len(argv) != 10 or argv[:3] != ["gh", "issue", "view"]:
            raise AssertionError(f"unmodeled gh invocation: {argv!r}")
        ref = argv[3]
        if argv[4] != "--repo" or argv[6] != "--json" or argv[8] != "--jq":
            raise AssertionError(f"unmodeled gh flags: {argv!r}")
        if argv[7] != "state" or argv[9] != ".state":
            raise AssertionError(f"unmodeled gh json/jq program: {argv!r}")
        self.calls.append((argv[5], ref))
        if self.raises:
            raise subprocess.TimeoutExpired(cmd=argv, timeout=10)
        if ref not in self.states:
            raise AssertionError(
                f"fixture gap: no state planted for #{ref} (calls={self.calls})")
        return subprocess.CompletedProcess(
            argv, 0, stdout=self.states[ref] + "\n", stderr="")


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


def _run(state, stub):
    with patch.object(_mod.subprocess, "run", stub):
        resolve_needs_plan_blocked_by(state)
    return state["repos"]["engine"]["needs_plan"][0]["blocked"]


class NeedsPlanLiveFallback(unittest.TestCase):

    def test_closed_blocker_outside_inmemory_sources_unblocks(self):
        """THE FIX. Blocker in neither in-memory source, CLOSED live.

        Fails against pre-fix code: with no fallback the ref stays unresolved
        and the issue projects blocked=True forever.
        """
        stub = _GhStub({_BLOCKER: "CLOSED"})
        self.assertFalse(_run(_state(), stub))
        self.assertEqual(stub.calls, [("jakildev/IrredenEngine", _BLOCKER)])

    def test_merged_blocker_unblocks(self):
        """A `Blocked by:` naming a PR number that MERGED is satisfied too."""
        self.assertFalse(_run(_state(), _GhStub({_BLOCKER: "MERGED"})))

    def test_open_blocker_still_blocks(self):
        """NEGATIVE CONTROL — the fallback must not be a gate that cannot fire.

        Same code path, only the live verdict differs.
        """
        stub = _GhStub({_BLOCKER: "OPEN"})
        self.assertTrue(_run(_state(), stub))
        self.assertEqual(len(stub.calls), 1)

    def test_gh_failure_fails_closed(self):
        """A flaky/timing-out gh leaves the issue blocked — the conservative
        direction, matching pre-fix behaviour rather than inverting it."""
        self.assertTrue(_run(_state(), _GhStub({}, raises=True)))

    def test_inmemory_hit_makes_no_live_call(self):
        """The common path must not acquire a per-tick network cost: a blocker
        already in closed_fleet_queued short-circuits before the fallback."""
        stub = _GhStub({})  # any call at all is a fixture gap -> raises
        self.assertFalse(_run(_state(closed=[int(_BLOCKER)]), stub))
        self.assertEqual(stub.calls, [])

    def test_merged_head_hit_makes_no_live_call(self):
        stub = _GhStub({})
        heads = [f"claude/{_BLOCKER}-light-volume-continuity"]
        self.assertFalse(_run(_state(merged_heads=heads), stub))
        self.assertEqual(stub.calls, [])

    def test_cross_repo_ref_makes_no_live_call(self):
        """A qualified cross-repo ref is deferred to the ingest's own check;
        it must not be resolved (or blocked on) from this repo's lane."""
        stub = _GhStub({})
        self.assertFalse(_run(_state(blocked_by="Irreden#41"), stub))
        self.assertEqual(stub.calls, [])

    def test_ref_cache_is_shared_across_lanes(self):
        """One live call per novel ref per tick, not one per lane."""
        cache = {}
        stub = _GhStub({_BLOCKER: "CLOSED"})
        with patch.object(_mod.subprocess, "run", stub):
            resolve_needs_plan_blocked_by(_state(), cache)
            resolve_needs_plan_blocked_by(_state(), cache)
        self.assertEqual(len(stub.calls), 1)

    def test_stub_fidelity(self):
        """The stub rejects the shapes it claims to reject — otherwise the
        arms above certify a call the real gh would never accept."""
        stub = _GhStub({_BLOCKER: "CLOSED"})
        with self.assertRaises(AssertionError):
            stub(["gh", "issue", "view", _BLOCKER])
        with self.assertRaises(AssertionError):
            stub(["gh", "pr", "view", _BLOCKER, "--repo", "x",
                  "--json", "state", "--jq", ".state"])
        with self.assertRaises(AssertionError):
            stub(["gh", "issue", "view", _BLOCKER, "--repository", "x",
                  "--json", "state", "--jq", ".state"])


if __name__ == "__main__":
    unittest.main()
