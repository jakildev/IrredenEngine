#!/usr/bin/env python3
"""Minimal coreutils-`timeout` stand-in for hosts that ship neither.

Installed to ~/bin/timeout by scripts/fleet/install.sh (step c) ONLY when
neither coreutils `timeout` nor `gtimeout` resolves, so fleet-net.sh's
`timeout <secs> <cmd...>` guard has a runner. It implements just the slice the
fleet uses: a leading duration in seconds, then the command to run.

`--version` advertises "coreutils" so fleet-net.sh's probe
(`timeout --version | grep -qi coreutils`) accepts this shim exactly as it
accepts the real thing — the probe exists to reject Windows System32
timeout.exe, and this shim is a genuine command runner, so it should pass.

Exit codes mirror coreutils: 124 on timeout, 125 on shim usage error, 126/127
for exec failures, 128+signal if the child is killed by a signal.
"""

import signal
import subprocess
import sys

# Seconds to wait after SIGTERM before escalating to SIGKILL, matching the
# spirit of coreutils `timeout -k`: a child wedged on a black-holed socket may
# ignore SIGTERM, and the whole point of the guard is that it cannot hang.
KILL_GRACE_SECONDS = 5.0


def _parse_duration(raw):
    """Parse a coreutils-style duration. The fleet only ever passes plain
    integer seconds; also accept a trailing s/m/h/d suffix for parity."""
    units = {"s": 1, "m": 60, "h": 3600, "d": 86400}
    mult = 1
    if raw and raw[-1] in units:
        mult = units[raw[-1]]
        raw = raw[:-1]
    try:
        return float(raw) * mult
    except ValueError:
        sys.stderr.write(f"timeout-shim: invalid duration '{raw}'\n")
        sys.exit(125)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stderr.write("usage: timeout <duration>[smhd] <command> [arg...]\n")
        return 0 if argv[:1] in (["-h"], ["--help"]) else 125
    if argv[0] == "--version":
        sys.stdout.write("fleet timeout-shim (coreutils-compatible) 1.0\n")
        return 0

    duration = _parse_duration(argv[0])
    cmd = argv[1:]
    if not cmd:
        sys.stderr.write("timeout-shim: missing command\n")
        return 125

    try:
        proc = subprocess.Popen(cmd)
    except FileNotFoundError:
        sys.stderr.write(f"timeout-shim: {cmd[0]}: command not found\n")
        return 127
    except OSError as exc:
        sys.stderr.write(f"timeout-shim: {cmd[0]}: {exc}\n")
        return 126

    try:
        rc = proc.wait(timeout=duration)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=KILL_GRACE_SECONDS)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        return 124

    # coreutils reports a signal-killed child as 128+signum.
    if rc < 0:
        return 128 + (-rc)
    return rc


if __name__ == "__main__":
    # Restore default SIGPIPE so a downstream `| head` closing the pipe doesn't
    # raise inside the shim (fleet log pipelines do this).
    try:
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (AttributeError, ValueError):
        pass  # SIGPIPE is absent on Windows — nothing to reset.
    sys.exit(main(sys.argv[1:]))
