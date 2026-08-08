#include <gtest/gtest.h>

// The GLSL `#include` resolver lives in the OpenGL backend TU
// (engine/render/src/opengl/opengl_shader.cpp), which compiles only under
// IRREDEN_GRAPHICS_BACKEND=OPENGL. Under the Metal backend this file is an
// empty translation unit — the same guard shape gpu_compute_dispatch_test.cpp
// uses for its backend-specific halves.
#if defined(IR_GRAPHICS_OPENGL)

#include <irreden/render/opengl/opengl_shader.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Contract tests for IRRender::detail::resolveShaderIncludes (#2514): the
// resolver expands nested `#include "…"` recursively, and pastes each file at
// most once per canonical path.
//
// Neither property is exercised by compiling the shader tree: every shipped
// wrapper still lists its full prerequisite chain, so nothing under
// engine/render/src/shaders/ currently depends on recursive resolution. These
// tests are the only guard on it.
//
// Pure string/filesystem logic — no GL context is created or needed.

namespace {

using IRRender::detail::resolveShaderIncludes;

class GlslIncludeResolver : public ::testing::Test {
  protected:
    void SetUp() override {
        // Per-test directory: gtest_discover_tests registers each test with
        // CTest individually, so a shared fixture dir would race under
        // `ctest -j`.
        m_dir = std::filesystem::temp_directory_path() /
                (std::string{"ir_glsl_include_resolver_test_"} +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(m_dir);
        std::filesystem::create_directories(m_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(m_dir);
    }

    // Writes `contents` to `relativePath` under the fixture dir, creating any
    // intermediate directories so nested-include cases can use subdirs.
    void writeShader(const std::string &relativePath, const std::string &contents) {
        const std::filesystem::path path = m_dir / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << contents;
    }

    std::string resolve(const std::string &source) {
        return resolveShaderIncludes(source, m_dir);
    }

    // Resolves a source read from a real file, passing its path so the visited
    // set is seeded with the top-level file — the shape the shader pipeline
    // uses, where the caller holds `ShaderStage::getFilepath()`.
    std::string resolveFile(const std::string &relativePath) {
        const std::filesystem::path path = m_dir / relativePath;
        std::ifstream file(path);
        std::ostringstream contents;
        contents << file.rdbuf();
        return resolveShaderIncludes(contents.str(), path.parent_path(), path);
    }

    static int countOccurrences(const std::string &haystack, const std::string &needle) {
        int count = 0;
        for (std::size_t at = haystack.find(needle); at != std::string::npos;
             at = haystack.find(needle, at + needle.size())) {
            ++count;
        }
        return count;
    }

    std::filesystem::path m_dir;
};

// Baseline: a source with no directives is passed through unchanged.
TEST_F(GlslIncludeResolver, PassesNonDirectiveLinesThrough) {
    const std::string resolved = resolve("#version 450 core\nvoid main() {}\n");
    EXPECT_EQ(resolved, "#version 450 core\nvoid main() {}\n");
}

// A fragment may self-include its prerequisites: the nested directive is
// expanded, not emitted verbatim. An unexpanded `#include` reaching the driver
// is a GLSL syntax error.
TEST_F(GlslIncludeResolver, ExpandsIncludeNestedInsideAnIncludedFile) {
    writeShader("prerequisite.glsl", "int prerequisiteSymbol = 1;\n");
    writeShader("fragment.glsl", "#include \"prerequisite.glsl\"\nint fragmentSymbol = 2;\n");

    const std::string resolved = resolve("#include \"fragment.glsl\"\n");

    EXPECT_EQ(countOccurrences(resolved, "int prerequisiteSymbol = 1;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "int fragmentSymbol = 2;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "#include"), 0) << "nested directive left unexpanded";
}

// A nested include resolves against the directory of the file that pulled it
// in, not the top-level shader dir — the resolver recurses with
// `includePath.parent_path()` as the new base.
TEST_F(GlslIncludeResolver, ResolvesNestedIncludeRelativeToItsIncluder) {
    writeShader("sub/sibling.glsl", "int siblingSymbol = 1;\n");
    writeShader("sub/entry.glsl", "#include \"sibling.glsl\"\nint entrySymbol = 2;\n");

    const std::string resolved = resolve("#include \"sub/entry.glsl\"\n");

    EXPECT_EQ(countOccurrences(resolved, "int siblingSymbol = 1;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "int entrySymbol = 2;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "#include"), 0);
}

// The visited set spans the whole resolve, not one branch: a header reached
// from two different branches is pasted once. GLSL has no `#pragma once`, so a
// second paste would be a redefinition error at compile time.
TEST_F(GlslIncludeResolver, PastesSharedPrerequisiteOnlyOnce) {
    writeShader("shared.glsl", "int sharedSymbol = 1;\n");
    writeShader("branch_a.glsl", "#include \"shared.glsl\"\nint branchASymbol = 2;\n");
    writeShader("branch_b.glsl", "#include \"shared.glsl\"\nint branchBSymbol = 3;\n");

    const std::string resolved =
        resolve("#include \"branch_a.glsl\"\n#include \"branch_b.glsl\"\n");

    EXPECT_EQ(countOccurrences(resolved, "int sharedSymbol = 1;"), 1)
        << "shared prerequisite pasted twice — a GLSL redefinition error";
    EXPECT_EQ(countOccurrences(resolved, "int branchASymbol = 2;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "int branchBSymbol = 3;"), 1);
}

// The same visited check is the cycle guard: a mutually-recursive pair
// terminates, and each body still lands exactly once. Without it this recurses
// until the stack is exhausted.
TEST_F(GlslIncludeResolver, TerminatesOnCyclicIncludeWithoutDuplicating) {
    writeShader("cycle_a.glsl", "int cycleASymbol = 1;\n#include \"cycle_b.glsl\"\n");
    writeShader("cycle_b.glsl", "int cycleBSymbol = 2;\n#include \"cycle_a.glsl\"\n");

    const std::string resolved = resolve("#include \"cycle_a.glsl\"\n");

    EXPECT_EQ(countOccurrences(resolved, "int cycleASymbol = 1;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "int cycleBSymbol = 2;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "#include"), 0);
}

// A cycle the TOP-LEVEL source participates in. The sibling cycle tests
// (`TerminatesOnCyclicIncludeWithoutDuplicating`, `TerminatesOnSelfInclude`)
// reach their cycle through *included* files only, so the top-level body is
// never a re-entry candidate there; here it is. Seeding the visited set with
// the source's own path drops the re-entry — without the seed the top-level
// body is pasted a second time, a GLSL redefinition error. Metal's twin gets
// this for free by taking a filepath rather than a source string.
TEST_F(GlslIncludeResolver, TerminatesOnCycleThroughTheTopLevelSource) {
    writeShader("top.glsl", "int topSymbol = 1;\n#include \"leaf.glsl\"\n");
    writeShader("leaf.glsl", "int leafSymbol = 2;\n#include \"top.glsl\"\n");

    const std::string resolved = resolveFile("top.glsl");

    EXPECT_EQ(countOccurrences(resolved, "int topSymbol = 1;"), 1)
        << "top-level body pasted twice — a GLSL redefinition error";
    EXPECT_EQ(countOccurrences(resolved, "int leafSymbol = 2;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "#include"), 0);
}

// Degenerate cycle: a file that includes itself. Same guard, one paste.
TEST_F(GlslIncludeResolver, TerminatesOnSelfInclude) {
    writeShader("self.glsl", "int selfSymbol = 1;\n#include \"self.glsl\"\n");

    const std::string resolved = resolve("#include \"self.glsl\"\n");

    EXPECT_EQ(countOccurrences(resolved, "int selfSymbol = 1;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "#include"), 0);
}

// Indented directives are recognized (the resolver trims leading whitespace
// before matching), and a repeat of an already-pasted include contributes
// nothing rather than re-pasting.
TEST_F(GlslIncludeResolver, RecognizesIndentedDirectiveAndSkipsRepeats) {
    writeShader("once.glsl", "int onceSymbol = 1;\n");

    const std::string resolved = resolve("    #include \"once.glsl\"\n#include \"once.glsl\"\n");

    EXPECT_EQ(countOccurrences(resolved, "int onceSymbol = 1;"), 1);
    EXPECT_EQ(countOccurrences(resolved, "#include"), 0);
}

// A wrapper that includes A then B, where B also self-includes A, resolves
// byte-identically to the same wrapper against a B that does not
// self-include A. This is what makes it safe for a shared fragment to
// self-include its own prerequisite — every existing wrapper already
// includes that prerequisite first, so the canonical path is already in the
// visited set when the fragment's own self-include is reached, and the
// redundant directive is dropped as a no-op. See #2811.
TEST_F(GlslIncludeResolver, SelfIncludedPrerequisiteIsANoOpWhenWrapperAlreadyIncludesIt) {
    writeShader("prerequisite.glsl", "int prerequisiteSymbol = 1;\n");
    writeShader("fragment_without_self_include.glsl", "int fragmentSymbol = 2;\n");
    writeShader(
        "fragment_with_self_include.glsl",
        "#include \"prerequisite.glsl\"\nint fragmentSymbol = 2;\n"
    );

    const std::string withoutSelfInclude = resolve(
        "#include \"prerequisite.glsl\"\n#include \"fragment_without_self_include.glsl\"\n"
    );
    const std::string withSelfInclude =
        resolve("#include \"prerequisite.glsl\"\n#include \"fragment_with_self_include.glsl\"\n");

    EXPECT_EQ(withSelfInclude, withoutSelfInclude);
    EXPECT_EQ(countOccurrences(withSelfInclude, "int prerequisiteSymbol = 1;"), 1);
}

} // namespace

#endif // IR_GRAPHICS_OPENGL
