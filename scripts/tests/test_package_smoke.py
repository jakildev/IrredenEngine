"""Hermetic tests for scripts/package-smoke.py.

Two layers. A logging ``fleet-run`` stub named by ``$FLEET_RUN`` records the argv the harness
launches, the mode of the extracted executable and the environment the wrapper
received, then prints the verdict the case asks for; that pins archive
selection, extraction, the per-host child options and the exit-code contract.
The end-to-end cases leave ``$FLEET_RUN`` unset, so the harness takes this
checkout's real ``scripts/fleet/fleet-run``, and
so the packaged "executable" (a shell script) reports the environment it was
actually launched with: the clean-box restrictions reach it while the wrapper
keeps the caller's environment, and the macOS sandbox prefix (a logging stub
standing in for ``/usr/bin/sandbox-exec``) sits directly in front of it.

No engine build, no GPU: ir-acquire runs against an isolated IR_LOCK_ROOT.
The stubs are POSIX scripts, so the suite skips on native Windows.
"""

from __future__ import annotations

import importlib.util
import io
import os
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPTS.parent
sys.path.insert(0, str(SCRIPTS))

_spec = importlib.util.spec_from_file_location("package_smoke", SCRIPTS / "package-smoke.py")
package_smoke = importlib.util.module_from_spec(_spec)
assert _spec and _spec.loader
_spec.loader.exec_module(package_smoke)

TARGET = "IRShapeDebug"


def _dyld_survives_exec() -> bool:
    """macOS SIP purges DYLD_* when it execs a protected binary such as /bin/sh,
    so a shell-script probe cannot observe them there; Linux keeps them."""
    if os.name != "posix":
        return False
    probe = subprocess.run(["/bin/sh", "-c", 'echo "${DYLD_LIBRARY_PATH-unset}"'],
                           env={**os.environ, "DYLD_LIBRARY_PATH": "/probe"},
                           capture_output=True, text=True)
    return probe.stdout.strip() == "/probe"


DYLD_OBSERVABLE = _dyld_survives_exec()

FLEET_RUN_STUB = """#!/bin/sh
log="$STUB_LOG"
: > "$log"
root=""
prev=""
for arg in "$@"; do
    echo "arg=$arg" >> "$log"
    [ "$prev" = "--build-dir" ] && root="$arg"
    prev="$arg"
done
exe="$root/IRShapeDebug/IRShapeDebug"
if [ -x "$exe" ]; then echo "exe=executable" >> "$log"; else echo "exe=not-executable" >> "$log"; fi
echo "wrapper-ld=${LD_LIBRARY_PATH-unset}" >> "$log"
echo "wrapper-dyld=${DYLD_LIBRARY_PATH-unset}" >> "$log"
echo "wrapper-path=$PATH" >> "$log"
case "$STUB_MODE" in
    clean) echo "ir-run: RESULT=CLEAN exe=IRShapeDebug exit=0"; exit 0 ;;
    crash) echo "dyld[1]: Library not loaded: @rpath/libglfw.3.dylib"
           echo "ir-run: RESULT=CRASH exe=IRShapeDebug exit=134 signal=SIGABRT"; exit 134 ;;
    none) echo "ir-run: running debug tree $root"; exit 0 ;;
    wrapper-error) echo "fleet-common.sh: line 241: task: unbound variable"; exit 1 ;;
    clean-then-crash) echo "ir-run: RESULT=CLEAN exe=IRShapeDebug exit=0"
                      echo "ir-run: RESULT=CRASH exe=IRShapeDebug exit=1 signal=none"; exit 1 ;;
esac
exit 3
"""

PACKAGED_EXE = """#!/bin/sh
echo "exe-ld=${LD_LIBRARY_PATH-unset}"
echo "exe-dyld=${DYLD_LIBRARY_PATH-unset}"
echo "exe-dyld-fallback=${DYLD_FALLBACK_LIBRARY_PATH-unset}"
echo "exe-path=$PATH"
echo "exe-cwd-lib=$(ls libdemo.dylib 2>/dev/null || echo missing)"
for arg in "$@"; do echo "exe-arg=$arg"; done
"""

SANDBOX_STUB = """#!/bin/sh
echo "sandbox-argv=$*" >> "{log}"
[ "$1" = "-p" ] || exit 64
shift 2
echo "sandbox-dyld=${{DYLD_LIBRARY_PATH-unset}}" >> "{log}"
exec "$@"
"""


def _write_exec(path: Path, body: str) -> Path:
    path.write_text(body)
    path.chmod(0o755)
    return path


def make_zip(path: Path, entries: list[tuple[str, bytes, int]]) -> Path:
    with zipfile.ZipFile(path, "w") as archive:
        for name, data, mode in entries:
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.external_attr = mode << 16
            archive.writestr(info, data)
    return path


def package_entries(exe_body: str = PACKAGED_EXE) -> list[tuple[str, bytes, int]]:
    return [
        (f"{TARGET}/", b"", stat.S_IFDIR | 0o755),
        (f"{TARGET}/{TARGET}", exe_body.encode(), stat.S_IFREG | 0o755),
        (f"{TARGET}/libdemo.dylib", b"lib", stat.S_IFREG | 0o444),
        (f"{TARGET}/data/", b"", stat.S_IFDIR | 0o755),
        (f"{TARGET}/data/config.lua", b"-- config", stat.S_IFREG | 0o644),
    ]


@unittest.skipUnless(os.name == "posix", "POSIX stubs")
class HarnessCase(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.tmp = Path(temp.name)
        self.build = self.tmp / "build"
        self.build.mkdir()
        self.bin = self.tmp / "bin"
        self.bin.mkdir()
        self.log = self.tmp / "fleet-run.log"
        self.sandbox_log = self.tmp / "sandbox.log"
        self.sandbox = _write_exec(self.tmp / "sandbox-exec",
                                   SANDBOX_STUB.format(log=self.sandbox_log))
        env = {
            "PATH": f"{self.bin}{os.pathsep}{os.environ.get('PATH', '')}",
            "STUB_LOG": str(self.log),
            "LD_LIBRARY_PATH": "/wrapper/ld",
            "DYLD_LIBRARY_PATH": "/wrapper/dyld",
            "DYLD_FALLBACK_LIBRARY_PATH": "/wrapper/dyld-fallback",
            "IR_LOCK_ROOT": str(self.tmp / "locks"),
        }
        patcher = mock.patch.dict(os.environ, env)
        patcher.start()
        self.addCleanup(patcher.stop)
        for name in ("FLEET_RUN", "IR_WINDOW_MODE", "FLEET_RUN_DEFAULT_TIMEOUT",
                     "IR_RUN_RESULT_FILE", "IR_ACQUIRE_HOLDER_PID", "IR_ACQUIRE_HELD_VERB"):
            os.environ.pop(name, None)
        sandbox = mock.patch.object(package_smoke, "SANDBOX_EXEC", str(self.sandbox))
        sandbox.start()
        self.addCleanup(sandbox.stop)

    def stub_fleet_run(self, mode: str) -> None:
        os.environ["FLEET_RUN"] = str(_write_exec(self.bin / "fleet-run", FLEET_RUN_STUB))
        os.environ["STUB_MODE"] = mode

    def real_fleet_run(self) -> None:
        os.environ.pop("FLEET_RUN", None)

    def package(self, name: str = f"{TARGET}-linux-x86_64.zip", **kwargs) -> Path:
        return make_zip(self.build / name, package_entries(**kwargs))

    def smoke(self, *argv: str, host: str = "Linux") -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = package_smoke.main(["--build-dir", str(self.build), *argv], host=host)
        return rc, out.getvalue(), err.getvalue()

    def stub_log(self) -> list[str]:
        return self.log.read_text().splitlines()

    def stub_args(self) -> list[str]:
        return [line[4:] for line in self.stub_log() if line.startswith("arg=")]


class LaunchContractTest(HarnessCase):
    def test_clean_run_passes_with_extracted_root_and_restored_mode(self) -> None:
        self.package()
        self.stub_fleet_run("clean")
        rc, out, _ = self.smoke("--frames", "7")
        self.assertEqual(rc, 0)
        self.assertIn("ir-run: RESULT=CLEAN exe=IRShapeDebug exit=0", out)
        self.assertIn("package-smoke: RESULT=PASS", out)
        args = self.stub_args()
        root = args[args.index("--build-dir") + 1]
        self.assertEqual(args, ["--child-unset", "LD_LIBRARY_PATH", "--build-dir", root,
                                TARGET, "--auto-screenshot", "7"])
        self.assertTrue(Path(root).name.startswith("package-smoke-"))
        self.assertIn(f"extracted={root}", out)
        self.assertIn("exe=executable", self.stub_log())
        self.assertFalse(Path(root).exists(), "the extraction root is removed after the run")

    def test_wrapper_keeps_the_callers_environment(self) -> None:
        self.package()
        self.stub_fleet_run("clean")
        self.smoke()
        log = self.stub_log()
        self.assertIn("wrapper-ld=/wrapper/ld", log)
        self.assertIn(f"wrapper-path={os.environ['PATH']}", log)

    def test_explicit_zip_wins_over_build_dir_selection(self) -> None:
        self.package()
        self.package(f"{TARGET}-linux-aarch64.zip")
        chosen = make_zip(self.tmp / "elsewhere.zip", package_entries())
        self.stub_fleet_run("clean")
        rc, out, _ = self.smoke("--zip", str(chosen))
        self.assertEqual(rc, 0)
        self.assertIn(f"package={chosen.resolve()}", out)

    def test_crash_verdict_fails_and_keeps_the_loader_error(self) -> None:
        self.package()
        self.stub_fleet_run("crash")
        rc, out, _ = self.smoke()
        self.assertEqual(rc, 1)
        self.assertIn("Library not loaded: @rpath/libglfw.3.dylib", out)
        self.assertIn("package-smoke: RESULT=FAIL ir-run verdict=CRASH fleet-run exit=134", out)
        self.assertNotIn("RESULT=PASS", out)

    def test_missing_verdict_fails(self) -> None:
        self.package()
        self.stub_fleet_run("none")
        rc, out, _ = self.smoke()
        self.assertEqual(rc, 1)
        self.assertIn("package-smoke: RESULT=FAIL ir-run verdict=none fleet-run exit=0", out)

    def test_wrapper_error_fails(self) -> None:
        self.package()
        self.stub_fleet_run("wrapper-error")
        rc, out, _ = self.smoke()
        self.assertEqual(rc, 1)
        self.assertIn("task: unbound variable", out)
        self.assertIn("ir-run verdict=none fleet-run exit=1", out)

    def test_only_the_last_verdict_counts(self) -> None:
        self.package()
        self.stub_fleet_run("clean-then-crash")
        rc, out, _ = self.smoke()
        self.assertEqual(rc, 1)
        self.assertIn("ir-run verdict=CRASH", out)


class ArchiveInputTest(HarnessCase):
    def setUp(self) -> None:
        super().setUp()
        self.stub_fleet_run("clean")

    def assert_usage(self, *argv: str, needle: str, host: str = "Linux") -> None:
        rc, out, err = self.smoke(*argv, host=host)
        self.assertEqual(rc, 2)
        self.assertIn(needle, err)
        self.assertFalse(self.log.exists(), "nothing launches on a usage error")
        self.assertNotIn("RESULT=", out)

    def test_missing_archive_exits_2(self) -> None:
        self.assert_usage(needle="expected one IRShapeDebug-linux-*.zip")

    def test_ambiguous_archive_exits_2(self) -> None:
        self.package()
        self.package(f"{TARGET}-linux-aarch64.zip")
        self.assert_usage(needle="expected one IRShapeDebug-linux-*.zip")

    def test_other_host_archive_is_not_selected(self) -> None:
        self.package(f"{TARGET}-macos-arm64.zip")
        self.assert_usage(needle="expected one IRShapeDebug-linux-*.zip")

    def test_nonexistent_zip_flag_exits_2(self) -> None:
        self.assert_usage("--zip", str(self.tmp / "absent.zip"), needle="no package at")

    def test_not_a_zip_exits_2(self) -> None:
        bogus = self.build / f"{TARGET}-linux-x86_64.zip"
        bogus.write_text("not a zip")
        self.assert_usage(needle="not a readable zip")

    def test_wrong_top_level_exits_2(self) -> None:
        make_zip(self.build / f"{TARGET}-linux-x86_64.zip",
                 [("Other/payload", b"x", stat.S_IFREG | 0o755)])
        self.assert_usage(needle="archive top level is ['Other']")

    def test_second_top_level_exits_2(self) -> None:
        make_zip(self.build / f"{TARGET}-linux-x86_64.zip",
                 package_entries() + [("stray.txt", b"x", stat.S_IFREG | 0o644)])
        self.assert_usage(needle="archive top level is")

    def test_missing_executable_exits_2(self) -> None:
        make_zip(self.build / f"{TARGET}-linux-x86_64.zip",
                 [(f"{TARGET}/libdemo.dylib", b"x", stat.S_IFREG | 0o644)])
        self.assert_usage(needle="no IRShapeDebug executable")

    def test_escaping_entry_exits_2(self) -> None:
        make_zip(self.build / f"{TARGET}-linux-x86_64.zip",
                 package_entries() + [(f"{TARGET}/../escape", b"x", stat.S_IFREG | 0o644)])
        self.assert_usage(needle="entries leave the bundle")

    def test_unsupported_host_exits_2(self) -> None:
        self.package()
        self.assert_usage(host="SunOS", needle="unsupported host 'SunOS'")

    def test_missing_fleet_run_exits_2(self) -> None:
        self.package()
        (self.bin / "fleet-run").unlink()
        self.assert_usage(needle="no fleet-run wrapper at")

    def test_default_wrapper_is_this_checkouts_not_paths(self) -> None:
        os.environ.pop("FLEET_RUN", None)
        self.assertEqual(package_smoke.resolve_fleet_run(),
                         (REPO_ROOT / "scripts" / "fleet" / "fleet-run").as_posix())

    def test_nonpositive_frames_is_a_usage_error(self) -> None:
        self.package()
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
            self.smoke("--frames", "0")
        self.assertEqual(caught.exception.code, 2)


class ExtractionTest(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "POSIX modes")
    def test_archived_modes_are_restored(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            zip_path = make_zip(root / "pkg.zip", package_entries())
            extract_root = root / "out"
            extract_root.mkdir()
            bundle = package_smoke.extract_package(zip_path, extract_root, TARGET)
            mode = lambda path: stat.S_IMODE(path.stat().st_mode)  # noqa: E731
            self.assertEqual(mode(bundle / TARGET), 0o755)
            self.assertEqual(mode(bundle / "libdemo.dylib"), 0o444)
            self.assertEqual(mode(bundle / "data" / "config.lua"), 0o644)

    @unittest.skipUnless(os.name == "posix", "POSIX modes")
    def test_plain_extractall_loses_the_executable_bit(self) -> None:
        # The fixture must reproduce the defect the restoration fixes, or the
        # test above passes vacuously.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            zip_path = make_zip(root / "pkg.zip", package_entries())
            with zipfile.ZipFile(zip_path) as archive:
                archive.extractall(root / "plain")
            self.assertFalse(os.access(root / "plain" / TARGET / TARGET, os.X_OK))


class HostOptionsTest(HarnessCase):
    def setUp(self) -> None:
        super().setUp()
        self.stub_fleet_run("clean")

    def test_linux_unsets_ld_library_path(self) -> None:
        self.package()
        self.smoke(host="Linux")
        self.assertEqual(self.stub_args()[:2], ["--child-unset", "LD_LIBRARY_PATH"])

    def test_windows_reduces_path_to_the_system_directories(self) -> None:
        self.package(f"{TARGET}-windows-amd64.zip")
        os.environ["SystemRoot"] = r"C:\Windows"
        rc, _, _ = self.smoke(host="Windows")
        self.assertEqual(rc, 0)
        self.assertEqual(self.stub_args()[:2],
                         ["--child-env", r"PATH=C:\Windows\System32;C:\Windows"])
        self.assertIn(f"wrapper-path={os.environ['PATH']}", self.stub_log())

    def test_windows_without_systemroot_exits_2(self) -> None:
        self.package(f"{TARGET}-windows-amd64.zip")
        os.environ.pop("SystemRoot", None)
        rc, _, err = self.smoke(host="Windows")
        self.assertEqual(rc, 2)
        self.assertIn("SystemRoot is not set", err)

    def test_macos_probe_applied_adds_the_sandbox_prefix_once(self) -> None:
        self.package(f"{TARGET}-macos-arm64.zip")
        rc, out, _ = self.smoke(host="Darwin")
        self.assertEqual(rc, 0)
        self.assertIn("package-smoke: isolation=applied (homebrew-denial profile)", out)
        args = self.stub_args()
        unsets = ["--child-unset", "DYLD_LIBRARY_PATH",
                  "--child-unset", "DYLD_FALLBACK_LIBRARY_PATH",
                  "--child-unset", "DYLD_FALLBACK_FRAMEWORK_PATH"]
        prefix = ["--child-prefix", str(self.sandbox), "--child-prefix", "-p",
                  "--child-prefix", package_smoke.DENIAL_PROFILE]
        self.assertEqual(args[:12], unsets + prefix)
        self.assertEqual(self.log.read_text().count("--child-prefix"), 3)
        probes = [line for line in self.sandbox_log.read_text().splitlines()
                  if line.startswith("sandbox-argv=")]
        self.assertEqual(probes, [f"sandbox-argv=-p {package_smoke.DENIAL_PROFILE} /usr/bin/true"])

    def test_macos_probe_failure_skips_the_prefix(self) -> None:
        _write_exec(self.sandbox, "#!/bin/sh\n"
                    "echo 'sandbox_apply: Operation not permitted' >&2\nexit 71\n")
        self.package(f"{TARGET}-macos-arm64.zip")
        rc, out, _ = self.smoke(host="Darwin")
        self.assertEqual(rc, 0)
        self.assertIn("isolation=skipped", out)
        self.assertIn("sandbox_apply: Operation not permitted", out)
        self.assertNotIn("--child-prefix", self.stub_args())
        self.assertIn("DYLD_LIBRARY_PATH", self.stub_args())

    def test_macos_missing_sandbox_exec_skips_the_prefix(self) -> None:
        self.sandbox.unlink()
        self.package(f"{TARGET}-macos-arm64.zip")
        rc, out, _ = self.smoke(host="Darwin")
        self.assertEqual(rc, 0)
        self.assertIn("isolation=skipped", out)
        self.assertNotIn("--child-prefix", self.stub_args())

    def test_macos_failed_profiled_launch_is_never_retried(self) -> None:
        self.package(f"{TARGET}-macos-arm64.zip")
        self.stub_fleet_run("crash")
        rc, out, _ = self.smoke(host="Darwin")
        self.assertEqual(rc, 1)
        self.assertEqual(out.count("ir-run: RESULT="), 1)
        self.assertIn("--child-prefix", self.stub_args())


class EndToEndTest(HarnessCase):
    """The real fleet-run → ir-run chain launching a packaged shell script."""

    def setUp(self) -> None:
        super().setUp()
        self.real_fleet_run()

    def exe_lines(self, out: str) -> list[str]:
        return [line for line in out.splitlines() if line.startswith("exe-")]

    def test_linux_arm_reaches_the_executable(self) -> None:
        self.package()
        rc, out, _ = self.smoke(host="Linux")
        self.assertEqual(rc, 0, out)
        lines = self.exe_lines(out)
        self.assertIn("exe-ld=unset", lines)
        if DYLD_OBSERVABLE:
            self.assertIn("exe-dyld=/wrapper/dyld", lines)
        self.assertIn("exe-cwd-lib=libdemo.dylib", lines, "runs from the bundle directory")
        self.assertIn("exe-arg=--auto-screenshot", lines)
        self.assertIn("ir-run: child isolation: prefix=none unset=LD_LIBRARY_PATH set=none", out)
        self.assertIn("ir-run: RESULT=CLEAN exe=IRShapeDebug exit=0", out)
        self.assertIn("package-smoke: RESULT=PASS", out)

    def test_macos_arm_puts_the_sandbox_directly_before_the_executable(self) -> None:
        self.package(f"{TARGET}-macos-arm64.zip")
        rc, out, _ = self.smoke(host="Darwin")
        self.assertEqual(rc, 0, out)
        lines = self.exe_lines(out)
        self.assertIn("exe-ld=/wrapper/ld", lines)
        if DYLD_OBSERVABLE:
            self.assertIn("exe-dyld=unset", lines)
            self.assertIn("exe-dyld-fallback=unset", lines)
        sandbox = self.sandbox_log.read_text().splitlines()
        self.assertEqual(sandbox[-2],
                         f"sandbox-argv=-p {package_smoke.DENIAL_PROFILE} "
                         f"./{TARGET} --auto-screenshot 10")
        if DYLD_OBSERVABLE:
            self.assertEqual(sandbox[-1], "sandbox-dyld=unset")
        self.assertIn(f"ir-run: child isolation: prefix={self.sandbox} unset=DYLD_LIBRARY_PATH,"
                      "DYLD_FALLBACK_LIBRARY_PATH,DYLD_FALLBACK_FRAMEWORK_PATH set=none", out)
        self.assertIn("package-smoke: RESULT=PASS", out)

    def test_failing_packaged_executable_fails_the_smoke(self) -> None:
        self.package(exe_body="#!/bin/sh\n"
                     "echo 'error while loading shared libraries: libdemo.so'\nexit 127\n")
        rc, out, _ = self.smoke(host="Linux")
        self.assertEqual(rc, 1)
        self.assertIn("error while loading shared libraries: libdemo.so", out)
        self.assertIn("ir-run: RESULT=CRASH exe=IRShapeDebug exit=127", out)
        self.assertIn("package-smoke: RESULT=FAIL ir-run verdict=CRASH", out)


if __name__ == "__main__":
    unittest.main()
