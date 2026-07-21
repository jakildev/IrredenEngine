#!/usr/bin/env python3
"""Shared helpers for the cull/render/gui/light verify-harness family (#2358).

``cull-verify.py``, ``render-verify.py``, ``gui-verify.py``, and
``light-verify.py`` all import from this module for process-run wrappers,
worktree/backend detection, executable discovery, and pixel-diff comparison
against ``render-compare.py``. render-verify.py's ``_run_capture`` (an
``--auto-screenshot`` demo-capture runner) and ``_compare_shot`` are domain-
specific and stay local to that file.

Assumes this file lives at ``<repo>/scripts/verify_common.py``, alongside the
harnesses that import it (they run as ``__main__`` from this directory, so
``import verify_common`` resolves via ``sys.path[0]``).
"""

from __future__ import annotations

import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
RENDER_COMPARE = SCRIPT_DIR / "render-compare.py"


def run(cmd: list[str], cwd: Path | None = None, check: bool = True,
        env: dict[str, str] | None = None, timeout: int | None = None) -> int:
    print("+ " + " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env,
                          timeout=timeout)
    if check and proc.returncode != 0:
        raise SystemExit(f"command failed ({proc.returncode}): {' '.join(cmd)}")
    return proc.returncode


def run_capture(cmd: list[str], cwd: Path | None = None,
                 timeout: int | None = None) -> tuple[int, str]:
    print("+ " + " ".join(cmd), flush=True)
    # errors="replace": the child's stdout can carry non-UTF-8 bytes that
    # aren't ours to sanitize — e.g. the engine logs system audio/MIDI device
    # names verbatim, and a device named "Robert's iPhone" emits a Mac-Roman
    # 0xd5 apostrophe. Strict decoding (the text=True default) raises
    # UnicodeDecodeError mid-stream and aborts the whole verify run. The
    # harnesses only ever grep for ASCII markers (GUI-ASSERT / PASS / FAIL /
    # screenshot paths), so replacing an undecodable byte with U+FFFD is loss-
    # free for our purposes.
    proc = subprocess.Popen(
        cmd, cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
    )
    lines: list[str] = []
    if proc.stdout is None:
        raise RuntimeError("stdout unavailable (Popen stdout=PIPE failed)")
    for line in proc.stdout:
        print(line, end="", flush=True)
        lines.append(line)
    proc.wait(timeout=timeout)
    return proc.returncode, "".join(lines)


def detect_worktree_root(start: Path) -> Path:
    proc = subprocess.run(["git", "-C", str(start), "rev-parse", "--show-toplevel"],
                          capture_output=True, text=True, check=True)
    return Path(proc.stdout.strip())


def detect_backend(build_dir: Path) -> str:
    """Return the preset name for the host, e.g. 'macos-debug'.

    Derived from ``platform.system()`` — matches the convention used by the
    ``backend-parity`` skill (``uname -s``). This assumes the harness runs
    on the same host that built the tree under ``build_dir``; we also
    require ``build/CMakeCache.txt`` to exist so we fail loudly if no
    configure has run.
    """
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        raise SystemExit(
            f"no CMakeCache.txt at {cache} — run `cmake --preset <name>` first"
        )
    system = platform.system().lower()
    if system == "darwin":
        return "macos-debug"
    if system == "linux":
        return "linux-debug"
    if system == "windows":
        return "windows-debug"
    return f"{system}-debug"


def find_exe(build_dir: Path, target_name: str, demo_name: str) -> Path:
    """Locate the demo's executable in its CMake output dir.

    Scoped to ``build/creations/demos/<demo>/`` first so we don't
    accidentally pick up a build-intermediate copy from elsewhere in
    the tree. Falls back to the full ``build/`` walk if that subtree
    is absent (e.g. editor creations under a different layout).
    """
    search_roots = [
        build_dir / "creations" / "demos" / demo_name,
        build_dir,
    ]
    names = (target_name, f"{target_name}.exe")
    for root in search_roots:
        if not root.exists():
            continue
        for name in names:
            candidates = [p for p in root.rglob(name)
                          if p.is_file() and os.access(p, os.X_OK)]
            if candidates:
                candidates.sort(key=lambda p: len(p.parts))
                return candidates[0]
    raise SystemExit(f"could not find executable {target_name} under {build_dir}")


def compare(actual: Path, reference: Path, diff_out: Path | None,
            thresholds: dict[str, Any] | None = None) -> dict[str, Any]:
    thresholds = thresholds or {}
    cmd = [
        sys.executable, str(RENDER_COMPARE),
        str(actual), str(reference),
        "--json",
        "--per-pixel-tol", str(thresholds.get("per_pixel_tol", 8)),
        "--threshold-match-pct", str(thresholds.get("match_pct", 99.9)),
        "--threshold-max-delta", str(thresholds.get("max_delta", 64)),
        "--threshold-psnr", str(thresholds.get("psnr_db", 35.0)),
    ]
    if diff_out:
        cmd.extend(["--diff-out", str(diff_out)])
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode == 2:
        raise SystemExit(f"render-compare errored: {proc.stderr}")
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as e:
        raise SystemExit(f"render-compare returned non-JSON: {proc.stdout!r} ({e})")


def run_pass(cmd: list[str], cwd: Path, shots_dir: Path,
             timeout: int | None = None) -> tuple[int, str, list[Path]]:
    """Clear ``shots_dir``, run ``cmd``, and collect the resulting screenshots.

    Callers that run multiple passes against the same ``shots_dir`` (e.g.
    light-verify's domain-matrix / boundary-sweep / hover-sweep flags) must
    route any per-pass diff output to a directory that is a *sibling* of
    ``shots_dir``, not nested under it — this function ``rmtree``s
    ``shots_dir`` on every call, which would silently wipe a previous pass's
    diff images if they lived underneath it (the bug #2356 fixed in
    light-verify.py).
    """
    if shots_dir.exists():
        shutil.rmtree(shots_dir)
    shots_dir.mkdir(parents=True, exist_ok=True)
    rc, output = run_capture(cmd, cwd=cwd, timeout=timeout)
    images = sorted(shots_dir.glob("screenshot_*.png"))
    return rc, output, images
