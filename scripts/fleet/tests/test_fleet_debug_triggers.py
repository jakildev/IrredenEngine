"""Tool-level tests for `fleet-debug triggers` (#2185).

Runs the real bash script as a subprocess with HOME pointed at a fabricated
~/.fleet/state tree, so the inline python heredoc reads the fixture instead of
the live fleet state. Asserts: exit 0, one output line per role covering every
column, graceful degradation on missing/torn projection files, and — the
load-bearing property — that the dump writes NOTHING under the state tree.
"""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-debug"


def _snapshot(root):
    # Map every file under root to (bytes, mtime_ns) so the test can prove the
    # tool is read-only — a stray write, touch, or temp file all show up here.
    out = {}
    for path in sorted(root.rglob("*")):
        if path.is_file():
            st = path.stat()
            out[str(path.relative_to(root))] = (path.read_bytes(), st.st_mtime_ns)
    return out


class FleetDebugTriggers(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.home = Path(self._tmp.name)
        self.state = self.home / ".fleet" / "state"
        self.triggers = self.state / "triggers"
        self.seen = self.state / "seen-hashes"
        self.projections = self.state / "projections"
        for d in (self.triggers, self.seen, self.projections):
            d.mkdir(parents=True)

    def tearDown(self):
        self._tmp.cleanup()

    def _run(self):
        env = dict(os.environ)
        env["HOME"] = str(self.home)
        return subprocess.run(
            ["bash", str(_SCRIPT), "triggers"],
            capture_output=True, text=True, env=env, timeout=30,
        )

    def _write(self, path, text):
        path.write_text(text)

    def _assert_suppressed(self, line, value):
        # The two-space column separator is load-bearing here: "suppressed=yes"
        # is a substring of "empty-suppressed=yes", so a bare assertIn on the
        # short form matches either spelling and pins nothing. Anchoring on the
        # separator, plus asserting the `empty-` form is absent, is what makes
        # this discriminate — the column must NOT carry the `empty-` prefix,
        # since for per-kind roles (#2700) a suppression does not imply an
        # empty projection.
        self.assertIn(f"  suppressed={value}", line)
        self.assertNotIn("empty-suppressed=", line)

    def test_full_state_reports_every_role(self):
        # A pending trigger with content, a suppressed role, a dict projection.
        (self.triggers / "merger").write_text("llm\n")
        self._write(self.seen / "worker", "caa618b4c02edc35\n")
        self._write(self.seen / "merger", "69f1c07810ac0000\n")
        self._write(self.seen / "worker.empty-suppressed", "caa618b4c02edc35\n")
        self._write(
            self.projections / "worker.json",
            json.dumps({"tasks_open": [1, 2, 3], "needs_plan": [],
                        "feedback_prs": [9], "generated_at": "2026-07-02T06:00:00Z"}),
        )
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = {ln.split()[0]: ln for ln in r.stdout.splitlines() if ln.strip()}
        # All 8 canonical roles present.
        for role in ("worker", "sonnet-reviewer", "opus-reviewer", "merger",
                     "smoke-worker", "epic-steward", "queue-manager",
                     "queue-manager-ingest"):
            self.assertIn(role, lines)
        # worker: suppressed marker present, 4 projection items, generated_at.
        self._assert_suppressed(lines["worker"], "yes")
        self.assertIn("4 items @ 2026-07-02T06:00:00Z", lines["worker"])
        self.assertIn("caa618b4c02e", lines["worker"])
        # merger: pending trigger with content, no suppression.
        self.assertIn('pending(', lines["merger"])
        self.assertIn('"llm"', lines["merger"])
        self._assert_suppressed(lines["merger"], "no")
        # queue-manager(-ingest): never get triggers/markers -> n/a.
        self._assert_suppressed(lines["queue-manager"], "n/a")
        self._assert_suppressed(lines["queue-manager-ingest"], "n/a")

    def test_missing_and_torn_projections_degrade(self):
        # worker projection absent; sonnet-reviewer projection is torn JSON.
        self._write(self.projections / "sonnet-reviewer.json", "{ this is not json")
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = {ln.split()[0]: ln for ln in r.stdout.splitlines() if ln.strip()}
        self.assertIn("proj=-", lines["worker"])            # missing -> placeholder
        self.assertIn("proj=unreadable", lines["sonnet-reviewer"])
        self.assertIn("trigger=-", lines["worker"])          # missing trigger -> "-"
        self.assertIn("seen=-", lines["worker"])             # missing seen -> "-"

    def test_empty_state_tree_exits_zero(self):
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        # Still one line per canonical role even with an empty state tree.
        roles = [ln.split()[0] for ln in r.stdout.splitlines() if ln.strip()]
        self.assertEqual(len(roles), 8)

    def test_discovers_unlisted_role(self):
        # A future role's files should surface rather than hide.
        self._write(self.seen / "future-role", "deadbeefdeadbeef\n")
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(any(ln.startswith("future-role") for ln in r.stdout.splitlines()))

    def test_marker_file_is_not_listed_as_a_role(self):
        # `worker.empty-suppressed` must feed the worker's column, never appear
        # as its own role line. The on-disk suffix carries the `empty-` prefix
        # that the column label does not — a deliberate asymmetry, so this also
        # pins that the file name is spelled `.empty-suppressed` while the
        # column it feeds reads `suppressed=`.
        self._write(self.seen / "worker.empty-suppressed", "abc\n")
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        roles = [ln.split()[0] for ln in r.stdout.splitlines() if ln.strip()]
        self.assertNotIn("worker.empty-suppressed", roles)
        lines = {ln.split()[0]: ln for ln in r.stdout.splitlines() if ln.strip()}
        self._assert_suppressed(lines["worker"], "yes")

    def test_fmt2_seen_file_reports_per_kind_counts(self):
        # #2700: roles in the scout's PER_KIND_TRIGGER_ROLES write a per-kind
        # payload instead of a bare hash. Collapsing it to one hash would hide
        # which sub-lane moved, which is the whole point of the format.
        self._write(self.seen / "worker", json.dumps({
            "fmt": 2,
            "kinds": {
                "task": ["a" * 16, "b" * 16],
                "pr": ["c" * 16],
                "needs_plan": [],
            },
        }) + "\n")
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = {ln.split()[0]: ln for ln in r.stdout.splitlines() if ln.strip()}
        self.assertIn("fmt2[needs_plan=0,pr=1,task=2]", lines["worker"])
        # The raw JSON must not leak into the column.
        self.assertNotIn('"fmt"', lines["worker"])

    def test_fmt2_empty_kinds_reports_empty(self):
        self._write(self.seen / "worker",
                    json.dumps({"fmt": 2, "kinds": {}}) + "\n")
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = {ln.split()[0]: ln for ln in r.stdout.splitlines() if ln.strip()}
        self.assertIn("fmt2[empty]", lines["worker"])

    def test_legacy_bare_hash_still_reports_truncated_hash(self):
        # The other roles keep the bare-hash format; both must render.
        self._write(self.seen / "merger", "69f1c07810ac0000\n")
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = {ln.split()[0]: ln for ln in r.stdout.splitlines() if ln.strip()}
        self.assertIn("seen=69f1c07810ac(", lines["merger"])

    def test_unparseable_json_seen_file_degrades(self):
        # Starts with "{" but is not valid JSON — must not raise.
        self._write(self.seen / "worker", "{ torn payload\n")
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = {ln.split()[0]: ln for ln in r.stdout.splitlines() if ln.strip()}
        self.assertIn("worker", lines)

    def test_read_only(self):
        (self.triggers / "worker").write_text("\n")
        self._write(self.seen / "worker", "caa618b4c02edc35\n")
        self._write(self.seen / "worker.empty-suppressed", "caa618b4c02edc35\n")
        self._write(
            self.projections / "merger.json",
            json.dumps({"prs": [1, 2], "generated_at": "2026-07-02T06:00:00Z"}),
        )
        before = _snapshot(self.state)
        r = self._run()
        self.assertEqual(r.returncode, 0, r.stderr)
        after = _snapshot(self.state)
        self.assertEqual(before, after, "fleet-debug triggers mutated the state tree")


if __name__ == "__main__":
    unittest.main()
