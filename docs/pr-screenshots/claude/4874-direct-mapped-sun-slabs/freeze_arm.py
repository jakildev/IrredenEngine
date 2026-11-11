#!/usr/bin/env python3
"""Freeze one slab arm: copy the built demo trees + CMakeCache and hash them.

usage: freeze_arm.py <worktree> <build-dir> <dest> <slots> <direct 0|1>
"""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

worktree, build, dest = (Path(a).resolve() for a in sys.argv[1:4])
slots, direct = int(sys.argv[4]), bool(int(sys.argv[5]))
if dest.exists():
    shutil.rmtree(dest)
dest.mkdir(parents=True)
shutil.copy2(build / "CMakeCache.txt", dest / "CMakeCache.txt")
for demo, exe in (("canvas_stress", "IRCanvasStress"), ("perf_grid", "IRPerfGrid")):
    src = build / "creations/demos" / demo
    out = dest / "creations/demos" / demo
    out.mkdir(parents=True)
    for entry in src.iterdir():
        if entry.name == exe or entry.name in ("shaders", "data", "scripts", "configs"):
            if entry.is_dir():
                shutil.copytree(entry, out / entry.name, symlinks=False)
            else:
                shutil.copy2(entry, out / entry.name)
hashes = {str(p.relative_to(dest)): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in sorted(dest.rglob("*")) if p.is_file()}
head = subprocess.run(["git", "-C", str(worktree), "rev-parse", "HEAD"],
                      capture_output=True, text=True, check=True).stdout.strip()
patch = subprocess.run(["git", "-C", str(worktree), "diff", "HEAD"],
                       capture_output=True, text=True, check=True).stdout
(dest / "source.patch").write_text(patch)
manifest = {"base": head, "slots": slots, "direct": direct,
            "source_patch_sha256": hashlib.sha256(patch.encode()).hexdigest(),
            "sha256": hashes}
(dest / "frozen-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"froze {dest}: {len(hashes)} files, base {head[:10]}, slots={slots} direct={direct}")
