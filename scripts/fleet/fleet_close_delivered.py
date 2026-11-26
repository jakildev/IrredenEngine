#!/usr/bin/env python3
"""Reconcile R13's close of an issue a merged PR already delivered.

A `Refs #N` merge leaves its issue open and queued. Once a worker declines
that issue with `--delivered-by <PR>`, the scout row and this host's decline
memory together form a shadowed close-out. R13 closes it when the declined PR
is also the scout's branch match for the issue, so the decline and the branch
agree on what delivered it.

The classifier (`candidates`, `refusal`) reads only the scout snapshot, the
decline memory and the listings its reconcile pass already fetched. The apply
arm (`apply`) re-reads the issue, the PR and the open-PR listing immediately
before its one close; any read that fails or disagrees leaves the issue for
the next pass.

The close comes before the comment. A comment moves the issue's `updatedAt`
past the decline stamp, so a comment whose close then failed would put the
issue out of every later pass's reach.

Each close appends one line to a host-local ledger, which `fleet-decisions`
renders once.

Source of truth: scripts/fleet/fleet_close_delivered.py in the engine repo.
"""
import datetime as dt
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fleet_branch_match  # noqa: E402
import fleet_github  # noqa: E402
import fleet_public_text  # noqa: E402
import fleet_task_class  # noqa: E402

HELD_LABELS = frozenset({
    "human:owned", "fleet:needs-human", "fleet:gated", "fleet:epic", "fleet:blocked",
})
# A listing this long may be cut off, so it proves nothing about what it lacks.
LIST_LIMIT = 300
GH_TIMEOUT = 60
# A prefix that is neither `owner/repo` nor a fleet repo name (`PR#12`) is prose.
REF_RE = re.compile(r"((?:[A-Za-z0-9][\w.-]*/)?[A-Za-z0-9][\w.-]*)?#(\d+)\b")


def ledger_path():
    home = os.environ.get("FLEET_HOME") or os.path.join(os.path.expanduser("~"), ".fleet")
    return Path(home) / "reconcile" / "closed-delivered.jsonl"


def candidates(state, key):
    """(issue number, task row, decline metadata) per shadowed close-out row."""
    tasks = ((state["repos"].get(key) or {}).get("tasks") or {}).get("open") or []
    for source in tasks:
        task = {**source, "repo": key}
        if fleet_task_class._shadowed_closeout(task) is not None:
            yield (int(fleet_task_class._record_number(task)), task,
                   fleet_task_class._decline_metadata("task", task, "worker"))


def held_by(labels):
    """The first label that keeps an issue open, or None."""
    return min(HELD_LABELS & set(labels), default=None)


def refusal(task, meta, labels, has_open_pr):
    """Why a candidate is not closed, or None. `labels` is the issue's label
    set in the live `fleet:queued` listing, None when it is not listed."""
    if labels is None:
        return "it is not in the live fleet:queued listing"
    held = held_by(labels)
    if held:
        return f"it carries {held}"
    if has_open_pr or task.get("inflight_pr"):
        return "an open PR resolves it"
    matched = fleet_task_class._shadow_number(task) or meta["shadow_merged_pr"]
    if matched is None:
        return "no merged PR's branch matches it"
    if matched != meta["delivered_pr"]:
        return (f"the decline names PR #{meta['delivered_pr']} but the branch match "
                f"is PR #{matched}")
    return None


def residuals(reason, repo, issue, pr):
    """Every issue reference in `reason` except the issue and the PR, in order."""
    own = fleet_branch_match.repo_key(repo)
    out = []
    for prefix, number in REF_RE.findall(reason):
        key = fleet_branch_match.repo_key(prefix) if prefix else own
        foreign = key != own if key else "/" in prefix
        if not foreign and int(number) in (issue, pr):
            continue
        ref = f"{prefix}#{number}" if foreign else f"#{number}"
        if ref not in out:
            out.append(ref)
    return out


def comment_body(pr, merge_sha, reason, refs, host):
    lines = [f"Closed automatically. PR #{pr} merged as {merge_sha} and delivered this "
             "issue, and a worker declined it as already delivered:",
             "", f"> {reason}", ""]
    if refs:
        lines += [f"The decline also names: {', '.join(refs)}.", ""]
    lines += ["Reopen the issue to undo this. A reopened issue is newer than the decline, "
              "so it stays open unless a worker declines it as delivered again.",
              "", f"— `fleet-claim reconcile` R13 ({host})", ""]
    return "\n".join(lines)


def _gh(args, **kwargs):
    try:
        return fleet_github.run(args, capture_output=True, text=True, timeout=GH_TIMEOUT,
                                **kwargs)
    except (OSError, ValueError, subprocess.SubprocessError) as e:
        return subprocess.CompletedProcess(args, 1, "", f"{type(e).__name__}: {e}")


def _gh_json(args, kind):
    proc = _gh(args)
    if proc.returncode != 0:
        return None
    try:
        found = json.loads(proc.stdout)
    except ValueError:
        return None
    return found if isinstance(found, kind) else None


def _live_issue_refusal(issue, stamp):
    if issue is None:
        return "the issue could not be read"
    if issue.get("state") != "OPEN":
        return f"the issue reads {issue.get('state')}"
    held = held_by(label.get("name") for label in issue.get("labels") or ())
    if held:
        return f"it carries {held}"
    if not issue.get("updatedAt") or issue["updatedAt"] > stamp:
        return "it changed after the decline"
    return None


def _live_pr_refusal(pr, repo, number, issue):
    if pr is None:
        return f"PR #{number} could not be read"
    if pr.get("state") != "MERGED" or pr.get("baseRefName") != "master":
        return (f"PR #{number} reads {pr.get('state')} into {pr.get('baseRefName')}, "
                "not MERGED into master")
    if not fleet_branch_match.branch_matches_issue(pr.get("headRefName"), issue, repo):
        return f"PR #{number}'s head branch does not match the issue"
    if not (pr.get("mergeCommit") or {}).get("oid"):
        return f"PR #{number} has no merge commit"
    return None


def _open_pr_refusal(prs, repo, issue):
    if prs is None:
        return "the open PRs could not be read"
    if len(prs) >= LIST_LIMIT:
        return "the open-PR listing may be cut off"
    ref = (fleet_branch_match.repo_key(repo), issue)
    for pr in prs:
        if (fleet_branch_match.branch_matches_issue(pr.get("headRefName"), issue, repo)
                or ref in fleet_branch_match.body_closed_issue_refs(pr.get("body"), repo)):
            return f"open PR #{pr.get('number')} resolves it"
    return None


def apply(action, host):
    """Close one classified issue; returns the line the reconcile pass prints."""
    repo, number, pr_number = action["repo"], action["issue"], action["pr"]
    reason = action["reason"]
    issue = _gh_json(["issue", "view", str(number), "--repo", repo,
                      "--json", "state,title,labels,updatedAt"], dict)
    why = _live_issue_refusal(issue, action["stamp"])
    pr = None
    if why is None:
        pr = _gh_json(["pr", "view", str(pr_number), "--repo", repo,
                       "--json", "state,baseRefName,headRefName,mergeCommit"], dict)
        why = _live_pr_refusal(pr, repo, pr_number, number)
    if why is None:
        why = _open_pr_refusal(
            _gh_json(["pr", "list", "--repo", repo, "--state", "open", "--json",
                      "number,headRefName,body", "--limit", str(LIST_LIMIT)], list),
            repo, number)
    if why is None:
        merge_sha = pr["mergeCommit"]["oid"]
        refs = residuals(reason, repo, number, pr_number)
        body = comment_body(pr_number, merge_sha, reason, refs, host)
        try:
            findings = fleet_public_text.find_public_text(
                body, repo, git_root=os.environ.get("FLEET_ENGINE_ROOT"))
        except (OSError, ValueError) as e:
            why = f"its comment could not be checked for public text ({e})"
        else:
            if findings:
                why = "its comment is refused public text: " + ", ".join(
                    fleet_public_text.format_finding(f) for f in findings)
    if why is None:
        closed = _gh(["issue", "close", str(number), "--repo", repo, "--reason", "completed"])
        if closed.returncode != 0:
            why = "the close failed: " + " ".join(closed.stderr.split())
    if why is not None:
        return f"R13 left {repo} #{number} open: {why} (the next pass retries)"
    commented = _gh(["issue", "comment", str(number), "--repo", repo, "--body-file", "-"],
                    input=body).returncode == 0
    line = f"R13 closed {repo} #{number}, delivered by PR #{pr_number} ({merge_sha[:9]})"
    if not commented:
        line += "; its comment failed (not retried)"
    entry = {"ts": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
             "repo": repo, "issue": number, "title": issue.get("title") or "",
             "pr": pr_number, "merge_sha": merge_sha, "reason": reason,
             "residuals": refs, "commented": commented, "host": host}
    try:
        path = ledger_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "ab") as handle:
            handle.write((json.dumps(entry, sort_keys=True) + "\n").encode("ascii"))
    except OSError as e:
        line += f"; its ledger line was not written ({e})"
    return line


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "apply":
        print(apply(json.load(sys.stdin)["apply"], sys.argv[2]))
        sys.exit(0)
    print("usage: fleet_close_delivered.py apply <host> < <finding-json>", file=sys.stderr)
    sys.exit(2)
