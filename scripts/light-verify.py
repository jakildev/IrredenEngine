#!/usr/bin/env python3
"""light-verify — automated pass/fail harness for light/shadow domain culling (#2317, V3).

Drives the lighting demo family's ``--light-domain-matrix`` (zoom x yaw x
pan-distance), ``--light-boundary-sweep`` (#2310), and ``--hover-sweep``
auto-screenshot series, parses each shot's DOMAIN-STATE log line (emitted by
``logDomainState`` in ``lighting_demo_scene.hpp``, #2315 V1), and asserts:

  1. Domain-matrix "inwin"/"band" shots never report SKIPPED — a light whose
     influence sphere intersects the viewport must always seed (V1's
     boundary-clamp contract).
  2. Boundary-sweep residuals fade monotonically (non-increasing) as the
     light pans away from the window — never pop or band.
  3. The light-anchor freeze is invariant to zoom/yaw — for a fixed pan
     category, every domain-matrix shot at that pan reports the same
     ``anchor``, since the anchor tracks camera XY pan only.
  4. Each shot's captured image matches its committed per-backend baseline
     (render-compare.py, same manifest/threshold machinery as
     render-verify.py / cull-verify.py). A backend without baselines SKIPS
     rather than fails (render-verify precedent) — hover-sweep in particular
     has no DOMAIN-STATE-derived assertion of its own (the camera never
     moves), so its correctness is entirely image-diff driven.

Mutation test (the acceptance gate a reviewer runs to confirm this harness
is load-bearing, not just green-by-construction): locally force every seed
to look boundary-clamped by editing the early-out at
``engine/prefabs/irreden/render/systems/system_compute_light_volume.hpp``
(the ``if (seedAlpha <= 0.0f)`` branch around line 271) to unconditionally
skip, e.g. ``if (true) {``. Rerun this script — assertion 1 goes red because
every domain-matrix "inwin" shot now reports SKIPPED instead of
SEEDED_FULL. Revert the edit before committing anything.

DOMAIN-STATE is versioned leniently: only the fields this script parses
(shot, anchor, lights, casters) are matched; unrecognized keys the V1/V2
contract adds later are ignored, not rejected.

Usage::

    python3 scripts/light-verify.py                    # verify (build + run + compare)
    python3 scripts/light-verify.py --no-build          # skip build (exe already fresh)
    python3 scripts/light-verify.py --target IRLightingEmissive
    python3 scripts/light-verify.py --update-baselines  # bless committed reference images
    python3 scripts/light-verify.py --update-baselines --force

Assumes this file lives at ``<repo>/scripts/light-verify.py``.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RENDER_COMPARE = SCRIPT_DIR / "render-compare.py"

DEMO_NAME = "lighting"
DEFAULT_TARGET = "IRLightingEmissive"
SCREENSHOT_SUBDIR = "save_files/screenshots"

# One pass per CLI flag the demo supports (see lighting_demo_scene.hpp
# registerArgs/initSystems). Each pass captures its own shot series and gets
# its own baseline subdirectory, so a threshold or shot-count change to one
# pass never touches the others' committed references.
PASSES = ("light-domain-matrix", "light-boundary-sweep", "hover-sweep")

# Thresholds mirror cull-verify.py's calibration (#1438): AO/light-volume
# shading differs by a handful of bytes in a small fraction of pixels between
# otherwise-identical scenes, but a genuine regression (a dropped light, a
# truncated shadow) flips a visible region from lit/shadowed to the opposite,
# which drops well below match_pct.
LIGHT_VERIFY_THRESHOLDS: dict[str, Any] = {
    "per_pixel_tol": 8,
    "match_pct": 99.5,
    "max_delta": 200,
    "psnr_db": 28.0,
}

_DOMAIN_STATE_RE = re.compile(
    r"DOMAIN-STATE\s+"
    r"shot=(\S+)\s+"
    r"anchor=(-?\d+),(-?\d+),(-?\d+)\s+"
    r"window=(-?\d+),(-?\d+),(-?\d+)\.\.(-?\d+),(-?\d+),(-?\d+)\s+"
    r"lights=\[(.*?)\]\s+"
    r"feeder=(-?[\d.]+),(-?[\d.]+)\.\.(-?[\d.]+),(-?[\d.]+)\s+"
    r"casters=(\d+)"
)
# One light entry inside the DOMAIN-STATE lights=[...] list: entity:STATE:residual
_LIGHT_ENTRY_RE = re.compile(r"(\d+):(SEEDED_FULL|BOUNDARY_DISCOUNTED|SKIPPED):([\d.]+)")


def _run(cmd: list[str], cwd: Path | None = None, check: bool = True) -> int:
    print("+ " + " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if check and proc.returncode != 0:
        raise SystemExit(f"command failed ({proc.returncode}): {' '.join(cmd)}")
    return proc.returncode


def _run_capture(
    cmd: list[str], cwd: Path | None = None, timeout: int | None = None
) -> tuple[int, str]:
    print("+ " + " ".join(cmd), flush=True)
    proc = subprocess.Popen(
        cmd, cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    lines: list[str] = []
    if proc.stdout is None:
        raise RuntimeError("stdout unavailable (Popen stdout=PIPE failed)")
    for line in proc.stdout:
        print(line, end="", flush=True)
        lines.append(line)
    proc.wait(timeout=timeout)
    return proc.returncode, "".join(lines)


def _detect_worktree_root(start: Path) -> Path:
    proc = subprocess.run(
        ["git", "-C", str(start), "rev-parse", "--show-toplevel"],
        capture_output=True, text=True, check=True,
    )
    return Path(proc.stdout.strip())


def _detect_backend() -> str:
    system = platform.system().lower()
    if system == "darwin":
        return "macos-debug"
    if system == "linux":
        return "linux-debug"
    if system == "windows":
        return "windows-debug"
    return f"{system}-debug"


def _find_exe(build_dir: Path, target: str) -> Path:
    search_root = build_dir / "creations" / "demos" / DEMO_NAME
    names = (target, f"{target}.exe")
    for root in (search_root, build_dir):
        if not root.exists():
            continue
        for name in names:
            candidates = [p for p in root.rglob(name)
                          if p.is_file() and os.access(p, os.X_OK)]
            if candidates:
                candidates.sort(key=lambda p: len(p.parts))
                return candidates[0]
    raise SystemExit(f"could not find executable {target} under {build_dir}")


def _parse_domain_state(output: str) -> list[dict[str, Any]]:
    shots = []
    for m in _DOMAIN_STATE_RE.finditer(output):
        (shot, ax, ay, az, wlx, wly, wlz, whx, why, whz,
         lights_raw, fmin_x, fmin_y, fmax_x, fmax_y, casters) = m.groups()
        lights = [
            {"entity": int(eid), "state": state, "residual": float(residual)}
            for eid, state, residual in _LIGHT_ENTRY_RE.findall(lights_raw)
        ]
        shots.append({
            "shot": shot,
            "anchor": (int(ax), int(ay), int(az)),
            "window": ((int(wlx), int(wly), int(wlz)), (int(whx), int(why), int(whz))),
            "lights": lights,
            "casters": int(casters),
        })
    return shots


def _compare(actual: Path, reference: Path, diff_out: Path | None) -> dict[str, Any]:
    cmd = [
        sys.executable, str(RENDER_COMPARE),
        str(actual), str(reference),
        "--json",
        "--per-pixel-tol", str(LIGHT_VERIFY_THRESHOLDS["per_pixel_tol"]),
        "--threshold-match-pct", str(LIGHT_VERIFY_THRESHOLDS["match_pct"]),
        "--threshold-max-delta", str(LIGHT_VERIFY_THRESHOLDS["max_delta"]),
        "--threshold-psnr", str(LIGHT_VERIFY_THRESHOLDS["psnr_db"]),
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


def _pan_category(shot_label: str) -> str | None:
    # domain_z<Z>_yaw<Y>_<cat> where cat in {inwin, band, beyond}.
    m = re.match(r"domain_z\d+_yaw\d+_(inwin|band|beyond)$", shot_label)
    return m.group(1) if m else None


def _check_domain_matrix(shots: list[dict[str, Any]]) -> list[str]:
    """Assertions 1 and 3 (see module docstring)."""
    failures: list[str] = []
    anchor_by_category: dict[str, set[tuple[int, int, int]]] = {}
    for s in shots:
        cat = _pan_category(s["shot"])
        if cat is None:
            continue
        anchor_by_category.setdefault(cat, set()).add(s["anchor"])
        if cat in ("inwin", "band"):
            for light in s["lights"]:
                if light["state"] == "SKIPPED":
                    failures.append(
                        f"{s['shot']}: light {light['entity']} reports SKIPPED but pan "
                        f"category {cat!r} should intersect the viewport"
                    )
        elif cat == "beyond":
            for light in s["lights"]:
                if light["state"] != "SKIPPED":
                    failures.append(
                        f"{s['shot']}: light {light['entity']} reports {light['state']} but "
                        f"pan category 'beyond' should be out of residual reach"
                    )
    for cat, anchors in anchor_by_category.items():
        if len(anchors) > 1:
            failures.append(
                f"pan category {cat!r}: light anchor varies across zoom/yaw ({sorted(anchors)}) "
                "— anchor should track camera pan only"
            )
    return failures


def _check_boundary_sweep(shots: list[dict[str, Any]]) -> list[str]:
    """Assertion 2 (see module docstring)."""
    failures: list[str] = []
    ordered = sorted(
        (s for s in shots if s["shot"].startswith("light_boundary_d")),
        key=lambda s: int(s["shot"].removeprefix("light_boundary_d")),
    )
    if not ordered:
        return failures
    prev_residual = None
    prev_label = None
    for s in ordered:
        if not s["lights"]:
            continue
        residual = s["lights"][0]["residual"]
        if prev_residual is not None and residual > prev_residual + 1e-6:
            failures.append(
                f"{s['shot']}: residual {residual:.3f} rose above {prev_label}'s "
                f"{prev_residual:.3f} — boundary fade must be non-increasing"
            )
        prev_residual, prev_label = residual, s["shot"]
    return failures


def _run_pass(
    worktree: Path, target: str, flag: str, warmup: int, timeout: int, shots_dir: Path
) -> tuple[list[dict[str, Any]], list[Path], int | None]:
    if shots_dir.exists():
        shutil.rmtree(shots_dir)
    shots_dir.mkdir(parents=True, exist_ok=True)

    run_cmd = [
        "fleet-run", "--timeout", str(timeout), target,
        f"--{flag}", "--auto-screenshot", str(warmup),
    ]
    rc, output = _run_capture(run_cmd, cwd=worktree, timeout=timeout + 30)
    domain_states = _parse_domain_state(output)
    images = sorted(shots_dir.glob("screenshot_*.png"))
    run_crash = rc if rc != 0 else None
    return domain_states, images, run_crash


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", default=DEFAULT_TARGET,
                    help=f"Lighting demo executable to drive (default: {DEFAULT_TARGET}).")
    ap.add_argument("--build-dir", default=None,
                    help="CMake build dir (default: <repo>/build).")
    ap.add_argument("--warmup", type=int, default=10,
                    help="Warmup frames before the first shot (default: 10).")
    ap.add_argument("--timeout", type=int, default=120,
                    help="Per-run timeout in seconds (default: 120).")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip fleet-build; assume the target is already built.")
    ap.add_argument(
        "--update-baselines", action="store_true",
        help="Copy each pass's shots to the committed baseline directory "
             "(creations/demos/lighting/test/references/<backend>/light-verify/<pass>/) "
             "instead of comparing against them.",
    )
    ap.add_argument("--force", action="store_true",
                    help="Skip the --update-baselines confirmation prompt.")
    args = ap.parse_args(argv)

    if not RENDER_COMPARE.exists():
        raise SystemExit(f"render-compare.py not found at {RENDER_COMPARE}")

    worktree = _detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = _detect_backend()
    demo_dir = worktree / "creations" / "demos" / DEMO_NAME

    print(f"[light-verify] target={args.target}  backend={backend}")

    if not args.no_build:
        _run(["fleet-build", "--target", args.target], cwd=worktree)

    exe = _find_exe(build_dir, args.target)
    shots_dir = exe.parent / SCREENSHOT_SUBDIR
    # Each pass rmtrees shots_dir on entry (_run_pass), so per-pass diff PNGs
    # must live in a sibling dir that outlives every pass — nested under
    # shots_dir they'd be wiped by the next pass before a human inspects them
    # (render-verify.py hit the identical multi-pass bug; see its diff_dir
    # routing in scripts/render-verify.py). Clear it once up front so stale
    # diffs from a prior run don't linger.
    diff_root = shots_dir.parent / "light_verify_diffs"
    if diff_root.exists():
        shutil.rmtree(diff_root)

    all_assertion_failures: list[str] = []
    all_image_results: list[tuple[str, str, dict[str, Any]]] = []  # (pass, label, result)
    any_run_crashed = False
    any_baselines_missing = False

    for flag in PASSES:
        print(f"\n[light-verify] === pass: --{flag} ===")
        domain_states, images, run_crash = _run_pass(
            worktree, args.target, flag, args.warmup, args.timeout, shots_dir
        )
        if run_crash is not None:
            any_run_crashed = True
            print(f"[light-verify] --{flag}: fleet-run exited {run_crash}", file=sys.stderr)

        labels = [s["shot"] for s in domain_states] or [f"shot_{i:03d}" for i in range(len(images))]
        if len(images) != len(labels):
            all_assertion_failures.append(
                f"--{flag}: captured {len(images)} screenshots but parsed {len(labels)} "
                "DOMAIN-STATE lines — counts must match 1:1"
            )

        if flag == "light-domain-matrix":
            all_assertion_failures.extend(_check_domain_matrix(domain_states))
        elif flag == "light-boundary-sweep":
            all_assertion_failures.extend(_check_boundary_sweep(domain_states))
        # hover-sweep has no DOMAIN-STATE-derived assertion (see module
        # docstring) — image comparison below is its only correctness gate.

        baseline_dir = demo_dir / "test" / "references" / backend / "light-verify" / flag
        if args.update_baselines:
            if not args.force:
                reply = input(
                    f"[light-verify] About to write {len(images)} baselines to "
                    f"{baseline_dir}. Continue? [y/N] "
                )
                if reply.strip().lower() not in ("y", "yes"):
                    print("[light-verify] aborted.")
                    return 1
            baseline_dir.mkdir(parents=True, exist_ok=True)
            for image, label in zip(images, labels):
                dest = baseline_dir / f"{label}.png"
                shutil.copy2(image, dest)
            print(f"[light-verify] --{flag}: wrote {len(images)} baselines to {baseline_dir}")
            continue

        if not baseline_dir.exists():
            print(
                f"[light-verify] --{flag}: no baselines at {baseline_dir} — SKIPPING image "
                "compare for this backend (render-verify precedent)."
            )
            any_baselines_missing = True
            continue

        diff_dir = diff_root / flag
        diff_dir.mkdir(parents=True, exist_ok=True)
        for image, label in zip(images, labels):
            reference = baseline_dir / f"{label}.png"
            if not reference.exists():
                all_image_results.append((flag, label, {"pass": False, "match_pct": 0.0,
                                                          "max_delta": -1, "psnr_db": "n/a",
                                                          "error": "no reference"}))
                continue
            result = _compare(image, reference, diff_dir / f"{label}.diff.png")
            all_image_results.append((flag, label, result))

    print()
    print(f"{'pass':22} {'shot':30} {'result':8} {'match%':>8} {'max_d':>6} {'psnr':>8}")
    print("-" * 90)
    image_failures = []
    for flag, label, result in all_image_results:
        verdict = "PASS" if result.get("pass") else "FAIL"
        raw_psnr = result.get("psnr_db")
        psnr_str = (
            f"{raw_psnr:>8.2f}" if isinstance(raw_psnr, (int, float)) else f"{str(raw_psnr):>8}"
        )
        print(
            f"{flag:22} {label:30} {verdict:8} {result.get('match_pct', 0):>8.3f} "
            f"{result.get('max_delta', -1):>6} {psnr_str}"
        )
        if not result.get("pass"):
            image_failures.append((flag, label, result))

    print()
    if all_assertion_failures:
        print(f"[light-verify] {len(all_assertion_failures)} DOMAIN-STATE assertion failure(s):")
        for f in all_assertion_failures:
            print(f"  - {f}")

    if image_failures:
        print(f"[light-verify] {len(image_failures)} image comparison failure(s):")
        for flag, label, result in image_failures:
            print(
                f"  {flag}/{label}: {result.get('error', '')} "
                f"match={result.get('match_pct', 0):.3f}%"
            )

    ok = not all_assertion_failures and not image_failures and not any_run_crashed
    if ok:
        suffix = " (some backends skipped — no baselines)" if any_baselines_missing else ""
        print(f"[light-verify] all checks PASS{suffix}")
        return 0

    if any_run_crashed:
        print("[light-verify] at least one pass crashed — see run output above.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
