#pragma once

#include <filesystem>
#include <string>

namespace IRRender {

namespace detail {

// Expands `#include "file.glsl"` directives in a GLSL source string against
// `baseDir`, recursively, at most once per canonical path (#2514) — the GLSL
// twin of Metal's `loadAndPreprocessMetalSource` (metal_pipeline.cpp). A
// nested include resolves relative to the directory of the file that pulled
// it in, not the top-level shader dir.
//
// The visited set spans the whole resolve rather than one branch, so a header
// reached from two branches is pasted once: GLSL has no `#pragma once`, and a
// second paste is a redefinition error. A cyclic chain (A includes B includes
// A) terminates on the same check instead of recursing forever.
//
// `sourceFilepath` is the file `source` was read from. Passing it seeds the
// visited set with that file, so a cycle the top-level participates in
// (`c_foo.glsl` includes `ir_bar.glsl` includes `c_foo.glsl`) drops the
// re-entry instead of pasting the top-level body a second time — matching the
// Metal twin, which takes a filepath and is seeded at entry by construction.
// Callers that resolve a source read from disk must pass it; the empty default
// is for a synthetic source with no backing file.
//
// Defined in src/opengl/opengl_shader.cpp, which builds only under the OpenGL
// backend — a caller outside that backend must guard on `IR_GRAPHICS_OPENGL`.
std::string resolveShaderIncludes(
    const std::string &source,
    const std::filesystem::path &baseDir,
    const std::filesystem::path &sourceFilepath = {}
);

} // namespace detail

} // namespace IRRender
