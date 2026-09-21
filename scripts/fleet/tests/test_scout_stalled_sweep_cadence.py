"""Tests for the scout's hourly fleet-stalled-sweep spawn.

The sweep's clock is a 7-day TTL on a PR, which no projection hash can
represent — the queue projection can sit unchanged for hours while a
`fleet:wip` PR crosses the threshold. So the lane is level-triggered off its
own deadline marker, exactly like the periodic claim cleanup beside it, and
the discriminating case here is the one a projection-edge fixture would pass
vacuously: **unchanged projection, deadline due → spawn happens anyway**.

Also pinned: the authoritative-poller gate (the sweep posts a global,
permanent comment, so N followers would post N comments), the degraded skip
leaving the deadline unconsumed, and a failed spawn doing the same.

The tick harness is shared with test_scout_degraded_fetch so there is one
hermetic `tick_once()` driver, not two.
"""
import tempfile
import unittest
from pathlib import Path

from test_scout_degraded_fetch import _mod, _ScoutTickHarness


def _stalled_spawns(spawns):
    return [a for a in spawns if any("fleet-stalled-sweep" in str(x) for x in a)]


class TestStalledSweepCadence(_ScoutTickHarness, unittest.TestCase):

    def _marker(self, tmp):
        return Path(tmp) / "seen-hashes" / _mod.STALLED_SWEEP_MARKER_NAME

    def _age_marker(self, tmp):
        """Push the deadline marker a full interval into the past."""
        m = self._marker(tmp)
        m.write_text(f"{int(m.read_text()) - _mod.STALLED_SWEEP_INTERVAL_SECONDS}\n")

    def test_first_tick_spawns_the_sweep_and_writes_the_marker(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)

            fired = _stalled_spawns(spawns)
            self.assertEqual(len(fired), 1, spawns)
            self.assertIn("--repo", fired[0])
            self.assertIn("jakildev/IrredenEngine", fired[0])
            self.assertTrue(self._marker(tmp).exists())

    def test_unchanged_projection_still_fires_once_the_deadline_passes(self):
        """The level-triggered property. A fixture that only ever changes the
        projection would pass with the guard un-widened and the sweep would
        silently fire only when queue state happened to move."""
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(_stalled_spawns(spawns)), 1)

            # Same projection, deadline not yet reached → no second spawn.
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(_stalled_spawns(spawns)), 1)

            self._age_marker(tmp)
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(
                len(_stalled_spawns(spawns)), 2,
                "an elapsed deadline must fire with an unchanged projection")

    def test_follower_never_spawns_the_sweep(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=True)

            self.assertEqual(_stalled_spawns(spawns), [],
                             "the sweep's comment is a global mutation")
            self.assertFalse(self._marker(tmp).exists())

    def test_degraded_tick_spawns_nothing_and_leaves_the_deadline_pending(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=True, spawns=spawns)
            self.assertEqual(_stalled_spawns(spawns), [])
            self.assertFalse(self._marker(tmp).exists())

            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(_stalled_spawns(spawns)), 1)
            self.assertTrue(self._marker(tmp).exists())

    def test_failed_spawn_leaves_the_marker_unwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            def _boom(argv, *a, **kw):
                raise OSError("fork refused")

            self._tick(tmp, ["issue-1"], degraded=False, spawns=[], popen=_boom)
            self.assertFalse(self._marker(tmp).exists())

            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns)
            self.assertEqual(len(_stalled_spawns(spawns)), 1)

    def test_game_clone_present_sweeps_both_repos_in_one_process(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "game"
            (game / ".git").mkdir(parents=True)
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       game_dir=game)

            fired = _stalled_spawns(spawns)
            self.assertEqual(len(fired), 1, "one process, both repos")
            self.assertIn("jakildev/IrredenEngine", fired[0])
            self.assertIn("jakildev/irreden", fired[0])

    def test_interval_is_hourly_and_far_under_the_threshold_it_measures(self):
        self.assertEqual(_mod.STALLED_SWEEP_INTERVAL_SECONDS, 3600)
        self.assertNotEqual(_mod.STALLED_SWEEP_MARKER_NAME,
                            _mod.CLAIM_CLEANUP_MARKER_NAME)


if __name__ == "__main__":
    unittest.main()
