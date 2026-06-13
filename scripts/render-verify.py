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
  7. For each shot the manifest opts into a `crops` block, compare its ROI
     crops against committed reference crops (`<label>__crop_<crop>.png`).
  8. For each shot the manifest opts into a `structural` block, run the named
     `render-<metric>-metric.py` gate (e.g. shadow hole_ratio) on the capture.
  9. Print a pass/fail table and exit non-zero on any failure.

The `crops` and `structural` manifest blocks are both optional and additive:
a manifest without them runs exactly the original full-frame pixel-diff. See
``creations/demos/<demo>/test/references/manifest.json`` ``notes`` for the
per-shot schema. Structural gates are backend-agnostic (one threshold gates
both the macos-debug and linux-debug reference sets); ROI-crop pixel-diff is
per-backend like the full-frame path.

A shot listed in the optional `structural_only` block is still captured but
skips the full-frame pixel-diff and needs no committed reference PNG — it is
gated solely by its `structural` entries. This lets an analytic-oracle scene
(epic #1766 T-4) gate the zoom regime that pixel-diff excludes as
non-deterministic: the structural metric is compared against a computed
expectation, not a jittery captured reference, so it is deterministic at zoom
and shared across backends. A manifest whose shots are *all* structural_only
commits no reference PNGs at all.

`--update-references` copies the captured shots (and any manifest-declared ROI
crops) over the reference set for the current backend (after explicit
confirmation unless ``--force`` is given).

Assumes this file lives at ``<repo>/scripts/render-verify.py``.
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


# Full-frame captures are ``screenshot_<6-digit-index>.png``. ROI crop files
# share the prefix but append ``_<shotLabel>__crop_<crop>.png`` (see
# VideoManager::writePendingRoiCrops). A bare ``screenshot_*.png`` glob matches
# both, and since ``.`` < ``_`` the crops sort *between* full frames — so the
# first-N slice below would pick 128x128 crops as full shots. Match the
# index-only form so crops never enter the *full-frame* shot->reference
# mapping. Crops are still compared, but via the separate manifest-driven
# ``crops`` gate (see ``evaluate_shots``), not this index mapping.
_FULL_FRAME_RE = re.compile(r"screenshot_\d+\.png")


def _collect_shots(shots_dir: Path, num_shots: int) -> list[Path]:
    shots = sorted(
        p for p in shots_dir.glob("screenshot_*.png")
        if _FULL_FRAME_RE.fullmatch(p.name)
    )
    if len(shots) < num_shots:
        raise SystemExit(
            f"expected {num_shots} screenshots in {shots_dir}, got {len(shots)}"
        )
    if len(shots) > num_shots:
        print(
            f"[render-verify] warning: captured {len(shots)} shots, "
            f"manifest expects {num_shots}; ignoring extras"
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


# ── ROI crops ────────────────────────────────────────────────────────────
# A shot's ROI crops are captured by VideoManager::writePendingRoiCrops as
# ``screenshot_<idx>_<shotLabel>__crop_<cropLabel>.png`` alongside the
# full-frame ``screenshot_<idx>.png``. The committed reference for a crop
# drops the per-run index prefix: ``<shotLabel>__crop_<cropLabel>.png``.
# Crops are compared only when the manifest's ``crops`` block opts the shot
# in — a demo may emit inspection-only crops that aren't part of the gate.

def _crop_capture_path(fullframe: Path, shot_label: str, crop_label: str) -> Path:
    """Captured-crop path for a full-frame, given its shot + crop labels.

    Reuses the full-frame's index (its ``stem``) so the crop and frame stay
    paired even across re-runs where the absolute index drifts.
    """
    return fullframe.with_name(f"{fullframe.stem}_{shot_label}__crop_{crop_label}.png")


def _crop_reference_name(shot_label: str, crop_label: str) -> str:
    return f"{shot_label}__crop_{crop_label}.png"


def _label_index(shot_labels: list[str]) -> dict[str, int]:
    return {label: i for i, label in enumerate(shot_labels)}


# ── Structural-metric gates ──────────────────────────────────────────────
# A structural gate asserts a backend-agnostic, zoom-stable property of one
# capture (e.g. shadow hole_ratio) instead of a pixel-diff against a per-
# backend reference. Each ``metric`` name maps to a sibling
# ``render-<metric>-metric.py`` script with a uniform CLI contract:
#   * positional ``image`` (the PNG to measure)
#   * ``--roi x,y,w,h`` (optional; default = whole image)
#   * one ``--<threshold-key>`` flag per manifest threshold key
#     (``max_hole_ratio`` -> ``--max-hole-ratio`` etc.)
#   * emits a JSON object on stdout; exit 0 = within thresholds, 1 = a
#     threshold was exceeded, 2 = I/O or format error.
# render-shadow-metric.py (#1765) is the first implementer; T-3 adds
# coverage / silhouette / clip metrics behind the same contract.

def _run_structural_metric(image: Path, entry: dict[str, Any],
                           shot_label: str) -> dict[str, Any]:
    metric = entry.get("metric")
    if not metric:
        raise SystemExit(
            f"structural entry for shot '{shot_label}' is missing a 'metric' key"
        )
    threshold_keys = [k for k in entry if k not in ("metric", "roi")]
    if not threshold_keys:
        raise SystemExit(
            f"structural '{metric}' gate on shot '{shot_label}' declares no "
            f"thresholds — the gate would always pass; add e.g. max_hole_ratio"
        )
    script = SCRIPT_DIR / f"render-{metric}-metric.py"
    if not script.exists():
        raise SystemExit(
            f"structural metric '{metric}' (shot '{shot_label}') is not "
            f"implemented: no {script.name}. See epic #1766 T-3."
        )

    cmd = [sys.executable, str(script), str(image)]
    roi = entry.get("roi")
    if roi is not None:
        cmd.extend(["--roi", ",".join(str(v) for v in roi)])
    for key in threshold_keys:
        cmd.extend([f"--{key.replace('_', '-')}", str(entry[key])])

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode == 2:
        raise SystemExit(
            f"render-{metric}-metric errored on '{image.name}': {proc.stderr}"
        )
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as e:
        raise SystemExit(
            f"render-{metric}-metric returned non-JSON: {proc.stdout!r} ({e})"
        )
    # The exit code is the authoritative pass/fail (0 within thresholds, 1
    # exceeded); the JSON carries the measured metrics + a human reason.
    data["pass"] = proc.returncode == 0
    data["metric"] = metric
    return data


def evaluate_shots(
    *,
    captured: list[Path],
    shot_labels: list[str],
    ref_dir: Path,
    diff_dir: Path,
    thresholds: dict[str, Any],
    crops: dict[str, list[str]] | None = None,
    structural: dict[str, list[dict[str, Any]]] | None = None,
    structural_only: set[str] | None = None,
) -> list[dict[str, Any]]:
    """Compare captures and run structural gates; return one row per check.

    Each row is ``{label, kind, pass, ...}`` where ``kind`` is ``frame``
    (full-frame pixel-diff), ``crop`` (ROI-crop pixel-diff) or ``struct``
    (structural-metric gate). Pure given its inputs — no build/run — so the
    gate logic is unit-testable against synthetic captures + references.

    ``structural_only`` shots are still captured (so the index→reference map
    stays aligned) but skip the full-frame pixel-diff and need no committed
    reference PNG — they're gated purely by their ``structural`` entries
    (an analytic oracle, deterministic at zoom where pixel-diff is excluded).
    """
    rows: list[dict[str, Any]] = []
    crops = crops or {}
    structural = structural or {}
    structural_only = structural_only or set()
    label_index = _label_index(shot_labels)

    # 1. Full-frame pixel-diff (the original gate; unchanged behavior). A
    #    structural-only shot is captured but not pixel-diffed — it has no
    #    reference PNG and is gated solely by its structural entries (step 3).
    for actual, label in zip(captured, shot_labels):
        if label in structural_only:
            continue
        reference = ref_dir / f"{label}.png"
        if not reference.exists():
            rows.append({"label": label, "kind": "frame", "pass": False,
                         "reason": f"no reference at {reference}"})
            continue
        result = _compare_shot(actual, reference, thresholds,
                               diff_dir / f"{label}.diff.png")
        rows.append({"label": label, "kind": "frame",
                     "pass": result["pass"], "result": result})

    # 2. ROI-crop pixel-diff (manifest-opted-in shots only).
    for label, crop_labels in crops.items():
        if label not in label_index:
            raise SystemExit(
                f"manifest 'crops' references unknown shot '{label}' "
                f"(not in 'shots')"
            )
        fullframe = captured[label_index[label]]
        for crop_label in crop_labels:
            disp = f"{label}:{crop_label}"
            actual_crop = _crop_capture_path(fullframe, label, crop_label)
            reference = ref_dir / _crop_reference_name(label, crop_label)
            if not actual_crop.exists():
                rows.append({"label": disp, "kind": "crop", "pass": False,
                             "reason": f"crop not captured ({actual_crop.name}) "
                                       f"— does the shot declare this RoiCrop?"})
                continue
            if not reference.exists():
                rows.append({"label": disp, "kind": "crop", "pass": False,
                             "reason": f"no reference at {reference}"})
                continue
            result = _compare_shot(actual_crop, reference, thresholds,
                                   diff_dir / f"{label}__crop_{crop_label}.diff.png")
            rows.append({"label": disp, "kind": "crop",
                         "pass": result["pass"], "result": result})

    # 3. Structural-metric gates (manifest-opted-in shots only).
    for label, entries in structural.items():
        if label not in label_index:
            raise SystemExit(
                f"manifest 'structural' references unknown shot '{label}' "
                f"(not in 'shots')"
            )
        fullframe = captured[label_index[label]]
        for entry in entries:
            result = _run_structural_metric(fullframe, entry, label)
            disp = f"{label}:{result['metric']}"
            rows.append({"label": disp, "kind": "struct",
                         "pass": result["pass"], "result": result,
                         "reason": result.get("reason")})

    return rows


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
    ap.add_argument("--warmup", type=int, default=None,
                    help="Warmup frames before the first shot (default: 10, or "
                         "manifest['warmup'] if set; CLI value always wins).")
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
    # Optional, additive gate blocks (a manifest without them behaves exactly
    # as before — full-frame pixel-diff only).
    crops_block: dict[str, list[str]] = manifest.get("crops", {})
    structural_block: dict[str, list[dict[str, Any]]] = manifest.get("structural", {})
    # Shots gated purely by structural metrics: still captured (for the
    # index→reference alignment) but no full-frame pixel-diff and no committed
    # reference PNG. This is how an analytic-oracle scene gates the zoom regime
    # that pixel-diff excludes (epic #1766 T-4). Each must be a declared shot
    # and must carry a structural gate, else it would be captured-but-ungated.
    structural_only: set[str] = set(manifest.get("structural_only", []))
    for label in structural_only:
        if label not in shot_labels:
            raise SystemExit(
                f"manifest 'structural_only' references unknown shot '{label}' "
                f"(not in 'shots')"
            )
        if label not in structural_block:
            raise SystemExit(
                f"shot '{label}' is 'structural_only' but has no 'structural' "
                f"gate — it would be captured but never checked. Add a "
                f"structural entry or drop it from structural_only."
            )
    screenshot_subdir = manifest.get("screenshot_subdir", "save_files/screenshots")
    # CLI --warmup wins; manifest["warmup"] overrides the hardcoded default of 10.
    warmup: int = args.warmup if args.warmup is not None else manifest.get("warmup", 10)

    print(f"[render-verify] target={args.target} demo={demo_name} backend={backend} warmup={warmup}")
    print(f"[render-verify] {len(shot_labels)} shots: {', '.join(shot_labels)}")

    if not args.no_build:
        _run(["fleet-build", "--target", args.target], cwd=worktree)

    exe = _find_exe(build_dir, args.target, demo_name)
    shots_dir = exe.parent / screenshot_subdir
    if shots_dir.exists():
        shutil.rmtree(shots_dir)
    shots_dir.mkdir(parents=True, exist_ok=True)

    run_cmd = ["fleet-run", "--timeout", str(args.timeout), args.target,
               "--auto-screenshot", str(warmup)]
    print("+ " + " ".join(run_cmd), flush=True)
    proc = subprocess.run(run_cmd, cwd=str(worktree),
                          capture_output=True, text=True)
    # In `--auto-screenshot` mode the demo fires `closeWindow()` after the
    # last shot and exits 0; any non-zero return is a crash (e.g. a Metal
    # static-destruction segfault that lands AFTER the screenshots are
    # saved, which the per-shot comparator would otherwise silently
    # "pass" — T-336). With `--timeout`, ir-run returns 0 on timeout-kill
    # too, so the only way we see non-zero here is a real early-exit
    # crash. Surface the tail and let the run_crash flag block a PASS
    # verdict below even when all shots compare clean.
    run_crash: tuple[int, str] | None = None
    if proc.returncode != 0:
        print(f"[render-verify] fleet-run exited {proc.returncode}; "
              f"tail of output follows (screenshot count will be checked "
              f"against manifest below):", file=sys.stderr)
        tail = (proc.stdout + proc.stderr).splitlines()[-40:]
        for line in tail:
            print(f"    {line}", file=sys.stderr)
        run_crash = (proc.returncode, "\n".join(tail))

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
            if label in structural_only:
                continue  # threshold-gated, no reference PNG to write
            dest = ref_dir / f"{label}.png"
            shutil.copy2(actual, dest)
            print(f"[render-verify] updated {dest}")
        # Also snapshot any manifest-declared ROI crops so the crop gate has
        # a baseline. Structural gates are threshold-based (no reference PNG),
        # so they need nothing here.
        label_index = _label_index(shot_labels)
        for label, crop_labels in crops_block.items():
            if label not in label_index:
                continue
            fullframe = captured[label_index[label]]
            for crop_label in crop_labels:
                src = _crop_capture_path(fullframe, label, crop_label)
                if not src.exists():
                    print(f"[render-verify] warning: declared crop not captured, "
                          f"skipping ({src.name})")
                    continue
                dest = ref_dir / _crop_reference_name(label, crop_label)
                shutil.copy2(src, dest)
                print(f"[render-verify] updated {dest}")
        return 0

    # A reference set is only required when at least one shot is pixel-diffed.
    # An all-structural-only manifest (a pure analytic oracle) is backend-
    # agnostic and commits no reference PNGs, so the missing-reference guard
    # must not fire for it.
    has_pixel_diff_shot = any(label not in structural_only for label in shot_labels)
    if has_pixel_diff_shot and (
        not ref_dir.exists() or not any(ref_dir.glob("*.png"))
    ):
        print(
            f"[render-verify] no references found for backend '{backend}' at "
            f"{ref_dir}. Run with --update-references to capture them.",
            file=sys.stderr,
        )
        return 2

    rows = evaluate_shots(
        captured=captured,
        shot_labels=shot_labels,
        ref_dir=ref_dir,
        diff_dir=diff_dir,
        thresholds=thresholds,
        crops=crops_block,
        structural=structural_block,
        structural_only=structural_only,
    )

    print()
    print(f"{'shot':40} {'result':8} {'match%':>8} {'max_d':>6} {'psnr':>8}")
    print("-" * 76)
    failures: list[dict[str, Any]] = []
    for row in rows:
        verdict = "PASS" if row["pass"] else "FAIL"
        result = row.get("result")
        if row["kind"] in ("frame", "crop") and result and "match_pct" in result:
            print(f"{row['label']:40} {verdict:8} {result['match_pct']:>8} "
                  f"{result['max_delta']:>6} {result['psnr_db']:>8}")
        else:
            # MISSING / not-captured rows and structural gates have no
            # pixel-diff numbers — show the reason in the wide column.
            reason = row.get("reason") or (result.get("reason") if result else "") or ""
            print(f"{row['label']:40} {verdict:8} {reason}")
        if not row["pass"]:
            failures.append(row)
    all_pass = not failures

    print()
    if all_pass and run_crash is None:
        print(f"[render-verify] all {len(rows)} checks PASS "
              f"({len(shot_labels)} frames"
              f"{f', {sum(len(v) for v in crops_block.values())} crops' if crops_block else ''}"
              f"{f', {sum(len(v) for v in structural_block.values())} structural' if structural_block else ''})")
        return 0

    if not all_pass:
        print(f"[render-verify] {len(failures)} of {len(rows)} checks FAIL")
        for row in failures:
            result = row.get("result") or {}
            reason = row.get("reason") or result.get("reason", "mismatch")
            diff = result.get("diff_path", "(no diff)")
            print(f"  - {row['label']}: {reason}  diff={diff}")

    if run_crash is not None:
        rc, _ = run_crash
        print(f"[render-verify] demo crashed at shutdown (fleet-run exit={rc}); "
              f"failing the verify run even when shots match — see tail above.",
              file=sys.stderr)
    return 1


def _target_to_demo_name(target: str) -> str:
    """Map a CMake target name to the on-disk demo directory name.

    Strips a leading ``IR`` prefix and splits CamelCase into snake_case.
    Limitation: consecutive uppercase letters are NOT split — an
    acronym-prefixed target like ``IRMIDIKeyboard`` becomes
    ``midikeyboard`` rather than ``midi_keyboard``. When the mapping is
    wrong, pass ``--demo <name>`` explicitly to override.
    """
    name = target[2:] if target.startswith("IR") else target
    out: list[str] = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0 and not name[i - 1].isupper():
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


if __name__ == "__main__":
    sys.exit(main())
