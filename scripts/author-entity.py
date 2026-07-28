#!/usr/bin/env python3
"""author-entity — author a voxel entity by replaying an editor session, verify it,
and commit the asset (#766 F-1.6).

Drives ``IRVoxelEditor``'s ``--gui-session <name>`` recipe through the GUI-test
harness (the same ``run_capture`` flow ``gui-verify`` uses), then:

  1. parses the ``GUI-ASSERT`` lines and fails on any FAIL / zero assertions —
     a swallowed gesture that authored the wrong thing shows up here, not as a
     silently-wrong asset;
  2. confirms the editor actually wrote its ``scene_frame_*.vxs`` files (the
     session's ``save`` op ran) — the runner owns resolving the editor's run
     directory (``<exe-dir>/data/editor_scene``, since ir-run ``cd``s into the
     exe dir before launch);
  3. re-runs the session and byte-compares the saved ``.vxs`` set against the
     first run — the determinism gate the acceptance requires (same host +
     backend; the format carries no timestamps, so a faithful replay is
     byte-identical);
  4. copies the frames to ``assets/voxel/entities/<entity>[_frame_N].vxs``
     (+ the regenerated ``.vxs.json`` sidecar) as the committed asset.

Usage:
    python3 scripts/author-entity.py rock
    python3 scripts/author-entity.py rock --scene-size 16 16 16 --no-build
    python3 scripts/author-entity.py ant --session ant --scene-size 20 20 20

A single ``scene_frame_0.vxs`` commits as ``<entity>.vxs``; a multi-frame
entity (e.g. the bird) commits as ``<entity>_frame_N.vxs``.
"""

from __future__ import annotations

import argparse
import os
import shutil
from pathlib import Path

import verify_common

# Where the editor writes on Ctrl+S, relative to the exe's run directory
# (scene_io.hpp kSceneSaveDir / kSceneBaseName). ir-run cd's into the exe dir,
# so the absolute save dir is <exe-dir>/<SAVE_SUBDIR>.
SAVE_SUBDIR = Path("data") / "editor_scene"
SAVE_BASENAME = "scene"
ASSET_SUBDIR = Path("assets") / "voxel" / "entities"
EDITOR_TARGET = "IRVoxelEditor"


def frame_files(save_dir: Path) -> list[Path]:
    """The scene_frame_*.vxs files currently in ``save_dir``, frame-ordered."""
    return sorted(
        save_dir.glob(f"{SAVE_BASENAME}_frame_*.vxs"),
        key=lambda p: int(p.stem.rsplit("_", 1)[1]),
    )


def clear_saves(save_dir: Path) -> None:
    """Remove any prior scene_frame_*.vxs(+.json) so a failed save can't leave a
    stale file the runner would mistake for this run's output."""
    if not save_dir.exists():
        return
    for p in save_dir.glob(f"{SAVE_BASENAME}_frame_*.vxs*"):
        p.unlink()


def run_session(exe: Path, save_dir: Path, session: str, scene_size: list[int] | None,
                warmup: int, timeout: int) -> tuple[int, str, list[Path]]:
    """Clear stale saves, replay the session once, and collect the saved frames."""
    clear_saves(save_dir)
    cmd = [
        "fleet-run", "--timeout", str(timeout),
        EDITOR_TARGET, "--auto-screenshot", str(warmup),
        "--gui-session", session,
    ]
    if scene_size:
        cmd += ["--scene-size", *(str(v) for v in scene_size)]
    rc, output = verify_common.run_capture(cmd)
    return rc, output, frame_files(save_dir)


def check_assertions(output: str, run_rc: int, timeout: int | None = None) -> list[dict[str, str]]:
    """Print the GUI-ASSERT table and exit non-zero on any FAIL / hang / empty.

    Unlike gui-verify, an assertion-less run is always a failure here — the
    session exists to run its recipe, so no GUI-ASSERT means the recipe never
    fired (a build / session-name / scene-size problem)."""
    assertions, hung, failures = verify_common.report_gui_asserts(
        output, "[author-entity] ", timeout=timeout)
    if not assertions:
        raise SystemExit("[author-entity] no GUI-ASSERT lines — the session never "
                         "ran its recipe (build/session-name/scene-size issue?)")
    if failures or run_rc != 0 or hung:
        raise SystemExit("[author-entity] session did not pass — asset not committed")
    return assertions


def commit_asset(frames: list[Path], entity: str, asset_dir: Path) -> list[Path]:
    """Copy the saved frames (+ regenerated .vxs.json sidecars) into
    assets/voxel/entities, dropping the ``_frame_0`` suffix for a single-frame
    entity."""
    asset_dir.mkdir(parents=True, exist_ok=True)
    multi = len(frames) > 1
    written: list[Path] = []
    for idx, frame in enumerate(frames):
        stem = f"{entity}_frame_{idx}" if multi else entity
        # frame is <...>.vxs; its sidecar is <...>.vxs.json.
        for src, dst_suffix in ((frame, ".vxs"),
                                (Path(f"{frame}.json"), ".vxs.json")):
            if not src.exists():
                raise SystemExit(f"[author-entity] expected {src} was not written")
            dst = asset_dir / f"{stem}{dst_suffix}"
            shutil.copyfile(src, dst)
            written.append(dst)
    return written


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Author a voxel entity through an IRVoxelEditor GUI session, "
                    "verify it, and commit the asset."
    )
    parser.add_argument("entity", help="entity name (also the asset base name)")
    parser.add_argument("--session", default=None,
                        help="--gui-session name (default: the entity name)")
    parser.add_argument("--scene-size", type=int, nargs=3, metavar=("W", "H", "D"),
                        default=None, help="editable grid dims (default: editor's 16³)")
    parser.add_argument("--no-build", action="store_true",
                        help="skip fleet-build (assume the editor is already built)")
    parser.add_argument("--warmup-frames", type=int, default=10, metavar="N",
                        help="--auto-screenshot warmup frames (default: 10)")
    parser.add_argument("--timeout", type=int, default=180, metavar="S",
                        help="per-run watchdog seconds (default: 180 — sessions are "
                             "longer than the standing shot table)")
    args = parser.parse_args()
    session = args.session or args.entity

    worktree = verify_common.detect_worktree_root(verify_common.SCRIPT_DIR)
    build_dir = Path(os.environ.get("IRREDEN_BUILD_DIR", str(worktree / "build")))

    if not args.no_build:
        rc = verify_common.run(["fleet-build", "--target", EDITOR_TARGET], check=False)
        if rc != 0:
            raise SystemExit(f"[author-entity] fleet-build failed ({rc})")

    # find_exe scopes to build/creations/demos/<demo> first, then falls back to
    # a full build/ walk — which is what locates the editor (it lives under
    # creations/editors/, so the demo-scoped root simply doesn't exist).
    exe = verify_common.find_exe(build_dir, EDITOR_TARGET, "voxel_editor")
    save_dir = exe.parent / SAVE_SUBDIR
    print(f"[author-entity] editor: {exe}")
    print(f"[author-entity] save dir: {save_dir}")

    rc1, out1, frames1 = run_session(exe, save_dir, session, args.scene_size,
                                     args.warmup_frames, args.timeout)
    check_assertions(out1, rc1, args.timeout)
    if not frames1:
        raise SystemExit(f"[author-entity] session '{session}' passed its asserts but "
                         f"wrote no {SAVE_BASENAME}_frame_*.vxs — did the recipe save()?")
    print(f"[author-entity] run 1 saved {len(frames1)} frame(s): "
          f"{', '.join(p.name for p in frames1)}")
    # Snapshot run-1 bytes before run 2 clears the save dir.
    first_bytes = {p.name: p.read_bytes() for p in frames1}

    rc2, out2, frames2 = run_session(exe, save_dir, session, args.scene_size,
                                     args.warmup_frames, args.timeout)
    check_assertions(out2, rc2, args.timeout)
    # Compare run 2's live files against run 1's captured bytes.
    if sorted(first_bytes) != sorted(p.name for p in frames2):
        raise SystemExit("[author-entity] non-deterministic: the two runs wrote "
                         "different frame files")
    for p in frames2:
        if first_bytes[p.name] != p.read_bytes():
            raise SystemExit(f"[author-entity] non-deterministic: {p.name} differs "
                             f"between runs")
    print(f"[author-entity] determinism gate: {len(frames2)} frame(s) byte-identical "
          f"across two runs")

    asset_dir = worktree / ASSET_SUBDIR
    written = commit_asset(frames2, args.entity, asset_dir)
    print(f"\n[author-entity] committed {args.entity}:")
    for p in written:
        print(f"  {p.relative_to(worktree)}")


if __name__ == "__main__":
    main()
