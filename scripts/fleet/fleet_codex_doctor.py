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
