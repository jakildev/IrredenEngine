import json
import tempfile
import unittest
from pathlib import Path

from grid_capacity_controls import (
    TICKS_PER_REVOLUTION,
    cases,
    common_args,
    verify_builds,
)
from repeat_profile import canvas_profile_frames, static_canvas_frame_sweep


def write_manifest(output: Path, case: str, round_index: int, binary: str, build_type: str):
    path = output / case / f"round-{round_index}"
    path.mkdir(parents=True)
    (path / "manifest.json").write_text(json.dumps({
        "binary_sha256": binary, "shader_sha256": "shaders", "host_power": "AC Power",
        "build_type": build_type,
    }))


class GridCapacityControlsTest(unittest.TestCase):
    def test_cases_differ_only_by_build_and_fixture_flag(self):
        selected = cases()
        self.assertEqual(sorted(selected),
                         ["head-cube", "head-plate", "parent-cube", "parent-plate"])
        for name, flags in selected.items():
            self.assertEqual(flags, ["--focus-exact-fit-plate"] if name.endswith("plate") else [])

    def test_common_arguments_are_a_capture_free_spinning_profile(self):
        common = common_args(2000)
        self.assertEqual(canvas_profile_frames(common), 2000)
        self.assertNotIn("--no-spin", common)
        self.assertTrue(static_canvas_frame_sweep(common))
        self.assertGreaterEqual(2000 * 3 // 4 // 2, TICKS_PER_REVOLUTION)

    def test_builds_must_be_distinct_release_trees_within_each_side(self):
        selected = cases()
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for name in selected:
                side = name.split("-")[0]
                for round_index in (1, 2):
                    write_manifest(output, name, round_index, side, "Release")
            identities = verify_builds(output, selected)
            self.assertEqual(identities["head"]["binary"], "head")

            (output / "head-cube/round-2/manifest.json").write_text(json.dumps({
                "binary_sha256": "rebuilt", "shader_sha256": "shaders",
                "host_power": "AC Power", "build_type": "Release"}))
            with self.assertRaisesRegex(ValueError, "head: runs disagree"):
                verify_builds(output, selected)

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for name in selected:
                write_manifest(output, name, 1, name.split("-")[0], "Debug")
            with self.assertRaisesRegex(ValueError, "not a Release tree"):
                verify_builds(output, selected)

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for name in selected:
                write_manifest(output, name, 1, "same", "Release")
            with self.assertRaisesRegex(ValueError, "same binary"):
                verify_builds(output, selected)


if __name__ == "__main__":
    unittest.main()
