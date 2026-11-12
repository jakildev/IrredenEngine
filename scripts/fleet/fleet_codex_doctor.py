"""Exercise configured Codex sandbox writes without calling a model."""

import json
import subprocess
import sys

# Run inside the same OS sandbox as the worker. Access checks like os.access
# cannot detect Seatbelt/Landlock restrictions; actual create/delete can.
PROBE = '''
import json, pathlib, sys, tempfile
results = []
for value in json.loads(sys.argv[1]):
    path = pathlib.Path(value)
    try:
        path.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="fleet-permission-probe-", dir=path) as temp:
            probe = pathlib.Path(temp) / "write"
            probe.write_text("sandbox write probe")
            probe.rename(probe.with_name("renamed"))
        results.append({"path": value, "ok": True})
    except OSError as exc:
        results.append({"path": value, "ok": False, "reason": str(exc)})
print(json.dumps(results))
sys.exit(int(any(not result["ok"] for result in results)))
'''


DISPLAY_PROBE = '''
import ctypes, ctypes.util
cg = ctypes.CDLL(ctypes.util.find_library("CoreGraphics"))
objc = ctypes.CDLL(ctypes.util.find_library("objc"))
ctypes.CDLL(ctypes.util.find_library("AppKit"))
ids = (ctypes.c_uint32 * 16)()
count = ctypes.c_uint32(0)
err = cg.CGGetOnlineDisplayList(16, ids, ctypes.byref(count))
if err:
    print(-abs(err), -abs(err), -abs(err))
else:
    cg.CGDisplayIsAsleep.argtypes = [ctypes.c_uint32]
    cg.CGDisplayIsAsleep.restype = ctypes.c_bool
    awake = sum(not cg.CGDisplayIsAsleep(ids[index]) for index in range(count.value))
    objc.objc_getClass.argtypes = [ctypes.c_char_p]
    objc.objc_getClass.restype = ctypes.c_void_p
    objc.sel_registerName.argtypes = [ctypes.c_char_p]
    objc.sel_registerName.restype = ctypes.c_void_p
    objc.objc_msgSend.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    objc.objc_msgSend.restype = ctypes.c_void_p
    screens = objc.objc_msgSend(objc.objc_getClass(b"NSScreen"),
                                objc.sel_registerName(b"screens"))
    objc.objc_msgSend.restype = ctypes.c_ulong
    screen_count = objc.objc_msgSend(screens, objc.sel_registerName(b"count"))
    print(count.value, awake, screen_count)
'''


def classify_display(online, awake, screens):
    if online < 1:
        return "none"
    if awake < 1 or screens < 1:
        return "mismatch"
    return "ok"


def display_counts(platform=sys.platform, timeout=30):
    """Return CoreGraphics, awake, and NSScreen counts on macOS."""
    if platform != "darwin":
        return None
    result = subprocess.run([sys.executable, "-c", DISPLAY_PROBE],
                            capture_output=True, text=True, timeout=timeout)
    try:
        online, awake, screens = (int(value) for value in result.stdout.split())
    except ValueError as exc:
        raise ValueError(
            f"invalid display probe output {result.stdout.strip()!r}; "
            f"{result.stderr.strip()[-500:] or 'no stderr'}") from exc
    return online, awake, screens


def probe_display(platform=sys.platform):
    """Count displays the launcher's session sees; allow-listed render commands inherit it.

    Returns None off macOS. Raises when a demo launched from this session
    could only idle to the fleet-run watchdog.
    """
    if platform != "darwin":
        return None
    try:
        counts = display_counts(platform)
    except ValueError as exc:
        counts = None
        detail = str(exc)
    else:
        detail = "no stderr"
    if counts is None or counts[0] < 1:
        online = None if counts is None else counts[0]
        raise ValueError(
            f"no display session (CoreGraphics online displays: {online}; {detail}). "
            "fleet-run and the display "
            "validators would time out. Start fleet-up from a logged-in GUI (Aqua) "
            "session, not ssh or a LaunchDaemon, then re-run "
            "`fleet-codex --role worker --doctor`.")
    return counts[0]


def probe(worktree, roots):
    argv = ["codex", "sandbox",
            "-c", 'sandbox_mode="workspace-write"',
            "-c", "sandbox_workspace_write.network_access=true",
            "-c", "sandbox_workspace_write.writable_roots=" + json.dumps(roots),
            "--", sys.executable, "-c", PROBE, json.dumps(roots)]
    result = subprocess.run(argv, cwd=worktree, capture_output=True, text=True, timeout=60)
    try:
        checks = json.loads(result.stdout)
    except ValueError as exc:
        raise ValueError("Codex sandbox probe could not run: " + result.stderr[-2000:]) from exc
    failures = [entry for entry in checks if not entry["ok"]]
    if result.returncode or failures:
        raise ValueError("Codex sandbox writes blocked: " + json.dumps(failures))
    return checks
