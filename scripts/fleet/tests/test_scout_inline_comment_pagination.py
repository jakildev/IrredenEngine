"""Regression test for the PR #2998 review nit: fetch_pr_inline_comments hit
`gh api repos/<slug>/pulls/<N>/comments` with no pagination, so REST's default
30-item window silently truncated the cached inlineComments list on any PR past
30 inline review comments — and `fleet-pr` served that short list as complete.

Same truncation class as #2856 (fetch_human_approved's per_page=30), one
surface over. The fix is per_page=100 + --paginate, mirrored in fleet-pr's
fetch_live_pr; tests/test_fleet_cache_reader_freshness.sh covers the live half.

The run_capture stub below emulates the endpoint's paging semantics rather than
special-casing the fixed call site: it honours per_page/page from the request
and serves a second page ONLY when --paginate is present, so the pre-fix call
(no flag, default window) requests exactly one 30-item page and never sees the
items this test plants past it.

Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet.
"""
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

fetch_pr_inline_comments = _mod.fetch_pr_inline_comments

_REPO = "jakildev/IrredenEngine"
_PR = 2998

# Sized past a single 100-item page so the test exercises real multi-page
# paging, not merely a raised per_page.
_POPULATION = 130
# REST serves inline comments oldest-first, so the last-planted item is the one
# a truncated window drops.
_NEWEST = _POPULATION

_REST_DEFAULT_PER_PAGE = 30


def _inline_comment(index):
    return {
        "path": f"engine/render/src/file{index}.cpp",
        "line": index,
        "user": {"login": "jakildev"},
        "body": f"INLINE_ITEM_{index}",
    }


def _stub_run_capture(cmd, **_kwargs):
    assert cmd[:2] == ["gh", "api"], cmd
    url = cmd[2]
    paginate = "--paginate" in cmd

    query = url.split("?", 1)[1] if "?" in url else ""
    params = dict(
        pair.split("=", 1) for pair in query.split("&") if "=" in pair
    )
    per_page = int(params.get("per_page", _REST_DEFAULT_PER_PAGE))

    # `gh --paginate` merges JSON array pages into one array; without it the
    # caller sees page 1 alone.
    all_numbers = list(range(1, _POPULATION + 1))
    window = all_numbers if paginate else all_numbers[:per_page]
    return json.dumps([_inline_comment(n) for n in window])


class TestInlineCommentPagination(unittest.TestCase):

    def setUp(self):
        patcher = patch.object(_mod, "run_capture", side_effect=_stub_run_capture)
        self.run_capture = patcher.start()
        self.addCleanup(patcher.stop)

    def test_item_past_the_default_window_survives(self):
        out = fetch_pr_inline_comments(_REPO, _PR)
        bodies = {c["body"] for c in out}
        self.assertIn(
            f"INLINE_ITEM_{_NEWEST}", bodies,
            f"inline comment #{_NEWEST} (of {_POPULATION}) must survive "
            "pagination, not just the first 30-item REST window",
        )
        self.assertEqual(len(out), _POPULATION)

    def test_request_carries_paginate_and_a_full_per_page(self):
        fetch_pr_inline_comments(_REPO, _PR)
        cmd = self.run_capture.call_args[0][0]
        self.assertIn("--paginate", cmd)
        url = cmd[2]
        self.assertRegex(url, r"[?&]per_page=100\b")
        self.assertIn(f"repos/{_REPO}/pulls/{_PR}/comments", url)

    def test_failed_fetch_still_degrades_to_empty(self):
        with patch.object(_mod, "run_capture", return_value=None):
            self.assertEqual(fetch_pr_inline_comments(_REPO, _PR), [])

    def test_bad_json_still_degrades_to_empty(self):
        with patch.object(_mod, "run_capture", return_value="{not json"):
            with patch.object(_mod, "log"):
                self.assertEqual(fetch_pr_inline_comments(_REPO, _PR), [])

    def test_stub_models_the_pre_fix_call_as_truncating(self):
        # Fidelity guard (scripts/fleet/CLAUDE.md): if a future stub rewrite
        # stops modelling the default window, the assertions above go vacuous
        # — they would pass against the unpaginated call they exist to reject.
        pre_fix = _stub_run_capture(
            ["gh", "api", f"repos/{_REPO}/pulls/{_PR}/comments"]
        )
        self.assertEqual(len(json.loads(pre_fix)), _REST_DEFAULT_PER_PAGE)
        self.assertNotIn(f"INLINE_ITEM_{_NEWEST}", pre_fix)


if __name__ == "__main__":
    unittest.main()
