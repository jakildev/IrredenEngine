"""Pins cull-verify's cross-run contract: relative-only, no committed set (#2955).

``scripts/cull-verify.py`` asserts one thing — at every pose, ``cv_live_i``
matches ``cv_frozen_i`` *from the same run*. It has no bless flag and no
committed reference set, and the reason is spelled out in
``docs/design/cull-validation-harness.md`` "Cross-run contract".

That section is prose. These tests are the same contract encoded in code
(CLAUDE-BASELINE "Encode contracts in code, not in comments"), so a future
"let's commit references for cull-verify" has to delete a test — and read the
rationale — rather than silently re-create an unread reference set.

Hermetic — filenames and argparse only. No engine, no build, no GL/Metal, no
demo launch. Import the dashed-name script via importlib, matching
test_verify_common_full_frames.py.
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent
_REPO = _SCRIPTS.parent
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_cv = _load("cull_verify", "cull-verify.py")

_DOC = 'docs/design/cull-validation-harness.md "Cross-run contract"'


class TestNoBlessFlag(unittest.TestCase):
    """The write half of the contract: there is no way to bless a baseline."""

    def test_update_baselines_flag_is_gone(self):
        # --build-dir <empty tempdir> is load-bearing for the positive control,
        # not for the passing path: argparse rejects the unknown flags before
        # anything expensive runs, but if the flags were ever restored, parsing
        # would succeed and detect_backend would then raise SystemExit("no
        # CMakeCache.txt ...") — a non-2 code, so the test fails — without
        # finding an exe or launching the demo on a pane with a real build.
        with tempfile.TemporaryDirectory() as td:
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                with self.assertRaises(SystemExit) as ctx:
                    _cv.main(["--update-baselines", "--force", "--build-dir", td])
        self.assertEqual(
            ctx.exception.code, 2,
            msg=(f"cull-verify must not accept a bless flag — see {_DOC} (#2955). "
                 f"stderr was: {err.getvalue()!r}"),
        )
        self.assertIn("unrecognized arguments", err.getvalue())


class TestNoCommittedReferenceSet(unittest.TestCase):
    """The read half: no committed frames for a harness that reads none."""

    def test_no_committed_cull_verify_reference_dir(self):
        refs = _REPO / "creations" / "demos" / _cv.DEMO_NAME / "test" / "references"
        found = sorted(str(p.relative_to(_REPO)) for p in refs.glob("*/cull-verify"))
        self.assertEqual(
            found, [],
            msg=(f"cull-verify reads no committed baseline, so committing one "
                 f"re-creates the write-only set #2955 deleted — see {_DOC}. "
                 f"For absolute coverage of a cull pose, add the shot to "
                 f"{_cv.DEMO_NAME}'s render-verify manifest instead. Found: {found}"),
        )


if __name__ == "__main__":
    unittest.main()
