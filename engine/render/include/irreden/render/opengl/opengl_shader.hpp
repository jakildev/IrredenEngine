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
// Defined in src/opengl/opengl_shader.cpp, which builds only under the OpenGL
// backend — a caller outside that backend must guard on `IR_GRAPHICS_OPENGL`.
std::string resolveShaderIncludes(const std::string &source, const std::filesystem::path &baseDir);

} // namespace detail

} // namespace IRRender
