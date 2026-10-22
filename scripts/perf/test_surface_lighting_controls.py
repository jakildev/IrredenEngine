import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from surface_lighting_controls import adoption_cases, cases, main, verify_routes


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
                self.assertEqual(environment["IR_PERAXIS_VISIBILITY_STATS"], "0")

    def test_adoption_selection_overrides_ambient_and_records_expected_arms(self):
        names = ["per-axis-zoom1-overlap0", "cardinal-zoom4-overlap16"]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "matrix"
            arguments = ["controls", "--output", str(output), "--adoption-matrix"]
            for name in names:
                arguments += ["--case", name]
            ambient = {"IR_PERAXIS_SURFACE_LIGHTING": "bad",
                       "IR_PERAXIS_VISIBILITY_PREPASS": "bad",
                       "IR_PERAXIS_VISIBILITY_STATS": "1"}
            with patch("sys.argv", arguments), patch.dict("os.environ", ambient), \
                    patch("surface_lighting_controls.run_rounds") as run:
                self.assertEqual(main(), 0)
            selected, expected = adoption_cases({name: cases()[name] for name in names})
            self.assertEqual(run.call_args.args[1], selected)
            self.assertEqual(len(selected), 8)
            recorded = json.loads((output / "cases.json").read_text())
            self.assertEqual(recorded["expected_render_environment"], expected)
            for name, environment in run.call_args.kwargs["environments"].items():
                for key, value in expected[name].items():
                    self.assertEqual(environment[key], value)
            for base in names:
                arms = [name for name in selected if name.startswith(base + "--")]
                self.assertEqual(len(arms), 4)
                self.assertTrue(all(selected[name] == cases()[base] for name in arms))

    def test_invalid_selection_rejects_before_creating_output(self):
        for names in (["unknown"], ["per-axis-zoom1-overlap0"] * 2):
            with tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / "matrix"
                arguments = ["controls", "--output", str(output)]
                for name in names:
                    arguments += ["--case", name]
                with patch("sys.argv", arguments), patch("sys.stderr"), \
                        self.assertRaises(SystemExit) as raised:
                    main()
                self.assertEqual(raised.exception.code, 2)
                self.assertFalse(output.exists())

    def test_adoption_routes_preserve_cardinal_and_require_compute_when_disabled(self):
        selected, expected = adoption_cases(cases())
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for name in selected:
                cardinal = name.startswith("cardinal-")
                unlit = "-unlit--" in name
                surface = expected[name]["IR_PERAXIS_SURFACE_LIGHTING"] == "1"
                stage = ("trixelToFb" if cardinal else "perAxisScatter") if unlit else (
                    "cardinalSurfaceFb" if cardinal else
                    ("perAxisSurfaceFb" if surface else "perAxisScatter"))
                rows = [stage]
                if not cardinal and not unlit and not surface:
                    rows.append("lightingPerAxis")
                directory = output / name / "round-1"
                directory.mkdir(parents=True)
                (directory / "manifest.json").write_text("{}")
                report = directory / "run-1.txt"
                text = "--- GPU stage timing ---\n" + "".join(
                    f"{row} 0.1 0.1 0.1 20\n" for row in rows)
                report.write_text(text)
                verify_routes(output, {name: selected[name]},
                              expected_environments=expected, rounds=1)
                if not cardinal and not unlit:
                    wrong = "perAxisScatter" if surface else "perAxisSurfaceFb"
                    report.write_text(text + f"{wrong} 0.1 0.1 0.1 20\n")
                    with self.assertRaisesRegex(ValueError, "unexpected"):
                        verify_routes(output, {name: []}, expected_environments=expected)
                report.write_text(text)
                if not cardinal and not unlit and surface:
                    report.write_text(text + "lightingPerAxis 0.1 0.1 0.1 20\n")
                    with self.assertRaisesRegex(ValueError, "unexpected lightingPerAxis"):
                        verify_routes(output, {name: []}, expected_environments=expected)
                    report.write_text(text)
                if len(rows) == 2:
                    report.write_text(text.replace("lightingPerAxis 0.1 0.1 0.1 20\n", ""))
                    with self.assertRaisesRegex(ValueError, "no completed lightingPerAxis"):
                        verify_routes(output, {name: []}, expected_environments=expected)
                report.write_text(text)
            verify_routes(output, selected, expected_environments=expected, rounds=1)
            with self.assertRaisesRegex(ValueError, "incomplete profile rounds"):
                verify_routes(output, selected, expected_environments=expected, rounds=2)
            (directory / "manifest.json").unlink()
            with self.assertRaisesRegex(ValueError, "missing profile manifest"):
                verify_routes(output, selected, expected_environments=expected, rounds=1)
            report.unlink()
            with self.assertRaisesRegex(ValueError, "no profile reports"):
                verify_routes(output, selected, expected_environments=expected, rounds=1)

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
