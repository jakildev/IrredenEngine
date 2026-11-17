"""Hermetic tests for render-verify result records and cached controls."""

import importlib.machinery
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))


def _load():
    loader = importlib.machinery.SourceFileLoader(
        "render_verify_control_subject", str(_SCRIPTS / "render-verify.py")
    )
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    sys.modules[loader.name] = module
    loader.exec_module(module)
    return module


_rv = _load()


def _run(repo: Path, *args: str, check: bool = True):
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        capture_output=True,
        text=True,
        check=check,
    )


def _row(label: str, passed: bool, match: float = 90.0, delta: int = 10):
    return {
        "label": label,
        "kind": "frame",
        "pass": passed,
        "result": {
            "pass": passed,
            "match_pct": match,
            "max_delta": delta,
            "psnr_db": 30.0,
            "diff_path": "/temporary/diff.png",
        },
    }


def _result(rows, rc=1):
    return {
        "target": "IRFake",
        "demo": "fake",
        "rc": rc,
        "checked": len(rows),
        "failed": sum(not row["pass"] for row in rows),
        "skipped": 0,
        "host_closed": 0,
        "rows": rows,
        "recordable": True,
    }


class RenderVerifyControlTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        _run(self.repo, "init", "-q")
        _run(self.repo, "config", "user.email", "test@example.com")
        _run(self.repo, "config", "user.name", "Render Verify Test")
        (self.repo / "tracked").write_text("base\n", encoding="utf-8")
        (self.repo / ".gitignore").write_text("build/\n", encoding="utf-8")
        _run(self.repo, "add", "tracked", ".gitignore")
        _run(self.repo, "commit", "-qm", "base")
        self.master = _run(self.repo, "rev-parse", "HEAD").stdout.strip()
        _run(self.repo, "update-ref", "refs/remotes/origin/master", self.master)
        self.build = self.repo / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Debug\n", encoding="utf-8"
        )
        self.state = self.root / "state"
        self.env = patch.dict(os.environ, {"FLEET_STATE_DIR": str(self.state)})
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.temp.cleanup()

    def _control(self, rows, *, commit=None, tracker=None):
        record = {
            "version": 1,
            "commit": commit or self.master,
            "preset": "macos-debug",
            "build_type": "Debug",
            "generated_at": "2026-01-01T00:00:00+00:00",
            "rows": [
                {
                    "demo": "fake",
                    "shot": row["label"],
                    "kind": row["kind"],
                    "verdict": "PASS" if row["pass"] else "FAIL",
                    "metrics": _rv._row_metrics(row),
                }
                for row in rows
            ],
        }
        if tracker is not None:
            record["tracker"] = tracker
        path = _rv._control_path("macos-debug", record["commit"])
        _rv._write_json_atomic(path, record)
        return path

    def test_storage_and_git_helpers(self):
        self.assertEqual(_rv._git(self.repo, "rev-parse", "HEAD").stdout.strip(),
                         self.master)
        self.assertEqual(_rv._head_commit(self.repo), self.master)
        self.assertEqual(_rv._merge_base(self.repo), self.master)
        self.assertEqual(_rv._build_type(self.build), "Debug")
        self.assertEqual(_rv._control_cache_root(),
                         self.state / _rv.CONTROL_CACHE_SUBDIR)

        scratch = self.state / "scratch" / "record.json"
        scratch.parent.mkdir(parents=True)
        temp = _rv._write_json_temp(scratch, {"value": 1})
        self.assertEqual(json.loads(temp.read_text(encoding="utf-8")),
                         {"value": 1})
        temp.unlink()

        preset_dir = self.state / "prune"
        for index in range(3):
            _rv._write_json_atomic(preset_dir / f"{index}.json", {"index": index})
        with patch.object(_rv, "CONTROL_CACHE_LIMIT", 2):
            _rv._prune_controls(preset_dir)
        self.assertEqual(len(list(preset_dir.glob("*.json"))), 2)

        failed = _row("A", False)
        serialized = {
            "demo": "fake", "shot": "A", "kind": "frame",
            "verdict": "FAIL", "metrics": _rv._row_metrics(failed),
        }
        self.assertEqual(_rv._row_key(serialized), ("fake", "A", "frame"))
        self.assertTrue(_rv._control_row_matches(
            {("fake", "A", "frame"): serialized}, _result([failed]), failed
        ))

    def test_json_out_records_each_row_without_changing_exit_code(self):
        for rc, rows in ((0, [_row("A", True)]), (1, [_row("A", False)])):
            with self.subTest(rc=rc):
                output = self.root / f"result-{rc}.json"
                result = _result(rows, rc=rc)
                with (
                    patch.object(_rv.verify_common, "detect_worktree_root",
                                 return_value=self.repo),
                    patch.object(_rv.verify_common, "detect_backend",
                                 return_value="macos-debug"),
                    patch.object(_rv, "_resolve_demo_dir",
                                 return_value=self.repo / "fake"),
                    patch.object(_rv, "_verify_one", return_value=result),
                ):
                    actual_rc = _rv.main([
                        "--target", "IRFake", "--build-dir", str(self.build),
                        "--json-out", str(output), "--no-build",
                    ])
                self.assertEqual(actual_rc, rc)
                record = json.loads(output.read_text(encoding="utf-8"))
                self.assertEqual(record["commit"], self.master)
                self.assertEqual(record["preset"], "macos-debug")
                self.assertEqual(record["build_type"], "Debug")
                self.assertEqual(len(record["rows"]), len(rows))
                self.assertEqual(record["rows"][0]["demo"], "fake")
                self.assertEqual(record["rows"][0]["shot"], "A")
                self.assertEqual(record["rows"][0]["verdict"],
                                 "PASS" if rc == 0 else "FAIL")
                self.assertEqual(record["rows"][0]["metrics"]["max_delta"], 10)

    def test_inherited_failure_is_green_but_new_failure_is_moved(self):
        self._control([_row("A", False), _row("B", True)])
        head = _result([_row("A", False), _row("B", False)])
        stream = io.StringIO()
        with redirect_stdout(stream):
            _rv._grade_against_control(
                results=[head], worktree=self.repo, preset="macos-debug"
            )
        self.assertEqual(head["rc"], 1)
        self.assertIn("fake:A: inherited", stream.getvalue())
        self.assertIn("fake:B: moved", stream.getvalue())

        inherited_only = _result([_row("A", False)])
        _rv._grade_against_control(
            results=[inherited_only], worktree=self.repo, preset="macos-debug"
        )
        self.assertEqual(inherited_only["rc"], 0)

    def test_equal_verdict_with_different_metrics_is_moved(self):
        self._control([_row("A", False, match=90.0)])
        head = _result([_row("A", False, match=89.9)])
        stream = io.StringIO()
        with redirect_stdout(stream):
            _rv._grade_against_control(
                results=[head], worktree=self.repo, preset="macos-debug"
            )
        self.assertEqual(head["rc"], 1)
        self.assertIn("fake:A: moved", stream.getvalue())

    def test_missing_exact_merge_base_never_uses_ancestor_record(self):
        parent = self.master
        (self.repo / "tracked").write_text("master two\n", encoding="utf-8")
        _run(self.repo, "commit", "-qam", "master two")
        merge_base = _run(self.repo, "rev-parse", "HEAD").stdout.strip()
        _run(self.repo, "update-ref", "refs/remotes/origin/master", merge_base)
        ancestor_path = self._control([_row("A", False)], commit=parent)
        exact_path = _rv._control_path("macos-debug", merge_base)
        exact_path.write_bytes(ancestor_path.read_bytes())
        head = _result([_row("A", False)])
        stream = io.StringIO()
        with redirect_stdout(stream):
            _rv._grade_against_control(
                results=[head], worktree=self.repo, preset="macos-debug"
            )
        output = stream.getvalue()
        self.assertEqual(head["rc"], 1)
        self.assertIn(f"no control for merge base {merge_base}", output)
        self.assertIn("--all --record-control", output)
        self.assertIn("fake:A: no control", output)

    def test_record_control_refuses_dirty_and_non_master_commits(self):
        (self.repo / "untracked").write_text("dirty\n", encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "clean worktree"):
            _rv._validate_control_source(self.repo)
        (self.repo / "untracked").unlink()

        _run(self.repo, "checkout", "-qb", "topic")
        (self.repo / "tracked").write_text("topic\n", encoding="utf-8")
        _run(self.repo, "commit", "-qam", "topic")
        with self.assertRaisesRegex(SystemExit, "commit on origin/master"):
            _rv._validate_control_source(self.repo)

    def test_existing_control_is_not_overwritten(self):
        path = self._control([_row("A", False)])
        original = path.read_bytes()
        replacement = json.loads(original)
        replacement["rows"] = []
        _rv._store_control(replacement)
        self.assertEqual(path.read_bytes(), original)

    def test_tracker_is_printed_for_inherited_row(self):
        self._control([_row("A", False)], tracker=4986)
        head = _result([_row("A", False)])
        stream = io.StringIO()
        with redirect_stdout(stream):
            _rv._grade_against_control(
                results=[head], worktree=self.repo, preset="macos-debug"
            )
        self.assertEqual(head["rc"], 0)
        self.assertIn("inherited tracker=#4986", stream.getvalue())

    def test_no_record_is_written_for_a_run_without_verdicts(self):
        result = _result([])
        result["recordable"] = False
        record = _rv._result_record(
            results=[result], worktree=self.repo, build_dir=self.build,
            preset="macos-debug", tracker=None,
        )
        self.assertIsNone(record)


if __name__ == "__main__":
    unittest.main()
