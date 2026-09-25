"""Hermetic tests for shader-compile-verify.py: program selection, include
resolution, nonce placement and the host gate. No GL context is created; the
timing itself runs only on the native-Windows host.

Import the dashed-name script via importlib, matching test_render_verify.py.
"""
import importlib.machinery
import importlib.util
import io
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest.mock import patch

_SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_SCRIPTS))


def _load(mod_name: str, file_name: str):
    loader = importlib.machinery.SourceFileLoader(mod_name, str(_SCRIPTS / file_name))
    spec = importlib.util.spec_from_loader(mod_name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    loader.exec_module(mod)
    return mod


_scv = _load("shader_compile_verify", "shader-compile-verify.py")


def _write(root: Path, name: str, text: str) -> Path:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8"))
    return path


class ProgramSelectionTest(unittest.TestCase):
    def test_top_level_programs_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("c_kernel.glsl", "v_quad.glsl", "f_quad.glsl", "ir_common.glsl",
                         "c_kernel_body.glsl", "notes.md", "metal/c_kernel.glsl"):
                _write(root, name, "#version 450 core\n")
            self.assertEqual([p.name for p in _scv.select_programs(root)],
                             ["c_kernel.glsl", "f_quad.glsl", "v_quad.glsl"])

    def test_stage_from_prefix(self):
        self.assertEqual(_scv.stage_for("c_bake.glsl"), "comp")
        self.assertEqual(_scv.stage_for("v_quad.glsl"), "vert")
        self.assertEqual(_scv.stage_for("f_quad.glsl"), "frag")
        self.assertIsNone(_scv.stage_for("ir_common.glsl"))

    def test_named_files_must_be_programs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write(root, "c_kernel.glsl", "#version 450 core\n")
            _write(root, "ir_common.glsl", "")
            self.assertEqual(_scv._resolve_targets(["c_kernel.glsl"], root),
                             [root / "c_kernel.glsl"])
            with self.assertRaisesRegex(ValueError, "not a c_/v_/f_ program"):
                _scv._resolve_targets(["ir_common.glsl"], root)
            with self.assertRaisesRegex(ValueError, "no such shader"):
                _scv._resolve_targets(["c_missing.glsl"], root)


class IncludeResolutionTest(unittest.TestCase):
    def test_first_include_of_a_canonical_path_wins(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write(root, "ir_a.glsl", "A\n")
            _write(root, "sub/ir_b.glsl", '#include "../ir_a.glsl"\nB\n')
            main = _write(root, "c_main.glsl",
                          '#version 450 core\n#include "ir_a.glsl"\n'
                          '  #include "sub/ir_b.glsl"\n#include "ir_a.glsl"\n'
                          '#include "c_main.glsl"\nmain\n')
            self.assertEqual(_scv.resolve_includes(main),
                             "#version 450 core\nA\n\nB\n\nmain\n")

    def test_line_without_trailing_newline_is_kept(self):
        with tempfile.TemporaryDirectory() as temporary:
            main = _write(Path(temporary), "c_main.glsl", "#version 450 core\nlast")
            self.assertEqual(_scv.resolve_includes(main), "#version 450 core\nlast\n")

    def test_unterminated_include_passes_through(self):
        with tempfile.TemporaryDirectory() as temporary:
            main = _write(Path(temporary), "c_main.glsl", '#include "ir_a.glsl\n')
            self.assertEqual(_scv.resolve_includes(main), '#include "ir_a.glsl\n')


class NonceTest(unittest.TestCase):
    def test_nonce_follows_version(self):
        source = "// header\n#version 450 core\nvoid main() {}\n"
        self.assertEqual(_scv.inject_nonce(source, "abc").split("\n")[:3],
                         ["// header", "#version 450 core", "const float _nonce_abc = 0.0;"])

    def test_missing_version_is_an_error(self):
        with self.assertRaises(ValueError):
            _scv.inject_nonce("void main() {}\n", "abc")


class HostGateTest(unittest.TestCase):
    def test_non_windows_host_exits_2(self):
        stderr = io.StringIO()
        with patch.object(sys, "platform", "linux"), redirect_stderr(stderr):
            self.assertEqual(_scv.main([]), 2)
        self.assertIn("native Windows only", stderr.getvalue())


class ShaderTreeContractTest(unittest.TestCase):
    """The real tree: every program resolves and takes a nonce. A compile-time-constant
    source-face header index is the construct that costs NVIDIA minutes per cold link.
    An image-typed function parameter has no portable form: NVIDIA rejects imageLoad on
    one without a format qualifier, and Mesa rejects a format qualifier on a parameter."""

    def test_every_program_resolves(self):
        programs = _scv.select_programs(_scv.DEFAULT_SHADER_DIR)
        self.assertGreater(len(programs), 40)
        header_constant_index = re.compile(r"\[\s*kSourceFaceHeaderOffset\s*\]")
        image_parameter = re.compile(r"\b[iu]?image(?:[123]D|Cube|2DRect|Buffer)\w*\s+\w+\s*[,)]")
        include_directive = re.compile(r'^[ \t]*#include "[^"]*"', re.MULTILINE)
        for path in programs:
            with self.subTest(program=path.name):
                self.assertIsNotNone(_scv.stage_for(path.name))
                source = _scv.resolve_includes(path)
                self.assertIsNone(include_directive.search(source))
                _scv.inject_nonce(source, "0")
                self.assertIsNone(header_constant_index.search(source))
                self.assertIsNone(image_parameter.search(source))


if __name__ == "__main__":
    unittest.main()
