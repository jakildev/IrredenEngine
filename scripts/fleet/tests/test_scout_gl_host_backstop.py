"""Tests for the scout's `needs_gl_host` body backstop (#1969, #2704).

`fetch_task_queue` projects `needs_gl_host` from the `fleet:needs-gl-host`
label OR, when the filer forgot it, from the body via
`_body_requires_gl_host`. That backstop shipped untested; these cover both
of its signals and — more importantly — its precision contract.

The contract is asymmetric on purpose: a MISS just reproduces the pre-#1969
churn (a Metal pane claims, can't build, releases), while a FALSE POSITIVE
wrongly skips a mac-runnable task and can starve the macOS lane outright.
So every negative case below is load-bearing, not filler.
"""
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

_SCRIPT = Path(__file__).parent.parent / "fleet-state-scout"
_loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_state_scout", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)

requires_gl_host = _mod._body_requires_gl_host


class TestProseForms(unittest.TestCase):
    """#1969: an explicit host-requirement declaration."""

    def test_verb_form(self):
        for body in (
            "This must run on a Linux host to refresh the references.",
            "It must be run on an OpenGL host.",
            "The harness only runs on a Windows host.",
            "This only builds on a linux-debug host.",
        ):
            self.assertTrue(requires_gl_host(body), body)

    def test_adjective_form(self):
        for body in (
            "It is GL-host only and cannot be validated on a macOS/Metal host.",
            "GL host only.",
            "This one is opengl-host-only work.",
        ):
            self.assertTrue(requires_gl_host(body), body)

    def test_passing_mention_does_not_trip(self):
        # Precision: a mention is not a declaration.
        for body in (
            "Tested on a Linux host, but the fix is backend-agnostic.",
            "The OpenGL backend renders this correctly already.",
            "Reproduced on Linux; the Metal path is unaffected.",
        ):
            self.assertFalse(requires_gl_host(body), body)

    def test_macos_declaration_never_self_gates(self):
        # The OS alternation is linux/windows/opengl, so a mac-only task is
        # never gated off its own host.
        self.assertFalse(requires_gl_host("This must run on a macOS host."))


class TestGlOnlySourcePaths(unittest.TestCase):
    """#2704: the body names a file CMake excludes from the Darwin target."""

    def test_path_form(self):
        for body in (
            "Edit `engine/render/src/opengl/opengl_shader.cpp` to fix it.",
            "The bug is in engine/video/src/opengl/video_backend.cpp.",
            "See include/irreden/render/opengl/opengl_types.hpp for the enum.",
            "src/opengl/glad.c needs regenerating.",
        ):
            self.assertTrue(requires_gl_host(body), body)

    def test_bare_basename_form(self):
        for body in (
            "opengl_shader.cpp pastes verbatim with no re-scan.",
            "The declaration lives in opengl_types.hpp.",
            "Touches opengl_render_impl.cpp only.",
        ):
            self.assertTrue(requires_gl_host(body), body)

    def test_issue_2514_body_regression(self):
        # The live miss that motivated #2704: the body cites the GL source file
        # bare, inside prose, and never declares a host requirement.
        body = (
            "The two backends' shader `#include` resolvers disagree: Metal's "
            "runtime preprocessor (`metal_pipeline.cpp::loadAndPreprocessMetal"
            "Source`) is recursive with a visited set, so a Metal fragment can "
            "include its own prerequisites; GLSL's (`opengl_shader.cpp "
            "detail::resolveShaderIncludes`) pastes verbatim with no re-scan."
        )
        self.assertFalse(_mod._GL_HOST_REQUIRED_RE.search(body),
                         "prose form should NOT fire — that is the #2704 miss")
        self.assertTrue(requires_gl_host(body))

    def test_glsl_only_does_not_trip(self):
        # GLSL is driver-compiled at runtime, so a shader-only change still
        # builds on macOS and shader-compile fixes are macOS-verifiable.
        for body in (
            "Fix the swizzle in c_voxel_to_trixel_stage_1_body.glsl.",
            "engine/render/src/shaders/glsl/ir_iso_common.glsl is stale.",
        ):
            self.assertFalse(requires_gl_host(body), body)

    def test_non_source_files_under_an_opengl_dir_do_not_trip(self):
        # The path form pins the same extension set as the basename form, so
        # the `.glsl` exclusion above is structural rather than incidental to
        # today's tree layout (nothing but C/C++ sources happens to sit under
        # an `opengl/` directory right now). A non-source file added there
        # must not trip the backstop — #2709 review nit.
        for body in (
            "engine/render/src/opengl/ir_iso_common.glsl is stale.",
            "Update the notes in engine/render/src/opengl/README.md.",
            "src/opengl/shader_dump.txt is regenerated by the tool.",
        ):
            self.assertFalse(requires_gl_host(body), body)

    def test_extension_match_is_whole_token(self):
        # The trailing \b is load-bearing: without it a longer extension that
        # merely STARTS with a matched one (`.cpp` inside `.cppx`) would slip
        # through and re-open the same hole in a narrower form.
        for body in (
            "engine/render/src/opengl/generated.cppx is machine-written.",
            "opengl_types.hppx is a generated mirror.",
        ):
            self.assertFalse(requires_gl_host(body), body)

    def test_metal_paths_do_not_trip(self):
        # The inverse (Metal-host) gate is a separate design call.
        for body in (
            "Edit engine/render/src/metal/metal_pipeline.cpp.",
            "metal_render_impl.cpp drops the binding.",
            "engine/video/src/metal/video_backend.cpp mirrors it.",
        ):
            self.assertFalse(requires_gl_host(body), body)

    def test_opengl_prose_without_a_filename_does_not_trip(self):
        # Both #2704 forms require a real extension, so the bare word cannot
        # match however it is spelled.
        for body in (
            "Port the analytic scatter coverage to OpenGL.",
            "The opengl backend and the metal backend have drifted.",
            "Covers OpenGL 4.5 compute dispatch.",
        ):
            self.assertFalse(requires_gl_host(body), body)


class TestEmptyBody(unittest.TestCase):

    def test_none_and_empty(self):
        self.assertFalse(requires_gl_host(None))
        self.assertFalse(requires_gl_host(""))


if __name__ == "__main__":
    unittest.main()
