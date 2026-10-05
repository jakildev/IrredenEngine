from pathlib import Path
import subprocess
import sys

root = Path.cwd()
output = root / "docs/perf/bounded-light-volume" / sys.argv[1]
cases = [("low", "1", "1", "0"), ("rotated", "4", "4", "0.7853981633974483"), ("dense", "4", "4", "0")]
for name, zoom, subdivisions, yaw in cases:
    command = [sys.executable, "scripts/perf/repeat_profile.py", "--target", "IRPerfGrid", "--output", str(output / name), "--repeats", "2", "--timeout", "120", "--", "--mode", "voxel_set", "--wave-freeze", "--auto-profile", "120", "--grid-size", "64", "--zoom", zoom, "--subdivision-mode", "full", "--base-subdivisions", subdivisions, "--wave-amplitude", "5", "--pivot-origin", "--no-overlay", "--yaw", yaw, "--config-preset", "configs/perf/million.lua", "--window-mode", "offscreen"]
    print("CONTROL", name, flush=True)
    subprocess.run(command, cwd=root, check=True)
