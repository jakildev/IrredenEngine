import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PERF = Path(__file__).resolve().parents[1] / "perf"
sys.path.insert(0, str(PERF))

from sun_depth_slab_census import summarize_probe  # noqa: E402
from sun_depth_slab_timing import BAKE_ROW, verdict  # noqa: E402


class SunDepthSlabToolsTest(unittest.TestCase):
    def summarize(self, mode, rows):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.csv"
            path.write_text(
                f"# slab_mode={mode}\n# slab_slots=3\n"
                "cascade,x,y,nearest,slab0_min,slab0_max,slab1_min,slab1_max,"
                "slab2_min,slab2_max,overflow,collision\n" + rows)
            return summarize_probe(path)["cascades"]["0"]

    def test_associative_census_separates_incomplete_write_sets(self):
        # Slabs 1 and 4 collide modulo three; an overflowed row is incomplete.
        row = "0,0,0,16777216,16777216,16777217,67108864,67108865,4294967295,0,"
        result = self.summarize("associative", row + "0,0\n" + row + "1,0\n")
        self.assertEqual(result["complete_rows"], 1)
        self.assertEqual(result["complete_modulo_collisions"], 1)
        self.assertEqual(result["overflowed_rows"], 1)
        self.assertEqual(result["overflowed_retained_modulo_collisions"], 1)
        self.assertEqual(result["direct_collisions"], 0)

    def test_direct_census_recomputes_collisions_across_sparse_slots(self):
        row = "0,0,0,16777216,4294967295,0,16777216,67108864,4294967295,0,0,"
        result = self.summarize("direct", row + "1\n")
        self.assertEqual(result["direct_collisions"], 1)
        self.assertEqual(result["complete_rows"], 0)
        with self.assertRaisesRegex(ValueError, "disagrees with raw extrema"):
            self.summarize("direct", row + "0\n")

    def test_budget_bound_cannot_excuse_a_frame_regression(self):
        pair = {"mean": 0.07, "lower95": 0.06}
        self.assertEqual(verdict("canvas-lower", BAKE_ROW, 1.12, pair)[1], "pass (ii)")
        self.assertEqual(verdict("canvas-lower", BAKE_ROW, 1.12, pair, False)[1], "FAIL")
        self.assertEqual(verdict("canvas-stacked-128", BAKE_ROW, 1.12, pair)[1], "FAIL")
        self.assertEqual(verdict("canvas-lower", BAKE_ROW, 1.16, pair)[1], "FAIL")


class CaptureLeaseTest(unittest.TestCase):
    """The bare form is what the macOS Codex policy lets outside the sandbox."""

    def launch(self, temporary, extra_env):
        record = Path(temporary) / "argv.json"
        stub = Path(temporary) / "ir-acquire"
        stub.write_text(f"#!{sys.executable}\nimport json, sys\n"
                        f"open({json.dumps(str(record))}, 'w').write(json.dumps(sys.argv[1:]))\n"
                        "sys.exit(7)\n")
        stub.chmod(0o755)
        env = {key: value for key, value in os.environ.items()
               if key not in ("IR_ACQUIRE_HELD_VERB", "IR_SUN_SLAB_CAPTURE_REEXEC")}
        env.update({"IR_ACQUIRE": stub.as_posix(), **extra_env})
        args = [sys.executable, str(PERF / "sun_depth_slab_capture.py"),
                "--output", str(Path(temporary) / "out"), "--cases", "lower"]
        return subprocess.run(args, env=env, capture_output=True, text=True,
                              timeout=30), record

    def test_bare_launch_reruns_itself_under_the_gpu_lease(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, record = self.launch(temporary, {})
            self.assertEqual(result.returncode, 7, result.stderr)
            argv = json.loads(record.read_text())
            script = (PERF / "sun_depth_slab_capture.py").resolve().as_posix()
            self.assertEqual(argv[:4], ["gpu", "--", sys.executable, script])
            self.assertEqual(argv[4:], ["--output", str(Path(temporary) / "out"),
                                        "--cases", "lower"])
            self.assertFalse((Path(temporary) / "out").exists())

    def test_a_lease_that_never_exports_refuses_to_recurse(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, record = self.launch(temporary, {"IR_SUN_SLAB_CAPTURE_REEXEC": "1"})
            self.assertEqual(result.returncode, 2)
            self.assertIn("refusing to recurse", result.stderr)
            self.assertFalse(record.exists())


if __name__ == "__main__":
    unittest.main()
