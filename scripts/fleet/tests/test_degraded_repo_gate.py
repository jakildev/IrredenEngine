"""Tests for the scout's per-repo degraded gate.

A degraded fetch field names its repo (`game.prs`). The queue-manager and
ingest lanes used to park on ANY degraded field, so one repo's failing fetch
held the other repo's reconcile, claim cleanup, stalled sweep and ingest:
approved issues stayed unqueued and abandoned claim labels stayed on PRs.
These cases pin the split: the healthy repo's half runs, the held repo is
owed a run that settles on its next healthy tick, and the ingest projection
handed to fleet-queue-ingest carries only the healthy repo's issues.

Import the script via importlib because it has no .py extension.
"""
import importlib.machinery
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout_degraded", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout_degraded", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class DegradedRepoKeys(unittest.TestCase):
    def test_keys_are_the_field_prefixes(self):
        state = {"degraded": ["game.prs", "game.tasks"]}
        self.assertEqual(_mod._degraded_repo_keys(state), {"game"})

    def test_no_degraded_field_is_empty(self):
        self.assertEqual(_mod._degraded_repo_keys({}), set())
        self.assertEqual(_mod._degraded_repo_keys({"degraded": []}), set())


class SweepRepoSplit(unittest.TestCase):
    def test_game_degraded_keeps_the_engine_half(self):
        healthy, held = _mod._sweep_repo_split({"game"}, game_present=True)
        self.assertEqual(healthy, [("engine", "jakildev/IrredenEngine")])
        self.assertEqual(held, ["game"])

    def test_engine_degraded_keeps_the_game_half(self):
        healthy, held = _mod._sweep_repo_split({"engine"}, game_present=True)
        self.assertEqual(healthy, [("game", "jakildev/irreden")])
        self.assertEqual(held, ["engine"])

    def test_both_degraded_leaves_nothing_to_sweep(self):
        healthy, held = _mod._sweep_repo_split({"engine", "game"}, game_present=True)
        self.assertEqual(healthy, [])
        self.assertEqual(held, ["engine", "game"])

    def test_absent_game_clone_is_neither_swept_nor_held(self):
        healthy, held = _mod._sweep_repo_split({"game"}, game_present=False)
        self.assertEqual(healthy, [("engine", "jakildev/IrredenEngine")])
        self.assertEqual(held, [])

    def test_nothing_degraded_sweeps_both_in_order(self):
        healthy, held = _mod._sweep_repo_split(set(), game_present=True)
        self.assertEqual([key for key, _ in healthy], ["engine", "game"])
        self.assertEqual(held, [])


class OwedMarkers(unittest.TestCase):
    def test_owe_then_settle_round_trips(self):
        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(_mod, "DEGRADED_OWED_DIR", Path(tmp) / "owed"):
            self.assertFalse(_mod._lane_owed("queue-manager", "game"))
            _mod._owe_lane("queue-manager", "game")
            self.assertTrue(_mod._lane_owed("queue-manager", "game"))
            self.assertFalse(_mod._lane_owed("queue-manager", "engine"), "per repo")
            self.assertFalse(_mod._lane_owed("queue-manager-ingest", "game"), "per lane")
            _mod._settle_lane("queue-manager", "game")
            self.assertFalse(_mod._lane_owed("queue-manager", "game"))
            _mod._settle_lane("queue-manager", "game")  # idempotent


class FilterIngestProjection(unittest.TestCase):
    def test_only_healthy_repo_issues_survive(self):
        projection = {
            "pending_issues": [{"number": 1, "repo": "engine"}, {"number": 2, "repo": "game"}],
            "unblock_issues": [{"number": 3, "repo": "game"}],
            "retract_issues": [{"number": 4}],
        }
        out = _mod._filter_ingest_projection(projection, ["engine"])
        self.assertEqual(out["pending_issues"], [{"number": 1, "repo": "engine"}])
        self.assertEqual(out["unblock_issues"], [])
        self.assertEqual(out["retract_issues"], [{"number": 4}], "a missing repo reads engine")
        self.assertEqual(projection["pending_issues"][1]["repo"], "game", "input untouched")


if __name__ == "__main__":
    unittest.main()
