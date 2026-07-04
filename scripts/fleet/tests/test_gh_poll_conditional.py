"""Tests for fleet_gh_poll.conditional_get — the #1394 Q1 conditional-REST core.

Covers the four contract paths: 200 (fresh + cached), 304 (unchanged, cached
body served + If-None-Match sent), stale-etag-without-body (refetch, no
conditional header), and network error (True, None so the caller keeps its
fallback). urllib is fully mocked — no real network.
"""
import importlib.machinery
import importlib.util
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import MagicMock, patch

_SCRIPT = Path(__file__).parent.parent / "fleet_gh_poll.py"
_loader = importlib.machinery.SourceFileLoader("fleet_gh_poll", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_gh_poll", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

conditional_get = _mod.conditional_get

_SLUG = "jakildev/IrredenEngine"


def _resp(body_bytes, etag):
    """A MagicMock standing in for a urlopen() context manager (200)."""
    resp = MagicMock()
    resp.__enter__.return_value = resp
    resp.__exit__.return_value = False
    resp.read.return_value = body_bytes
    resp.headers.get.return_value = etag
    return resp


class TestConditionalGet(unittest.TestCase):

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.cache_dir = Path(self._tmp.name)
        # Patch out the real gh auth-token shell-out for every test.
        self._token_patch = patch.object(_mod, "auth_token", return_value="tok")
        self._token_patch.start()

    def tearDown(self):
        self._token_patch.stop()
        self._tmp.cleanup()

    def _get(self, **kw):
        return conditional_get(_SLUG, "issues", params={"state": "open"},
                               cache_dir=self.cache_dir, **kw)

    def test_200_returns_fresh_and_caches(self):
        with patch("urllib.request.urlopen", return_value=_resp(b'[{"n":1}]', 'W/"e1"')):
            changed, body = self._get()
        self.assertTrue(changed)
        self.assertEqual(body, '[{"n":1}]')
        # The etag+body landed in one cache file.
        files = list(self.cache_dir.rglob("*.json"))
        self.assertEqual(len(files), 1)
        entry = json.loads(files[0].read_text())
        self.assertEqual(entry["etag"], 'W/"e1"')
        self.assertEqual(entry["body"], '[{"n":1}]')

    def test_304_serves_cached_body_and_sends_if_none_match(self):
        # First call (200) primes the cache.
        with patch("urllib.request.urlopen", return_value=_resp(b'[{"n":1}]', 'W/"e1"')):
            self._get()

        captured = {}

        def _raise_304(req, timeout=None):
            captured["inm"] = req.get_header("If-none-match")
            raise urllib.error.HTTPError(
                "http://x", 304, "Not Modified", {}, None)

        with patch("urllib.request.urlopen", side_effect=_raise_304):
            changed, body = self._get()

        self.assertFalse(changed)                 # unchanged
        self.assertEqual(body, '[{"n":1}]')       # cached body served
        self.assertEqual(captured["inm"], 'W/"e1"')  # conditional header sent

    def test_stale_etag_without_body_refetches_unconditionally(self):
        # A cache entry that somehow has an etag but no body must NOT drive a
        # 304 (which would serve nothing); it's treated as absent → full fetch.
        url = _mod.build_url(_SLUG, "issues", {"state": "open"})
        path = _mod._cache_path(self.cache_dir, _SLUG, url)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({"etag": 'W/"stale"', "body": None}))

        captured = {}

        def _capture(req, timeout=None):
            captured["inm"] = req.get_header("If-none-match")
            return _resp(b'[{"n":2}]', 'W/"e2"')

        with patch("urllib.request.urlopen", side_effect=_capture):
            changed, body = self._get()

        self.assertTrue(changed)
        self.assertEqual(body, '[{"n":2}]')
        self.assertIsNone(captured["inm"])        # no conditional header sent

    def test_network_error_returns_true_none(self):
        with patch("urllib.request.urlopen",
                   side_effect=urllib.error.URLError("boom")):
            changed, body = self._get()
        self.assertTrue(changed)                  # an error is never "unchanged"
        self.assertIsNone(body)

    def test_no_token_returns_true_none(self):
        with patch.object(_mod, "auth_token", return_value=None), \
             patch("urllib.request.urlopen") as urlopen:
            changed, body = self._get()
        self.assertTrue(changed)
        self.assertIsNone(body)
        urlopen.assert_not_called()               # no request without a token

    def test_304_with_missing_cache_forces_fallback(self):
        # A 304 with no cached body to serve (cache wiped between requests) must
        # return (True, None) so the caller degrades rather than serving empty.
        def _raise_304(req, timeout=None):
            raise urllib.error.HTTPError(
                "http://x", 304, "Not Modified", {}, None)
        with patch("urllib.request.urlopen", side_effect=_raise_304):
            changed, body = self._get()
        self.assertTrue(changed)
        self.assertIsNone(body)


if __name__ == "__main__":
    unittest.main()
