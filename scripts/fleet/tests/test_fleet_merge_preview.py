"""Hermetic tests for fleet-merge-preview, the role-allowed read-only form of
the auto-merge executor: it reaches the executor only as `--dry-run --repo
<validated value>`, whatever arguments it is handed."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUBJECT = ROOT / "fleet-merge-preview"
if not SUBJECT.is_file():
    print("SKIP: fleet-merge-preview subject absent", file=sys.stderr)
    sys.exit(3)

EXECUTOR_STUB = r'''import json, os, sys
with open(os.environ["PREVIEW_STUB_LOG"], "a", encoding="utf-8") as f:
    f.write(json.dumps(sys.argv[1:]) + "\n")
sys.exit(int(os.environ.get("PREVIEW_STUB_RC", "0")))
'''


class Preview(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        shutil.copy(SUBJECT, self.dir / "fleet-merge-preview")
        (self.dir / "fleet-automerge").write_text(EXECUTOR_STUB, encoding="utf-8")
        self.log = self.dir / "argv.log"

    def tearDown(self):
        self.temp.cleanup()

    def run_preview(self, *args, rc="0"):
        env = {"PATH": "", "PREVIEW_STUB_LOG": str(self.log), "PREVIEW_STUB_RC": rc,
               "SYSTEMROOT": os.environ.get("SYSTEMROOT", "")}
        return subprocess.run([sys.executable, str(self.dir / "fleet-merge-preview"), *args],
                              capture_output=True, text=True, timeout=60, env=env, check=False)

    def calls(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()]

    def test_executor_lives_beside_the_preview(self):
        self.assertTrue((ROOT / "fleet-automerge").is_file())

    def test_always_dry_run(self):
        for args, repo in (((), "engine"), (("--repo", "game"), "game"),
                           (("--repo", "owner/name"), "owner/name"),
                           (("--repo=engine",), "engine")):
            with self.subTest(args=args):
                result = self.run_preview(*args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.calls()[-1], ["--dry-run", "--repo", repo])

    def test_mutating_arguments_never_reach_the_executor(self):
        for args in (("--auto",), ("--reconcile",), ("--repo", "engine", "--auto"),
                     ("--dry-run",), ("--repo=--auto",), ("--repo", "engine --auto"),
                     ("--repo", "engine;true"), ("--repo", "../x/y"), ("engine",)):
            with self.subTest(args=args):
                result = self.run_preview(*args)
                self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(self.calls(), [])

    def test_executor_exit_code_propagates(self):
        self.assertEqual(self.run_preview(rc="1").returncode, 1)


if __name__ == "__main__":
    unittest.main()
