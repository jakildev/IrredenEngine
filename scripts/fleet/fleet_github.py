"""The fleet's one way to launch the GitHub CLI, and per-attempt accounting.

Every fleet Python caller builds its `gh` argv here (`argv()` / `run()`),
and every shell caller reaches the same resolver through the
`gh-accounting-bin/gh` launcher. Resolution is fixed:

  1. An explicit per-tool override (`FLEET_STALLED_SWEEP_GH_BIN`, ...) wins
     and is executed unchanged, never counted.
  2. Otherwise the first `gh` on the caller's PATH (PATHEXT-aware on native
     Windows, where an extensionless shim is invisible).
  3. With accounting enabled, that candidate is counted only when it is the
     accounting launcher or the same file as the real CLI fleet-up pinned;
     the pinned CLI is then what runs. Any other candidate is a hermetic or
     user stub: executed unchanged, never counted.

Accounting records command attempts, not API points: one metadata-only
event per counted launch (UTC time, actor, runtime, route, access). An event
never carries argv, repository, issue, URL, body, token, or output. Events
are one file each, written to a hidden temp name and renamed into
`<event-root>/<YYYYMMDDTHH>/`, so concurrent writers share no file and a
reader never sees a torn event.

Environment (set by fleet-up; absent = accounting off):
  FLEET_GH_ACCOUNTING   "1" enables counting
  FLEET_GH_REAL         absolute path of the real gh (Windows mixed form, with .exe)
  FLEET_GH_LAUNCHER     absolute path of gh-accounting-bin/gh
  FLEET_GH_PYTHON       interpreter the shell launcher runs this module under
  FLEET_GH_EVENT_ROOT   event directory
  FLEET_GH_ACTOR        explicit actor (scout, dispatcher); else FLEET_ROLE

Launcher-internal (set by `launch`, never by fleet-up):
  FLEET_GH_LAUNCH_DEPTH launcher passes one call has already made; `launch`
                        refuses at MAX_LAUNCH_DEPTH instead of looping
"""

import datetime as dt
import json
import os
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

ENV_ENABLED = "FLEET_GH_ACCOUNTING"
ENV_REAL = "FLEET_GH_REAL"
ENV_LAUNCHER = "FLEET_GH_LAUNCHER"
ENV_PYTHON = "FLEET_GH_PYTHON"
ENV_EVENT_ROOT = "FLEET_GH_EVENT_ROOT"
ENV_ACTOR = "FLEET_GH_ACTOR"
# Not an ENV_* name: those are what fleet-up hands a pane or a daemon, and
# nothing is ever launched with a depth.
LAUNCH_DEPTH_VAR = "FLEET_GH_LAUNCH_DEPTH"
ACCOUNTING_ENV = (ENV_ENABLED, ENV_REAL, ENV_LAUNCHER, ENV_PYTHON, ENV_EVENT_ROOT, ENV_ACTOR,
                  LAUNCH_DEPTH_VAR)
# The launcher delegates to whatever gh fleet-up pinned. When that is itself a
# wrapper that finds the launcher first on PATH, the two exec each other until
# the caller's timeout. A legitimate nested call (gh -> git -> credential
# helper -> gh) is two or three passes deep.
MAX_LAUNCH_DEPTH = 8

LAUNCHER_PATH = Path(__file__).resolve().parent / "gh-accounting-bin" / "gh"
RETENTION_HOURS = 48
HOUR_FORMAT = "%Y%m%dT%H"
PRUNE_ONE_IN = 256

BUCKETS = ("graphql_read", "graphql_write", "graphql_api", "rest", "other")

# `gh pr|issue <verb>`: GraphQL-backed commands, split by whether they mutate.
GRAPHQL_READ_VERBS = frozenset({"view", "list", "status", "diff", "checks", "checkout"})
GRAPHQL_WRITE_VERBS = frozenset({
    "create", "edit", "comment", "close", "reopen", "merge", "ready", "review", "lock",
    "unlock", "delete", "transfer", "pin", "unpin", "develop", "revert", "update-branch",
})
# `gh api` flags that consume the next argument; the endpoint is the first
# token that is neither a flag nor such a value.
API_VALUE_FLAGS = frozenset({
    "-X", "--method", "-H", "--header", "-f", "--raw-field", "-F", "--field", "--input",
    "-q", "--jq", "-t", "--template", "--hostname", "--cache", "-p", "--preview",
})
API_BODY_FLAGS = frozenset({"-f", "--raw-field", "-F", "--field", "--input"})

_warned = set()


class Resolution:
    """The argv prefix to launch, whether it is counted, and whether a gh was found."""

    __slots__ = ("argv", "accounted", "found")

    def __init__(self, argv, accounted, found):
        self.argv, self.accounted, self.found = argv, accounted, found


def _warn_once(key, message):
    if key not in _warned:
        _warned.add(key)
        print(f"fleet_github: {message}", file=sys.stderr)


def _same_file(a, b):
    """True/False, or None when either side cannot be stat'd."""
    try:
        return os.path.samefile(a, b)
    except OSError:
        return None


def _launch_argv(path):
    """The argv prefix for a resolved `gh` path.

    Native Windows CreateProcess routes a `.bat` through cmd.exe, which
    re-parses the whole argv (`&per_page=100` splits into two commands). The
    one `.bat` safe to bypass is a hermetic twin beside an extensionless
    Python sibling of the same name: run the sibling through the interpreter.
    """
    if os.name == "nt" and path.lower().endswith(".bat"):
        sibling = path[: -len(".bat")]
        try:
            with open(sibling, "r", encoding="utf-8") as f:
                first_line = f.readline()
        except OSError:
            first_line = ""
        if first_line.startswith("#!") and "python" in first_line:
            return [sys.executable, sibling]
    return [path]


def _path_candidates(env):
    """Each `gh` on env's PATH, in order (PATHEXT-aware via shutil.which)."""
    for entry in (env.get("PATH") or "").split(os.pathsep):
        if entry:
            hit = shutil.which("gh", path=entry)
            if hit:
                yield hit


def resolve(override_var=None, env=None, skip_launcher=False):
    """Resolve the `gh` to launch under the fixed precedence (module docstring).

    `skip_launcher` is the launcher's own view: when accounting does not claim
    the call, its own PATH entry is not a delegation target, so a disabled
    launcher never re-executes itself.
    """
    env = os.environ if env is None else env
    if override_var and env.get(override_var):
        return Resolution(_launch_argv(env[override_var]), False, True)
    candidates = _path_candidates(env)
    candidate = next(candidates, None)
    launcher = env.get(ENV_LAUNCHER) or str(LAUNCHER_PATH)

    def is_launcher(path):
        return bool(_same_file(path, launcher) or _same_file(path, LAUNCHER_PATH))

    enabled = env.get(ENV_ENABLED) == "1"
    real = env.get(ENV_REAL) or ""
    if enabled and candidate:
        if not real:
            _warn_once("no-real", f"{ENV_ENABLED}=1 but {ENV_REAL} is unset; "
                                  "GitHub CLI calls are not being counted")
        elif is_launcher(candidate):
            return Resolution(_launch_argv(real), True, True)
        else:
            same_real = _same_file(candidate, real)
            if same_real:
                return Resolution(_launch_argv(real), True, True)
            if same_real is None:
                _warn_once("stat:" + real,
                           f"cannot compare {candidate} with the pinned {ENV_REAL}={real} "
                           "(unreadable); this call runs uncounted")
    if skip_launcher:
        while candidate is not None and is_launcher(candidate):
            candidate = next(candidates, None)
    if candidate is None:
        return Resolution(["gh"], False, False)
    return Resolution(_launch_argv(candidate), False, True)


# --- Classification -----------------------------------------------------------

def _api_endpoint_and_access(args):
    endpoint, method, has_body, query_mutates = None, None, False, False
    i = 0
    while i < len(args):
        tok = args[i]
        name, eq, value = tok.partition("=")
        if tok.startswith("-") and name in API_VALUE_FLAGS:
            if not eq:
                i += 1
                value = args[i] if i < len(args) else ""
            if name in ("-X", "--method"):
                method = value.upper()
            if name in API_BODY_FLAGS:
                has_body = True
                if value.startswith("query=") and "mutation" in value:
                    query_mutates = True
        elif not tok.startswith("-") and endpoint is None:
            endpoint = tok
        i += 1
    if endpoint == "graphql":
        return "graphql_api", "write" if query_mutates else "read"
    if method is None:
        method = "POST" if has_body else "GET"
    return "rest", "read" if method in ("GET", "HEAD") else "write"


def classify(args):
    """(route, access) for one `gh` argv tail; the argv itself is never kept."""
    if not args:
        return "other", "unknown"
    head = args[0]
    if head in ("pr", "issue"):
        verb = args[1] if len(args) > 1 else ""
        if verb in GRAPHQL_READ_VERBS:
            return "graphql", "read"
        if verb in GRAPHQL_WRITE_VERBS:
            return "graphql", "write"
        return "other", "unknown"
    if head == "api":
        return _api_endpoint_and_access(args[1:])
    return "other", "unknown"


def bucket(route, access):
    if route == "graphql":
        return {"read": "graphql_read", "write": "graphql_write"}.get(access, "other")
    return route if route in ("graphql_api", "rest") else "other"


# --- Events -------------------------------------------------------------------

def actor_of(env):
    return env.get(ENV_ACTOR) or env.get("FLEET_ROLE") or "unknown"


def record(args, env=None, now=None):
    """Write one event for a counted attempt. Never raises: a failed write is
    reported once on stderr and the command still runs."""
    env = os.environ if env is None else env
    root = env.get(ENV_EVENT_ROOT)
    if not root:
        _warn_once("no-root", f"{ENV_ENABLED}=1 but {ENV_EVENT_ROOT} is unset; not counted")
        return None
    ts = time.time() if now is None else now
    route, access = classify(list(args))
    event = {
        "ts": dt.datetime.fromtimestamp(ts, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ"),
        "actor": actor_of(env),
        "runtime": env.get("FLEET_RUNTIME") or "none",
        "route": route,
        "access": access,
    }
    hour_dir = Path(root) / time.strftime(HOUR_FORMAT, time.gmtime(ts))
    name = f"{int(ts * 1000)}-{os.getpid()}-{random.getrandbits(32):08x}.json"
    tmp = hour_dir / f".{name}.tmp"
    try:
        hour_dir.mkdir(parents=True, exist_ok=True)
        with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(event, fh, sort_keys=True)
            fh.write("\n")
        os.replace(tmp, hour_dir / name)
    except OSError as exc:
        _warn_once("write", f"cannot write an accounting event under {root}: {exc}")
        try:
            tmp.unlink()
        except OSError:
            pass
        return None
    if random.randrange(PRUNE_ONE_IN) == 0:
        prune(root, ts)
    return hour_dir / name


def _hour_start(name):
    try:
        return dt.datetime.strptime(name, HOUR_FORMAT).replace(tzinfo=dt.timezone.utc).timestamp()
    except ValueError:
        return None


def prune(root, now=None, retention_hours=RETENTION_HOURS):
    """Remove hour directories that ended more than `retention_hours` ago.
    Only whole, closed hours go; the current hour and unparseable names stay."""
    now = time.time() if now is None else now
    cutoff = now - retention_hours * 3600
    removed = []
    try:
        entries = list(Path(root).iterdir())
    except OSError:
        return removed
    for entry in entries:
        start = _hour_start(entry.name)
        if start is None or not entry.is_dir() or start + 3600 > cutoff:
            continue
        shutil.rmtree(entry, ignore_errors=True)
        removed.append(entry.name)
    return removed


def iter_events(root, since, until):
    """Complete events with since <= ts < until (aware datetimes). Hidden temp
    files, non-JSON, and malformed events are skipped."""
    lo, hi = since.timestamp(), until.timestamp()
    try:
        entries = sorted(Path(root).iterdir())
    except OSError:
        return
    for entry in entries:
        start = _hour_start(entry.name)
        if start is None or start + 3600 <= lo or start >= hi or not entry.is_dir():
            continue
        # A concurrent writer's prune() can remove the shard after the is_dir().
        try:
            paths = sorted(entry.iterdir())
        except OSError:
            continue
        for path in paths:
            if path.name.startswith(".") or not path.name.endswith(".json"):
                continue
            try:
                event = json.loads(path.read_text(encoding="utf-8"))
                ts = dt.datetime.fromisoformat(event["ts"].replace("Z", "+00:00")).timestamp()
            except (OSError, ValueError, KeyError, TypeError, AttributeError):
                continue
            if lo <= ts < hi:
                yield event


def summarize(events):
    """Per-actor bucket counts; every row's `total` is the sum of its buckets."""
    rows = {}
    for event in events:
        actor = str(event.get("actor") or "unknown")
        row = rows.setdefault(actor, {b: 0 for b in BUCKETS})
        row[bucket(event.get("route"), event.get("access"))] += 1
    for row in rows.values():
        row["total"] = sum(row[b] for b in BUCKETS)
    return rows


# --- Launch helpers -------------------------------------------------------------

def argv(args, override_var=None, env=None):
    """The full argv for `gh <args>`, counting the attempt when accounted."""
    args = list(args)
    res = resolve(override_var, env)
    if res.accounted:
        record(args, env)
    return res.argv + args


def run(args, override_var=None, env=None, **kwargs):
    """`subprocess.run` of `gh <args>` through the resolver."""
    return subprocess.run(argv(args, override_var, env), **kwargs)


def _launch_depth(env):
    try:
        return max(0, int(env.get(LAUNCH_DEPTH_VAR) or 0))
    except ValueError:
        return 0


def launch(args, env=None):
    """The shell launcher: resolve, count, run with inherited stdio, return the
    child's status. A missing gh is bash's own 127; a call that has already
    passed through the launcher MAX_LAUNCH_DEPTH times is refused with 126."""
    env = os.environ if env is None else env
    depth = _launch_depth(env)
    if depth >= MAX_LAUNCH_DEPTH:
        print(f"gh: the fleet launcher was re-entered {depth} times for one call: the gh "
              f"it delegates to ({env.get(ENV_REAL) or 'first on PATH'}) leads back to "
              "the launcher; refusing to loop", file=sys.stderr)
        return 126
    res = resolve(env=env, skip_launcher=True)
    if not res.found:
        print("gh: command not found", file=sys.stderr)
        return 127
    if res.accounted:
        record(args, env)
    full = res.argv + list(args)
    child_env = {**env, LAUNCH_DEPTH_VAR: str(depth + 1)}
    if os.name != "nt":
        sys.stdout.flush()
        try:
            os.execve(full[0], full, child_env)
        except OSError as exc:
            print(f"gh: {exc}", file=sys.stderr)
            return 126
    # Native Windows has no exec: os.exec* spawns and returns to the caller at
    # once, so the parent's status would stand in for the child's. Run the
    # child on the inherited handles and hand its exact status back.
    try:
        return subprocess.run(full, env=child_env).returncode
    except OSError as exc:
        print(f"gh: {exc}", file=sys.stderr)
        return 126


def main(argv_in):
    if len(argv_in) >= 2 and argv_in[1] == "launch":
        return launch(argv_in[2:])
    print("usage: fleet_github.py launch <gh args...>", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
