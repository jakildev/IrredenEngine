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
    python3 scripts/author-entity.py --prefab composite_proof --module test/module

A single ``scene_frame_0.vxs`` commits as ``<entity>.vxs``; a multi-frame
entity (e.g. the bird) commits as ``<entity>_frame_N.vxs``.

``--prefab <name>`` is the form for a session that saves an entity scene: the
editor writes ``scene.prefab.lua`` plus one ``scene_part_<n>.vxs`` per voxel
part. The manifest, the parts and their sidecars are byte-compared across the
two runs and copied to ``<asset-dir>/<name>/``, the manifest as
``<name>.prefab.lua``. A manifest names its parts by path from the directory
the spawning executable runs in, so each ``voxel_ref`` is rewritten to the
copied part's path under ``--asset-dir``; ``--part-names`` gives the parts'
file stems in manifest order (default: the part ids). The arguments a prefab
was authored with are written beside it as ``author.json`` and are the
defaults the next ``--prefab <name>`` run takes, so that bare command
re-authors a committed prefab.
"""

from __future__ import annotations

import argparse
import json
import os
import re
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


MANIFEST_NAME = f"{SAVE_BASENAME}.prefab.lua"
PART_PREFIX = f"{SAVE_BASENAME}_"
VOXEL_REF = re.compile(r'voxel_ref = "([^"]+)"')
PART_NAME = re.compile(r"[A-Za-z0-9_]+")


def prefab_files(save_dir: Path) -> list[Path]:
    """The entity-scene save currently in ``save_dir``: the manifest first, then
    every part and sidecar, name-ordered. Empty when no manifest was written."""
    manifest = save_dir / MANIFEST_NAME
    if not manifest.exists():
        return []
    return [manifest, *sorted(save_dir.glob(f"{PART_PREFIX}part_*.vxs*"))]


def clear_prefab_saves(save_dir: Path) -> None:
    """Remove a prior entity-scene save, so a session that never saved cannot
    pass on the last run's files."""
    for p in prefab_files(save_dir):
        p.unlink()


def commit_prefab(saved: list[Path], name: str, asset_dir: Path, ref_dir: Path,
                  part_names: list[str] | None) -> list[Path]:
    """Copy an entity-scene save into ``asset_dir/name``. Parts are renamed in
    manifest order (``part_names``, else their ids) and the manifest's
    ``voxel_ref`` lines follow them under ``ref_dir/name``."""
    manifest = saved[0]
    by_name = {p.name: p for p in saved[1:]}
    text = manifest.read_text(encoding="utf-8")
    refs = [Path(ref).name for ref in VOXEL_REF.findall(text)]
    missing = [ref for ref in refs if ref not in by_name]
    if missing:
        raise SystemExit(f"[author-entity] the manifest names parts the session "
                         f"did not write: {', '.join(missing)}")
    if len(set(refs)) != len(refs):
        raise SystemExit("[author-entity] the manifest names one part file twice")
    stems = part_names or [ref.removeprefix(PART_PREFIX).removesuffix(".vxs") for ref in refs]
    if len(stems) != len(refs):
        raise SystemExit(f"[author-entity] --part-names gives {len(stems)} name(s) "
                         f"for {len(refs)} voxel part(s)")

    out_dir = asset_dir / name
    out_dir.mkdir(parents=True, exist_ok=True)
    renamed = dict(zip(refs, stems))

    def staged_ref(match: re.Match) -> str:
        stem = renamed[Path(match.group(1)).name]
        return f'voxel_ref = "{(ref_dir / name / f"{stem}.vxs").as_posix()}"'

    written = [out_dir / f"{name}.prefab.lua"]
    written[0].write_text(VOXEL_REF.sub(staged_ref, text), encoding="utf-8")
    for ref, stem in renamed.items():
        for src_name, suffix in ((ref, ".vxs"), (f"{ref}.json", ".vxs.json")):
            if src_name not in by_name:
                raise SystemExit(f"[author-entity] expected {src_name} was not written")
            dst = out_dir / f"{stem}{suffix}"
            shutil.copyfile(by_name[src_name], dst)
            written.append(dst)
    return written


AUTHOR_ARGS_NAME = "author.json"
# What author.json records: the arguments that decide what a prefab's session
# authors and how its part files are named.
AUTHOR_ARG_KEYS = ("session", "module_session", "module", "scene_size", "part_names")


def load_author_args(prefab_dir: Path) -> dict:
    """The arguments ``prefab_dir``'s prefab was last authored with; empty when
    it has not been authored."""
    path = prefab_dir / AUTHOR_ARGS_NAME
    if not path.exists():
        return {}
    recorded = json.loads(path.read_text(encoding="utf-8"))
    unknown = sorted(set(recorded) - set(AUTHOR_ARG_KEYS))
    if unknown:
        raise SystemExit(f"[author-entity] {path} names unknown argument(s): "
                         f"{', '.join(unknown)}")
    return recorded


def merge_author_args(given: dict, recorded: dict) -> dict:
    """``given`` (the command line's values, None where unset) over ``recorded``.
    The two session flags are one choice, so naming either drops both recorded
    ones."""
    if given.get("session") or given.get("module_session"):
        recorded = {k: v for k, v in recorded.items()
                    if k not in ("session", "module_session")}
    merged = {key: given.get(key) or recorded.get(key) for key in AUTHOR_ARG_KEYS}
    return {key: value for key, value in merged.items() if value}


def write_author_args(prefab_dir: Path, author_args: dict) -> Path:
    path = prefab_dir / AUTHOR_ARGS_NAME
    path.write_text(json.dumps(author_args, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")
    return path


def run_session(exe: Path, save_dir: Path, session: str, scene_size: list[int] | None,
                warmup: int, timeout: int, module: str | None,
                module_session: str | None, prefab: bool) -> tuple[int, str, list[Path]]:
    """Clear stale saves, replay the session once, and collect what it saved:
    the frames, or with ``prefab`` the entity-scene manifest and its parts."""
    clear_saves(save_dir)
    clear_prefab_saves(save_dir)
    cmd = [
        "fleet-run", "--timeout", str(timeout),
        EDITOR_TARGET, "--auto-screenshot", str(warmup),
    ]
    if module:
        cmd += ["--module", module]
    if module_session:
        cmd += ["--module-session", module_session]
    else:
        cmd += ["--gui-session", session]
    if scene_size:
        cmd += ["--scene-size", *(str(v) for v in scene_size)]
    rc, output = verify_common.run_capture(cmd)
    return rc, output, prefab_files(save_dir) if prefab else frame_files(save_dir)


def check_assertions(output: str, run_rc: int, timeout: int | None = None) -> list[dict[str, str]]:
    """Print the GUI-ASSERT table and exit non-zero on any FAIL / hang / empty.

    Unlike gui-verify, an assertion-less run is always a failure here — the
    session exists to run its recipe, so no GUI-ASSERT means the recipe never
    fired (a build / session-name / scene-size problem)."""
    assertions, hung, failures, missing_shots = verify_common.report_gui_asserts(
        output, "[author-entity] ", timeout=timeout)
    if not assertions:
        raise SystemExit("[author-entity] no GUI-ASSERT lines — the session never "
                         "ran its recipe (build/session-name/scene-size issue?)")
    if failures or missing_shots or run_rc != 0 or hung:
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
    parser.add_argument("entity", nargs="?", default=None,
                        help="entity name (also the asset base name)")
    parser.add_argument("--prefab", default=None, metavar="NAME",
                        help="commit the session's entity scene (manifest + parts) "
                             "to <asset-dir>/NAME/ instead of frames")
    parser.add_argument("--part-names", nargs="+", default=None, metavar="STEM",
                        help="with --prefab: file stems for the voxel parts, in "
                             "manifest order (default: the part ids)")
    session_group = parser.add_mutually_exclusive_group()
    session_group.add_argument("--session", default=None,
                               help="--gui-session name (default: the entity name)")
    session_group.add_argument("--module-session", default=None,
                               help="session registered by --module")
    parser.add_argument("--module", default=None,
                        help="creation-module directory passed through to the editor")
    parser.add_argument("--asset-dir", type=Path, default=ASSET_SUBDIR,
                        help="asset copy destination (default: assets/voxel/entities)")
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
    if (args.entity is None) == (args.prefab is None):
        parser.error("name exactly one of an entity or --prefab NAME")
    if args.part_names and args.prefab is None:
        parser.error("--part-names requires --prefab")
    name = args.prefab or args.entity
    prefab = args.prefab is not None
    kind = "file" if prefab else "frame"

    worktree = verify_common.detect_worktree_root(verify_common.SCRIPT_DIR)
    build_dir = Path(os.environ.get("IRREDEN_BUILD_DIR", str(worktree / "build")))
    asset_dir = args.asset_dir if args.asset_dir.is_absolute() else worktree / args.asset_dir

    author_args = merge_author_args(
        {key: getattr(args, key) for key in AUTHOR_ARG_KEYS},
        load_author_args(asset_dir / name) if prefab else {},
    )
    args.module = author_args.get("module")
    args.module_session = author_args.get("module_session")
    args.scene_size = author_args.get("scene_size")
    args.part_names = author_args.get("part_names")
    if args.module_session and not args.module:
        parser.error("--module-session requires --module")
    if args.part_names:
        bad = [n for n in args.part_names if not PART_NAME.fullmatch(n)]
        if bad or len(set(args.part_names)) != len(args.part_names):
            parser.error("--part-names takes distinct names of letters, digits and _")
    session = author_args.get("session") or name

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
                                     args.warmup_frames, args.timeout, args.module,
                                     args.module_session, prefab)
    check_assertions(out1, rc1, args.timeout)
    if not frames1:
        expected = MANIFEST_NAME if prefab else f"{SAVE_BASENAME}_frame_*.vxs"
        raise SystemExit(f"[author-entity] session '{session}' passed its asserts but "
                         f"wrote no {expected} — did the recipe save()?")
    print(f"[author-entity] run 1 saved {len(frames1)} {kind}(s): "
          f"{', '.join(p.name for p in frames1)}")
    # Snapshot run-1 bytes before run 2 clears the save dir.
    first_bytes = {p.name: p.read_bytes() for p in frames1}

    rc2, out2, frames2 = run_session(exe, save_dir, session, args.scene_size,
                                     args.warmup_frames, args.timeout, args.module,
                                     args.module_session, prefab)
    check_assertions(out2, rc2, args.timeout)
    # Compare run 2's live files against run 1's captured bytes.
    if sorted(first_bytes) != sorted(p.name for p in frames2):
        raise SystemExit("[author-entity] non-deterministic: the two runs wrote "
                         f"different {kind} files")
    for p in frames2:
        if first_bytes[p.name] != p.read_bytes():
            raise SystemExit(f"[author-entity] non-deterministic: {p.name} differs "
                             f"between runs")
    print(f"[author-entity] determinism gate: {len(frames2)} {kind}(s) byte-identical "
          f"across two runs")

    if prefab:
        written = commit_prefab(frames2, name, asset_dir, args.asset_dir, args.part_names)
        written.append(write_author_args(asset_dir / name, author_args))
    else:
        written = commit_asset(frames2, name, asset_dir)
    print(f"\n[author-entity] committed {name}:")
    for p in written:
        try:
            display_path = p.relative_to(worktree)
        except ValueError:
            display_path = p
        print(f"  {display_path}")


if __name__ == "__main__":
    main()
