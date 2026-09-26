"""fleet_claim_liveness.py — keep/reap verdict for the owner of a claim label.

Every sweep that removes a ``fleet:amending-<host>-<agent>`` or
``fleet:claim-<host>-<agent>`` label asks this one predicate whether the
owner is still working: ``fleet-claim cleanup --gh`` (PR-label and issue-claim
passes), ``_acquire_label_on``'s force-sweep, and ``reconcile`` R1. Bash
reaches it through the CLI arm (``verdict``); reconcile's heredoc imports it.

Verdicts are data, not errors:

  0  LIVE          — the owner is working; the sweep keeps the label.
  1  CANNOT VOUCH  — the sweep falls through to its own age rule.
  2  CONFIRMED DEAD— the owning iteration has provably ended (amending only);
                     the caller may apply its short orphan grace.

Every input is host-local — heartbeats, dispatch records, reservations, the
amend snapshot, the FS claim lock — so a label naming another host is always
``CANNOT VOUCH``: only the owning host can judge its own claims. A pane's
branch or HEAD is never an input; a detached worktree is judged exactly like
one on a feature branch.

Arms, first match wins:

  (i)   the label is unparsable or names another host        → 1
  (ii)  amending: the dispatcher's pre-claim sentinel, stamped
        within the pre-claim grace                           → 0
  (iii) a live dispatch record names this owner and item
        (``task|stack:<ns>:<N>`` for a claim,
        ``feedback:<ns>:<N>`` for an amend)                   → 0
  (iv)  amending: the stamped dispatch id has been superseded
        in ``dispatch-current/<agent>``                       → 2
  (v)   the owner's heartbeat is younger than the calling
        sweep's TTL and its identity holds                   → 0
  (vi)  otherwise                                            → 1

A heartbeat never vouches alone. Every role's step 0 touches the pane's
heartbeat under the worktree basename, so a later role in the same pane would
renew a dead claim forever. Identity holds when the claim's stamped dispatch
id is the pane's current one, when either id is absent (an architect pane has
none; a claim stamped before ids were recorded has none), or when the owner's
worktree reservation names this item — a reserved pane is dispatched only
into the worker lane, to resume that item. The pre-claim sentinel is a
stamped id that matches no dispatch, so past its grace it needs the dispatch
record, a reservation, or the pane's current id being absent.

A superseded id is proof of death only for an amend: a task claim legitimately
outlives its first dispatch (abandon retry, reservation resume), so arm (iv)
never fires for ``fleet:claim-*``.
"""

import json
import os
import re
import sys
import time

LIVE, CANNOT_VOUCH, DEAD = 0, 1, 2

HOSTS = ("mac", "linux", "windows")

# keep in sync with fleet-common.sh FLEET_PRECLAIM_DISPATCH_ID
DEFAULT_PRECLAIM_ID = "preclaim"
DEFAULT_PRECLAIM_GRACE_SECS = 300

# Dispatch-target kinds that mean "this pane is working the item" per label kind.
_DISPATCH_KINDS = {"claim": ("task", "stack"), "amending": ("feedback",)}

_TARGET_RE = re.compile(r"^([a-z]+):([a-z]+):(\d+)(?::|$)")


def parse_claim_label(label):
    """``(kind, host, agent)`` for a ``fleet:amending-*`` / ``fleet:claim-*``
    label, else None. The host is one of HOSTS; the agent is everything after
    it and may itself contain dashes."""
    for kind in ("amending", "claim"):
        prefix = f"fleet:{kind}-"
        if not label.startswith(prefix):
            continue
        rest = label[len(prefix):]
        for host in HOSTS:
            if rest.startswith(host + "-") and len(rest) > len(host) + 1:
                return kind, host, rest[len(host) + 1:]
        return None
    return None


def claim_slug(ns, number):
    """The FS lock slug fleet-claim's slugify() gives an issue claim."""
    return f"{ns}-{number}" if ns and ns != "engine" else str(number)


def _read_text(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return fh.read().strip()
    except OSError:
        return ""


def _read_json(path):
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def _mtime(path):
    try:
        return int(os.path.getmtime(path))
    except OSError:
        return 0


def live_dispatch_target(dispatch_dir, agent, kinds, ns, number):
    """The target of a live dispatch record owned by ``agent`` that names this
    item, or "". A record exists only while its pane is occupied."""
    try:
        names = sorted(os.listdir(dispatch_dir))
    except OSError:
        return ""
    for name in names:
        if not name.endswith(".json"):
            continue
        rec = _read_json(os.path.join(dispatch_dir, name))
        if (rec.get("agent") or "") != agent:
            continue
        target = rec.get("target") or ""
        m = _TARGET_RE.match(target)
        if m and m.group(1) in kinds and m.group(2) == ns and int(m.group(3)) == number:
            return target
    return ""


def _stamped_identity(kind, agent, ns, number, dirs):
    """``(dispatch_id, acquired_epoch)`` the claim was stamped with, or
    ``("", 0)`` when no record of this owner's claim carries one."""
    if kind == "amending":
        snap = _read_json(os.path.join(dirs["amend_snapshots"], f"{number}.json"))
        if (snap.get("agent") or "") != agent:
            return "", 0
        try:
            epoch = int(snap.get("acquired_epoch") or 0)
        except (TypeError, ValueError):
            epoch = 0
        return str(snap.get("dispatch_id") or ""), epoch
    lock = os.path.join(dirs["claims"], claim_slug(ns, number))
    owner = _read_text(os.path.join(lock, "owner"))
    if owner and owner != agent:
        return "", 0
    return _read_text(os.path.join(lock, "dispatch_id")), 0


def _reservation_names(dirs, agent, number):
    rec = _read_json(os.path.join(dirs["reservations"], f"{agent}.json"))
    return str(rec.get("task_id") or "") == str(number)


def verdict(label, ns, number, *, this_host, ttl, now, dirs,
            preclaim_id=DEFAULT_PRECLAIM_ID,
            preclaim_grace=DEFAULT_PRECLAIM_GRACE_SECS):
    """``(code, detail)`` for the owner of ``label`` on item ``ns``/``number``.

    ``dirs`` maps ``heartbeats``, ``state`` (holding ``dispatch/`` and
    ``dispatch-current/``), ``reservations``, ``amend_snapshots`` and
    ``claims`` to directories. ``ttl`` is the calling sweep's own TTL, the
    window a heartbeat must fall inside. ``detail`` is a dict whose ``arm``
    names the deciding arm; a DEAD verdict also carries ``owner_dispatch`` and
    ``superseded_by``, a dispatch keep carries ``target``.
    """
    parsed = parse_claim_label(label)
    if parsed is None:
        return CANNOT_VOUCH, {"arm": "unparsable"}
    kind, host, agent = parsed
    if host != this_host:
        return CANNOT_VOUCH, {"arm": "cross-host", "host": host}
    number = int(number)

    stamped, stamped_epoch = _stamped_identity(kind, agent, ns, number, dirs)
    if (kind == "amending" and stamped == preclaim_id
            and stamped_epoch > 0 and now - stamped_epoch < preclaim_grace):
        return LIVE, {"arm": "preclaim"}

    target = live_dispatch_target(os.path.join(dirs["state"], "dispatch"),
                                  agent, _DISPATCH_KINDS[kind], ns, number)
    if target:
        return LIVE, {"arm": "dispatch", "target": target, "agent": agent}

    current = _read_text(os.path.join(dirs["state"], "dispatch-current", agent))
    current = current.splitlines()[0].strip() if current else ""
    if (kind == "amending" and stamped and stamped != preclaim_id
            and current and current != stamped):
        return DEAD, {"arm": "superseded", "owner_dispatch": stamped,
                      "superseded_by": current}

    beat = _mtime(os.path.join(dirs["heartbeats"], agent))
    if beat > 0 and now - beat < ttl:
        if not stamped or not current or stamped == current:
            return LIVE, {"arm": "heartbeat", "agent": agent}
        if _reservation_names(dirs, agent, number):
            return LIVE, {"arm": "heartbeat+reservation", "agent": agent}
        return CANNOT_VOUCH, {"arm": "identity-mismatch", "agent": agent}
    return CANNOT_VOUCH, {"arm": "no-signal", "agent": agent}


_DIR_ENV = {
    "heartbeats": ("FLEET_HEARTBEATS_DIR", "heartbeats"),
    "state": ("FLEET_STATE_DIR", "state"),
    "reservations": ("FLEET_RESERVATIONS_DIR", "reservations"),
    "amend_snapshots": ("FLEET_AMEND_SNAPSHOTS_DIR", "amend-snapshots"),
    "claims": ("FLEET_CLAIMS_DIR", "claims"),
}


def _dirs_from_env():
    fleet = os.path.join(os.path.expanduser("~"), ".fleet")
    return {key: os.environ.get(var) or os.path.join(fleet, sub)
            for key, (var, sub) in _DIR_ENV.items()}


_USAGE = ("usage: fleet_claim_liveness.py verdict <label> <ns> <number> <host> <ttl-secs>\n"
          "  [--heartbeats-dir D] [--state-dir D] [--reservations-dir D]\n"
          "  [--amend-snapshots-dir D] [--claims-dir D] [--now EPOCH]")


def main(argv):
    """``verdict`` prints one tab-separated line
    ``<code>\\t<arm>\\t<owner_dispatch>\\t<superseded_by>\\t<target>`` and
    exits 0; the caller branches on the printed code, so a crash or a usage
    error (exit != 0, nothing printed) can never read as a verdict. An empty
    field prints as ``-``: tab is IFS whitespace, so bash's ``read`` would
    merge an empty field into its neighbour. Directory flags override the same
    FLEET_* variables fleet-claim reads."""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(newline="\n")
    args = list(argv[1:])
    if len(args) < 6 or args[0] != "verdict":
        print(_USAGE, file=sys.stderr)
        return 2
    label, ns, number, host, ttl = args[1:6]
    dirs = _dirs_from_env()
    now = int(time.time())
    flags = {"--heartbeats-dir": "heartbeats", "--state-dir": "state",
             "--reservations-dir": "reservations",
             "--amend-snapshots-dir": "amend_snapshots", "--claims-dir": "claims"}
    rest = args[6:]
    while rest:
        flag = rest.pop(0)
        if not rest:
            print(f"fleet_claim_liveness: {flag} needs a value\n{_USAGE}", file=sys.stderr)
            return 2
        value = rest.pop(0)
        if flag in flags and value:
            dirs[flags[flag]] = value
        elif flag == "--now" and value.isdigit():
            now = int(value)
        else:
            print(f"fleet_claim_liveness: bad argument {flag} {value!r}\n{_USAGE}",
                  file=sys.stderr)
            return 2
    if not number.isdigit() or not ttl.isdigit():
        print(f"fleet_claim_liveness: <number> and <ttl-secs> must be integers\n{_USAGE}",
              file=sys.stderr)
        return 2
    code, detail = verdict(
        label, ns, int(number), this_host=host, ttl=int(ttl), now=now, dirs=dirs,
        preclaim_id=os.environ.get("FLEET_PRECLAIM_DISPATCH_ID") or DEFAULT_PRECLAIM_ID,
        preclaim_grace=int(os.environ.get("FLEET_CLAIM_PRECLAIM_GRACE_SECS")
                           or DEFAULT_PRECLAIM_GRACE_SECS))
    fields = [detail.get(k) or "-" for k in
              ("arm", "owner_dispatch", "superseded_by", "target")]
    print("\t".join([str(code)] + fields))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
