"""Tests for the scout's per-host claim-cleanup spawn.

Claim liveness is judged only on the host that owns the claim (heartbeats and
dispatch records are host-local), so every host runs the claim-cleanup cadence:
the authoritative poller spawns the full, global `fleet-claim cleanup --gh`,
and a follower spawns `cleanup --gh --own-host`, which touches only labels
naming its own host. Pinned: both argv forms, both repos when the game clone
is present, and the lane's all-or-none partial-failure rule on the follower
(the cleanup marker is written only once every spawn succeeded).

The tick harness is shared with test_scout_degraded_fetch.
"""
import tempfile
import unittest
from pathlib import Path

from test_scout_degraded_fetch import _mod, _ScoutTickHarness


def _cleanup_argvs(spawns):
    """Each cleanup spawn's argv from `fleet-claim` on (the win32 bash wrapper
    and the resolved script path are the host's business, not the lane's)."""
    out = []
    for argv in spawns:
        words = [str(x) for x in argv]
        if "cleanup" not in words:
            continue
        start = next(i for i, w in enumerate(words) if w.endswith("fleet-claim"))
        out.append(["fleet-claim", *words[start + 1:]])
    return out


def _raise_on(needle):
    def _p(argv, *a, **kw):
        if needle in [str(x) for x in argv]:
            raise OSError(35, "Resource temporarily unavailable")
        return None
    return _p


class TestClaimCleanupPerHost(_ScoutTickHarness, unittest.TestCase):

    def _marker(self, tmp):
        return Path(tmp) / "seen-hashes" / _mod.CLAIM_CLEANUP_MARKER_NAME

    def _game(self, tmp):
        game = Path(tmp) / "game"
        (game / ".git").mkdir(parents=True)
        return game

    def test_follower_spawns_the_own_host_form_for_both_repos(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       served=True, game_dir=self._game(tmp))
            self.assertEqual(_cleanup_argvs(spawns), [
                ["fleet-claim", "cleanup", "--gh", "--own-host",
                 "--repo", "jakildev/IrredenEngine"],
                ["fleet-claim", "--repo", "game", "cleanup", "--gh", "--own-host",
                 "--repo", "jakildev/irreden"],
            ])
            self.assertTrue(self._marker(tmp).exists())

    def test_authoritative_poller_spawns_the_full_form(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns,
                       served=False, game_dir=self._game(tmp))
            self.assertEqual(_cleanup_argvs(spawns), [
                ["fleet-claim", "cleanup", "--gh", "--repo", "jakildev/IrredenEngine"],
                ["fleet-claim", "--repo", "game", "cleanup", "--gh",
                 "--repo", "jakildev/irreden"],
            ])

    def test_follower_cadence_is_level_triggered_like_the_leaders(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=True)
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=True)
            self.assertEqual(len(_cleanup_argvs(spawns)), 1, "not due again inside the interval")
            m = self._marker(tmp)
            m.write_text(f"{int(m.read_text()) - _mod.CLAIM_CLEANUP_INTERVAL_SECONDS}\n")
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=True)
            self.assertEqual(len(_cleanup_argvs(spawns)), 2, "an elapsed interval fires again")

    def test_follower_partial_failure_leaves_the_marker_unwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=False, spawns=spawns, served=True,
                       game_dir=self._game(tmp), popen=_raise_on("jakildev/irreden"))
            self.assertFalse(self._marker(tmp).exists(),
                             "a failed game-repo spawn must not consume the cadence")

    def test_degraded_follower_tick_spawns_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            spawns = []
            self._tick(tmp, ["issue-1"], degraded=True, spawns=spawns, served=True)
            self.assertEqual(_cleanup_argvs(spawns), [])
            self.assertFalse(self._marker(tmp).exists())


if __name__ == "__main__":
    unittest.main()
