#!/usr/bin/env python3
"""Tests for explicit decline markers and the default temporary lifetime."""

import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import fleet_decline  # noqa: E402


class DeclineMetadataTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.state_dir = Path(self.tempdir.name)
        (self.state_dir / "declined").mkdir()
        self.previous_state_dir = os.environ.get("FLEET_STATE_DIR")
        os.environ["FLEET_STATE_DIR"] = str(self.state_dir)
        self.record = {
            "number": 42,
            "repo": "engine",
            "updatedAt": "2026-11-18T00:00:00Z",
        }

    def tearDown(self):
        if self.previous_state_dir is None:
            os.environ.pop("FLEET_STATE_DIR", None)
        else:
            os.environ["FLEET_STATE_DIR"] = self.previous_state_dir
        self.tempdir.cleanup()

    def write_decline(self, detail, age_seconds, runtime=None):
        path = self.state_dir / "declined" / "task-engine-42"
        text = f"{self.record['updatedAt']}\n{detail}\nworker\n"
        if runtime:
            text += f"runtime={runtime}\n"
        path.write_text(text, encoding="utf-8")
        modified = time.time() - age_seconds
        os.utime(path, (modified, modified))

    def metadata(self, record=None):
        return fleet_decline.metadata("task", record or self.record, "worker")

    def test_unmarked_default_expires_after_thirty_minutes(self):
        self.write_decline("declined this iteration: unfamiliar condition", 10 * 60)
        recent = self.metadata()
        self.assertIsNotNone(recent)
        self.assertTrue(recent["transient"])
        self.assertEqual(recent["class"], "temporary")

        self.write_decline("declined this iteration: unfamiliar condition", 31 * 60)
        self.assertIsNone(self.metadata())

    def test_markers_hold_for_seven_days_and_release_when_item_changes(self):
        cases = (
            ("[objection] acceptance contract is contradictory", "objection", None),
            ("[delivered-by #77] already shipped", "delivered", 77),
        )
        for detail, marker, delivered_pr in cases:
            with self.subTest(marker=marker, age="31 minutes"):
                self.write_decline(f"declined this iteration: {detail}", 31 * 60)
                metadata = self.metadata()
                self.assertFalse(metadata["transient"])
                self.assertEqual(metadata["marker"], marker)
                self.assertEqual(metadata["delivered_pr"], delivered_pr)
            with self.subTest(marker=marker, age="6 days"):
                self.write_decline(f"declined this iteration: {detail}", 6 * 24 * 3600)
                self.assertIsNotNone(self.metadata())
            with self.subTest(marker=marker, state="item changed"):
                changed = dict(self.record, updatedAt="2026-11-18T00:00:01Z")
                self.assertIsNone(self.metadata(changed))

    def test_incident_reasons_are_temporary(self):
        reasons = (
            "blocked: Codex quiet-window hook stalled every post-review operation",
            "IRShapeDebug against-control verifier stalled past its 60-second per-run bound",
            "macOS preflight passed, but exact base nine-yaw sweep produced no terminal outpu",
        )
        for reason in reasons:
            with self.subTest(reason=reason):
                self.write_decline(f"declined this iteration: {reason}", 10 * 60)
                self.assertTrue(self.metadata()["transient"])
                self.write_decline(f"declined this iteration: {reason}", 31 * 60)
                self.assertIsNone(self.metadata())

    def test_marked_sandbox_text_is_not_runtime_scoped(self):
        self.write_decline(
            "declined this iteration: [objection] sandbox policy contradicts the item",
            31 * 60,
            runtime="codex",
        )
        metadata = self.metadata()
        self.assertEqual(metadata["class"], "objection")
        self.assertEqual(metadata["reason"], "sandbox policy contradicts the item")


if __name__ == "__main__":
    unittest.main()
