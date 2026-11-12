#!/usr/bin/env python3
"""Set the `all-checks` commit status: one verdict over every other check on a head.

Called by `.github/workflows/all-checks.yml` after any other workflow run
completes, and by hand through that workflow's manual dispatch:

    fleet_checks_aggregate.py --repo OWNER/NAME --sha <40-hex> [--root DIR] [--target-url URL]

The verdict, read on the newest check run per check name:

  failure  any concluded failure, timed_out, startup_failure or action_required
  pending  otherwise, any check queued or running, a newest run that was
           cancelled or went stale, or an always-run workflow with no run on
           the head yet
  success  otherwise; skipped and neutral are green

An always-run workflow is one whose pull_request trigger fires on every head
push: no paths / branches filter and no types list narrower than GitHub's
default. The set is derived from the workflow files under `--root` (the
default branch's checkout), so a path-filtered workflow that never started on
a head is simply not counted. A workflow run with no check run of its own (a
startup failure, or one still queued) is judged by its own status.

The status is written only when it differs from the current one, and a run
that starts from `success` writes `pending` before reading the checks, so a run
that dies part-way can never leave a stale `success` behind. Any API failure
exits 1 with no verdict written.

Never reads or runs pull request code: the workflow calling this has a write
token even for a pull request from a fork, so the only inputs are the API and
the default branch's workflow files.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import fleet_github

CONTEXT = "all-checks"
WORKFLOW_DIR = ".github/workflows"
OWN_WORKFLOW = f"{WORKFLOW_DIR}/all-checks.yml"

FAILED = {"failure", "timed_out", "startup_failure", "action_required"}
GREEN = {"success", "skipped", "neutral"}

# pull_request filters that keep a workflow off some head pushes. A `types`
# list is a filter only when it drops one of GitHub's defaults.
PR_FILTER_KEYS = {"paths", "paths-ignore", "branches", "branches-ignore"}
DEFAULT_PR_TYPES = {"opened", "synchronize", "reopened"}

PER_PAGE = 100
MAX_PAGES = 10
DESCRIPTION_LIMIT = 140
API_TIMEOUT_SECONDS = 60
SHA_RE = re.compile(r"^[0-9a-f]{40}$")


class ApiError(Exception):
    pass


class GhApi:
    """REST through the fleet's gh resolver."""

    def get(self, path):
        try:
            return json.loads(self._call(["api", path]) or "null")
        except ValueError as e:
            raise ApiError(f"gh api {path.split('?')[0]}: not JSON") from e

    def post(self, path, fields):
        args = ["api", "--method", "POST", path]
        for key, value in fields.items():
            args += ["-f", f"{key}={value}"]
        self._call(args)

    @staticmethod
    def _call(args):
        try:
            run = fleet_github.run(args, capture_output=True, text=True,
                                   timeout=API_TIMEOUT_SECONDS)
        except (OSError, subprocess.TimeoutExpired) as e:
            raise ApiError(f"gh {' '.join(args[:4])}: {e}") from e
        if run.returncode != 0:
            last = run.stderr.strip().splitlines()[-1:] or [f"exit {run.returncode}"]
            raise ApiError(f"gh {' '.join(args[:4])}: {last[0]}")
        return run.stdout


def list_all(api, path, key):
    """Every item of the paged REST list at `path`. A list still full at
    MAX_PAGES is an error: its tail was never read."""
    items = []
    sep = "&" if "?" in path else "?"
    for page in range(1, MAX_PAGES + 1):
        body = api.get(f"{path}{sep}per_page={PER_PAGE}&page={page}")
        if not isinstance(body, dict) or not isinstance(body.get(key), list):
            raise ApiError(f"{path.split('?')[0]}: no `{key}` list in the response")
        items.extend(body[key])
        if len(body[key]) < PER_PAGE:
            return items
    raise ApiError(f"{path.split('?')[0]}: more than {PER_PAGE * MAX_PAGES} items")


# ----------------------------------------------------------------------
# Always-run workflows, from the workflow files
# ----------------------------------------------------------------------
def _strip_comments(text):
    return [re.sub(r"(^|\s)#.*", "", line).rstrip() for line in text.splitlines()]


def _flow_items(value):
    """The words of a flow sequence or scalar: `[a, 'b']` or `a`."""
    return re.findall(r"[A-Za-z_][\w-]*", value)


def _block(lines, start):
    """The lines after `lines[start]` indented deeper than it, and the index
    past them. Blank lines ride along."""
    indent = len(lines[start]) - len(lines[start].lstrip())
    end = start + 1
    while end < len(lines) and (not lines[end].strip()
                                or len(lines[end]) - len(lines[end].lstrip()) > indent):
        end += 1
    return lines[start + 1:end], end


def _mapping(lines):
    """`{key: (inline value, child lines)}` for the shallowest keys of a block,
    or None when the block is a sequence."""
    body = [line for line in lines if line.strip()]
    if not body:
        return {}
    if body[0].lstrip().startswith("- "):
        return None
    indent = min(len(line) - len(line.lstrip()) for line in body)
    keys, i = {}, 0
    while i < len(lines):
        line = lines[i]
        if line.strip() and len(line) - len(line.lstrip()) == indent:
            key, _, value = line.strip().partition(":")
            children, i = _block(lines, i)
            keys[key.strip().strip("'\"")] = (value.strip(), children)
        else:
            i += 1
    return keys


def runs_on_every_pr(text):
    """True when the workflow's pull_request trigger fires on every head push.
    Parsed without a YAML library: `on:` has a scalar, a list, and a mapping
    form, and the mapping's pull_request entry may carry filter keys."""
    lines = _strip_comments(text)
    at = next((i for i, line in enumerate(lines)
               if re.match(r"""^["']?on["']?\s*:""", line)), None)
    if at is None:
        return False
    inline = lines[at].split(":", 1)[1].strip()
    if inline:
        return "pull_request" in _flow_items(inline)
    block, _ = _block(lines, at)
    events = _mapping(block)
    if events is None:
        return "pull_request" in [line.strip()[2:].strip().strip("'\"")
                                  for line in block if line.strip().startswith("- ")]
    if "pull_request" not in events:
        return False
    value, children = events["pull_request"]
    if value.startswith("{"):
        filters = {k.strip(): v for k, _, v in
                   (part.partition(":") for part in re.split(r",\s*(?![^\[]*\])",
                                                             value.strip("{}")))}
    elif value and value not in ("~", "null"):
        return False
    else:
        filters = {k: v if v else " ".join(c.strip() for c in ch)
                   for k, (v, ch) in (_mapping(children) or {}).items()}
    if PR_FILTER_KEYS & set(filters):
        return False
    return "types" not in filters or DEFAULT_PR_TYPES <= set(_flow_items(filters["types"]))


def workflow_name(text):
    """The top-level `name:` of a workflow file, or None."""
    for line in _strip_comments(text):
        match = re.match(r"^name\s*:\s*(.+)$", line)
        if match:
            return match.group(1).strip().strip("'\"")
    return None


def workflow_files(root):
    directory = Path(root) / WORKFLOW_DIR
    return sorted(p for p in directory.iterdir() if p.suffix in (".yml", ".yaml"))


def always_run_workflows(root):
    """`{repo-relative path: name}` of every other workflow that runs on every
    pull request head."""
    found = {}
    for path in workflow_files(root):
        rel = f"{WORKFLOW_DIR}/{path.name}"
        if rel == OWN_WORKFLOW:
            continue
        text = path.read_text(encoding="utf-8")
        if runs_on_every_pr(text):
            found[rel] = workflow_name(text) or rel
    return found


# ----------------------------------------------------------------------
# Verdict
# ----------------------------------------------------------------------
def _describe(prefix, names):
    """`prefix: a, b, c`, cut to the status description limit with a count of
    what was left out."""
    names = sorted(set(names))
    for shown in range(len(names), 0, -1):
        text = f"{prefix}: {', '.join(names[:shown])}"
        if shown < len(names):
            text += f", +{len(names) - shown} more"
        if len(text) <= DESCRIPTION_LIMIT:
            return text
    return f"{prefix}: {len(names)} checks"[:DESCRIPTION_LIMIT]


def _workflow_path(run):
    return (run.get("path") or "").split("@", 1)[0]


def _newest(items, key):
    newest = {}
    for item in items:
        k = key(item)
        if k not in newest or item["id"] > newest[k]["id"]:
            newest[k] = item
    return newest


def _reading(status, conclusion):
    """`failure`, `pending` or `success` for one run."""
    if status != "completed":
        return "pending"
    if conclusion in FAILED:
        return "failure"
    if conclusion in GREEN:
        return "success"
    return "pending"


def verdict(check_runs, workflow_runs, always_run):
    """`(state, description)` for one head."""
    own_suites = {r.get("check_suite_id") for r in workflow_runs
                  if _workflow_path(r) == OWN_WORKFLOW}
    workflow_runs = [r for r in workflow_runs if _workflow_path(r) != OWN_WORKFLOW]
    check_runs = [c for c in check_runs
                  if (c.get("check_suite") or {}).get("id") not in own_suites]

    checks = _newest(check_runs, key=lambda c: c["name"])
    suites_with_checks = {(c.get("check_suite") or {}).get("id") for c in checks.values()}
    runs = _newest(workflow_runs, key=_workflow_path)

    readings = {"failure": [], "pending": []}
    for name, check in checks.items():
        reading = _reading(check.get("status"), check.get("conclusion"))
        if reading != "success":
            readings[reading].append(name)
    for run in runs.values():
        if run.get("check_suite_id") in suites_with_checks:
            continue
        reading = _reading(run.get("status"), run.get("conclusion"))
        if reading != "success":
            readings[reading].append(run.get("name") or _workflow_path(run))

    if readings["failure"]:
        return "failure", _describe("failed", readings["failure"])
    missing = [name for path, name in always_run.items() if path not in runs]
    if readings["pending"] or missing:
        parts = []
        if readings["pending"]:
            parts.append(_describe("waiting on", readings["pending"]))
        if missing:
            parts.append(_describe("not reported", missing))
        return "pending", "; ".join(parts)[:DESCRIPTION_LIMIT]
    return "success", f"{len(checks)} check{'' if len(checks) == 1 else 's'} passed"


# ----------------------------------------------------------------------
# Status read and write
# ----------------------------------------------------------------------
def current_status(api, repo, sha):
    """`(state, description)` of this head's `all-checks` status, or None."""
    for status in list_all(api, f"repos/{repo}/commits/{sha}/status", "statuses"):
        if status.get("context") == CONTEXT:
            return status.get("state"), status.get("description") or ""
    return None


def write_status(api, repo, sha, state, description, target_url):
    fields = {"state": state, "context": CONTEXT, "description": description}
    if target_url:
        fields["target_url"] = target_url
    api.post(f"repos/{repo}/statuses/{sha}", fields)


def reconcile(api, repo, sha, root, target_url=None):
    """Compute and post the verdict for `sha`. Returns the posted-or-kept
    `(state, description)`."""
    current = current_status(api, repo, sha)
    if current and current[0] == "success":
        current = ("pending", "recomputing")
        write_status(api, repo, sha, *current, target_url)
    check_runs = list_all(api, f"repos/{repo}/commits/{sha}/check-runs?filter=latest",
                          "check_runs")
    workflow_runs = list_all(api, f"repos/{repo}/actions/runs?head_sha={sha}",
                             "workflow_runs")
    computed = verdict(check_runs, workflow_runs, always_run_workflows(root))
    if computed != current:
        write_status(api, repo, sha, *computed, target_url)
    return computed


def main(argv=None, api=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", required=True, help="OWNER/NAME")
    parser.add_argument("--sha", required=True, help="the head commit, 40 hex digits")
    parser.add_argument("--root", default=".",
                        help="checkout of the default branch (workflow files are read here)")
    parser.add_argument("--target-url", default="", help="link the status points at")
    args = parser.parse_args(argv)
    if not SHA_RE.match(args.sha):
        parser.error(f"--sha must be 40 lowercase hex digits, got {args.sha!r}")
    if not re.match(r"^[\w.-]+/[\w.-]+$", args.repo):
        parser.error(f"--repo must be OWNER/NAME, got {args.repo!r}")
    if not (Path(args.root) / WORKFLOW_DIR).is_dir():
        parser.error(f"no {WORKFLOW_DIR} under --root {args.root}")
    try:
        state, description = reconcile(api or GhApi(), args.repo, args.sha, args.root,
                                       args.target_url)
    except ApiError as e:
        print(f"{CONTEXT}: {e}; no verdict written", file=sys.stderr)
        return 1
    print(f"{CONTEXT} {args.sha[:12]}: {state} — {description}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
