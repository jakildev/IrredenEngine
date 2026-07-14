"""parse_dt Z-suffix regression: applied_at silently returned None, killing
both mechanical transitions."""
import importlib.machinery
import importlib.util
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "review-fleet-feedback"
_loader = importlib.machinery.SourceFileLoader("review_fleet_feedback", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("review_fleet_feedback", _loader)
_mod = importlib.util.module_from_spec(_spec)
# Register before exec so @dataclass cls.__module__ resolves in sys.modules (Python 3.12+).
sys.modules["review_fleet_feedback"] = _mod
_loader.exec_module(_mod)

parse_dt = _mod.parse_dt
Entry = _mod.Entry
Cluster = _mod.Cluster
cross_reference = _mod.cross_reference


class TestParseDt(unittest.TestCase):
    def test_z_suffixed_utc_parses(self):
        # The exact shape `apply` writes into applied_at — the regression.
        self.assertEqual(
            parse_dt("2026-05-24T06:39:33Z"),
            datetime(2026, 5, 24, 6, 39, 33, tzinfo=timezone.utc),
        )

    def test_all_persisted_formats_parse(self):
        for s in (
            "2026-05-24T06:39:33Z",   # writer / GitHub
            "2026-05-24T06:39:33",    # T-sep, no zone
            "2026-05-28T21:43:13",    # marker
            "2026-05-24 23:05",       # space-sep, no seconds
            "2026-05-31 19:08:33",    # space-sep, seconds
            "2026-06-13",             # date-only (must match Entry.dt)
        ):
            self.assertIsNotNone(parse_dt(s), f"{s!r} should parse")

    def test_date_only_agrees_with_entry_dt(self):
        # Reader (Entry.dt) and writer (parse_dt) must never disagree on a
        # date-only stamp — a mismatch is what broke recurrence.
        e = Entry(ts="2026-06-13", role="r", headline="h")
        self.assertEqual(parse_dt("2026-06-13"), e.dt)

    def test_empty_and_garbage_are_none(self):
        for s in ("", None, "garbage", "2026/06/13"):
            self.assertIsNone(parse_dt(s))


class TestRecurrenceFires(unittest.TestCase):
    """End-to-end: an applied fix with Z-suffixed applied_at must flip to
    recurring when a fresh entry lands after it."""

    def _entry(self, ts):
        return Entry(ts=ts, role="opus-worker-1", headline="snag recurred")

    def test_applied_with_z_stamp_recurs_on_fresh_entry(self):
        cluster = Cluster("state-cache-lag", [self._entry("2026-06-13 10:00")])
        rows = [{
            "id": "fix-006", "signature": "state-cache-lag", "status": "applied",
            "applied_at": "2026-05-24T06:39:33Z",  # Z-suffixed — the trap
            "proposed_at": "2026-05-23T00:00:00", "applied_ref": "#1136",
            "verified_closed_at": None, "last_recurrence_at": None,
            "recurrence_count": 0, "notes": "",
        }]
        now = datetime(2026, 6, 13, 12, 0, 0, tzinfo=timezone.utc)
        recurring, _still, _closed, _fresh, _singles, mutations = cross_reference(
            [cluster], rows, now)
        self.assertEqual(len(recurring), 1, "applied fix with fresh entry must recur")
        self.assertEqual(recurring[0]["row"]["id"], "fix-006")
        self.assertTrue(any(m["op"] == "flip-to-recurring" and m["id"] == "fix-006"
                            for m in mutations))

    def test_quiet_applied_with_z_stamp_auto_closes(self):
        # No cluster this run, applied long ago with a Z stamp -> must auto-close.
        rows = [{
            "id": "fix-008", "signature": "format-target-overreach", "status": "applied",
            "applied_at": "2026-05-24T06:35:09Z", "proposed_at": "2026-05-23T00:00:00",
            "applied_ref": "#1138", "verified_closed_at": None,
            "last_recurrence_at": None, "recurrence_count": 0, "notes": "",
        }]
        now = datetime(2026, 6, 13, 12, 0, 0, tzinfo=timezone.utc)
        _rec, _still, closed, _fresh, _singles, mutations = cross_reference([], rows, now)
        self.assertEqual(len(closed), 1, "quiet applied fix must auto-close")
        self.assertTrue(any(m["op"] == "flip-to-closed" and m["id"] == "fix-008"
                            for m in mutations))


class TestGlHostStarvationSignature(unittest.TestCase):
    """gl-host-starvation must catch the macOS-pane starvation entries that
    previously fell through as singletons, and must win over queue-staleness
    for "queue ... GL-host-starved" phrasing (first match wins)."""

    def _sig(self, headline, body=""):
        e = Entry(ts="2026-07-14", role="worker-4", headline=headline,
                  body=[body] if body else [])
        return _mod.signature_for(e)

    def test_observed_starvation_headlines_cluster(self):
        for h in (
            "~06:24Z — opus macOS pane: zero macOS-viable opus work (GL-host churn, #1998)",
            "macOS opus pane idle: all opus tasks are GL-host or covered",
            "macOS opus pane: 0 pickable tasks (all opus candidates GL-host-blocked)",
            "opus queue GL-host-starved on this host",
        ):
            self.assertEqual(self._sig(h), "gl-host-starvation", h)

    def test_body_line_participates(self):
        # Headline alone lacks the starvation token; the first body line
        # supplies it (signature_for matches headline + first body line).
        self.assertEqual(
            self._sig("opus iteration (macOS/Metal) — GL-saturated queue",
                      body="every candidate skipped as GL-host-gated"),
            "gl-host-starvation")

    def test_plain_queue_staleness_still_routes_there(self):
        self.assertEqual(
            self._sig("queue starved: fleet:queued diverged, ingest never ran"),
            "queue-staleness")


if __name__ == "__main__":
    unittest.main()
