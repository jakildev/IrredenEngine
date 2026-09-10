"""Git-path and documented PR-command regressions; no network or model calls."""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fleet_codex as codex


class Paths(unittest.TestCase):
    def test_git_directories_preserve_spaces_and_reject_incomplete_output(self):
        with tempfile.TemporaryDirectory(prefix="fleet path with spaces ") as temp:
            root = Path(temp).resolve()
            common = root / "repository/.git"
            gitdir = common / "worktrees/pool-1"
            with patch.object(codex.subprocess, "run",
                              return_value=Mock(stdout=f"{gitdir}\r\n{common}\r\n")):
                self.assertEqual(codex._git_dirs(root), (str(gitdir), str(common)))
            for output in ("", f"{gitdir}\n", f"{gitdir}\n\n"):
                with self.subTest(output=output), patch.object(codex.subprocess, "run",
                                                              return_value=Mock(stdout=output)):
                    with self.assertRaisesRegex(ValueError, "expected worktree"):
                        codex._git_dirs(root)

    def test_downstream_twin_has_both_git_directories(self):
        with tempfile.TemporaryDirectory(prefix="fleet twin ") as temp:
            root = Path(temp).resolve()
            worktree = root / "repo/.claude/worktrees/pool-1"
            common = root / "repo/.git"
            twin = common.parent / "creations/game/.claude/worktrees/pool-1"
            twin.mkdir(parents=True)
            twin_common = common.parent / "creations/game/.git"
            outputs = [Mock(stdout=f"{common}/worktrees/pool-1\n{common}\n"),
                       Mock(stdout=f"{twin_common}/worktrees/pool-1\n{twin_common}\n")]
            with patch.object(codex.Path, "home", return_value=root), \
                    patch.object(codex.subprocess, "run", side_effect=outputs), \
                    patch.dict(codex.os.environ, {}, clear=True):
                roots = codex.writable_roots(worktree, root / "state")
            for path in (twin, twin_common, twin_common / "worktrees/pool-1"):
                self.assertIn(str(path), roots)
            self.assertIn(str(common.parent / "build-game-pool-1"), roots)
            self.assertNotIn(str(twin_common.parent), roots)

    def test_stackable_example_reconciles_provenance_with_valid_edit_flag(self):
        root = Path(__file__).resolve().parents[3]
        doc = root / ".claude/skills/commit-and-push/procedures/stackable-on.md"
        snippet = doc.read_text().split("## Open (or reconcile)", 1)[1].split("```bash\n")[1]
        snippet = snippet.split("```", 1)[0].replace("<claude|codex>", "codex")
        # Execute the documented existing-PR branch with a flag-validating CLI
        # stub: `gh pr edit` accepts --add-label, whereas create uses --label.
        script = '''
git() { printf 'codex/example\\n'; }
gh() {
    if [[ "$1 $2" == "pr list" ]]; then printf 'https://example.test/pull/1\\n'; return; fi
    [[ "$1 $2" == "pr edit" ]] || return 10
    shift 3
    [[ "$*" == "--base master --add-label fleet:author-codex" ]] || return 11
    printf 'provenance reconciled\\n'
}
base=master
'''
        result = subprocess.run([shutil.which("bash") or "bash", "-c", script + snippet],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("provenance reconciled", result.stdout)


if __name__ == "__main__":
    unittest.main()
