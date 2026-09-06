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
  5. Per-target expected gather states (``EXPECTED_LIGHT_STATES``): a fixture
     demo built to exercise one specific gather outcome asserts that outcome
     by shot label. A listed shot whose DOMAIN-STATE line parsed *no* light
     entry fails rather than passing vacuously — that is the
     ``_LIGHT_ENTRY_RE`` whitelist trap (an unrecognised state token drops
     the light silently) made loud. An expectation pattern that matches no
     shot at all also fails, so a stale or mistyped key can't sit green.

Mutation tests (the acceptance gates a reviewer runs to confirm this harness
is load-bearing, not just green-by-construction). Revert each edit before
committing anything.

  * Assertion 1: locally force every seed to look boundary-clamped by editing
    the early-out at
    ``engine/prefabs/irreden/render/systems/system_compute_light_volume.hpp``
    (the ``if (seedAlpha <= 0.0f)`` branch) to unconditionally skip, e.g.
    ``if (true) {``. Rerun — assertion 1 goes red because every domain-matrix
    "inwin" shot now reports SKIPPED instead of SEEDED_FULL.
  * Assertion 5: pass ``nullptr`` for ``occlusion`` in
    ``System<COMPUTE_LIGHT_VOLUME>::tick`` (or comment out the relocation
    block in ``gatherLightSources``). Rerun with
    ``--target IRLightingOccludedBoundary`` — ``light_boundary_d070`` and
    every ``domain_z*_yaw*_band`` shot go red, because the occluded clamp
    reverts to ``BOUNDARY_DISCOUNTED``.

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
import fnmatch
import re
import shutil
import sys
from pathlib import Path
from typing import Any

import verify_common

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

# Assertion 5. Per-target, per-shot-label-glob expected gather state. A fixture
# demo exists to make one gather outcome fire, so the outcome is the gate — the
# image half only locks whatever that outcome renders as. Keys are fnmatch
# globs against the DOMAIN-STATE `shot=` label; every pattern must match at
# least one shot across the run's passes or the run fails.
EXPECTED_LIGHT_STATES: dict[str, dict[str, str]] = {
    # #2330: the d070 sweep shot's per-axis-clamped seed cell lands inside the
    # demo's wall slab, so the gather must relocate it to the nearest free cell
    # on the clamped window face. d088 clamps to a free cell and must stay on
    # the untouched path. The domain-matrix "band" shots share d070's anchor.
    "IRLightingOccludedBoundary": {
        "light_boundary_d070": "BOUNDARY_RELOCATED",
        "light_boundary_d088": "BOUNDARY_DISCOUNTED",
        "domain_z*_yaw*_band": "BOUNDARY_RELOCATED",
    },
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
# One light entry inside the DOMAIN-STATE lights=[...] list: entity:STATE:residual.
# The state alternation is a WHITELIST — a light reporting a token missing from
# it is dropped from the parsed shot entirely, and the downstream checks then
# skip that shot instead of failing. Every `LightGatherState` value in
# system_compute_light_volume.hpp must appear here; assertion 5 is what makes a
# forgotten one loud instead of vacuous. Order matters: SKIPPED_OCCLUDED before
# SKIPPED, or the shorter prefix wins the alternation and truncates the token.
_LIGHT_ENTRY_RE = re.compile(
    r"(\d+):"
    r"(SEEDED_FULL|BOUNDARY_DISCOUNTED|BOUNDARY_RELOCATED|SKIPPED_OCCLUDED|SKIPPED):"
    r"([\d.]+)"
)


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
                # Both skip outcomes are defects here: residual-exhausted
                # (SKIPPED) and occluded-with-no-reachable-face-cell
                # (SKIPPED_OCCLUDED, #2330).
                if light["state"].startswith("SKIPPED"):
                    failures.append(
                        f"{s['shot']}: light {light['entity']} reports {light['state']} but pan "
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


def _check_expected_states(
    target: str, shots: list[dict[str, Any]], matched_patterns: set[str]
) -> list[str]:
    """Assertion 5 (see module docstring). Records matched patterns in-place."""
    expectations = EXPECTED_LIGHT_STATES.get(target)
    if not expectations:
        return []
    failures: list[str] = []
    for s in shots:
        for pattern, expected in expectations.items():
            if not fnmatch.fnmatch(s["shot"], pattern):
                continue
            matched_patterns.add(pattern)
            if not s["lights"]:
                failures.append(
                    f"{s['shot']}: expected a light in state {expected}, but the DOMAIN-STATE "
                    "line parsed no light entries — an unrecognised state token would look "
                    "exactly like this (see _LIGHT_ENTRY_RE)"
                )
                continue
            states = [light["state"] for light in s["lights"]]
            if expected not in states:
                failures.append(
                    f"{s['shot']}: expected a light in state {expected}, got {states}"
                )
    return failures


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

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = verify_common.detect_backend(build_dir)
    demo_dir = worktree / "creations" / "demos" / DEMO_NAME

    print(f"[light-verify] target={args.target}  backend={backend}")

    if not args.no_build:
        verify_common.run(["fleet-build", "--target", args.target], cwd=worktree)

    exe = verify_common.find_exe(build_dir, args.target, DEMO_NAME)
    shots_dir = exe.parent / SCREENSHOT_SUBDIR
    # Each pass rmtrees shots_dir on entry (verify_common.run_pass), so
    # per-pass diff PNGs must live in a sibling dir that outlives every pass —
    # nested under shots_dir they'd be wiped by the next pass before a human
    # inspects them (render-verify.py hit the identical multi-pass bug; see
    # its diff_dir routing in scripts/render-verify.py). Clear it once up
    # front so stale diffs from a prior run don't linger.
    diff_root = shots_dir.parent / "light_verify_diffs"
    if diff_root.exists():
        shutil.rmtree(diff_root)

    all_assertion_failures: list[str] = []
    all_image_results: list[tuple[str, str, dict[str, Any]]] = []  # (pass, label, result)
    any_run_crashed = False
    any_baselines_missing = False
    matched_state_patterns: set[str] = set()

    # Non-default targets get their own baseline subtree: the per-pass path is
    # keyed by CLI flag alone, so a second demo run through the same pass would
    # otherwise compare against the default target's geometry and fail on the
    # scene rather than on the light. DEFAULT_TARGET's path is left byte-for-byte
    # where it is so the committed references stay valid.
    baseline_root = demo_dir / "test" / "references" / backend / "light-verify"
    if args.target != DEFAULT_TARGET:
        baseline_root = baseline_root / args.target

    for flag in PASSES:
        print(f"\n[light-verify] === pass: --{flag} ===")
        run_cmd = [
            "fleet-run", "--timeout", str(args.timeout), args.target,
            f"--{flag}", "--auto-screenshot", str(args.warmup),
        ]
        rc, output, images = verify_common.run_pass(
            run_cmd, worktree, shots_dir, timeout=args.timeout + 30
        )
        domain_states = _parse_domain_state(output)
        run_crash = rc if rc != 0 else None
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
        # hover-sweep has no DOMAIN-STATE-derived assertion of its own (see
        # module docstring) — image comparison below is its only correctness
        # gate. Assertion 5 runs on every pass: its patterns are shot labels,
        # not pass names.
        all_assertion_failures.extend(
            _check_expected_states(args.target, domain_states, matched_state_patterns)
        )

        baseline_dir = baseline_root / flag
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
            result = verify_common.compare(
                image, reference, diff_dir / f"{label}.diff.png", LIGHT_VERIFY_THRESHOLDS
            )
            all_image_results.append((flag, label, result))

    expectations = EXPECTED_LIGHT_STATES.get(args.target, {})
    if expectations:
        print()
        print(f"[light-verify] assertion 5 — expected gather states for {args.target}:")
        for pattern, expected in expectations.items():
            hit = pattern in matched_state_patterns
            print(f"  {'checked' if hit else 'NO MATCH'}  {pattern:28} -> {expected}")
        # A pattern that matched nothing is a stale or mistyped key, which would
        # otherwise read as a silent pass — the same vacuity the regex whitelist
        # produced (#2330).
        for pattern in expectations:
            if pattern not in matched_state_patterns:
                all_assertion_failures.append(
                    f"expected-state pattern {pattern!r} matched no shot label in any pass "
                    f"for target {args.target}"
                )

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
