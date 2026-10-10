"""Exercise lighting capture verdicts and domain evidence without launching a demo."""

import importlib.util
import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location("light_verify", SCRIPTS / "light-verify.py")
lv = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lv)

CLEAN = "ir-run: RESULT=CLEAN exe=IRLightingEmissive exit=0\n"
TIMEOUT = "ir-run: RESULT=ALIVE-TIMEOUT exe=IRLightingEmissive exit=0\n"


def domain_line(label, state="SEEDED_FULL", residual=1.0):
    return (f"DOMAIN-STATE shot={label} anchor=0,0,0 window=-64,-64,-64..64,64,64 "
            f"lights=[1:{state}:{residual}] feeder=0,0..1,1 casters=1\n")


class LightVerifyCompletionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.exe = self.root / "build/lighting/IRLightingEmissive"
        self.exe.parent.mkdir(parents=True)
        self.exe.touch()
        self.baselines = self.root / "creations/demos/lighting/test/references/test/light-verify"
        self.logs = {
            "light-domain-matrix": domain_line("domain_z1_yaw0_inwin")
            + domain_line("domain_z1_yaw0_band", "BOUNDARY_DISCOUNTED", 0.5)
            + domain_line("domain_z1_yaw0_beyond", "SKIPPED", 0.0),
            "light-boundary-sweep": domain_line("light_boundary_d000")
            + domain_line("light_boundary_d070", "BOUNDARY_DISCOUNTED", 0.5),
            "hover-sweep": "",
        }

    def drive(self, *, token=CLEAN, rc=0, empty=False, omit_domains=False,
              baselines=False, update=False):
        def run_pass(command, worktree, shots_dir, timeout):
            flag = next(flag for flag in lv.PASSES if f"--{flag}" in command)
            log = self.logs[flag]
            states = lv._parse_domain_state(log)
            labels = [state["shot"] for state in states] or ["shot_000"]
            images = []
            shots_dir.mkdir(parents=True, exist_ok=True)
            for index, label in enumerate(labels):
                if not empty:
                    image = shots_dir / f"screenshot_{index:06d}.png"
                    image.write_bytes(b"synthetic capture")
                    images.append(image)
                if baselines:
                    reference = self.baselines / flag / f"{label}.png"
                    reference.parent.mkdir(parents=True, exist_ok=True)
                    reference.write_bytes(b"reference")
            return rc, ("" if omit_domains else log) + token, images

        argv = ["--no-build"] + (["--update-baselines", "--force"] if update else [])
        out, err = io.StringIO(), io.StringIO()
        with patch.object(lv.verify_common, "detect_worktree_root", return_value=self.root), \
                patch.object(lv.verify_common, "detect_backend", return_value="test"), \
                patch.object(lv.verify_common, "find_exe", return_value=self.exe), \
                patch.object(lv.verify_common, "run_pass", side_effect=run_pass), \
                patch.object(lv.verify_common, "compare", return_value={"pass": True}) as compare, \
                redirect_stdout(out), redirect_stderr(err):
            result = lv.main(argv)
        return result, out.getvalue(), err.getvalue(), compare.call_count

    def test_clean_complete_passes_still_compare_every_image(self):
        result, out, _, compared = self.drive(baselines=True)
        self.assertEqual((result, compared), (0, 6))
        self.assertIn("all checks PASS", out)

    def test_clean_evidence_preserves_missing_backend_baseline_skip(self):
        result, out, _, compared = self.drive()
        self.assertEqual((result, compared), (0, 0))
        self.assertIn("some backends skipped", out)

    def test_zero_exit_watchdog_with_matching_images_fails(self):
        result, out, err, compared = self.drive(token=TIMEOUT, baselines=True)
        self.assertEqual((result, compared), (1, 6))
        self.assertNotIn("all checks PASS", out)
        self.assertIn("RESULT=ALIVE-TIMEOUT", err)

    def test_empty_clean_and_watchdog_passes_both_fail(self):
        for token in (CLEAN, TIMEOUT):
            with self.subTest(token=token):
                result, out, _, _ = self.drive(token=token, empty=True, omit_domains=True)
                self.assertEqual(result, 1)
                self.assertIn("captured no screenshots", out)
                self.assertNotIn("all checks PASS", out)

    def test_missing_completion_marker_fails(self):
        result, _, err, _ = self.drive(token="demo logs only\n")
        self.assertEqual(result, 1)
        self.assertIn("RESULT=MISSING", err)

    def test_clean_marker_does_not_override_nonzero_exit(self):
        self.assertEqual(self.drive(rc=1)[0], 1)

    def test_images_without_domain_evidence_fail(self):
        result, out, _, _ = self.drive(omit_domains=True)
        self.assertEqual(result, 1)
        self.assertIn("DOMAIN-STATE", out)

    def test_unrecognized_light_states_cannot_pass_vacuously(self):
        self.logs["light-domain-matrix"] = self.logs["light-domain-matrix"].replace(
            "SEEDED_FULL", "UNKNOWN_STATE")
        result, out, _, _ = self.drive()
        self.assertEqual(result, 1)
        self.assertIn("parsed no light entries", out)

    def test_boundary_sweep_requires_multiple_measurements(self):
        self.logs["light-boundary-sweep"] = domain_line("light_boundary_d000")
        result, out, _, _ = self.drive()
        self.assertEqual(result, 1)
        self.assertIn("at least two DOMAIN-STATE shots", out)

    def test_failed_passes_cannot_update_baselines(self):
        result, _, err, _ = self.drive(token=TIMEOUT, update=True)
        self.assertEqual(result, 1)
        self.assertFalse(self.baselines.exists())
        self.assertIn("baselines not updated", err)

    def test_empty_clean_passes_cannot_update_baselines(self):
        result, _, err, _ = self.drive(empty=True, omit_domains=True, update=True)
        self.assertEqual(result, 1)
        self.assertFalse(self.baselines.exists())
        self.assertIn("baselines not updated", err)


class LightVerifyChangedGateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(
            ["git", "-C", str(self.root), "config", "user.email", "test@example.com"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(self.root), "config", "user.name", "Test"], check=True
        )
        seed = self.root / "README"
        seed.write_text("seed\n")
        subprocess.run(["git", "-C", str(self.root), "add", "README"], check=True)
        subprocess.run(
            ["git", "-C", str(self.root), "commit", "-q", "-m", "seed"], check=True
        )

    def write(self, relative: str, contents: str = "changed\n") -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
        return path

    def drive(self, against: str = "HEAD") -> tuple[int, str, int]:
        out = io.StringIO()
        with patch.object(lv.verify_common, "detect_worktree_root", return_value=self.root), \
                patch.object(lv.verify_common, "detect_backend", return_value="test"), \
                patch.object(lv.verify_common, "find_exe", return_value=self.root / "fake"), \
                patch.object(lv.verify_common, "run_pass", return_value=(1, "", [])) as run_pass, \
                redirect_stdout(out):
            result = lv.main(["--no-build", "--if-changed", "--against", against])
        return result, out.getvalue(), run_pass.call_count

    def test_unstaged_engine_edit_runs_all_passes(self):
        path = self.write("engine/example.cpp", "base\n")
        subprocess.run(["git", "-C", str(self.root), "add", str(path)], check=True)
        subprocess.run(
            ["git", "-C", str(self.root), "commit", "-q", "-m", "engine file"], check=True
        )
        path.write_text("unstaged\n")
        _, out, calls = self.drive()
        self.assertEqual(calls, 3)
        self.assertIn("1 of 1 changed path(s)", out)

    def test_staged_engine_edit_runs_all_passes(self):
        self.write("engine/staged.cpp")
        subprocess.run(["git", "-C", str(self.root), "add", "engine/staged.cpp"], check=True)
        _, out, calls = self.drive()
        self.assertEqual(calls, 3)
        self.assertIn("first: engine/staged.cpp", out)

    def test_untracked_lighting_demo_file_runs_all_passes(self):
        self.write("creations/demos/lighting/probe.txt")
        _, out, calls = self.drive()
        self.assertEqual(calls, 3)
        self.assertIn("first: creations/demos/lighting/probe.txt", out)

    def test_markdown_only_changes_do_not_run(self):
        self.write("docs/x.md")
        self.write("engine/render/CLAUDE.md")
        result, out, calls = self.drive()
        self.assertEqual((result, calls), (0, 0))
        self.assertIn("0 of 2 changed path(s)", out)
        self.assertIn("not required", out)

    def test_unresolvable_against_runs_all_passes(self):
        _, out, calls = self.drive("missing-ref")
        self.assertEqual(calls, 3)
        self.assertIn("cannot resolve --against 'missing-ref'", out)
        self.assertIn("running", out)

    def test_changed_path_helpers_cover_git_and_trigger_contract(self):
        self.assertEqual(lv._changed_paths(self.root, "HEAD"), ([], ""))
        self.assertTrue(lv._is_trigger_path("engine/example.cpp"))
        self.assertFalse(lv._is_trigger_path("engine/render/CLAUDE.md"))
        self.write("engine/example.cpp")
        self.assertTrue(lv._if_changed_requires_run(self.root, "HEAD"))


if __name__ == "__main__":
    unittest.main()
