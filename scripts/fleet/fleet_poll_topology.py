"""Centralized cross-device polling topology (#1394 Q2) — leader / follower.

The fleet's steady-state GitHub quota drain is the scout's per-tick list
fetches, multiplied by N hosts (#1394). Q1 (`fleet_gh_poll`) made each host's
own polling conditional; Q2 collapses the N hosts into **one authoritative
poller** (the *leader*) plus N-1 *followers* that consume the leader's
already-fetched `~/.fleet/state/` over the LAN instead of calling `gh`
themselves. Idle-fleet GitHub traffic drops from N× to 1×.

Mechanism — follower-pull over a leader-served HTTP endpoint:
  * The leader polls GitHub exactly as before and additionally serves a
    read-only bundle of the shareable state artifacts (state.json + the
    prs/ diffs/ issues/ detail caches) with the snapshot's `generated_at`
    as the ETag.
  * A follower conditional-GETs that endpoint (reusing Q1's If-None-Match
    machinery via `fleet_gh_poll.conditional_get_url`), unpacks the bundle,
    and re-derives only the host-local fields (`clone_freshness`,
    `repos.<key>.path`) — **preserving the leader's `generated_at` verbatim**
    so a dead leader's frozen timestamp trips every consumer's existing
    staleness guard.
  * A follower that can't reach the leader (off-LAN, or the leader is dead or
    wedged with a stale `generated_at`) transparently self-polls GitHub for
    that tick — a temporary N=1 solo poller, exactly the pre-Q2 behavior.

Design invariants:
  * **Only GitHub-derived data is shareable.** The bundle carries the exact
    reads that multiply per host; host-local artifacts (clone path,
    clone_freshness, seen-hashes/, triggers/) are recomputed on the follower,
    never copied.
  * **Never restamp `generated_at` on a follower.** It is the liveness signal;
    the follower copies the leader's value so a frozen leader is detectable.
  * stdlib-only (`http.server`, `urllib`, optional `tomllib`) — no new
    dependency, runs in-process in the already-Python scout on mac / WSL2 /
    native-Windows alike.

Source of truth: scripts/fleet/fleet_poll_topology.py in the engine repo.
Co-located with fleet-state-scout so the scout's `sys.path.insert(script dir)`
import resolves (same proven path as fleet_gh_poll.py / fleet_blocked_by.py).
"""
import calendar
import json
import os
import socketserver
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from fleet_gh_poll import conditional_get_url

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11 (defensive; the fleet runs 3.11+)
    tomllib = None

# Static config surface, shared with fleet-claim's host identity + setup-windows.
HOST_TOML = Path.home() / ".config" / "irreden" / "host.toml"

DEFAULT_POLL_PORT = 8477
# A follower whose consumed `generated_at` ages past this self-polls GitHub for
# that tick. Kept above the scout's 30 s interval and below the 120 s dispatcher
# watchdog so a wedged (alive-but-not-ticking) leader is caught before the
# dispatcher declares the whole scout dead.
DEFAULT_LEADER_STALE_SECONDS = 90
DEFAULT_BIND_HOST = "0.0.0.0"  # LAN-reachable; the endpoint is read-only state
# The LAN hop is sub-second when the leader is up; a short timeout means an
# unreachable leader (laptop off-LAN — no route, so the connect TIMES OUT rather
# than refusing) fails fast into a self-poll instead of stalling the 30 s tick.
LEADER_FETCH_TIMEOUT_SECONDS = 5

# Shareable `~/.fleet/state/` artifacts the leader serves and the follower
# mirrors. state.json rides separately (the follower overlays host-local fields
# onto it). Projections are NOT bundled: they are a deterministic, host-agnostic
# function of state.json, so the follower recomputes them via its own slicer
# pass (which it must run anyway to fire its host-local triggers) rather than
# shipping redundant copies. seen-hashes/ triggers/ scout.pid etag/ are
# host-local and never shared.
STATE_FILE_NAME = "state.json"
SHAREABLE_SUBDIRS = ("prs", "diffs", "issues")


# --- Poll config (host.toml [fleet] section) -------------------------------


class PollConfig:
    def __init__(self, role, port, leader_host, leader_stale_seconds,
                 bind_host=DEFAULT_BIND_HOST):
        self.role = role
        self.port = port
        self.leader_host = leader_host
        self.leader_stale_seconds = leader_stale_seconds
        self.bind_host = bind_host

    @property
    def is_leader(self):
        return self.role == "leader"

    @property
    def is_follower(self):
        return self.role == "follower"

    def describe(self):
        if self.is_leader:
            return f"leader (serving :{self.port})"
        return (f"follower (leader={self.leader_host or 'UNSET'}:{self.port}, "
                f"stale>{self.leader_stale_seconds}s -> self-poll)")


def _coerce(raw):
    raw = raw.strip()
    if len(raw) >= 2 and raw[0] == raw[-1] and raw[0] in "\"'":
        return raw[1:-1]
    if raw.lower() in ("true", "false"):
        return raw.lower() == "true"
    try:
        return int(raw)
    except ValueError:
        return raw


def _minimal_toml(text):
    # Fallback for interpreters without tomllib (< 3.11). host.toml is flat
    # `[section]` + `key = value`, so a line parser suffices — no arrays/nested
    # tables. Naive `#`-comment stripping is fine here (values are bare words /
    # numbers / simple quoted strings, never a `#` inside a string).
    out, section = {}, None
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
            out.setdefault(section, {})
            continue
        if section is None or "=" not in line:
            continue
        key, value = line.split("=", 1)
        out[section][key.strip()] = _coerce(value)
    return out


def _read_toml(path):
    try:
        raw = Path(path).read_bytes()
    except (FileNotFoundError, OSError):
        return {}
    text = raw.decode("utf-8", "replace")
    if tomllib is not None:
        try:
            return tomllib.loads(text)
        except (tomllib.TOMLDecodeError, ValueError):
            return {}
    return _minimal_toml(text)


def _as_int(value, default):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def read_poll_config(config_path=None):
    """Parse the `[fleet]` section of host.toml into a PollConfig.

    Missing file / missing section / unknown role all resolve to the safe
    default: `leader`. A single-host box with no host.toml (the common case,
    and every pre-Q2 install) is therefore an unchanged solo poller.
    """
    data = _read_toml(config_path or HOST_TOML)
    fleet = data.get("fleet", {}) if isinstance(data, dict) else {}
    if not isinstance(fleet, dict):
        fleet = {}
    role = str(fleet.get("poll_role", "leader")).strip().lower()
    if role not in ("leader", "follower"):
        role = "leader"
    return PollConfig(
        role=role,
        port=_as_int(fleet.get("poll_port"), DEFAULT_POLL_PORT),
        leader_host=(fleet.get("leader_host") or None),
        leader_stale_seconds=_as_int(
            fleet.get("leader_stale_seconds"), DEFAULT_LEADER_STALE_SECONDS),
    )


# --- generated_at staleness ------------------------------------------------


def parse_generated_at(ts):
    """Parse a scout `YYYY-MM-DDTHH:MM:SSZ` UTC stamp to epoch seconds.

    None on missing/malformed input (callers treat None as "stale").
    """
    if not ts or not isinstance(ts, str):
        return None
    try:
        return calendar.timegm(time.strptime(ts, "%Y-%m-%dT%H:%M:%SZ"))
    except (ValueError, TypeError):
        return None


def generated_at_of(state_text):
    try:
        return json.loads(state_text).get("generated_at")
    except (json.JSONDecodeError, AttributeError, TypeError):
        return None


def is_stale(generated_at, now_epoch, window_seconds):
    """True if `generated_at` is missing/unparseable or older than the window."""
    epoch = parse_generated_at(generated_at)
    if epoch is None:
        return True
    return (now_epoch - epoch) > window_seconds


def now_epoch():
    return calendar.timegm(time.gmtime())


# --- Bundle build / parse / apply ------------------------------------------


def _quote_etag(generated_at):
    # A strong validator: the follower echoes it back verbatim in
    # If-None-Match, and we compare raw strings on the leader, so the exact
    # quoting only has to round-trip — not satisfy a strict cache.
    return f'"{generated_at}"'


def build_bundle(state_dir):
    """Read the shareable state artifacts off disk into a serialized bundle.

    Returns (etag, payload_bytes). etag is the state.json `generated_at`
    (quoted); payload is `{"generated_at", "files": {relpath: text}}` JSON.
    Returns (None, b"") when state.json is missing/unparseable (the leader
    hasn't completed a tick yet) so the server answers 503 rather than lying.
    """
    state_dir = Path(state_dir)
    try:
        state_text = (state_dir / STATE_FILE_NAME).read_text()
    except (FileNotFoundError, OSError):
        return (None, b"")
    generated_at = generated_at_of(state_text)
    if not generated_at:
        return (None, b"")

    files = {STATE_FILE_NAME: state_text}
    for sub in SHAREABLE_SUBDIRS:
        root = state_dir / sub
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            try:
                files[path.relative_to(state_dir).as_posix()] = path.read_text()
            except (OSError, UnicodeDecodeError):
                continue
    payload = json.dumps(
        {"generated_at": generated_at, "files": files}).encode("utf-8")
    return (_quote_etag(generated_at), payload)


def parse_bundle(payload):
    """Parse a served bundle body into {"generated_at", "files"}.

    Raises ValueError / json.JSONDecodeError on a malformed payload so the
    caller degrades to self-poll rather than unpacking garbage.
    """
    text = payload if isinstance(payload, str) else payload.decode("utf-8")
    data = json.loads(text)
    if not isinstance(data, dict) or not isinstance(data.get("files"), dict):
        raise ValueError("bundle missing a 'files' object")
    return {"generated_at": data.get("generated_at"), "files": data["files"]}


def _atomic_write_text(path, text):
    # Unique-temp + os.replace, mirroring fleet-state-scout.write_atomic. Kept
    # inline (not imported from the scout) so this module stays standalone and
    # unit-testable without loading the 2.6k-line scout.
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(
        dir=str(path.parent), prefix="." + path.name + ".", suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass
        raise


def apply_bundle_detail_caches(state_dir, files):
    """Sync the prs/ diffs/ issues/ detail caches from a bundle to disk.

    Writes every bundled detail file and prunes local files the bundle no
    longer carries, so a follower's caches stay byte-identical to the leader's
    (mirroring the leader's own GC). state.json and projections are handled by
    the caller's tick tail, not here.
    """
    state_dir = Path(state_dir)
    wanted = {sub: set() for sub in SHAREABLE_SUBDIRS}
    for rel, content in files.items():
        top = rel.split("/", 1)[0]
        if top in wanted:
            wanted[top].add(rel)
            _atomic_write_text(state_dir / rel, content)
    for sub in SHAREABLE_SUBDIRS:
        root = state_dir / sub
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.is_file() and \
                    path.relative_to(state_dir).as_posix() not in wanted[sub]:
                path.unlink(missing_ok=True)


def overlay_host_local(leader_state, *, engine_path, game_path, clone_freshness):
    """Return the leader's resolved state with host-local fields swapped for
    THIS host's. `generated_at` and every GitHub-derived field are preserved
    verbatim (the follower must not restamp); only `repos.<key>.path` and
    `clone_freshness` are re-derived locally (the leader's are wrong here).
    """
    state = dict(leader_state)
    repos = {key: dict(val) for key, val in state.get("repos", {}).items()}
    if "engine" in repos:
        repos["engine"]["path"] = str(engine_path)
    if "game" in repos and game_path is not None:
        repos["game"]["path"] = str(game_path)
    state["repos"] = repos
    state["clone_freshness"] = clone_freshness
    return state


# --- Leader HTTP server -----------------------------------------------------


class _BundleServer(ThreadingHTTPServer):
    daemon_threads = True  # worker threads never block shutdown()
    allow_reuse_address = True  # re-bind fast across scout restarts

    def server_bind(self):
        # Skip HTTPServer.server_bind's socket.getfqdn() reverse-DNS lookup: on
        # a host whose resolver has no PTR for the bind address (observed
        # binding 0.0.0.0 on macOS) it blocks ~30s, which would stall the whole
        # scout startup. We never use server_name, so bind via the grandparent
        # (TCPServer) and stamp the fields directly.
        socketserver.TCPServer.server_bind(self)
        host, port = self.server_address[:2]
        self.server_name = host
        self.server_port = port

    def __init__(self, addr, handler):
        super().__init__(addr, handler)
        self._lock = threading.Lock()
        self._etag = None
        self._payload = b""

    def set_bundle(self, etag, payload):
        with self._lock:
            self._etag, self._payload = etag, payload

    def get_bundle(self):
        with self._lock:
            return self._etag, self._payload


class _BundleHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        etag, payload = self.server.get_bundle()
        if etag is None:
            self.send_response(503)  # leader hasn't produced a snapshot yet
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.headers.get("If-None-Match") == etag:
            self.send_response(304)
            self.send_header("ETag", etag)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("ETag", etag)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_HEAD(self):
        # Cheap liveness probe (curl -I): headers only, same ETag semantics.
        etag, payload = self.server.get_bundle()
        code = 503 if etag is None else 200
        self.send_response(code)
        if etag is not None:
            self.send_header("ETag", etag)
            self.send_header("Content-Length", str(len(payload)))
        else:
            self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *_args):
        # Silence per-request stderr spam; the scout logs its own tick lines.
        return


class LeaderServer:
    """Daemon-thread HTTP server that serves the current state bundle.

    `update_bundle` is called by the leader at the end of each tick with the
    freshly-built snapshot; the handler serves whatever the latest update set,
    so a mid-tick request always gets the last *complete* snapshot.
    """

    def __init__(self, port, bind_host=DEFAULT_BIND_HOST):
        self._httpd = _BundleServer((bind_host, port), _BundleHandler)
        self._thread = None

    def update_bundle(self, etag, payload):
        self._httpd.set_bundle(etag, payload)

    def start(self):
        self._thread = threading.Thread(
            target=self._httpd.serve_forever,
            name="fleet-leader-http", daemon=True)
        self._thread.start()

    def stop(self):
        try:
            self._httpd.shutdown()
        except Exception:
            pass
        try:
            self._httpd.server_close()
        except Exception:
            pass


def start_leader_server(cfg, state_dir):
    """Build the initial bundle (if a snapshot already exists) and start the
    server. Returns the LeaderServer, or None if the port can't be bound (the
    leader still polls + writes locally; followers just can't reach it and
    self-poll — a safe degradation, never a scout crash).
    """
    try:
        server = LeaderServer(cfg.port, cfg.bind_host)
    except OSError:
        return None
    etag, payload = build_bundle(state_dir)
    if etag is not None:
        server.update_bundle(etag, payload)
    server.start()
    return server


# --- Follower fetch ---------------------------------------------------------


def fetch_from_leader(cfg, prev_etag, *, getter=None):
    """Conditional-GET the leader's bundle over the LAN.

    `getter` is injectable for tests (defaults to conditional_get_url).
    Returns (status, etag, files):
      * (200, new_etag, files_dict) — fresh bundle.
      * (304, prev_etag, None)      — unchanged; caller reuses its on-disk copy.
      * (None, prev_etag, None)     — unreachable / error / no leader_host /
                                      malformed bundle: caller self-polls.
    """
    if not cfg.leader_host:
        return (None, prev_etag, None)
    url = f"http://{cfg.leader_host}:{cfg.port}/state"
    get = getter or conditional_get_url
    status, body, etag = get(
        url, etag=prev_etag, timeout=LEADER_FETCH_TIMEOUT_SECONDS)
    if status == 200 and body is not None:
        try:
            bundle = parse_bundle(body)
        except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
            return (None, prev_etag, None)
        return (200, etag or _quote_etag(bundle.get("generated_at")),
                bundle["files"])
    if status == 304:
        return (304, prev_etag, None)
    return (None, prev_etag, None)
