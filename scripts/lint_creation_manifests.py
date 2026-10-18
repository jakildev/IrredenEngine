#!/usr/bin/env python3
"""Validate the checked-in creation catalog manifests."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

KINDS = {"demo", "editor", "game", "tool", "test"}


def creation_dirs(root: Path) -> list[Path]:
    demos = sorted(path for path in (root / "creations/demos").iterdir()
                   if path.is_dir())
    return demos + [root / "creations/editors/voxel_editor"]


def fail(path: Path, message: str) -> None:
    print(f"{path}: {message}", file=sys.stderr)


def validate(path: Path) -> list[str] | None:
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        fail(path, str(error))
        return None
    required = ("version", "name", "description", "group", "tags", "kind", "targets")
    missing = [key for key in required if key not in data]
    if missing:
        fail(path, f"missing required key(s): {', '.join(missing)}")
        return None
    if data["version"] != 1:
        fail(path, "version must be 1")
        return None
    if data["kind"] not in KINDS:
        fail(path, f"kind must be one of: {', '.join(sorted(KINDS))}")
        return None
    if not isinstance(data["targets"], list) or not data["targets"]:
        fail(path, "targets must be a non-empty array")
        return None
    names = []
    for target in data["targets"]:
        if not isinstance(target, dict) or not isinstance(target.get("name"), str):
            fail(path, "targets[].name must be a string")
            return None
        names.append(target["name"])
        for preset in target.get("presets", []):
            args = preset.get("args") if isinstance(preset, dict) else None
            if not isinstance(args, list) or not all(isinstance(arg, str) for arg in args):
                fail(path, "targets[].presets[].args must contain only strings")
                return None
    return names


def build_targets(build_dir: Path) -> set[str]:
    result = subprocess.run(["cmake", "--build", str(build_dir), "--target", "help"],
                            check=False, capture_output=True, text=True)
    if result.returncode:
        print(result.stderr, file=sys.stderr, end="")
        raise RuntimeError("cmake target help failed")
    names = set()
    for line in result.stdout.splitlines():
        name = line.strip().removeprefix("...").strip().split(maxsplit=1)[0]
        if name.startswith(("IR", "Demo")):
            names.add(name)
    return names


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    target_names = build_targets(args.build_dir) if args.build_dir else None
    manifests = 0
    valid = True
    for directory in creation_dirs(args.root):
        manifest = directory / "creation.json"
        if not manifest.is_file():
            fail(directory, "missing creation.json")
            valid = False
            continue
        names = validate(manifest)
        if names is None:
            valid = False
            continue
        manifests += 1
        if target_names is not None:
            for name in names:
                if name not in target_names:
                    fail(manifest, f"unknown build target: {name}")
                    valid = False
    print(f"creation manifests: {manifests}/{len(creation_dirs(args.root))}")
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
