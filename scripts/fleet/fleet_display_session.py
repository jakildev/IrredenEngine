"""Wake a sleeping macOS display and hold it awake for a display-backed run.

The engine discovers monitors only when CoreGraphics reports an awake online
display and AppKit reports an Aqua ``NSScreen``. A display that idled to sleep
reads ``awake=0``, so an unattended capture finds 0 monitors. The owner of a
display-backed run (``ir-run``, a verifier, the ``fleet-jobs`` supervisor)
calls ``hold_display`` before its first launch: one
``caffeinate -d -u -w <owner-pid>`` declares user activity (which wakes the
display) and holds ``PreventUserIdleDisplaySleep`` until the owner exits, even
when the owner is SIGKILLed. ``HOLDER_ENV`` carries the holder's pid to the
owner's children, so a nested owner reuses the hold instead of starting a
second one.

A process inside a Seatbelt sandbox that denies the WindowServer lookup (a
Codex shell command that missed its allow rule) cannot reach any display, so
the helper refuses it first: before the wake, the holder and the probe, which
aborts there.

CLI (for ir-run):
  fleet_display_session.py hold --owner-pid PID [--prefix TEXT]
    exit 0  ready; stdout is the new holder's pid, empty when an inherited
            hold or a non-macOS host made one unnecessary
    exit 69 not ready; stderr names the one condition that stayed unmet
            and ends with the failure line

Test seams (environment):
  FLEET_DISPLAY_PLATFORM      replaces sys.platform
  FLEET_DISPLAY_SANDBOX       replaces the kernel query: "denied", "allowed",
                              or "error" (the query could not answer)
  FLEET_DISPLAY_CAFFEINATE    the caffeinate executable
  FLEET_DISPLAY_PROBE         command printing "<online> <awake> <NSScreen>"
  FLEET_DISPLAY_WAKE_TIMEOUT  seconds to wait for readiness (default 15)
"""

import argparse
import ctypes
import os
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from fleet_codex_doctor import display_counts  # noqa: E402

HOLDER_ENV = "FLEET_DISPLAY_HOLDER"
EXIT_ENV_FAIL = 69
DEFAULT_WAKE_TIMEOUT = 15.0
POLL = 0.25
PROBE_TIMEOUT = 10
CAFFEINATE = "/usr/bin/caffeinate"
WINDOWSERVER_SERVICE = b"com.apple.windowserver.active"
SANDBOX_FILTER_GLOBAL_NAME = 2
# Each is a host state the tooling cannot change; the message names it
# without guessing the physical cause (closed lid, lock, sign-out, unplug).
CONDITIONS = {
    "none": "no online CoreGraphics display",
    "asleep": "the online display stayed asleep",
    "no-aqua": "an awake CoreGraphics display but no Aqua NSScreen",
    "probe-failed": "the CoreGraphics/AppKit display probe failed",
    "sandboxed": "the launching process runs inside a sandbox that denies the "
                 "WindowServer lookup (mach-lookup com.apple.windowserver.active)",
}
# What the agent that hit the condition can do about it.
REMEDIES = {
    "sandboxed": "launch the command bare and allow-listed (no redirection, compound "
                 "statement, subshell, `cd ... &&`, or env/VAR= prefix), or as `fleet-jobs "
                 "start render-verify|light-verify|gui-verify` (docs/agents/CODEX.md "
                 "\"Capability qualification\")",
}


def platform_name(env=os.environ):
    return env.get("FLEET_DISPLAY_PLATFORM") or sys.platform


def _sandbox_check(*args):
    """libSystem's sandbox_check(pid, operation, filter, ...): 1 denied, 0 allowed, -1 error.

    None when the symbol cannot be loaded. The function is variadic: the fixed
    argtypes cover pid, operation and filter, and a filter argument rides as
    the extra (variadic) argument.
    """
    try:
        check = ctypes.CDLL("/usr/lib/libSystem.B.dylib").sandbox_check
    except (OSError, AttributeError):
        return None
    check.restype = ctypes.c_int
    check.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    return check(os.getpid(), *args)


def in_any_sandbox():
    """True when this process runs under any Seatbelt profile (macOS only)."""
    return _sandbox_check(None, 0) == 1


def windowserver_denied(env=os.environ):
    """True only when this process's sandbox denies the WindowServer lookup.

    A query that errors or cannot load counts as not denied, so the launch
    falls through to the wake and probe path.
    """
    seam = env.get("FLEET_DISPLAY_SANDBOX")
    if seam:
        return seam == "denied"
    return _sandbox_check(b"mach-lookup", SANDBOX_FILTER_GLOBAL_NAME,
                          ctypes.c_char_p(WINDOWSERVER_SERVICE)) == 1


def condition(counts):
    """'ok' or the CONDITIONS key the (online, awake, NSScreen) counts fail."""
    online, awake, screens = counts
    if online < 1:
        return "none"
    if awake < 1:
        return "asleep"
    if screens < 1:
        return "no-aqua"
    return "ok"


def read_counts(env=os.environ):
    command = env.get("FLEET_DISPLAY_PROBE")
    if not command:
        return display_counts(platform="darwin", timeout=PROBE_TIMEOUT)
    result = subprocess.run(shlex.split(command), capture_output=True, text=True,
                            timeout=PROBE_TIMEOUT)
    try:
        online, awake, screens = (int(value) for value in result.stdout.split())
    except ValueError as exc:
        raise ValueError(f"invalid display probe output {result.stdout.strip()!r}") from exc
    return online, awake, screens


def _probe(env):
    """(condition, counts-or-None, error-or-None)."""
    try:
        counts = read_counts(env)
    except (OSError, subprocess.SubprocessError, ValueError) as exc:
        return "probe-failed", None, str(exc)
    return condition(counts), counts, None


def _describe(counts, error):
    if counts is None:
        return f"probe error: {error}"
    online, awake, screens = counts
    return f"CoreGraphics online={online} awake={awake} NSScreen={screens}"


def _alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        pass
    return True


@dataclass
class Hold:
    ready: bool
    condition: str
    lines: list = field(default_factory=list)
    holder: subprocess.Popen = None

    @property
    def holder_pid(self):
        return self.holder.pid if self.holder else None

    @property
    def blocks_launch(self):
        """A probe that cannot run is not proof the engine cannot see a display."""
        return not self.ready and self.condition != "probe-failed"

    @property
    def failure(self):
        text = f"{self.lines[-1]}; the engine would discover 0 display monitors"
        remedy = REMEDIES.get(self.condition)
        return f"{text} — {remedy}" if remedy else text

    def release(self):
        """End the hold this call started; an inherited hold is the caller's."""
        if self.holder is None or self.holder.poll() is not None:
            return
        self.holder.terminate()
        try:
            self.holder.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.holder.kill()
            self.holder.wait()


def inherited_holder(env=os.environ):
    value = env.get(HOLDER_ENV, "")
    return int(value) if value.isdigit() and _alive(int(value)) else None


def hold_display(owner_pid, env=os.environ, clock=time.monotonic, sleep=time.sleep):
    """Wake the display, hold it awake for owner_pid's lifetime, and wait for it.

    Off macOS this does nothing and reports ready. Not ready means the wait
    ended on the condition in ``Hold.condition``; the caller must still
    ``release()`` (a no-op when no holder was started).
    """
    if platform_name(env) != "darwin":
        return Hold(True, "ok")
    if windowserver_denied(env):
        return Hold(False, "sandboxed", [f"display unreachable: {CONDITIONS['sandboxed']}"])
    inherited = inherited_holder(env)
    state, counts, error = _probe(env)
    if inherited is not None:
        lines = [f"display held by the outer run (caffeinate pid {inherited}): "
                 f"{_describe(counts, error)}"]
        if state != "ok":
            lines.append(f"display not ready under the outer run's hold: {CONDITIONS[state]} "
                         f"({_describe(counts, error)})")
        return Hold(state == "ok", state, lines)
    lines = [f"display before wake: {_describe(counts, error)}"]
    caffeinate = env.get("FLEET_DISPLAY_CAFFEINATE") or CAFFEINATE
    argv = [caffeinate, "-d", "-u", "-w", str(owner_pid)]
    try:
        holder = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, env=dict(env))
    except OSError as exc:
        lines.append(f"display wake not requested: cannot start {caffeinate}: {exc}")
        return Hold(state == "ok", state, lines)
    lines.append(f"display wake requested: {' '.join(argv)} (pid {holder.pid}) holds "
                 "PreventUserIdleDisplaySleep for the run")
    wait = float(env.get("FLEET_DISPLAY_WAKE_TIMEOUT") or DEFAULT_WAKE_TIMEOUT)
    started = clock()
    while True:
        state, counts, error = _probe(env)
        waited = clock() - started
        if state == "ok":
            lines.append(f"display ready after {waited:.1f}s: {_describe(counts, error)}")
            return Hold(True, state, lines, holder)
        if holder.poll() is not None:
            lines.append(f"display holder exited early (exit {holder.returncode})")
            break
        # Waiting cannot repair a probe that cannot run.
        if state == "probe-failed" or waited >= wait:
            break
        sleep(POLL)
    lines.append(f"display not ready after requesting a wake and waiting {waited:.1f}s: "
                 f"{CONDITIONS[state]} ({_describe(counts, error)})")
    return Hold(False, state, lines, holder)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="verb", required=True)
    hold = sub.add_parser("hold")
    hold.add_argument("--owner-pid", type=int, required=True)
    hold.add_argument("--prefix", default="")
    args = parser.parse_args(argv)
    result = hold_display(args.owner_pid)
    if not result.blocks_launch:
        for line in result.lines:
            print(f"{args.prefix}{line}", file=sys.stderr)
        print(result.holder_pid or "")
        return 0
    result.release()
    for line in [*result.lines[:-1], result.failure]:
        print(f"{args.prefix}{line}", file=sys.stderr)
    return EXIT_ENV_FAIL


if __name__ == "__main__":
    sys.exit(main())
