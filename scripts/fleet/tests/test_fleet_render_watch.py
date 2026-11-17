"""Hermetic scheduling, tracker, and failure tests for fleet-render-watch."""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

SUBJECT = Path(__file__).resolve().parents[1] / "fleet-render-watch"
if not SUBJECT.is_file():
    print("SKIP: fleet-render-watch subject absent", file=sys.stderr)
    sys.exit(3)

spec = importlib.util.spec_from_loader("fleet_render_watch", loader=None)
watch = importlib.util.module_from_spec(spec)
watch.__file__ = str(SUBJECT)
exec(compile(SUBJECT.read_text(), str(SUBJECT), "exec"), watch.__dict__)  # noqa: S102

from test_scout_degraded_fetch import _mod as scout  # noqa: E402

COMMIT = "a" * 40


@contextmanager
def environment(**values):
    prior = {key: os.environ.get(key) for key in values}
    for key, value in values.items():
        if value is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = str(value)
    try:
        yield
    finally:
        for key, value in prior.items():
            os.environ.pop(key, None)
            if value is not None:
                os.environ[key] = value


class Scheduler(unittest.TestCase):

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.seen = self.root / "seen"
        self.state = self.root / "state"
        self.seen.mkdir()
        self.state.mkdir()
        scout._spawn_fail_streak.clear()
        self.addCleanup(scout._spawn_fail_streak.clear)
        self.spawns = []

    def run_lane(self, now=100, *, running=False, popen=None):
        def record(argv, **_kwargs):
            self.spawns.append(list(argv))

        with environment(FLEET_RENDER_WATCH="1", FLEET_RENDER_WATCH_INTERVAL_SECS="50",
                         FLEET_STATE_DIR=self.state), \
             patch.object(scout, "SEEN_DIR", self.seen), \
             patch.object(scout, "STATE_FILE", self.state / "state.json"), \
             patch.object(scout, "_render_watch_commit", return_value=COMMIT), \
             patch.object(scout, "_render_watch_running", return_value=running), \
             patch.object(scout.subprocess, "Popen", popen or record):
            scout.run_render_watch_lane(now)

    def test_elapsed_interval_starts_once_and_fresh_interval_starts_none(self):
        self.run_lane(100)
        self.run_lane(120)
        self.assertEqual(len(self.spawns), 1)
        self.assertEqual(self.spawns[0][-2:], ["--commit", COMMIT])

    def test_cached_head_and_running_sweep_each_block_a_second_start(self):
        record = self.state / "render-verify-controls" / "macos-debug" / f"{COMMIT}.json"
        record.parent.mkdir(parents=True)
        record.write_text("{}\n")
        self.run_lane()
        record.unlink()
        self.run_lane(running=True)
        self.assertEqual(self.spawns, [])
        self.assertFalse((self.seen / scout.RENDER_WATCH_MARKER_NAME).exists())

    def test_failed_spawn_leaves_deadline_unconsumed_and_records_failure(self):
        def fail(*_args, **_kwargs):
            raise OSError("fork refused")

        self.run_lane(popen=fail)
        self.assertFalse((self.seen / scout.RENDER_WATCH_MARKER_NAME).exists())
        self.assertEqual(scout._spawn_fail_streak["render-watch"][1], 1)

    def test_off_by_default_starts_and_writes_nothing(self):
        with environment(FLEET_RENDER_WATCH=None, FLEET_STATE_DIR=self.state), \
             patch.object(scout, "SEEN_DIR", self.seen), \
             patch.object(scout.subprocess, "Popen") as popen:
            scout.run_render_watch_lane(100)
        popen.assert_not_called()
        self.assertEqual(list(self.seen.iterdir()), [])
        self.assertEqual(list(self.state.iterdir()), [])


class RecordingGh:
    """Fail-closed GitHub stub that records every accepted argument vector."""

    def __init__(self, lookup=None, lookup_rc=0):
        self.lookup = [] if lookup is None else lookup
        self.lookup_rc = lookup_rc
        self.calls = []

    def __call__(self, argv, **_kwargs):
        self.calls.append(list(argv))
        if argv[:2] == ["issue", "list"]:
            return subprocess.CompletedProcess(argv, self.lookup_rc,
                                               json.dumps(self.lookup), "lookup failed")
        if argv[:2] == ["issue", "create"]:
            url = "https://github.com/jakildev/IrredenEngine/issues/77\n"
            return subprocess.CompletedProcess(argv, 0, url, "")
        if argv[:2] in (["issue", "edit"], ["issue", "comment"]):
            return subprocess.CompletedProcess(argv, 0, "", "")
        raise AssertionError(f"unmodeled gh call: {argv}")


class WatchRun(unittest.TestCase):

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = self.root / "state"
        self.worktree = self.root / "worktree"
        self.worktree.mkdir()
        self.record_rows = [{
            "demo": "shape_debug", "shot": "yaw0", "kind": "image",
            "verdict": "FAIL", "metrics": {"max_delta": 9},
        }]

    def result(self, rows=None):
        return {"version": 1, "commit": COMMIT, "preset": "macos-debug",
                "build_type": "Debug", "generated_at": "2026-11-16T00:00:00Z",
                "rows": self.record_rows if rows is None else rows}

    def sweep(self, record=None, rc=1):
        def run(_worktree, result_path):
            if record is not False:
                result_path.parent.mkdir(parents=True, exist_ok=True)
                result_path.write_text(json.dumps(record or self.result()))
            return subprocess.CompletedProcess([], rc, "sweep output", "")
        return run

    @contextmanager
    def patched(self, gh, sweep):
        with environment(FLEET_DIR=self.root, FLEET_STATE_DIR=self.state,
                         FLEET_RENDER_WATCH_ROOT=self.root / "watch"), \
             patch.object(watch, "_prepare_worktree", return_value=self.worktree), \
             patch.object(watch, "_run_sweep", side_effect=sweep), \
             patch.object(watch, "_public_text_check"), \
             patch.object(watch.fleet_github, "run", side_effect=gh):
            yield

    def control(self):
        return self.state / "render-verify-controls" / "macos-debug" / f"{COMMIT}.json"

    def status(self):
        return self.state / "render-watch-status.json"

    def test_red_without_tracker_files_once_and_caches_the_tracker_number(self):
        gh = RecordingGh()
        with self.patched(gh, self.sweep()):
            self.assertEqual(watch.run_watch(COMMIT), 0)
        self.assertEqual([call[:2] for call in gh.calls],
                         [["issue", "list"], ["issue", "create"]])
        record = json.loads(self.control().read_text())
        self.assertEqual(record["tracker"], 77)
        self.assertIn("shape_debug:yaw0", (self.root / "watch" / "tracker-body.md").read_text())

    def test_red_with_tracker_edits_it_and_files_nothing(self):
        marker = watch.TRACKER_MARKER.format(preset="macos-debug")
        gh = RecordingGh([{"number": 42, "title": watch._tracker_title("macos-debug"),
                           "body": marker}])
        with self.patched(gh, self.sweep()):
            self.assertEqual(watch.run_watch(COMMIT), 0)
        verbs = [call[1] for call in gh.calls]
        self.assertEqual(verbs, ["list", "edit"])
        self.assertEqual(json.loads(self.control().read_text())["tracker"], 42)

    def test_green_closes_the_open_tracker(self):
        marker = watch.TRACKER_MARKER.format(preset="macos-debug")
        gh = RecordingGh([{"number": 42, "title": watch._tracker_title("macos-debug"),
                           "body": marker}])
        with self.patched(gh, self.sweep(self.result(rows=[]), rc=0)):
            self.assertEqual(watch.run_watch(COMMIT), 0)
        self.assertEqual([call[1] for call in gh.calls], ["list", "comment", "edit"])
        self.assertIn("--state", gh.calls[-1])
        self.assertIn("closed", gh.calls[-1])

    def test_unreadable_lookup_writes_no_tracker_or_control(self):
        gh = RecordingGh(lookup_rc=1)
        with self.patched(gh, self.sweep()):
            self.assertEqual(watch.main(["--commit", COMMIT]), 1)
        self.assertEqual([call[1] for call in gh.calls], ["list"])
        self.assertFalse(self.control().exists())
        status = json.loads(self.status().read_text())
        self.assertEqual(status["outcome"], "failed")
        self.assertIn("tracker lookup failed", status["reason"])

    def test_worktree_failure_writes_failed_status(self):
        gh = RecordingGh()
        with self.patched(gh, self.sweep()), \
             patch.object(watch, "_prepare_worktree",
                          side_effect=watch.WatchError("render-watch worktree is dirty")):
            self.assertEqual(watch.main(["--commit", COMMIT]), 1)
        self.assertEqual(gh.calls, [])
        self.assertFalse(self.control().exists())
        status = json.loads(self.status().read_text())
        self.assertEqual(status["outcome"], "failed")
        self.assertEqual(status["reason"], "render-watch worktree is dirty")

    def test_live_lock_does_not_overwrite_status(self):
        self.state.mkdir(parents=True)
        self.status().write_text('{"outcome": "running"}\n')
        lock = self.state / "render-watch.lock"
        lock.write_text(json.dumps({"pid": os.getpid()}))
        with environment(FLEET_DIR=self.root, FLEET_STATE_DIR=self.state,
                         FLEET_RENDER_WATCH_ROOT=self.root / "watch"):
            self.assertEqual(watch.main(["--commit", COMMIT]), 1)
        self.assertEqual(json.loads(self.status().read_text()), {"outcome": "running"})

    def test_no_display_or_timeout_stores_no_record_and_leaves_tracker_untouched(self):
        for label, sweep in {
            "no display": self.sweep(False, rc=1),
            "timeout": subprocess.TimeoutExpired("render-verify", 5),
        }.items():
            with self.subTest(label=label):
                gh = RecordingGh()
                effect = sweep if callable(sweep) else lambda *_args: (_ for _ in ()).throw(sweep)
                with self.patched(gh, effect):
                    self.assertEqual(watch.run_watch(COMMIT), 1)
                self.assertEqual(gh.calls, [])
                self.assertFalse(self.control().exists())
                status = json.loads(self.status().read_text())
                self.assertEqual(status["outcome"], "failed")


if __name__ == "__main__":
    unittest.main()
