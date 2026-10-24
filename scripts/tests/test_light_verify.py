"""Exercise lighting capture verdicts and domain evidence without launching a demo."""

import importlib.util
import io
import re
import subprocess
import sys
import tempfile
import threading
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
PROGRESS_RE = re.compile(r"^\[light-verify\] --(\S+): (\d+)/(\d+) (\S+) (PASS|FAIL|no reference)$",
                         re.MULTILINE)


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
              baselines=False, update=False, compare_results=None):
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
        compare_kwargs = (
            {"return_value": {"pass": True}}
            if compare_results is None
            else {"side_effect": compare_results}
        )
        with patch.object(lv.verify_common, "detect_worktree_root", return_value=self.root), \
                patch.object(lv.verify_common, "detect_backend", return_value="test"), \
                patch.object(lv.verify_common, "find_exe", return_value=self.exe), \
                patch.object(lv.verify_common, "run_pass", side_effect=run_pass) as runner, \
                patch.object(lv.verify_common, "compare", **compare_kwargs) as compare, \
                redirect_stdout(out), redirect_stderr(err):
            self.runner = runner
            result = lv.main(argv)
        self.compare_calls = compare.call_args_list
        # call_count's `+= 1` is not atomic across the comparison worker
        # threads; call_args_list.append is.
        return result, out.getvalue(), err.getvalue(), len(compare.call_args_list)

    def test_clean_complete_passes_still_compare_every_image(self):
        result, out, _, compared = self.drive(baselines=True)
        self.assertEqual((result, compared), (0, 6))
        self.assertIn("all checks PASS", out)

    def test_difference_classes_are_requested_and_printed_for_failures(self):
        failures = [
            {"pass": False, "match_pct": 90.0, "max_delta": 20, "psnr_db": 20.0,
             "difference_class": "shift", "shift": [-1, -1]},
            {"pass": False, "match_pct": 90.0, "max_delta": 20, "psnr_db": 20.0,
             "difference_class": "local"},
            {"pass": False, "match_pct": 90.0, "max_delta": 20, "psnr_db": 20.0,
             "difference_class": "global"},
        ]
        passing = [
            {"pass": True, "match_pct": 100.0, "max_delta": 0, "psnr_db": "inf",
             "difference_class": "identical"}
            for _ in range(3)
        ]
        result, out, _, compared = self.drive(
            baselines=True, compare_results=failures + passing
        )

        self.assertEqual((result, compared), (1, 6))
        self.assertIn("shift(-1,-1)", out)
        self.assertIn("class=shift(-1,-1)", out)
        self.assertIn("class=local", out)
        self.assertIn("class=global", out)
        self.assertTrue(all(call.kwargs.get("classify") is True
                            for call in self.compare_calls))

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


    def labels_by_pass(self):
        return {flag: [state["shot"] for state in lv._parse_domain_state(log)] or ["shot_000"]
                for flag, log in self.logs.items()}

    def test_each_pass_prints_one_progress_line_per_row(self):
        result, out, _, _ = self.drive(baselines=True)
        self.assertEqual(result, 0)
        progress = PROGRESS_RE.findall(out)
        for flag, labels in self.labels_by_pass().items():
            with self.subTest(flag=flag):
                rows = [row for row in progress if row[0] == flag]
                self.assertEqual(len(rows), len(labels))
                self.assertEqual([int(row[1]) for row in rows], list(range(1, len(labels) + 1)))
                self.assertTrue(all(int(row[2]) == len(labels) for row in rows))
                self.assertEqual(sorted(row[3] for row in rows), sorted(labels))
                self.assertIn(f"--{flag}: comparing {len(labels)} shot(s)", out)

    def test_missing_reference_rows_report_without_a_comparison(self):
        self.drive(baselines=True)
        (self.baselines / "hover-sweep" / "shot_000.png").unlink()
        result, out, _, compared = self.drive()
        self.assertEqual(compared, 5)
        self.assertIn("--hover-sweep: 1/1 shot_000 no reference", out)
        self.assertEqual(result, 1)

    def test_comparisons_run_concurrently(self):
        first_label = self.labels_by_pass()["light-domain-matrix"][0]
        later_ran = threading.Event()
        released = []

        def compare(image, reference, diff_out, thresholds, classify=False):
            if reference.stem == first_label:
                # Bounded: a serial loop runs this call alone and must fail
                # the case rather than hang the suite.
                released.append(later_ran.wait(timeout=5))
            else:
                later_ran.set()
            return {"pass": True}

        with patch("os.cpu_count", return_value=4):
            result, _, _, compared = self.drive(baselines=True, compare_results=compare)
        self.assertEqual((result, compared), (0, 6))
        self.assertEqual(released, [True])

    def test_summary_rows_keep_shot_order_when_results_arrive_reversed(self):
        def reversed_completion(futures):
            pending = list(futures)
            for future in pending:
                future.exception()
            return reversed(pending)

        def compare(image, reference, diff_out, thresholds, classify=False):
            return {"pass": True, "match_pct": 100.0, "max_delta": 0, "psnr_db": "inf"}

        with patch.object(lv, "as_completed", side_effect=reversed_completion):
            result, out, _, _ = self.drive(baselines=True, compare_results=compare)
        self.assertEqual(result, 0)
        labels = self.labels_by_pass()["light-domain-matrix"]
        progress = [row[3] for row in PROGRESS_RE.findall(out) if row[0] == "light-domain-matrix"]
        self.assertEqual(progress, list(reversed(labels)))
        summary = [line.split()[1] for line in out.splitlines()
                   if line.startswith("light-domain-matrix ")]
        self.assertEqual(summary, labels)

    def test_compare_error_stops_main(self):
        error_label = self.labels_by_pass()["light-domain-matrix"][1]

        def compare(image, reference, diff_out, thresholds, classify=False):
            if reference.stem == error_label:
                raise SystemExit("render-compare errored: synthetic")
            return {"pass": True}

        with self.assertRaises(SystemExit) as raised:
            self.drive(baselines=True, compare_results=compare)
        self.assertEqual(str(raised.exception.code), "render-compare errored: synthetic")
        self.assertEqual(self.runner.call_count, 1)

    def test_compare_error_cancels_queued_comparisons(self):
        release = threading.Event()
        both_active = threading.Event()
        started = []

        def compare(image, reference, diff_out, thresholds, classify=False):
            started.append(reference.stem)
            if len(started) >= 2:
                both_active.set()
            release.wait(timeout=5)
            return {"pass": True}

        def comparator_error(futures):
            both_active.wait(timeout=5)
            raise SystemExit("render-compare errored: synthetic")

        class ReleasingPool(lv.ThreadPoolExecutor):
            # Holds both workers until shutdown, so the third row stays queued
            # and only an explicit cancel can keep it from running.
            def shutdown(self, wait=True, *, cancel_futures=False):
                if cancel_futures:
                    super().shutdown(wait=False, cancel_futures=True)
                release.set()
                super().shutdown(wait=wait, cancel_futures=cancel_futures)

        with patch("os.cpu_count", return_value=2), \
                patch.object(lv, "ThreadPoolExecutor", ReleasingPool), \
                patch.object(lv, "as_completed", side_effect=comparator_error):
            with self.assertRaises(SystemExit):
                self.drive(baselines=True, compare_results=compare)
        self.assertEqual(len(started), 2)
        self.assertEqual(self.runner.call_count, 1)


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
