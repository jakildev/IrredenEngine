"""Tests for the scout's provenance-backfill spawn.

`fleet-pr-stamp --backfill` writes a global label (fleet:author-claude on
every open PR whose head commit carries the Claude co-author trailer), so it
rides the authoritative poller's claim-cleanup cadence beside `cleanup --gh`:
one process per healthy repo, never on a follower, never on a degraded tick,
and the lane's all-or-none partial-failure rule leaves the cleanup marker
unwritten when the backfill spawn fails.

The tick harness is shared with test_scout_degraded_fetch.
"""
import tempfile
import unittest
from pathlib import Path

from test_scout_degraded_fetch import _mod, _ScoutTickHarness


def _backfill_argvs(spawns):
    """Each backfill spawn's argv from `fleet-pr-stamp` on (the interpreter
    and the resolved script path are the host's business, not the lane's)."""
    out = []
    for argv in spawns:
        words = [str(x) for x in argv]
        if "--backfill" not in words:
            continue
        start = next(i for i, w in enumerate(words) if w.endswith("fleet-pr-stamp"))
        out.append(["fleet-pr-stamp", *words[start + 1:]])
    return out


def _raise_on(needle):
    def _p(argv, *a, **kw):
        if needle in [str(x) for x in argv]:
            raise OSError(35, "Resource temporarily unavailable")
        return None
    return _p


class TestProvenanceBackfill(_ScoutTickHarness, unittest.TestCase):

    def _marker(self, tmp):
        return Path(tmp) / "seen-hashes" / _mod.CLAIM_CLEANUP_MARKER_NAME

    def _game(self, tmp):
        game = Path(tmp) / "game"
        (game / ".git").mkdir(parents=True)
        return game

    def test_authoritative_poller_backfills_every_healthy_repo(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       served=False, game_dir=self._game(tmp))
            self.assertEqual(_backfill_argvs(spawns), [
                ["fleet-pr-stamp", "--backfill", "--repo", "jakildev/IrredenEngine"],
                ["fleet-pr-stamp", "--backfill", "--repo", "jakildev/irreden"],
            ])
            self.assertTrue(self._marker(tmp).exists())

    def test_engine_only_host_backfills_the_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=False)
            self.assertEqual(_backfill_argvs(spawns), [
                ["fleet-pr-stamp", "--backfill", "--repo", "jakildev/IrredenEngine"],
            ])

    def test_follower_never_backfills(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       served=True, game_dir=self._game(tmp))
            self.assertEqual(_backfill_argvs(spawns), [])
            self.assertTrue(self._marker(tmp).exists(),
                            "the follower's own cleanup cadence still runs")

    def test_cadence_is_the_cleanup_interval(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=False)
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=False)
            self.assertEqual(len(_backfill_argvs(spawns)), 1, "not due again inside the interval")
            m = self._marker(tmp)
            m.write_text(f"{int(m.read_text()) - _mod.CLAIM_CLEANUP_INTERVAL_SECONDS}\n")
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=False)
            self.assertEqual(len(_backfill_argvs(spawns)), 2, "an elapsed interval fires again")

    def test_failed_backfill_spawn_leaves_the_marker_unwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=False,
                       popen=_raise_on("--backfill"))
            self.assertFalse(self._marker(tmp).exists(),
                             "a failed backfill spawn must not consume the cadence")

    def test_degraded_tick_backfills_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=True, spawns=spawns, served=False)
            self.assertEqual(_backfill_argvs(spawns), [])
            self.assertFalse(self._marker(tmp).exists())


if __name__ == "__main__":
    unittest.main()
