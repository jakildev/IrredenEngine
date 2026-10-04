#!/usr/bin/env python3
"""Execute paired-ref orchestration against local commits and synthetic matrices."""

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

PERF = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PERF))
import ci_pair  # noqa: E402

WORKFLOW = PERF.parents[1] / ".github/workflows/perf-gate.yml"


class PairTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        self.git("init", "-q")
        self.git("config", "user.name", "fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        (self.root / ".gitignore").write_text("build/\nsave_files/\n")
        self.base = self.commit("base")
        self.head = self.commit("head")
        self.calls = []
        self.fail_base = False
        self.wrong_ref = False
        self.real_checked = ci_pair.checked
        self.environment = patch.dict(os.environ, {"GITHUB_ACTIONS": "true",
                                                   "RUNNER_ENVIRONMENT": "github-hosted"})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.command_patch = patch.object(ci_pair, "checked", self.command)
        self.command_patch.start()
        self.addCleanup(self.command_patch.stop)
        self.build()
        self.first = self.matrix("ci")

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.root, text=True,
                                       stderr=subprocess.DEVNULL).strip()

    def commit(self, content):
        (self.root / "source").write_text(content)
        self.git("add", ".gitignore", "source")
        self.git("commit", "-qm", content)
        return self.git("rev-parse", "HEAD")

    def build(self):
        binary = self.root / "build/creations/demos/perf_grid/IRPerfGrid"
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_text((self.root / "source").read_text())
        (binary.parent / "shaders").mkdir(exist_ok=True)
        (binary.parent / "shaders/a.glsl").write_text(binary.read_text())
        for name in ("shaders", "configs"):
            directory = binary.parent / name
            directory.mkdir(exist_ok=True)
            if self.git("rev-parse", "HEAD") == self.head:
                (directory / "head-only.asset").write_text("head asset")
            else:
                self.assertFalse((directory / "head-only.asset").exists())

    def matrix(self, label):
        sha = self.head if self.wrong_ref else self.git("rev-parse", "HEAD")
        directory = self.root / "save_files/perf" / (sha[:9] + "-" + label)
        directory.mkdir(parents=True)
        (directory / "manifest.json").write_text(json.dumps({
            "git_sha": sha[:9], "git_dirty": False,
            "cells": [{"id": "fixture", "status": "ok", "exit_status": 0,
                       "report": "fixture.txt"}]}))
        (directory / "fixture.txt").write_text(
            "Frame time: avg=10ms p50=10ms p95=10ms p99=10ms min=10ms max=10ms\n"
            "Steady frame time (first 15 of 60 frames excluded): "
            "avg=10ms p50=10ms p95=10ms p99=10ms min=10ms max=10ms\n")
        return directory

    def command(self, command, root, *, capture=False):
        self.calls.append(command)
        if command[0] == "cmake":
            if self.fail_base and self.git("rev-parse", "HEAD") == self.base:
                raise subprocess.CalledProcessError(1, command)
            if command[1] == "--build":
                self.build()
            return None
        if command[0] == "ir-perf-grid":
            self.matrix(command[-1])
            return None
        return self.real_checked(command, root, capture=capture)

    def test_measures_both_refs_and_returns_to_head(self):
        ci_pair.run_pair(self.root, self.base, self.first)
        output = self.root / "save_files/perf/paired"
        records = json.loads((output / "provenance.json").read_text())
        self.assertEqual([r["sha"] for r in records], [self.head, self.base, self.head])
        self.assertEqual(records[0]["binary_sha256"], records[2]["binary_sha256"])
        self.assertNotEqual(records[0]["shader_sha256"], records[1]["shader_sha256"])
        self.assertEqual(len(list(output.glob("*.md"))), 3)
        self.assertEqual(self.git("rev-parse", "HEAD"), self.head)
        self.assertEqual(self.git("status", "--porcelain"), "")

    def test_build_failure_restores_head_and_retains_provenance(self):
        self.fail_base = True
        with self.assertRaises(subprocess.CalledProcessError):
            ci_pair.run_pair(self.root, self.base, self.first)
        self.assertEqual(self.git("rev-parse", "HEAD"), self.head)
        self.assertTrue((self.root / "save_files/perf/paired/provenance.json").exists())

    def test_wrong_measured_ref_is_rejected(self):
        self.wrong_ref = True
        with self.assertRaisesRegex(ValueError, "clean measured commit"):
            ci_pair.run_pair(self.root, self.base, self.first)
        self.assertEqual(self.git("rev-parse", "HEAD"), self.head)

    def test_missing_report_is_rejected_before_switching(self):
        (self.first / "fixture.txt").unlink()
        with self.assertRaisesRegex(ValueError, "missing measurements"):
            ci_pair.run_pair(self.root, self.base, self.first)
        self.assertFalse(any(c[:2] == ["git", "switch"] for c in self.calls))

    def test_local_or_dirty_checkout_is_rejected(self):
        with patch.dict(os.environ, {"RUNNER_ENVIRONMENT": "self-hosted"}):
            with self.assertRaisesRegex(ValueError, "disposable"):
                ci_pair.run_pair(self.root, self.base, self.first)
        (self.root / "source").write_text("uncommitted")
        with self.assertRaisesRegex(ValueError, "clean checkout"):
            ci_pair.run_pair(self.root, self.base, self.first)
        self.assertFalse(any(c[0] == "cmake" for c in self.calls))


class WorkflowTest(unittest.TestCase):
    def test_dispatch_cannot_publish_diagnostic_measurements(self):
        workflow = WORKFLOW.read_text()
        marker = "      - name: Update baseline on the perf-baseline branch\n"
        condition = re.search(r"        if: (.+)", workflow.split(marker)[1])[1]
        for event, ref, expected in (("push", "", True), ("pull_request", "", False),
                                     ("workflow_dispatch", "", True),
                                     ("workflow_dispatch", "abc123", False)):
            expression = condition.replace("github.event_name", repr(event)).replace(
                "inputs.diagnostic_base", repr(ref)).replace("||", "or").replace("&&", "and")
            self.assertEqual(eval(expression, {"__builtins__": {}}), expected)
        self.assertIn("PAIR_BASE_REF: ${{ inputs.diagnostic_base }}", workflow)
        self.assertIn("run: python3 scripts/perf/ci_pair.py", workflow)


if __name__ == "__main__":
    unittest.main()
