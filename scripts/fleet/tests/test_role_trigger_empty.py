"""Tests for update_role_trigger's empty-projection wake suppression.

A projection-hash change whose NEW projection is empty is a transition to
nothing-to-do (a verdict label-swap, an amend claim, a merge emptied the
set). Waking the role then is a guaranteed no-op iteration — observed
2026-07-01 as the opus-reviewer dispatching 4x in 5 minutes, every
iteration "no actionable candidates". The invariants:

  - non-empty -> non-empty change: hash recorded, trigger touched;
  - non-empty -> EMPTY change: hash recorded, trigger NOT touched (the
    trailing wake is swallowed);
  - empty -> non-empty change: fires normally (the recorded empty hash
    guarantees the flip back is seen as a change);
  - unchanged projection: no hash write, no trigger (pre-existing).
"""
import importlib.machinery
import importlib.util
import tempfile
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class UpdateRoleTriggerEmpty(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        base = Path(self._tmp.name)
        self._orig_seen = _mod.SEEN_DIR
        self._orig_triggers = _mod.TRIGGERS_DIR
        _mod.SEEN_DIR = base / "seen-hashes"
        _mod.TRIGGERS_DIR = base / "triggers"
        _mod.SEEN_DIR.mkdir(parents=True)
        _mod.TRIGGERS_DIR.mkdir(parents=True)

    def tearDown(self):
        _mod.SEEN_DIR = self._orig_seen
        _mod.TRIGGERS_DIR = self._orig_triggers
        self._tmp.cleanup()

    def _trigger_exists(self, role):
        return (_mod.TRIGGERS_DIR / role).exists()

    def _seen(self, role):
        return (_mod.SEEN_DIR / role).read_text().strip()

    def test_non_empty_change_fires(self):
        self.assertTrue(_mod.update_role_trigger("r", [{"pr": 1}]))
        self.assertTrue(self._trigger_exists("r"))

    def test_transition_to_empty_records_hash_without_wake(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()  # dispatcher consumed it
        self.assertFalse(_mod.update_role_trigger("r", []))
        self.assertFalse(self._trigger_exists("r"))
        self.assertEqual(self._seen("r"), _mod.stable_hash([]))

    def test_empty_to_non_empty_fires(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()
        _mod.update_role_trigger("r", [])
        self.assertTrue(_mod.update_role_trigger("r", [{"pr": 2}]))
        self.assertTrue(self._trigger_exists("r"))

    def test_transition_to_empty_leaves_pending_trigger_pending(self):
        # A trigger armed by earlier non-empty work and not yet consumed
        # must survive the empty transition — suppression only skips the
        # touch, it never clears.
        _mod.update_role_trigger("r", [{"pr": 1}])
        self.assertTrue(self._trigger_exists("r"))
        _mod.update_role_trigger("r", [])
        self.assertTrue(self._trigger_exists("r"))

    def test_unchanged_projection_is_inert(self):
        _mod.update_role_trigger("r", [{"pr": 1}])
        (_mod.TRIGGERS_DIR / "r").unlink()
        self.assertFalse(_mod.update_role_trigger("r", [{"pr": 1}]))
        self.assertFalse(self._trigger_exists("r"))


if __name__ == "__main__":
    unittest.main()
