import json
import tempfile
import unittest
from pathlib import Path

from repeat_profile import shape_profile_arguments
from sdf_controls import cases, verify_configurations


class SdfControlsTest(unittest.TestCase):
    def test_each_pair_changes_only_its_representation(self):
        selected = cases()
        self.assertEqual(len(selected), 16)
        common = ["--auto-profile", "300", "--zoom", "4", "--sdf-voxel-blockers", "--no-ao"]
        for name, args in selected.items():
            shape_profile_arguments([*common, *args])
            if name.startswith("direct-"):
                baked = selected[name.replace("direct-", "baked-", 1)]
                self.assertEqual(["--sdf-voxel-scene", "baked", *args[2:]], baked)
            self.assertEqual("--no-shadows" in args, "shadows-off" in name)

    def test_qualification_matrix_crosses_timing_without_conflating_controls(self):
        selected = cases("qualification")
        self.assertEqual(len(selected), 56)
        for name, args in selected.items():
            shape_profile_arguments([
                "--auto-profile", "300", "--zoom", "4", "--sdf-voxel-blockers",
                "--no-ao", *args,
            ])
            timing = "on" if name.endswith("timing-on") else "off"
            self.assertIn(f"configs/perf/sdf-{timing}.lua", args)
        self.assertIn("--sdf-voxel-density", selected["density-baked-2-timing-off"])
        self.assertIn("--sdf-voxel-overlap",
                      selected["population-baked-16-overlap1-timing-on"])
        self.assertIn("--sdf-voxel-motion", selected["motion-direct-root-timing-off"])
        self.assertIn("revoxelized", selected["local-light-only-revoxelized-timing-on"])

    def test_runtime_configuration_drift_between_rounds_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for index in (1, 2):
                path = output / f"arm/round-{index}"
                path.mkdir(parents=True)
                (path / "manifest.json").write_text(json.dumps({
                    "configuration_sha256": {"scripts/config.lua": "base",
                                             "configs/perf/sdf-on.lua": "on"},
                }))
            verify_configurations(output)
            manifest = output / "arm/round-2/manifest.json"
            manifest.write_text(json.dumps({"configuration_sha256": {
                "scripts/config.lua": "base", "configs/perf/sdf-on.lua": "changed",
            }}))
            with self.assertRaisesRegex(ValueError, "configuration changed"):
                verify_configurations(output)
            manifest.write_text("{}")
            with self.assertRaisesRegex(ValueError, "no configuration fingerprint"):
                verify_configurations(output)


if __name__ == "__main__":
    unittest.main()
