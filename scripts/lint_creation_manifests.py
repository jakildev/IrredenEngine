#!/usr/bin/env python3
"""Validate the checked-in creation catalog manifests."""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

KINDS = {"demo", "editor", "game", "tool", "test"}
WINDOWS_DEVICE_PATTERN = re.compile(r"^(?:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])$")
# The editors .gitignore tracks; every other creations/editors/* is private.
TRACKED_EDITORS = ("font_maker", "voxel_editor")
# Prefixes of the build targets a manifest may name.
TARGET_PREFIXES = ("IR",)
TARGET_NAME_PATTERN = re.compile(r"^IR[A-Z][A-Za-z0-9]*$")


def creation_dirs(root: Path) -> list[Path]:
    demos = sorted(path for path in (root / "creations/demos").iterdir() if path.is_dir())
    editors = [root / "creations/editors" / editor for editor in TRACKED_EDITORS]
    return demos + editors + [root / "creations/launcher"]


def fail(path: Path, message: str) -> None:
    print(f"{path}: {message}", file=sys.stderr)


def validate_target_name(path: Path, key: str, name: object) -> bool:
    if not isinstance(name, str):
        fail(path, f"{key} must be a string")
        return False
    if not TARGET_NAME_PATTERN.fullmatch(name) or "Ui" in name:
        fail(path, f"{key} {name!r} must be IR followed by PascalCase words (with UI capitalized)")
        return False
    stem = name.split(".", maxsplit=1)[0]
    if WINDOWS_DEVICE_PATTERN.fullmatch(stem.upper()):
        fail(path, f"{key} has a reserved Windows device stem")
        return False
    return True


def validate_tracked_path(
    path: Path, key: str, value: object, base: Path, root: Path, tracked_files: set[str]
) -> bool:
    if not isinstance(value, str) or not value:
        fail(path, f"{key} must be a non-empty string")
        return False
    if "\0" in value:
        fail(path, f"{key} must not contain NUL")
        return False
    if value.startswith(("/", "\\")) or re.match(r"^[A-Za-z]:", value):
        fail(path, f"{key} must be relative")
        return False
    if ".." in re.split(r"[/\\]+", value):
        fail(path, f"{key} must not contain a '..' component")
        return False

    resolved_base = base.resolve()
    candidate = base / value
    try:
        candidate.resolve().relative_to(resolved_base)
    except ValueError:
        fail(path, f"{key} escapes its base directory")
        return False

    relative = candidate.relative_to(root).as_posix()
    if relative not in tracked_files or not candidate.is_file():
        fail(path, f"{key} must name a tracked file")
        return False
    return True


def validate(path: Path, root: Path, tracked_files: set[str]) -> list[tuple[str, bool]] | None:
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        fail(path, str(error))
        return None
    if not isinstance(data, dict):
        fail(path, "manifest must be an object")
        return None
    required = ("version", "name", "description", "group", "tags", "kind", "targets")
    missing = [key for key in required if key not in data]
    if missing:
        fail(path, f"missing required key(s): {', '.join(missing)}")
        return None
    if type(data["version"]) is not int or data["version"] != 1:
        fail(path, "version must be 1")
        return None
    if not isinstance(data["kind"], str) or data["kind"] not in KINDS:
        fail(path, f"kind must be one of: {', '.join(sorted(KINDS))}")
        return None
    for key in ("name", "description", "group"):
        if not isinstance(data[key], str) or not data[key]:
            fail(path, f"{key} must be a non-empty string")
            return None
    if not isinstance(data["tags"], list) or not all(isinstance(tag, str) for tag in data["tags"]):
        fail(path, "tags must be an array of strings")
        return None
    if "thumbnail" in data and not validate_tracked_path(
        path, "thumbnail", data["thumbnail"], path.parent, root, tracked_files
    ):
        return None
    if "docs" in data:
        if not isinstance(data["docs"], list):
            fail(path, "docs must be an array")
            return None
        for index, doc in enumerate(data["docs"]):
            if not validate_tracked_path(path, f"docs[{index}]", doc, root, root, tracked_files):
                return None
    if not isinstance(data["targets"], list) or not data["targets"]:
        fail(path, "targets must be a non-empty array")
        return None
    targets = []
    for target_index, target in enumerate(data["targets"]):
        target_key = f"targets[{target_index}]"
        if not isinstance(target, dict):
            fail(path, f"{target_key} must be an object")
            return None
        if not validate_target_name(path, f"{target_key}.name", target.get("name")):
            return None
        for key in ("display", "description"):
            if key in target and not isinstance(target[key], str):
                fail(path, f"{target_key}.{key} must be a string")
                return None
        package = target.get("package", False)
        if not isinstance(package, bool):
            fail(path, f"{target_key}.package must be a boolean")
            return None
        targets.append((target["name"], package))
        presets = target.get("presets", [])
        if not isinstance(presets, list):
            fail(path, f"{target_key}.presets must be an array")
            return None
        for preset_index, preset in enumerate(presets):
            preset_key = f"{target_key}.presets[{preset_index}]"
            if not isinstance(preset, dict):
                fail(path, f"{preset_key} must be an object")
                return None
            if not isinstance(preset.get("name"), str) or not preset["name"]:
                fail(path, f"{preset_key}.name must be a non-empty string")
                return None
            args = preset.get("args")
            if not isinstance(args, list) or not all(isinstance(arg, str) for arg in args):
                fail(path, f"{preset_key}.args must contain only strings")
                return None
        if not isinstance(target.get("display"), str) or not target["display"]:
            fail(path, f"{target_key}.display must be a non-empty string")
            return None
    return targets


def git_tracked_files(root: Path) -> set[str]:
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        print(result.stderr, file=sys.stderr, end="")
        raise RuntimeError("git ls-files failed")
    return set(result.stdout.split("\0"))


def build_targets(build_dir: Path) -> set[str]:
    result = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "help"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        print(result.stderr, file=sys.stderr, end="")
        raise RuntimeError("cmake target help failed")
    names = set()
    for line in result.stdout.splitlines():
        name = line.strip().removeprefix("...").strip().split(maxsplit=1)[0]
        if name.startswith(TARGET_PREFIXES):
            names.add(name)
    return names


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    args.root = args.root.resolve()
    target_names = build_targets(args.build_dir) if args.build_dir else None
    tracked_files = git_tracked_files(args.root)
    manifests = 0
    valid = True
    for directory in creation_dirs(args.root):
        manifest = directory / "creation.json"
        if not manifest.is_file():
            fail(directory, "missing creation.json")
            valid = False
            continue
        targets = validate(manifest, args.root, tracked_files)
        if targets is None:
            valid = False
            continue
        manifests += 1
        if target_names is not None:
            for name, package in targets:
                if name not in target_names:
                    fail(manifest, f"unknown build target: {name}")
                    valid = False
                package_target = f"{name}Package"
                if package and package_target not in target_names:
                    fail(manifest, f"{name} sets package; no build target {package_target}")
                    valid = False
                elif not package and package_target in target_names:
                    fail(manifest, f"build target {package_target} needs package: true on {name}")
                    valid = False
    print(f"creation manifests: {manifests}/{len(creation_dirs(args.root))}")
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
