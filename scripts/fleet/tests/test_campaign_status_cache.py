#!/usr/bin/env python3
"""fleet-campaign-status's scout-cache freshness read.

The shared-state contract (scripts/fleet/CLAUDE.md §Shared-state contracts) is
that a state.json snapshot's age comes from the in-file `generated_at`, never
from file mtime: a follower rewrites the file every tick while preserving the
leader's `generated_at`, so a dead leader keeps mtime fresh over a frozen
snapshot. `lint_state_mtime.py` cannot enforce it here — its pattern is
`.st_mtime`, and `os.path.getmtime` slips past.

It matters for this tool specifically: a snapshot judged fresh but stale can
omit a campaign PR opened after it froze, which drops the branch out of the
`live` arm and into classification, where `--apply` becomes reachable.

The subject is extension-less, so it loads through SourceFileLoader.
"""
import importlib.util
import json
import os
import sys
import tempfile
import time
import unittest
from importlib.machinery import SourceFileLoader
from pathlib import Path

SUBJECT = Path(__file__).resolve().parent.parent / "fleet-campaign-status"

if not SUBJECT.exists():
    print("SKIP: %s not found" % SUBJECT, file=sys.stderr)
    raise SystemExit(3)

_loader = SourceFileLoader("fleet_campaign_status", str(SUBJECT))
_spec = importlib.util.spec_from_loader(_loader.name, _loader)
mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(mod)


class StateCacheFreshness(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "state.json")
        self._saved = mod.STATE_JSON
        mod.STATE_JSON = self.path

    def tearDown(self):
        mod.STATE_JSON = self._saved
        self.tmp.cleanup()

    def write(self, generated_at):
        payload = {"repos": {"engine": {"prs": []}}}
        if generated_at is not None:
            payload["generated_at"] = generated_at
        Path(self.path).write_text(json.dumps(payload))
        # Freshly written, so mtime is now: every case below has a fresh mtime
        # and differs only in generated_at.

    def iso(self, ago_seconds):
        return time.strftime("%Y-%m-%dT%H:%M:%SZ",
                             time.gmtime(time.time() - ago_seconds))

    def test_frozen_generated_at_is_stale_despite_a_fresh_mtime(self):
        self.write(self.iso(mod.CACHE_MAX_AGE_S * 4))
        _, fresh = mod.load_state_cache()
        self.assertFalse(
            fresh,
            "a snapshot frozen well past the cache window read as fresh — the "
            "mtime was consulted instead of generated_at",
        )

    def test_recent_generated_at_is_fresh(self):
        self.write(self.iso(1))
        _, fresh = mod.load_state_cache()
        self.assertTrue(fresh)

    def test_missing_generated_at_falls_back_to_mtime(self):
        # The documented fallback for a malformed snapshot: mtime is now, so it
        # reads fresh rather than raising.
        self.write(None)
        data, fresh = mod.load_state_cache()
        self.assertIsNotNone(data)
        self.assertTrue(fresh)

    def test_absent_cache_is_not_fresh(self):
        _, fresh = mod.load_state_cache()
        self.assertFalse(fresh)


class OpenPrReadability(unittest.TestCase):
    """An unread PR list and an empty one are different facts — the first hides
    the `live` verdict, so the caller must be able to tell them apart."""

    def test_empty_list_is_readable(self):
        rows, readable = mod.open_prs("owner/repo", "engine", {"open": [], "merged": []})
        self.assertEqual(rows, [])
        self.assertTrue(readable)

    def test_null_list_is_not_readable(self):
        rows, readable = mod.open_prs("owner/repo", "engine", {"open": None, "merged": []})
        self.assertEqual(rows, [])
        self.assertFalse(readable)


if __name__ == "__main__":
    unittest.main(verbosity=2)
