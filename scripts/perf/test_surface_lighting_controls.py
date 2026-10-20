import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from surface_lighting_controls import cases, main, verify_routes


class SurfaceLightingControlsTest(unittest.TestCase):
    def test_runner_enables_surface_lighting_for_every_control(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "matrix"
            with patch("sys.argv", ["controls", "--output", str(output)]), \
                    patch("surface_lighting_controls.run_rounds") as run:
                self.assertEqual(main(), 0)
            environments = run.call_args.kwargs["environments"]
            common = run.call_args.args[2]
            self.assertIn("--profile-frames", common)
            self.assertEqual(common[common.index("--profile-frames") + 1], "360")
            self.assertNotIn("--auto-screenshot", common)
            self.assertNotIn("--sweep-frames", common)
            self.assertEqual(set(environments), set(cases()))
            for environment in environments.values():
                self.assertEqual(environment["IR_PERAXIS_SURFACE_LIGHTING"], "1")

    def test_paired_controls_keep_geometry_and_camera(self):
        selected = cases()
        self.assertEqual(len(selected), 20)
        for route in ("cardinal", "per-axis"):
            base = selected[f"{route}-zoom4-overlap16"]
            self.assertEqual(selected[f"{route}-zoom4-overlap16-no-shadows"],
                             [*base, "--no-shadows"])
            self.assertEqual(selected[f"{route}-zoom4-overlap16-unlit"],
                             [*base, "--debug-overlay", "unlit"])
            for zoom in (1, 4):
                base = selected[f"{route}-zoom{zoom}-overlap8"].copy()
                base[base.index("--subdivisions") + 1] = "4"
                self.assertEqual(selected[f"{route}-zoom{zoom}-overlap8-subdiv4"], base)
        for name, arguments in selected.items():
            if name.startswith("cardinal-"):
                rotated = selected[name.replace("cardinal-", "per-axis-", 1)]
                self.assertEqual(rotated, ["--yaw", "0.6", *arguments[2:]])

    def test_rejects_missing_zero_and_wrong_path_samples(self):
        for route, stage, unlit_stage in (
            ("cardinal", "cardinalSurfaceFb", "trixelToFb"),
            ("per-axis", "perAxisSurfaceFb", "perAxisScatter"),
        ):
            with self.subTest(route=route), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary)
                name = f"{route}-zoom1-overlap0"
                selected = {name: []}
                with self.assertRaisesRegex(ValueError, "no profile"):
                    verify_routes(output, selected)
                directory = output / name / "round-1"
                directory.mkdir(parents=True)
                path = directory / "run-1.txt"
                for text in ("", f"{stage} 0.1 0.1 0.1 0\n",
                             f"{unlit_stage} 0.1 0.1 0.1 20\n"):
                    path.write_text("--- GPU stage timing ---\n" + text)
                    with self.assertRaisesRegex(ValueError, "no completed"):
                        verify_routes(output, selected)
                path.write_text(f"--- GPU stage timing ---\n{stage} 0.1 0.1 0.1 20\n")
                verify_routes(output, selected)
                other = "perAxisSurfaceFb" if route == "cardinal" else "cardinalSurfaceFb"
                with path.open("a") as file:
                    file.write(f"{other} 0.1 0.1 0.1 1\n")
                with self.assertRaisesRegex(ValueError, "unexpected"):
                    verify_routes(output, selected)

                unlit = f"{name}-unlit"
                directory = output / unlit / "round-1"
                directory.mkdir(parents=True)
                path = directory / "run-1.txt"
                # A lit route hidden behind a plausible unlit row is still invalid.
                path.write_text(f"--- GPU stage timing ---\n{stage} 0.1 0.1 0.1 20\n"
                                f"{unlit_stage} 0.1 0.1 0.1 20\n")
                with self.assertRaisesRegex(ValueError, "unexpected"):
                    verify_routes(output, {unlit: []})
                path.write_text(
                    f"--- GPU stage timing ---\n{unlit_stage} 0.1 0.1 0.1 20\n")
                verify_routes(output, {unlit: []})


if __name__ == "__main__":
    unittest.main()
