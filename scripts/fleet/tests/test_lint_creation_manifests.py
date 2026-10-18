#!/usr/bin/env python3
"""Hermetic controls for lint_creation_manifests.py."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "lint_creation_manifests.py"
CORPUS = Path(__file__).resolve().parents[3] / "test/asset/fixtures/creation_manifest"
TRACKED_DOC = "/".join(("docs", "design", "voxel-and-sdf-rendering.md"))


class CreationManifestLintTest(unittest.TestCase):
    def fixture(self, root: Path) -> None:
        for directory in ("creations/demos/valid", "creations/editors/voxel_editor"):
            path = root / directory
            path.mkdir(parents=True)
            (path / "creation.json").write_text(
                json.dumps(
                    {
                        "version": 1,
                        "name": "Valid",
                        "description": "A fixture.",
                        "group": "test",
                        "tags": [],
                        "kind": "demo",
                        "targets": [
                            {"name": "IRValid", "presets": [{"name": "Interactive", "args": []}]}
                        ],
                    }
                )
            )
        (root / TRACKED_DOC).parent.mkdir(parents=True)
        (root / TRACKED_DOC).write_text("tracked\n")
        thumbnail = root / "creations/demos/valid/thumbnail.png"
        thumbnail.write_bytes(b"fixture")
        subprocess.run(["git", "init", "--quiet", str(root)], check=True)
        subprocess.run(["git", "-C", str(root), "add", "."], check=True)

    def run_lint(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(SCRIPT), "--root", str(root)],
            text=True,
            capture_output=True,
            check=False,
        )

    def test_valid_missing_and_bad_kind(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            self.assertEqual(self.run_lint(root).returncode, 0)
            (root / "creations/demos/valid/creation.json").unlink()
            missing = self.run_lint(root)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("valid", missing.stderr)
            path = root / "creations/demos/valid/creation.json"
            path.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "name": "Bad",
                        "description": "Bad.",
                        "group": "test",
                        "tags": [],
                        "kind": "bad",
                        "targets": [],
                    }
                )
            )
            bad = self.run_lint(root)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("kind", bad.stderr)

    def test_unknown_build_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "root"
            root.mkdir()
            self.fixture(root)
            bin_dir = Path(temporary) / "bin"
            bin_dir.mkdir()
            cmake = bin_dir / "cmake"
            cmake.write_text("#!/bin/sh\nprintf '%s\\n' '... IROther'\n")
            cmake.chmod(0o755)
            environment = os.environ | {"PATH": f"{bin_dir}:{os.environ['PATH']}"}
            result = subprocess.run(
                ["python3", str(SCRIPT), "--root", str(root), "--build-dir", "."],
                text=True,
                capture_output=True,
                check=False,
                env=environment,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unknown build target: IRValid", result.stderr)

    def test_shared_manifest_corpus(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            manifest = root / "creations/demos/valid/creation.json"
            for fixture in sorted((CORPUS / "invalid").glob("*.json")):
                with self.subTest(fixture=fixture.name):
                    manifest.write_text(fixture.read_text())
                    result = self.run_lint(root)
                    self.assertNotEqual(result.returncode, 0)
                    expected_key = fixture.stem.split("__", maxsplit=1)[0]
                    self.assertIn(expected_key, result.stderr)

            manifest.write_text((CORPUS / "accepted_target_names.json").read_text())
            self.assertEqual(self.run_lint(root).returncode, 0)

    def test_tracked_paths_use_their_own_bases(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            manifest = root / "creations/demos/valid/creation.json"
            data = json.loads(manifest.read_text())
            data["thumbnail"] = "thumbnail.png"
            data["docs"] = [TRACKED_DOC]
            manifest.write_text(json.dumps(data))
            self.assertEqual(self.run_lint(root).returncode, 0)

            data["thumbnail"] = TRACKED_DOC
            manifest.write_text(json.dumps(data))
            wrong_base = self.run_lint(root)
            self.assertNotEqual(wrong_base.returncode, 0)
            self.assertIn("thumbnail", wrong_base.stderr)

            data["thumbnail"] = "thumbnail.png"
            data["docs"] = ["docs/untracked.md"]
            (root / "docs/untracked.md").write_text("untracked\n")
            manifest.write_text(json.dumps(data))
            result = self.run_lint(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("docs[0]", result.stderr)
            self.assertIn("tracked file", result.stderr)


if __name__ == "__main__":
    unittest.main()
