#!/usr/bin/env python3
"""Verify the native package carries its complete runtime-library closure.

On Windows, the probe walks the built executable with the package helper's
search directories and the audit walks the unpacked zip using only the bundle
and ``%SystemRoot%``. On macOS, the probe exercises CMake's recursive Mach-O
resolver and the audit inspects every unpacked Mach-O load command, rpath, and
ad-hoc signature.

Build the package first (``fleet-build --target <target>Package``).

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
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_common import select_package_zip  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
WINDOWS_WALK_SCRIPT = REPO_ROOT / "cmake" / "windows_dll_walk.cmake"
MACOS_WALK_SCRIPT = REPO_ROOT / "cmake" / "macos_bundle_dylibs.cmake"

FFMPEG_WINDOWS_ROOT_RES = {
    "avcodec": r"^avcodec-[0-9]+\.dll$",
    "avformat": r"^avformat-[0-9]+\.dll$",
    "avutil": r"^avutil-[0-9]+\.dll$",
    "swscale": r"^swscale-[0-9]+\.dll$",
}
FFMPEG_MACOS_ROOT_RES = {
    "avcodec": r"^libavcodec\.[0-9]+\.dylib$",
    "avformat": r"^libavformat\.[0-9]+\.dylib$",
    "avutil": r"^libavutil\.[0-9]+\.dylib$",
    "swscale": r"^libswscale\.[0-9]+\.dylib$",
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
        match = re.match(r"^([A-Za-z0-9_.+-]+):[A-Z]+=(.*)$", line)
        if match:
            entries[match.group(1)] = match.group(2)
    return entries


def norm(path: str) -> str:
    return path.replace("\\", "/").lower()


def under(path: str, root: str) -> bool:
    return norm(path).startswith(norm(root).rstrip("/") + "/")


def parse_walk_output(output: str) -> Walk:
    walk = Walk()
    for line in output.splitlines():
        if line.startswith("-- R "):
            walk.resolved.append(line[5:])
        elif line.startswith("-- U "):
            walk.unresolved.append(line[5:])
        elif line.startswith("-- C "):
            walk.conflicts.append(line[5:])
    return walk


def run_windows_walk(cmake: str, objdump: str, exe: Path, dirs: list[Path],
                     library_dir: Path | None = None,
                     skip: list[str] | None = None) -> Walk:
    cmd = [cmake, f"-DEXE={exe.as_posix()}", f"-DOBJDUMP={objdump}",
           "-DDIRS=" + ";".join(directory.as_posix() for directory in dirs)]
    if library_dir is not None:
        cmd.append(f"-DLIBRARY_DIR={library_dir.as_posix()}")
    if skip:
        cmd.append("-DSKIP_REGEXES=" + ";".join(skip))
    cmd += ["-P", str(WINDOWS_WALK_SCRIPT)]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace",
                          stdin=subprocess.DEVNULL)
    if proc.returncode != 0:
        print(proc.stdout + proc.stderr, file=sys.stderr)
        fail_usage(f"dependency walk failed (rc={proc.returncode}) for {exe}")
    return parse_walk_output(proc.stdout)


def run_macos_walk(cmake: str, otool: str, exe: Path) -> Walk:
    cmd = [cmake, f"-DEXE={exe.as_posix()}", f"-DOTOOL={otool}",
           "-DREPORT_ONLY=ON", "-P", str(MACOS_WALK_SCRIPT)]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace",
                          stdin=subprocess.DEVNULL)
    output = proc.stdout + proc.stderr
    walk = parse_walk_output(output)
    if proc.returncode != 0 and not (walk.unresolved or walk.conflicts):
        print(output, file=sys.stderr)
        fail_usage(f"dependency walk failed (rc={proc.returncode}) for {exe}")
    return walk


def find_built_exe(build_dir: Path, target: str, suffix: str) -> Path:
    name = f"{target}{suffix}"
    candidates = [path for path in (build_dir / "creations").rglob(name)
                  if path.is_file()]
    if not candidates:
        fail_usage(f"no {name} under {build_dir / 'creations'}; build {target} first")
    candidates.sort(key=lambda path: len(path.parts))
    return candidates[0]


def windows_search_dirs(cache: dict[str, str], exe_dir: Path) -> list[Path]:
    dirs = [exe_dir]
    ffmpeg_root = cache.get("IRREDEN_FFMPEG_ROOT", "")
    if ffmpeg_root:
        dirs.append(Path(ffmpeg_root) / "bin")
    dirs.append(Path(cache["CMAKE_CXX_COMPILER"]).parent)
    return dirs


def report_failures(walk: Walk, noun: str) -> list[str]:
    failures = []
    if walk.unresolved:
        failures.append(f"unresolved {noun}: {walk.unresolved}")
    if walk.conflicts:
        failures.append(f"conflicting library names: {walk.conflicts}")
    return failures


def check_windows_probe(cache: dict[str, str], cmake: str, build_dir: Path,
                        target: str, sysroot: str) -> bool:
    objdump = cache.get("CMAKE_OBJDUMP", "")
    if not objdump or not cache.get("CMAKE_CXX_COMPILER"):
        fail_usage("CMakeCache.txt lacks CMAKE_OBJDUMP or CMAKE_CXX_COMPILER")
    exe = find_built_exe(build_dir, target, ".exe")
    dirs = windows_search_dirs(cache, exe.parent)
    print(f"probe: exe={exe}")
    print(f"probe: dirs={[directory.as_posix() for directory in dirs]}")

    full = run_windows_walk(cmake, objdump, exe, dirs)
    without = run_windows_walk(
        cmake, objdump, exe, dirs, skip=list(FFMPEG_WINDOWS_ROOT_RES.values()))

    def non_system(walk: Walk) -> dict[str, str]:
        return {os.path.basename(norm(path)): path for path in walk.resolved
                if not under(path, sysroot)}

    full_set = non_system(full)
    without_set = non_system(without)
    roots = {}
    for label, pattern in FFMPEG_WINDOWS_ROOT_RES.items():
        for name, path in full_set.items():
            if re.match(pattern, name):
                roots[label] = path
    ffmpeg_only = sorted(set(full_set) - set(without_set)
                         - {os.path.basename(norm(path))
                            for path in roots.values()})

    print(f"probe: resolved non-system={len(full_set)} "
          f"system={len(full.resolved) - len(full_set)}")
    for label in FFMPEG_WINDOWS_ROOT_RES:
        print(f"probe: root {label}: {roots.get(label, 'MISSING')}")
    print(f"probe: reachable only through the FFmpeg roots={len(ffmpeg_only)}: "
          f"{', '.join(ffmpeg_only)}")
    print(f"probe: unresolved={full.unresolved} conflicts={full.conflicts}")

    failures = report_failures(full, "imports")
    missing = [label for label in FFMPEG_WINDOWS_ROOT_RES if label not in roots]
    if missing:
        failures.append(f"FFmpeg roots not resolved: {missing}")
    if not ffmpeg_only:
        failures.append("no DLL is reachable only through the FFmpeg roots")
    return verdict("probe", failures)


def check_windows_audit(cache: dict[str, str], cmake: str, build_dir: Path,
                        target: str, zip_path: Path | None, sysroot: str) -> bool:
    objdump = cache.get("CMAKE_OBJDUMP", "")
    if not objdump:
        fail_usage("CMakeCache.txt lacks CMAKE_OBJDUMP")
    zip_path = select_zip(build_dir, target, "windows", zip_path)
    with tempfile.TemporaryDirectory(prefix="package-verify-") as tmp:
        names, bundle = extract_zip(zip_path, Path(tmp), target)
        exe = bundle / f"{target}.exe"
        bundled = sorted(path.name for path in bundle.glob("*.dll"))
        print(f"audit: zip={zip_path} ({zip_path.stat().st_size} B) "
              f"top-level={top_levels(names)}")
        print(f"audit: bundled DLLs={len(bundled)}")

        failures = validate_archive_shape(names, bundle, exe, target)
        if failures:
            return verdict("audit", failures)
        walk = run_windows_walk(cmake, objdump, exe, [bundle], library_dir=bundle)
        in_bundle = [path for path in walk.resolved if under(path, str(bundle))]
        in_system = [path for path in walk.resolved if under(path, sysroot)]
        outside = [path for path in walk.resolved
                   if not under(path, str(bundle)) and not under(path, sysroot)]
        print(f"audit: resolved in bundle={len(in_bundle)} system={len(in_system)} "
              f"outside={outside}")
        print(f"audit: unresolved={walk.unresolved} conflicts={walk.conflicts}")

    failures += report_failures(walk, "imports")
    if outside:
        failures.append(f"resolved outside the bundle and {sysroot}: {outside}")
    if not in_bundle:
        failures.append("nothing resolved from the bundle; the walk saw no bundled DLL")
    return verdict("audit", failures)


def command_path(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        fail_usage(f"{name} is not on PATH")
    return path


def run_tool(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace",
                          stdin=subprocess.DEVNULL)


def macos_dependencies(otool: str, macho: Path) -> list[str]:
    proc = run_tool([otool, "-L", str(macho)])
    if proc.returncode != 0:
        fail_usage(f"otool -L failed for {macho}: {proc.stderr.strip()}")
    dependencies = []
    for line in proc.stdout.splitlines()[1:]:
        match = re.match(r"^\s+(.+) \(compatibility", line)
        if match:
            dependencies.append(match.group(1))
    return dependencies


def macos_rpaths(otool: str, macho: Path) -> list[str]:
    proc = run_tool([otool, "-l", str(macho)])
    if proc.returncode != 0:
        fail_usage(f"otool -l failed for {macho}: {proc.stderr.strip()}")
    rpaths = []
    expect_path = False
    for line in proc.stdout.splitlines():
        if "cmd LC_RPATH" in line:
            expect_path = True
        elif expect_path:
            match = re.search(r"path (.+) \(offset", line)
            if match:
                rpaths.append(match.group(1))
                expect_path = False
    return rpaths


def is_macos_system(reference: str) -> bool:
    return reference.startswith("/usr/lib/") or reference.startswith(
        "/System/Library/")


def dependency_leaf(reference: str) -> str:
    return reference.rsplit("/", 1)[-1]


def check_macos_probe(cmake: str, build_dir: Path, target: str,
                      otool: str) -> bool:
    exe = find_built_exe(build_dir, target, "")
    full = run_macos_walk(cmake, otool, exe)
    closure = {Path(path).name: path for path in full.resolved}
    direct = {dependency_leaf(reference) for reference in macos_dependencies(otool, exe)
              if not is_macos_system(reference)}
    roots = {}
    for label, pattern in FFMPEG_MACOS_ROOT_RES.items():
        for name in direct:
            if re.match(pattern, name):
                roots[label] = closure.get(name, "MISSING")
    transitive = sorted(set(closure) - direct)

    print(f"probe: exe={exe}")
    print(f"probe: resolved non-system={len(closure)} direct={len(direct)} "
          f"transitive-only={len(transitive)}")
    for label in FFMPEG_MACOS_ROOT_RES:
        print(f"probe: root {label}: {roots.get(label, 'MISSING')}")
    print(f"probe: transitive-only dylibs={len(transitive)}: {', '.join(transitive)}")
    print(f"probe: unresolved={full.unresolved} conflicts={full.conflicts}")

    failures = report_failures(full, "dependencies")
    missing = [label for label in FFMPEG_MACOS_ROOT_RES
               if roots.get(label, "MISSING") == "MISSING"]
    if missing:
        failures.append(f"FFmpeg roots not resolved directly: {missing}")
    if not transitive:
        failures.append("no dylib is reachable only through a direct dependency")
    return verdict("probe", failures)


def check_macos_audit(build_dir: Path, target: str, zip_path: Path | None,
                      otool: str, codesign: str) -> bool:
    zip_path = select_zip(build_dir, target, "macos", zip_path)
    with tempfile.TemporaryDirectory(prefix="package-verify-") as tmp:
        names, bundle = extract_zip(zip_path, Path(tmp), target)
        exe = bundle / target
        failures = validate_archive_shape(names, bundle, exe, target)
        machos = [exe] + sorted(bundle.glob("*.dylib")) if exe.is_file() else []
        bundled = {path.name for path in machos[1:]}
        direct = set()
        local_edges = 0
        external_edges = []
        package_rpaths = []
        signature_failures = []

        for macho in machos:
            signature = run_tool([codesign, "--verify", "--strict", str(macho)])
            if signature.returncode != 0:
                signature_failures.append(
                    f"{macho.name}: {(signature.stderr or signature.stdout).strip()}")
            for reference in macos_dependencies(otool, macho):
                if is_macos_system(reference):
                    continue
                leaf = dependency_leaf(reference)
                if macho == exe:
                    direct.add(leaf)
                if reference == f"@loader_path/{macho.name}" and macho != exe:
                    continue
                if reference.startswith("@loader_path/") and leaf in bundled:
                    local_edges += 1
                else:
                    external_edges.append(f"{macho.name}: {reference}")
            for rpath in macos_rpaths(otool, macho):
                if rpath.startswith(("/opt/homebrew", "/usr/local")):
                    package_rpaths.append(f"{macho.name}: {rpath}")

        transitive = sorted(bundled - direct)
        print(f"audit: zip={zip_path} ({zip_path.stat().st_size} B) "
              f"top-level={top_levels(names)}")
        print(f"audit: bundled dylibs={len(bundled)} direct={len(direct)} "
              f"transitive-only={len(transitive)} local-edges={local_edges}")
        print(f"audit: external non-system edges={external_edges}")
        print(f"audit: package-manager rpaths={package_rpaths}")
        print(f"audit: signature failures={signature_failures}")

    if external_edges:
        failures.append(f"non-system load commands do not resolve in bundle: {external_edges}")
    if package_rpaths:
        failures.append(f"package-manager rpaths remain: {package_rpaths}")
    if signature_failures:
        failures.append(f"invalid Mach-O signatures: {signature_failures}")
    if not transitive:
        failures.append("no transitive-only dylib is bundled")
    return verdict("audit", failures)


def select_zip(build_dir: Path, target: str, platform: str,
               zip_path: Path | None) -> Path:
    try:
        return select_package_zip(build_dir, target, platform, zip_path)
    except ValueError as exc:
        fail_usage(str(exc))
        raise


def extract_zip(zip_path: Path, root: Path,
                target: str) -> tuple[list[str], Path]:
    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()
        archive.extractall(root)
    return names, root / target


def top_levels(names: list[str]) -> list[str]:
    return sorted({name.split("/")[0] for name in names})


def validate_archive_shape(names: list[str], bundle: Path, exe: Path,
                           target: str) -> list[str]:
    failures = []
    top = top_levels(names)
    if top != [target]:
        failures.append(f"archive top level is {top}, not [{target!r}]")
    if not exe.is_file():
        failures.append(f"no {exe.name} in the archive's {target}/ folder")
    return failures


def verdict(check: str, failures: list[str]) -> bool:
    for failure in failures:
        print(f"{check}: FAIL {failure}")
    print(f"{check}: RESULT={'FAIL' if failures else 'PASS'}")
    return not failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--target", default="IRShapeDebug")
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--zip", type=Path, default=None,
                        help="package to audit (default: native target package zip)")
    parser.add_argument("--check", action="append", choices=("probe", "audit"),
                        help="run only these checks (default: both)")
    args = parser.parse_args()

    if sys.platform not in ("win32", "darwin"):
        fail_usage("native Windows or macOS only")
    build_dir = args.build_dir.resolve()
    cache = read_cache(build_dir)
    cmake = cache.get("CMAKE_COMMAND", "")
    if not cmake:
        fail_usage("CMakeCache.txt lacks CMAKE_COMMAND")

    checks = args.check or ["probe", "audit"]
    ok = True
    if sys.platform == "win32":
        sysroot = os.environ.get("SystemRoot", "")
        if not sysroot:
            fail_usage("SystemRoot is not set")
        if "probe" in checks:
            ok &= check_windows_probe(cache, cmake, build_dir, args.target, sysroot)
        if "audit" in checks:
            ok &= check_windows_audit(
                cache, cmake, build_dir, args.target, args.zip, sysroot)
    else:
        otool = command_path("otool")
        if "probe" in checks:
            ok &= check_macos_probe(cmake, build_dir, args.target, otool)
        if "audit" in checks:
            ok &= check_macos_audit(
                build_dir, args.target, args.zip, otool, command_path("codesign"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
