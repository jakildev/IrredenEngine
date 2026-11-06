import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from repeat_profile import shape_profile_arguments
from sdf_controls import cases, main, runner_options, summarize_memory, verify_configurations


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
                                             "configs/perf/sdf-on.lua": "on",
                                             "--sdf-voxel-pose": f"pose-{index}"},
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

    def test_release_selects_the_release_runner_without_an_environment_prefix(self):
        self.assertEqual(("--target", "IRShapeDebug"), runner_options(False))
        self.assertEqual(
            ("--target", "IRShapeDebug", "--release"), runner_options(True)
        )
        self.assertEqual(
            ("--target", "IRShapeDebug", "--release", "--allow-unsupported-gpu-frame"),
            runner_options(True, allow_unsupported_gpu_frame=True),
        )

    def test_unsupported_gpu_frame_is_accepted_only_for_timing_off_matrices(self):
        for case, accepted in (
            ("density-baked-1-timing-off", True),
            ("density-baked-1-timing-on", False),
        ):
            with (
                self.subTest(case=case),
                tempfile.TemporaryDirectory() as temporary,
                patch("sys.argv", ["sdf_controls.py", "--output", str(Path(temporary) / "out"),
                                   "--allow-unsupported-gpu-frame", "--case", case]),
                patch("sdf_controls.run_rounds") as run_rounds,
                patch("sys.stderr", new_callable=io.StringIO) as stderr,
            ):
                if accepted:
                    self.assertEqual(main(), 0)
                    self.assertIn("--allow-unsupported-gpu-frame",
                                  run_rounds.call_args.kwargs["runner_options"])
                else:
                    with self.assertRaises(SystemExit):
                        main()
                    self.assertIn("requires every case to be timing-off", stderr.getvalue())
                    run_rounds.assert_not_called()

    def test_memory_summary_distinguishes_process_and_driver_measurements(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            path = output / "density-baked-2-timing-off/round-1"
            path.mkdir(parents=True)
            (path / "manifest.json").write_text(json.dumps({"runs": [{
                "sdf_occupancy": {
                    "bake_us": 42,
                    "asset_bytes": 300,
                    "bake_scratch_peak_bytes": 100,
                    "bake_payload_peak_bytes": 400,
                    "process_before_bytes": 1000,
                    "process_after_bake_bytes": 1400,
                },
                "sdf_residency": {
                    "process_final_bytes": 1700,
                    "process_peak_bytes": 1800,
                    "gpu_process_allocated_bytes": 1900,
                    "gpu_device_capacity_bytes": 10000,
                    "gpu_device_available_bytes": 7000,
                },
            }]}))
            selected = {"density-baked-2-timing-off": []}
            summarize_memory(output, selected)
            summary = (output / "memory-summary.md").read_text()
            self.assertIn("| 400.0 | 700.0 | 800.0 | 1900.0 | 3000.0 |", summary)
            self.assertIn("not process residency", summary)


if __name__ == "__main__":
    unittest.main()
