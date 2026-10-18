#!/usr/bin/env python3
"""Hermetic controls for lint_creation_manifests.py."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "lint_creation_manifests.py"


class CreationManifestLintTest(unittest.TestCase):
    def fixture(self) -> Path:
        root = Path(tempfile.mkdtemp())
        for directory in ("creations/demos/valid", "creations/editors/voxel_editor"):
            path = root / directory
            path.mkdir(parents=True)
            (path / "creation.json").write_text(json.dumps({
                "version": 1, "name": "Valid", "description": "A fixture.",
                "group": "test", "tags": [], "kind": "demo",
                "targets": [{"name": "IRValid", "presets": [{"name": "Interactive", "args": []}]}],
            }))
        return root

    def run_lint(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(["python3", str(SCRIPT), "--root", str(root)],
                              text=True, capture_output=True, check=False)

    def test_valid_missing_and_bad_kind(self) -> None:
        root = self.fixture()
        self.assertEqual(self.run_lint(root).returncode, 0)
        (root / "creations/demos/valid/creation.json").unlink()
        missing = self.run_lint(root)
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("valid", missing.stderr)
        path = root / "creations/demos/valid/creation.json"
        path.write_text(json.dumps({
            "version": 1,
            "name": "Bad",
            "description": "Bad.",
            "group": "test",
            "tags": [],
            "kind": "bad",
            "targets": [],
        }))
        bad = self.run_lint(root)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("kind", bad.stderr)

    def test_unknown_build_target(self) -> None:
        root = self.fixture()
        with tempfile.TemporaryDirectory() as temporary:
            bin_dir = Path(temporary)
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


if __name__ == "__main__":
    unittest.main()
