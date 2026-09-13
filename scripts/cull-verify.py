#!/usr/bin/env python3
"""Cull-regression harness for Irreden Engine (#1441).

Drives the shape_debug ``--cull-validate`` capture flow, then pairwise-compares
each live shot against the corresponding frozen shot.  A wide-viewport frozen
cull is a superset of the live cull at every pose; if live == frozen, the live
cull never dropped on-screen content.

The harness captures two phases in a single run:

  Phase 1 (live):   cv_live_000 … cv_live_NNN   — cull tracks the camera
  Separator:         cv_freeze_ref_000           — freeze at wide reference
  Phase 2 (frozen): cv_frozen_000 … cv_frozen_NNN — cull pinned at wide ref
  Trailer:           cv_unfreeze_000             — cleanup (not compared)

For each i in 0..N-1, live_i is compared against frozen_i.  A mismatch means
the live cull dropped on-screen geometry that the frozen cull retained.

The image assertion is relative by design — live_i vs frozen_i from the same
capture.  There is no committed cross-run baseline: absolute render drift on
shape_debug is render-verify's job (see docs/design/cull-validation-harness.md
"Cross-run contract"), and this harness must never grow a reference set it does
not read (#2955).

A relative image check cannot police the mechanism it exercises.  If the cull
freeze accepts the call and pins nothing, phase 2 re-walks the pose list with a
*live* cull, every pair is byte-identical, and the comparison above reports a
green pass with the freeze entirely disabled — and the broken arm scores
*better* (100 %) than the working one, so no threshold can separate them.  The
harness therefore carries a second, independent assertion set over the cull
*viewport state* the demo reports per captured frame:

    [cull-validate] DOMAIN-STATE shot=<label> frozen=<0|1> cull_frozen=<0|1>
      cull_cam=<x>,<y> cull_zoom=<x>,<y> cull_canvas=<w>,<h>
      cam=<x>,<y> zoom=<x>,<y>

Four arms, each failing the run and naming the freeze:

  (a) census    — one parsed line per captured frame, labels aligned with the
                  shot table.  Zero lines is a failure, never a silent pass.
  (b) engagement— the frozen phase reports the freeze flag set and honoured;
                  the live phase reports it clear.
  (c) pinning   — the frozen phase's cull viewport is constant and equals the
                  freeze-reference shot's own reading, and does not track the
                  live camera.
  (d) tracking  — the live phase moves its cull viewport, so a cull that never
                  updates at all cannot satisfy (c) by being globally frozen.

Arm (a) is what keeps this guard from becoming a second vacuous gate: a matcher
that silently drops every line would otherwise assert nothing.

Usage::

    python3 scripts/cull-verify.py                    # verify (build + run + compare)
    python3 scripts/cull-verify.py --no-build         # skip build (exe already fresh)
    python3 scripts/cull-verify.py --warmup 20        # more warmup frames

Assumes this file lives at ``<repo>/scripts/cull-verify.py``.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

import verify_common

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RENDER_COMPARE = SCRIPT_DIR / "render-compare.py"

DEMO_NAME = "shape_debug"
TARGET = "IRShapeDebug"
SCREENSHOT_SUBDIR = "save_files/screenshots"

# Cull-validate sweep shape — must match the constants in shape_debug/main.cpp.
# kYawSteps=8, kNumPans=4 → posesPerPhase=12.
POSES_PER_PHASE = 12
# Shot layout within the captured sequence (0-indexed):
#   [0 .. POSES_PER_PHASE-1]        live phase
#   [POSES_PER_PHASE]               freeze-ref marker (not compared)
#   [POSES_PER_PHASE+1 ..
#    2*POSES_PER_PHASE]             frozen phase
#   [2*POSES_PER_PHASE+1]           unfreeze marker (not compared)
LIVE_START = 0
LIVE_END = POSES_PER_PHASE          # exclusive
FROZEN_START = POSES_PER_PHASE + 1  # after freeze-ref
FROZEN_END = POSES_PER_PHASE * 2 + 1
TOTAL_SHOTS = POSES_PER_PHASE * 2 + 2  # live + freeze-ref + frozen + unfreeze

# Live shot i (offset from LIVE_START) pairs with frozen shot i (offset from FROZEN_START).
LIVE_LABELS = [f"cv_live_{i:03d}" for i in range(POSES_PER_PHASE)]
FROZEN_LABELS = [f"cv_frozen_{i:03d}" for i in range(POSES_PER_PHASE)]
FREEZE_REF_LABEL = "cv_freeze_ref_000"
UNFREEZE_LABEL = "cv_unfreeze_000"
# Every captured full frame, in capture order — the census arm's expected
# sequence, and the reason a state line that drifts out of step with the shot
# table is a failure rather than a re-pairing.
ALL_LABELS = LIVE_LABELS + [FREEZE_REF_LABEL] + FROZEN_LABELS + [UNFREEZE_LABEL]

# Thresholds calibrated to the P1 harness finding (issue #1438):
# at non-cardinal yaw the frozen and live passes differ in AO/light-volume shading
# because both computations use the cull viewport — even with sun shadows disabled.
# Observed in the P1 sweep: ~0.2 % of bytes differ by up to 89–127, always in
# AO-dependent regions, not in discrete voxel silhouettes.
# A genuine geometry drop (a visible entity missing from the live frame) would
# convert a coloured voxel region to background, dropping at least 0.1–0.3 % of
# bytes — detectable above this baseline with the 99.7 % match_pct threshold.
# max_delta is set permissively (200) because AO can produce large single-byte
# deltas; the fraction-of-bytes metric (match_pct) is the reliable gate.
CULL_THRESHOLDS: dict[str, Any] = {
    "per_pixel_tol": 8,
    "match_pct": 99.7,
    "max_delta": 200,
    "psnr_db": 30.0,
}


# --- Freeze guard: the cull-viewport state arms -----------------------------
#
# shape_debug's --cull-validate capture hook emits one of these per captured
# frame.  The tag is matched loosely and the field list is parsed as free-form
# key=value pairs, so the demo can add a field without breaking this parser
# (the light-verify DOMAIN-STATE tradition) — but every field the arms below
# read is required, and a line missing one is named by the census arm rather
# than silently skipped.
CULL_STATE_TAG = "[cull-validate] DOMAIN-STATE"
_CULL_STATE_RE = re.compile(r"\[cull-validate\]\s+DOMAIN-STATE\s+(\S.*)")
_FIELD_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(\S+)")

STATE_REQUIRED_FIELDS = (
    "shot", "frozen", "cull_frozen", "cull_cam", "cull_zoom", "cull_canvas",
    "cam", "zoom",
)

# The demo emits floats at 4 decimals; 1e-3 is loose enough to absorb that
# rounding and tight enough that a pinned viewport cannot be confused with one
# tracking a camera whose poses are whole trixels apart.
STATE_TOL = 1e-3

# Cap on the per-shot failure lines printed for the freeze guard.
GUARD_FAILURES_SHOWN = 12


def parse_cull_states(output: str) -> list[dict[str, str]]:
    """Every ``[cull-validate] DOMAIN-STATE`` line of ``output``, in order.

    Values stay strings; the arms below do their own typed reads so a malformed
    field is reported as a named failure instead of raising mid-run.
    """
    return [dict(_FIELD_RE.findall(m.group(1)))
            for m in _CULL_STATE_RE.finditer(output)]


def _nums(state: dict[str, str], key: str) -> tuple[float, ...] | None:
    raw = state.get(key)
    if raw is None:
        return None
    try:
        return tuple(float(part) for part in raw.split(","))
    except ValueError:
        return None


def _flag(state: dict[str, str], key: str) -> bool | None:
    raw = state.get(key)
    if raw not in ("0", "1"):
        return None
    return raw == "1"


def _close(a: tuple[float, ...] | None, b: tuple[float, ...] | None) -> bool:
    if a is None or b is None or len(a) != len(b):
        return False
    return all(abs(x - y) <= STATE_TOL for x, y in zip(a, b))


def _expects_frozen(label: str) -> bool:
    return label.startswith("cv_frozen_") or label.startswith("cv_freeze_ref")


def check_state_census(states: list[dict[str, str]],
                       labels: list[str]) -> list[str]:
    """Arm (a): the state lines exist, and line i describes shot i."""
    if not states:
        return [
            f"cull state not emitted — 0 `{CULL_STATE_TAG}` lines in the run "
            f"log, expected {len(labels)}. Every freeze arm reads those lines, "
            f"so with none the whole guard is vacuous: check that the "
            f"--cull-validate branch still sets "
            f"AutoScreenshotConfig::onCaptureFrame_ and that the build logs at "
            f"INFO."
        ]
    failures: list[str] = []
    if len(states) != len(labels):
        failures.append(
            f"cull state census: {len(states)} `{CULL_STATE_TAG}` lines, "
            f"expected {len(labels)} (one per captured full frame)"
        )
    for i, (state, label) in enumerate(zip(states, labels)):
        reported = state.get("shot")
        if reported != label:
            failures.append(
                f"cull state census: line {i} reports shot={reported!r}, the "
                f"shot table has {label!r} — the state lines no longer align "
                f"with the capture order the image pairing indexes by"
            )
        missing = [k for k in STATE_REQUIRED_FIELDS if k not in state]
        if missing:
            failures.append(
                f"cull state census: {reported or label} omits "
                f"{','.join(missing)}; the arms that read those fields cannot "
                f"judge this shot"
            )
    return failures


def check_freeze_engagement(states: list[dict[str, str]]) -> list[str]:
    """Arm (b): the freeze is on for the frozen phase and off for the live one.

    Both reported flags are checked because they fail independently: ``frozen``
    is what ``setCullingFrozen`` wrote, ``cull_frozen`` is what
    ``updateCullViewport`` last honoured. A setter that is ignored reds the
    first; a cull update that stopped running on the captured frame reds the
    second.
    """
    source = {
        "frozen": "IRRender::isCullingFrozen() — the flag the shot table set",
        "cull_frozen": "the cull viewport's own record of the last update",
    }
    failures: list[str] = []
    for state in states:
        label = state.get("shot", "<unlabeled>")
        want = _expects_frozen(label)
        for key, what in source.items():
            got = _flag(state, key)
            if got is None:
                failures.append(
                    f"cull freeze not engaged: {label} has no readable {key}= "
                    f"field ({what})"
                )
            elif got != want:
                verb = ("cull freeze not engaged" if want
                        else "cull freeze not released")
                failures.append(
                    f"{verb}: {label} reports {key}={int(got)}, expected "
                    f"{int(want)} — {what}"
                )
    return failures


def pinning_is_zoom_discriminating(states: list[dict[str, str]]) -> bool:
    """Whether the pinned zoom can tell a live cull from a frozen one.

    The freeze reference is hard-coded to zoom 1 while the sweep runs at
    ``--zoom`` (default 4), so pinned and live zoom normally differ and a
    frozen-phase reading that tracks the live camera is a defect. Under
    ``--zoom 1`` they coincide in a *correct* build, and the "differs from
    live" half of the pinning arm would then fire on correct behaviour. The
    constancy half is zoom-independent and stays in force either way.
    """
    ref = _find_freeze_ref(states)
    frozen = _frozen_phase(states)
    if ref is None or not frozen:
        return False
    ref_zoom = _nums(ref, "cull_zoom")
    return not any(_close(ref_zoom, _nums(s, "zoom")) for s in frozen)


def _find_freeze_ref(states: list[dict[str, str]]) -> dict[str, str] | None:
    return next((s for s in states
                 if s.get("shot", "").startswith("cv_freeze_ref")), None)


def _frozen_phase(states: list[dict[str, str]]) -> list[dict[str, str]]:
    return [s for s in states if s.get("shot", "").startswith("cv_frozen_")]


def check_frozen_pinning(states: list[dict[str, str]]) -> list[str]:
    """Arm (c): the frozen phase culls against one pinned, non-live viewport.

    Survives a defect that leaves the freeze flag true while
    ``updateCullViewport`` stops honouring it — arm (b) reads flags, this arm
    reads the viewport those flags are supposed to pin.
    """
    ref = _find_freeze_ref(states)
    frozen = _frozen_phase(states)
    if ref is None or not frozen:
        return [
            "cull viewport not pinned: the run log carries no "
            f"{FREEZE_REF_LABEL} / cv_frozen_* state lines to compare, so the "
            "pinning arm has nothing to judge"
        ]
    pinned = {key: _nums(ref, key)
              for key in ("cull_cam", "cull_zoom", "cull_canvas")}
    failures: list[str] = []
    missing = sorted(k for k, v in pinned.items() if v is None)
    if missing:
        return [
            f"cull viewport not pinned: {FREEZE_REF_LABEL} has no readable "
            f"{','.join(missing)} — the freeze reference every frozen shot is "
            f"compared against is unreadable"
        ]
    discriminating = pinning_is_zoom_discriminating(states)
    for state in frozen:
        label = state.get("shot", "<unlabeled>")
        for key, want in pinned.items():
            got = _nums(state, key)
            if not _close(got, want):
                failures.append(
                    f"cull viewport not pinned: {label} culled against "
                    f"{key}={state.get(key)}, but the freeze reference pinned "
                    f"{key}={ref.get(key)} — the frozen phase moved its cull "
                    f"viewport"
                )
        if discriminating and _close(_nums(state, "cull_zoom"),
                                     _nums(state, "zoom")):
            failures.append(
                f"cull viewport not pinned: {label} culled at "
                f"cull_zoom={state.get('cull_zoom')}, which is the live camera "
                f"zoom={state.get('zoom')} — the cull is tracking the camera "
                f"the freeze is supposed to have detached it from"
            )
    return failures


def check_live_tracking(states: list[dict[str, str]]) -> list[str]:
    """Arm (d): the live phase moves its cull viewport.

    Without this, a build whose cull viewport never updates at all passes the
    pinning arm by being globally frozen — the mirror image of the vacuity this
    whole guard exists to close.
    """
    live = [s for s in states if s.get("shot", "").startswith("cv_live_")]
    distinct: list[tuple[float, ...]] = []
    for state in live:
        cam = _nums(state, "cull_cam")
        if cam is not None and not any(_close(cam, seen) for seen in distinct):
            distinct.append(cam)
    if len(distinct) >= 2:
        return []
    return [
        f"cull viewport never moved: the live phase reports {len(distinct)} "
        f"distinct cull_cam value(s) across {len(live)} shot(s), so the pinning "
        f"arm above cannot distinguish a working freeze from a cull that never "
        f"updates"
    ]


def run_freeze_guard(output: str,
                     labels: list[str] | None = None
                     ) -> tuple[list[str], list[str]]:
    """All four arms over a captured run log. Returns (failures, notes)."""
    labels = ALL_LABELS if labels is None else labels
    states = parse_cull_states(output)
    failures = (
        check_state_census(states, labels)
        + check_freeze_engagement(states)
        + check_frozen_pinning(states)
        + check_live_tracking(states)
    )
    notes = [f"parsed {len(states)} cull-state line(s)"]
    if states and not pinning_is_zoom_discriminating(states):
        notes.append(
            "pinned zoom equals the sweep's live zoom, so the pinning arm ran "
            "constancy-only (pass --zoom with a value other than the freeze "
            "reference's to restore the differs-from-live half)"
        )
    return failures, notes


def _collect_shots(shots_dir: Path,
                   shots: list[Path] | None = None) -> list[Path]:
    """Count-guard and trim the run's captures to the shot table's length.

    The guard, the extras warning and the trim are this harness's, not
    ``verify_common.run_pass``'s: substituting run_pass's raw image list for a
    call here would let a short capture reach the live/frozen pairing instead
    of raising with the count. ``shots`` lets the caller hand back the list
    run_pass already collected rather than re-walking the directory; the
    directory-only form is what the crop-filter control drives.

    Full frames only either way — the pairing is positional (shots[i] vs
    shots[i + POSES_PER_PHASE + 1]), so a crop sorting between full frames
    shifts every index (verify_common.FULL_FRAME_RE).
    """
    if shots is None:
        shots = verify_common.collect_full_frames(shots_dir)
    if len(shots) < TOTAL_SHOTS:
        raise SystemExit(
            f"expected {TOTAL_SHOTS} screenshots in {shots_dir}, got {len(shots)}"
        )
    if len(shots) > TOTAL_SHOTS:
        print(
            f"[cull-verify] warning: captured {len(shots)} shots, "
            f"expected {TOTAL_SHOTS}; ignoring extras"
        )
    return shots[:TOTAL_SHOTS]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", default=None,
                    help="CMake build dir (default: <repo>/build).")
    ap.add_argument("--warmup", type=int, default=10,
                    help="Warmup frames before the first shot (default: 10).")
    ap.add_argument("--timeout", type=int, default=120,
                    help="Per-run timeout in seconds (default: 120).")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip fleet-build; assume the target is already built.")
    args = ap.parse_args(argv)

    # Fast-fail on a misconfigured checkout before the build+run cycle.
    if not RENDER_COMPARE.exists():
        raise SystemExit(f"render-compare.py not found at {RENDER_COMPARE}")

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = verify_common.detect_backend(build_dir)

    print(f"[cull-verify] target={TARGET}  backend={backend}")
    print(f"[cull-verify] {POSES_PER_PHASE} poses/phase × 2 + 2 markers = {TOTAL_SHOTS} shots")

    if not args.no_build:
        verify_common.run(["fleet-build", "--target", TARGET], cwd=worktree)

    exe = verify_common.find_exe(build_dir, TARGET, DEMO_NAME)
    shots_dir = exe.parent / SCREENSHOT_SUBDIR

    run_cmd = [
        "fleet-run", "--timeout", str(args.timeout), TARGET,
        "--cull-validate", "--auto-screenshot", str(args.warmup),
    ]
    # run_pass clears shots_dir itself and streams the demo's output instead of
    # swallowing it — the freeze guard below reads that output, so it has to
    # reach us whether or not the run exits clean.
    rc, output, captured = verify_common.run_pass(
        run_cmd, worktree, shots_dir, timeout=args.timeout + 30
    )
    run_crash: tuple[int, str] | None = None
    if rc != 0:
        print(
            f"[cull-verify] fleet-run exited {rc}; "
            f"tail of output follows:", file=sys.stderr,
        )
        tail = output.splitlines()[-40:]
        for line in tail:
            print(f"    {line}", file=sys.stderr)
        run_crash = (rc, "\n".join(tail))

    all_shots = _collect_shots(shots_dir, captured)
    live_shots = all_shots[LIVE_START:LIVE_END]
    frozen_shots = all_shots[FROZEN_START:FROZEN_END]
    if not (len(live_shots) == len(frozen_shots) == POSES_PER_PHASE):
        print(
            f"[cull-verify] expected {POSES_PER_PHASE} live and {POSES_PER_PHASE} frozen shots, "
            f"got {len(live_shots)} live / {len(frozen_shots)} frozen "
            f"(captured {len(all_shots)} total) — run did not produce the full pose set.",
            file=sys.stderr,
        )
        return 1

    # Nested under shots_dir, which run_pass rmtrees on every call. Safe only
    # because this harness runs exactly one pass and creates the directory
    # after it; a second pass would need diff_dir re-sited as a sibling.
    diff_dir = shots_dir / "cull_diffs"
    diff_dir.mkdir(exist_ok=True)

    print()
    print(f"{'pose':30} {'result':8} {'match%':>8} {'max_d':>6} {'psnr':>8}")
    print("-" * 66)
    all_pass = True
    failures: list[tuple[str, dict[str, Any]]] = []
    for i, (live, frozen, label) in enumerate(
        zip(live_shots, frozen_shots, LIVE_LABELS)
    ):
        diff_out = diff_dir / f"{label}_vs_frozen.diff.png"
        result = verify_common.compare(live, frozen, diff_out, CULL_THRESHOLDS)
        verdict = "PASS" if result["pass"] else "FAIL"
        raw_psnr = result["psnr_db"]
        psnr_str = f"{raw_psnr:>8.2f}" if isinstance(raw_psnr, (int, float)) else f"{raw_psnr:>8}"
        print(
            f"{label:30} {verdict:8} {result['match_pct']:>8.3f} "
            f"{result['max_delta']:>6} {psnr_str}"
        )
        if not result["pass"]:
            all_pass = False
            failures.append((label, result))

    guard_failures, guard_notes = run_freeze_guard(output)

    print()
    print("[cull-verify] freeze guard (cull-viewport state):")
    for note in guard_notes:
        print(f"  {note}")
    if not guard_failures:
        print("  census / engagement / pinning / live-tracking: PASS")

    print()
    if all_pass and not guard_failures and run_crash is None:
        print(f"[cull-verify] all {POSES_PER_PHASE} poses PASS — live cull is conservative")
        return 0

    if guard_failures:
        print(
            f"[cull-verify] {len(guard_failures)} freeze-guard assertion(s) FAIL "
            f"— the cull freeze this harness exercises is not doing its job, so "
            f"the live-vs-frozen comparison above proves nothing:"
        )
        # A fully disabled freeze reds most arms at most shots; the per-shot
        # enumeration is what pins down a single drifting pose, so keep enough
        # of it to serve that case without burying the verdict.
        for failure in guard_failures[:GUARD_FAILURES_SHOWN]:
            print(f"  {failure}")
        if len(guard_failures) > GUARD_FAILURES_SHOWN:
            print(
                f"  ... and {len(guard_failures) - GUARD_FAILURES_SHOWN} more "
                f"freeze-guard failure(s)"
            )

    if not all_pass:
        print(
            f"[cull-verify] {len(failures)} of {POSES_PER_PHASE} poses FAIL "
            f"— live cull dropped on-screen content at the following poses:"
        )
        for label, result in failures:
            diff = result.get("diff_path", "(no diff)")
            print(
                f"  {label}: match={result['match_pct']:.3f}% "
                f"max_delta={result['max_delta']}  diff={diff}"
            )
        print(
            "[cull-verify] A live/frozen mismatch means the live cull culled "
            "geometry that should have been visible.  Check the diff images "
            "under " + str(diff_dir)
        )

    if run_crash is not None:
        rc, _ = run_crash
        print(
            f"[cull-verify] demo crashed (fleet-run exit={rc}); "
            "failing even when shots match — see tail above.",
            file=sys.stderr,
        )
    return 1


if __name__ == "__main__":
    sys.exit(main())
