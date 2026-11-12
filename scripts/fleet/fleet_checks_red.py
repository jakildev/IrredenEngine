#!/usr/bin/env python3
"""`fleet:checks-red`: reconcile R12's reading of an approved PR's head.

Branch protection requires five lint contexts, so GitHub's merge button stays
enabled while any other check on the head is failing, and `fleet:approved`
says nothing about it. R12 marks such a PR with an informational label and
takes it off again. No lane filter, trigger hash, merger skip set or
auto-merge blocking set reads the label.

The label is wanted when the PR is open and approved and its head has a
completed, failed check of its own. Not its own: a job GitHub never started
(the scout's `not_started` reading of the same head), and a workflow red on
master right now (the scout's master-CI alert file for it). A check still
running is never a failure. Required and non-required checks count alike.
The `all-checks` commit status is not a check of the PR's own: it fails
whenever any other check on the head failed, a master-red one included, so
the head is judged by the checks it aggregates.

Candidates come from the scout's cached reading. Each one is confirmed by a
live read of labels, head and rollup, and the apply arm re-reads the same
three immediately before writing (`confirm`). An add is skipped when the PR's
timeline already has a `fleet:checks-red` labeled or unlabeled event at or
after the newest own failure completed: a person who removed the label keeps
it off until a later failure.

The master-CI alerts exist only on the host that runs the scout's master-CI
lane, so R12 runs only where that lane's deadline marker is fresh. That lane
watches the engine repo's master alone, so R12 covers engine PRs only: in
another repo an inherited master red would read as the PR's own.

Source of truth: scripts/fleet/fleet_checks_red.py in the engine repo.
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fleet_github  # noqa: E402
from fleet_checks_aggregate import CONTEXT as AGGREGATE_CONTEXT  # noqa: E402

LABEL = "fleet:checks-red"
APPROVED = "fleet:approved"

# Mirrors of fleet-state-scout's master-CI lane names and its rollup sets.
MASTER_CI_ALERT_PREFIX = "scout-master-ci-"
MASTER_CI_MARKER = "master-ci.last-run"
MASTER_CI_INTERVAL_DEFAULT = 15 * 60
# The marker may lag one missed pass and a slow tick before the alerts stop
# describing master.
MASTER_CI_STALE_INTERVALS = 3
CHECK_RUN_RED = frozenset({"FAILURE", "TIMED_OUT", "STARTUP_FAILURE"})
STATUS_CONTEXT_RED = frozenset({"FAILURE", "ERROR"})

ENGINE_SLUG = "jakildev/IrredenEngine"
LIVE_FIELDS = "number,state,headRefOid,labels,statusCheckRollup"
GH_TIMEOUT = 60

_NAME_RE = re.compile(r"""^name:\s*(['"]?)(.+?)\1\s*(?:#.*)?$""")


def default_workflows_dir():
    return Path(__file__).resolve().parent.parent.parent / ".github" / "workflows"


def alerts_dir():
    return Path(os.environ.get("FLEET_ALERTS_DIR") or (Path.home() / ".fleet" / "alerts"))


def workflow_files_by_name(workflows_dir):
    """{rollup workflowName: workflow file name}. A workflow without a
    top-level `name:` is named by its path, as GitHub names it."""
    out = {}
    try:
        paths = sorted(p for p in Path(workflows_dir).iterdir()
                       if p.is_file() and p.suffix in (".yml", ".yaml"))
    except OSError:
        return out
    for path in paths:
        out[f".github/workflows/{path.name}"] = path.name
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeDecodeError):
            continue
        for line in lines:
            match = _NAME_RE.match(line)
            if match:
                out[match.group(2).strip()] = path.name
                break
    return out


def master_red_files():
    """Workflow file names with a standing master-CI alert."""
    try:
        return {p.name[len(MASTER_CI_ALERT_PREFIX):]
                for p in alerts_dir().glob(MASTER_CI_ALERT_PREFIX + "*")}
    except OSError:
        return set()


def master_watch_fresh(state_dir, now=None):
    """Whether this host's master-CI lane completed a pass recently enough
    for its alert files to describe master."""
    try:
        interval = int(os.environ.get("FLEET_MASTER_CI_INTERVAL_SECS") or "")
    except ValueError:
        interval = MASTER_CI_INTERVAL_DEFAULT
    if interval < 0:
        interval = MASTER_CI_INTERVAL_DEFAULT
    try:
        last = int((Path(state_dir) / "seen-hashes" / MASTER_CI_MARKER).read_text().strip())
    except (OSError, ValueError):
        return False
    age = (now if now is not None else time.time()) - last
    return age <= MASTER_CI_STALE_INTERVALS * max(interval, 60)


def failed_checks(rollup):
    """[(name, workflowName, completed_at)] for each completed red check,
    the `all-checks` aggregate excluded."""
    out = []
    for node in rollup or ():
        if not isinstance(node, dict):
            continue
        if node.get("__typename") == "StatusContext":
            if (node.get("state") in STATUS_CONTEXT_RED
                    and node.get("context") != AGGREGATE_CONTEXT):
                out.append((node.get("context") or "?", None,
                            node.get("startedAt") or node.get("createdAt") or ""))
        elif node.get("status") == "COMPLETED" and node.get("conclusion") in CHECK_RUN_RED:
            out.append((node.get("name") or "?", node.get("workflowName"),
                        node.get("completedAt") or ""))
    return out


def own_failures(rollup, master_red, by_name):
    """The failed checks whose workflow is not red on master. An unmapped
    workflow name is the PR's own."""
    return sorted(f for f in failed_checks(rollup)
                  if not (f[1] and by_name.get(f[1]) in master_red))


def label_names(obj):
    out = set()
    for label in obj.get("labels") or ():
        out.add(label.get("name", "") if isinstance(label, dict) else str(label))
    return out


def signature(live, master_red, by_name):
    """What a write depends on; the apply arm writes only while it holds."""
    labels = label_names(live)
    return {"head": live.get("headRefOid") or "",
            "open": (live.get("state") or "OPEN") == "OPEN",
            "approved": APPROVED in labels,
            "labeled": LABEL in labels,
            "own": [list(f) for f in own_failures(live.get("statusCheckRollup"),
                                                  master_red, by_name)]}


def wanted(sig, not_started_head):
    return (sig["open"] and sig["approved"] and bool(sig["own"])
            and sig["head"] != not_started_head)


def cached_candidate(record, live_labels=None, master_red=frozenset()):
    """The op a cached PR record makes worth a live read, or None. The
    reconcile pass's own PR list supplies fresher labels when it has them.

    The cache carries no per-check names, so a labeled, approved, cache-red
    PR may hold only failures master now shares; while any workflow is red on
    master it is a removal candidate, and the live rollup decides."""
    labels = live_labels if live_labels is not None else label_names(record)
    red = record.get("checks") == "red" and not record.get("not_started")
    if APPROVED in labels and red and LABEL not in labels:
        return "add"
    if LABEL in labels and not (APPROVED in labels and red):
        return "remove"
    if LABEL in labels and master_red:
        return "remove"
    return None


def newest_event_at(events):
    """The newest `fleet:checks-red` labeled/unlabeled event time, or ""."""
    return max((e.get("created_at") or "" for e in events or ()
                if isinstance(e, dict) and e.get("event") in ("labeled", "unlabeled")
                and (e.get("label") or {}).get("name") == LABEL), default="")


def decide(record, live, events_reader, master_red, by_name):
    """(op, reason, sig) for one cached candidate after its live read; op is
    None when nothing is written. `events_reader()` returns the PR's issue
    events, or None when they could not be read."""
    sig = signature(live, master_red, by_name)
    cached_head = record.get("headRefOid") or ""
    not_started_head = cached_head if record.get("not_started") else None
    want = wanted(sig, not_started_head)
    if want and not sig["labeled"]:
        if sig["head"] != cached_head:
            return None, "the live head differs from the cached read", sig
        newest = max(f[2] for f in sig["own"])
        events = events_reader()
        if events is None:
            return None, "its timeline could not be read", sig
        seen = newest_event_at(events)
        if seen and seen >= newest:
            return None, f"the label was already applied or removed at {seen}", sig
        return "add", "failed on head %s: %s" % (
            sig["head"][:9], ", ".join(f[0] for f in sig["own"])), sig
    if sig["labeled"] and not want:
        if not sig["open"]:
            why = "the PR is no longer open"
        elif not sig["approved"]:
            why = f"{APPROVED} is gone"
        elif sig["head"] == not_started_head:
            why = "its failures are jobs GitHub never started"
        else:
            why = "head %s has no failed check of its own" % sig["head"][:9]
        return "remove", why, sig
    return None, "", sig


def _gh_json(args):
    try:
        proc = fleet_github.run(args, capture_output=True, text=True, timeout=GH_TIMEOUT)
    except (OSError, ValueError, subprocess.SubprocessError) as e:
        print(f"fleet_checks_red: gh {' '.join(args[:2])}: {type(e).__name__}: {e}",
              file=sys.stderr)
        return None
    if proc.returncode != 0:
        return None
    try:
        return json.loads(proc.stdout)
    except ValueError:
        return None


def live_read(repo, number):
    found = _gh_json(["pr", "view", str(number), "--repo", repo, "--json", LIVE_FIELDS])
    return found if isinstance(found, dict) else None


def read_events(repo, number):
    """Every `fleet:checks-red` label event on the PR, or None."""
    try:
        proc = fleet_github.run(
            ["api", f"repos/{repo}/issues/{number}/events", "--paginate",
             "--jq", '.[] | select(.label.name == "%s") | {event, created_at, label}' % LABEL],
            capture_output=True, text=True, timeout=GH_TIMEOUT)
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    if proc.returncode != 0:
        return None
    events = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            events.append(json.loads(line))
        except ValueError:
            return None
    return events


def master_context(repo):
    """(master_red, by_name) for `repo`, or None where no master-CI lane
    watches its master and R12 cannot tell an inherited red from its own."""
    if repo != ENGINE_SLUG:
        return None
    return master_red_files(), workflow_files_by_name(default_workflows_dir())


def confirm(repo, number, sig_json):
    """Exit 0 when the live labels, head and rollup still give `sig_json`."""
    try:
        expected = json.loads(sig_json)
    except ValueError:
        return 2
    context = master_context(repo)
    if context is None:
        return 1
    live = live_read(repo, number)
    if live is None:
        return 1
    return 0 if signature(live, *context) == expected else 1


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "confirm":
        sys.exit(confirm(sys.argv[2], sys.argv[3], sys.argv[4]))
    print("usage: fleet_checks_red.py confirm <repo> <pr> <signature-json>", file=sys.stderr)
    sys.exit(2)
