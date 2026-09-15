"""Prevent a matrix from silently combining different runtime artifacts."""

import json
import tempfile
import unittest
from pathlib import Path

from rotation_controls import verify_artifacts


class ArtifactIdentityTest(unittest.TestCase):
    def test_shader_or_binary_change_rejects_matrix(self):
        for changed in ("binary_sha256", "shader_sha256"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = {"binary_sha256": "binary", "shader_sha256": "shaders"}
                for case in ("first", "second"):
                    directory = root / case / "round-1"
                    directory.mkdir(parents=True)
                    (directory / "manifest.json").write_text(json.dumps(manifest))
                verify_artifacts(root)
                manifest[changed] = "different"
                (root / "second/round-1/manifest.json").write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    verify_artifacts(root)

    def test_empty_matrix_is_not_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                verify_artifacts(Path(temporary))


if __name__ == "__main__":
    unittest.main()
