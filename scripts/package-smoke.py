#!/usr/bin/env python3
"""Clean-box smoke for a packaged creation: unpack the archive, launch it, require CLEAN.

The archive is extracted into an empty temporary directory with its POSIX
permission bits restored, and the unpacked target is launched through
``fleet-run --build-dir <extraction root> <target> --auto-screenshot <frames>``.
The host's clean-box restrictions travel as ``ir-run`` child options, so they
reach only the packaged executable and never the bash wrapper chain that
launches it, and the wrapper is this checkout's ``scripts/fleet/fleet-run``
(``$FLEET_RUN`` overrides):

  macOS    unsets DYLD_LIBRARY_PATH, DYLD_FALLBACK_LIBRARY_PATH and
           DYLD_FALLBACK_FRAMEWORK_PATH, and runs the executable under a
           sandbox-exec profile denying reads of /opt/homebrew and /usr/local
           when a probe shows sandbox-exec can run here
           (``isolation=applied``); otherwise ``isolation=skipped``
  Windows  reduces the executable's PATH to %SystemRoot%\\System32 and
           %SystemRoot%
  Linux    unsets LD_LIBRARY_PATH

Build the package first (``fleet-build --target <target>Package``). A
fleet-host validator: it needs a display, so hosted CI runs only its suite.

Usage:
  package-smoke.py [--target NAME] [--build-dir DIR] [--zip FILE] [--frames N]

Exit codes:
  0 — the packaged executable ran and ir-run reported RESULT=CLEAN
  1 — any other verdict, or none (a loader failure, a crash, a wrapper error)
  2 — usage error, unsupported host, or a missing, ambiguous or malformed archive
"""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_common import run_capture, run_result, select_package_zip  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
PREFIX = "package-smoke"

HOST_PLATFORMS = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}

SANDBOX_EXEC = "/usr/bin/sandbox-exec"
DENIAL_PROFILE = ('(version 1) (allow default) (deny file-read* '
                  '(subpath "/opt/homebrew") (subpath "/usr/local"))')
MACOS_LOADER_VARS = ("DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
                     "DYLD_FALLBACK_FRAMEWORK_PATH")
PROBE_TIMEOUT_SECONDS = 10


class UsageError(Exception):
    pass


def extract_package(zip_path: Path, root: Path, target: str) -> Path:
    """Unpack ``zip_path`` under ``root``; returns the ``<target>/`` bundle.

    ``ZipFile.extract`` drops the archived mode bits, which leaves the packaged
    executable unlaunchable, so each entry's Unix mode is restored after the
    whole tree exists (a read-only directory must not block its children).
    """
    try:
        with zipfile.ZipFile(zip_path) as archive:
            infos = archive.infolist()
            tops = sorted({info.filename.split("/")[0] for info in infos})
            if tops != [target]:
                raise UsageError(
                    f"{zip_path.name}: archive top level is {tops}, not [{target!r}]")
            escaping = [info.filename for info in infos
                        if ".." in info.filename.split("/") or "\\" in info.filename]
            if escaping:
                raise UsageError(f"{zip_path.name}: entries leave the bundle: {escaping}")
            archive.extractall(root)
    except zipfile.BadZipFile as exc:
        raise UsageError(f"{zip_path.name}: not a readable zip ({exc})") from exc
    bundle = root / target
    if not any((bundle / name).is_file() for name in (target, f"{target}.exe")):
        raise UsageError(f"{zip_path.name}: no {target} executable in {target}/")
    if os.name == "posix":
        for info in sorted(infos, key=lambda entry: entry.is_dir()):
            mode = (info.external_attr >> 16) & 0o7777
            if mode:
                os.chmod(root / info.filename, mode)
    return bundle


def sandbox_available() -> tuple[bool, str]:
    try:
        probe = subprocess.run(
            [SANDBOX_EXEC, "-p", DENIAL_PROFILE, "/usr/bin/true"],
            capture_output=True, text=True, errors="replace",
            stdin=subprocess.DEVNULL, timeout=PROBE_TIMEOUT_SECONDS)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, f"{SANDBOX_EXEC} could not run ({exc})"
    if probe.returncode != 0:
        detail = (probe.stderr or probe.stdout).strip().splitlines()
        return False, (f"{SANDBOX_EXEC} probe exited {probe.returncode}"
                       + (f": {detail[-1]}" if detail else ""))
    return True, "homebrew-denial profile"


def child_options(host: str, environ: dict[str, str]) -> tuple[list[str], str | None]:
    """The ``ir-run`` child options for ``host`` and the isolation reading.

    The reading is ``None`` off macOS, where no sandbox applies.
    """
    if host == "Darwin":
        options = [arg for name in MACOS_LOADER_VARS
                   for arg in ("--child-unset", name)]
        applied, detail = sandbox_available()
        if applied:
            options += ["--child-prefix", SANDBOX_EXEC, "--child-prefix", "-p",
                        "--child-prefix", DENIAL_PROFILE]
            return options, f"isolation=applied ({detail})"
        return options, f"isolation=skipped ({detail})"
    if host == "Windows":
        sysroot = environ.get("SystemRoot", "")
        if not sysroot:
            raise UsageError("SystemRoot is not set")
        return ["--child-env", f"PATH={sysroot}\\System32;{sysroot}"], None
    return ["--child-unset", "LD_LIBRARY_PATH"], None


def resolve_fleet_run() -> str:
    """This checkout's wrapper, or ``$FLEET_RUN``.

    Not the ``fleet-run`` on PATH: that one belongs to whichever clone
    install.sh linked, and an ir-run without the child options rejects them.
    """
    wrapper = Path(os.environ.get("FLEET_RUN") or REPO_ROOT / "scripts" / "fleet" / "fleet-run")
    if not wrapper.is_file():
        raise UsageError(f"no fleet-run wrapper at {wrapper}")
    return wrapper.as_posix()


def smoke(args: argparse.Namespace, host: str) -> int:
    host_platform = HOST_PLATFORMS.get(host)
    if host_platform is None:
        raise UsageError(f"unsupported host {host!r} (macOS, Windows or Linux)")
    try:
        zip_path = select_package_zip(args.build_dir.resolve(), args.target, host_platform,
                                      args.zip.resolve() if args.zip else None)
    except ValueError as exc:
        raise UsageError(str(exc)) from exc
    fleet_run = resolve_fleet_run()
    options, isolation = child_options(host, dict(os.environ))

    with tempfile.TemporaryDirectory(prefix="package-smoke-") as tmp:
        root = Path(tmp)
        extract_package(zip_path, root, args.target)
        print(f"{PREFIX}: package={zip_path} extracted={root}")
        if isolation is not None:
            print(f"{PREFIX}: {isolation}")
        cmd = [fleet_run, *options, "--build-dir", str(root), args.target,
               "--auto-screenshot", str(args.frames)]
        rc, output = run_capture(cmd)

    verdict = run_result(output)
    if verdict == "CLEAN" and rc == 0:
        print(f"{PREFIX}: RESULT=PASS ir-run: RESULT=CLEAN exe={args.target}")
        return 0
    print(f"{PREFIX}: RESULT=FAIL ir-run verdict={verdict or 'none'} "
          f"fleet-run exit={rc}")
    return 1


def positive_int(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def main(argv: list[str] | None = None, host: str | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--target", default="IRShapeDebug")
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--zip", type=Path, default=None,
                        help="package to smoke (default: the native target package zip)")
    parser.add_argument("--frames", type=positive_int, default=10,
                        help="--auto-screenshot warmup frames (default: 10)")
    args = parser.parse_args(argv)
    try:
        return smoke(args, host or platform.system())
    except UsageError as exc:
        print(f"{PREFIX}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
