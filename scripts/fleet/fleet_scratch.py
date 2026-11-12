"""fleet-scratch: a pane's scratch directory, bounded deletes, and reaping its own orphans.

  fleet-scratch dir [<name>]     create (if absent) and print the pane's scratch
                                 directory, or a named child of it
  fleet-scratch rm <path>...     delete paths inside the pane's own roots
  fleet-scratch reap <pid>       stop one process the pane owns

The pane is the basename of the launcher-set FLEET_ASSIGNED_WORKTREE; nothing
else (no flag, no other variable) moves a root. Its roots:

  * the scratch root, /tmp/fleet-<pane> in the host's canonical spelling;
  * the pane's worktrees: the assigned one and the same-named worktree of the
    engine clone or of the game clone nested in it, where one exists.

`rm` deletes a path inside the scratch root when that root is a real directory
the caller owns, or inside a pane worktree when the path is under a top-level
build* directory, is ignored by git, or lies in a __pycache__ directory, and in
every worktree case holds no tracked file. A worktree root and any .git path
are never deletable. Each argument is made absolute, collapsed lexically, and
has every component but the last resolved through symlinks; containment is
tested on that result, and that result is what gets deleted. A final-component
symlink is unlinked, never followed. Every argument is checked before any is
deleted: one refusal deletes nothing.

`reap` signals a pid only when the caller's user owns it, its working directory
resolves inside a pane root, and it is not pid 1, the caller, or an ancestor of
the caller. It sends SIGTERM, waits up to 10 s, re-checks, then sends SIGKILL
only when the pid still names the process it first checked (same start time);
a pid recycled to another process is never signalled again.
A process started through fleet-jobs is stopped with `fleet-jobs kill`. On a
native-Windows host `reap` refuses: its two process tables number pids
independently.

Exit status: 0 done, 1 refused by a check (nothing done), 2 usage or identity
error (nothing done).

The tool runs outside the sandboxed runtime's sandbox (its allow entry becomes
an allow rule there), so these checks are the only boundary.
"""
import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import time
from dataclasses import dataclass

EXIT_OK = 0
EXIT_REFUSED = 1
EXIT_USAGE = 2

PROG = "fleet-scratch"
ASSIGNMENT_VAR = "FLEET_ASSIGNED_WORKTREE"
SAFE_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
GIT_TIMEOUT_S = 10.0
PROBE_TIMEOUT_S = 10.0
TERM_WAIT_S = 10.0
KILL_WAIT_S = 5.0
POLL_S = 0.1
# A walk up the parent chain stops here, so a cyclic or garbled process table
# cannot hang the tool.
MAX_ANCESTRY = 256

USAGE = __doc__.split("\n\n", 2)[1]


class Refusal(Exception):
    """A check refused an argument; nothing was done."""


class UsageError(Exception):
    """Bad arguments or no usable pane identity; nothing was done."""


def _native_windows():
    return os.name == "nt" or sys.platform in ("cygwin", "msys")


def _key(path):
    return os.path.normcase(path)


def is_within(path, root):
    """True when `path` is `root` or below it. Both are canonical absolute
    paths; the trailing separator keeps a prefix-sharing sibling out."""
    path_key, root_key = _key(path), _key(root)
    return path_key == root_key or path_key.startswith(root_key.rstrip(os.sep) + os.sep)


def native_spelling(path, nt=None):
    """Rewrite an MSYS drive path (/c/Users/x) to the drive form a
    native-Windows interpreter resolves (c:/Users/x). Inert elsewhere."""
    if nt is None:
        nt = os.name == "nt"
    match = re.match(r"^/([A-Za-z])(/.*)?$", path) if nt else None
    if match:
        return f"{match.group(1)}:{match.group(2) or '/'}"
    return path


def canonical(path, cwd=None):
    """Absolute, lexically collapsed, symlinks resolved on every component but
    the last. The last component is kept as spelled, so a symlink there names
    the link itself."""
    absolute = os.path.normpath(os.path.join(cwd or os.getcwd(), path))
    parent, leaf = os.path.split(absolute)
    if not leaf:
        return absolute
    return os.path.join(os.path.realpath(parent), leaf)


def tmp_root(nt=None, run=subprocess.run):
    """The host's canonical spelling of /tmp, or None when it cannot be found.
    A native-Windows interpreter launched from MSYS2 bash receives /tmp/...
    arguments already rewritten to the drive spelling of MSYS2's /tmp, which
    cygpath reports."""
    if nt is None:
        nt = os.name == "nt"
    if not nt:
        return os.path.realpath("/tmp")
    try:
        out = run(["cygpath", "-m", "/tmp"], capture_output=True, text=True,
                  timeout=PROBE_TIMEOUT_S, check=True).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    return os.path.realpath(out) if out else None


@dataclass(frozen=True)
class Pane:
    name: str
    scratch_root: str | None
    worktrees: tuple[str, ...]

    def roots(self):
        return ((self.scratch_root,) if self.scratch_root else ()) + self.worktrees


def _twin_worktrees(assigned, name):
    """The assigned worktree plus the same-named worktree of the clone it sits
    in, of the game clone nested in that clone, and of the engine clone when
    the assignment is itself a game worktree. Existing directories only."""
    found = [assigned]
    parts = assigned.split(os.sep)
    if len(parts) >= 3 and parts[-2] == "worktrees" and parts[-3] == ".claude":
        clone = os.sep.join(parts[:-3])
        candidates = [os.path.join(clone, "creations", "game")]
        game_suffix = os.sep + os.path.join("creations", "game")
        if _key(clone).endswith(_key(game_suffix)):
            candidates.append(clone[: -len(game_suffix)])
        for other in candidates:
            wt = os.path.realpath(os.path.join(other, ".claude", "worktrees", name))
            if os.path.isdir(wt) and wt not in found:
                found.append(wt)
    return tuple(found)


def pane_from_env(environ=None, tmp=None):
    """The pane named by FLEET_ASSIGNED_WORKTREE. Raises UsageError when the
    variable is unset or does not name a worktree directory."""
    environ = os.environ if environ is None else environ
    raw = environ.get(ASSIGNMENT_VAR, "")
    if not raw:
        raise UsageError(f"{ASSIGNMENT_VAR} is unset; fleet-scratch runs only in a "
                         "launcher-assigned fleet pane")
    assigned = native_spelling(raw)
    if not os.path.isabs(assigned):
        raise UsageError(f"{ASSIGNMENT_VAR}={raw!r} is not an absolute path")
    assigned = os.path.realpath(assigned)
    name = os.path.basename(assigned)
    if not SAFE_NAME_RE.match(name):
        raise UsageError(f"{ASSIGNMENT_VAR}={raw!r} has no usable pane name")
    if not os.path.isdir(assigned):
        raise UsageError(f"{ASSIGNMENT_VAR}={raw!r} is not a directory")
    tmp = tmp_root() if tmp is None else tmp
    scratch = os.path.join(tmp, f"fleet-{name}") if tmp else None
    return Pane(name=name, scratch_root=scratch, worktrees=_twin_worktrees(assigned, name))


# ---------------------------------------------------------------------------
# dir
# ---------------------------------------------------------------------------

def _check_private_dir(path):
    """Accept an existing real directory the caller owns. Anything else (a
    symlink, a file, another user's directory, nothing) is refused, since /tmp
    is shared and anyone may pre-create the predictable name."""
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        raise Refusal(f"'{path}' does not exist") from None
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        raise Refusal(f"'{path}' exists and is not a directory")
    if hasattr(os, "getuid") and info.st_uid != os.getuid():
        raise Refusal(f"'{path}' exists and belongs to another user")


def _ensure_private_dir(path):
    """Create `path` owner-only, or accept what `_check_private_dir` accepts."""
    try:
        os.mkdir(path, 0o700)
        return
    except FileExistsError:
        pass
    _check_private_dir(path)


def cmd_dir(pane, name=None):
    if not pane.scratch_root:
        raise UsageError("this host's /tmp could not be located")
    if name is not None and not SAFE_NAME_RE.match(name):
        raise UsageError(f"'{name}' is not a single plain directory name")
    _ensure_private_dir(pane.scratch_root)
    target = pane.scratch_root if name is None else os.path.join(pane.scratch_root, name)
    if name is not None:
        _ensure_private_dir(target)
    print(target)
    return EXIT_OK


# ---------------------------------------------------------------------------
# rm
# ---------------------------------------------------------------------------

def _git(worktree, *args):
    """Run git in `worktree` with no inherited GIT_* redirection."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    return subprocess.run(["git", "-C", worktree, *args], capture_output=True,
                          env=env, stdin=subprocess.DEVNULL, timeout=GIT_TIMEOUT_S)


def _worktree_reason(worktree, target):
    """Why `target` (strictly inside `worktree`) may be deleted, or raise."""
    rel = os.path.relpath(target, worktree)
    parts = rel.split(os.sep)
    if ".git" in parts:
        raise Refusal("a .git path is never deletable")
    try:
        if parts[0].startswith("build"):
            reason = "build directory"
        elif "__pycache__" in parts:
            reason = "__pycache__"
        else:
            ignored = _git(worktree, "check-ignore", "-q", "--", rel)
            if ignored.returncode == 1:
                raise Refusal("not under build*/, not ignored by git, not a __pycache__")
            if ignored.returncode != 0:
                raise Refusal("git check-ignore failed")
            reason = "ignored by git"
        # check-ignore takes plain paths; ls-files takes pathspecs, so a glob
        # character in a name would otherwise widen the match.
        tracked = _git(worktree, "--literal-pathspecs", "ls-files", "-z", "--", rel)
    except (OSError, subprocess.SubprocessError) as exc:
        raise Refusal(f"git could not be run in '{worktree}': {exc}") from exc
    if tracked.returncode != 0:
        raise Refusal("git ls-files failed")
    if tracked.stdout:
        first = tracked.stdout.split(b"\0", 1)[0].decode(errors="replace")
        raise Refusal(f"holds a tracked file ({first})")
    return reason


def check_rm_target(pane, arg, cwd=None):
    """The canonical path to delete for `arg`, or raise Refusal."""
    target = canonical(arg, cwd)
    if pane.scratch_root and is_within(target, pane.scratch_root):
        _check_private_dir(pane.scratch_root)
        return target
    for worktree in pane.worktrees:
        if not is_within(target, worktree):
            continue
        if _key(target) == _key(worktree):
            raise Refusal("a worktree root is never deletable")
        _worktree_reason(worktree, target)
        return target
    raise Refusal(f"resolves to '{target}', outside the pane's scratch root and worktrees")


def _delete(target):
    info = os.lstat(target)
    if stat.S_ISDIR(info.st_mode):
        shutil.rmtree(target)
    else:
        os.unlink(target)


def cmd_rm(pane, args, cwd=None):
    if not args:
        raise UsageError("rm needs at least one path")
    targets, refused = [], []
    for arg in args:
        try:
            targets.append(check_rm_target(pane, arg, cwd))
        except Refusal as exc:
            refused.append(f"{PROG}: refused '{arg}': {exc}")
    if refused:
        for line in refused:
            print(line, file=sys.stderr)
        print(f"{PROG}: nothing deleted", file=sys.stderr)
        return EXIT_REFUSED
    status = EXIT_OK
    for target in targets:
        if not os.path.lexists(target):
            print(f"absent {target}")
            continue
        try:
            _delete(target)
        except OSError as exc:
            print(f"{PROG}: could not delete '{target}': {exc}", file=sys.stderr)
            status = EXIT_REFUSED
            continue
        print(f"removed {target}")
    return status


# ---------------------------------------------------------------------------
# reap
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class ProcessFacts:
    pid: int
    uid: int
    ruid: int
    ppid: int
    zombie: bool
    cwd: str | None
    started: str | None


def _read_process_linux(pid):
    try:
        with open(f"/proc/{pid}/status", encoding="utf-8") as fh:
            fields = dict(line.split(":", 1) for line in fh if ":" in line)
    except FileNotFoundError:
        return None
    uids = fields["Uid"].split()
    try:
        cwd = os.readlink(f"/proc/{pid}/cwd")
    except OSError:
        cwd = None
    try:
        with open(f"/proc/{pid}/stat", encoding="utf-8") as fh:
            stat = fh.read()
        # The comm field may hold spaces and ')'; fields after the last ')' start at
        # field 3 (state), so starttime (field 22) is index 19.
        started = stat[stat.rindex(")") + 2:].split()[19]
    except (OSError, ValueError, IndexError):
        started = None
    return ProcessFacts(pid=pid, uid=int(uids[1]), ruid=int(uids[0]),
                        ppid=int(fields["PPid"]), zombie=fields["State"].strip().startswith("Z"),
                        cwd=cwd, started=started)


def _read_process_ps(pid):
    try:
        ps = subprocess.run(["/bin/ps", "-o", "uid=,ruid=,ppid=,stat=,lstart=", "-p", str(pid)],
                            capture_output=True, text=True, timeout=PROBE_TIMEOUT_S)
    except (OSError, subprocess.SubprocessError):
        return None
    fields = ps.stdout.split()
    if ps.returncode != 0 or len(fields) < 4:
        return None
    started = " ".join(fields[4:]) or None
    cwd = None
    try:
        lsof = subprocess.run(["/usr/sbin/lsof", "-a", "-p", str(pid), "-d", "cwd", "-Fn"],
                              capture_output=True, text=True, timeout=PROBE_TIMEOUT_S)
        names = [line[1:] for line in lsof.stdout.splitlines() if line.startswith("n")]
        if lsof.returncode == 0 and len(names) == 1:
            cwd = names[0]
    except (OSError, subprocess.SubprocessError):
        pass
    return ProcessFacts(pid=pid, uid=int(fields[0]), ruid=int(fields[1]), ppid=int(fields[2]),
                        zombie=fields[3].startswith("Z"), cwd=cwd, started=started)


def read_process(pid):
    """Facts for `pid`, or None when no such process exists."""
    if os.path.isdir("/proc/self"):
        return _read_process_linux(pid)
    return _read_process_ps(pid)


def _ancestors(pid, reader):
    chain, seen = [], set()
    while pid > 1 and pid not in seen and len(chain) < MAX_ANCESTRY:
        seen.add(pid)
        facts = reader(pid)
        if facts is None:
            break
        chain.append(facts.ppid)
        pid = facts.ppid
    return chain


def check_reap_target(pane, pid, reader=read_process, my_pid=None, my_uid=None):
    """The facts of a process `reap` may signal, or raise Refusal."""
    my_pid = os.getpid() if my_pid is None else my_pid
    my_uid = os.getuid() if my_uid is None else my_uid
    if pid == 1:
        raise Refusal("pid 1 is never reaped")
    if pid == my_pid:
        raise Refusal("that is this process")
    facts = reader(pid)
    if facts is None or facts.zombie:
        raise Refusal("no such process")
    if facts.uid != my_uid or facts.ruid != my_uid:
        raise Refusal(f"owned by uid {facts.uid}, not the caller's uid {my_uid}")
    if pid in _ancestors(my_pid, reader):
        raise Refusal("that is an ancestor of this process")
    if not facts.started:
        raise Refusal("its start time could not be read, so a recycled pid could not be told apart")
    if not facts.cwd:
        raise Refusal("its working directory could not be read")
    cwd = os.path.realpath(facts.cwd)
    if not any(is_within(cwd, root) for root in pane.roots()):
        raise Refusal(f"its working directory '{cwd}' is outside the pane's scratch root "
                      "and worktrees")
    return facts


def _wait_gone(target, reader, deadline_s, sleep, clock):
    """True once `target` has exited; a pid now naming a newer process counts as exited."""
    end = clock() + deadline_s
    while True:
        facts = reader(target.pid)
        if facts is None or facts.zombie or facts.started != target.started:
            return True
        if clock() >= end:
            return False
        sleep(POLL_S)


def cmd_reap(pane, pid_arg, reader=read_process, kill=os.kill, sleep=time.sleep,
             clock=time.monotonic, my_pid=None, my_uid=None):
    if _native_windows():
        print(f"{PROG}: refused '{pid_arg}': reap is not supported on a native-Windows host "
              "(its two process tables number pids independently)", file=sys.stderr)
        return EXIT_REFUSED
    if not re.fullmatch(r"[0-9]+", pid_arg or "") or int(pid_arg) < 1:
        raise UsageError(f"'{pid_arg}' is not a process id")
    pid = int(pid_arg)
    checks = {"reader": reader, "my_pid": my_pid, "my_uid": my_uid}
    try:
        target = check_reap_target(pane, pid, **checks)
        kill(pid, signal.SIGTERM)
        if _wait_gone(target, reader, TERM_WAIT_S, sleep, clock):
            print(f"reaped {pid} (SIGTERM)")
            return EXIT_OK
        # Re-checked so a pid recycled during the wait is not killed, even by a
        # replacement that passes every ownership and cwd check.
        if check_reap_target(pane, pid, **checks).started != target.started:
            raise Refusal("the pid now names a different process; it was not signalled again")
        kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        print(f"reaped {pid}")
        return EXIT_OK
    except Refusal as exc:
        print(f"{PROG}: refused '{pid_arg}': {exc}", file=sys.stderr)
        return EXIT_REFUSED
    if _wait_gone(target, reader, KILL_WAIT_S, sleep, clock):
        print(f"reaped {pid} (SIGKILL)")
        return EXIT_OK
    print(f"{PROG}: pid {pid} survived SIGKILL", file=sys.stderr)
    return EXIT_REFUSED


# ---------------------------------------------------------------------------
# entry
# ---------------------------------------------------------------------------

def main(argv=None, environ=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__.strip())
        return EXIT_OK if argv else EXIT_USAGE
    verb, rest = argv[0], argv[1:]
    try:
        if verb not in ("dir", "rm", "reap"):
            raise UsageError(f"unknown verb '{verb}'")
        pane = pane_from_env(environ)
        if verb == "dir":
            if len(rest) > 1:
                raise UsageError("dir takes at most one name")
            return cmd_dir(pane, rest[0] if rest else None)
        if verb == "rm":
            return cmd_rm(pane, rest)
        if len(rest) != 1:
            raise UsageError("reap takes exactly one pid")
        return cmd_reap(pane, rest[0])
    except UsageError as exc:
        print(f"{PROG}: {exc}\nusage:\n{USAGE}", file=sys.stderr)
        return EXIT_USAGE
    except Refusal as exc:
        print(f"{PROG}: refused: {exc}", file=sys.stderr)
        return EXIT_REFUSED


if __name__ == "__main__":
    sys.exit(main())
