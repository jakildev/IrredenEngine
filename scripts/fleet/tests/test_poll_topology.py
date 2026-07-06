"""Tests for the #1394 Q2 centralized-polling topology.

Two layers:
  * fleet_poll_topology unit tests — config parse, generated_at staleness,
    bundle build/parse/apply, host-local overlay, and a real leader-server ↔
    follower-fetch HTTP round-trip (200 / 304 / leader-down).
  * fleet-state-scout integration — build_state()'s leader vs follower routing:
    a follower serving a bundle makes ZERO GitHub calls (collect_state is never
    invoked), preserves the leader's generated_at, swaps in host-local fields,
    and falls back to a GitHub self-poll when the leader is unreachable or its
    generated_at has gone stale.

stdlib-only; the HTTP round-trip binds 127.0.0.1:0 (ephemeral) — no network.
"""
import importlib.machinery
import importlib.util
import json
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

_HERE = Path(__file__).resolve().parent
_FLEET = _HERE.parent
if str(_FLEET) not in os.sys.path:
    os.sys.path.insert(0, str(_FLEET))

import fleet_poll_topology as T  # noqa: E402


def _load_scout():
    loader = importlib.machinery.SourceFileLoader(
        "fleet_state_scout", str(_FLEET / "fleet-state-scout"))
    spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def _stamp(offset_seconds=0):
    return time.strftime(
        "%Y-%m-%dT%H:%M:%SZ", time.gmtime(T.now_epoch() - offset_seconds))


# --- fleet_poll_topology --------------------------------------------------


class TestPollConfig(unittest.TestCase):

    def test_missing_file_is_leader(self):
        cfg = T.read_poll_config("/no/such/host.toml")
        self.assertTrue(cfg.is_leader)
        self.assertEqual(cfg.port, T.DEFAULT_POLL_PORT)

    def test_follower_section_parsed(self):
        with tempfile.NamedTemporaryFile("w", suffix=".toml", delete=False) as f:
            f.write('[cpu]\nbudget = 8\n\n[fleet]\n'
                    'poll_role = "follower"\npoll_port = 9001\n'
                    'leader_host = "10.0.0.2"\nleader_stale_seconds = 45\n')
            path = f.name
        self.addCleanup(os.unlink, path)
        cfg = T.read_poll_config(path)
        self.assertTrue(cfg.is_follower)
        self.assertEqual(cfg.port, 9001)
        self.assertEqual(cfg.leader_host, "10.0.0.2")
        self.assertEqual(cfg.leader_stale_seconds, 45)

    def test_unknown_role_falls_back_to_leader(self):
        with tempfile.NamedTemporaryFile("w", suffix=".toml", delete=False) as f:
            f.write('[fleet]\npoll_role = "captain"\n')
            path = f.name
        self.addCleanup(os.unlink, path)
        self.assertTrue(T.read_poll_config(path).is_leader)

    def test_minimal_parser_matches_tomllib(self):
        # Force the < 3.11 fallback path and confirm it parses identically.
        with tempfile.NamedTemporaryFile("w", suffix=".toml", delete=False) as f:
            f.write('[fleet]\npoll_role = "follower"\n'
                    'poll_port = 8477  # inline comment\nleader_host = "host.lan"\n')
            path = f.name
        self.addCleanup(os.unlink, path)
        with patch.object(T, "tomllib", None):
            cfg = T.read_poll_config(path)
        self.assertTrue(cfg.is_follower)
        self.assertEqual(cfg.port, 8477)
        self.assertEqual(cfg.leader_host, "host.lan")


class TestStaleness(unittest.TestCase):

    def test_fresh_not_stale(self):
        self.assertFalse(T.is_stale(_stamp(10), T.now_epoch(), 90))

    def test_old_is_stale(self):
        self.assertTrue(T.is_stale(_stamp(500), T.now_epoch(), 90))

    def test_missing_or_garbage_is_stale(self):
        now = T.now_epoch()
        self.assertTrue(T.is_stale(None, now, 90))
        self.assertTrue(T.is_stale("not-a-date", now, 90))

    def test_parse_generated_at(self):
        self.assertIsNone(T.parse_generated_at("garbage"))
        self.assertIsInstance(T.parse_generated_at("2026-07-04T16:00:00Z"), int)

    def test_state_age_prefers_generated_at_over_mtime(self):
        # generated_at is 100s old; mtime claims 5s old (a follower that just
        # rewrote a frozen leader snapshot). Age must follow generated_at.
        now = T.now_epoch()
        age = T.state_age_seconds(_stamp(100), now - 5, now=now)
        self.assertAlmostEqual(age, 100, delta=1)

    def test_state_age_falls_back_to_mtime_when_missing(self):
        now = T.now_epoch()
        self.assertAlmostEqual(
            T.state_age_seconds(None, now - 42, now=now), 42, delta=1)
        self.assertAlmostEqual(
            T.state_age_seconds("not-a-date", now - 42, now=now), 42, delta=1)

    def test_state_age_default_now_is_wall_clock(self):
        # Omitting `now` uses time.time(); a fresh stamp reads as ~0s old.
        self.assertLess(abs(T.state_age_seconds(_stamp(0), 0.0)), 5)


class TestBundle(unittest.TestCase):

    def _leader_dir(self, generated_at="2026-07-04T16:00:00Z"):
        d = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(d, ignore_errors=True))
        (d / "state.json").write_text(json.dumps(
            {"generated_at": generated_at, "repos": {"engine": {"path": "/leader"}}}))
        (d / "prs" / "engine").mkdir(parents=True)
        (d / "prs" / "engine" / "2227.json").write_text('{"number": 2227}')
        (d / "issues" / "engine").mkdir(parents=True)
        (d / "issues" / "engine" / "2220.json").write_text('{"number": 2220}')
        return d

    def test_build_parse_roundtrip(self):
        d = self._leader_dir()
        etag, payload = T.build_bundle(d)
        self.assertEqual(etag, '"2026-07-04T16:00:00Z"')
        bundle = T.parse_bundle(payload)
        self.assertEqual(bundle["generated_at"], "2026-07-04T16:00:00Z")
        self.assertEqual(
            sorted(bundle["files"]),
            ["issues/engine/2220.json", "prs/engine/2227.json", "state.json"])

    def test_missing_state_json_is_no_bundle(self):
        d = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(d, ignore_errors=True))
        self.assertEqual(T.build_bundle(d), (None, b""))

    def test_apply_writes_and_prunes(self):
        d = self._leader_dir()
        _etag, payload = T.build_bundle(d)
        files = T.parse_bundle(payload)["files"]
        follower = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(follower, ignore_errors=True))
        # A stale cache file the bundle no longer carries must be pruned.
        (follower / "prs" / "engine").mkdir(parents=True)
        (follower / "prs" / "engine" / "9999.json").write_text("{}")
        T.apply_bundle_detail_caches(follower, files)
        self.assertEqual(
            sorted(p.name for p in (follower / "prs" / "engine").glob("*.json")),
            ["2227.json"])
        self.assertTrue((follower / "issues" / "engine" / "2220.json").exists())

    def test_overlay_preserves_generated_at_swaps_host_local(self):
        leader_state = {"generated_at": "2026-07-04T16:00:00Z",
                        "repos": {"engine": {"path": "/leader/eng", "prs": [1]}}}
        out = T.overlay_host_local(
            leader_state, engine_path="/follower/eng", game_path=None,
            clone_freshness={"fresh": True, "behind": 0})
        self.assertEqual(out["generated_at"], "2026-07-04T16:00:00Z")
        self.assertEqual(out["repos"]["engine"]["path"], "/follower/eng")
        self.assertEqual(out["repos"]["engine"]["prs"], [1])  # data untouched
        self.assertEqual(out["clone_freshness"]["fresh"], True)
        # original leader_state must not be mutated in place
        self.assertEqual(leader_state["repos"]["engine"]["path"], "/leader/eng")


class TestLeaderFollowerHTTP(unittest.TestCase):

    def setUp(self):
        self.d = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.d, ignore_errors=True))
        (self.d / "state.json").write_text(json.dumps(
            {"generated_at": "2026-07-04T16:10:00Z", "repos": {"engine": {"path": "/l"}}}))
        self.srv = T.LeaderServer(0, "127.0.0.1")
        self.addCleanup(self.srv.stop)
        self.port = self.srv._httpd.server_address[1]
        self.srv.update_bundle(*T.build_bundle(self.d))
        self.srv.start()
        self.cfg = T.PollConfig("follower", self.port, "127.0.0.1", 90)

    def test_200_then_304_then_200_after_tick(self):
        status, etag, files = T.fetch_from_leader(self.cfg, None)
        self.assertEqual(status, 200)
        self.assertIn("state.json", files)

        # Same etag → 304, no body (the zero-cost unchanged poll).
        status2, _etag2, files2 = T.fetch_from_leader(self.cfg, etag)
        self.assertEqual(status2, 304)
        self.assertIsNone(files2)

        # Leader ticks (new generated_at → new etag) → 200 again.
        (self.d / "state.json").write_text(json.dumps(
            {"generated_at": "2026-07-04T16:10:30Z", "repos": {"engine": {"path": "/l"}}}))
        self.srv.update_bundle(*T.build_bundle(self.d))
        status3, _etag3, files3 = T.fetch_from_leader(self.cfg, etag)
        self.assertEqual(status3, 200)
        self.assertEqual(
            json.loads(files3["state.json"])["generated_at"], "2026-07-04T16:10:30Z")

    def test_leader_down_returns_none(self):
        self.srv.stop()
        time.sleep(0.1)
        status, _etag, files = T.fetch_from_leader(self.cfg, None)
        self.assertIsNone(status)
        self.assertIsNone(files)

    def test_no_leader_host_returns_none(self):
        cfg = T.PollConfig("follower", self.port, None, 90)
        self.assertEqual(T.fetch_from_leader(cfg, None)[0], None)


# --- fleet-state-scout build_state() routing ------------------------------


class TestScoutBuildState(unittest.TestCase):

    def setUp(self):
        self.scout = _load_scout()
        self.state_dir = Path(tempfile.mkdtemp())
        self.addCleanup(
            lambda: __import__("shutil").rmtree(self.state_dir, ignore_errors=True))
        # Redirect all scout state paths into the temp dir.
        self.scout.STATE_DIR = self.state_dir
        self.scout.STATE_FILE = self.state_dir / "state.json"
        self.scout.ENGINE = self.state_dir / "engine"  # non-git → clone_freshness fresh
        self.scout.GAME = self.state_dir / "game"       # no .git → game_path None
        # A distinctive collect_state stand-in so we can assert whether GitHub
        # was polled (its presence in the result == a self-poll / leader tick).
        self._collect = MagicMock(return_value={"generated_at": _stamp(0),
                                                 "repos": {"engine": {"path": "SELF"}}})
        self.scout.collect_state = self._collect

    def test_leader_polls_github(self):
        self.scout.POLL_CFG = T.PollConfig("leader", 8477, None, 90)
        state, served = self.scout.build_state()
        self.assertFalse(served)
        self._collect.assert_called_once()
        self.assertEqual(state["repos"]["engine"]["path"], "SELF")

    def test_none_cfg_polls_github(self):
        self.scout.POLL_CFG = None
        _state, served = self.scout.build_state()
        self.assertFalse(served)
        self._collect.assert_called_once()

    def test_follower_consumes_bundle_zero_github(self):
        self.scout.POLL_CFG = T.PollConfig("follower", 8477, "leader.lan", 90)
        leader_state = {"generated_at": _stamp(5),
                        "repos": {"engine": {"path": "/leader/eng", "prs": [{"number": 1}]}}}
        files = {"state.json": json.dumps(leader_state),
                 "prs/engine/1.json": '{"number": 1}'}
        with patch.object(self.scout.fleet_poll_topology, "fetch_from_leader",
                          return_value=(200, '"etag-a"', files)):
            state, served = self.scout.build_state()
        self.assertTrue(served)
        self._collect.assert_not_called()  # ZERO GitHub reads on the follower
        # generated_at preserved verbatim; host-local path re-derived locally.
        self.assertEqual(state["generated_at"], leader_state["generated_at"])
        self.assertEqual(state["repos"]["engine"]["path"], str(self.scout.ENGINE))
        self.assertEqual(state["repos"]["engine"]["prs"], [{"number": 1}])
        self.assertIn("clone_freshness", state)
        # Detail cache mirrored to the follower's state dir.
        self.assertTrue((self.state_dir / "prs" / "engine" / "1.json").exists())

    def test_follower_unreachable_self_polls(self):
        self.scout.POLL_CFG = T.PollConfig("follower", 8477, "leader.lan", 90)
        with patch.object(self.scout.fleet_poll_topology, "fetch_from_leader",
                          return_value=(None, None, None)):
            state, served = self.scout.build_state()
        self.assertFalse(served)  # fell back to self-poll
        self._collect.assert_called_once()
        self.assertEqual(state["repos"]["engine"]["path"], "SELF")

    def test_follower_stale_leader_bundle_self_polls(self):
        # A 200 whose generated_at is already past the leader-stale window must
        # NOT be trusted — self-poll instead of propagating a frozen timestamp.
        self.scout.POLL_CFG = T.PollConfig("follower", 8477, "leader.lan", 90)
        stale = {"generated_at": _stamp(500), "repos": {"engine": {"path": "/l"}}}
        files = {"state.json": json.dumps(stale)}
        with patch.object(self.scout.fleet_poll_topology, "fetch_from_leader",
                          return_value=(200, '"old"', files)):
            _state, served = self.scout.build_state()
        self.assertFalse(served)
        self._collect.assert_called_once()

    def test_follower_304_reuses_fresh_on_disk(self):
        self.scout.POLL_CFG = T.PollConfig("follower", 8477, "leader.lan", 90)
        on_disk = {"generated_at": _stamp(10),
                   "repos": {"engine": {"path": str(self.scout.ENGINE)}}}
        self.scout.STATE_FILE.write_text(json.dumps(on_disk))
        with patch.object(self.scout.fleet_poll_topology, "fetch_from_leader",
                          return_value=(304, '"etag-a"', None)):
            state, served = self.scout.build_state()
        self.assertTrue(served)
        self._collect.assert_not_called()
        self.assertEqual(state["generated_at"], on_disk["generated_at"])

    def test_follower_304_stale_on_disk_self_polls(self):
        self.scout.POLL_CFG = T.PollConfig("follower", 8477, "leader.lan", 90)
        stale = {"generated_at": _stamp(500), "repos": {"engine": {"path": "/l"}}}
        self.scout.STATE_FILE.write_text(json.dumps(stale))
        with patch.object(self.scout.fleet_poll_topology, "fetch_from_leader",
                          return_value=(304, '"etag-a"', None)):
            _state, served = self.scout.build_state()
        self.assertFalse(served)  # frozen leader → self-poll
        self._collect.assert_called_once()


if __name__ == "__main__":
    unittest.main()
