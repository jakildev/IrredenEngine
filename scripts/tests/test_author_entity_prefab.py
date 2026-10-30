"""Tests for author-entity.py's --prefab form: what it collects from the
editor's save directory and what it writes to the asset directory."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

_SPEC = importlib.util.spec_from_file_location(
    "author_entity", SCRIPTS_DIR / "author-entity.py"
)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("could not load author-entity.py")
author_entity = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(author_entity)

MANIFEST = """return {
  prefab_version = 2,
  parts = {
    {
      id = "part_0",
      voxel_ref = "data/editor_scene/scene_part_0.vxs",
      anchor = 1,
    },
    {
      id = "part_1",
      shape = { type = 1 },
    },
    {
      id = "part_2",
      voxel_ref = "data/editor_scene/scene_part_2.vxs",
    },
  },
}
"""


def _write_save(save_dir: Path, manifest: str = MANIFEST) -> None:
    save_dir.mkdir(parents=True, exist_ok=True)
    (save_dir / "scene.prefab.lua").write_text(manifest, encoding="utf-8")
    for part in (0, 2):
        (save_dir / f"scene_part_{part}.vxs").write_bytes(bytes([part]) * 4)
        (save_dir / f"scene_part_{part}.vxs.json").write_text(f'{{"part": {part}}}')


class PrefabFilesTest(unittest.TestCase):
    def test_collects_manifest_first_then_parts_and_sidecars(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            save_dir = Path(tmp)
            _write_save(save_dir)
            (save_dir / "scene_frame_0.vxs").write_bytes(b"frame")

            names = [p.name for p in author_entity.prefab_files(save_dir)]

        self.assertEqual(
            names,
            [
                "scene.prefab.lua",
                "scene_part_0.vxs",
                "scene_part_0.vxs.json",
                "scene_part_2.vxs",
                "scene_part_2.vxs.json",
            ],
        )

    def test_parts_without_a_manifest_are_not_a_save(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            save_dir = Path(tmp)
            _write_save(save_dir)
            (save_dir / "scene.prefab.lua").unlink()

            self.assertEqual(author_entity.prefab_files(save_dir), [])

    def test_clear_removes_the_save_and_keeps_frames(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            save_dir = Path(tmp)
            _write_save(save_dir)
            (save_dir / "scene_frame_0.vxs").write_bytes(b"frame")

            author_entity.clear_prefab_saves(save_dir)

            self.assertEqual([p.name for p in save_dir.iterdir()], ["scene_frame_0.vxs"])


class CommitPrefabTest(unittest.TestCase):
    def _commit(self, tmp: str, part_names: list[str] | None, manifest: str = MANIFEST):
        save_dir = Path(tmp) / "save"
        _write_save(save_dir, manifest)
        asset_dir = Path(tmp) / "out"
        written = author_entity.commit_prefab(
            author_entity.prefab_files(save_dir),
            "proof",
            asset_dir,
            Path("assets") / "voxel" / "entities",
            part_names,
        )
        return asset_dir / "proof", written

    def test_default_names_are_the_part_ids(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir, written = self._commit(tmp, None)

            self.assertEqual(
                sorted(p.name for p in written),
                [
                    "part_0.vxs",
                    "part_0.vxs.json",
                    "part_2.vxs",
                    "part_2.vxs.json",
                    "proof.prefab.lua",
                ],
            )
            manifest = (out_dir / "proof.prefab.lua").read_text(encoding="utf-8")
            self.assertIn('voxel_ref = "assets/voxel/entities/proof/part_0.vxs"', manifest)
            self.assertIn('voxel_ref = "assets/voxel/entities/proof/part_2.vxs"', manifest)
            self.assertNotIn("data/editor_scene", manifest)
            self.assertEqual((out_dir / "part_2.vxs").read_bytes(), bytes([2]) * 4)
            self.assertEqual((out_dir / "part_2.vxs.json").read_text(), '{"part": 2}')

    def test_part_names_rename_in_manifest_order(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir, _ = self._commit(tmp, ["stem", "frame"])

            manifest = (out_dir / "proof.prefab.lua").read_text(encoding="utf-8")
            self.assertIn('voxel_ref = "assets/voxel/entities/proof/stem.vxs"', manifest)
            self.assertIn('voxel_ref = "assets/voxel/entities/proof/frame.vxs"', manifest)
            self.assertEqual((out_dir / "stem.vxs").read_bytes(), bytes([0]) * 4)
            self.assertEqual((out_dir / "frame.vxs").read_bytes(), bytes([2]) * 4)
            self.assertIn('id = "part_2"', manifest)

    def test_everything_but_the_refs_is_copied_verbatim(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir, _ = self._commit(tmp, None)

            manifest = (out_dir / "proof.prefab.lua").read_text(encoding="utf-8")
            restored = manifest.replace(
                "assets/voxel/entities/proof/part_", "data/editor_scene/scene_part_"
            )
            self.assertEqual(restored, MANIFEST)

    def test_a_name_count_that_misses_the_voxel_parts_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as raised:
                self._commit(tmp, ["stem", "blob", "frame"])
            self.assertIn("3 name(s) for 2 voxel part(s)", str(raised.exception))

    def test_a_ref_to_an_unwritten_part_is_refused(self) -> None:
        manifest = MANIFEST.replace("scene_part_2.vxs", "scene_part_7.vxs")
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as raised:
                self._commit(tmp, None, manifest)
            self.assertIn("scene_part_7.vxs", str(raised.exception))

    def test_a_part_named_twice_is_refused(self) -> None:
        manifest = MANIFEST.replace("scene_part_2.vxs", "scene_part_0.vxs")
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as raised:
                self._commit(tmp, None, manifest)
            self.assertIn("twice", str(raised.exception))


class AuthorArgsTest(unittest.TestCase):
    RECORDED = {"module": "test/module", "part_names": ["stem", "frame"]}

    def test_a_prefab_never_authored_records_nothing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(author_entity.load_author_args(Path(tmp) / "proof"), {})

    def test_written_arguments_load_back(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = author_entity.write_author_args(Path(tmp), self.RECORDED)

            self.assertEqual(path.name, "author.json")
            self.assertEqual(author_entity.load_author_args(Path(tmp)), self.RECORDED)

    def test_writing_the_same_arguments_twice_is_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            first = author_entity.write_author_args(Path(tmp), self.RECORDED).read_bytes()
            reordered = dict(reversed(list(self.RECORDED.items())))
            second = author_entity.write_author_args(Path(tmp), reordered).read_bytes()

        self.assertEqual(first, second)

    def test_an_unknown_recorded_argument_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "author.json").write_text('{"modul": "test/module"}')
            with self.assertRaises(SystemExit) as raised:
                author_entity.load_author_args(Path(tmp))
        self.assertIn("modul", str(raised.exception))

    def test_bare_command_takes_every_recorded_argument(self) -> None:
        given = dict.fromkeys(author_entity.AUTHOR_ARG_KEYS)

        self.assertEqual(author_entity.merge_author_args(given, self.RECORDED), self.RECORDED)

    def test_a_given_argument_wins_and_the_rest_stay_recorded(self) -> None:
        given = dict.fromkeys(author_entity.AUTHOR_ARG_KEYS)
        given["module"] = "other/module"

        merged = author_entity.merge_author_args(given, self.RECORDED)

        self.assertEqual(merged, {"module": "other/module", "part_names": ["stem", "frame"]})

    def test_naming_a_session_drops_the_recorded_module_session(self) -> None:
        given = dict.fromkeys(author_entity.AUTHOR_ARG_KEYS)
        given["session"] = "rock"
        recorded = {"module": "test/module", "module_session": "test_parts"}

        merged = author_entity.merge_author_args(given, recorded)

        self.assertEqual(merged, {"module": "test/module", "session": "rock"})


class ResolveModuleDirTest(unittest.TestCase):
    def test_a_worktree_directory_is_passed_absolute(self) -> None:
        module = "creations/editors/voxel_editor/test/module"

        resolved = author_entity.resolve_module_dir(module)

        self.assertTrue(Path(resolved).is_absolute())
        self.assertTrue(resolved.endswith(module))

    def test_a_name_that_is_no_worktree_directory_is_passed_as_given(self) -> None:
        self.assertEqual(author_entity.resolve_module_dir("test/module"), "test/module")


if __name__ == "__main__":
    unittest.main()
