"""Unit + integration tests for lint_state_mtime.py (#2267).

The lint is a best-effort co-occurrence ratchet: it flags a file that both reads
`.st_mtime` AND references the scout cache (`state.json` / `STATE_JSON` /
`STATE_FILE`), unless the line carries an inline `# lint: state-mtime-ok`
opt-out. These cases lock the contract:

  - positive: `.st_mtime` + a state token, unsuppressed          -> flagged
  - display-negative: `.st_mtime`, no state token                -> clean
  - sort-negative: `.st_mtime` sort key, no state token          -> clean
  - bare `mtime` parameter (state_age_seconds arg)               -> clean
  - suppressed trailing and suppressed line-above                -> clean
  - `st_mtime_ns` variant                                        -> flagged
  - extension-less bash-with-embedded-Python (fleet-dispatcher)  -> flagged
  - tests/ and __pycache__/ subtrees are pruned from the walk
  - non-Python extensions (.md/.sh) are skipped
  - the committed scripts/fleet/ tree is green (both offenders suppressed)

stdlib-only; every fixture is written under a TemporaryDirectory (no network,
no repo mutation).
"""
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import lint_state_mtime as lint

_FLEET_DIR = Path(__file__).resolve().parent.parent


def _run_main(root):
    # Exercise the CLI entry point, swallowing its report; return the exit code.
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        return lint.main(["lint_state_mtime.py", str(root)])


class TmpTreeTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, rel, body):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body, encoding="utf-8")
        return path


class ScanFile(TmpTreeTest):
    def test_positive_state_token_and_mtime_is_flagged(self):
        path = self.write("consumer.py",
                          "STATE_FILE = STATE / 'state.json'\n"
                          "age = time.time() - STATE_FILE.stat().st_mtime\n")
        self.assertEqual(lint.scan_file(path), [2])

    def test_display_mtime_without_state_token_is_clean(self):
        path = self.write("show.py",
                          "def fmt(path):\n"
                          "    return time.localtime(path.stat().st_mtime)\n")
        self.assertEqual(lint.scan_file(path), [])

    def test_sort_mtime_without_state_token_is_clean(self):
        path = self.write("sorter.py", "prs.sort(key=lambda p: p.stat().st_mtime)\n")
        self.assertEqual(lint.scan_file(path), [])

    def test_bare_mtime_parameter_is_not_matched(self):
        # state_age_seconds(gen, mtime) takes mtime as a plain parameter — the
        # dot-anchored regex must not flag it even beside a state token.
        path = self.write("helper.py",
                          "STATE_JSON = 'state.json'\n"
                          "def age(gen, mtime):\n"
                          "    return state_age_seconds(gen, mtime)\n")
        self.assertEqual(lint.scan_file(path), [])

    def test_trailing_suppression_is_clean(self):
        path = self.write("ok_trailing.py",
                          "STATE_JSON = 'state.json'\n"
                          "m = p.stat().st_mtime  # lint: state-mtime-ok legit (#2267)\n")
        self.assertEqual(lint.scan_file(path), [])

    def test_line_above_suppression_is_clean(self):
        path = self.write("ok_above.py",
                          "STATE_FILE = 'state.json'\n"
                          "# lint: state-mtime-ok legit (#2267)\n"
                          "m = STATE_FILE.stat().st_mtime\n")
        self.assertEqual(lint.scan_file(path), [])

    def test_st_mtime_ns_variant_is_flagged(self):
        path = self.write("ns.py",
                          "STATE_FILE = 'state.json'\n"
                          "snap = STATE_FILE.stat().st_mtime_ns\n")
        self.assertEqual(lint.scan_file(path), [2])


class IterFilesAndMain(TmpTreeTest):
    def test_extensionless_bash_with_embedded_python_is_caught(self):
        # Mirrors fleet-dispatcher: a bash shebang wrapping an embedded-Python
        # heredoc. Selection must NOT be shebang-gated or this offender escapes.
        self.write("fleet-dispatcher-like",
                   "#!/usr/bin/env bash\n"
                   "python3 - <<'PY'\n"
                   "STATE_FILE = STATE / 'state.json'\n"
                   "age = time.time() - STATE_FILE.stat().st_mtime\n"
                   "PY\n")
        self.assertEqual(_run_main(self.root), 1)

    def test_tests_and_pycache_dirs_are_pruned(self):
        self.write("tests/test_fixture.py",
                   "STATE_FILE = 'state.json'\n"
                   "m = STATE_FILE.stat().st_mtime\n")
        self.write("__pycache__/cached.py",
                   "STATE_FILE = 'state.json'\n"
                   "m = STATE_FILE.stat().st_mtime\n")
        self.assertEqual([p.name for p in lint.iter_files([self.root])], [])
        self.assertEqual(_run_main(self.root), 0)

    def test_non_python_extension_is_skipped(self):
        # A .md carrying both tokens as prose must not be scanned.
        self.write("notes.md", "reads state.json via p.stat().st_mtime\n")
        self.assertEqual([p.name for p in lint.iter_files([self.root])], [])

    def test_clean_tree_exits_zero(self):
        self.write("fine.py", "m = p.stat().st_mtime  # display only, no state token\n")
        self.assertEqual(_run_main(self.root), 0)


class CommittedTree(unittest.TestCase):
    def test_committed_fleet_tree_is_green(self):
        # Acceptance: both real offenders (fleet-dispatcher, fleet-epic-status)
        # carry the opt-out, so the ratchet passes on the committed tree.
        self.assertEqual(_run_main(_FLEET_DIR), 0)


if __name__ == "__main__":
    unittest.main()
