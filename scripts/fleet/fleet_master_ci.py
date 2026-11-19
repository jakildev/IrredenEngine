#!/usr/bin/env python3
"""GitHub-backed incident tracking for workflows that are red on master."""

from __future__ import annotations

import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

import fleet_github
import fleet_public_text

ENGINE_REPO = "jakildev/IrredenEngine"
TRACKER_LABEL = "fleet:master-red"
ALERT_PREFIX = "scout-master-ci-"
RUN_MARKER_RE = re.compile(
    r"<!-- fleet-master-ci run=(\d+) conclusion=(red|green) -->"
)
WORKFLOW_MARKER_RE = re.compile(r"<!-- fleet-master-ci workflow=([^ ]+) -->")
SUITES_ANNOTATION_TITLE = "fleet-tests failed suites"
GH_TIMEOUT = 30


class MasterCiError(RuntimeError):
    """A read or write miss that must leave the master-CI lane due."""


def alert_path(alerts_dir: Path, workflow: str) -> Path:
    return Path(alerts_dir) / f"{ALERT_PREFIX}{workflow}"


def render_alert(workflow: str, run: dict, issue: int) -> str:
    def field(key: str) -> str:
        return str(run.get(key) or "unknown").replace("\n", " ")

    return (
        f"master CI red: {workflow}\n"
        f"workflow={workflow}\n"
        f"run={field('databaseId')}\n"
        f"sha={field('headSha')}\n"
        f"created_at={field('createdAt')}\n"
        f"url={field('url')}\n"
        f"issue={issue}\n"
    )


def parse_alert(text: str) -> dict:
    lines = text.splitlines()
    if not lines or not lines[0].startswith("master CI red: "):
        raise ValueError("missing master-CI alert header")
    fields = {}
    for line in lines[1:]:
        key, separator, value = line.partition("=")
        if separator:
            fields[key] = value
    workflow = fields.get("workflow")
    if not workflow:
        raise ValueError("missing workflow")
    issue = fields.get("issue")
    if issue:
        try:
            fields["issue"] = int(issue)
        except ValueError as exc:
            raise ValueError("invalid issue") from exc
    else:
        fields["issue"] = None
    return fields


def read_alerts(alerts_dir: Path) -> dict[str, dict]:
    alerts = {}
    try:
        paths = Path(alerts_dir).glob(f"{ALERT_PREFIX}*")
        for path in paths:
            payload = parse_alert(path.read_text(encoding="utf-8"))
            alerts[payload["workflow"]] = payload
    except (OSError, UnicodeDecodeError, ValueError):
        return {}
    return alerts


def _run(args: list[str]) -> subprocess.CompletedProcess:
    try:
        result = fleet_github.run(
            args, capture_output=True, text=True, timeout=GH_TIMEOUT
        )
    except (OSError, subprocess.SubprocessError, ValueError) as exc:
        raise MasterCiError(f"gh {' '.join(args[:2])}: {type(exc).__name__}: {exc}") from exc
    if result.returncode != 0:
        detail = (result.stderr or "").strip()[:300]
        raise MasterCiError(f"gh {' '.join(args[:2])}: exit {result.returncode}: {detail}")
    return result


def _json(args: list[str]):
    result = _run(args)
    try:
        return json.loads(result.stdout)
    except (TypeError, ValueError) as exc:
        raise MasterCiError(f"gh {' '.join(args[:2])}: unreadable JSON: {exc}") from exc


def _rest_list(endpoint: str, key: str | None = None) -> list[dict]:
    pages = _json(["api", "--paginate", "--slurp", endpoint])
    if not isinstance(pages, list):
        raise MasterCiError(f"{endpoint}: unexpected REST page shape")
    rows = []
    for page in pages:
        if key is None:
            values = page
        elif isinstance(page, dict):
            values = page.get(key)
        else:
            values = None
        if not isinstance(values, list) or any(not isinstance(row, dict) for row in values):
            raise MasterCiError(f"{endpoint}: unexpected REST row shape")
        rows.extend(values)
    return rows


def failed_run_details(run_id: int) -> dict:
    jobs = _rest_list(
        f"repos/{ENGINE_REPO}/actions/runs/{run_id}/jobs?per_page=100", "jobs"
    )
    failed = [job for job in jobs if job.get("conclusion") == "failure"]
    suites = []
    for job in failed:
        job_id = job.get("id")
        if not isinstance(job_id, int):
            raise MasterCiError("failed job is missing its id")
        annotations = _rest_list(
            f"repos/{ENGINE_REPO}/check-runs/{job_id}/annotations?per_page=100"
        )
        for annotation in annotations:
            if annotation.get("title") == SUITES_ANNOTATION_TITLE:
                suites.extend(str(annotation.get("message") or "").split())
    return {
        "failed_jobs": [str(job.get("name") or "unknown") for job in failed],
        "failed_suites": suites,
        "has_skipped_job": any(job.get("conclusion") == "skipped" for job in jobs),
    }


def _workflow_marker(workflow: str) -> str:
    return f"<!-- fleet-master-ci workflow={workflow} -->"


def _run_marker(run: dict, conclusion: str) -> str:
    return f"<!-- fleet-master-ci run={int(run['databaseId'])} conclusion={conclusion} -->"


def _evidence(workflow: str, run: dict, details: dict, conclusion: str) -> str:
    sha = str(run.get("headSha") or "unknown")
    url = str(run.get("url") or "unknown")
    marker = _run_marker(run, conclusion)
    if conclusion == "green":
        return f"{marker}\n\nMaster is green at commit `{sha}`. Run: {url}\n"
    jobs = details.get("failed_jobs") or []
    suites = details.get("failed_suites") or []
    lines = [
        marker,
        "",
        f"`{workflow}` is red on master at commit `{sha}`.",
        f"Run: {url}",
        f"Failed jobs: {', '.join(f'`{name}`' for name in jobs) if jobs else '(none reported)'}",
    ]
    if suites:
        lines.append(f"Failed Fleet Tests suites: {', '.join(f'`{suite}`' for suite in suites)}")
    else:
        lines.append("Failed Fleet Tests suites: no suite annotation was reported.")
    return "\n".join(lines) + "\n"


def _tracker_body(workflow: str, run: dict, details: dict) -> str:
    return "\n".join([
        _workflow_marker(workflow),
        _evidence(workflow, run, details, "red").rstrip(),
        "",
        "## Resolution",
        "",
        "Fix the named failure in a pull request carrying `Closes #<this issue>`.",
        "A later green run is evidence, not resolution; only that pull request "
        "closes this incident.",
        "",
        "**Area:** master workflow " + workflow,
        "**Model:** opus",
        "",
    ])


def _public_text(body: str, git_root: Path) -> None:
    findings = fleet_public_text.find_public_text(
        body, "engine", git_root=git_root
    )
    if findings:
        detail = ", ".join(fleet_public_text.format_finding(row) for row in findings)
        raise MasterCiError(f"public-text gate rejected tracker text: {detail}")


def _send_body(args: list[str], body: str, git_root: Path) -> subprocess.CompletedProcess:
    _public_text(body, git_root)
    path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", suffix=".md", delete=False
        ) as handle:
            handle.write(body)
            path = handle.name
        return _run([*args, "--body-file", path])
    finally:
        if path:
            try:
                os.unlink(path)
            except OSError:
                pass


def _trackers(workflow: str) -> list[dict]:
    endpoint = (
        f"repos/{ENGINE_REPO}/issues?state=all&labels={TRACKER_LABEL}"
        "&per_page=100&sort=created&direction=asc"
    )
    marker = _workflow_marker(workflow)
    return [
        issue for issue in _rest_list(endpoint)
        if "pull_request" not in issue and marker in str(issue.get("body") or "")
    ]


def _comments(number: int) -> list[dict]:
    return _rest_list(
        f"repos/{ENGINE_REPO}/issues/{number}/comments?per_page=100"
    )


def _events(issue: dict, comments: list[dict]) -> list[tuple[int, str]]:
    events = []
    texts = [str(issue.get("body") or "")]
    texts.extend(str(comment.get("body") or "") for comment in comments)
    for text in texts:
        events.extend((int(run_id), conclusion)
                      for run_id, conclusion in RUN_MARKER_RE.findall(text))
    return events


def _create_tracker(workflow: str, run: dict, details: dict, git_root: Path) -> int:
    result = _send_body([
        "issue", "create", "--repo", ENGINE_REPO,
        "--title", f"master red: {workflow}",
        "--label", TRACKER_LABEL,
        "--label", "fleet:agent-approved",
        "--label", "fleet:no-plan",
    ], _tracker_body(workflow, run, details), git_root)
    url = (result.stdout or "").strip().rstrip("/")
    try:
        return int(url.rsplit("/", 1)[1])
    except (IndexError, ValueError) as exc:
        raise MasterCiError(f"issue create returned no issue number: {url!r}") from exc


def _comment(number: int, body: str, git_root: Path) -> None:
    _send_body([
        "issue", "comment", str(number), "--repo", ENGINE_REPO,
    ], body, git_root)


def post_public_comment(number: int, body: str, git_root: Path) -> None:
    """Post engine issue/PR text after the public-repository gate passes."""
    _comment(number, body, git_root)


def upsert_tracker(
    workflow: str,
    run: dict,
    details: dict,
    conclusion: str,
    git_root: Path,
    logger=None,
) -> int | None:
    """Create or advance the workflow tracker and return its open issue number."""
    trackers = _trackers(workflow)
    open_trackers = [issue for issue in trackers if issue.get("state") == "open"]
    closed_trackers = [issue for issue in trackers if issue.get("state") == "closed"]
    open_events = []
    open_tracker = open_trackers[0] if open_trackers else None
    if len(open_trackers) > 1 and logger:
        logger(
            f"master-ci: {workflow} has {len(open_trackers)} open trackers; "
            f"using oldest #{open_tracker['number']}"
        )
    if open_tracker:
        open_events = _events(open_tracker, _comments(int(open_tracker["number"])))
    closed_events = []
    for issue in closed_trackers:
        closed_events.extend(_events(issue, _comments(int(issue["number"]))))

    run_id = int(run["databaseId"])
    if run_id in {event[0] for event in closed_events}:
        return int(open_tracker["number"]) if open_tracker else None
    if conclusion == "red":
        if open_tracker is None:
            return _create_tracker(workflow, run, details, git_root)
        number = int(open_tracker["number"])
        if run_id not in {event[0] for event in open_events}:
            _comment(number, _evidence(workflow, run, details, "red"), git_root)
        return number
    if conclusion == "green" and open_tracker and open_events:
        number = int(open_tracker["number"])
        if open_events[-1][1] == "red":
            _comment(number, _evidence(workflow, run, details, "green"), git_root)
        return number
    return int(open_tracker["number"]) if open_tracker else None
