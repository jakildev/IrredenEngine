#!/usr/bin/env python3
"""Run two Debug LOD source fault controls, restoring and rebuilding after each."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
from datetime import datetime, timezone


CONTROLS = (
    ("residency", "creations/demos/shape_debug/configs/lod_resident.prefab.lua",
     "    part.resident = true", "    part.resident = false",
     "LOD row must use its authored residency"),
    ("missing-parent", "creations/demos/shape_debug/lod_scene.hpp",
     "            IREntity::setParent(entity, m_denseRoot);",
     "            // Negative control: intentionally omit the DENSE parent relation.",
     "LOD child must follow its moving parent translation"),
)


def utc():
    return datetime.now(timezone.utc).isoformat()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    build = (args.build_dir or root / "build").resolve()
    output = args.output.resolve()
    builder, runner = [root / "scripts/fleet" / name for name in ("fleet-build", "fleet-run")]
    cache = (build / "CMakeCache.txt").read_text()
    if not re.search(r"^CMAKE_BUILD_TYPE:STRING=Debug$", cache, re.MULTILINE):
        parser.error("explicit Debug build required")
    commands = json.loads((build / "compile_commands.json").read_text())
    entries = [e for e in commands if Path(e["file"]).resolve() == root / "creations/demos/shape_debug/main.cpp"]
    if len(entries) != 1:
        parser.error("expected exactly one ShapeDebug compile entry")
    compile_argv = entries[0].get("arguments") or shlex.split(entries[0]["command"])
    if any(a == "-DIR_RELEASE" or a.startswith("-DIR_RELEASE=") for a in compile_argv):
        parser.error("IR_RELEASE removes required assertions")
    if output.exists() or not builder.is_file() or not runner.is_file():
        parser.error("output must be new and fleet wrappers must exist")
    originals = {rel: (root / rel).read_bytes() for _, rel, *_ in CONTROLS}
    for _, rel, old, _, _ in CONTROLS:
        if originals[rel].decode().count(old) != 1:
            parser.error(f"expected one mutation anchor in {rel}")
    output.mkdir(parents=True)
    env = dict(os.environ, IRREDEN_BUILD_DIR=str(build), IR_WINDOW_MODE="offscreen")
    manifest = {"start_utc": utc(), "root": str(root), "build_dir": str(build),
                "assertions_enabled": True, "scope": "Debug source fault controls, no performance claim",
                "driver_sha256": digest(Path(__file__).read_bytes()), "runs": [], "errors": [],
                "initial_source_sha256": {rel: digest(data) for rel, data in originals.items()}}
    for rel, data in originals.items():
        path = output / "original-source" / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def save():
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def execute(argv, directory, label, record):
        log = directory / (label + ".log")
        step = {"argv": list(map(str, argv)), "start_utc": utc(), "log": str(log)}
        record["steps"].append(step)
        save()
        with log.open("wb") as stream:
            process = subprocess.Popen(argv, cwd=root, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait()
            except BaseException:
                # Reap the wrapper and its subprocesses before source restoration.
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                step.update(end_utc=utc(), exit_code=process.returncode, interrupted=True)
                save()
                raise
        step.update(end_utc=utc(), exit_code=code)
        save()
        return code, log.read_text(errors="replace")

    def checked(argv, directory, label, record):
        code, log = execute(argv, directory, label, record)
        if code != 0:
            raise RuntimeError(f"{record['name']}: {label} failed ({code})")
        return log

    def interrupted(signum, _frame):
        raise KeyboardInterrupt(f"received signal {signum}")

    signal.signal(signal.SIGTERM, interrupted)
    save()
    try:
        for name, rel, old, new, expected in CONTROLS:
            directory = output / name
            directory.mkdir()
            record = {"name": name, "source": rel, "expected_diagnostic": expected,
                      "steps": [], "passed": False, "restored": False}
            manifest["runs"].append(record)
            source = root / rel
            original = originals[rel]
            if any((root / p).read_bytes() != data for p, data in originals.items()):
                raise RuntimeError("source changed externally before mutation")
            mutated = original.decode().replace(old, new).encode()
            patch = directory / "mutation.patch"
            patch.write_text("diff --git a/" + rel + " b/" + rel + "\n" + "".join(
                difflib.unified_diff(original.decode().splitlines(True), mutated.decode().splitlines(True),
                                     fromfile="a/" + rel, tofile="b/" + rel)))
            record["mutation_sha256"] = digest(mutated)
            applied = False
            staged = build / "creations/demos/shape_debug/configs/lod_resident.prefab.lua"
            try:
                checked(["git", "apply", "--check", str(patch)], directory, "apply-check", record)
                checked(["git", "apply", str(patch)], directory, "apply", record)
                applied = True
                if source.read_bytes() != mutated:
                    raise RuntimeError("mutation did not produce exact expected bytes")
                checked([str(builder), "--target", "IRShapeDebug", "--", "-j4"], directory, "build-mutated", record)
                if name == "residency" and staged.read_bytes() != mutated:
                    raise RuntimeError("build did not stage mutated residency Lua")
                code, log = execute([str(runner), "--build-dir", str(build), "--timeout", "120",
                                     "IRShapeDebug", "--lod-scene", "--pivot-origin", "--auto-screenshot", "20",
                                     "--window-mode", "offscreen"], directory, "run-mutated", record)
                record["intended_assertion_seen"] = expected in log
                record["native_exit_code"] = code
                if code == 0 or expected not in log or "RESULT=CLEAN" in log:
                    raise RuntimeError(f"{name}: intended nonzero assertion failure not observed")
                if source.read_bytes() != mutated:
                    raise RuntimeError("source changed externally during negative run")
                record["passed"] = True
            finally:
                if applied or source.read_bytes() == mutated:
                    # Do not clobber concurrent edits. Reverse the exact patch and verify bytes.
                    old_int, old_term = signal.signal(signal.SIGINT, signal.SIG_IGN), signal.signal(signal.SIGTERM, signal.SIG_IGN)
                    try:
                        checked(["git", "apply", "--reverse", "--check", str(patch)], directory, "reverse-check", record)
                        checked(["git", "apply", "--reverse", str(patch)], directory, "reverse", record)
                        if source.read_bytes() != original:
                            raise RuntimeError("source restoration differs from saved original")
                        record["restored"] = True
                        record["restored_sha256"] = digest(source.read_bytes())
                        checked([str(builder), "--target", "IRShapeDebug", "--", "-j4"], directory, "build-restored", record)
                        if name == "residency" and staged.read_bytes() != original:
                            raise RuntimeError("restoration build did not restage original Lua")
                    finally:
                        signal.signal(signal.SIGINT, old_int)
                        signal.signal(signal.SIGTERM, old_term)
                        save()
    except BaseException as error:
        manifest["errors"].append(f"{type(error).__name__}: {error}")
    finally:
        manifest["end_utc"] = utc()
        manifest["final_source_sha256"] = {rel: digest((root / rel).read_bytes()) for rel in originals}
        manifest["sources_restored"] = all((root / rel).read_bytes() == data for rel, data in originals.items())
        manifest["passed"] = (not manifest["errors"] and manifest["sources_restored"] and
                              len(manifest["runs"]) == 2 and all(r["passed"] and r["restored"] for r in manifest["runs"]))
        save()
    print(json.dumps({"passed": manifest["passed"], "sources_restored": manifest["sources_restored"],
                      "errors": manifest["errors"], "manifest": str(output / "manifest.json")}, indent=2))
    return 0 if manifest["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
