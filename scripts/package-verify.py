#!/usr/bin/env python3
"""Check that a Windows ``<target>Package`` zip carries its complete DLL closure.

Two checks, both driven by ``cmake/windows_dll_walk.cmake``
(``file(GET_RUNTIME_DEPENDENCIES)`` with the build's configured objdump):

``probe``
  Walks the built executable with the search directories the package helper
  uses (exe dir, ``IRREDEN_FFMPEG_ROOT/bin`` when set, the compiler's
  ``bin/``). Passes when the four FFmpeg roots resolve, at least one DLL is
  reachable only through them (a second walk with the roots pre-excluded
  must lose it), and nothing is unresolved or conflicting. This is the
  premise the bundler rests on: the resolver really does see the FFmpeg
  closure that a direct-dependency copy misses.

``audit``
  Unzips ``<build>/<target>-windows-*.zip`` into an empty temporary
  directory and walks the unpacked exe plus every bundled DLL with only the
  bundle directory to search. Passes when every resolved DLL is inside the
  bundle or under ``%SystemRoot%``, nothing is unresolved or conflicting,
  and at least one dependency resolved from the bundle.

Build the package first (``fleet-build --target <target>Package``). Native
Windows only; the walk never consults ``PATH``.

Usage:
  package-verify.py [--target NAME] [--build-dir DIR] [--zip FILE]
                    [--check probe|audit]...

Exit codes:
  0 — every selected check passed
  1 — a check failed
  2 — usage error, unsupported host, or a missing build artifact
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
WALK_SCRIPT = REPO_ROOT / "cmake" / "windows_dll_walk.cmake"

# CMake lowercases import names before matching pre-exclude regexes.
FFMPEG_ROOT_RES = {
    "avcodec": r"^avcodec-[0-9]+\.dll$",
    "avformat": r"^avformat-[0-9]+\.dll$",
    "avutil": r"^avutil-[0-9]+\.dll$",
    "swscale": r"^swscale-[0-9]+\.dll$",
}


@dataclass
class Walk:
    resolved: list[str] = field(default_factory=list)
    unresolved: list[str] = field(default_factory=list)
    conflicts: list[str] = field(default_factory=list)


def fail_usage(msg: str) -> None:
    print(f"package-verify: {msg}", file=sys.stderr)
    sys.exit(2)


def read_cache(build_dir: Path) -> dict[str, str]:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        fail_usage(f"no CMakeCache.txt at {cache}; configure the build first")
    entries: dict[str, str] = {}
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"^([A-Za-z0-9_.+-]+):[A-Z]+=(.*)$", line)
        if m:
            entries[m.group(1)] = m.group(2)
    return entries


def norm(path: str) -> str:
    return path.replace("\\", "/").lower()


def under(path: str, root: str) -> bool:
    return norm(path).startswith(norm(root).rstrip("/") + "/")


def run_walk(cmake: str, objdump: str, exe: Path, dirs: list[Path],
             library_dir: Path | None = None,
             skip: list[str] | None = None) -> Walk:
    cmd = [cmake, f"-DEXE={exe.as_posix()}", f"-DOBJDUMP={objdump}",
           "-DDIRS=" + ";".join(d.as_posix() for d in dirs)]
    if library_dir is not None:
        cmd.append(f"-DLIBRARY_DIR={library_dir.as_posix()}")
    if skip:
        cmd.append("-DSKIP_REGEXES=" + ";".join(skip))
    cmd += ["-P", str(WALK_SCRIPT)]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace",
                          stdin=subprocess.DEVNULL)
    if proc.returncode != 0:
        print(proc.stdout + proc.stderr, file=sys.stderr)
        fail_usage(f"dependency walk failed (rc={proc.returncode}) for {exe}")
    walk = Walk()
    for line in proc.stdout.splitlines():
        if line.startswith("-- R "):
            walk.resolved.append(line[5:])
        elif line.startswith("-- U "):
            walk.unresolved.append(line[5:])
        elif line.startswith("-- C "):
            walk.conflicts.append(line[5:])
    return walk


def find_built_exe(build_dir: Path, target: str) -> Path:
    name = f"{target}.exe"
    candidates = [p for p in (build_dir / "creations").rglob(name) if p.is_file()]
    if not candidates:
        fail_usage(f"no {name} under {build_dir / 'creations'}; build {target} first")
    candidates.sort(key=lambda p: len(p.parts))
    return candidates[0]


def search_dirs(cache: dict[str, str], exe_dir: Path) -> list[Path]:
    dirs = [exe_dir]
    ffmpeg_root = cache.get("IRREDEN_FFMPEG_ROOT", "")
    if ffmpeg_root:
        dirs.append(Path(ffmpeg_root) / "bin")
    dirs.append(Path(cache["CMAKE_CXX_COMPILER"]).parent)
    return dirs


def report_failures(walk: Walk) -> list[str]:
    failures = []
    if walk.unresolved:
        failures.append(f"unresolved imports: {walk.unresolved}")
    if walk.conflicts:
        failures.append(f"conflicting DLL names: {walk.conflicts}")
    return failures


def check_probe(cache: dict[str, str], cmake: str, objdump: str,
                build_dir: Path, target: str, sysroot: str) -> bool:
    exe = find_built_exe(build_dir, target)
    dirs = search_dirs(cache, exe.parent)
    print(f"probe: exe={exe}")
    print(f"probe: dirs={[d.as_posix() for d in dirs]}")

    full = run_walk(cmake, objdump, exe, dirs)
    without = run_walk(cmake, objdump, exe, dirs, skip=list(FFMPEG_ROOT_RES.values()))

    def non_system(walk: Walk) -> dict[str, str]:
        return {os.path.basename(norm(p)): p for p in walk.resolved
                if not under(p, sysroot)}

    full_set = non_system(full)
    without_set = non_system(without)
    roots = {}
    for label, pattern in FFMPEG_ROOT_RES.items():
        for name, path in full_set.items():
            if re.match(pattern, name):
                roots[label] = path
    ffmpeg_only = sorted(set(full_set) - set(without_set)
                         - {os.path.basename(norm(p)) for p in roots.values()})

    print(f"probe: resolved non-system={len(full_set)} "
          f"system={len(full.resolved) - len(full_set)}")
    for label in FFMPEG_ROOT_RES:
        print(f"probe: root {label}: {roots.get(label, 'MISSING')}")
    print(f"probe: reachable only through the FFmpeg roots={len(ffmpeg_only)}: "
          f"{', '.join(ffmpeg_only)}")
    print(f"probe: unresolved={full.unresolved} conflicts={full.conflicts}")

    failures = report_failures(full)
    missing = [label for label in FFMPEG_ROOT_RES if label not in roots]
    if missing:
        failures.append(f"FFmpeg roots not resolved: {missing}")
    if not ffmpeg_only:
        failures.append("no DLL is reachable only through the FFmpeg roots")
    return verdict("probe", failures)


def check_audit(cmake: str, objdump: str, build_dir: Path, target: str,
                zip_path: Path | None, sysroot: str) -> bool:
    if zip_path is None:
        zips = sorted(build_dir.glob(f"{target}-windows-*.zip"))
        if len(zips) != 1:
            fail_usage(f"expected one {target}-windows-*.zip in {build_dir}, found "
                       f"{[z.name for z in zips]}; build {target}Package or pass --zip")
        zip_path = zips[0]
    if not zip_path.is_file():
        fail_usage(f"no package at {zip_path}")

    with tempfile.TemporaryDirectory(prefix="package-verify-") as tmp:
        with zipfile.ZipFile(zip_path) as archive:
            names = archive.namelist()
            archive.extractall(tmp)
        top = sorted({n.split("/")[0] for n in names})
        bundle = Path(tmp) / target
        exe = bundle / f"{target}.exe"
        bundled = sorted(p.name for p in bundle.glob("*.dll"))
        print(f"audit: zip={zip_path} ({zip_path.stat().st_size} B) top-level={top}")
        print(f"audit: bundled DLLs={len(bundled)}")

        failures = []
        if top != [target]:
            failures.append(f"archive top level is {top}, not [{target!r}]")
        if not exe.is_file():
            failures.append(f"no {exe.name} in the archive's {target}/ folder")
            return verdict("audit", failures)

        walk = run_walk(cmake, objdump, exe, [bundle], library_dir=bundle)
        in_bundle = [p for p in walk.resolved if under(p, str(bundle))]
        in_system = [p for p in walk.resolved if under(p, sysroot)]
        outside = [p for p in walk.resolved
                   if not under(p, str(bundle)) and not under(p, sysroot)]
        print(f"audit: resolved in bundle={len(in_bundle)} system={len(in_system)} "
              f"outside={outside}")
        print(f"audit: unresolved={walk.unresolved} conflicts={walk.conflicts}")

    failures += report_failures(walk)
    if outside:
        failures.append(f"resolved outside the bundle and {sysroot}: {outside}")
    if not in_bundle:
        failures.append("nothing resolved from the bundle; the walk saw no bundled DLL")
    return verdict("audit", failures)


def verdict(check: str, failures: list[str]) -> bool:
    for f in failures:
        print(f"{check}: FAIL {f}")
    print(f"{check}: RESULT={'FAIL' if failures else 'PASS'}")
    return not failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--target", default="IRShapeDebug")
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--zip", type=Path, default=None,
                        help="package to audit (default: <build>/<target>-windows-*.zip)")
    parser.add_argument("--check", action="append", choices=("probe", "audit"),
                        help="run only these checks (default: both)")
    args = parser.parse_args()

    if sys.platform != "win32":
        fail_usage("native Windows only")
    sysroot = os.environ.get("SystemRoot", "")
    if not sysroot:
        fail_usage("SystemRoot is not set")

    build_dir = args.build_dir.resolve()
    cache = read_cache(build_dir)
    cmake = cache.get("CMAKE_COMMAND", "")
    objdump = cache.get("CMAKE_OBJDUMP", "")
    if not cmake or not objdump or not cache.get("CMAKE_CXX_COMPILER"):
        fail_usage("CMakeCache.txt lacks CMAKE_COMMAND, CMAKE_OBJDUMP or CMAKE_CXX_COMPILER")

    ok = True
    checks = args.check or ["probe", "audit"]
    if "probe" in checks:
        ok &= check_probe(cache, cmake, objdump, build_dir, args.target, sysroot)
    if "audit" in checks:
        ok &= check_audit(cmake, objdump, build_dir, args.target, args.zip, sysroot)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
