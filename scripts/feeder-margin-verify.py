#!/usr/bin/env python3
"""Shadow-feeder classify-margin adequacy gate for Irreden Engine (#3010).

Stage 2 skips the colour / entity-id taps of any voxel classified as an
off-screen **shadow feeder** (the #1740 depth-only path).  That is only safe if
no *on-screen* pixel ever resolves from a feeder — which holds because
``visibleIsoBounds`` carries a ``+kGpuMargin`` (4 iso-texel) pad that covers
stage 1's cardinal write set (``base + {0,1}x{0,1,2}``, reach +1 x / +2 y).

At the shipped margin that property is **invisible**: every capture is
byte-identical whether or not the skip is correct, so byte-identity alone is
worthless as a gate (``engine/render/CLAUDE.md`` §"Verifying render changes").
This harness makes it observable by running ``IRPerfGrid`` three times with the
diagnostic ``--feeder-classify-pad`` knob, which pads
``frameData_.visibleIsoBounds_`` and nothing else — the cull box, the Hi-Z
window and the #2488 ring guard keep reading the unpadded values, and at
``--subdivision-mode none`` (``feederSubCap == subdivisions == 1``) a voxel's
stage-1 depth is identical on either side of the classification.  So a pixel
can only change through stage 2's colour tap:

  * **pad 0** — the shipped baseline, and the source of the non-vacuity witness.
  * **pad +12 (adequacy)** — the classify box is 12 texels wider, so off-screen
    feeders in the ``[visible+4, visible+16)`` band become visibles.  If the
    shipped margin were inadequate, some on-screen pixel would be resolving
    from a feeder today and promoting it would change that pixel.  It must
    change **nothing**.
  * **pad -16 (liveness)** — the box is 12 texels *inside* the viewport, so
    on-screen winners become feeders.  It must change **something**, otherwise
    the instrument is not wired to anything and the adequacy arm's zero is
    meaningless.

Three verdict rules, all of which must hold for exit 0:

  1. ``liveness`` changes ``zoom4_pan``          — else ``vacuous instrument``
  2. ``adequacy`` changes no compared shot       — else ``feeder-won on-screen pixels``
  3. the pad-0 arm reports a non-empty feeder ring
                                                 — else ``vacuous adequacy arm``

Rule 3 is not redundant with rule 1.  ``isShadowFeederIso`` tests only against
``visibleIsoBounds`` — there is no upper cull-bound term — so the two arms are
asymmetric: the shrink arm manufactures feeders out of on-screen winners and
fires even when the sun-feeder ring is empty, while the widen arm can only
change a pixel if there were feeders in the band **to promote**.  With sun
shadows off (``--no-sun-shadows``, or a scene with no directional light)
``shadowFeederCullViewport`` returns ``visible`` unchanged, every survivor is
inside ``visibleIsoBounds``, and the adequacy arm reads 0 *tautologically*
while the liveness arm still fires.  The engine publishes the witness through
``IRPrefab::SunShadow::feederClassifyRingNonEmpty()``; ``perf_grid`` logs it per
shot as ``FEEDER-CLASSIFY ... ring_non_empty=<0|1>``.

Only **cardinal** shots are compared.  ``perf_grid``'s two rotated shots
(``zoom4_rot`` / ``zoom4_rot_pan``) are run-to-run non-deterministic on this
demo (3 distinct hashes in 3 runs, per #3010), so an identity claim over them
would be noise.  The skip itself is structurally inert at ``residualYaw != 0``
anyway — the predicate's own route terms exclude it — so cardinal is the only
regime where the question is live.

Usage::

    python3 scripts/feeder-margin-verify.py                   # build + 3 runs + verdict
    python3 scripts/feeder-margin-verify.py --no-build        # exe already fresh
    python3 scripts/feeder-margin-verify.py --liveness-pad 0  # prove the vacuity guard fires

Assumes this file lives at ``<repo>/scripts/feeder-margin-verify.py``.
Pure stdlib, mirroring the rest of ``scripts/``.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

import render_metric_util
import verify_common

read_png = render_metric_util.read_png

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

DEMO_NAME = "perf_grid"
TARGET = "IRPerfGrid"
SCREENSHOT_SUBDIR = "save_files/screenshots"

# Must match `kShots` in creations/demos/perf_grid/main.cpp — the harness pairs
# arms positionally, so a shot inserted there without updating this list would
# silently compare the wrong pair.  The demo logs the label it captured for each
# index (FEEDER-CLASSIFY ... label=), and `check_shot_labels` below asserts the
# two agree rather than trusting the copy.
SHOT_LABELS = (
    "fit_grid",
    "zoom1_origin",
    "profiler_overlay",
    "zoom1_rot",
    "zoom4_rot",
    "zoom4_pan",
    "zoom4_rot_pan",
)
# The cardinal (yaw 0) subset — the only shots an identity claim may cover.
COMPARED_INDICES = (0, 1, 2, 5)
# The shot the liveness arm must move.  It is the only one whose grid content
# reaches the frame edge at the recipe's zoom/offset, so it is the only one
# where a shrunken classify box has on-screen winners to demote.
LIVENESS_INDEX = 5

DEFAULT_ADEQUACY_PAD = 12
DEFAULT_LIVENESS_PAD = -16

FEEDER_CLASSIFY_RE = re.compile(
    r"FEEDER-CLASSIFY\s+idx=(?P<idx>-?\d+)\s+label=(?P<label>\S+)\s+"
    r"pad=(?P<pad>-?\d+)\s+ring_non_empty=(?P<ring>[01])"
)

# ---------------------------------------------------------------------------
# Pure logic — importable and unit-tested (scripts/tests/test_feeder_margin_verify.py).
# ---------------------------------------------------------------------------


def parse_feeder_classify(output: str) -> list[dict[str, object]]:
    """Every ``FEEDER-CLASSIFY`` line in a run's output, in capture order."""
    records: list[dict[str, object]] = []
    for m in FEEDER_CLASSIFY_RE.finditer(output):
        records.append(
            {
                "idx": int(m.group("idx")),
                "label": m.group("label"),
                "pad": int(m.group("pad")),
                "ring_non_empty": m.group("ring") == "1",
            }
        )
    return records


def ring_non_empty(records: list[dict[str, object]]) -> bool:
    """The adequacy arm's non-vacuity witness for a whole run.

    True when ANY captured shot classified against a non-empty off-screen
    feeder ring.  Any is the right quantifier: one shot with feeders to promote
    is enough to make a widened-pad zero informative, and the low-zoom shots
    legitimately have the grid nowhere near the viewport edge.  An empty list
    (no witness lines at all — a demo too old for the knob, or a run that
    captured nothing) is False, which fails closed.
    """
    return any(bool(r["ring_non_empty"]) for r in records)


Image = tuple[int, int, int, bytes]  # (width, height, bytes-per-pixel, pixels) — read_png's shape


def census(base: Image, arm: Image) -> dict[str, object]:
    """Exact per-pixel diff of two ``read_png`` tuples.

    Returns ``{"changed": n, "bbox": (x0, y0, x1, y1) | None}``.  Deliberately
    exact rather than tolerance-based: both arms are the same build rendering
    the same frozen scene, so any tolerance would only hide the signal.
    """
    w, h, bpp, pixels = base
    w2, h2, bpp2, pixels2 = arm
    if (w, h, bpp) != (w2, h2, bpp2):
        raise ValueError(f"shot geometry differs: {(w, h, bpp)} vs {(w2, h2, bpp2)}")
    changed = 0
    x0 = y0 = None
    x1 = y1 = None
    for offset in range(0, len(pixels), bpp):
        if pixels[offset : offset + bpp] == pixels2[offset : offset + bpp]:
            continue
        changed += 1
        index = offset // bpp
        px, py = index % w, index // w
        if x0 is None:
            x0, y0, x1, y1 = px, py, px, py
        else:
            x0, y0 = min(x0, px), min(y0, py)
            x1, y1 = max(x1, px), max(y1, py)
    bbox = None if x0 is None else (x0, y0, x1, y1)
    return {"changed": changed, "bbox": bbox}


def evaluate(
    adequacy: dict[int, dict[str, object]],
    liveness: dict[int, dict[str, object]],
    baseline_ring_non_empty: bool,
    liveness_pad: int,
) -> list[tuple[str, str]]:
    """The three verdict rules.  Returns ``[(verdict, detail), ...]``; empty == pass.

    Order is diagnostic, not severity: a vacuous instrument invalidates the
    adequacy result, so it is reported first, and the two vacuity verdicts are
    kept distinct because they have different causes and different fixes.
    """
    failures: list[tuple[str, str]] = []

    live = liveness.get(LIVENESS_INDEX, {"changed": 0, "bbox": None})
    if int(live["changed"]) == 0:
        failures.append(
            (
                "vacuous instrument",
                f"the liveness arm (pad {liveness_pad}) changed 0 pixels on "
                f"{SHOT_LABELS[LIVENESS_INDEX]}. A shrunken classify box must demote "
                f"on-screen winners to feeders; if it changes nothing, the pad is not "
                f"reaching the classify box and the adequacy arm's zero proves nothing.",
            )
        )

    if not baseline_ring_non_empty:
        failures.append(
            (
                "vacuous adequacy arm",
                "the pad-0 arm reported an EMPTY off-screen shadow-feeder ring "
                "(ring_non_empty=0 on every shot), so the adequacy arm had zero "
                "feeders to promote and its 0-changed-pixels result is tautological. "
                "Check that sun shadows are on in this scene (see #3010 plan-review C1).",
            )
        )

    dirty = sorted(i for i, r in adequacy.items() if int(r["changed"]) > 0)
    if dirty:
        detail = "; ".join(
            f"{SHOT_LABELS[i]}: {adequacy[i]['changed']} px bbox={adequacy[i]['bbox']}"
            for i in dirty
        )
        failures.append(
            (
                "feeder-won on-screen pixels",
                "widening the classify box changed on-screen pixels, so those pixels "
                f"were resolving from a depth-only shadow feeder whose colour tap stage 2 "
                f"skipped — the #1740 margin is inadequate against stage 1's emit hull. {detail}",
            )
        )

    return failures


def check_shot_labels(records: list[dict[str, object]]) -> str | None:
    """Guard SHOT_LABELS against drift in perf_grid's kShots table.

    The arms are paired by positional index, so a shot added to (or reordered
    in) the demo without updating SHOT_LABELS would compare mismatched frames
    and report a confident wrong answer.  Returns an error string, or None.
    """
    for record in records:
        idx = int(record["idx"])
        if not (0 <= idx < len(SHOT_LABELS)):
            return f"demo captured shot index {idx}, outside SHOT_LABELS (len {len(SHOT_LABELS)})"
        if record["label"] != SHOT_LABELS[idx]:
            return (
                f"shot index {idx} is '{record['label']}' in the demo but "
                f"'{SHOT_LABELS[idx]}' in SHOT_LABELS — update this script's table "
                f"to match creations/demos/perf_grid/main.cpp kShots"
            )
    return None


# ---------------------------------------------------------------------------
# Run driving
# ---------------------------------------------------------------------------


def _run_arm(
    worktree: Path,
    shots_dir: Path,
    pad: int,
    warmup: int,
    timeout: int,
    subdivision_mode: str,
) -> tuple[list[Path], str, int]:
    """One capture arm, via ``verify_common.run_pass``.

    ``run_pass`` wipes ``shots_dir`` before running, which this harness depends
    on: screenshot numbering continues from leftovers
    (``VideoManager::reserveNextScreenshotIndex``), so a stale directory would
    shift every positional arm-to-arm pairing.  Its ``collect_full_frames``
    excludes ROI crops for the same reason.  The arm stashes its captures to a
    sibling directory afterwards — never under ``shots_dir``, which the next
    arm's ``run_pass`` would wipe (the #2356 hazard its docstring names).
    """
    cmd = [
        "fleet-run", "--timeout", str(timeout), TARGET,
        "--mode", "voxel_set",
        "--no-overlay",
        "--subdivision-mode", subdivision_mode,
        "--wave-freeze",
        "--wave-amplitude", "5",
        # Part of #3010's published recipe.  zoom4_pan is byte-identical either
        # way, so this only keeps the numbers comparable with the issue.
        "--occlusion-cull",
        "--feeder-classify-pad", str(pad),
        "--auto-screenshot", str(warmup),
    ]
    rc, output, shots = verify_common.run_pass(cmd, cwd=worktree, shots_dir=shots_dir)
    return shots, output, rc


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", default=None,
                    help="CMake build dir (default: <repo>/build).")
    ap.add_argument("--warmup", type=int, default=10,
                    help="Warmup frames before the first shot (default: 10).")
    ap.add_argument("--timeout", type=int, default=180,
                    help="Per-run timeout in seconds (default: 180).")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip fleet-build; assume the target is already built.")
    ap.add_argument("--liveness-pad", type=int, default=DEFAULT_LIVENESS_PAD,
                    help=f"Classify pad for the liveness arm (default: {DEFAULT_LIVENESS_PAD}). "
                         "0 makes the arm identical to baseline and MUST fail as "
                         "'vacuous instrument' — that is how the vacuity guard is tested.")
    ap.add_argument("--adequacy-pad", type=int, default=DEFAULT_ADEQUACY_PAD,
                    help=f"Classify pad for the adequacy arm (default: +{DEFAULT_ADEQUACY_PAD}). "
                         "Must be positive to promote band feeders to visibles.")
    ap.add_argument("--subdivision-mode", default="none",
                    help="Passthrough to --subdivision-mode (default: none). The shadow-neutral "
                         "argument rests on feederSubCap == subdivisions, which holds at 'none'; "
                         "other modes are unmeasured (#3010 out-of-scope).")
    args = ap.parse_args(argv)

    if args.adequacy_pad <= 0:
        raise SystemExit(
            f"--adequacy-pad must be positive (got {args.adequacy_pad}); a non-positive "
            "pad does not promote band feeders and cannot test margin adequacy."
        )
    if args.subdivision_mode != "none":
        print(
            f"[feeder-margin-verify] WARNING: --subdivision-mode {args.subdivision_mode} is an "
            "unmeasured regime — the shadow-neutral premise (feederSubCap == subdivisions) only "
            "holds at 'none', so a FAIL here may reflect subdivision-density drift rather than an "
            "inadequate margin (#3010 out-of-scope).",
            file=sys.stderr,
        )

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = verify_common.detect_backend(build_dir)

    print(f"[feeder-margin-verify] target={TARGET}  backend={backend}")
    print(
        f"[feeder-margin-verify] arms: pad=0 (baseline) / pad=+{args.adequacy_pad} (adequacy) "
        f"/ pad={args.liveness_pad} (liveness)"
    )

    if not args.no_build:
        verify_common.run(["fleet-build", "--target", TARGET], cwd=worktree)

    exe = verify_common.find_exe(build_dir, TARGET, DEMO_NAME)
    shots_dir = exe.parent / SCREENSHOT_SUBDIR

    arms: dict[str, tuple[list[Path], str, int]] = {}
    stash_root = build_dir / "_feeder_margin_arms"
    if stash_root.exists():
        shutil.rmtree(stash_root)
    for name, pad in (
        ("baseline", 0),
        ("adequacy", args.adequacy_pad),
        ("liveness", args.liveness_pad),
    ):
        shots, output, rc = _run_arm(
            worktree, shots_dir, pad, args.warmup, args.timeout, args.subdivision_mode
        )
        if rc != 0:
            print(
                f"[feeder-margin-verify] the {name} arm (pad {pad}) did not exit cleanly "
                f"— refusing to report a verdict on an incomplete capture.",
                file=sys.stderr,
            )
            return 1
        # Park the arm: the next arm wipes the live shots dir.
        stash = stash_root / name
        stash.mkdir(parents=True, exist_ok=True)
        parked = []
        for shot in shots:
            dest = stash / shot.name
            shutil.copy2(shot, dest)
            parked.append(dest)
        arms[name] = (parked, output, rc)

    baseline_shots, baseline_output, _ = arms["baseline"]
    records = parse_feeder_classify(baseline_output)
    label_error = check_shot_labels(records)
    if label_error is not None:
        print(f"[feeder-margin-verify] {label_error}", file=sys.stderr)
        return 1
    if len(baseline_shots) < len(SHOT_LABELS):
        print(
            f"[feeder-margin-verify] expected {len(SHOT_LABELS)} shots per arm, "
            f"baseline captured {len(baseline_shots)} — run did not produce the full set.",
            file=sys.stderr,
        )
        return 1

    baseline_pngs = {i: read_png(str(baseline_shots[i])) for i in COMPARED_INDICES}
    results: dict[str, dict[int, dict[str, object]]] = {}
    for name in ("adequacy", "liveness"):
        shots = arms[name][0]
        if len(shots) < len(SHOT_LABELS):
            print(
                f"[feeder-margin-verify] the {name} arm captured {len(shots)} shots, "
                f"expected {len(SHOT_LABELS)}.",
                file=sys.stderr,
            )
            return 1
        results[name] = {
            i: census(baseline_pngs[i], read_png(str(shots[i]))) for i in COMPARED_INDICES
        }

    witness = ring_non_empty(records)

    print()
    print(f"{'shot':20} {'adequacy Δpx':>14} {'liveness Δpx':>14}")
    print("-" * 50)
    for i in COMPARED_INDICES:
        print(
            f"{SHOT_LABELS[i]:20} {results['adequacy'][i]['changed']:>14} "
            f"{results['liveness'][i]['changed']:>14}"
        )
    print()
    print(f"[feeder-margin-verify] pad-0 feeder ring non-empty: {witness}")
    live_bbox = results["liveness"][LIVENESS_INDEX]["bbox"]
    print(f"[feeder-margin-verify] liveness bbox on {SHOT_LABELS[LIVENESS_INDEX]}: {live_bbox}")

    failures = evaluate(results["adequacy"], results["liveness"], witness, args.liveness_pad)
    if not failures:
        print()
        print(
            "[feeder-margin-verify] PASS — the shipped classify margin is adequate: "
            "widening it moves no on-screen pixel, the instrument demonstrably fires "
            "when it is shrunk, and the promoted population was non-empty."
        )
        return 0

    print()
    for verdict, detail in failures:
        print(f"[feeder-margin-verify] FAIL ({verdict}): {detail}", file=sys.stderr)
    print(f"[feeder-margin-verify] arm captures kept under {stash_root}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
