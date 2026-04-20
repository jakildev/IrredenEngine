#!/usr/bin/env python3
"""Render-regression harness for Irreden Engine demos.

Drives the `--auto-screenshot` capture flow, then compares each shot against
a committed reference-image library under
``creations/demos/<demo>/test/references/<backend>/<label>.png``.

Workflow:
  1. Read `manifest.json` for the target demo.
  2. Detect the active CMake preset (linux-debug / macos-debug / windows-debug).
  3. Build the target via `fleet-build`.
  4. Clear the demo's `save_files/screenshots/` directory.
  5. Run the demo with `--auto-screenshot` via `fleet-run`.
  6. For each shot (manifest order), map `screenshot_NNNNNN.png` → `<label>.png`
     and compare via `render-compare.py`.
  7. Print a pass/fail table and exit non-zero on any failure.

`--update-references` copies the captured shots over the reference set for
the current backend (after explicit confirmation unless ``--force`` is given).

Assumes this file lives at ``<repo>/scripts/render-verify.py``.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RENDER_COMPARE = SCRIPT_DIR / "render-compare.py"


def _run(cmd: list[str], cwd: Path | None = None, check: bool = True,
         env: dict[str, str] | None = None, timeout: int | None = None) -> int:
    print("+ " + " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env,
                          timeout=timeout)
    if check and proc.returncode != 0:
        raise SystemExit(f"command failed ({proc.returncode}): {' '.join(cmd)}")
    return proc.returncode


def _detect_worktree_root(start: Path) -> Path:
    proc = subprocess.run(["git", "-C", str(start), "rev-parse", "--show-toplevel"],
                          capture_output=True, text=True, check=True)
    return Path(proc.stdout.strip())


def _detect_backend(build_dir: Path) -> str:
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


def _find_exe(build_dir: Path, target_name: str, demo_name: str) -> Path:
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


def _load_manifest(demo_dir: Path) -> dict[str, Any]:
    path = demo_dir / "test" / "references" / "manifest.json"
    if not path.exists():
        raise SystemExit(f"manifest not found: {path}")
    try:
        with path.open() as f:
            data = json.load(f)
    except json.JSONDecodeError as e:
        raise SystemExit(f"manifest {path} is not valid JSON: {e}")
    shots = data.get("shots")
    if not isinstance(shots, list) or not shots:
        raise SystemExit(
            f"manifest {path} must contain a non-empty 'shots' array"
        )
    return data


def _collect_shots(shots_dir: Path, num_shots: int) -> list[Path]:
    shots = sorted(shots_dir.glob("screenshot_*.png"))
    if len(shots) < num_shots:
        raise SystemExit(
            f"expected {num_shots} screenshots in {shots_dir}, got {len(shots)}"
        )
    return shots[:num_shots]


def _compare_shot(actual: Path, reference: Path, thresholds: dict[str, Any],
                  diff_out: Path | None) -> dict[str, Any]:
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


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", default="IRShapeDebug",
                    help="CMake target to build and run (default: IRShapeDebug).")
    ap.add_argument("--demo", default=None,
                    help="Demo directory name under creations/demos/ (default: "
                         "inferred from target by stripping 'IR' prefix and "
                         "lowercasing, so IRShapeDebug → shape_debug; override "
                         "if the mapping doesn't hold).")
    ap.add_argument("--build-dir", default=None,
                    help="CMake build dir (default: <repo>/build).")
    ap.add_argument("--warmup", type=int, default=10,
                    help="Warmup frames before the first shot (default: 10).")
    ap.add_argument("--timeout", type=int, default=60,
                    help="Per-run timeout in seconds (default: 60).")
    ap.add_argument("--update-references", action="store_true",
                    help="Overwrite the reference set with the captured shots.")
    ap.add_argument("--force", action="store_true",
                    help="Skip the --update-references confirmation prompt.")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip `fleet-build`; assume the target is already built.")
    args = ap.parse_args(argv)

    worktree = _detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = _detect_backend(build_dir)

    demo_name = args.demo or _target_to_demo_name(args.target)
    demo_dir = worktree / "creations" / "demos" / demo_name
    if not demo_dir.exists():
        raise SystemExit(f"demo dir not found: {demo_dir}")

    manifest = _load_manifest(demo_dir)
    shot_labels: list[str] = manifest["shots"]
    thresholds = manifest.get("thresholds", {})
    screenshot_subdir = manifest.get("screenshot_subdir", "save_files/screenshots")

    print(f"[render-verify] target={args.target} demo={demo_name} backend={backend}")
    print(f"[render-verify] {len(shot_labels)} shots: {', '.join(shot_labels)}")

    if not args.no_build:
        _run(["fleet-build", "--target", args.target], cwd=worktree)

    exe = _find_exe(build_dir, args.target, demo_name)
    shots_dir = exe.parent / screenshot_subdir
    if shots_dir.exists():
        shutil.rmtree(shots_dir)
    shots_dir.mkdir(parents=True, exist_ok=True)

    run_cmd = ["fleet-run", "--timeout", str(args.timeout), args.target,
               "--auto-screenshot", str(args.warmup)]
    print("+ " + " ".join(run_cmd), flush=True)
    proc = subprocess.run(run_cmd, cwd=str(worktree),
                          capture_output=True, text=True)
    if proc.returncode != 0:
        # Expected on timeout; also fires if the demo crashed. Surface the
        # tail so a crash isn't hidden behind "expected N shots, got M".
        print(f"[render-verify] fleet-run exited {proc.returncode}; "
              f"tail of output follows (screenshot count will be checked "
              f"against manifest below):", file=sys.stderr)
        tail = (proc.stdout + proc.stderr).splitlines()[-40:]
        for line in tail:
            print(f"    {line}", file=sys.stderr)

    captured = _collect_shots(shots_dir, len(shot_labels))
    ref_dir = demo_dir / "test" / "references" / backend
    diff_dir = shots_dir / "diffs"

    if args.update_references:
        if not args.force:
            reply = input(
                f"[render-verify] About to overwrite references in {ref_dir}. "
                f"Continue? [y/N] "
            )
            if reply.strip().lower() not in ("y", "yes"):
                print("[render-verify] aborted.")
                return 1
        ref_dir.mkdir(parents=True, exist_ok=True)
        for actual, label in zip(captured, shot_labels):
            dest = ref_dir / f"{label}.png"
            shutil.copy2(actual, dest)
            print(f"[render-verify] updated {dest}")
        return 0

    if not ref_dir.exists() or not any(ref_dir.glob("*.png")):
        print(
            f"[render-verify] no references found for backend '{backend}' at "
            f"{ref_dir}. Run with --update-references to capture them.",
            file=sys.stderr,
        )
        return 2

    print()
    print(f"{'shot':30} {'result':8} {'match%':>8} {'max_d':>6} {'psnr':>8}")
    print("-" * 66)
    all_pass = True
    failures: list[tuple[str, dict[str, Any]]] = []
    for actual, label in zip(captured, shot_labels):
        reference = ref_dir / f"{label}.png"
        if not reference.exists():
            print(f"{label:30} {'MISSING':8}")
            all_pass = False
            failures.append((label, {"reason": f"no reference at {reference}"}))
            continue
        diff_out = diff_dir / f"{label}.diff.png"
        result = _compare_shot(actual, reference, thresholds, diff_out)
        verdict = "PASS" if result["pass"] else "FAIL"
        psnr = result["psnr_db"]
        print(f"{label:30} {verdict:8} {result['match_pct']:>8} "
              f"{result['max_delta']:>6} {psnr:>8}")
        if not result["pass"]:
            all_pass = False
            failures.append((label, result))

    print()
    if all_pass:
        print(f"[render-verify] all {len(shot_labels)} shots PASS")
        return 0

    print(f"[render-verify] {len(failures)} of {len(shot_labels)} shots FAIL")
    for label, result in failures:
        reason = result.get("reason", "mismatch")
        diff = result.get("diff_path", "(no diff)")
        print(f"  - {label}: {reason}  diff={diff}")
    return 1


def _target_to_demo_name(target: str) -> str:
    name = target[2:] if target.startswith("IR") else target
    # CamelCase → snake_case
    out: list[str] = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0 and not name[i - 1].isupper():
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


if __name__ == "__main__":
    sys.exit(main())
