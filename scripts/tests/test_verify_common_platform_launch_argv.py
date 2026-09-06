"""Tests for ``platform_launch_argv`` — the Windows bash-launch rewrite (#3007).

``fleet-run`` / ``fleet-build`` are extensionless bash scripts on PATH, and
native-Windows Python resolves an argv[0] with no extension only by appending
``.exe`` — so every harness in the verify family died ``FileNotFoundError:
[WinError 2]`` before its demo started. ``platform_launch_argv`` routes the
command line through ``bash -lc`` on Windows and is a no-op everywhere else.

Both halves are worth pinning. The no-op branch guards the Linux/macOS hosts
this fix was never meant to touch, so an inverted ``!=`` would break the
platforms that were already working. The Windows branch's per-argument
``shlex.quote`` is the subtler half: a dropped quote reintroduces word-splitting
on any argument containing a space, and it fails *silently* — bash still runs,
the demo just receives a different argv than the harness passed (a ``center
axis`` block name arriving as two arguments, a path truncated at its space).

``platform.system`` / ``shutil.which`` are both mocked, so the suite is hermetic
and gives the same verdict on every host — the Windows branch is exercised on
Linux and the POSIX branch on Windows. No engine, no GL/Metal build.
"""
import shlex
import sys
import unittest
from pathlib import Path
from unittest import mock

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))

import verify_common  # noqa: E402  (needs the sys.path insert above)

# Stand-in for a bash found on PATH — threaded through, never executed.
_FAKE_BASH = r"C:\Program Files\Git\usr\bin\bash.EXE"

# The fallback when bash is absent from PATH. Duplicated from
# platform_launch_argv on purpose: the point of the assertion below is to fail
# if that literal ever drifts.
_FALLBACK_BASH = r"C:\Program Files\Git\bin\bash.exe"

# A representative harness launch — nothing here needs quoting.
_PLAIN_ARGV = ["fleet-run", "IRShapeDebug", "--auto-screenshot", "--zoom", "4"]

# Arguments that word-split or get mangled the moment a quote goes missing.
_HAZARDOUS_ARGV = [
    "fleet-run",
    "IRShapeDebug",
    "--blocks",
    "center axis",                        # space: splits into two arguments
    "--out",
    "C:/Users/evin j/save_files/shots",   # space inside a path
    "--label",
    "a;b$HOME*",                          # shell metacharacters
]


class TestNonWindowsIsNoOp(unittest.TestCase):
    """Linux/macOS must get their argv back byte-identical."""

    def test_linux_returns_argv_unchanged(self):
        with mock.patch("platform.system", return_value="Linux"):
            self.assertEqual(
                verify_common.platform_launch_argv(_PLAIN_ARGV), _PLAIN_ARGV)

    def test_darwin_returns_argv_unchanged(self):
        with mock.patch("platform.system", return_value="Darwin"):
            self.assertEqual(
                verify_common.platform_launch_argv(_PLAIN_ARGV), _PLAIN_ARGV)

    def test_non_windows_never_wraps(self):
        """The inverted-conditional lock: a flipped ``!=`` surfaces here."""
        for system in ("Linux", "Darwin"):
            with self.subTest(system=system), \
                 mock.patch("platform.system", return_value=system):
                got = verify_common.platform_launch_argv(_HAZARDOUS_ARGV)
                self.assertEqual(got, _HAZARDOUS_ARGV)
                self.assertNotIn("-lc", got)


class TestWindowsWrapsThroughBash(unittest.TestCase):

    def _wrap(self, argv, which_result=_FAKE_BASH):
        with mock.patch("platform.system", return_value="Windows"), \
             mock.patch("shutil.which", return_value=which_result):
            return verify_common.platform_launch_argv(argv)

    def test_shape_is_bash_dash_lc_plus_one_command_string(self):
        got = self._wrap(_PLAIN_ARGV)
        self.assertEqual(len(got), 3)
        self.assertEqual(got[0], _FAKE_BASH)
        self.assertEqual(got[1], "-lc")
        # Nothing in _PLAIN_ARGV needs quoting, so the command string is the
        # plain join — the un-mangled baseline the hazardous case departs from.
        self.assertEqual(got[2], " ".join(_PLAIN_ARGV))

    def test_quoting_round_trips_through_shlex_split(self):
        """The actual risk surface: bash must recover the original argv."""
        got = self._wrap(_HAZARDOUS_ARGV)
        self.assertEqual(shlex.split(got[2]), _HAZARDOUS_ARGV)

    def test_a_bare_join_would_not_round_trip(self):
        """Positive control: without the quoting, the test above would pass
        vacuously only if word-splitting were harmless here. It isn't."""
        naive = " ".join(_HAZARDOUS_ARGV)
        self.assertNotEqual(self._wrap(_HAZARDOUS_ARGV)[2], naive)
        self.assertNotEqual(shlex.split(naive), _HAZARDOUS_ARGV)

    def test_uses_bash_found_on_path(self):
        got = self._wrap(_PLAIN_ARGV)
        self.assertEqual(got[0], _FAKE_BASH)

    def test_falls_back_to_git_bash_when_not_on_path(self):
        got = self._wrap(_PLAIN_ARGV, which_result=None)
        self.assertEqual(got[0], _FALLBACK_BASH)
        self.assertEqual(got[1], "-lc")
        self.assertEqual(shlex.split(got[2]), _PLAIN_ARGV)


if __name__ == "__main__":
    unittest.main()
