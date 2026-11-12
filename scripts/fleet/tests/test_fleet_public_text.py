"""Positive and negative controls for the public-text checker."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import fleet_public_text  # noqa: E402


class PublicTextCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        subprocess.run(["git", "init", "-q", "-b", "master"], cwd=self.repo, check=True)
        (self.repo / ".gitignore").write_text("creations/hidden/\n", encoding="utf-8")
        tracked = self.repo / "creations" / "tracked"
        tracked.mkdir(parents=True)
        (tracked / "note.txt").write_text("tracked\n", encoding="utf-8")
        subprocess.run(["git", "add", ".gitignore", "creations/tracked/note.txt"],
                       cwd=self.repo, check=True)
        self.repos = {"engine": "example/Public", "other": "secret/FakeRepo"}
        self.terms = self.root / "terms"
        self.terms.write_text("clockwork codename\n", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def find(self, text, target="engine"):
        return fleet_public_text.find_public_text(
            text, target, self.repos, self.terms, self.repo)

    def assertFinding(self, text, kind, line=1):
        found = self.find(text)
        self.assertEqual([(f.kind, f.line) for f in found], [(kind, line)])

    def test_each_private_shape_reports_class_and_line(self):
        cases = {
            "secret/FakeRepo": "foreign-repository",
            "secret/FakeRepo#41": "foreign-repository",
            "https://github.com/secret/FakeRepo/pull/41": "foreign-repository",
            "other#41": "foreign-repository-key",
            "other PR #41": "foreign-repository-key",
            "FakeRepo issue #41": "foreign-repository-key",
            "creations/hidden/design.md": "ignored-creation-path",
            "clockwork codename": "private-term",
        }
        for text, kind in cases.items():
            with self.subTest(text=text):
                self.assertFinding("safe\n" + text, kind, 2)

    def test_public_and_tracked_shapes_are_clean(self):
        text = "example/Public#12\n#123\ncreations/tracked/note.txt\n"
        self.assertEqual(self.find(text), [])

    def test_non_public_target_is_not_checked(self):
        self.assertEqual(self.find("secret/FakeRepo#1", target="other"), [])

    def test_cli_reports_without_echoing_the_line(self):
        repo_map = self.root / "repos.json"
        repo_map.write_text(json.dumps(self.repos), encoding="utf-8")
        body = self.root / "body.txt"
        body.write_text("safe\nother#41\n", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(ROOT / "fleet_public_text.py"),
             "--target-repo", "engine", "--text-file", str(body),
             "--repo-map-file", str(repo_map), "--term-file", str(self.terms),
             "--git-root", str(self.repo)],
            capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "foreign-repository-key line 2\n")
        self.assertEqual(result.stderr, "  other#41\n")


if __name__ == "__main__":
    unittest.main()
