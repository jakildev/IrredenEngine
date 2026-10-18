#!/usr/bin/env python3
"""Apply and verify a pull request's author-runtime and author-host labels."""

import argparse
import json
import os
import platform
import subprocess
import sys

import fleet_runtime

AUTHOR_LABELS = {f"fleet:author-{runtime}" for runtime in fleet_runtime.RUNTIMES}
REVIEWER_ROLES = ("sonnet-reviewer", "opus-reviewer", "smoke-worker")


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

    view_args = ["pr", "view", pr, "--repo", resolved_repo, "--json", "labels"]
    proc = fleet_runtime.github_call(view_args)
    if proc.returncode != 0:
        print_error("label read-back failed", proc)
        return 1
    try:
        labels = [label["name"] for label in json.loads(proc.stdout or b"{}").get("labels", [])]
    except (json.JSONDecodeError, KeyError, TypeError) as exc:
        print_error("invalid label read-back", exc)
        return 1

    report = [label for label in labels if label.startswith("fleet:author")]
    print(json.dumps(report, separators=(",", ":")))
    expected = {f"fleet:author-{runtime}"}
    actual = AUTHOR_LABELS.intersection(labels)
    if actual != expected:
        print(f"fleet-pr-stamp: expected author labels {sorted(expected)}, saw {sorted(actual)}",
              file=sys.stderr)
        failed = True
    if host and f"fleet:authored-on-{host}" not in labels:
        print(f"fleet-pr-stamp: missing fleet:authored-on-{host}", file=sys.stderr)
        failed = True
    return 1 if failed else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pr")
    parser.add_argument("--repo", required=True)
    parser.add_argument("--runtime", choices=fleet_runtime.RUNTIMES)
    args = parser.parse_args(argv)
    if not args.pr.isdigit() or int(args.pr) < 1:
        parser.error("PR number must be a positive integer")
    if os.environ.get("FLEET_ROLE", "") in REVIEWER_ROLES:
        parser.error("reviewers must not change author provenance")
    try:
        fleet_runtime.resolve_repo(args.repo)
    except ValueError as exc:
        parser.error(str(exc))
    runtime = args.runtime or os.environ.get("FLEET_RUNTIME") or "claude"
    if runtime not in fleet_runtime.RUNTIMES:
        parser.error("runtime must be claude or codex")
    kernel = os.environ.get("FLEET_PR_STAMP_KERNEL") or platform.system()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(newline="\n")
    return stamp_and_verify(args.pr, args.repo, runtime, kernel)


if __name__ == "__main__":
    sys.exit(main())
