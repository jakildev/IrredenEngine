"""Codex CLI transport for a target-bound fleet iteration or interactive architect."""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from fleet_codex_doctor import probe
from fleet_codex_policy import check, prepare
from fleet_runtime import CODEX_MODELS, atomic_json

ROOT = Path(__file__).resolve().parents[2]
ROLES = ("worker", "sonnet-reviewer", "opus-reviewer", "smoke-worker", "opus-architect")


def prompt(role, mode, target, worktree=None):
    if role not in ROLES or mode not in ("live", "dry-run", "review-only"):
        raise ValueError("unsupported Codex role or mode")
    role_path = (worktree or ROOT) / ".claude" / "commands" / f"role-{role}.md"
    if not role_path.is_file():
        raise ValueError(f"missing role instructions: {role_path}")
    if role == "opus-architect":
        return (f"Read AGENTS.md, docs/agents/CODEX.md, and {role_path}. "
                "You are the human-driven Codex architect using Astra at xhigh effort. "
                "Read the rendering skills linked from CODEX.md and inspect current open PRs "
                "before proposing work. Wait for the human to choose the rendering problem. "
                "Do not claim queue work or start implementation without a concrete assignment.")
    return (
        f"Read AGENTS.md and docs/agents/CODEX.md, then {role_path}. "
        f"Execute that role in {mode} mode. Runtime is codex, not Claude. "
        f"Your assigned target is {target or 'the human-driven architect conversation'}. "
        "Follow FLEET-RUNTIME.md's target and completion contracts. "
        "Do not discover or claim a different item. Read the complete issue/PR thread. "
        "Use the available Codex tools for referenced Claude tool names. "
        "For every PR you create or amend, run fleet-runtime stamp <PR> --repo <owner/repo> "
        "--runtime codex after pushing and before requesting review. "
        "Use the real model/runtime in authorship. Preserve human merge authority. "
        "A permissions failure is a blocked operation: report the exact command and "
        "reason through the completion contract; do not repeatedly retry it. "
        "Finish the assigned workflow and return a final response so this iteration exits."
    )


def command(model, effort, worktree, writable, task_prompt, resume="", interactive=False):
    if effort not in ("low", "medium", "high", "xhigh", "max"):
        raise ValueError("unsupported Codex effort")
    args = ["codex"]
    if not interactive:
        args += ["exec"]
    args += ["-m", model, "-c", f'model_reasoning_effort="{effort}"',
             "-c", 'approval_policy="on-request"' if interactive else 'approval_policy="never"',
             "-c", 'sandbox_mode="workspace-write"',
             "-c", "sandbox_workspace_write.network_access=true",
             "-c", 'forced_login_method="chatgpt"',
             "-c", "sandbox_workspace_write.writable_roots=" + json.dumps(writable)]
    if not interactive:
        args += ["--json"]
    if resume:
        args += ["resume", resume]
    else:
        args += ["-C", str(worktree)]
    if not (interactive and resume):
        args += [task_prompt]
    return args


def _git_dirs(checkout):
    """(per-worktree gitdir, common gitdir) for a checkout, both absolute.

    The sandbox carves every `.git` path out of a writable root as read-only
    unless an explicit root names that exact path, and for a linked worktree
    it resolves the `.git` pointer file and protects the resolved gitdir
    (`.git/worktrees/<name>`) too. Listing only the common dir therefore
    still leaves `index.lock` / `FETCH_HEAD` unwritable — every git write
    from the worktree fails with "Operation not permitted" — so both
    directories are named explicitly.
    """
    out = subprocess.run(["git", "-C", str(checkout), "rev-parse", "--path-format=absolute",
                          "--absolute-git-dir", "--git-common-dir"],
                         check=True, capture_output=True, text=True, timeout=10).stdout.splitlines()
    if len(out) != 2 or not all(out):
        raise ValueError(f"expected worktree and common Git directories for {checkout}")
    return tuple(str(Path(path).resolve()) for path in out)


def writable_roots(worktree, state):
    gitdir, common = _git_dirs(worktree)
    roots = [str(worktree), gitdir, common, str(state)]
    for name in ("sessions", "reservations", "claims", "molecules", "locks",
                 "feedback", "plans", "logs", "alerts", "heartbeats",
                 "iteration-summaries", "amend-snapshots", "orphans"):
        key = "FLEET_" + name.upper().replace("-", "_") + "_DIR"
        roots.append(str(Path(os.environ.get(key) or str(Path.home() / ".fleet" / name)).resolve()))
    for key in ("FLEET_CODEX_SIDECAR",):
        if os.environ.get(key):
            roots.append(str(Path(os.environ[key]).resolve().parent))
    cache = Path(os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache"))
    roots.append(str((cache / "irreden").resolve()))
    if os.environ.get("IRREDEN_BUILD_DIR"):
        roots.append(str(Path(os.environ["IRREDEN_BUILD_DIR"]).resolve()))
    if os.environ.get("IR_LOCK_ROOT"):
        roots.append(str(Path(os.environ["IR_LOCK_ROOT"]).resolve()))
    elif os.environ.get("XDG_RUNTIME_DIR"):
        roots.append(str(Path(os.environ["XDG_RUNTIME_DIR"]) / "irreden/locks"))
    # The worker's downstream twin is its only other editable source checkout.
    twin = Path(common).parent / "creations/game/.claude/worktrees" / worktree.name
    if twin.is_dir():
        roots.append(str(twin.resolve()))
        roots.extend(_git_dirs(twin))
        # ir_default_build_dir puts a creation's build outside its source tree.
        roots.append(str((Path(common).parent / f"build-game-{worktree.name}").resolve()))
    return list(dict.fromkeys(roots))


def observe(event, sidecar, state, model):
    kind = event.get("type")
    if kind == "thread.started":
        sid = event.get("thread_id", "")
        if not re.fullmatch(r"[a-zA-Z0-9_-]+", sid):
            raise ValueError("invalid Codex thread ID")
        data = json.loads(sidecar.read_text()) if sidecar.exists() else {}
        data.update(session_id=sid, runtime="codex")
        atomic_json(sidecar, data)
    elif kind == "item.completed":
        item = event.get("item", {})
        if item.get("type") == "agent_message":
            print(item.get("text", ""), flush=True)
    elif kind == "turn.completed":
        atomic_json(state / "codex-usage" / (sidecar.stem + ".json"),
                    {"model": model, "observed_at": int(time.time()),
                     "usage": event.get("usage", {})})
    if kind in ("error", "turn.failed"):
        detail = event.get("error", event.get("message", event))
        message = json.dumps(detail)
        print(f"codex: {message}", file=sys.stderr, flush=True)
        if re.search(r"usage.limit|quota|rate.limit|429", message, re.I):
            atomic_json(state / "runtime-cooldown" / "codex.json",
                        {"until": int(time.time()) + 900, "reason": message})
        return True
    return False


def run(args):
    worktree = Path.cwd().resolve()
    if worktree.parent.name != "worktrees" or worktree.parent.parent.name != ".claude":
        raise ValueError("Codex fleet sessions require a dedicated fleet worktree")
    state = Path(os.environ.get("FLEET_STATE_DIR") or str(Path.home() / ".fleet/state")).resolve()
    if args.doctor:
        policy = prepare(worktree, args.role)
        count = check(policy, args.role)
        checks = probe(worktree, writable_roots(worktree, state))
        print(json.dumps({"policy_checks": count, "sandbox_writes": checks}, indent=2))
        return 0
    if args.prepare or args.check:
        policy = prepare(worktree, args.role)
        if args.check:
            print(f"fleet-codex: {check(policy, args.role)} command-policy checks passed")
        else:
            print(policy)
        return 0
    target = os.environ.get("FLEET_DISPATCH_TARGET", "")
    if args.role != "opus-architect" and not target:
        raise ValueError("Codex transient session requires an explicit dispatch target")
    sidecar = Path(os.environ.get("FLEET_CODEX_SIDECAR", str(state / "codex-architect.json")))
    argv = command(args.model, args.effort, worktree, writable_roots(worktree, state),
                   prompt(args.role, args.mode, target, worktree), args.resume, args.interactive)
    if args.print_launch:
        print(json.dumps(argv))
        return 0
    if not shutil.which("codex"):
        raise ValueError("codex CLI is not installed")
    prepare(worktree, args.role)
    if not args.interactive:
        try:
            probe(worktree, writable_roots(worktree, state))
        except (ValueError, OSError, subprocess.SubprocessError) as exc:
            # The dispatcher already respects the provider cooldown and the
            # wrapper preserves resumable sessions for exit 2. Fail before
            # spending a model iteration discovering the same host defect.
            atomic_json(state / "runtime-cooldown/codex.json",
                        {"until": int(time.time()) + 900, "kind": "permissions",
                         "worktree": str(worktree), "reason": str(exc)})
            print(f"fleet-codex: preflight blocked; no model launched: {exc}", file=sys.stderr)
            return 2
    env = dict(os.environ, FLEET_RUNTIME="codex", FLEET_ROLE=args.role,
               FLEET_ASSIGNED_WORKTREE=str(worktree))
    # Saved CLI subscription login is intentional; never inherit an API billing override.
    env.pop("CODEX_API_KEY", None)
    env.pop("OPENAI_API_KEY", None)
    if args.interactive:
        return subprocess.call(argv, env=env)
    log_dir = state / "codex-events"
    log_dir.mkdir(parents=True, exist_ok=True)
    failed = False
    completed = False
    with (log_dir / f"{sidecar.stem}-{time.time_ns()}.jsonl").open("w") as log, \
            tempfile.TemporaryFile(mode="w+") as errors:
        proc = subprocess.Popen(argv, env=env, stdout=subprocess.PIPE, stderr=errors, text=True)
        try:
            for line in proc.stdout:
                log.write(line)
                log.flush()
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    print(line, end="", file=sys.stderr)
                    continue
                completed = completed or event.get("type") == "turn.completed"
                failed = observe(event, sidecar, state, args.model) or failed
            rc = proc.wait()
            errors.seek(0)
            stderr = errors.read()
            if stderr:
                print(stderr, end="", file=sys.stderr)
                if rc:
                    failed = observe({"type": "error", "message": stderr},
                                     sidecar, state, args.model) or failed
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    return rc or int(failed or not completed)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=ROLES, default="opus-architect")
    parser.add_argument("--model", default=CODEX_MODELS["fable"])
    parser.add_argument("--effort", default="xhigh")
    parser.add_argument("--mode", default="live")
    parser.add_argument("--resume", default="")
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--print-launch", action="store_true")
    parser.add_argument("--prepare", action="store_true",
                        help="write this worktree's role permissions")
    parser.add_argument("--check", action="store_true", help="prepare and verify command decisions")
    parser.add_argument("--doctor", action="store_true",
                        help="check policy and actual sandbox writes without a model call")
    try:
        return run(parser.parse_args(argv))
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print(f"fleet-codex: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
