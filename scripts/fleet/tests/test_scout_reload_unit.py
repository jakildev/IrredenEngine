"""Unit tests for fleet-state-scout's self-reload surface.

The scout binds its modules at import, so absent a reload a merged fix to the
script or to anything it imports reaches disk and never memory — including a
watchdog, which sits in the same unloaded image as the bound it watches and so
cannot fire on its own staleness.

`_source_surface()` is derived from `sys.modules` rather than from a
hand-written list, so one group of tests is about that derivation: it must
find the whole import closure and exclude everything the scout merely spawns
as a subprocess. The rest are about the two halves that keep the derivation
honest once the process is running — the closure is frozen at boot
(`_BOOT_SURFACE`), and the question "is there something new on disk to load?"
is put to a fresh process (`_prospective_rev`) rather than to this one's
module table.

Import the script via importlib because it has no .py extension.
"""
import ast
import importlib.machinery
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)


class TestSourceSurface(unittest.TestCase):
    def test_contains_the_script_itself(self):
        names = {p.name for p in _mod._source_surface()}
        self.assertIn("fleet-state-scout", names)

    def test_contains_the_import_closure(self):
        # The five modules imported at load time (fleet-state-scout:54-71).
        # fleet_stack_base and fleet_gh_poll are named individually because
        # they are the two whose merged fixes are known to have landed inert.
        names = {p.name for p in _mod._source_surface()}
        for expected in ("fleet_stack_base.py", "fleet_gh_poll.py",
                         "fleet_blocked_by.py", "fleet_branch_match.py",
                         "fleet_poll_topology.py"):
            self.assertIn(expected, names)

    def test_excludes_subprocess_reloaded_siblings(self):
        # fleet_task_class.py sits in the same directory but is spawned as a
        # subprocess, never imported — it already picks up merged fixes, so a
        # reload on it would be spurious. A surface that globbed the directory
        # would pass every other test here and fail this one.
        names = {p.name for p in _mod._source_surface()}
        self.assertNotIn("fleet_task_class.py", names)

    def test_excludes_stdlib(self):
        # Every entry must live in the script's own directory; a surface that
        # swept all of sys.modules would re-exec on a python upgrade.
        root = Path(_SCRIPT).resolve().parent
        for path in _mod._source_surface():
            self.assertEqual(path.parent, root, f"{path} is outside {root}")

    def test_deterministic_across_calls(self):
        # The tick check compares this tick's hash against the last one, so a
        # surface that reordered (or a hash that varied) between calls would
        # re-exec the daemon every tick.
        self.assertEqual(_mod._source_surface(), _mod._source_surface())
        self.assertEqual(
            _mod._surface_hash(_mod._source_surface()),
            _mod._surface_hash(_mod._source_surface()),
        )

    def test_all_entries_exist(self):
        for path in _mod._source_surface():
            self.assertTrue(path.is_file(), f"{path} is in the surface but absent")


def _late_import():
    """A function-local `import fleet_something` landing after boot.

    Modelled as the module table growing by one sibling entry, which is all
    such an import is to the surface derivation. The file it names is a real,
    compilable sibling, so a surface that picked the entry up would clear a
    syntax gate and go on to exec.
    """
    late = types.ModuleType("fleet_late_import_probe")
    late.__file__ = str(_SCRIPT.parent / "fleet_task_class.py")
    return patch.dict(sys.modules, {"fleet_late_import_probe": late})


class TestBootSurfaceIsFrozen(unittest.TestCase):
    """A lazy import must not be able to move this process's own revision."""

    def setUp(self):
        # The debounce and quiet markers are module state; leaving them set
        # would make a later test's first tick behave like a second one.
        self.addCleanup(setattr, _mod, "_reload_pending_hash",
                        _mod._reload_pending_hash)
        self.addCleanup(setattr, _mod, "_reload_quiet_hash",
                        _mod._reload_quiet_hash)

    def test_a_late_import_does_reach_the_module_table(self):
        # Reachability for the two arms below: without it they would pass on
        # a fixture that never grew the table in the first place.
        with _late_import():
            names = {p.name for p in _mod._source_surface()}
        self.assertIn("fleet_task_class.py", names)

    def test_a_late_import_does_not_move_the_revision(self):
        with _late_import():
            self.assertEqual(_mod._surface_hash(_mod._BOOT_SURFACE),
                             _mod.SOURCE_REV)

    def test_a_late_import_cannot_trigger_a_reload(self):
        # Two ticks, because the debounce requires the same new hash twice. A
        # surface that grew mid-life would exec here — into a process whose
        # table has not run the lazy import yet, which then grows again on
        # its own first call, once per boot until the oscillation cap eats
        # the budget.
        execs = []
        with _late_import(), tempfile.TemporaryDirectory() as d, \
                patch.object(_mod, "STATE_DIR", Path(d)), \
                patch.object(_mod.os, "execv",
                             lambda *a: execs.append(a)):
            _mod.check_source_reload()
            _mod.check_source_reload()
        self.assertEqual(execs, [])


class TestImportsAreTopLevel(unittest.TestCase):
    def test_no_closure_file_imports_a_fleet_module_inside_a_function(self):
        # What makes the frozen boot surface the WHOLE closure. A fleet_*
        # module first imported inside a function is bound after the surface
        # is captured, so nothing watches it and its merged fixes run inert
        # in the one daemon this mechanism exists to keep current. Asserted
        # over every file in the closure, not just the scout: a lazy import
        # one level down is the same hole.
        offenders = set()
        for path in _mod._BOOT_SURFACE:
            tree = ast.parse(Path(path).read_text())
            for scope in ast.walk(tree):
                if not isinstance(scope, (ast.FunctionDef,
                                          ast.AsyncFunctionDef)):
                    continue
                for node in ast.walk(scope):
                    if isinstance(node, ast.Import):
                        names = [alias.name for alias in node.names]
                    elif isinstance(node, ast.ImportFrom):
                        names = [node.module or ""]
                    else:
                        continue
                    if any(n.startswith("fleet_") for n in names):
                        offenders.add(f"{Path(path).name}:{node.lineno}")
        self.assertEqual(sorted(offenders), [])


class TestProspectiveRev(unittest.TestCase):
    def test_matches_a_fresh_boot_of_the_unchanged_image(self):
        self.assertEqual(_mod._prospective_rev(), (_mod.SOURCE_REV, None))

    def test_an_image_that_cannot_import_reports_nothing(self):
        # The probe is the syntax gate as well: an image that dies at import
        # cannot answer, and a None revision is what makes
        # check_source_reload refuse instead of exec'ing into a process that
        # will not come up. The reason is the refusal's whole diagnostic, so
        # an empty one is a silent daemon.
        with tempfile.TemporaryDirectory() as d:
            broken = Path(d) / _SCRIPT.name
            broken.write_text("def (\n")
            with patch.object(_mod, "_SELF", broken):
                rev, reason = _mod._prospective_rev()
        self.assertIsNone(rev)
        self.assertIn("SyntaxError", reason)

    def test_a_dropped_module_reports_a_revision_rather_than_refusing(self):
        # The membership failure the probe exists for: the running image
        # holds a module the on-disk one no longer imports. Judged from the
        # old surface, that path reads MISSING and then fails every syntax
        # gate, refusing forever; asked of the new image, it answers with the
        # revision that image would run.
        with tempfile.TemporaryDirectory() as d:
            staged = Path(d)
            for source in _mod._BOOT_SURFACE:
                shutil.copy2(source, staged / Path(source).name)
            scout = staged / _SCRIPT.name
            text = scout.read_text()
            dropped = "from fleet_stack_base import unsafe_base_reason"
            self.assertIn(dropped, text)
            scout.write_text(text.replace(
                dropped, "def unsafe_base_reason(*_a, **_k): return None"))
            (staged / "fleet_stack_base.py").unlink()
            with patch.object(_mod, "_SELF", scout):
                rev, reason = _mod._prospective_rev()
        self.assertIsNone(reason)
        self.assertNotEqual(rev, _mod.SOURCE_REV)


class TestSurfaceHash(unittest.TestCase):
    def test_changes_with_contents(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "a"
            f.write_text("one")
            before = _mod._surface_hash([f])
            f.write_text("two")
            self.assertNotEqual(before, _mod._surface_hash([f]))

    def test_missing_entry_is_distinct_from_an_absent_one(self):
        # Same property the bash helper's T3 asserts: a missing file must hash
        # to a distinct token, not be skipped, or a surface entry appearing or
        # disappearing on disk would register as no change at all.
        with tempfile.TemporaryDirectory() as d:
            present = Path(d) / "a"
            present.write_text("one")
            missing = Path(d) / "gone"
            self.assertNotEqual(
                _mod._surface_hash([present, missing]),
                _mod._surface_hash([present]),
            )

    def test_stable_across_install_locations(self):
        # The aggregate is published in state.json as scout_source_rev, so it
        # is hashed over basenames: the same files staged under a different
        # directory must produce the same revision, or a repo-checkout scout
        # and a ~/bin-symlink scout would never compare equal.
        with tempfile.TemporaryDirectory() as one, tempfile.TemporaryDirectory() as two:
            for root in (one, two):
                (Path(root) / "a").write_text("alpha")
                (Path(root) / "b").write_text("beta")
            self.assertEqual(
                _mod._surface_hash([Path(one) / "a", Path(one) / "b"]),
                _mod._surface_hash([Path(two) / "a", Path(two) / "b"]),
            )


class TestReloadGate(unittest.TestCase):
    def test_permits_max_attempts_then_suppresses(self):
        with tempfile.TemporaryDirectory() as d:
            state = Path(d) / "history"
            results = [_mod._reload_gate(state, 3, 900) for _ in range(4)]
            self.assertEqual(results, [True, True, True, False])

    def test_max_one_permits_one_attempt(self):
        # `max_attempts` is the count PERMITTED, so the documented floor still
        # permits a reload. Under a `<` spelling FLEET_RELOAD_MAX=1 refuses
        # every reload, with no runtime signal distinguishing that from a
        # quiet source surface.
        with tempfile.TemporaryDirectory() as d:
            state = Path(d) / "history"
            results = [_mod._reload_gate(state, 1, 900) for _ in range(2)]
            self.assertEqual(results, [True, False])

    def test_window_drains(self):
        with tempfile.TemporaryDirectory() as d:
            state = Path(d) / "history"
            # Spend the whole budget first, so a live window would refuse the
            # next call — otherwise the pruning this asserts is inert and the
            # test passes with the drain removed.
            for _ in range(3):
                self.assertTrue(_mod._reload_gate(state, 3, 900))
            self.assertFalse(_mod._reload_gate(state, 3, 900))
            # A zero-width window prunes every prior entry, re-arming the cap.
            self.assertTrue(_mod._reload_gate(state, 3, 0))


class TestStateStamp(unittest.TestCase):
    def test_emit_state_stamps_the_source_rev(self):
        # Criterion: a consumer can compare state.json's scout_source_rev
        # against `fleet-state-scout --print-surface` to tell whether the
        # RUNNING scout has loaded what is on disk. Stamped in emit_state
        # rather than build_state so it covers the follower path too, where
        # the overlaid leader bundle would otherwise carry the leader's rev.
        with tempfile.TemporaryDirectory() as d:
            state_file = Path(d) / "state.json"
            with patch.object(_mod, "STATE_FILE", state_file), \
                 patch.object(_mod, "check_state_size", lambda _size: None):
                _mod.emit_state({"generated_at": "2026-01-01T00:00:00Z", "repos": {}})
            written = json.loads(state_file.read_text())
            self.assertEqual(written["scout_source_rev"], _mod.SOURCE_REV)

    def test_source_rev_matches_a_fresh_process(self):
        # SOURCE_REV is captured at import so it describes THIS image. Run the
        # --print-surface arm in a fresh interpreter and require agreement:
        # that equality is the whole diagnostic, and it is what breaks when
        # the running daemon is stale.
        out = subprocess.run(
            [sys.executable, str(_SCRIPT), "--print-surface"],
            capture_output=True, text=True, env={**os.environ},
        )
        self.assertEqual(out.returncode, 0, out.stderr)
        aggregate = [
            line.split("\t")[1]
            for line in out.stdout.splitlines()
            if line.startswith("aggregate\t")
        ]
        self.assertEqual(aggregate, [_mod.SOURCE_REV])


if __name__ == "__main__":
    unittest.main()
