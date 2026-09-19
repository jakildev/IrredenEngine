"""Tests for render-verify.py's top-level manifest ``demo_args`` key.

A demo whose reference set was blessed under a fixture (perf_grid's
``--mode voxel_set --no-overlay …``) must be able to pin that fixture in its
manifest, because a flag that lives only on the CLI gates nothing under
``--all`` (the sweep passes no ``--demo-arg``). These tests prove:

  * a manifest ``demo_args`` list reaches the DEFAULT pass's run command,
    directly after ``--auto-screenshot N``;
  * CLI ``--demo-arg`` values append AFTER the manifest's, so the CLI still
    layers a feature flag on top of the fixture;
  * a manifest without the key produces the pre-key command exactly, so every
    committed manifest that omits it is unaffected;
  * ``extra_runs`` passes keep their own declared ``demo_args`` and do NOT
    inherit the top-level list;
  * a malformed key is rejected loudly rather than silently ignored.

No build, no demo launch: ``fleet-run`` is stubbed at ``subprocess.run`` so the
real ``_run_capture`` composes the command, and ``evaluate_shots`` is stubbed
so no comparator runs. Import the dashed-name subject via importlib, matching
test_render_verify.py.

Positive control: with the ``default_demo_args`` threading in
``_verify_one`` reverted to ``demo_args=args.demo_arg``, the manifest-key
tests here fail and the no-key test still passes.
"""
import argparse
import importlib.machinery
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(
        mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_rv = _load("render_verify_demo_args_subject", "render-verify.py")

TARGET = "IRPerfGrid"
SHOTS = ["fit_grid", "zoom1_origin"]
FIXTURE = ["--mode", "voxel_set", "--no-overlay"]


class _FakeProc:
    returncode = 0
    stdout = ""
    stderr = ""


def _drive(manifest: dict, demo_arg: list[str]) -> list[list[str]]:
    """Run ``_verify_one`` against ``manifest``; return every fleet-run argv.

    The stub stands in for ``subprocess.run``: it records the argv and writes
    the screenshot files the harness then collects, exactly as the demo would.
    """
    commands: list[list[str]] = []

    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        demo_dir = root / "creations" / "demos" / "perf_grid"
        ref_dir = demo_dir / _rv.REFERENCES_SUBDIR / "test-debug"
        ref_dir.mkdir(parents=True)
        (ref_dir / "fit_grid.png").touch()  # satisfies the missing-reference guard
        (demo_dir / _rv.REFERENCES_SUBDIR / _rv.MANIFEST_NAME).write_text(
            json.dumps(manifest))
        exe = root / "build" / "creations" / "demos" / "perf_grid" / TARGET
        exe.parent.mkdir(parents=True)
        exe.touch()

        def fake_run(argv, **kwargs):
            commands.append(list(argv))
            shots_dir = exe.parent / "save_files" / "screenshots"
            shots_dir.mkdir(parents=True, exist_ok=True)
            for i in range(1, len(SHOTS) + 1):
                (shots_dir / f"screenshot_{i:06d}.png").touch()
            return _FakeProc()

        args = argparse.Namespace(
            warmup=None, timeout=60, update_references=False, force=False,
            no_build=True, demo_arg=demo_arg)
        with patch.object(_rv.subprocess, "run", side_effect=fake_run), \
                patch.object(_rv.verify_common, "find_exe", return_value=exe), \
                patch.object(_rv, "evaluate_shots", return_value=[]), \
                redirect_stdout(io.StringIO()):
            tally = _rv._verify_one(
                args=args, worktree=root, build_dir=root / "build",
                backend="test-debug", target=TARGET, demo_dir=demo_dir)
        assert tally["rc"] == 0, tally
    return commands


def _manifest(**extra) -> dict:
    m = {"demo": "perf_grid", "target": TARGET, "shots": SHOTS}
    m.update(extra)
    return m


BARE_DEFAULT = ["fleet-run", "--timeout", "60", TARGET, "--auto-screenshot", "10"]


class DefaultPassDemoArgs(unittest.TestCase):
    def test_manifest_key_reaches_default_pass(self):
        cmds = _drive(_manifest(demo_args=FIXTURE), demo_arg=[])
        self.assertEqual(cmds, [BARE_DEFAULT + FIXTURE])

    def test_cli_demo_arg_appends_after_manifest_args(self):
        cmds = _drive(_manifest(demo_args=FIXTURE), demo_arg=["--occlusion-cull"])
        self.assertEqual(cmds, [BARE_DEFAULT + FIXTURE + ["--occlusion-cull"]])

    def test_manifest_without_key_is_unchanged(self):
        cmds = _drive(_manifest(), demo_arg=[])
        self.assertEqual(cmds, [BARE_DEFAULT])

    def test_manifest_without_key_still_takes_cli_demo_arg(self):
        cmds = _drive(_manifest(), demo_arg=["--occlusion-cull"])
        self.assertEqual(cmds, [BARE_DEFAULT + ["--occlusion-cull"]])

    def test_empty_key_equals_absent_key(self):
        cmds = _drive(_manifest(demo_args=[]), demo_arg=[])
        self.assertEqual(cmds, [BARE_DEFAULT])

    def test_extra_runs_do_not_inherit_top_level_args(self):
        cmds = _drive(_manifest(
            demo_args=FIXTURE,
            extra_runs=[{"name": "cull_on",
                         "demo_args": ["--occlusion-cull"],
                         "shots": SHOTS}]), demo_arg=[])
        self.assertEqual(cmds, [BARE_DEFAULT + FIXTURE,
                                BARE_DEFAULT + ["--occlusion-cull"]])


class ParseDemoArgs(unittest.TestCase):
    def test_absent_is_empty(self):
        self.assertEqual(_rv._parse_demo_args({}), [])

    def test_list_of_strings_passes_through(self):
        self.assertEqual(_rv._parse_demo_args({"demo_args": FIXTURE}), FIXTURE)

    def test_rejects_non_list_and_non_string_items(self):
        for bad in ("--mode voxel_set", {"--mode": "voxel_set"}, ["--zoom", 4]):
            with self.subTest(bad=bad), self.assertRaises(SystemExit):
                _rv._parse_demo_args({"demo_args": bad})


if __name__ == "__main__":
    unittest.main()
