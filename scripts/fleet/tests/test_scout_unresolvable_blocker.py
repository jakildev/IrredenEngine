"""resolve_blocked_by records a same-repo Blocked-by ref that 404s.

A definite HTTP 404 on the ref read means the number names nothing in the
repo (usually a cross-repo ref written without its `Repo#` qualifier). The task
keeps blocking, and gains `blocked_by_unresolvable` so `fleet-claim reconcile`
and `fleet-survey` can say the body line is wrong. Timeouts, 5xx, 401 and
malformed bodies stay fail-closed with no field. The conditional-REST seam is
stubbed to a fixed answer per case, so nothing reaches GitHub.
"""
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


def _state(blocked_by, key="engine"):
    return {"repos": {key: {
        "tasks": {"open": [{"id": "#621", "blocked_by": blocked_by, "area": None}],
                  "in_progress": []},
        "prs": [], "closed_fleet_queued": [], "recent_merged_prs": [],
    }}}


def _reply(http_status=None, body=None, raises=None):
    """A conditional_get stand-in answering one fixed way."""
    def fake(_slug, _path, *, status_out=None, **_kw):
        if raises is not None:
            raise raises
        if http_status is not None and status_out is not None:
            status_out["http_status"] = http_status
        return True, body
    return fake


class UnresolvableBlocker(unittest.TestCase):

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        patcher = patch.object(_mod, "REF_STATE_ETAG_DIR", Path(self._tmp.name))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self._tmp.cleanup)

    def resolve(self, fake, blocked_by="#4753", key="engine"):
        state = _state(blocked_by, key)
        with patch.object(_mod, "conditional_get", fake):
            _mod.resolve_blocked_by(state)
        return state["repos"][key]["tasks"]["open"][0]

    def test_404_marks_the_ref_unresolvable_and_keeps_the_task_blocked(self):
        task = self.resolve(_reply(http_status=404))
        self.assertEqual(task["blocked_by_unresolvable"], "#4753")
        self.assertEqual(task["blocked_by"], "#4753")
        self.assertTrue(task["has_open_blocker"])

    def test_404_on_a_merge_required_pull_ref_is_unresolvable_too(self):
        raw = "https://github.com/jakildev/IrredenEngine/pull/4753"
        task = self.resolve(_reply(http_status=404), blocked_by=raw)
        self.assertEqual(task["blocked_by_unresolvable"], "#4753")

    def test_only_the_404_ref_of_several_is_named(self):
        def fake(_slug, path, *, status_out=None, **_kw):
            if path == "issues/4753":
                status_out["http_status"] = 404
                return True, None
            return True, json.dumps({"state": "open"})
        task = self.resolve(fake, blocked_by="#101, #4753")
        self.assertEqual(task["blocked_by_unresolvable"], "#4753")
        self.assertEqual(task["blocked_by"], "#101, #4753")

    def test_an_open_ref_is_blocked_but_not_unresolvable(self):
        task = self.resolve(_reply(body=json.dumps({"state": "open"})))
        self.assertTrue(task["has_open_blocker"])
        self.assertNotIn("blocked_by_unresolvable", task)

    def test_a_closed_ref_clears_the_task(self):
        task = self.resolve(_reply(body=json.dumps({"state": "closed"})))
        self.assertEqual(task["blocked_by"], "(none)")
        self.assertNotIn("blocked_by_unresolvable", task)

    def test_a_stale_field_is_dropped_once_the_ref_resolves(self):
        state = _state("#4753")
        state["repos"]["engine"]["tasks"]["open"][0]["blocked_by_unresolvable"] = "#4753"
        with patch.object(_mod, "conditional_get",
                          _reply(body=json.dumps({"state": "closed"}))):
            _mod.resolve_blocked_by(state)
        self.assertNotIn("blocked_by_unresolvable",
                         state["repos"]["engine"]["tasks"]["open"][0])

    def test_transient_arms_stay_fail_closed_and_unclassified(self):
        arms = {
            "5xx": _reply(http_status=503),
            "401": _reply(http_status=401),
            "timeout": _reply(raises=TimeoutError()),
            "network": _reply(raises=OSError("down")),
            "malformed": _reply(body="{not json"),
            "non-object": _reply(body="[]"),
            "no-body": _reply(),
        }
        for name, fake in arms.items():
            with self.subTest(name):
                task = self.resolve(fake)
                self.assertTrue(task["has_open_blocker"])
                self.assertEqual(task["blocked_by"], "#4753")
                self.assertNotIn("blocked_by_unresolvable", task)

    def test_a_cross_repo_qualified_ref_is_never_read_or_flagged(self):
        def fake(*_a, **_kw):
            raise AssertionError("a cross-repo ref must not reach the REST seam")
        task = self.resolve(fake, blocked_by="IrredenEngine#4753", key="game")
        self.assertNotIn("blocked_by_unresolvable", task)

    def test_the_missing_set_is_dropped_by_the_prune(self):
        cache = {}
        with patch.object(_mod, "conditional_get", _reply(http_status=404)):
            _mod._resolve_ref_satisfied("jakildev/irreden", "621", cache)
        self.assertTrue(_mod._ref_is_missing("jakildev/irreden", "621", cache))
        _mod._prune_ref_state_cache(cache)
        self.assertNotIn(_mod._REF_MISSING_KEY, cache)


if __name__ == "__main__":
    unittest.main()
