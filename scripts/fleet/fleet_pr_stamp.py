#!/usr/bin/env python3
"""Apply and verify a pull request's author-runtime and author-host labels.

    fleet-pr-stamp <PR> --repo <r> [--runtime claude|codex]
        Stamp fleet:author-<runtime> and fleet:authored-on-<host>, then read
        both back. The author-side gate commit-and-push runs after PR open.
    fleet-pr-stamp <PR> --repo <r> --infer
        Stamp fleet:author-claude when the head commit carries the Claude
        co-author trailer. Codex work carries no trailer, so nothing is
        inferred from its absence. No host label: the author host is not
        inferable from another host.
    fleet-pr-stamp --backfill --repo <r>
        --infer over every open PR that carries no fleet:author-* label.

Exit 0: stamped or already stamped. 1: a label write or read-back failed.
2: usage. 3: --infer found no Claude co-author trailer; nothing written.
"""

import argparse
import json
import os
import platform
import re
import subprocess
import sys

import fleet_github
import fleet_runtime

AUTHOR_LABELS = {f"fleet:author-{runtime}" for runtime in fleet_runtime.RUNTIMES}
REVIEWER_ROLES = ("sonnet-reviewer", "opus-reviewer", "smoke-worker")
CLAUDE_TRAILER = re.compile(r"^co-authored-by:.*<noreply@anthropic\.com>\s*$",
                            re.IGNORECASE | re.MULTILINE)
BACKFILL_LIST_LIMIT = "200"
EXIT_NOT_INFERABLE = 3


def host_name(kernel):
    if kernel == "Linux":
        return "linux"
    if kernel == "Darwin":
        return "macos"
    if kernel == "Windows" or kernel.startswith(("MINGW", "MSYS", "CYGWIN")):
        return "windows"
    return None


def print_error(prefix, error):
    detail = getattr(error, "stderr", b"") or str(error)
    if isinstance(detail, bytes):
        detail = detail.decode("utf-8", "replace")
    print(f"fleet-pr-stamp: {prefix}: {detail.strip()}", file=sys.stderr)


def infer_runtime(message):
    return "claude" if CLAUDE_TRAILER.search(message or "") else None


def _pr_fields(pr, repo, fields):
    proc = fleet_runtime.github_call(["pr", "view", pr, "--repo", repo, "--json", fields])
    if proc.returncode != 0:
        print_error(f"{pr}: PR read failed", proc)
        return None
    try:
        return json.loads(proc.stdout or b"{}")
    except json.JSONDecodeError as exc:
        print_error(f"{pr}: invalid PR read", exc)
        return None


def _label_names(view):
    try:
        return {label["name"] for label in view.get("labels", [])}
    except (KeyError, TypeError, AttributeError):
        return None


def _head_commit_message(repo, sha):
    proc = fleet_github.run(["api", f"repos/{repo}/commits/{sha}"],
                            capture_output=True, timeout=30)
    if proc.returncode != 0:
        print_error(f"head commit {sha[:7]} read failed", proc)
        return None
    try:
        return json.loads(proc.stdout or b"{}")["commit"]["message"]
    except (json.JSONDecodeError, KeyError, TypeError) as exc:
        print_error(f"invalid head commit {sha[:7]} read", exc)
        return None


def _read_back(pr, repo, expected, host, report=True):
    view = _pr_fields(pr, repo, "labels")
    labels = _label_names(view) if view is not None else None
    if labels is None:
        print_error(f"{pr}: invalid label read-back", "no labels field")
        return 1
    if report:
        print(json.dumps(sorted(label for label in labels if label.startswith("fleet:author")),
                         separators=(",", ":")))
    failed = False
    actual = AUTHOR_LABELS.intersection(labels)
    if actual != expected:
        print(f"fleet-pr-stamp: expected author labels {sorted(expected)}, saw {sorted(actual)}",
              file=sys.stderr)
        failed = True
    if host and f"fleet:authored-on-{host}" not in labels:
        print(f"fleet-pr-stamp: missing fleet:authored-on-{host}", file=sys.stderr)
        failed = True
    return 1 if failed else 0


def stamp_and_verify(pr, repo, runtime, kernel):
    failed = False
    try:
        fleet_runtime.stamp(pr, repo, runtime)
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print_error("author label write failed", exc)
        failed = True

    host = host_name(kernel)
    resolved_repo = fleet_runtime.resolve_repo(repo)
    if host:
        args = ["pr", "edit", pr, "--repo", resolved_repo,
                "--add-label", f"fleet:authored-on-{host}"]
        proc = fleet_runtime.github_call(args)
        if proc.returncode != 0:
            print_error("host label write failed", proc)
            failed = True

    rc = _read_back(pr, resolved_repo, {f"fleet:author-{runtime}"}, host)
    return 1 if failed else rc


def infer_and_stamp(pr, repo, report=True):
    """Stamp from the head commit's trailer. The live label read first keeps a
    backfill from overwriting a stamp the author wrote after the PR was listed."""
    resolved_repo = fleet_runtime.resolve_repo(repo)
    view = _pr_fields(pr, resolved_repo, "labels,headRefOid")
    labels = _label_names(view) if view is not None else None
    if labels is None or not view.get("headRefOid"):
        print_error(f"{pr}: invalid PR read", "labels and headRefOid expected")
        return 1
    present = AUTHOR_LABELS.intersection(labels)
    if present:
        return _read_back(pr, resolved_repo, present, None, report)
    sha = view["headRefOid"]
    message = _head_commit_message(resolved_repo, sha)
    if message is None:
        return 1
    runtime = infer_runtime(message)
    if runtime is None:
        print(f"fleet-pr-stamp: {pr}: no Claude co-author trailer on head commit {sha[:7]}; "
              "provenance stays the author's to stamp", file=sys.stderr)
        return EXIT_NOT_INFERABLE
    try:
        fleet_runtime.stamp(pr, resolved_repo, runtime)
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print_error(f"{pr}: author label write failed", exc)
        return 1
    return _read_back(pr, resolved_repo, {f"fleet:author-{runtime}"}, None, report)


def backfill(repo):
    resolved_repo = fleet_runtime.resolve_repo(repo)
    proc = fleet_runtime.github_call(
        ["pr", "list", "--repo", resolved_repo, "--state", "open",
         "--limit", BACKFILL_LIST_LIMIT, "--json", "number,labels"])
    if proc.returncode != 0:
        print_error("open PR list failed", proc)
        return 1
    try:
        rows = json.loads(proc.stdout or b"[]")
    except json.JSONDecodeError as exc:
        print_error("invalid open PR list", exc)
        return 1
    failed = False
    for row in rows:
        labels = _label_names(row)
        if labels is None or labels & AUTHOR_LABELS:
            continue
        pr = str(row.get("number", ""))
        rc = infer_and_stamp(pr, resolved_repo, report=False)
        if rc == 0:
            print(f"{pr}: stamped")
        elif rc == EXIT_NOT_INFERABLE:
            print(f"{pr}: skipped")
        else:
            failed = True
    return 1 if failed else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pr", nargs="?")
    parser.add_argument("--repo", required=True)
    parser.add_argument("--runtime", choices=fleet_runtime.RUNTIMES)
    parser.add_argument("--infer", action="store_true",
                        help="stamp from the head commit's Claude co-author trailer")
    parser.add_argument("--backfill", action="store_true",
                        help="--infer over every open PR with no fleet:author-* label")
    args = parser.parse_args(argv)
    if args.backfill:
        if args.pr or args.runtime or args.infer:
            parser.error("--backfill takes --repo only")
    else:
        if not args.pr or not args.pr.isdigit() or int(args.pr) < 1:
            parser.error("PR number must be a positive integer")
        if args.infer and args.runtime:
            parser.error("--infer and --runtime are exclusive")
    if os.environ.get("FLEET_ROLE", "") in REVIEWER_ROLES:
        parser.error("reviewers must not change author provenance")
    try:
        fleet_runtime.resolve_repo(args.repo)
    except ValueError as exc:
        parser.error(str(exc))
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(newline="\n")
    if args.backfill:
        return backfill(args.repo)
    if args.infer:
        return infer_and_stamp(args.pr, args.repo)
    runtime = args.runtime or os.environ.get("FLEET_RUNTIME") or "claude"
    if runtime not in fleet_runtime.RUNTIMES:
        parser.error("runtime must be claude or codex")
    kernel = os.environ.get("FLEET_PR_STAMP_KERNEL") or platform.system()
    return stamp_and_verify(args.pr, args.repo, runtime, kernel)


if __name__ == "__main__":
    sys.exit(main())
