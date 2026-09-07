"""Provider selection is independent of queue class and host capability."""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

RUNTIMES = ("claude", "codex")
CODEX_MODELS = {"fable": "gpt-6-astra", "opus": "gpt-5.6-sol", "sonnet": "gpt-5.6-terra"}
ROLE_CLASSES = {"sonnet-reviewer": "sonnet", "opus-reviewer": "opus", "smoke-worker": "sonnet"}
TARGET_RECORDS = {
    "task": ("tasks_open",), "stack": ("tasks_open",),
    "plan": ("needs_plan",), "review": ("candidate_prs", "flagged_prs"),
    "planreview": ("plan_review",), "smoke": ("smoke_pending_prs",),
    "feedback": ("feedback_prs",), "conflict": ("semantic_conflict_prs",),
}


def atomic_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(data, stream)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
    finally:
        Path(temp).unlink(missing_ok=True)


def routing_problem(state, key, message=None):
    """Warn once, then maintain an alert after three identical route failures."""
    tag = hashlib.sha256(key.encode()).hexdigest()[:16]
    path = Path(state) / "runtime-problems" / (tag + ".json")
    alerts = Path(os.environ.get("FLEET_ALERTS_DIR", str(Path(state).parent / "alerts")))
    alert = alerts / ("fleet-runtime-" + tag)
    if message is None:
        path.unlink(missing_ok=True)
        alert.unlink(missing_ok=True)
        return
    previous = json.loads(path.read_text()) if path.exists() else {}
    count = previous.get("count", 0) + 1 if previous.get("reason") == message else 1
    data = dict(key=key, reason=message, count=count, observed_at=int(time.time()))
    atomic_json(path, data)
    if count >= 3:
        atomic_json(alert, data)
    if count in (1, 3):
        suffix = " (alert recorded)" if count == 3 else ""
        print(f"fleet-runtime: {key}: {message}{suffix}", file=sys.stderr)


def target_record(data, target):
    parts = target.split(":")
    if len(parts) not in (3, 4) or parts[0] not in TARGET_RECORDS:
        raise ValueError("unknown dispatch target")
    kind, repo, number = parts[:3]
    if repo not in ("engine", "game") or not number.isdigit() or int(number) < 1:
        raise ValueError("invalid dispatch target")
    for key in TARGET_RECORDS[kind]:
        for record in data.get(key) or []:
            n = str(record.get("number", record.get("issue", record.get("id", "")))).lstrip("#")
            if n == number and (record.get("repo") or "engine") == repo:
                return kind, record
    raise ValueError("target missing from projection")


def choose_runtime(kind, record, target, env):
    available = sorted({x.strip() for x in env.get("FLEET_RUNTIMES", "claude").split(",")})
    if not available or any(x not in RUNTIMES for x in available):
        raise ValueError("FLEET_RUNTIMES must contain claude and/or codex")
    labels = {x["name"] if isinstance(x, dict) else x for x in record.get("labels") or []}
    authors = [x for x in RUNTIMES if f"fleet:author-{x}" in labels]
    if len(authors) > 1:
        raise ValueError("conflicting author runtime labels")
    if kind == "review" and env.get("FLEET_CROSS_PROVIDER_REVIEW", "1") == "1":
        if not authors:
            raise ValueError("unstamped PR: record implementation provider before review")
        chosen = "claude" if authors == ["codex"] else "codex"
    else:
        pins = [x for x in RUNTIMES if f"fleet:runtime-{x}" in labels]
        if len(pins) > 1:
            raise ValueError("conflicting runtime pins")
        policy = env.get("FLEET_WORKER_RUNTIME", "balanced")
        if pins:
            chosen = pins[0]
        elif kind in ("feedback", "conflict") and authors:
            chosen = authors[0]
        elif policy in RUNTIMES:
            chosen = policy
        elif policy == "balanced":
            # Stable across hosts and restarts; local counters would race or skew.
            index = int.from_bytes(hashlib.sha256(target.encode()).digest()[:4], "big")
            chosen = available[index % len(available)]
        else:
            raise ValueError("FLEET_WORKER_RUNTIME must be balanced, claude, or codex")
    if chosen not in available:
        raise ValueError(f"required runtime {chosen} unavailable on this host")
    return chosen


def route(data, target, role, cls, model, effort, env):
    kind, record = target_record(data, target)
    runtime = choose_runtime(kind, record, target, env)
    cls = cls or ROLE_CLASSES.get(role, "opus")
    if cls not in CODEX_MODELS:
        raise ValueError("unknown queue class")
    if runtime == "codex":
        model = env.get(f"FLEET_CODEX_MODEL_{cls.upper()}", CODEX_MODELS[cls])
        explicit = record.get("effort")
        effort = explicit or env.get(f"FLEET_CODEX_EFFORT_{cls.upper()}",
                                    "xhigh" if cls == "fable" else "medium")
        if role == "opus-reviewer" and not explicit:
            effort = env.get("FLEET_CODEX_EFFORT_REVIEW", "high")
    if not re.fullmatch(r"[a-zA-Z0-9_.:/\[\]-]+", model):
        raise ValueError("invalid model identifier")
    if effort not in ("low", "medium", "high", "xhigh", "max"):
        raise ValueError("unsupported reasoning effort")
    return runtime, cls, model, effort


def stamp(pr, repo, runtime):
    if not pr.isdigit() or int(pr) < 1 or runtime not in RUNTIMES:
        raise ValueError("expected positive PR number and supported runtime")
    if os.environ.get("FLEET_ROLE", "") in ("sonnet-reviewer", "opus-reviewer", "smoke-worker"):
        raise ValueError("reviewers must not change author provenance")
    subprocess.run(["gh", "pr", "edit", pr, "--repo", repo,
                    "--remove-label", f"fleet:author-{'claude' if runtime == 'codex' else 'codex'}",
                    "--add-label", f"fleet:author-{runtime}"], check=True, timeout=30)


def ready(state):
    path = Path(state) / "runtime-cooldown/codex.json"
    if path.exists() and json.loads(path.read_text()).get("until", 0) > time.time():
        return False
    return True


def resume_route(path, env):
    """Restore a reserved Codex assignment without consulting a newer queue."""
    path = Path(path)
    if not path.exists():
        return None  # Legacy Claude reservations did not require a sidecar.
    data = json.loads(path.read_text())
    if data.get("runtime", "claude") == "claude":
        return None
    available = env.get("FLEET_RUNTIMES", "claude").split(",")
    if data.get("runtime") != "codex" or "codex" not in available:
        raise ValueError("reserved runtime unavailable")
    target = data.get("target", "")
    if not re.fullmatch(r"(?:task|plan|feedback|conflict):(?:engine|game):[1-9][0-9]*"
                        r"|stack:(?:engine|game):[1-9][0-9]*:[1-9][0-9]*", target):
        raise ValueError("invalid reserved assignment")
    cls, model, effort = (data.get(key, "") for key in ("class", "model", "effort"))
    if cls not in CODEX_MODELS or effort not in ("low", "medium", "high", "xhigh", "max"):
        raise ValueError("invalid reserved class or effort")
    if not re.fullmatch(r"[a-zA-Z0-9_.:/\[\]-]+", model):
        raise ValueError("invalid reserved model")
    return "codex", cls, model, effort, target


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    p = subs.add_parser("route")
    for name in ("slice", "target", "role", "cls", "model", "effort"):
        p.add_argument(name)
    p = subs.add_parser("stamp", help="record the provider that last authored this PR")
    p.add_argument("pr")
    p.add_argument("--repo", required=True)
    p.add_argument("--runtime", choices=RUNTIMES, default=os.environ.get("FLEET_RUNTIME", "claude"))
    p = subs.add_parser("ready", help="check the Codex provider cooldown")
    p.add_argument("state")
    p = subs.add_parser("resume-route", help="restore a reserved provider assignment")
    p.add_argument("sidecar")
    args = parser.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(newline="\n")
    state = os.environ.get("FLEET_STATE_DIR")
    key = args.command + ":" + getattr(args, "target", getattr(args, "sidecar", ""))
    try:
        if args.command == "resume-route":
            result = resume_route(args.sidecar, os.environ)
            if result is None:
                return 2
            if state:
                routing_problem(state, key)
            print(" ".join(result))
            return 0
        if args.command == "ready":
            return 0 if ready(args.state) else 1
        if args.command == "stamp":
            stamp(args.pr, args.repo, args.runtime)
        else:
            data = json.loads(Path(args.slice).read_text())
            result = route(data, args.target, args.role, args.cls,
                           args.model, args.effort, os.environ)
            if state:
                routing_problem(state, key)
            print(" ".join(result))
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        if state and args.command in ("route", "resume-route"):
            try:
                routing_problem(state, key, str(exc))
            except (OSError, ValueError):
                print(f"fleet-runtime: {exc}", file=sys.stderr)
        else:
            print(f"fleet-runtime: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
