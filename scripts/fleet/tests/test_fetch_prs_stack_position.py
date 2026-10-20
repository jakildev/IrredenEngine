"""Hermetic tests for the stack_position field on fetched PR records."""
import importlib.machinery
import importlib.util
import json
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

_REPO = "jakildev/IrredenEngine"


def _record(number):
    return {"number": number, "mergeable": "MERGEABLE",
            "labels": [], "schema": _mod.PR_RECORD_SCHEMA}


class FetchPrStackPosition(unittest.TestCase):
    def setUp(self):
        _mod._mergeable_requery_ticks.clear()
        _mod._prs_refetch_pending.clear()

    def _fetch(self, detector_result, fresh, prev=None):
        def detector(_repo, path, **_kwargs):
            if path != "pulls":
                self.fail(f"unexpected network path: {path}")
            return detector_result

        def graphql(repo, keep=None):
            self.assertEqual(repo, _REPO)
            return fresh

        with patch.object(_mod, "conditional_get", side_effect=detector), \
                patch.object(_mod, "_fetch_prs_graphql", side_effect=graphql):
            return _mod.fetch_prs(_REPO, prev=prev)

    def test_readable_detector_stamps_position_and_null(self):
        body = json.dumps([
            {"number": 5, "stack": {"position": 2}},
            {"number": 6, "stack": None},
        ])
        out = self._fetch((True, body), [_record(5), _record(6)])
        self.assertEqual([pr["stack_position"] for pr in out], [2, None])
        self.assertTrue(all(pr["schema"] == _mod.PR_RECORD_SCHEMA for pr in out))

    def test_unreadable_detector_carries_previous_position(self):
        prev = [_record(5)]
        prev[0]["stack_position"] = 2
        out = self._fetch((True, None), [_record(5), _record(6)], prev=prev)
        self.assertEqual([pr["stack_position"] for pr in out], [2, None])

    def test_plain_304_reuses_previous_list_by_identity(self):
        prev = [_record(5)]
        prev[0]["stack_position"] = 2
        body = json.dumps([{"number": 5, "stack": {"position": 2}}])
        out = self._fetch((False, body), self.fail, prev=prev)
        self.assertIs(out, prev)


if __name__ == "__main__":
    unittest.main()
