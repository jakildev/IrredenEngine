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
from unittest.mock import patch

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


class CampaignParticipation(unittest.TestCase):
    campaign = "fixture-render"

    def row(self, number, head, labels=(), merged="2026-01-01"):
        return {"number": number, "headRefName": head, "labels": list(labels),
                "mergedAt": merged}

    def test_membership_is_explicit_and_provider_independent(self):
        label = "fleet:campaign-fixture-render"
        for labels in ([label], [{"name": label}]):
            self.assertTrue(mod.campaign_member(
                self.row(1, "codex/surface", labels), self.campaign))
            self.assertTrue(mod.campaign_member(
                self.row(2, "human/surface", labels), self.campaign))
        self.assertTrue(mod.campaign_member(
            self.row(3, "claude/fixture-render-surface"), self.campaign))
        for labels in ([], ["fleet:campaign-fixture-render-other"], ["fleet:author-codex"]):
            self.assertFalse(mod.campaign_member(
                self.row(4, "codex/surface", labels), self.campaign))
        self.assertFalse(mod.campaign_member(
            self.row(5, "claude/fixture-rendering"), self.campaign))

    def test_both_merge_queries_are_unioned_and_deduplicated(self):
        legacy = self.row(1, "claude/fixture-render-one")
        shared = self.row(2, "codex/two", ["fleet:campaign-fixture-render"], "2026-01-02")
        unrelated = self.row(3, "codex/other")
        closed = self.row(4, "codex/closed", ["fleet:campaign-fixture-render"], None)
        closed["body"] = "Campaign fixture-render, slice D0.2"

        def fetch(args):
            self.assertIn("labels", args[args.index("--json") + 1])
            self.assertIn("body", args[args.index("--json") + 1])
            self.assertEqual(args[args.index("--state") + 1], "closed")
            selector = args[args.index("--search") + 1]
            if selector == "head:claude/fixture-render-":
                return [legacy]
            if selector == "label:fleet:campaign-fixture-render":
                return [shared, legacy, unrelated, closed]
            self.fail("unexpected GitHub query: %r" % args)

        with patch.object(mod, "gh_json", side_effect=fetch) as reader:
            rows, complete = mod.closed_campaign_prs("fixture/repo", self.campaign, None)
        self.assertEqual(reader.call_count, 2)
        self.assertTrue(complete)
        self.assertEqual([row["number"] for row in rows], [2, 1, 4])
        self.assertEqual(rows[-1]["body"], "Campaign fixture-render, slice D0.2")

    def test_failed_query_is_not_an_empty_history(self):
        legacy = self.row(1, "claude/fixture-render-one")
        with patch.object(mod, "gh_json", side_effect=[[legacy], None]):
            rows, complete = mod.closed_campaign_prs("fixture/repo", self.campaign, None)
        self.assertEqual(rows, [legacy])
        self.assertFalse(complete)
        self.assertEqual(mod.closed_campaign_prs("fixture/repo", self.campaign, {"merged": []}),
                         ([], True))
        self.assertEqual(mod.closed_campaign_prs("fixture/repo", self.campaign, {"merged": None}),
                         ([], False))

    def test_membership_does_not_hide_another_contributors_overlap(self):
        shared = self.row(2, "codex/two", ["fleet:campaign-fixture-render"])
        with patch.object(mod, "git_status", return_value=(0, "", "")), \
             patch.object(mod, "git", return_value="fixture-sha"), \
             patch.object(mod, "git_lines", return_value=["engine/surface.glsl"]):
            overlaps = mod.foreign_open_prs("fixture", [shared], "claude/fixture-render-",
                                           {"engine/surface.glsl"}, "origin/master")
        self.assertEqual([row["number"] for row in overlaps], [2])

    def test_contributor_history_cannot_evict_driver_surface(self):
        rows = [{"mergeCommit": {"oid": "participant-%d" % index}}
                for index in range(mod.SURFACE_PR_LIMIT)]
        rows.append({"mergeCommit": {"oid": "driver"}})

        def paths(_wt, *args):
            self.assertEqual(args[:3], ("show", "--name-only", "--format="))
            return ["engine/%s.glsl" % args[3]]

        with patch.object(mod, "git_lines", side_effect=paths):
            surface = mod.campaign_surface("fixture", rows, [])
        self.assertIn("engine/driver.glsl", surface)
        self.assertIn("engine/participant-0.glsl", surface)

    def test_participant_head_is_a_stacking_base(self):
        shared = self.row(2, "codex/two", ["fleet:campaign-fixture-render"])
        shared["headRefOid"] = "fixture-oid"
        with patch.object(mod, "git_status", return_value=(0, "", "")), \
             patch.object(mod, "git", return_value="0"):
            result = mod.nearest_stacked_pr("fixture", [shared], "claude/fixture-render-",
                                           {"codex/two"})
        self.assertEqual(result, 2)
        rows = mod.stacked_on_campaign(
            [{"pull_requests": [{"number": 2, "head": {"ref": "codex/two"}},
                                {"number": 3, "head": {"ref": "human/above"}}]}],
            [{"number": 4, "headRefName": "human/child", "baseRefName": "codex/two"}],
            "claude/fixture-render-", {"codex/two"})
        self.assertEqual([row["number"] for row in rows], [3, 4])


if __name__ == "__main__":
    unittest.main(verbosity=2)
