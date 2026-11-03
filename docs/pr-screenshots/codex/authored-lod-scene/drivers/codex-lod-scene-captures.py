#!/usr/bin/env python3
"""Capture and qualify the authored LOD fixture; never build or change source."""

import argparse
import hashlib
import importlib.util
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


LABELS = (
    "lod_scene_coarse", "lod_scene_medium_yaw45", "lod_scene_fine_yaw90",
    "lod_scene_return_yaw180", "lod_scene_coarse_yaw270", "lod_scene_restored",
)
TIERS = [4, 2, 0, 2, 4, 4]
FIXTURE_FILES = (
    "creations/demos/shape_debug/main.cpp",
    "creations/demos/shape_debug/lod_scene.hpp",
    "creations/demos/shape_debug/configs/lod_resident.prefab.lua",
    "creations/demos/shape_debug/test/references/manifest.json",
)
CONFLICT = "--lod-scene cannot be combined with another scene or capture override"


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, help="Default: the selected worktree's build/")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    build = (args.build_dir or root / "build").resolve()
    output = args.output.resolve()
    demo = build / "creations/demos/shape_debug"
    binary = demo / ("IRShapeDebug.exe" if os.name == "nt" else "IRShapeDebug")
    runner = root / "scripts/fleet/fleet-run"
    comparator = root / "scripts/render-compare.py"
    if not binary.is_file() or not runner.is_file():
        parser.error(f"missing selected executable or runner under {build} / {root}")
    if output.exists():
        parser.error("output already exists; prior evidence is never overwritten")
    sys.path.insert(0, str(root / "scripts/perf"))
    profiler = load_module("lod_repeat_profile", root / "scripts/perf/repeat_profile.py")
    compare = load_module("lod_render_compare", comparator)
    cache = (build / "CMakeCache.txt").read_text()
    config = re.search(r"^CMAKE_BUILD_TYPE:STRING=(.+)$", cache, re.MULTILINE)
    commands = json.loads((build / "compile_commands.json").read_text())
    entries = [entry for entry in commands
               if Path(entry["file"]).resolve() == root / FIXTURE_FILES[0]]
    if config is None or config[1] != "Debug" or len(entries) != 1:
        parser.error("the fixture requires one selected Debug compile entry with assertions")
    compile_argv = entries[0].get("arguments") or shlex.split(entries[0]["command"])
    if any(arg == "-DIR_RELEASE" or arg.startswith("-DIR_RELEASE=") for arg in compile_argv):
        parser.error("IR_RELEASE strips required assertions and shot witnesses")
    output.mkdir(parents=True)
    overrides = {"IRREDEN_BUILD_DIR": str(build), "IR_WINDOW_MODE": "offscreen"}
    env = dict(os.environ, **overrides)
    manifest = {
        "start_utc": utc(), "root": str(root), "build_dir": str(build),
        "platform": platform.platform(), "assertions_enabled": True,
        "build_type": config[1], "compile_entry": entries[0],
        "env_overrides": overrides, "expected_labels": LABELS,
        "expected_tiers": TIERS, "runs": [], "errors": [],
        "render_experiment_env": {key: env.get(key) for key in profiler.RENDER_EXPERIMENT_ENV},
        "scope": "Debug correctness fixture; no performance qualification",
    }

    def git(*arguments):
        return subprocess.check_output(["git", *arguments], cwd=root, text=True)

    def fingerprint():
        patch = git("diff", "--binary", "HEAD")
        state = {
            "head": git("rev-parse", "HEAD").strip(),
            "porcelain": git("status", "--porcelain=v1", "--untracked-files=all"),
            "diff_sha256": hashlib.sha256(patch.encode()).hexdigest(),
            "fixture_sha256": {name: sha(root / name) for name in FIXTURE_FILES},
            "binary_sha256": sha(binary), "driver_sha256": sha(Path(__file__)),
            "comparator_sha256": sha(comparator),
            "compile_commands_sha256": sha(build / "compile_commands.json"),
            "cmake_cache_sha256": sha(build / "CMakeCache.txt"),
            "staged_directory_sha256": {
                name: profiler.directory_digest(demo / name)
                for name in ("shaders", "scripts", "configs", "assets/prefabs")
            },
        }
        return state, patch

    def save():
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def require(condition, message):
        if not condition:
            manifest["errors"].append(message)

    def full_frames(directory):
        return {path.resolve() for path in directory.glob("screenshot_*.png")
                if re.fullmatch(r"screenshot_\d+", path.stem)}

    def native(name, extra, timeout):
        directory = output / name
        directory.mkdir()
        pre, patch = fingerprint()
        (directory / "source.patch").write_text(patch)
        shots_dir = demo / "save_files/screenshots"
        old = full_frames(shots_dir)
        command = [str(runner), "--build-dir", str(build), "--timeout", str(timeout),
                   "IRShapeDebug", "--lod-scene", "--pivot-origin", "--auto-screenshot", "20",
                   "--window-mode", "offscreen", *extra]
        start_ns = time.time_ns()
        record = {"name": name, "argv": command, "start_utc": utc(), "pre": pre}
        manifest["runs"].append(record)
        save()
        run = subprocess.run(command, cwd=root, env=env, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        end_ns = time.time_ns()
        log = run.stdout
        (directory / "run.log").write_text(log)
        post, _ = fingerprint()
        saved = [Path(path) for path in re.findall(
            r"Saved screenshot: [\"']?(save_files/screenshots/[^\s\"']+\.png)", log)]
        saved = [path for path in saved if re.fullmatch(r"screenshot_\d+", path.stem)]
        paths = [(path if path.is_absolute() else demo / path).resolve() for path in saved]
        fresh = full_frames(shots_dir) - old
        record.update({"end_utc": utc(), "exit_code": run.returncode, "post": post,
                       "stable": pre == post, "log": str(directory / "run.log"),
                       "verdicts": re.findall(r"RESULT=([A-Z-]+)", log), "images": []})
        require(pre == post, f"{name}: source, binary or staged inputs changed during capture")
        if extra:
            child_refusal = re.search(r"RESULT=CRASH\s+exe=IRShapeDebug\s+exit=2(?:\s|$)", log)
            require(run.returncode in (1, 2) and child_refusal is not None and CONFLICT in log,
                    f"{name}: missing witnessed child exit-2 CLI refusal")
            started = re.search(
                r"AutoScreenshot|LOD scene:|Saved screenshot:|Creating (?:window|Window)|"
                r"(?:Initializ\w*|Created).*(?:graphics|Graphics|GLFW|Metal|OpenGL)", log)
            require(started is None and not paths and not fresh,
                    f"{name}: scene/graphics/capture initialized before refusal")
        else:
            require(run.returncode == 0 and "RESULT=CLEAN" in log,
                    "positive: missing clean exit")
            witnesses = re.findall(r"LOD scene shot=(\d+) tier=(\d+) ([^\r\n]+)", log)
            record["lod_witnesses"] = witnesses
            require([(int(index), int(tier)) for index, tier, _ in witnesses]
                    == list(enumerate(TIERS)), "positive: missing or misordered LOD witnesses")
            labels = re.findall(r"AutoScreenshot \d+/\d+: (lod_scene_\w+)", log)
            record["capture_labels"] = labels
            require(labels == list(LABELS), "positive: capture label order mismatch")
            require(len(paths) == 6 and len(set(paths)) == 6 and set(paths) == fresh,
                    "positive: expected exactly six fresh logged images")
            for label, path in zip(LABELS, paths):
                valid = path.is_file() and path not in old and path.parent == shots_dir.resolve()
                valid = valid and start_ns <= path.stat().st_mtime_ns <= end_ns
                require(valid, f"positive: stale/missing/out-of-run image {path}")
                if valid:
                    copied = directory / f"{label}.png"
                    shutil.copy2(path, copied)
                    record["images"].append({"label": label, "original": str(path),
                                             "file": str(copied), "sha256": sha(copied)})
        (directory / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
        save()
        print(f"{name}: exit={run.returncode} stable={pre == post}", flush=True)
        return record

    def exact(name, actual, reference, expected_exit):
        command = [sys.executable, str(comparator), str(actual), str(reference),
                   "--per-pixel-tol", "0", "--threshold-match-pct", "100",
                   "--threshold-max-delta", "0", "--json",
                   "--diff-out", str(output / f"{name}-diff.png")]
        run = subprocess.run(command, cwd=root, capture_output=True, text=True)
        (output / f"{name}.log").write_text(run.stdout + run.stderr)
        metric = json.loads(run.stdout)
        result = {"argv": command, "exit_code": run.returncode, "metric": metric}
        (output / f"{name}.json").write_text(json.dumps(result, indent=2) + "\n")
        manifest[name] = result
        require(run.returncode == expected_exit, f"{name}: wrong comparison exit")
        if expected_exit == 0:
            require(metric["match_pct"] == 100 and metric["max_delta"] == 0,
                    "exact replay differs after restoring pose")
        else:
            require(metric["max_delta"] == 1 and not metric["pass"],
                    "one-channel mutation was not rejected at delta 1")

    try:
        manifest["driver_pre"], patch = fingerprint()
        (output / "source.patch").write_text(patch)
        save()
        positive = native("positive", [], 120)
        for flag in ("--viewport-portrait", "--ao-contact-probe", "--cull-evict-test", "--yaw-sweep"):
            native(flag[2:], [flag], 20)
        if len(positive["images"]) == 6:
            first, last = (Path(positive["images"][index]["file"]) for index in (0, -1))
            exact("exact-replay", last, first, 0)
            width, height, bpp, pixels = compare.read_png(str(first))
            changed = bytearray(pixels)
            changed[0] += 1 if changed[0] < 255 else -1
            control = output / "one-channel-mutation.png"
            compare.write_png(str(control), width, height, bytes(changed), bpp)
            exact("one-channel-control", control, first, 1)
    except Exception as error:
        manifest["errors"].append(f"{type(error).__name__}: {error}")
    finally:
        try:
            manifest["driver_post"], _ = fingerprint()
            require(manifest.get("driver_pre") == manifest["driver_post"],
                    "source, binary or staged inputs changed during qualification")
        except Exception as error:
            manifest["errors"].append(f"final fingerprint: {error}")
        manifest["end_utc"] = utc()
        manifest["pass"] = not manifest["errors"]
        save()
    print(json.dumps({"pass": manifest["pass"], "errors": manifest["errors"],
                      "manifest": str(output / "manifest.json")}, indent=2))
    return 0 if manifest["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
