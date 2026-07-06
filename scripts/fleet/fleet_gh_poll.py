"""Conditional GitHub REST polling with per-endpoint ETag caching.

The fleet's steady-state quota drain is the scout's per-tick list fetches
(#1394). Each `gh pr/issue list` is an unconditional GraphQL query, so an
idle 30 s tick that changes nothing still spends ~8 GraphQL points per repo.
This module is the fix: `conditional_get` sends `If-None-Match` with a cached
ETag, and GitHub answers a `304 Not Modified` — which costs **zero** rate-limit
quota — whenever the endpoint hasn't changed. A quiescent tick converges to all
304s and spends nothing from either the `core` (REST) or `graphql` pool.

It is a general, single-host-agnostic helper on purpose: `conditional_get`
takes an injectable `cache_dir` and no hardcoded `~/.fleet`, so the Q2
centralized-poller (#1394 phase 2) reuses it verbatim. The `fleet-gh-poll` CLI
wrapper is the subprocess seam for callers that aren't already Python.

stdlib-only (`urllib.request`) — no `curl`/`requests` dependency, so it runs
in-process inside the already-Python scout and on native-Windows/MSYS2 alike.

Design invariants:
  * **etag+body stored together, atomically.** One JSON file per endpoint holds
    `{etag, body, fetched_at}`. A 304 carries no body, so serving the cached
    body depends on never observing an etag without its body — a single-file
    atomic write (temp + os.replace) guarantees that.
  * **an error is never "unchanged".** Network/5xx/parse failures return
    `(True, None)` so a caller keeps its own last-known-good fallback rather
    than mistaking a poll failure for a genuine 304.

Source of truth: scripts/fleet/fleet_gh_poll.py in the engine repo.
Co-located with fleet-state-scout so the scout's `sys.path.insert(script dir)`
import resolves (same proven path as fleet_blocked_by.py).
"""
import hashlib
import json
import os
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

API_ROOT = "https://api.github.com"
DEFAULT_ACCEPT = "application/vnd.github+json"
API_VERSION = "2022-11-28"
USER_AGENT = "fleet-gh-poll"
DEFAULT_TIMEOUT_SECONDS = 30

# Default per-endpoint ETag cache root. Sits alongside the scout's existing
# prs/ diffs/ issues/ layout under STATE_DIR. Callers that must stay
# single-host-agnostic (Q2) pass their own cache_dir instead of relying on
# this ~/.fleet default.
DEFAULT_CACHE_DIR = Path.home() / ".fleet" / "state" / "etag"


# Auth token is fetched from `gh` once and memoized — a long-running scout
# calls conditional_get many times per tick, and re-shelling `gh auth token`
# each call is pure overhead. Cleared on a 401 so a rotated/expired token is
# picked up on the next request (the daemon-token-staleness gotcha). The lock
# serializes the check-then-shell so the several collect_state() worker threads
# that race past the memo check on a cold start shell `gh auth token` once
# between them, not once per thread.
_token_cache = {"value": None}
_token_lock = threading.Lock()


def auth_token(refresh=False):
    """Return the gh auth token (memoized). refresh=True re-shells gh first."""
    # Fast path: an already-memoized token needs no lock (dict read is atomic
    # under the GIL). Only a cold-start / refresh miss takes the lock and
    # double-checks, so concurrent threads collapse onto a single shell-out.
    if not refresh and _token_cache["value"]:
        return _token_cache["value"]
    with _token_lock:
        if refresh:
            _token_cache["value"] = None
        if _token_cache["value"]:
            return _token_cache["value"]
        try:
            proc = subprocess.run(
                ["gh", "auth", "token"],
                capture_output=True, text=True, timeout=DEFAULT_TIMEOUT_SECONDS,
            )
        except (OSError, subprocess.TimeoutExpired):
            return None
        if proc.returncode != 0:
            return None
        _token_cache["value"] = proc.stdout.strip() or None
        return _token_cache["value"]


def _cache_key(url):
    # The full URL already encodes method (always GET here), path, and sorted
    # params, so sha1 of the URL is a stable, collision-free cache identity.
    return hashlib.sha1(url.encode("utf-8")).hexdigest()


def _safe_slug(repo_slug):
    # `owner/repo` -> filesystem-safe subdir so each repo's endpoint caches
    # live in their own directory (mirrors the scout's per-repo cache layout).
    return repo_slug.replace("/", "_")


def _cache_path(cache_dir, repo_slug, url):
    return Path(cache_dir) / _safe_slug(repo_slug) / f"{_cache_key(url)}.json"


def _read_cache(path):
    try:
        entry = json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return None, None
    # etag+body are written together; a partial entry (etag without body) can't
    # arise from the atomic writer, but guard anyway — a stale etag with no body
    # must not let a 304 serve nothing.
    etag = entry.get("etag")
    body = entry.get("body")
    if etag is None or body is None:
        return None, None
    return etag, body


def _write_cache(path, etag, body):
    # Atomic: unique temp in the same directory + os.replace, so a concurrent
    # reader sees either the complete prior entry or the complete new one, never
    # a torn splice (mirrors fleet-state-scout.write_atomic). etag and body land
    # in one file so a 304 can never find an etag whose body was half-written.
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps({
        "etag": etag,
        "body": body,
        "fetched_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    })
    fd, tmp = tempfile.mkstemp(
        dir=str(path.parent), prefix="." + path.name + ".", suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass
        raise


def build_url(repo_slug, path, params=None):
    """Compose the api.github.com URL for a repo-relative REST path.

    Params are sorted so the same logical request always yields the same URL
    (hence the same cache key) regardless of dict ordering.
    """
    base = f"{API_ROOT}/repos/{repo_slug}/{path.lstrip('/')}"
    if params:
        query = urllib.parse.urlencode(sorted(params.items()))
        return f"{base}?{query}"
    return base


def conditional_get(repo_slug, path, *, params=None, accept=None,
                    cache_dir=None, token=None, timeout=DEFAULT_TIMEOUT_SECONDS):
    """Conditional GET against the GitHub REST API with per-endpoint ETag cache.

    Returns (changed, body):
      * (True, body_str)  — 200: fresh body, cached together with its ETag.
      * (False, body_str) — 304: unchanged; the cached body is served.
      * (True, None)      — network / HTTP error / no-token: caller keeps its
                            own last-known-good fallback. An error is never
                            reported as "unchanged".

    `cache_dir` defaults to DEFAULT_CACHE_DIR but is injectable so the helper
    carries zero single-host assumptions (Q2 reuse).
    """
    if cache_dir is None:
        cache_dir = DEFAULT_CACHE_DIR
    url = build_url(repo_slug, path, params)
    path_obj = _cache_path(cache_dir, repo_slug, url)
    cached_etag, cached_body = _read_cache(path_obj)

    return _request(url, path_obj, cached_etag, cached_body,
                    accept or DEFAULT_ACCEPT, token, timeout, allow_reauth=True)


def _request(url, cache_path, cached_etag, cached_body, accept, token,
             timeout, allow_reauth):
    tok = token or auth_token()
    if not tok:
        return (True, None)

    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", f"Bearer {tok}")
    req.add_header("Accept", accept)
    req.add_header("X-GitHub-Api-Version", API_VERSION)
    req.add_header("User-Agent", USER_AGENT)
    if cached_etag:
        # Weak validators (W/"…") are fine — GitHub compares them by equality
        # and we pass the value back verbatim.
        req.add_header("If-None-Match", cached_etag)

    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8")
            etag = resp.headers.get("ETag")
            if etag:
                _write_cache(cache_path, etag, body)
            return (True, body)
    except urllib.error.HTTPError as e:
        # HTTPError is itself a response object; close it so the connection
        # isn't leaked (and no ResourceWarning at gc). We only need the status.
        code = e.code
        e.close()
        if code == 304:
            # Unchanged. Serve the cached body; if the cache is somehow
            # unreadable, force the caller onto its fallback rather than
            # returning an empty "unchanged".
            if cached_body is not None:
                return (False, cached_body)
            return (True, None)
        if code == 401 and allow_reauth and token is None:
            # Memoized token may be stale on a long-running poller — re-shell
            # gh once and retry. (When an explicit token was passed, the caller
            # owns rotation, so don't second-guess it.)
            fresh = auth_token(refresh=True)
            if fresh:
                return _request(url, cache_path, cached_etag, cached_body,
                                accept, fresh, timeout, allow_reauth=False)
        return (True, None)
    except (urllib.error.URLError, TimeoutError, OSError):
        return (True, None)


def conditional_get_url(url, *, etag=None, timeout=DEFAULT_TIMEOUT_SECONDS,
                        headers=None):
    """Plain If-None-Match GET against an arbitrary URL (no GitHub auth).

    The Q2 centralized poller (#1394 phase 2) uses this for the intra-fleet LAN
    hop: a follower conditional-GETs the leader's served state bundle, passing
    the last-seen `generated_at` as the ETag. It reuses conditional_get's
    machinery — If-None-Match, "an error is never 304" — but drops the two
    GitHub-specific pieces (`gh auth token`, the api.github.com URL builder) and
    the on-disk etag cache: the follower already tracks the last `generated_at`
    in its own state.json, so a second cache would just duplicate it.

    Returns (status, body, new_etag):
      * (200, body_str, etag) — changed: fresh body + its response ETag.
      * (304, None, sent_etag) — unchanged: caller keeps its cached copy (the
                                 ETag it sent is echoed back for convenience).
      * (None, None, None)     — network / HTTP error / unreachable: caller
                                 degrades (self-poll). An error is never a 304.
    """
    req = urllib.request.Request(url, method="GET")
    req.add_header("User-Agent", USER_AGENT)
    if headers:
        for key, value in headers.items():
            req.add_header(key, value)
    if etag:
        req.add_header("If-None-Match", etag)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8")
            return (resp.status, body, resp.headers.get("ETag"))
    except urllib.error.HTTPError as e:
        code = e.code
        e.close()
        if code == 304:
            return (304, None, etag)
        return (None, None, None)
    except (urllib.error.URLError, TimeoutError, OSError):
        return (None, None, None)
