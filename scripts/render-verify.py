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

The optional ``extra_runs`` manifest block declares additional capture passes
of the same target, each with its own demo args and its own gated reference
subset. This gates shots the default scene *suppresses* — e.g. canvas_stress's
``compare_yaw0`` / ``compare_yaw_q``, which only spawn under ``--only compare``
(a flag that hides the default groups, so the two can't share one capture).
Each extra pass re-runs the demo, slices its gated shots out of the capture by
``capture_offset`` (negative = from the tail, for end-appended shots), and
pixel-diffs them against the same backend reference dir. Because an extra
pass's references are blessed per-host (a fresh shot is captured on macOS, then
on Linux), a backend that has not yet committed them *skips* that pass instead
of failing the run — the gate engages on a backend once its references exist.

`--update-references` copies the captured shots (and any manifest-declared ROI
crops) over the reference set for the current backend (after explicit
confirmation unless ``--force`` is given). It blesses the default pass and
every ``extra_runs`` pass, so a first run on a new host bootstraps all of them.

Assumes this file lives at ``<repo>/scripts/render-verify.py``.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

import verify_common

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
RENDER_COMPARE = SCRIPT_DIR / "render-compare.py"


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


def _collect_all_shots(shots_dir: Path) -> list[Path]:
    """All full-frame captures in index order, with no count expectation.

    The default pass knows exactly how many shots it gates (``_collect_shots``
    asserts the count). An ``extra_runs`` pass instead gates a *slice* of a
    larger capture — e.g. canvas_stress ``--only compare`` still emits every
    default-scene shot (the leading frames render a near-empty scene) and
    appends the compare shots at the tail — so its mapping is positional into
    the full list, resolved by the pass's ``capture_offset``.
    """
    return sorted(
        p for p in shots_dir.glob("screenshot_*.png")
        if _FULL_FRAME_RE.fullmatch(p.name)
    )


def _collect_shots(shots_dir: Path, num_shots: int) -> list[Path]:
    shots = _collect_all_shots(shots_dir)
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


def _slice_capture(all_shots: list[Path], offset: int, count: int,
                   pass_name: str) -> list[Path]:
    """Pick ``count`` captures starting at ``offset`` (Python-negative ok).

    A negative ``offset`` indexes from the end, so a tail-appended block (the
    compare shots) stays correctly mapped even as leading default shots are
    added or removed over time — the offset only couples to the block's own
    length, which is co-located in the demo's shot-list source.
    """
    start = offset if offset >= 0 else len(all_shots) + offset
    if start < 0 or start + count > len(all_shots):
        raise SystemExit(
            f"extra run '{pass_name}': capture_offset {offset} + {count} shots "
            f"is out of range for the {len(all_shots)} captured screenshots — "
            f"did the demo args emit a different shot count than expected?"
        )
    return all_shots[start:start + count]


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
    missing_ref_is_skip: bool = False,
    backend: str | None = None,
) -> list[dict[str, Any]]:
    """Compare captures and run structural gates; return one row per check.

    Each row is ``{label, kind, pass, ...}`` where ``kind`` is ``frame``
    (full-frame pixel-diff), ``crop`` (ROI-crop pixel-diff), ``struct``
    (structural-metric gate) or ``skip`` (a frame whose backend reference is
    not yet committed; non-fatal). Pure given its inputs — no build/run — so
    the gate logic is unit-testable against synthetic captures + references.

    ``structural_only`` shots are still captured (so the index→reference map
    stays aligned) but skip the full-frame pixel-diff and need no committed
    reference PNG — they're gated purely by their ``structural`` entries
    (an analytic oracle, deterministic at zoom where pixel-diff is excluded).

    ``missing_ref_is_skip`` flips a missing full-frame reference from a hard
    FAIL into a non-fatal ``skip`` row. The default-pass references are always
    committed for both backends, so a missing one there is a real failure; an
    ``extra_runs`` pass (e.g. canvas_stress ``--only compare``) blesses its
    references per-host, so a backend that hasn't captured them yet must skip
    rather than fail the whole run while the cross-host handoff is in flight.
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
            if missing_ref_is_skip:
                rows.append({"label": label, "kind": "skip", "pass": True,
                             "reason": f"no {backend or ref_dir.name} reference "
                                       f"yet — bless with --update-references "
                                       f"on this host"})
                continue
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


def _parse_extra_runs(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    """Validate + normalize the optional ``extra_runs`` manifest block.

    Each entry declares a *second capture pass* of the same target with its
    own demo args and its own gated reference subset, so a manifest can gate
    shots that the default scene suppresses (e.g. canvas_stress
    ``compare_yaw0`` / ``compare_yaw_q``, which only spawn under
    ``--only compare`` — a flag that hides the default groups). Fields:

      * ``name`` (required)      — pass label for logs / diff filenames.
      * ``demo_args`` (required) — args appended after ``--auto-screenshot N``.
      * ``shots`` (required)     — gated labels, positional into this pass's
                                   captures starting at ``capture_offset``.
      * ``capture_offset``       — index of the first gated shot in the pass's
                                   full capture list (default 0; negative =
                                   from the tail, for end-appended shots).
      * ``warmup`` / ``thresholds`` / ``crops`` / ``structural`` /
        ``structural_only`` — optional per-pass overrides; each defaults to
        the top-level manifest value.

    A manifest with no ``extra_runs`` behaves exactly as before.
    """
    raw = manifest.get("extra_runs", [])
    if not isinstance(raw, list):
        raise SystemExit("manifest 'extra_runs' must be a list")
    parsed: list[dict[str, Any]] = []
    seen: set[str] = set()
    for i, entry in enumerate(raw):
        if not isinstance(entry, dict):
            raise SystemExit(f"manifest 'extra_runs'[{i}] must be an object")
        name = entry.get("name")
        if not isinstance(name, str) or not name:
            raise SystemExit(f"manifest 'extra_runs'[{i}] needs a non-empty 'name'")
        if name in seen:
            raise SystemExit(f"manifest 'extra_runs' has a duplicate name '{name}'")
        seen.add(name)
        demo_args = entry.get("demo_args")
        if not isinstance(demo_args, list) or not demo_args \
                or not all(isinstance(a, str) for a in demo_args):
            raise SystemExit(
                f"extra run '{name}': 'demo_args' must be a non-empty list of "
                f"strings (the args that select this pass's scene)"
            )
        shots = entry.get("shots")
        if not isinstance(shots, list) or not shots \
                or not all(isinstance(s, str) for s in shots):
            raise SystemExit(
                f"extra run '{name}': 'shots' must be a non-empty list of "
                f"reference labels to gate"
            )
        offset = entry.get("capture_offset", 0)
        if not isinstance(offset, int):
            raise SystemExit(
                f"extra run '{name}': 'capture_offset' must be an integer"
            )
        parsed.append({
            "name": name,
            "demo_args": demo_args,
            "shots": shots,
            "capture_offset": offset,
            "warmup": entry.get("warmup"),
            "thresholds": entry.get("thresholds"),
            "crops": entry.get("crops", {}),
            "structural": entry.get("structural", {}),
            "structural_only": set(entry.get("structural_only", [])),
        })
    return parsed


def _run_capture(*, worktree: Path, target: str, shots_dir: Path, warmup: int,
                 timeout: int, demo_args: list[str],
                 pass_label: str) -> tuple[int, str] | None:
    """Clear ``shots_dir`` and run one ``--auto-screenshot`` capture pass.

    Returns a ``(returncode, tail)`` crash tuple if ``fleet-run`` exits
    non-zero (a real early-exit crash — ``--timeout`` makes a clean kill exit
    0), else ``None``. Each pass owns the whole ``shots_dir``, so the caller
    must collect this pass's screenshots before starting the next one.
    """
    if shots_dir.exists():
        shutil.rmtree(shots_dir)
    shots_dir.mkdir(parents=True, exist_ok=True)

    run_cmd = ["fleet-run", "--timeout", str(timeout), target,
               "--auto-screenshot", str(warmup)]
    run_cmd.extend(demo_args)
    print("+ " + " ".join(run_cmd), flush=True)
    # The demo logs arbitrary OS-provided strings at startup — audio/MIDI
    # device names enumerate through here, and on some hosts those carry
    # non-UTF-8 bytes (e.g. a Mac-Roman curly apostrophe in "Robert's iPhone").
    # The captured output is only used for the crash tail below, never parsed,
    # so decode tolerantly instead of letting a stray byte abort the whole run.
    proc = subprocess.run(run_cmd, cwd=str(worktree), capture_output=True,
                          text=True, errors="replace")
    if proc.returncode != 0:
        print(f"[render-verify] ({pass_label}) fleet-run exited "
              f"{proc.returncode}; tail of output follows (screenshot count "
              f"will be checked against manifest below):", file=sys.stderr)
        tail = (proc.stdout + proc.stderr).splitlines()[-40:]
        for line in tail:
            print(f"    {line}", file=sys.stderr)
        return (proc.returncode, "\n".join(tail))
    return None


def _write_references(*, captured: list[Path], shot_labels: list[str],
                      ref_dir: Path, crops_block: dict[str, list[str]],
                      structural_only: set[str]) -> None:
    """Overwrite the backend reference set from one pass's captures.

    Frame references for every non-structural-only shot, plus any
    manifest-declared ROI crops. Structural gates are threshold-based (no
    reference PNG), so they need nothing here.
    """
    ref_dir.mkdir(parents=True, exist_ok=True)
    for actual, label in zip(captured, shot_labels):
        if label in structural_only:
            continue  # threshold-gated, no reference PNG to write
        dest = ref_dir / f"{label}.png"
        shutil.copy2(actual, dest)
        print(f"[render-verify] updated {dest}")
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
    ap.add_argument("--demo-arg", action="append", default=[], metavar="ARG",
                    help="Extra argument passed through to the DEFAULT pass's "
                         "demo run after --auto-screenshot (repeatable; "
                         "manifest 'extra_runs' passes use their own declared "
                         "demo_args). Use to prove a feature flag is output-"
                         "neutral against the committed references — e.g. "
                         "`--demo-arg --occlusion-cull` checks the voxel "
                         "occlusion cull renders bit-identical to the cull-off "
                         "baseline (#1294 child 3/3).")
    args = ap.parse_args(argv)

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = verify_common.detect_backend(build_dir)

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
    # Optional second/third capture passes with their own demo args + gated
    # reference subset (e.g. canvas_stress `--only compare`). Empty for a
    # single-pass manifest, so the default path is untouched.
    extra_runs = _parse_extra_runs(manifest)
    screenshot_subdir = manifest.get("screenshot_subdir", "save_files/screenshots")
    # CLI --warmup wins; manifest["warmup"] overrides the hardcoded default of 10.
    warmup: int = args.warmup if args.warmup is not None else manifest.get("warmup", 10)

    print(
        f"[render-verify] target={args.target} demo={demo_name} "
        f"backend={backend} warmup={warmup}"
    )
    print(f"[render-verify] {len(shot_labels)} shots: {', '.join(shot_labels)}")

    if extra_runs:
        print(f"[render-verify] {len(extra_runs)} extra run(s): "
              f"{', '.join(e['name'] for e in extra_runs)}")

    if not args.no_build:
        verify_common.run(["fleet-build", "--target", args.target], cwd=worktree)

    exe = verify_common.find_exe(build_dir, args.target, demo_name)
    shots_dir = exe.parent / screenshot_subdir
    ref_dir = demo_dir / "test" / "references" / backend
    # Single-pass runs keep diffs in shots_dir/diffs (the original location,
    # cleared along with shots_dir each run). With extra_runs, each pass
    # rmtrees shots_dir on entry, so a diff written there by an earlier pass's
    # evaluate would be wiped — route those to a sibling dir that survives the
    # later passes, cleared once up front so stale diffs don't linger.
    diff_dir = (shots_dir.parent / "render_verify_diffs") if extra_runs \
        else (shots_dir / "diffs")
    if extra_runs and diff_dir.exists():
        shutil.rmtree(diff_dir)

    if args.update_references and not args.force:
        reply = input(
            f"[render-verify] About to overwrite references in {ref_dir}. "
            f"Continue? [y/N] "
        )
        if reply.strip().lower() not in ("y", "yes"):
            print("[render-verify] aborted.")
            return 1

    # ── Default pass ──────────────────────────────────────────────────────
    # `--auto-screenshot` fires `closeWindow()` after the last shot and exits
    # 0; a non-zero return is a real early-exit crash (e.g. a Metal static-
    # destruction segfault landing AFTER the screenshots save, which the per-
    # shot comparator would otherwise silently "pass" — T-336). `--timeout`
    # also exits 0 on a clean kill, so a crash is the only non-zero path; we
    # let it block a PASS verdict even when every shot compares clean.
    crashes: list[tuple[int, str]] = []
    crash_main = _run_capture(
        worktree=worktree, target=args.target, shots_dir=shots_dir,
        warmup=warmup, timeout=args.timeout, demo_args=args.demo_arg,
        pass_label="default")
    if crash_main is not None:
        crashes.append(crash_main)
    captured = _collect_shots(shots_dir, len(shot_labels))

    if args.update_references:
        _write_references(captured=captured, shot_labels=shot_labels,
                          ref_dir=ref_dir, crops_block=crops_block,
                          structural_only=structural_only)
        # Each extra pass must run even when its references don't exist yet —
        # this is exactly how those references get blessed for the first time.
        for extra in extra_runs:
            crash = _run_capture(
                worktree=worktree, target=args.target, shots_dir=shots_dir,
                warmup=extra["warmup"] if extra["warmup"] is not None else warmup,
                timeout=args.timeout, demo_args=extra["demo_args"],
                pass_label=extra["name"])
            if crash is not None:
                print(
                    f"[render-verify] --update-references: extra run "
                    f"'{extra['name']}' crashed (exit {crash[0]}); "
                    f"references not updated for this pass.",
                    file=sys.stderr,
                )
                return 1
            all_caps = _collect_all_shots(shots_dir)
            sliced = _slice_capture(all_caps, extra["capture_offset"],
                                    len(extra["shots"]), extra["name"])
            _write_references(captured=sliced, shot_labels=extra["shots"],
                              ref_dir=ref_dir, crops_block=extra["crops"],
                              structural_only=extra["structural_only"])
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

    # ── Extra passes ──────────────────────────────────────────────────────
    # Each is a separate capture with its own demo args (e.g. `--only compare`)
    # and gated reference subset. The default-pass captures are already
    # consumed (evaluated) above, so it is safe for the pass below to rmtree
    # shots_dir. A pass whose backend has not blessed ANY of its references is
    # skipped wholesale (no demo run) — the cross-host reference handoff may
    # still be in flight; a per-shot missing reference becomes a non-fatal
    # `skip` row rather than failing the run.
    for extra in extra_runs:
        gated = extra["shots"]
        pixel_shots = [s for s in gated if s not in extra["structural_only"]]
        if pixel_shots and not any((ref_dir / f"{s}.png").exists()
                                   for s in pixel_shots):
            print(f"[render-verify] extra run '{extra['name']}' skipped — no "
                  f"{backend} references for {', '.join(pixel_shots)} yet "
                  f"(bless with --update-references on this host).")
            for s in pixel_shots:
                rows.append({"label": f"{extra['name']}:{s}", "kind": "skip",
                             "pass": True,
                             "reason": f"no {backend} reference yet"})
            continue
        print(f"[render-verify] extra run '{extra['name']}': "
              f"{' '.join(extra['demo_args'])}")
        crash = _run_capture(
            worktree=worktree, target=args.target, shots_dir=shots_dir,
            warmup=extra["warmup"] if extra["warmup"] is not None else warmup,
            timeout=args.timeout, demo_args=extra["demo_args"],
            pass_label=extra["name"])
        if crash is not None:
            crashes.append(crash)
        all_caps = _collect_all_shots(shots_dir)
        sliced = _slice_capture(all_caps, extra["capture_offset"],
                                len(gated), extra["name"])
        extra_rows = evaluate_shots(
            captured=sliced,
            shot_labels=gated,
            ref_dir=ref_dir,
            diff_dir=diff_dir,
            thresholds=extra["thresholds"] or thresholds,
            crops=extra["crops"],
            structural=extra["structural"],
            structural_only=extra["structural_only"],
            missing_ref_is_skip=True,
            backend=backend,
        )
        # Namespace each row under its pass so a label shared across passes
        # stays unambiguous in the report and its diff path.
        for r in extra_rows:
            r["label"] = f"{extra['name']}:{r['label']}"
        rows.extend(extra_rows)

    # ── Report ────────────────────────────────────────────────────────────
    print()
    print(f"{'shot':40} {'result':8} {'match%':>8} {'max_d':>6} {'psnr':>8}")
    print("-" * 76)
    failures: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []
    for row in rows:
        verdict = ("SKIP" if row["kind"] == "skip"
                   else "PASS" if row["pass"] else "FAIL")
        result = row.get("result")
        if row["kind"] in ("frame", "crop") and result and "match_pct" in result:
            print(f"{row['label']:40} {verdict:8} {result['match_pct']:>8} "
                  f"{result['max_delta']:>6} {result['psnr_db']:>8}")
        else:
            # SKIP / MISSING / not-captured rows and structural gates have no
            # pixel-diff numbers — show the reason in the wide column.
            reason = row.get("reason") or (result.get("reason") if result else "") or ""
            print(f"{row['label']:40} {verdict:8} {reason}")
        if row["kind"] == "skip":
            skipped.append(row)
        elif not row["pass"]:
            failures.append(row)
    all_pass = not failures
    checked = len(rows) - len(skipped)

    print()
    skip_note = f" ({len(skipped)} skipped — references pending)" if skipped else ""
    if all_pass and not crashes:
        print(f"[render-verify] all {checked} checks PASS{skip_note}")
        return 0

    if not all_pass:
        print(f"[render-verify] {len(failures)} of {checked} checks FAIL{skip_note}")
        for row in failures:
            result = row.get("result") or {}
            reason = row.get("reason") or result.get("reason", "mismatch")
            diff = result.get("diff_path", "(no diff)")
            print(f"  - {row['label']}: {reason}  diff={diff}")

    for rc, _ in crashes:
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
