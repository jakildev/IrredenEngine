"""A fake macOS display session for the fleet_display_session seams.

``FakeDisplay(root)`` writes a probe state file ("<online> <awake> <NSScreen>"),
a fake probe that logs each call and prints that state, and a fake caffeinate
that logs each invocation, optionally rewrites the state (the wake), and then
lives until the ``-w`` pid exits, as caffeinate does. ``env()`` returns the
seam variables that point the helper at them. It also pins the sandbox seam
(default "allowed"): unpinned, a suite run inside a sandbox would get the
kernel's "denied" and stop every arm at the sandbox check.
"""

import os
import shlex
import subprocess
import sys
import time
from pathlib import Path

CAFFEINATE = r"""import os, sys, time
args = sys.argv[1:]
with open(os.environ["FAKE_CAFFEINATE_LOG"], "a") as log:
    log.write(f"{os.getpid()} {' '.join(args)}\n")
wake = os.environ.get("FAKE_WAKE_TO")
if wake:
    with open(os.environ["FAKE_DISPLAY_STATE"] + ".tmp", "w") as state:
        state.write(wake)
    os.replace(os.environ["FAKE_DISPLAY_STATE"] + ".tmp", os.environ["FAKE_DISPLAY_STATE"])
owner = int(args[args.index("-w") + 1])
while True:
    try:
        os.kill(owner, 0)
    except ProcessLookupError:
        break
    time.sleep(0.05)
"""
# Paths are baked in: the helper runs the probe without passing its env.
PROBE = r"""import sys
with open({log!r}, "a") as log:
    log.write("probe\n")
with open({state!r}) as state:
    sys.stdout.write(state.read())
"""


def gone(pid):
    """True once pid has exited; a zombie holds no assertion either."""
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    except PermissionError:
        return False
    state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True,
                           text=True).stdout.strip()
    return not state or state.startswith("Z")


def wait_gone(pid, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if gone(pid):
            return True
        time.sleep(0.05)
    return False


class FakeDisplay:
    def __init__(self, root, state="1 0 1", wake_to="1 1 1"):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.state = self.root / "display-state"
        self.log = self.root / "caffeinate.log"
        self.caffeinate = self.root / "caffeinate"
        self.caffeinate.write_text(f"#!{sys.executable}\n{CAFFEINATE}")
        self.caffeinate.chmod(0o755)
        self.probe_log = self.root / "probe.log"
        self.probe = self.root / "probe"
        probe = PROBE.format(log=str(self.probe_log), state=str(self.state))
        self.probe.write_text(f"#!{sys.executable}\n{probe}")
        self.probe.chmod(0o755)
        self.wake_to = wake_to
        self.set_state(state)

    def set_state(self, state):
        self.state.write_text(state)

    def env(self, platform="darwin", timeout="2", sandbox="allowed"):
        env = {"FLEET_DISPLAY_PLATFORM": platform,
               "FLEET_DISPLAY_SANDBOX": sandbox,
               "FLEET_DISPLAY_CAFFEINATE": str(self.caffeinate),
               "FLEET_DISPLAY_PROBE": shlex.quote(str(self.probe)),
               "FLEET_DISPLAY_WAKE_TIMEOUT": timeout,
               "FAKE_CAFFEINATE_LOG": str(self.log),
               "FAKE_DISPLAY_STATE": str(self.state)}
        if self.wake_to:
            env["FAKE_WAKE_TO"] = self.wake_to
        return env

    def probes(self):
        """How many times the fake probe ran."""
        return len(self.probe_log.read_text().splitlines()) if self.probe_log.exists() else 0

    def invocations(self):
        """[(pid, argv)] for every fake caffeinate started."""
        if not self.log.exists():
            return []
        rows = []
        for line in self.log.read_text().splitlines():
            pid, _, args = line.partition(" ")
            rows.append((int(pid), args.split()))
        return rows
